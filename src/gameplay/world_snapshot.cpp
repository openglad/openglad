#include <openglad/gameplay/world_snapshot.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{

using EntityStorage = GameWorld::EntityList::Storage;
using EntitySnapshotLookup =
    std::unordered_map<std::uint32_t, const og::sim::EntitySnapshot*>;
using GuyStorage = std::unordered_map<std::int32_t, std::unique_ptr<guy>>;
using GuyLookup = std::unordered_map<std::int32_t, guy*>;

class ApplyingSnapshotGuard
{
public:
    explicit ApplyingSnapshotGuard(GameWorld& world)
        : world_(world)
        , prev_(world.applying_snapshot_)
    {
        world_.applying_snapshot_ = true;
    }

    ~ApplyingSnapshotGuard()
    {
        world_.applying_snapshot_ = prev_;
    }

    ApplyingSnapshotGuard(const ApplyingSnapshotGuard&) = delete;
    ApplyingSnapshotGuard& operator=(const ApplyingSnapshotGuard&) = delete;

private:
    GameWorld& world_;
    bool prev_ = false;
};

og::sim::GuySnapshot capture_guy_snapshot(const guy& source)
{
    og::sim::GuySnapshot snapshot;
    snapshot.guy_id = source.id;
    snapshot.name = source.name;
    snapshot.family = source.family;
    snapshot.strength = source.strength;
    snapshot.dexterity = source.dexterity;
    snapshot.constitution = source.constitution;
    snapshot.intelligence = source.intelligence;
    snapshot.armor = source.armor;
    snapshot.exp = source.exp;
    snapshot.kills = source.kills;
    snapshot.level_kills = source.level_kills;
    snapshot.total_damage = source.total_damage;
    snapshot.total_hits = source.total_hits;
    snapshot.total_shots = source.total_shots;
    snapshot.teamnum = source.teamnum;
    snapshot.scen_damage = source.scen_damage;
    snapshot.scen_kills = source.scen_kills;
    snapshot.scen_damage_taken = source.scen_damage_taken;
    snapshot.scen_min_hp = source.scen_min_hp;
    snapshot.scen_shots = source.scen_shots;
    snapshot.scen_hits = source.scen_hits;
    snapshot.level = source.level;
    return snapshot;
}

void apply_guy_snapshot(guy& target, const og::sim::GuySnapshot& snapshot)
{
    target.id = snapshot.guy_id;
    target.name = snapshot.name;
    target.family = snapshot.family;
    target.strength = snapshot.strength;
    target.dexterity = snapshot.dexterity;
    target.constitution = snapshot.constitution;
    target.intelligence = snapshot.intelligence;
    target.armor = snapshot.armor;
    target.exp = snapshot.exp;
    target.kills = snapshot.kills;
    target.level_kills = snapshot.level_kills;
    target.total_damage = snapshot.total_damage;
    target.total_hits = snapshot.total_hits;
    target.total_shots = snapshot.total_shots;
    target.teamnum = snapshot.teamnum;
    target.scen_damage = snapshot.scen_damage;
    target.scen_kills = snapshot.scen_kills;
    target.scen_damage_taken = snapshot.scen_damage_taken;
    target.scen_min_hp = snapshot.scen_min_hp;
    target.scen_shots = snapshot.scen_shots;
    target.scen_hits = snapshot.scen_hits;
    target.level = snapshot.level;
}

void build_entity_snapshot_lookup(
    const std::vector<og::sim::EntitySnapshot>& snapshots,
    EntitySnapshotLookup& lookup)
{
    lookup.clear();
    lookup.reserve(snapshots.size());
    for (const auto& snapshot : snapshots)
        lookup[snapshot.entity_id] = &snapshot;
}

template <typename EntityList>
void clear_entity_guy_links(EntityList& entities)
{
    for (const auto& entry : entities)
    {
        if (entry != nullptr)
            entry->clear_myguy();
    }
}

template <typename EntityList>
void remove_missing_entities(GameWorld& world,
                             EntityList& entities,
                             const EntitySnapshotLookup& snapshots)
{
    for (auto it = entities.begin(); it != entities.end();)
    {
        walker* const entity = it->get();
        if (entity == nullptr)
        {
            it = entities.erase(it);
            continue;
        }

        if (snapshots.find(entity->entity_id()) != snapshots.end())
        {
            ++it;
            continue;
        }

        if (world.myobmap != nullptr)
            world.myobmap->remove(entity);
        it = entities.erase(it);
    }
}

