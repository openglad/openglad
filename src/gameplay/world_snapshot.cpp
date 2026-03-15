#include <openglad/gameplay/world_snapshot.h>

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace
{

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

SimEventBatch drain_sim_events(SimEventLog& log)
{
    SimEventBatch batch;
    batch.sequence = log.current_tick_;
    batch.events = log.drain();
    return batch;
}

} // namespace og::sim
