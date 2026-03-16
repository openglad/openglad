#pragma once

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/world_snapshot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class GameWorld;
class walker;

namespace og::sim {

class SimEventLog;

enum class SnapshotCaptureMode : std::uint8_t {
    Consume,
    Peek,
};

enum class EventDeliveryMode : std::uint8_t {
    Drain,
    Skip,
};

enum class SnapshotEntityListKind : std::uint8_t {
    Ob,
    Fx,
    Weap,
};

struct PendingRemovedEntity {
    std::uint32_t entity_id = 0;
    SnapshotEntityListKind list_kind = SnapshotEntityListKind::Ob;
};

// Phase 9 server-side snapshot bookkeeping. GameServer owns one of these per
// connected client and feeds it snapshots from capture_snapshot()/peek_snapshot().
struct PerClientState {
    std::uint32_t last_sent_tick = 0;
    std::unordered_map<std::uint32_t, EntitySnapshotDirtyMask> accumulated_dirty;
    std::vector<std::uint32_t> new_entity_ids;
    std::vector<PendingRemovedEntity> removed_entities;
    std::unordered_map<std::uint32_t, SnapshotEntityListKind> known_entity_lists;
    bool pending_grid_dirty = false;
    bool pending_grid_full_resend = false;
    std::vector<std::uint8_t> pending_full_grid_data;
    std::vector<GridTileSnapshot> pending_grid_dirty_tiles;
};

void reset_client_snapshot_state(PerClientState& client_state) noexcept;
void seed_client_snapshot_baseline(PerClientState& client_state,
                                   const WorldSnapshot& keyframe);
void accumulate_snapshot_for_client(PerClientState& client_state,
                                    const WorldSnapshot& snapshot);
WorldSnapshot consume_delta_snapshot_for_client(PerClientState& client_state,
                                                const WorldSnapshot& snapshot);

struct ConnectedClientState {
    PerClientState snapshot_state;
    std::size_t player_index = 0;
    short team_num = 0;
    walker* control = nullptr;
    bool has_player_binding = false;
    bool has_initial_snapshot = false;
    bool initial_setup_sent = false;
    bool client_ready = false;
    bool force_keyframe = false;
    std::unordered_map<std::uint32_t, PlayerInput> pending_inputs;
    PlayerInput last_known_input = {};
    std::uint32_t last_received_input_tick = 0;
    std::uint64_t last_received_input_ms = 0;
    std::uint64_t last_pause_request_ms = 0;
};

struct PendingExitPromptState {
    std::int16_t destination_level = -1;
    bool withdraw_prompt = false;
    std::string prompt_text;
    std::uint64_t opened_at_ms = 0;
    std::size_t triggering_player_index = static_cast<std::size_t>(-1);
};

struct PendingPauseState {
    std::size_t player_index = static_cast<std::size_t>(-1);
    std::string player_name;
    std::uint64_t opened_at_ms = 0;
};

class GameServer
{
public:
    GameServer(GameWorld& world, SimEventLog& events, ITransport& transport);

    void connect_client(PeerId peer_id);
    void disconnect_client(PeerId peer_id);
    void bind_player(PeerId peer_id,
                     std::size_t player_index,
                     short team_num,
                     walker* control = nullptr);
    void set_player_control(std::size_t player_index, walker* control) noexcept;
    [[nodiscard]] walker* player_control(std::size_t player_index) const noexcept;

    void poll_incoming_messages();
    void send_initial_snapshot(
        PeerId peer_id,
        SnapshotCaptureMode capture_mode = SnapshotCaptureMode::Consume);
    void send_initial_snapshots(
        SnapshotCaptureMode capture_mode = SnapshotCaptureMode::Consume);
    void forward_event_batch(const SimEventBatch& batch);
    void broadcast_current_state(
        SnapshotCaptureMode capture_mode = SnapshotCaptureMode::Consume,
        EventDeliveryMode event_mode = EventDeliveryMode::Drain);
    void step();

    void set_wall_clock_ms_source(std::function<std::uint64_t()> source);
    [[nodiscard]] bool pending_exit_prompt() const noexcept
    {
        return pending_exit_prompt_state_.has_value();
    }
    [[nodiscard]] bool paused() const noexcept
    {
        return pending_pause_state_.has_value();
    }
    [[nodiscard]] std::size_t snapshot_hash_mismatch_count() const noexcept
    {
        return snapshot_hash_mismatch_count_;
    }

    [[nodiscard]] const std::vector<TypedReceivedMessage>&
    last_polled_messages() const noexcept
    {
        return last_polled_messages_;
    }

    std::function<bool(int level_id)> on_level_transition;
    std::function<void()> on_save_sync;
    std::function<bool(int destination)> on_exit_accepted;
    std::function<bool(int destination)> on_withdraw_accepted;

private:
    [[nodiscard]] bool apply_polled_inputs(std::uint32_t expected_tick);
    void process_non_input_messages(std::uint32_t expected_tick);
    void update_timeouts();
    void clear_pending_exit_prompt();
    void clear_pause_state();
    void handle_exit_prompt_response(bool accepted);
    void handle_level_transition(std::int16_t next_level);
    void handle_pause_request(PeerId peer_id);
    void handle_pause_response();
    void prepare_clients_for_loaded_level();
    void rebind_players_for_loaded_level();
    void remember_snapshot_hash(const WorldSnapshot& snapshot);
    [[nodiscard]] std::size_t infer_exit_triggering_player_index() const noexcept;
    void maybe_send_control_change(std::size_t player_index, walker* control);
    void maybe_resolve_world_events(SimEventBatch& batch, WorldSnapshot& snapshot);
    void maybe_broadcast_special_state();
    [[nodiscard]] std::uint64_t now_ms() const;
    [[nodiscard]] InitialSetupMessage build_initial_setup(PeerId peer_id) const;
    void send_initial_setup(PeerId peer_id);
    [[nodiscard]] bool should_send_to_client(const ConnectedClientState& client) const;
    [[nodiscard]] bool should_send_keyframe(const ConnectedClientState& client,
                                            const WorldSnapshot& snapshot) const;
    [[nodiscard]] PlayerInput select_effective_input(ConnectedClientState& client,
                                                     std::uint32_t expected_tick);

    GameWorld& world_;
    SimEventLog& events_;
    ITransport& transport_;
    std::unordered_map<PeerId, ConnectedClientState> clients_;
    std::array<walker*, MAX_PLAYERS> player_controls_ = {};
    std::array<SimInputDebounce, MAX_PLAYERS> player_input_debounce_ = {};
    std::string special_names_[NUM_FAMILIES][NUM_SPECIALS] = {};
    std::vector<TypedReceivedMessage> last_polled_messages_;
    std::optional<PendingExitPromptState> pending_exit_prompt_state_ =
        std::nullopt;
    std::optional<PendingPauseState> pending_pause_state_ = std::nullopt;
    std::unordered_map<std::uint32_t, std::uint32_t> snapshot_hashes_by_tick_;
    std::size_t snapshot_hash_mismatch_count_ = 0;
    std::uint32_t next_sim_event_sequence_ = 1;
    std::uint32_t next_game_flow_event_sequence_ = 1;
    std::optional<PeerId> host_peer_id_ = std::nullopt;
    std::function<std::uint64_t()> wall_clock_ms_source_;
};

} // namespace og::sim