void apply_entity_snapshot_fields(GameWorld& world,
                                  walker& entity,
                                  const og::sim::EntitySnapshot& snapshot,
                                  bool reconfigure)
{
    if (world.myobmap != nullptr)
        world.myobmap->remove(&entity);

    if (reconfigure)
    {
        if (world.entity_configurator != nullptr)
        {
            const PixieData* data = world.configure_existing_entity(
                entity, snapshot.order, snapshot.family);
            if (data == nullptr)
            {
                LogError("apply_snapshot: failed to configure entity {} ({}, {})\n",
                         snapshot.entity_id, static_cast<int>(snapshot.order),
                         static_cast<int>(snapshot.family));
            }
        }
        world.set_entity_derived_stats(&entity, snapshot.order, snapshot.family);
    }

    entity.clear_myguy();
    entity.set_foe(nullptr);
    entity.set_leader(nullptr);
    entity.set_owner(nullptr);
    entity.set_collide_ob(nullptr);
    entity.path_to_foe.clear();
    entity.damage_numbers.clear();

    entity.set_order_family(snapshot.order, snapshot.family);
    entity.set_snapshot_position(snapshot.xpos, snapshot.ypos,
                                 snapshot.worldx, snapshot.worldy);
    entity.sizex = snapshot.sizex;
    entity.sizey = snapshot.sizey;
    entity.team_num = snapshot.team_num;
    entity.real_team_num = snapshot.real_team_num;
    entity.user = snapshot.user;
    entity.dead = snapshot.dead;
    entity.death_called = snapshot.death_called;
    entity.invulnerable_left = snapshot.invulnerable_left;
    entity.invisibility_left = snapshot.invisibility_left;
    entity.flight_left = snapshot.flight_left;
    entity.bonus_rounds = snapshot.bonus_rounds;
    entity.lastx = snapshot.lastx;
    entity.lasty = snapshot.lasty;
    entity.stepsize = snapshot.stepsize;
    entity.normal_stepsize = snapshot.normal_stepsize;
    entity.curdir = snapshot.curdir;
    entity.enddir = snapshot.enddir;
    entity.damage = snapshot.damage;
    entity.fire_frequency = snapshot.fire_frequency;
    entity.busy = snapshot.busy;
    entity.current_weapon = snapshot.current_weapon;
    entity.default_weapon = snapshot.default_weapon;
    entity.attack_lunge = snapshot.attack_lunge;
    entity.attack_lunge_angle = snapshot.attack_lunge_angle;
    entity.hit_recoil = snapshot.hit_recoil;
    entity.hit_recoil_angle = snapshot.hit_recoil_angle;
    entity.last_hitpoints = snapshot.last_hitpoints;
    entity.action = snapshot.action;
    entity.act_type = snapshot.act_type;
    entity.old_act_type = snapshot.old_act_type;
    entity.ani_type = snapshot.ani_type;
    entity.cycle = snapshot.cycle;
    entity.drawcycle = snapshot.drawcycle;
    entity.current_special = snapshot.current_special;
    entity.ignore = snapshot.ignore;
    entity.in_act = snapshot.in_act != 0;
    entity.shifter_down = snapshot.shifter_down;
    entity.yo_delay = snapshot.yo_delay;
    entity.skip_exit = snapshot.skip_exit;
    entity.outline = snapshot.outline;
    entity.hurt_flash = snapshot.hurt_flash != 0;
    entity.lifetime = snapshot.lifetime;
    entity.speed_bonus = snapshot.speed_bonus;
    entity.speed_bonus_left = snapshot.speed_bonus_left;
    entity.charm_left = snapshot.charm_left;
    entity.weapons_left = snapshot.weapons_left;
    entity.keys = snapshot.keys;
    entity.view_all = snapshot.view_all;
    entity.lineofsight = snapshot.lineofsight;
    entity.path_check_counter = snapshot.path_check_counter;
    entity.set_regen_delay(snapshot.regen_delay);
    entity.foe_id = snapshot.foe_id;
    entity.leader_id = snapshot.leader_id;
    entity.owner_id = snapshot.owner_id;
    entity.collide_ob_id = snapshot.collide_ob_id;

    if (statistics* const stats = entity.stats(); stats != nullptr)
    {
        stats->set_controller(nullptr);
        stats->hitpoints = snapshot.hitpoints;
        stats->max_hitpoints = snapshot.max_hitpoints;
        stats->magicpoints = snapshot.magicpoints;
        stats->max_magicpoints = snapshot.max_magicpoints;
        stats->max_heal_delay = snapshot.max_heal_delay;
        stats->current_heal_delay = snapshot.current_heal_delay;
        stats->max_magic_delay = snapshot.max_magic_delay;
        stats->current_magic_delay = snapshot.current_magic_delay;
        stats->magic_per_round = snapshot.magic_per_round;
        stats->heal_per_round = snapshot.heal_per_round;
        stats->armor = snapshot.armor;
        stats->level = snapshot.level;
        stats->bit_flags = snapshot.bit_flags;
        stats->delete_me = snapshot.delete_me;
        stats->frozen_delay = snapshot.frozen_delay;
        stats->weapon_cost = snapshot.weapon_cost;
        std::copy(std::begin(snapshot.special_cost),
                  std::end(snapshot.special_cost),
                  std::begin(stats->special_cost));
        stats->old_order = snapshot.old_order;
        stats->old_family = snapshot.old_family;
        stats->last_distance = snapshot.last_distance;
        stats->current_distance = snapshot.current_distance;
        stats->controller_id = snapshot.controller_id;
        stats->commands.clear();
    }

    if (auto* weapon = dynamic_cast<weap*>(&entity); weapon != nullptr)
        weapon->do_bounce = snapshot.do_bounce;

    entity.set_direct_frame(snapshot.frame);
}

