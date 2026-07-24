#pragma once

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/world_snapshot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
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

// Consecutive snapshot-hash mismatches (no intervening success) from one
// client before the server declares it unrecoverably desynced and disconnects
// it. Every mismatch forces a full keyframe, so an unbounded mismatch stream
// is both an infinite rubber-band for the client and a bandwidth
// amplification lever for a crafted one. A healthy heal clears within a few
// in-flight ticks; 120 (~4 seconds of ticks) is decisively pathological.
inline constexpr std::uint32_t kMaxConsecutiveSnapshotHashMismatches = 120;

// One bound seat of a connected peer: a peer may bind up to MAX_PLAYERS local
// seats (local_slot selects the peer's InputState slot), each mapped to a
// GLOBAL player_index < kMaxGlobalPlayers.
struct BoundPlayer {
    std::uint8_t local_slot = 0;
    std::size_t player_index = 0;
    short team_num = 0;
    walker* control = nullptr;
    bool resume_in_dead_state = false;
    std::unordered_map<std::uint32_t, PlayerInput> pending_inputs = {};
    PlayerInput last_known_input = {};
    std::uint32_t last_received_input_tick = 0;
};

struct ConnectedClientState {
    PerClientState snapshot_state;
    // Keep hashes in send order so same-tick keyframe resends remain strict.
    std::unordered_map<std::uint32_t, std::deque<std::uint32_t>>
        expected_snapshot_hashes;
    std::uint32_t consecutive_hash_mismatches = 0;
    // Bound seats, kept sorted by local_slot (bind_player upserts by slot).
    std::vector<BoundPlayer> bound_players;
    bool has_initial_snapshot = false;
    bool initial_setup_sent = false;
    bool client_ready = false;
    bool allow_initial_keyframe_without_ready = false;
    bool budget_pending_keyframe = false;
    bool force_keyframe = false;
    // Set only by a lobby-to-gameplay handoff for a connected peer that owns
    // no seats. Such a peer may complete a fresh zero-token Hello and receives
    // the same InitialSetup/snapshot stream as a player; arbitrary transport
    // arrivals remain unauthorized reconnect attempts.
    bool spectator_admitted = false;
    SessionToken session_token = kZeroSessionToken;
    std::uint64_t last_received_input_ms = 0;
    std::uint64_t last_pause_request_ms = 0;

    [[nodiscard]] bool has_player_binding() const noexcept
    {
        return !bound_players.empty();
    }
};

struct DisconnectedPlayer {
    SessionToken session_token = kZeroSessionToken;
    // Seat slot on the disconnected peer; every seat of one peer shares the
    // peer's session token, so reconnects rebind all matching entries.
    std::uint8_t local_slot = 0;
    std::size_t player_index = 0;
    short team_num = 0;
    walker* control = nullptr;
    PlayerInput repeated_input = {};
    std::uint64_t grace_started_at_ms = 0;
    std::uint64_t disconnected_at_ms = 0;
    bool ai_control_enabled = false;
    bool was_host = false;
};

struct DisconnectedSpectator {
    SessionToken session_token = kZeroSessionToken;
    std::uint64_t disconnected_at_ms = 0;
    bool was_host = false;
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
    // Admit a lobby-authorized, zero-seat peer to the gameplay stream. This is
    // intentionally distinct from connect_client(): transport discovery alone
    // must not let an unknown zero-token connection become a spectator.
    void connect_spectator(PeerId peer_id);
    void disconnect_client(PeerId peer_id);
    // Upserts the peer's seat keyed by local_slot (defaults keep historic
    // single-seat callers source-compatible: their one binding is slot 0).
    void bind_player(PeerId peer_id,
                     std::size_t player_index,
                     short team_num,
                     walker* control = nullptr,
                     std::uint8_t local_slot = 0);
    void set_player_control(std::size_t player_index, walker* control) noexcept;
    [[nodiscard]] walker* player_control(std::size_t player_index) const noexcept;

    void poll_incoming_messages();
    void poll_incoming_messages(int max_messages);
    [[nodiscard]] int messages_drained_last_call() const noexcept
    {
        return messages_drained_last_call_;
    }
    [[nodiscard]] std::size_t pending_inbound_message_count() const noexcept
    {
        return pending_inbound_messages_.size();
    }
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

    // In return-to-lobby mode a completed level does NOT auto-advance into the
    // next level in the same session. Instead the server persists per-player
    // progress + advances the campaign cursor (the on_level_transition /
    // on_exit_accepted / on_withdraw_accepted hooks finalize-only, without
    // loading the next level) and every display ends its session (terminal
    // EndGame), so each peer's glad_main returns to the team-build menu —
    // matching single-player. The next level is started fresh from the menu over
    // the persisted connection. Off (default) preserves the in-session
    // auto-advance used by tests/local split-screen.
    void set_return_to_lobby_mode(bool enabled) noexcept
    {
        return_to_lobby_mode_ = enabled;
    }
    [[nodiscard]] bool return_to_lobby_mode() const noexcept
    {
        return return_to_lobby_mode_;
    }

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
    [[nodiscard]] const std::vector<DisconnectedPlayer>&
    disconnected_players() const noexcept
    {
        return disconnected_players_;
    }

