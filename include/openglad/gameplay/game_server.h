#pragma once

#include <openglad/gameplay/world_snapshot.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace og::sim {

enum class SnapshotEntityListKind : std::uint8_t {
    Ob,
    Fx,
    Weap,
};

struct PendingRemovedEntity {
    std::uint32_t entity_id = 0;
    SnapshotEntityListKind list_kind = SnapshotEntityListKind::Ob;
};

// Phase 9 server-side snapshot bookkeeping. A future GameServer will own one
// of these per connected client and feed it snapshots from capture_snapshot().
struct PerClientState {
    std::uint32_t last_sent_tick = 0;
    std::unordered_map<std::uint32_t, EntitySnapshotDirtyMask> accumulated_dirty;
    std::vector<std::uint32_t> new_entity_ids;
    std::vector<PendingRemovedEntity> removed_entities;
    std::unordered_map<std::uint32_t, SnapshotEntityListKind> known_entity_lists;
};

void reset_client_snapshot_state(PerClientState& client_state) noexcept;
void seed_client_snapshot_baseline(PerClientState& client_state,
                                   const WorldSnapshot& keyframe);
void accumulate_snapshot_for_client(PerClientState& client_state,
                                    const WorldSnapshot& snapshot);
WorldSnapshot consume_delta_snapshot_for_client(PerClientState& client_state,
                                                const WorldSnapshot& snapshot);

} // namespace og::sim