template <typename EntityList>
void update_existing_entities(GameWorld& world,
                              EntityList& entities,
                              const EntitySnapshotLookup& snapshots)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr)
            continue;

        const auto snapshot_it = snapshots.find(entity->entity_id());
        if (snapshot_it == snapshots.end() || snapshot_it->second == nullptr)
            continue;

        const og::sim::EntitySnapshot& snapshot = *snapshot_it->second;
        const bool reconfigure =
            entity->order != snapshot.order || entity->family != snapshot.family;
        apply_entity_snapshot_fields(world, *entity, snapshot, reconfigure);
    }
}

GameWorld::EntityList& snapshot_target_list(GameWorld& world,
                                            GameWorld::EntityList& preferred_list,
                                            Order order)
{
    if (&preferred_list == &world.weaplist || order == Order::Weapon)
        return world.weaplist;
    if (&preferred_list == &world.fxlist || order == Order::FX)
        return world.fxlist;
    return world.oblist;
}

template <typename EntityList>
void create_missing_entities(GameWorld& world,
                             EntityList& preferred_list,
                             const std::vector<og::sim::EntitySnapshot>& snapshots)
{
    for (const auto& snapshot : snapshots)
    {
        if (world.find_by_id(snapshot.entity_id) != nullptr)
            continue;

        if (!world.entity_factory)
        {
            LogError("apply_snapshot: entity_factory unavailable for entity {}\n",
                     snapshot.entity_id);
            return;
        }

        auto created = world.entity_factory(snapshot.order, snapshot.family);
        if (!created)
        {
            LogError("apply_snapshot: failed to create entity {} ({}, {})\n",
                     snapshot.entity_id, static_cast<int>(snapshot.order),
                     static_cast<int>(snapshot.family));
            continue;
        }

        walker* const raw = created.get();
        raw->set_snapshot_entity_id(snapshot.entity_id);
        if (world.entity_configurator != nullptr)
            (void)world.configure_existing_entity(*raw, snapshot.order, snapshot.family);
        world.set_entity_derived_stats(raw, snapshot.order, snapshot.family);
        apply_entity_snapshot_fields(world, *raw, snapshot, false);

        GameWorld::EntityList& target =
            snapshot_target_list(world, preferred_list, snapshot.order);
        target.push_back(std::move(created));
    }
}

template <typename EntityList>
void append_entities_to_index(EntityList& entities,
                              std::unordered_map<std::uint32_t, walker*>& index)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr || entity->entity_id() == 0)
            continue;
        index[entity->entity_id()] = entity;
    }
}