    std::function<bool(int level_id)> on_level_transition;
    std::function<void()> on_save_sync;
    std::function<bool(int destination)> on_exit_accepted;
    std::function<bool(int destination)> on_withdraw_accepted;

private:
    void synchronize_transport_peers();
    void apply_transport_disconnects();
    void handle_transport_disconnect(
        PeerId peer_id,
        bool close_transport,
        std::optional<std::uint64_t> grace_started_at_ms = std::nullopt);
    [[nodiscard]] bool apply_polled_inputs(std::uint32_t expected_tick);
    void process_non_input_messages(std::uint32_t expected_tick);
    void update_timeouts();
    void update_disconnected_players(std::uint64_t now_ms);
    [[nodiscard]] bool process_disconnected_players(std::uint32_t expected_tick);
    void clear_pending_exit_prompt();
    void clear_pause_state();
    void handle_exit_prompt_response(bool accepted);
    // Withdraw ALL players to the current level (any peer's "quit this mission").
    void abort_level_for_all();
    [[nodiscard]] bool handle_level_transition(std::int16_t next_level);
    // Forward a terminal EndGame (plus palette/redraw) to every display so each
    // peer shows the results screen and returns to the menu. Used by the
    // exit-portal / withdraw paths in return-to-lobby mode (the win path's
    // EndGame is already synthesized by broadcast_current_state).
    void forward_level_end_to_clients(int ending, int next_level);
    // Exit acceptance can finalize and load the next world synchronously,
    // outside the normal tick broadcast. Flush one full old-level snapshot
    // first so every EndGame consumer folds the authoritative score/tick.
    void send_forced_keyframe_to_ready_clients();
    void handle_pause_request(PeerId peer_id);
    void handle_pause_response();
    void handle_hello(PeerId peer_id, const HelloMessage& message);
    void handle_heartbeat(PeerId peer_id);
    void prepare_clients_for_loaded_level();
    void rebind_players_for_loaded_level();
    void remember_snapshot_hash(ConnectedClientState& client,
                                const WorldSnapshot& snapshot,
                                bool expect_client_hash_check = true);
    [[nodiscard]] std::size_t infer_exit_triggering_player_index() const noexcept;
    void maybe_send_control_change(std::size_t player_index, walker* control);
    // CTF respawn reclaim: a revived player walker keeps its user tag, but a
    // player whose control was nulled at death (no fallback body) has no
    // rebind path through sim_find_next_control (which only claims user==-1
    // walkers). During an active CTF match, find the live walker still
    // wearing this player's exact user tag so the caller can rebind it.
    [[nodiscard]] walker* find_ctf_reclaim_control(
        std::size_t player_index) const;
    void maybe_resolve_world_events(SimEventBatch& batch, WorldSnapshot& snapshot);
    void maybe_broadcast_special_state();
    [[nodiscard]] SessionToken allocate_session_token();
    [[nodiscard]] std::uint64_t now_ms() const;
    [[nodiscard]] InitialSetupMessage build_initial_setup(PeerId peer_id) const;
    void send_initial_setup(PeerId peer_id);
    [[nodiscard]] bool should_send_to_client(const ConnectedClientState& client) const;
    [[nodiscard]] bool should_send_keyframe(const ConnectedClientState& client,
                                            const WorldSnapshot& snapshot) const;
    [[nodiscard]] PlayerInput select_effective_input(BoundPlayer& seat,
                                                     std::uint32_t expected_tick);

    GameWorld& world_;
    SimEventLog& events_;
    ITransport& transport_;
    std::vector<PeerId> connected_transport_peers_;
    std::vector<PeerId> pending_transport_disconnects_;
    std::unordered_map<PeerId, ConnectedClientState> clients_;
    std::vector<DisconnectedPlayer> disconnected_players_;
    std::vector<DisconnectedSpectator> disconnected_spectators_;
    std::array<walker*, kMaxGlobalPlayers> player_controls_ = {};
    std::array<SimInputDebounce, kMaxGlobalPlayers> player_input_debounce_ = {};
    std::string special_names_[NUM_FAMILIES][NUM_SPECIALS] = {};
    std::vector<TypedReceivedMessage> last_polled_messages_;
    std::deque<TypedReceivedMessage> pending_inbound_messages_;
    int messages_drained_last_call_ = 0;
    std::optional<PendingExitPromptState> pending_exit_prompt_state_ =
        std::nullopt;
    std::optional<PendingPauseState> pending_pause_state_ = std::nullopt;
    std::size_t snapshot_hash_mismatch_count_ = 0;
    std::uint32_t next_sim_event_sequence_ = 1;
    std::uint32_t next_game_flow_event_sequence_ = 1;
    std::optional<PeerId> host_peer_id_ = std::nullopt;
    std::function<std::uint64_t()> wall_clock_ms_source_;
    bool return_to_lobby_mode_ = false;
};

} // namespace og::sim
