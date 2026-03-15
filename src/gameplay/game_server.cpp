#include <openglad/gameplay/game_server.h>

#include <algorithm>

namespace
{

bool contains_entity_id(const std::vector<std::uint32_t>& ids,
                        std::uint32_t entity_id)
{
    return std::find(ids.begin(), ids.end(), entity_id) != ids.end();
}

void erase_entity_id(std::vector<std::uint32_t>& ids, std::uint32_t entity_id)
{
    ids.erase(std::remove(ids.begin(), ids.end(), entity_id), ids.end());
}

bool dirty_mask_has_bits(const og::sim::EntitySnapshotDirtyMask& mask) noexcept
{
    return mask[0] != 0 || mask[1] != 0;
}

void collect_snapshot_entity_ids(const og::sim::WorldSnapshot& snapshot,
                                 std::unordered_set<std::uint32_t>& ids)
{
    ids.clear();
    ids.reserve(snapshot.oblist.size() + snapshot.fxlist.size() +
                snapshot.weaplist.size());

    for (const auto& entity : snapshot.oblist)
        ids.insert(entity.entity_id);
    for (const auto& entity : snapshot.fxlist)
        ids.insert(entity.entity_id);
    for (const auto& entity : snapshot.weaplist)
        ids.insert(entity.entity_id);
}

std::vector<og::sim::EntitySnapshot> select_delta_entities(
    const std::vector<og::sim::EntitySnapshot>& current_entities,
    const og::sim::PerClientState& client_state)
{
    std::vector<og::sim::EntitySnapshot> selected;
    selected.reserve(current_entities.size());

    for (const auto& entity : current_entities)
    {
        og::sim::EntitySnapshot delta_entity = entity;
        if (contains_entity_id(client_state.new_entity_ids, entity.entity_id))
        {
            delta_entity.dirty_mask[0] = ~0ULL;
            delta_entity.dirty_mask[1] = ~0ULL;
            selected.push_back(delta_entity);
            continue;
        }

        const auto mask_it = client_state.accumulated_dirty.find(entity.entity_id);
        if (mask_it == client_state.accumulated_dirty.end() ||
            !dirty_mask_has_bits(mask_it->second))
        {
            continue;
        }

        delta_entity.dirty_mask[0] = mask_it->second[0];
        delta_entity.dirty_mask[1] = mask_it->second[1];
        selected.push_back(delta_entity);
    }

    return selected;
}

} // namespace

namespace og::sim {

void reset_client_snapshot_state(PerClientState& client_state) noexcept
{
    client_state.last_sent_tick = 0;
    client_state.accumulated_dirty.clear();
    client_state.new_entity_ids.clear();
    client_state.removed_entity_ids.clear();
    client_state.known_entity_ids.clear();
}

void seed_client_snapshot_baseline(PerClientState& client_state,
                                   const WorldSnapshot& keyframe)
{
    reset_client_snapshot_state(client_state);
    client_state.last_sent_tick = keyframe.tick_count;
    collect_snapshot_entity_ids(keyframe, client_state.known_entity_ids);
}

void accumulate_snapshot_for_client(PerClientState& client_state,
                                    const WorldSnapshot& snapshot)
{
    auto accumulate_entities = [&client_state](const auto& entities) {
        for (const auto& entity : entities)
        {
            if (client_state.known_entity_ids.find(entity.entity_id) ==
                client_state.known_entity_ids.end())
            {
                if (!contains_entity_id(client_state.new_entity_ids,
                                        entity.entity_id))
                {
                    client_state.new_entity_ids.push_back(entity.entity_id);
                }
                continue;
            }

            if (contains_entity_id(client_state.new_entity_ids, entity.entity_id))
                continue;

            if (entity.dirty_mask[0] == 0 && entity.dirty_mask[1] == 0)
                continue;

            EntitySnapshotDirtyMask& accumulated =
                client_state.accumulated_dirty[entity.entity_id];
            accumulated[0] |= entity.dirty_mask[0];
            accumulated[1] |= entity.dirty_mask[1];
        }
    };

    accumulate_entities(snapshot.oblist);
    accumulate_entities(snapshot.fxlist);
    accumulate_entities(snapshot.weaplist);

    for (std::uint32_t removed_id : snapshot.removed_entity_ids)
    {
        client_state.accumulated_dirty.erase(removed_id);
        if (contains_entity_id(client_state.new_entity_ids, removed_id))
        {
            erase_entity_id(client_state.new_entity_ids, removed_id);
            continue;
        }

        if (client_state.known_entity_ids.erase(removed_id) == 0)
            continue;

        if (!contains_entity_id(client_state.removed_entity_ids, removed_id))
            client_state.removed_entity_ids.push_back(removed_id);
    }
}

WorldSnapshot consume_delta_snapshot_for_client(PerClientState& client_state,
                                                const WorldSnapshot& snapshot)
{
    WorldSnapshot delta = snapshot;
    delta.oblist = select_delta_entities(snapshot.oblist, client_state);
    delta.fxlist = select_delta_entities(snapshot.fxlist, client_state);
    delta.weaplist = select_delta_entities(snapshot.weaplist, client_state);
    delta.removed_entity_ids = client_state.removed_entity_ids;

    for (std::uint32_t entity_id : client_state.new_entity_ids)
        client_state.known_entity_ids.insert(entity_id);

    client_state.last_sent_tick = snapshot.tick_count;
    client_state.accumulated_dirty.clear();
    client_state.new_entity_ids.clear();
    client_state.removed_entity_ids.clear();
    return delta;
}

} // namespace og::sim