void resolve_cross_references(GameWorld& world,
                              const std::unordered_map<std::uint32_t, walker*>& index)
{
    auto resolve_entity = [&index](walker& entity) {
        const auto lookup = [&index](std::uint32_t entity_id) -> walker* {
            const auto it = index.find(entity_id);
            return (it == index.end()) ? nullptr : it->second;
        };

        entity.set_foe(lookup(entity.foe_id));
        entity.set_leader(lookup(entity.leader_id));
        entity.set_owner(lookup(entity.owner_id));
        entity.set_collide_ob(lookup(entity.collide_ob_id));

        if (statistics* const stats = entity.stats(); stats != nullptr)
            stats->set_controller(lookup(stats->controller_id));
    };

    for (const auto& entry : world.oblist)
        if (entry != nullptr)
            resolve_entity(*entry);
    for (const auto& entry : world.fxlist)
        if (entry != nullptr)
            resolve_entity(*entry);
    for (const auto& entry : world.weaplist)
        if (entry != nullptr)
            resolve_entity(*entry);
}

void rebind_guys(GameWorld& world,
                 const EntitySnapshotLookup& ob_snapshots,
                 const EntitySnapshotLookup& fx_snapshots,
                 const EntitySnapshotLookup& weap_snapshots,
                 GuyStorage& guy_storage,
                 const GuyLookup& guy_lookup)
{
    std::unordered_set<std::int32_t> claimed_ids;

    auto bind_list = [&](auto& entities, const EntitySnapshotLookup& snapshots) {
        for (const auto& entry : entities)
        {
            walker* const entity = entry.get();
            if (entity == nullptr)
                continue;

            const auto snapshot_it = snapshots.find(entity->entity_id());
            if (snapshot_it == snapshots.end() || snapshot_it->second == nullptr)
                continue;

            const std::int32_t guy_id = snapshot_it->second->guy_id;
            if (guy_id == og::sim::kNoGuyId)
                continue;

            const auto guy_it = guy_lookup.find(guy_id);
            if (guy_it == guy_lookup.end())
            {
                LogError("apply_snapshot: missing guy {} for entity {}\n",
                         guy_id, entity->entity_id());
                continue;
            }

            if (claimed_ids.insert(guy_id).second)
            {
                auto owned_it = guy_storage.find(guy_id);
                if (owned_it != guy_storage.end() && owned_it->second != nullptr)
                    entity->set_owned_myguy(std::move(owned_it->second));
                else
                    entity->set_myguy_view(guy_it->second);
            }
            else
            {
                entity->set_myguy_view(guy_it->second);
            }
        }
    };

    bind_list(world.oblist, ob_snapshots);
    bind_list(world.fxlist, fx_snapshots);
    bind_list(world.weaplist, weap_snapshots);
}

void rebuild_obmap(GameWorld& world)
{
    if (world.myobmap == nullptr)
        return;

    auto readd_list = [&world](const auto& entities) {
        for (const auto& entry : entities)
        {
            if (entry == nullptr)
                continue;
            if (!entry->ignore)
                world.myobmap->add(entry.get(), entry->xpos, entry->ypos);
            else
                world.myobmap->remove(entry.get());
        }
    };

    readd_list(world.oblist);
    readd_list(world.fxlist);
    readd_list(world.weaplist);
}

void apply_grid_snapshot(GameWorld& world, const og::sim::WorldSnapshot& snapshot)
{
    if (!world.grid.valid())
        return;

    const std::size_t grid_size =
        static_cast<std::size_t>(world.grid.w) * world.grid.h;

    if (snapshot.grid_full_resend)
    {
        if (snapshot.full_grid_data.size() != grid_size)
        {
            LogError("apply_snapshot: full grid size mismatch (got {}, expected {})\n",
                     snapshot.full_grid_data.size(), grid_size);
        }
        else
        {
            std::copy(snapshot.full_grid_data.begin(),
                      snapshot.full_grid_data.end(),
                      world.grid.data.get());
        }
    }

    for (const auto& tile : snapshot.grid_dirty_tiles)
    {
        if (tile.x < 0 || tile.y < 0 ||
            tile.x >= world.grid.w || tile.y >= world.grid.h)
        {
            continue;
        }

        const std::size_t grid_index =
            static_cast<std::size_t>(tile.y) * world.grid.w + tile.x;
        world.grid.data[grid_index] = tile.value;
    }
}

