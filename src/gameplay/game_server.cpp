#include <openglad/gameplay/game_server.h>

#include <algorithm>

namespace
{

using EntityListKind = og::sim::SnapshotEntityListKind;
using PendingRemovedEntity = og::sim::PendingRemovedEntity;

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

bool is_removed_entity_sentinel(const og::sim::EntitySnapshot& entity) noexcept
{
    return entity.dirty_mask[0] == 0 && entity.dirty_mask[1] == 0;
}

bool contains_removed_entity(const std::vector<PendingRemovedEntity>& removed_entities,
                             std::uint32_t entity_id)
{
    return std::find_if(
               removed_entities.begin(), removed_entities.end(),
               [entity_id](const PendingRemovedEntity& removed_entity) {
                   return removed_entity.entity_id == entity_id;
               }) != removed_entities.end();
}

void upsert_grid_dirty_tile(std::vector<og::sim::GridTileSnapshot>& tiles,
                            const og::sim::GridTileSnapshot& tile)
{
    const auto it = std::find_if(
        tiles.begin(), tiles.end(),
        [&tile](const og::sim::GridTileSnapshot& existing) {
            return existing.x == tile.x && existing.y == tile.y;
        });
    if (it == tiles.end())
    {
        tiles.push_back(tile);
    }
    else
    {
        *it = tile;
    }
}

void accumulate_grid_for_client(og::sim::PerClientState& client_state,
                                const og::sim::WorldSnapshot& snapshot)
{
    if (snapshot.grid_full_resend)
    {
        client_state.pending_grid_dirty = true;
        client_state.pending_grid_full_resend = true;
        client_state.pending_full_grid_data = snapshot.full_grid_data;
        client_state.pending_grid_dirty_tiles.clear();
    }

    if (snapshot.grid_dirty)
        client_state.pending_grid_dirty = true;

    for (const auto& tile : snapshot.grid_dirty_tiles)
        upsert_grid_dirty_tile(client_state.pending_grid_dirty_tiles, tile);
}

void collect_snapshot_entity_lists(
    const og::sim::WorldSnapshot& snapshot,
    std::unordered_map<std::uint32_t, EntityListKind>& entity_lists)
{
    entity_lists.clear();
    entity_lists.reserve(snapshot.oblist.size() + snapshot.fxlist.size() +
                         snapshot.weaplist.size());

    const auto collect = [&entity_lists](const auto& entities, EntityListKind list_kind) {
        for (const auto& entity : entities)
            entity_lists[entity.entity_id] = list_kind;
    };

    collect(snapshot.oblist, EntityListKind::Ob);
    collect(snapshot.fxlist, EntityListKind::Fx);
    collect(snapshot.weaplist, EntityListKind::Weap);
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

void append_removed_entity_sentinels(
    std::vector<og::sim::EntitySnapshot>& entities,
    const std::vector<PendingRemovedEntity>& removed_entities,
    EntityListKind list_kind)
{
    for (const PendingRemovedEntity& removed_entity : removed_entities)
    {
        if (removed_entity.list_kind != list_kind)
            continue;

        og::sim::EntitySnapshot sentinel;
        sentinel.entity_id = removed_entity.entity_id;
        sentinel.guy_id = og::sim::kNoGuyId;
        entities.push_back(sentinel);
    }
}

void collect_removed_entity_ids(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::vector<std::uint32_t>& removed_entity_ids)
{
    for (const auto& entity : entities)
    {
        if (is_removed_entity_sentinel(entity))
            removed_entity_ids.push_back(entity.entity_id);
    }
}

void remember_sent_entity_lists(og::sim::PerClientState& client_state,
                                const std::vector<og::sim::EntitySnapshot>& entities,
                                EntityListKind list_kind)
{
    for (const auto& entity : entities)
    {
        if (is_removed_entity_sentinel(entity))
            continue;
        client_state.known_entity_lists[entity.entity_id] = list_kind;
    }
}

} // namespace

namespace og::sim {

void reset_client_snapshot_state(PerClientState& client_state) noexcept
{
    client_state.last_sent_tick = 0;
    client_state.accumulated_dirty.clear();
    client_state.new_entity_ids.clear();
    client_state.removed_entities.clear();
    client_state.known_entity_lists.clear();
    client_state.pending_grid_dirty = false;
    client_state.pending_grid_full_resend = false;
    client_state.pending_full_grid_data.clear();
    client_state.pending_grid_dirty_tiles.clear();
}

void seed_client_snapshot_baseline(PerClientState& client_state,
                                   const WorldSnapshot& keyframe)
{
    reset_client_snapshot_state(client_state);
    client_state.last_sent_tick = keyframe.tick_count;
    collect_snapshot_entity_lists(keyframe, client_state.known_entity_lists);
}

void accumulate_snapshot_for_client(PerClientState& client_state,
                                    const WorldSnapshot& snapshot)
{
    accumulate_grid_for_client(client_state, snapshot);

    auto accumulate_entities = [&client_state](const auto& entities,
                                               EntityListKind list_kind) {
        for (const auto& entity : entities)
        {
            auto known_it = client_state.known_entity_lists.find(entity.entity_id);
            if (known_it == client_state.known_entity_lists.end())
            {
                if (!contains_entity_id(client_state.new_entity_ids,
                                        entity.entity_id))
                {
                    client_state.new_entity_ids.push_back(entity.entity_id);
                }
                continue;
            }

            known_it->second = list_kind;
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

    accumulate_entities(snapshot.oblist, EntityListKind::Ob);
    accumulate_entities(snapshot.fxlist, EntityListKind::Fx);
    accumulate_entities(snapshot.weaplist, EntityListKind::Weap);

    for (std::uint32_t removed_id : snapshot.removed_entity_ids)
    {
        client_state.accumulated_dirty.erase(removed_id);
        if (contains_entity_id(client_state.new_entity_ids, removed_id))
        {
            erase_entity_id(client_state.new_entity_ids, removed_id);
            continue;
        }

        const auto known_it = client_state.known_entity_lists.find(removed_id);
        if (known_it == client_state.known_entity_lists.end())
            continue;

        const PendingRemovedEntity removed_entity = {
            removed_id,
            known_it->second,
        };
        client_state.known_entity_lists.erase(known_it);

        if (!contains_removed_entity(client_state.removed_entities, removed_id))
            client_state.removed_entities.push_back(removed_entity);
    }
}

WorldSnapshot consume_delta_snapshot_for_client(PerClientState& client_state,
                                                const WorldSnapshot& snapshot)
{
    WorldSnapshot delta = snapshot;
    delta.grid_dirty = client_state.pending_grid_dirty;
    delta.grid_full_resend = client_state.pending_grid_full_resend;
    delta.full_grid_data = client_state.pending_grid_full_resend
        ? client_state.pending_full_grid_data
        : std::vector<std::uint8_t>{};
    delta.grid_dirty_tiles = client_state.pending_grid_dirty_tiles;
    delta.oblist = select_delta_entities(snapshot.oblist, client_state);
    delta.fxlist = select_delta_entities(snapshot.fxlist, client_state);
    delta.weaplist = select_delta_entities(snapshot.weaplist, client_state);
    append_removed_entity_sentinels(delta.oblist, client_state.removed_entities,
                                    EntityListKind::Ob);
    append_removed_entity_sentinels(delta.fxlist, client_state.removed_entities,
                                    EntityListKind::Fx);
    append_removed_entity_sentinels(delta.weaplist, client_state.removed_entities,
                                    EntityListKind::Weap);

    delta.removed_entity_ids.clear();
    delta.removed_entity_ids.reserve(client_state.removed_entities.size());
    collect_removed_entity_ids(delta.oblist, delta.removed_entity_ids);
    collect_removed_entity_ids(delta.fxlist, delta.removed_entity_ids);
    collect_removed_entity_ids(delta.weaplist, delta.removed_entity_ids);

    remember_sent_entity_lists(client_state, delta.oblist, EntityListKind::Ob);
    remember_sent_entity_lists(client_state, delta.fxlist, EntityListKind::Fx);
    remember_sent_entity_lists(client_state, delta.weaplist, EntityListKind::Weap);

    client_state.last_sent_tick = snapshot.tick_count;
    client_state.accumulated_dirty.clear();
    client_state.new_entity_ids.clear();
    client_state.removed_entities.clear();
    client_state.pending_grid_dirty = false;
    client_state.pending_grid_full_resend = false;
    client_state.pending_full_grid_data.clear();
    client_state.pending_grid_dirty_tiles.clear();
    return delta;
}

} // namespace og::sim
