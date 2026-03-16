#pragma once

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/world_snapshot.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct InputState;
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

    [[nodiscard]] const std::vector<TypedReceivedMessage>&
    last_polled_messages() const noexcept
    {
        return last_polled_messages_;
    }

private:
    void apply_polled_inputs(std::uint32_t expected_tick);

    GameWorld& world_;
    SimEventLog& events_;
    ITransport& transport_;
    std::unordered_map<PeerId, ConnectedClientState> clients_;
    std::array<walker*, MAX_PLAYERS> player_controls_ = {};
    std::array<SimInputDebounce, MAX_PLAYERS> player_input_debounce_ = {};
    std::string special_names_[NUM_FAMILIES][NUM_SPECIALS] = {};
    std::vector<TypedReceivedMessage> last_polled_messages_;
};

} // namespace og::sim