template <typename EntityList>
void reorder_entity_list(EntityList& entities,
                         const std::vector<og::sim::EntitySnapshot>& snapshots)
{
    EntityStorage detached;
    entities.splice_into(detached);

    if (detached.empty())
        return;

    std::unordered_map<std::uint32_t, EntityStorage::iterator> by_id;
    by_id.reserve(detached.size());
    for (auto it = detached.begin(); it != detached.end(); ++it)
    {
        if (*it != nullptr)
            by_id[(*it)->entity_id()] = it;
    }

    EntityStorage ordered;
    for (const auto& snapshot : snapshots)
    {
        const auto found = by_id.find(snapshot.entity_id);
        if (found == by_id.end())
            continue;
        ordered.splice(ordered.end(), detached, found->second);
    }

    ordered.splice(ordered.end(), detached);
    entities.splice(entities.end(), ordered);
}

void clear_entity_dirty_masks(GameWorld& world)
{
    auto clear_list = [](const auto& entities) {
        for (const auto& entry : entities)
        {
            if (entry != nullptr)
                entry->clear_dirty();
        }
    };

    clear_list(world.oblist);
    clear_list(world.fxlist);
    clear_list(world.weaplist);
}

std::uint8_t capture_bool_byte(const bool& value)
{
    unsigned char raw = 0;
    static_assert(sizeof(raw) == sizeof(value));
    std::memcpy(&raw, &value, sizeof(raw));
    return raw != 0 ? 1U : 0U;
}

void capture_world_grid(const GameWorld& world,
                        og::sim::WorldSnapshot& snapshot,
                        std::vector<std::pair<short, short>> dirty_tiles,
                        bool keyframe)
{
    if (!world.grid.valid())
        return;

    const bool overflowed = dirty_tiles.size() > og::sim::MAX_GRID_DIRTY_TILES;
    snapshot.grid_dirty = keyframe || !dirty_tiles.empty();
    snapshot.grid_full_resend = keyframe || overflowed;

    if (snapshot.grid_full_resend)
    {
        const std::size_t grid_size =
            static_cast<std::size_t>(world.grid.w) * world.grid.h;
        snapshot.full_grid_data.assign(world.grid.data.get(),
                                       world.grid.data.get() + grid_size);
    }

    if (overflowed)
        dirty_tiles.clear();

    snapshot.grid_dirty_tiles.reserve(dirty_tiles.size());
    for (const auto& [x, y] : dirty_tiles)
    {
        if (x < 0 || y < 0 || x >= world.grid.w || y >= world.grid.h)
            continue;

        const std::size_t grid_index =
            static_cast<std::size_t>(y) * world.grid.w + x;
        snapshot.grid_dirty_tiles.push_back(
            {x, y, world.grid.data[grid_index]});
    }
}

void capture_entity_stats(const statistics* entity_stats,
                          og::sim::EntitySnapshot& snapshot)
{
    if (entity_stats == nullptr)
        return;

    snapshot.hitpoints = entity_stats->hitpoints;
    snapshot.max_hitpoints = entity_stats->max_hitpoints;
    snapshot.magicpoints = entity_stats->magicpoints;
    snapshot.max_magicpoints = entity_stats->max_magicpoints;
    snapshot.max_heal_delay = entity_stats->max_heal_delay;
    snapshot.current_heal_delay = entity_stats->current_heal_delay;
    snapshot.max_magic_delay = entity_stats->max_magic_delay;
    snapshot.current_magic_delay = entity_stats->current_magic_delay;
    snapshot.magic_per_round = entity_stats->magic_per_round;
    snapshot.heal_per_round = entity_stats->heal_per_round;
    snapshot.armor = entity_stats->armor;
    snapshot.level = entity_stats->level;
    snapshot.bit_flags = entity_stats->bit_flags;
    snapshot.delete_me = entity_stats->delete_me;
    snapshot.frozen_delay = entity_stats->frozen_delay;
    snapshot.weapon_cost = entity_stats->weapon_cost;
    std::copy(std::begin(entity_stats->special_cost),
              std::end(entity_stats->special_cost),
              std::begin(snapshot.special_cost));
    snapshot.old_order = entity_stats->old_order;
    snapshot.old_family = entity_stats->old_family;
    snapshot.last_distance = entity_stats->last_distance;
    snapshot.current_distance = entity_stats->current_distance;
    snapshot.controller_id = entity_stats->controller_id;
}

og::sim::EntitySnapshot capture_entity_snapshot(walker& entity, bool keyframe)
{
    entity.sync_ids_from_pointers();

    og::sim::EntitySnapshot snapshot;
    if (keyframe)
    {
        std::fill(std::begin(snapshot.dirty_mask),
                  std::end(snapshot.dirty_mask),
                  ~0ULL);
    }
    else
    {
        for (std::size_t i = 0; i < og::sim::kEntitySnapshotDirtyMaskWords; ++i)
            snapshot.dirty_mask[i] = entity.dirty_mask_word(i);
    }

    snapshot.guy_id = (entity.myguy != nullptr) ? entity.myguy->id
                                                : og::sim::kNoGuyId;
    snapshot.entity_id = entity.entity_id();
    snapshot.xpos = entity.xpos;
    snapshot.ypos = entity.ypos;
    snapshot.sizex = entity.sizex;
    snapshot.sizey = entity.sizey;
    snapshot.team_num = entity.team_num;
    snapshot.real_team_num = entity.real_team_num;
    snapshot.user = entity.user;
    snapshot.dead = entity.dead;
    snapshot.death_called = entity.death_called;
    snapshot.invulnerable_left = entity.invulnerable_left;
    snapshot.invisibility_left = entity.invisibility_left;
    snapshot.flight_left = entity.flight_left;
    snapshot.bonus_rounds = entity.bonus_rounds;
    snapshot.order = entity.order;
    snapshot.family = entity.family;
    snapshot.frame = entity.frame;
    snapshot.worldx = entity.worldx();
    snapshot.worldy = entity.worldy();

    snapshot.lastx = entity.lastx;
    snapshot.lasty = entity.lasty;
    snapshot.stepsize = entity.stepsize;
    snapshot.normal_stepsize = entity.normal_stepsize;
    snapshot.curdir = entity.curdir;
    snapshot.enddir = entity.enddir;
    snapshot.damage = entity.damage;
    snapshot.fire_frequency = entity.fire_frequency;
    snapshot.busy = entity.busy;
    snapshot.current_weapon = entity.current_weapon;
    snapshot.default_weapon = entity.default_weapon;
    snapshot.attack_lunge = entity.attack_lunge;
    snapshot.attack_lunge_angle = entity.attack_lunge_angle;
    snapshot.hit_recoil = entity.hit_recoil;
    snapshot.hit_recoil_angle = entity.hit_recoil_angle;
    snapshot.last_hitpoints = entity.last_hitpoints;
    snapshot.action = entity.action;
    snapshot.act_type = entity.act_type;
    snapshot.old_act_type = entity.old_act_type;
    snapshot.ani_type = entity.ani_type;
    snapshot.cycle = entity.cycle;
    snapshot.drawcycle = entity.drawcycle;
    snapshot.current_special = entity.current_special;
    snapshot.ignore = entity.ignore;
    snapshot.in_act = capture_bool_byte(entity.in_act);
    snapshot.shifter_down = entity.shifter_down;
    snapshot.yo_delay = entity.yo_delay;
    snapshot.skip_exit = entity.skip_exit;
    snapshot.outline = entity.outline;
    snapshot.hurt_flash = capture_bool_byte(entity.hurt_flash);
    snapshot.lifetime = entity.lifetime;
    snapshot.speed_bonus = entity.speed_bonus;
    snapshot.speed_bonus_left = entity.speed_bonus_left;
    snapshot.charm_left = entity.charm_left;
    snapshot.weapons_left = entity.weapons_left;
    snapshot.keys = entity.keys;
    snapshot.view_all = entity.view_all;
    snapshot.lineofsight = entity.lineofsight;
    snapshot.path_check_counter = entity.path_check_counter;
    snapshot.regen_delay = entity.regen_delay();
    snapshot.foe_id = entity.foe_id;
    snapshot.leader_id = entity.leader_id;
    snapshot.owner_id = entity.owner_id;
    snapshot.collide_ob_id = entity.collide_ob_id;

    capture_entity_stats(entity.stats(), snapshot);

    if (const auto* weapon = dynamic_cast<const weap*>(&entity);
        weapon != nullptr)
    {
        snapshot.do_bounce = weapon->do_bounce;
    }
    else
    {
        snapshot.do_bounce = 0;
    }

    entity.clear_dirty();
    return snapshot;
}

template <typename EntityList>
void capture_entity_list(EntityList& entities,
                         std::vector<og::sim::EntitySnapshot>& entity_snapshots,
                         std::vector<og::sim::GuySnapshot>& guy_snapshots,
                         std::unordered_set<int>& seen_guy_ids,
                         bool keyframe)
{
    entity_snapshots.reserve(entities.size());
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr)
            continue;

        if (entity->myguy != nullptr &&
            seen_guy_ids.insert(entity->myguy->id).second)
        {
            guy_snapshots.push_back(capture_guy_snapshot(*entity->myguy));
        }

        entity_snapshots.push_back(capture_entity_snapshot(*entity, keyframe));
    }
}

og::sim::WorldSnapshot capture_snapshot_impl(GameWorld& world, bool keyframe)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = world.tick_count_;
    snapshot.rng_state = world.rng_.state_;
    snapshot.level_tick_count = world.level_tick_count();

    snapshot.level_done = world.level_done;
    snapshot.game_ended = world.game_ended;
    snapshot.end = world.end;
    snapshot.retry = world.retry;
    snapshot.next_level = world.next_level;
    snapshot.ending = world.ending;

    snapshot.enemy_freeze = world.enemy_freeze;
    snapshot.timer_wait = world.timer_wait;
    snapshot.living_count = world.living_count;
    snapshot.control_hp = world.control_hp;
    snapshot.withdraw_requested = world.withdraw_requested;
    snapshot.withdraw_level = world.withdraw_level;
    snapshot.guy_id_counter = world.guy_id_counter;
    std::copy(std::begin(world.m_score), std::end(world.m_score),
              std::begin(snapshot.m_score));

    snapshot.current_palette_id = world.current_palette_id;
    snapshot.pending_exit_prompt = world.pending_exit_prompt;
    snapshot.paused = world.paused;
    snapshot.pause_player_index = world.pause_player_index;

    std::unordered_set<int> seen_guy_ids;
    capture_entity_list(world.oblist, snapshot.oblist, snapshot.guy_snapshots,
                        seen_guy_ids, keyframe);
    capture_entity_list(world.fxlist, snapshot.fxlist, snapshot.guy_snapshots,
                        seen_guy_ids, keyframe);
    capture_entity_list(world.weaplist, snapshot.weaplist, snapshot.guy_snapshots,
                        seen_guy_ids, keyframe);

    snapshot.removed_entity_ids = world.take_removed_entity_ids();
    capture_world_grid(world, snapshot, world.take_grid_dirty_tiles(), keyframe);

    return snapshot;
}

} // namespace

namespace og::sim {

WorldSnapshot capture_snapshot(GameWorld& world)
{
    return capture_snapshot_impl(world, false);
}

WorldSnapshot capture_keyframe_snapshot(GameWorld& world)
{
    return capture_snapshot_impl(world, true);
}

void apply_snapshot(GameWorld& world, const WorldSnapshot& snapshot)
{
    ApplyingSnapshotGuard applying_guard(world);

    GameplayContext gameplay_context;
    const bool has_context = world.populate_gameplay_context(gameplay_context);
    assert(has_context && gameplay_context.world == &world &&
           "apply_snapshot requires a bound gameplay context for the target world");
    if (!has_context || gameplay_context.world != &world)
    {
        LogError("apply_snapshot: missing gameplay context bindings for world {}\n",
                 world.id);
        return;
    }

    GameplayContext* installed_context = &gameplay_context;
    if (current_game != nullptr &&
        current_game->world == &world &&
        current_game->save == gameplay_context.save &&
        current_game->sim_events == gameplay_context.sim_events &&
        current_game->config == gameplay_context.config)
    {
        installed_context = current_game;
    }

    GameplayContextGuard gameplay_guard(installed_context);
    SimEventLogSuppressGuard event_guard(*gameplay_context.sim_events);

    world.tick_count_ = snapshot.tick_count;
    world.set_level_tick_count(snapshot.level_tick_count);

    world.level_done = snapshot.level_done;
    world.game_ended = snapshot.game_ended;
    world.end = snapshot.end;
    world.retry = snapshot.retry;
    world.next_level = snapshot.next_level;
    world.ending = snapshot.ending;
    world.enemy_freeze = snapshot.enemy_freeze;
    world.timer_wait = snapshot.timer_wait;
    world.living_count = snapshot.living_count;
    world.control_hp = snapshot.control_hp;
    world.withdraw_requested = snapshot.withdraw_requested;
    world.withdraw_level = snapshot.withdraw_level;
    world.guy_id_counter = snapshot.guy_id_counter;
    std::copy(std::begin(snapshot.m_score), std::end(snapshot.m_score),
              std::begin(world.m_score));
    world.current_palette_id = snapshot.current_palette_id;
    world.pending_exit_prompt = snapshot.pending_exit_prompt;
    world.paused = snapshot.paused;
    world.pause_player_index = snapshot.pause_player_index;

    GuyStorage guy_storage;
    GuyLookup guy_lookup;
    guy_storage.reserve(snapshot.guy_snapshots.size());
    guy_lookup.reserve(snapshot.guy_snapshots.size());
    for (const auto& guy_snapshot : snapshot.guy_snapshots)
    {
        auto applied_guy = std::make_unique<guy>(guy_snapshot.family);
        apply_guy_snapshot(*applied_guy, guy_snapshot);
        guy_lookup[guy_snapshot.guy_id] = applied_guy.get();
        guy_storage[guy_snapshot.guy_id] = std::move(applied_guy);
    }
    world.guy_id_counter = snapshot.guy_id_counter;

    clear_entity_guy_links(world.oblist);
    clear_entity_guy_links(world.fxlist);
    clear_entity_guy_links(world.weaplist);

    EntitySnapshotLookup ob_snapshots;
    EntitySnapshotLookup fx_snapshots;
    EntitySnapshotLookup weap_snapshots;
    build_entity_snapshot_lookup(snapshot.oblist, ob_snapshots);
    build_entity_snapshot_lookup(snapshot.fxlist, fx_snapshots);
    build_entity_snapshot_lookup(snapshot.weaplist, weap_snapshots);

    remove_missing_entities(world, world.oblist, ob_snapshots);
    remove_missing_entities(world, world.fxlist, fx_snapshots);
    remove_missing_entities(world, world.weaplist, weap_snapshots);

    update_existing_entities(world, world.oblist, ob_snapshots);
    update_existing_entities(world, world.fxlist, fx_snapshots);
    update_existing_entities(world, world.weaplist, weap_snapshots);

    create_missing_entities(world, world.oblist, snapshot.oblist);
    create_missing_entities(world, world.fxlist, snapshot.fxlist);
    create_missing_entities(world, world.weaplist, snapshot.weaplist);

    std::unordered_map<std::uint32_t, walker*> live_entities;
    live_entities.reserve(
        world.oblist.size() + world.fxlist.size() + world.weaplist.size());
    append_entities_to_index(world.oblist, live_entities);
    append_entities_to_index(world.fxlist, live_entities);
    append_entities_to_index(world.weaplist, live_entities);

    resolve_cross_references(world, live_entities);
    rebind_guys(world, ob_snapshots, fx_snapshots, weap_snapshots,
                guy_storage, guy_lookup);
    rebuild_obmap(world);
    apply_grid_snapshot(world, snapshot);
    reorder_entity_list(world.oblist, snapshot.oblist);
    reorder_entity_list(world.fxlist, snapshot.fxlist);
    reorder_entity_list(world.weaplist, snapshot.weaplist);
    world.dead_list.clear();
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
    clear_entity_dirty_masks(world);

    // walker construction rolls path_check_counter from the world RNG, so
    // restore the authoritative snapshot state after all apply-side effects.
    world.rng_.state_ = snapshot.rng_state;
}

SimEventBatch drain_sim_events(SimEventLog& log)
{
    SimEventBatch batch;
    batch.sequence = log.current_tick_;
    batch.events = log.drain();
    return batch;
}

} // namespace og::sim
