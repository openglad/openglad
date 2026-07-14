#include <openglad/gameplay/world_snapshot.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/weap.h>
#include <openglad/legacy/base.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <list>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <gtest/gtest.h>

#include "zlib.h"

#include "test_game_world_fixture.h"

namespace {

struct SnapshotWalker final : walker
{
    SnapshotWalker()
        : walker()
    {
    }

    void set_regen_delay(std::int32_t value)
    {
        walker::set_regen_delay(value);
    }

    bool act() override
    {
        return true;
    }
};

struct SnapshotWeapon final : weap
{
    SnapshotWeapon()
        : weap()
    {
    }

    bool act() override
    {
        return true;
    }
};

PixieData make_snapshot_pixie(unsigned char frames = 2,
                              unsigned char w = 16,
                              unsigned char h = 16,
                              unsigned char fill = 0)
{
    const std::size_t size =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(w) *
        static_cast<std::size_t>(h);
    auto* raw = new unsigned char[size];
    std::fill_n(raw, size, fill);
    return PixieData(frames, w, h, raw);
}

const signed char kDefaultAnim[] = {0, -1};
const signed char kSmallSlimeAnim[] = {1, -1};
const signed char kWeaponAnim[] = {0, 1, -1};
const signed char* const kDefaultAnimRows[16] = {
    kDefaultAnim, kDefaultAnim, kDefaultAnim, kDefaultAnim,
    kDefaultAnim, kDefaultAnim, kDefaultAnim, kDefaultAnim,
    kDefaultAnim, kDefaultAnim, kDefaultAnim, kDefaultAnim,
    kDefaultAnim, kDefaultAnim, kDefaultAnim, kDefaultAnim,
};
const signed char* const kSmallSlimeAnimRows[16] = {
    kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim,
    kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim,
    kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim,
    kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim, kSmallSlimeAnim,
};
const signed char* const kWeaponAnimRows[16] = {
    kWeaponAnim, kWeaponAnim, kWeaponAnim, kWeaponAnim,
    kWeaponAnim, kWeaponAnim, kWeaponAnim, kWeaponAnim,
    kWeaponAnim, kWeaponAnim, kWeaponAnim, kWeaponAnim,
    kWeaponAnim, kWeaponAnim, kWeaponAnim, kWeaponAnim,
};

const signed char* const* animation_rows_for_family(std::int32_t family)
{
    if (family == FAMILY_SMALL_SLIME)
        return kSmallSlimeAnimRows;
    if (family == FAMILY_ARROW || family == FAMILY_KNIFE)
        return kWeaponAnimRows;
    return kDefaultAnimRows;
}

const PixieData& pixie_for_family(std::int32_t family)
{
    static PixieData default_pix = make_snapshot_pixie(2, 16, 16, 10);
    static PixieData small_slime_pix = make_snapshot_pixie(2, 12, 12, 20);
    static PixieData weapon_pix = make_snapshot_pixie(2, 8, 8, 30);

    if (family == FAMILY_SMALL_SLIME)
        return small_slime_pix;
    if (family == FAMILY_ARROW || family == FAMILY_KNIFE)
        return weapon_pix;
    return default_pix;
}

void configure_snapshot_test_entity(walker& entity, Order order, std::int32_t family)
{
    const PixieData& pix = pixie_for_family(family);
    entity.set_order_family(order, static_cast<char>(family));
    entity.set_data(pix);
    entity.ani = animation_rows_for_family(family);
    entity.set_sizex(pix.w);
    entity.set_sizey(pix.h);
    entity.set_direct_frame(0);
}

void apply_snapshot_test_derived_stats(walker* entity,
                                       Order order,
                                       std::int32_t family)
{
    if (entity == nullptr || entity->stats() == nullptr)
        return;

    entity->set_stepsize(1.0f + static_cast<float>(family % 3));
    entity->set_normal_stepsize(entity->stepsize());
    entity->set_lineofsight(20 + family);
    entity->set_damage(4.0f + static_cast<float>(family));
    entity->set_fire_frequency(0.5f + static_cast<float>(family) / 10.0f);
    entity->stats()->set_max_hitpoints(10.0f + static_cast<float>(family));
    entity->stats()->set_hitpoints(entity->stats()->max_hitpoints());
    entity->stats()->set_level(static_cast<std::int32_t>(order) + family);
}

void configure_snapshot_test_services(GameWorld& world)
{
    world.entity_factory =
        [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
            std::unique_ptr<walker> entity;
            if (order == Order::Weapon)
                entity = std::make_unique<SnapshotWeapon>();
            else
                entity = std::make_unique<SnapshotWalker>();

            configure_snapshot_test_entity(*entity, order, family);
            apply_snapshot_test_derived_stats(entity.get(), order, family);
            return entity;
        };

    world.entity_configurator =
        [](walker& entity, Order order, std::int32_t family) -> const PixieData* {
            configure_snapshot_test_entity(entity, order, family);
            return &pixie_for_family(family);
        };

    world.entity_derived_stats =
        [](walker* entity, Order order, std::int32_t family) {
            apply_snapshot_test_derived_stats(entity, order, family);
        };
}

bool pile_contains(const std::list<walker*>& pile, const walker* target)
{
    return std::find(pile.begin(), pile.end(), target) != pile.end();
}

void reverse_entity_list(GameWorld::EntityList& entities)
{
    GameWorld::EntityList::Storage detached;
    GameWorld::EntityList::Storage reversed;
    entities.splice_into(detached);
    while (!detached.empty())
    {
        auto it = detached.end();
        --it;
        reversed.splice(reversed.end(), detached, it);
    }
    entities.splice(entities.end(), reversed);
}

std::vector<std::uint32_t> snapshot_ids(
    const std::vector<og::sim::EntitySnapshot>& snapshots)
{
    std::vector<std::uint32_t> ids;
    ids.reserve(snapshots.size());
    for (const auto& snapshot : snapshots)
        ids.push_back(snapshot.entity_id);
    return ids;
}

const og::sim::EntitySnapshotFieldDesc* find_desc(std::uint8_t bit)
{
    const auto it = std::find_if(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [bit](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == bit;
        });
    return it == std::end(og::sim::kEntitySnapshotFields) ? nullptr : &*it;
}

const og::sim::EntitySnapshot* find_entity_snapshot(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::uint32_t entity_id)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    return it == entities.end() ? nullptr : &*it;
}

bool is_removed_entity_sentinel(const og::sim::EntitySnapshot& snapshot)
{
    return snapshot.dirty_mask[0] == 0 && snapshot.dirty_mask[1] == 0;
}

const og::sim::GridTileSnapshot* find_grid_dirty_tile(
    const std::vector<og::sim::GridTileSnapshot>& tiles,
    short x,
    short y)
{
    const auto it = std::find_if(
        tiles.begin(), tiles.end(),
        [x, y](const og::sim::GridTileSnapshot& tile) {
            return tile.x == x && tile.y == y;
        });
    return it == tiles.end() ? nullptr : &*it;
}

void set_mask_bit(
    std::array<std::uint64_t, og::sim::kEntitySnapshotDirtyMaskWords>& mask,
    std::uint8_t bit)
{
    mask[bit / 64] |= (1ULL << (bit % 64));
}

std::uint8_t snapshot_bool(bool value)
{
    return value ? 1U : 0U;
}

void expect_guy_snapshot_matches(const guy& live,
                                 const og::sim::GuySnapshot& snapshot)
{
    EXPECT_EQ(live.id, snapshot.guy_id);
    EXPECT_EQ(live.name, snapshot.name);
    EXPECT_EQ(live.family, snapshot.family);
    EXPECT_EQ(live.strength, snapshot.strength);
    EXPECT_EQ(live.dexterity, snapshot.dexterity);
    EXPECT_EQ(live.constitution, snapshot.constitution);
    EXPECT_EQ(live.intelligence, snapshot.intelligence);
    EXPECT_EQ(live.armor, snapshot.armor);
    EXPECT_EQ(live.exp, snapshot.exp);
    EXPECT_EQ(live.kills, snapshot.kills);
    EXPECT_EQ(live.level_kills, snapshot.level_kills);
    EXPECT_EQ(live.total_damage, snapshot.total_damage);
    EXPECT_EQ(live.total_hits, snapshot.total_hits);
    EXPECT_EQ(live.total_shots, snapshot.total_shots);
    EXPECT_EQ(live.teamnum, snapshot.teamnum);
    EXPECT_FLOAT_EQ(live.scen_damage, snapshot.scen_damage);
    EXPECT_EQ(live.scen_kills, snapshot.scen_kills);
    EXPECT_FLOAT_EQ(live.scen_damage_taken, snapshot.scen_damage_taken);
    EXPECT_FLOAT_EQ(live.scen_min_hp, snapshot.scen_min_hp);
    EXPECT_EQ(live.scen_shots, snapshot.scen_shots);
    EXPECT_EQ(live.scen_hits, snapshot.scen_hits);
    EXPECT_EQ(live.level, snapshot.level);
}

void expect_entity_snapshot_matches(
    const walker& live,
    const og::sim::EntitySnapshot& snapshot,
    const std::array<std::uint64_t, og::sim::kEntitySnapshotDirtyMaskWords>&
        expected_dirty_mask)
{
    SCOPED_TRACE(::testing::Message() << "entity_id=" << live.entity_id());

    ASSERT_NE(nullptr, live.stats());
    const statistics* const stats = live.stats();

    EXPECT_EQ(expected_dirty_mask[0], snapshot.dirty_mask[0]);
    EXPECT_EQ(expected_dirty_mask[1], snapshot.dirty_mask[1]);
    EXPECT_EQ(live.myguy != nullptr ? live.myguy->id : og::sim::kNoGuyId,
              snapshot.guy_id);

    EXPECT_EQ(live.entity_id(), snapshot.entity_id);
    EXPECT_EQ(live.xpos(), snapshot.xpos);
    EXPECT_EQ(live.ypos(), snapshot.ypos);
    EXPECT_EQ(live.sizex(), snapshot.sizex);
    EXPECT_EQ(live.sizey(), snapshot.sizey);
    EXPECT_EQ(live.team_num(), snapshot.team_num);
    EXPECT_EQ(live.real_team_num(), snapshot.real_team_num);
    EXPECT_EQ(live.user(), snapshot.user);
    EXPECT_EQ(live.dead(), snapshot.dead);
    EXPECT_EQ(live.death_called(), snapshot.death_called);
    EXPECT_EQ(live.invulnerable_left(), snapshot.invulnerable_left);
    EXPECT_EQ(live.invisibility_left(), snapshot.invisibility_left);
    EXPECT_EQ(live.flight_left(), snapshot.flight_left);
    EXPECT_EQ(live.bonus_rounds(), snapshot.bonus_rounds);
    EXPECT_EQ(live.order(), snapshot.order);
    EXPECT_EQ(live.family(), snapshot.family);
    EXPECT_EQ(live.frame(), snapshot.frame);
    EXPECT_FLOAT_EQ(live.worldx(), snapshot.worldx);
    EXPECT_FLOAT_EQ(live.worldy(), snapshot.worldy);

    EXPECT_FLOAT_EQ(live.lastx(), snapshot.lastx);
    EXPECT_FLOAT_EQ(live.lasty(), snapshot.lasty);
    EXPECT_FLOAT_EQ(live.stepsize(), snapshot.stepsize);
    EXPECT_FLOAT_EQ(live.normal_stepsize(), snapshot.normal_stepsize);
    EXPECT_EQ(live.curdir(), snapshot.curdir);
    EXPECT_EQ(live.enddir(), snapshot.enddir);
    EXPECT_FLOAT_EQ(live.damage(), snapshot.damage);
    EXPECT_FLOAT_EQ(live.fire_frequency(), snapshot.fire_frequency);
    EXPECT_FLOAT_EQ(live.busy(), snapshot.busy);
    EXPECT_EQ(live.current_weapon(), snapshot.current_weapon);
    EXPECT_EQ(live.default_weapon(), snapshot.default_weapon);
    EXPECT_FLOAT_EQ(live.attack_lunge(), snapshot.attack_lunge);
    EXPECT_FLOAT_EQ(live.attack_lunge_angle(), snapshot.attack_lunge_angle);
    EXPECT_FLOAT_EQ(live.hit_recoil(), snapshot.hit_recoil);
    EXPECT_FLOAT_EQ(live.hit_recoil_angle(), snapshot.hit_recoil_angle);
    EXPECT_FLOAT_EQ(live.last_hitpoints(), snapshot.last_hitpoints);
    EXPECT_EQ(live.action(), snapshot.action);
    EXPECT_EQ(live.act_type(), snapshot.act_type);
    EXPECT_EQ(live.old_act_type(), snapshot.old_act_type);
    EXPECT_EQ(live.ani_type(), snapshot.ani_type);
    EXPECT_EQ(live.cycle(), snapshot.cycle);
    EXPECT_EQ(live.drawcycle(), snapshot.drawcycle);
    EXPECT_EQ(live.current_special(), snapshot.current_special);
    EXPECT_EQ(live.ignore(), snapshot.ignore);
    EXPECT_EQ(snapshot_bool(live.in_act()), snapshot.in_act);
    EXPECT_EQ(live.shifter_down(), snapshot.shifter_down);
    EXPECT_EQ(live.yo_delay(), snapshot.yo_delay);
    EXPECT_EQ(live.skip_exit(), snapshot.skip_exit);
    EXPECT_EQ(live.outline(), snapshot.outline);
    EXPECT_EQ(snapshot_bool(live.hurt_flash()), snapshot.hurt_flash);
    EXPECT_EQ(live.lifetime(), snapshot.lifetime);
    EXPECT_FLOAT_EQ(live.speed_bonus(), snapshot.speed_bonus);
    EXPECT_EQ(live.speed_bonus_left(), snapshot.speed_bonus_left);
    EXPECT_EQ(live.charm_left(), snapshot.charm_left);
    EXPECT_EQ(live.weapons_left(), snapshot.weapons_left);
    EXPECT_EQ(live.keys(), snapshot.keys);
    EXPECT_EQ(live.view_all(), snapshot.view_all);
    EXPECT_EQ(live.lineofsight(), snapshot.lineofsight);
    EXPECT_EQ(live.path_check_counter(), snapshot.path_check_counter);
    EXPECT_EQ(live.regen_delay(), snapshot.regen_delay);
    EXPECT_EQ(live.foe_id(), snapshot.foe_id);
    EXPECT_EQ(live.leader_id(), snapshot.leader_id);
    EXPECT_EQ(live.owner_id(), snapshot.owner_id);
    EXPECT_EQ(live.collide_ob_id(), snapshot.collide_ob_id);

    EXPECT_FLOAT_EQ(stats->hitpoints(), snapshot.hitpoints);
    EXPECT_FLOAT_EQ(stats->max_hitpoints(), snapshot.max_hitpoints);
    EXPECT_FLOAT_EQ(stats->magicpoints(), snapshot.magicpoints);
    EXPECT_FLOAT_EQ(stats->max_magicpoints(), snapshot.max_magicpoints);
    EXPECT_EQ(stats->max_heal_delay(), snapshot.max_heal_delay);
    EXPECT_EQ(stats->current_heal_delay(), snapshot.current_heal_delay);
    EXPECT_EQ(stats->max_magic_delay(), snapshot.max_magic_delay);
    EXPECT_EQ(stats->current_magic_delay(), snapshot.current_magic_delay);
    EXPECT_FLOAT_EQ(stats->magic_per_round(), snapshot.magic_per_round);
    EXPECT_FLOAT_EQ(stats->heal_per_round(), snapshot.heal_per_round);
    EXPECT_FLOAT_EQ(stats->armor(), snapshot.armor);
    EXPECT_EQ(stats->level(), snapshot.level);
    EXPECT_EQ(stats->bit_flags(), snapshot.bit_flags);
    EXPECT_EQ(stats->delete_me(), snapshot.delete_me);
    EXPECT_EQ(stats->frozen_delay(), snapshot.frozen_delay);
    EXPECT_EQ(stats->weapon_cost(), snapshot.weapon_cost);
    for (int i = 0; i < NUM_SPECIALS; ++i)
        EXPECT_EQ(stats->special_cost(i), snapshot.special_cost[i]);
    EXPECT_EQ(stats->old_order(), snapshot.old_order);
    EXPECT_EQ(stats->old_family(), snapshot.old_family);
    EXPECT_EQ(stats->last_distance(), snapshot.last_distance);
    EXPECT_EQ(stats->current_distance(), snapshot.current_distance);
    EXPECT_EQ(stats->controller_id(), snapshot.controller_id);

    const std::int32_t expected_do_bounce =
        live.query_order() == Order::Weapon
            ? static_cast<const weap&>(live).do_bounce()
            : 0;
    EXPECT_EQ(expected_do_bounce, snapshot.do_bounce);
}

void expect_guy_snapshot_eq(const og::sim::GuySnapshot& expected,
                            const og::sim::GuySnapshot& actual)
{
    EXPECT_EQ(expected.guy_id, actual.guy_id);
    EXPECT_EQ(expected.name, actual.name);
    EXPECT_EQ(expected.family, actual.family);
    EXPECT_EQ(expected.strength, actual.strength);
    EXPECT_EQ(expected.dexterity, actual.dexterity);
    EXPECT_EQ(expected.constitution, actual.constitution);
    EXPECT_EQ(expected.intelligence, actual.intelligence);
    EXPECT_EQ(expected.armor, actual.armor);
    EXPECT_EQ(expected.exp, actual.exp);
    EXPECT_EQ(expected.kills, actual.kills);
    EXPECT_EQ(expected.level_kills, actual.level_kills);
    EXPECT_EQ(expected.total_damage, actual.total_damage);
    EXPECT_EQ(expected.total_hits, actual.total_hits);
    EXPECT_EQ(expected.total_shots, actual.total_shots);
    EXPECT_EQ(expected.teamnum, actual.teamnum);
    EXPECT_FLOAT_EQ(expected.scen_damage, actual.scen_damage);
    EXPECT_EQ(expected.scen_kills, actual.scen_kills);
    EXPECT_FLOAT_EQ(expected.scen_damage_taken, actual.scen_damage_taken);
    EXPECT_FLOAT_EQ(expected.scen_min_hp, actual.scen_min_hp);
    EXPECT_EQ(expected.scen_shots, actual.scen_shots);
    EXPECT_EQ(expected.scen_hits, actual.scen_hits);
    EXPECT_EQ(expected.level, actual.level);
    EXPECT_EQ(expected.owner_player_index, actual.owner_player_index);
    EXPECT_EQ(expected.owner_save_slot, actual.owner_save_slot);
}

void expect_entity_snapshot_eq(const og::sim::EntitySnapshot& expected,
                               const og::sim::EntitySnapshot& actual,
                               bool compare_dirty_mask = true)
{
    SCOPED_TRACE(::testing::Message() << "entity_id=" << expected.entity_id);

    if (compare_dirty_mask)
    {
        EXPECT_EQ(expected.dirty_mask[0], actual.dirty_mask[0]);
        EXPECT_EQ(expected.dirty_mask[1], actual.dirty_mask[1]);
    }

    EXPECT_EQ(expected.guy_id, actual.guy_id);
    EXPECT_EQ(expected.entity_id, actual.entity_id);
    EXPECT_EQ(expected.xpos, actual.xpos);
    EXPECT_EQ(expected.ypos, actual.ypos);
    EXPECT_EQ(expected.sizex, actual.sizex);
    EXPECT_EQ(expected.sizey, actual.sizey);
    EXPECT_EQ(expected.sizez, actual.sizez);
    EXPECT_EQ(expected.floor, actual.floor);
    EXPECT_EQ(expected.team_num, actual.team_num);
    EXPECT_EQ(expected.real_team_num, actual.real_team_num);
    EXPECT_EQ(expected.user, actual.user);
    EXPECT_EQ(expected.dead, actual.dead);
    EXPECT_EQ(expected.death_called, actual.death_called);
    EXPECT_EQ(expected.invulnerable_left, actual.invulnerable_left);
    EXPECT_EQ(expected.invisibility_left, actual.invisibility_left);
    EXPECT_EQ(expected.flight_left, actual.flight_left);
    EXPECT_EQ(expected.bonus_rounds, actual.bonus_rounds);
    EXPECT_EQ(expected.order, actual.order);
    EXPECT_EQ(expected.family, actual.family);
    EXPECT_EQ(expected.frame, actual.frame);
    EXPECT_FLOAT_EQ(expected.worldx, actual.worldx);
    EXPECT_FLOAT_EQ(expected.worldy, actual.worldy);
    EXPECT_FLOAT_EQ(expected.worldz, actual.worldz);
    EXPECT_FLOAT_EQ(expected.lastx, actual.lastx);
    EXPECT_FLOAT_EQ(expected.lasty, actual.lasty);
    EXPECT_FLOAT_EQ(expected.vz, actual.vz);
    EXPECT_FLOAT_EQ(expected.stepsize, actual.stepsize);
    EXPECT_FLOAT_EQ(expected.normal_stepsize, actual.normal_stepsize);
    EXPECT_EQ(expected.curdir, actual.curdir);
    EXPECT_EQ(expected.enddir, actual.enddir);
    EXPECT_FLOAT_EQ(expected.damage, actual.damage);
    EXPECT_FLOAT_EQ(expected.fire_frequency, actual.fire_frequency);
    EXPECT_FLOAT_EQ(expected.busy, actual.busy);
    EXPECT_EQ(expected.current_weapon, actual.current_weapon);
    EXPECT_EQ(expected.default_weapon, actual.default_weapon);
    EXPECT_FLOAT_EQ(expected.attack_lunge, actual.attack_lunge);
    EXPECT_FLOAT_EQ(expected.attack_lunge_angle, actual.attack_lunge_angle);
    EXPECT_FLOAT_EQ(expected.hit_recoil, actual.hit_recoil);
    EXPECT_FLOAT_EQ(expected.hit_recoil_angle, actual.hit_recoil_angle);
    EXPECT_FLOAT_EQ(expected.last_hitpoints, actual.last_hitpoints);
    EXPECT_EQ(expected.action, actual.action);
    EXPECT_EQ(expected.act_type, actual.act_type);
    EXPECT_EQ(expected.old_act_type, actual.old_act_type);
    EXPECT_EQ(expected.ani_type, actual.ani_type);
    EXPECT_EQ(expected.cycle, actual.cycle);
    EXPECT_EQ(expected.drawcycle, actual.drawcycle);
    EXPECT_EQ(expected.current_special, actual.current_special);
    EXPECT_EQ(expected.ignore, actual.ignore);
    EXPECT_EQ(expected.in_act, actual.in_act);
    EXPECT_EQ(expected.shifter_down, actual.shifter_down);
    EXPECT_EQ(expected.yo_delay, actual.yo_delay);
    EXPECT_EQ(expected.skip_exit, actual.skip_exit);
    EXPECT_EQ(expected.outline, actual.outline);
    EXPECT_EQ(expected.hurt_flash, actual.hurt_flash);
    EXPECT_EQ(expected.lifetime, actual.lifetime);
    EXPECT_FLOAT_EQ(expected.speed_bonus, actual.speed_bonus);
    EXPECT_EQ(expected.speed_bonus_left, actual.speed_bonus_left);
    EXPECT_EQ(expected.charm_left, actual.charm_left);
    EXPECT_EQ(expected.weapons_left, actual.weapons_left);
    EXPECT_EQ(expected.keys, actual.keys);
    EXPECT_EQ(expected.view_all, actual.view_all);
    EXPECT_EQ(expected.lineofsight, actual.lineofsight);
    EXPECT_EQ(expected.path_check_counter, actual.path_check_counter);
    EXPECT_EQ(expected.regen_delay, actual.regen_delay);
    EXPECT_EQ(expected.foe_id, actual.foe_id);
    EXPECT_EQ(expected.leader_id, actual.leader_id);
    EXPECT_EQ(expected.owner_id, actual.owner_id);
    EXPECT_EQ(expected.collide_ob_id, actual.collide_ob_id);
    EXPECT_FLOAT_EQ(expected.hitpoints, actual.hitpoints);
    EXPECT_FLOAT_EQ(expected.max_hitpoints, actual.max_hitpoints);
    EXPECT_FLOAT_EQ(expected.magicpoints, actual.magicpoints);
    EXPECT_FLOAT_EQ(expected.max_magicpoints, actual.max_magicpoints);
    EXPECT_EQ(expected.max_heal_delay, actual.max_heal_delay);
    EXPECT_EQ(expected.current_heal_delay, actual.current_heal_delay);
    EXPECT_EQ(expected.max_magic_delay, actual.max_magic_delay);
    EXPECT_EQ(expected.current_magic_delay, actual.current_magic_delay);
    EXPECT_FLOAT_EQ(expected.magic_per_round, actual.magic_per_round);
    EXPECT_FLOAT_EQ(expected.heal_per_round, actual.heal_per_round);
    EXPECT_FLOAT_EQ(expected.armor, actual.armor);
    EXPECT_EQ(expected.level, actual.level);
    EXPECT_EQ(expected.bit_flags, actual.bit_flags);
    EXPECT_EQ(expected.delete_me, actual.delete_me);
    EXPECT_EQ(expected.frozen_delay, actual.frozen_delay);
    EXPECT_EQ(expected.weapon_cost, actual.weapon_cost);
    for (int i = 0; i < NUM_SPECIALS; ++i)
        EXPECT_EQ(expected.special_cost[i], actual.special_cost[i]);
    EXPECT_EQ(expected.old_order, actual.old_order);
    EXPECT_EQ(expected.old_family, actual.old_family);
    EXPECT_EQ(expected.last_distance, actual.last_distance);
    EXPECT_EQ(expected.current_distance, actual.current_distance);
    EXPECT_EQ(expected.controller_id, actual.controller_id);
    EXPECT_EQ(expected.do_bounce, actual.do_bounce);
    EXPECT_EQ(expected.spawn_x, actual.spawn_x);
    EXPECT_EQ(expected.spawn_y, actual.spawn_y);
    EXPECT_EQ(expected.spawn_floor, actual.spawn_floor);
}

void expect_world_snapshot_eq(const og::sim::WorldSnapshot& expected,
                              const og::sim::WorldSnapshot& actual,
                              bool compare_dirty_mask = true)
{
    EXPECT_EQ(expected.tick_count, actual.tick_count);
    EXPECT_EQ(expected.rng_state, actual.rng_state);
    EXPECT_EQ(expected.level_tick_count, actual.level_tick_count);
    EXPECT_EQ(expected.level_done, actual.level_done);
    EXPECT_EQ(expected.game_ended, actual.game_ended);
    EXPECT_EQ(expected.end, actual.end);
    EXPECT_EQ(expected.retry, actual.retry);
    EXPECT_EQ(expected.next_level, actual.next_level);
    EXPECT_EQ(expected.ending, actual.ending);
    EXPECT_EQ(expected.enemy_freeze, actual.enemy_freeze);
    EXPECT_EQ(expected.timer_wait, actual.timer_wait);
    EXPECT_EQ(expected.living_count, actual.living_count);
    EXPECT_FLOAT_EQ(expected.control_hp, actual.control_hp);
    EXPECT_EQ(expected.my_team, actual.my_team);
    EXPECT_EQ(expected.allied_mode, actual.allied_mode);
    EXPECT_EQ(expected.difficulty, actual.difficulty);
    EXPECT_EQ(expected.withdraw_requested, actual.withdraw_requested);
    EXPECT_EQ(expected.withdraw_level, actual.withdraw_level);
    EXPECT_EQ(expected.guy_id_counter, actual.guy_id_counter);
    EXPECT_EQ(expected.current_palette_id, actual.current_palette_id);
    EXPECT_EQ(expected.pending_exit_prompt, actual.pending_exit_prompt);
    EXPECT_EQ(expected.paused, actual.paused);
    EXPECT_EQ(expected.pause_player_index, actual.pause_player_index);
    EXPECT_EQ(expected.weather, actual.weather);
    EXPECT_EQ(expected.respawn_mode, actual.respawn_mode);
    EXPECT_EQ(expected.generator_rate, actual.generator_rate);
    EXPECT_EQ(expected.grid_width, actual.grid_width);
    EXPECT_EQ(expected.grid_height, actual.grid_height);
    EXPECT_EQ(expected.grid_dirty, actual.grid_dirty);
    EXPECT_EQ(expected.grid_full_resend, actual.grid_full_resend);
    EXPECT_EQ(expected.full_grid_data, actual.full_grid_data);
    EXPECT_EQ(expected.grid_dirty_tiles.size(), actual.grid_dirty_tiles.size());
    for (std::size_t i = 0; i < expected.grid_dirty_tiles.size(); ++i)
    {
        EXPECT_EQ(expected.grid_dirty_tiles[i].x, actual.grid_dirty_tiles[i].x);
        EXPECT_EQ(expected.grid_dirty_tiles[i].y, actual.grid_dirty_tiles[i].y);
        EXPECT_EQ(expected.grid_dirty_tiles[i].value,
                  actual.grid_dirty_tiles[i].value);
    }
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(expected.m_score[i], actual.m_score[i]);

    ASSERT_EQ(expected.guy_snapshots.size(), actual.guy_snapshots.size());
    for (std::size_t i = 0; i < expected.guy_snapshots.size(); ++i)
        expect_guy_snapshot_eq(expected.guy_snapshots[i], actual.guy_snapshots[i]);

    EXPECT_EQ(snapshot_ids(expected.oblist), snapshot_ids(actual.oblist));
    EXPECT_EQ(snapshot_ids(expected.fxlist), snapshot_ids(actual.fxlist));
    EXPECT_EQ(snapshot_ids(expected.weaplist), snapshot_ids(actual.weaplist));

    ASSERT_EQ(expected.oblist.size(), actual.oblist.size());
    for (std::size_t i = 0; i < expected.oblist.size(); ++i)
        expect_entity_snapshot_eq(expected.oblist[i], actual.oblist[i], compare_dirty_mask);

    ASSERT_EQ(expected.fxlist.size(), actual.fxlist.size());
    for (std::size_t i = 0; i < expected.fxlist.size(); ++i)
        expect_entity_snapshot_eq(expected.fxlist[i], actual.fxlist[i], compare_dirty_mask);

    ASSERT_EQ(expected.weaplist.size(), actual.weaplist.size());
    for (std::size_t i = 0; i < expected.weaplist.size(); ++i)
        expect_entity_snapshot_eq(expected.weaplist[i], actual.weaplist[i], compare_dirty_mask);

    EXPECT_EQ(expected.removed_entity_ids, actual.removed_entity_ids);
}

void fill_world_grid(GameWorld& world, std::uint8_t value)
{
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                value);
}

std::vector<std::uint8_t> zlib_compress_for_test(
    const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> compressed(compressBound(static_cast<uLong>(payload.size())));
    uLongf compressed_size = static_cast<uLongf>(compressed.size());
    const int rc = compress2(compressed.data(),
                             &compressed_size,
                             payload.data(),
                             static_cast<uLong>(payload.size()),
                             Z_DEFAULT_COMPRESSION);
    EXPECT_EQ(Z_OK, rc);
    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

std::vector<std::uint8_t> zlib_decompress_for_test(
    const std::uint8_t* data,
    std::size_t size)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    EXPECT_EQ(Z_OK, inflateInit(&stream));

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 256> chunk{};
    int rc = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK) {
            EXPECT_EQ(Z_STREAM_END, rc);
        }
        output.insert(output.end(),
                      chunk.begin(),
                      chunk.begin() + (chunk.size() - stream.avail_out));
    } while (rc != Z_STREAM_END);

    EXPECT_EQ(Z_OK, inflateEnd(&stream));
    return output;
}

std::size_t payload_length_from_header_for_test(
    const std::vector<std::uint8_t>& bytes)
{
    EXPECT_GE(bytes.size(), og::sim::kTransportHeaderSize);
    return static_cast<std::size_t>(bytes[2]) |
           (static_cast<std::size_t>(bytes[3]) << 8);
}

bool delta_payload_is_uncompressed_for_test(
    const std::vector<std::uint8_t>& bytes)
{
    EXPECT_GE(bytes.size(),
              og::sim::kTransportHeaderSize + og::sim::kDeltaPayloadHeaderSize);
    return (bytes[og::sim::kTransportHeaderSize] &
            og::sim::kDeltaPayloadUncompressedFlag) != 0;
}

std::vector<std::uint8_t> decode_delta_payload_for_test(
    const std::vector<std::uint8_t>& bytes)
{
    const std::size_t payload_length = payload_length_from_header_for_test(bytes);
    EXPECT_EQ(bytes.size(), og::sim::kTransportHeaderSize + payload_length);
    EXPECT_GE(payload_length, og::sim::kDeltaPayloadHeaderSize);

    const bool payload_is_uncompressed =
        delta_payload_is_uncompressed_for_test(bytes);
    const std::size_t wire_payload_length =
        payload_length - og::sim::kDeltaPayloadHeaderSize;
    const std::uint8_t* const wire_payload =
        bytes.data() + og::sim::kTransportHeaderSize +
        og::sim::kDeltaPayloadHeaderSize;

    if (payload_is_uncompressed)
    {
        return std::vector<std::uint8_t>(wire_payload,
                                         wire_payload + wire_payload_length);
    }

    return zlib_decompress_for_test(wire_payload, wire_payload_length);
}

void append_u32_for_test(std::vector<std::uint8_t>& bytes,
                         std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

std::vector<std::uint8_t> frame_event_batch_payload_for_test(
    std::uint8_t message_type,
    const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(og::sim::kTransportHeaderSize + payload.size());
    bytes.push_back(og::sim::kSnapshotProtocolVersion);
    bytes.push_back(message_type);
    bytes.push_back(static_cast<std::uint8_t>(payload.size() & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((payload.size() >> 8) & 0xffu));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

} // namespace

TEST(WorldSnapshot, entity_snapshot_layout_matches_dirty_field_table)
{
    static_assert(std::is_standard_layout_v<og::sim::EntitySnapshot>);
    static_assert(std::is_trivially_copyable_v<og::sim::EntitySnapshot>);
    EXPECT_EQ(91u, og::sim::kEntitySnapshotTableFieldCount);
    EXPECT_EQ(2u, og::sim::kEntitySnapshotManualFieldCount);
    EXPECT_EQ(og::dirty::FIELD_COUNT, og::sim::kEntitySnapshotTrackedFieldCount);

    std::array<bool, og::dirty::FIELD_COUNT> seen_bits = {};
    for (const og::sim::EntitySnapshotFieldDesc& desc :
         og::sim::kEntitySnapshotFields) {
        ASSERT_LT(desc.bit_index, og::dirty::FIELD_COUNT);
        EXPECT_FALSE(og::sim::entity_snapshot_field_is_manual(desc.bit_index));
        EXPECT_FALSE(seen_bits[desc.bit_index]);
        seen_bits[desc.bit_index] = true;
        EXPECT_GT(desc.size, 0);
        EXPECT_LE(static_cast<std::size_t>(desc.snap_offset) + desc.size,
                  sizeof(og::sim::EntitySnapshot));
    }

    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit) {
        if (og::sim::entity_snapshot_field_is_manual(bit)) {
            EXPECT_FALSE(seen_bits[bit]);
        } else {
            EXPECT_TRUE(seen_bits[bit]);
        }
    }

    const auto* entity_id_desc = find_desc(og::dirty::BIT_ENTITY_ID);
    ASSERT_NE(nullptr, entity_id_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, entity_id),
              entity_id_desc->snap_offset);

    const auto* special_cost_desc = find_desc(og::dirty::BIT_SPECIAL_COST);
    ASSERT_NE(nullptr, special_cost_desc);
    EXPECT_EQ(offsetof(og::sim::EntitySnapshot, special_cost),
              special_cost_desc->snap_offset);
    EXPECT_EQ(sizeof(std::uint16_t) * NUM_SPECIALS, special_cost_desc->size);
}

TEST(WorldSnapshot, manual_entity_fields_stay_out_of_the_generic_field_table)
{
    const bool regen_delay_in_field_table = std::any_of(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == og::dirty::BIT_REGEN_DELAY;
        });
    const bool do_bounce_in_field_table = std::any_of(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == og::dirty::BIT_DO_BOUNCE;
        });

    EXPECT_FALSE(regen_delay_in_field_table);
    EXPECT_FALSE(do_bounce_in_field_table);
    EXPECT_TRUE(
        og::sim::entity_snapshot_field_is_manual(og::dirty::BIT_REGEN_DELAY));
    EXPECT_TRUE(
        og::sim::entity_snapshot_field_is_manual(og::dirty::BIT_DO_BOUNCE));
}

TEST(WorldSnapshot, guy_linkage_is_not_dirty_mask_tracked)
{
    const bool guy_id_in_field_table = std::any_of(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.snap_offset == offsetof(og::sim::EntitySnapshot, guy_id);
        });

    EXPECT_FALSE(guy_id_in_field_table);
}

TEST(WorldSnapshot, guy_linkage_uses_negative_sentinel_for_unlinked_entities)
{
    og::sim::EntitySnapshot npc_snapshot;
    og::sim::GuySnapshot guy_snapshot;
    guy_snapshot.guy_id = 0;

    EXPECT_EQ(og::sim::kNoGuyId, npc_snapshot.guy_id);
    EXPECT_NE(guy_snapshot.guy_id, npc_snapshot.guy_id);
}

TEST(WorldSnapshot, world_snapshot_can_hold_world_and_guy_state)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = 42;
    snapshot.rng_state = 1234;
    snapshot.level_tick_count = 7;
    snapshot.current_palette_id = 1;
    snapshot.pending_exit_prompt = true;
    snapshot.paused = true;
    snapshot.pause_player_index = 2;
    snapshot.grid_width = 8;
    snapshot.grid_height = 8;
    snapshot.grid_dirty = true;
    snapshot.grid_full_resend = false;
    snapshot.grid_dirty_tiles.push_back({3, 4, 5});
    snapshot.removed_entity_ids.push_back(17);

    og::sim::GuySnapshot guy_snapshot;
    guy_snapshot.guy_id = 9;
    guy_snapshot.name = "Aldo";
    guy_snapshot.exp = 123;
    guy_snapshot.scen_damage = 4.5f;
    snapshot.guy_snapshots.push_back(guy_snapshot);

    og::sim::EntitySnapshot entity_snapshot;
    entity_snapshot.guy_id = 9;
    entity_snapshot.entity_id = 17;
    entity_snapshot.order = Order::Living;
    entity_snapshot.family = 3;
    entity_snapshot.special_cost[0] = 11;
    snapshot.oblist.push_back(entity_snapshot);

    ASSERT_EQ(1u, snapshot.guy_snapshots.size());
    EXPECT_EQ(9, snapshot.guy_snapshots.front().guy_id);
    EXPECT_EQ("Aldo", snapshot.guy_snapshots.front().name);
    ASSERT_EQ(1u, snapshot.oblist.size());
    EXPECT_EQ(17u, snapshot.oblist.front().entity_id);
    EXPECT_EQ(9, snapshot.oblist.front().guy_id);
    EXPECT_EQ(11u, snapshot.oblist.front().special_cost[0]);
    ASSERT_EQ(1u, snapshot.grid_dirty_tiles.size());
    EXPECT_EQ(5u, snapshot.grid_dirty_tiles.front().value);
}

TEST(WorldSnapshot, capture_snapshot_matches_live_world_and_drains_bookkeeping)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        std::unique_ptr<walker> entity;
        if (order == Order::Weapon)
            entity = std::make_unique<SnapshotWeapon>();
        else
            entity = std::make_unique<SnapshotWalker>();

        entity->set_order_family(order, static_cast<char>(family));
        entity->set_sizex(16);
        entity->set_sizey(16);
        return entity;
    };

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    walker* leader = world.add_ob(Order::Living, FAMILY_ORC);
    walker* owner = world.add_ob(Order::Living, FAMILY_ORC);
    walker* collide = world.add_ob(Order::Living, FAMILY_ORC);
    walker* controller = world.add_ob(Order::Living, FAMILY_ORC);
    walker* fx_entity = world.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, leader);
    ASSERT_NE(nullptr, owner);
    ASSERT_NE(nullptr, collide);
    ASSERT_NE(nullptr, controller);
    ASSERT_NE(nullptr, fx_entity);
    ASSERT_NE(nullptr, weapon);

    world.tick();

    actor->setxy(48, 64);
    actor->setworldxy(48.5f, 64.25f);
    actor->set_sizex(18);
    actor->set_sizey(20);
    actor->set_team_num(2);
    actor->set_real_team_num(3);
    actor->set_user(1);
    actor->set_dead(2);
    actor->set_death_called(1);
    actor->set_invulnerable_left(12);
    actor->set_invisibility_left(13);
    actor->set_flight_left(14);
    actor->set_bonus_rounds(15);
    actor->set_direct_frame(6);
    actor->set_lastx(1.25f);
    actor->set_lasty(-0.5f);
    actor->set_stepsize(2.5f);
    actor->set_normal_stepsize(2.0f);
    actor->set_curdir(FACE_RIGHT);
    actor->set_enddir(FACE_LEFT);
    actor->set_damage(9.5f);
    actor->set_fire_frequency(3.0f);
    actor->set_busy(1.0f);
    actor->set_current_weapon(FAMILY_ARROW);
    actor->set_default_weapon(FAMILY_KNIFE);
    actor->set_attack_lunge(4.0f);
    actor->set_attack_lunge_angle(0.75f);
    actor->set_hit_recoil(2.0f);
    actor->set_hit_recoil_angle(1.25f);
    actor->set_last_hitpoints(17.0f);
    actor->set_action(3);
    actor->set_act_type_state(ACT_CONTROL);
    actor->set_old_act_type(ACT_RANDOM);
    actor->set_ani_type(ANI_WALK);
    actor->set_cycle(2);
    actor->set_drawcycle(3);
    actor->set_current_special(1);
    actor->set_ignore(0);
    actor->set_in_act(true);
    actor->set_shifter_down(1);
    actor->set_yo_delay(4);
    actor->set_skip_exit(5);
    actor->set_outline(1);
    actor->set_hurt_flash(true);
    actor->set_lifetime(123);
    actor->set_speed_bonus(1.5f);
    actor->set_speed_bonus_left(22);
    actor->set_charm_left(7);
    actor->set_weapons_left(8);
    actor->set_keys(0x1234);
    actor->set_view_all(1);
    actor->set_lineofsight(33);
    actor->set_path_check_counter(44);
    static_cast<SnapshotWalker*>(actor)->set_regen_delay(73);

    actor->stats()->set_hitpoints(19.0f);
    actor->stats()->set_max_hitpoints(20.0f);
    actor->stats()->set_magicpoints(6.0f);
    actor->stats()->set_max_magicpoints(7.0f);
    actor->stats()->set_max_heal_delay(101);
    actor->stats()->set_current_heal_delay(51);
    actor->stats()->set_max_magic_delay(201);
    actor->stats()->set_current_magic_delay(91);
    actor->stats()->set_magic_per_round(0.5f);
    actor->stats()->set_heal_per_round(0.25f);
    actor->stats()->set_armor(4.0f);
    actor->stats()->set_level(9);
    actor->stats()->set_bit_flags(BIT_FORESTWALK | BIT_MAGICAL);
    actor->stats()->set_delete_me(1);
    actor->stats()->set_frozen_delay(2);
    actor->stats()->set_weapon_cost(3);
    actor->stats()->set_special_cost(0, 11);
    actor->stats()->set_special_cost(1, 12);
    actor->stats()->set_special_cost(2, 13);
    actor->stats()->set_special_cost(3, 14);
    actor->stats()->set_special_cost(4, 15);
    actor->stats()->set_special_cost(5, 16);
    actor->stats()->set_old_order(Order::Weapon);
    actor->stats()->set_old_family(FAMILY_ARROW);
    actor->stats()->set_last_distance(99);
    actor->stats()->set_current_distance(88);

    auto player_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    player_guy->name = "Aldo";
    player_guy->strength = 11;
    player_guy->dexterity = 12;
    player_guy->constitution = 13;
    player_guy->intelligence = 14;
    player_guy->armor = 15;
    player_guy->exp = 1234;
    player_guy->kills = 9;
    player_guy->level_kills = 17;
    player_guy->total_damage = 42;
    player_guy->total_hits = 8;
    player_guy->total_shots = 10;
    player_guy->teamnum = 2;
    player_guy->scen_damage = 5.5f;
    player_guy->scen_kills = 6;
    player_guy->scen_damage_taken = 2.0f;
    player_guy->scen_min_hp = 11.0f;
    player_guy->scen_shots = 7;
    player_guy->scen_hits = 4;
    player_guy->level = 3;
    const int guy_id = player_guy->id;
    actor->set_owned_myguy(std::move(player_guy));

    static_cast<weap*>(weapon)->set_do_bounce(7);

    actor->set_foe(foe);
    actor->set_leader(leader);
    actor->set_owner(owner);
    actor->set_collide_ob(collide);
    actor->stats()->set_controller(controller);

    actor->clear_dirty();
    weapon->clear_dirty();
    fx_entity->clear_dirty();
    actor->set_foe_id(0);
    actor->set_leader_id(0);
    actor->set_owner_id(0);
    actor->set_collide_ob_id(0);
    actor->stats()->set_controller_id(0);

    std::array<std::uint64_t, og::sim::kEntitySnapshotDirtyMaskWords>
        actor_expected_dirty = {};
    set_mask_bit(actor_expected_dirty, og::dirty::BIT_FOE_ID);
    set_mask_bit(actor_expected_dirty, og::dirty::BIT_LEADER_ID);
    set_mask_bit(actor_expected_dirty, og::dirty::BIT_OWNER_ID);
    set_mask_bit(actor_expected_dirty, og::dirty::BIT_COLLIDE_OB_ID);
    set_mask_bit(actor_expected_dirty, og::dirty::BIT_CONTROLLER_ID);
    const std::array<std::uint64_t, og::sim::kEntitySnapshotDirtyMaskWords>
        clean_expected_dirty = {};

    walker* removed = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, removed);
    const std::uint32_t removed_id = removed->entity_id();
    ASSERT_EQ(1, world.remove_ob(removed));

    world.tick_count_ = 42;
    world.rng_.state_ = 777;
    world.set_level_tick_count(7);
    world.game_ended = true;
    world.level_done = 1;
    world.end = 1;
    world.retry = true;
    world.next_level = 8;
    world.ending = 2;
    world.enemy_freeze = 3;
    world.timer_wait = 4;
    world.control_hp = 15.5f;
    world.withdraw_requested = true;
    world.withdraw_level = 9;
    world.guy_id_counter = 123;
    world.current_palette_id = 1;
    world.pending_exit_prompt = true;
    world.paused = true;
    world.pause_player_index = 2;
    world.set_weather(WeatherKind::Rain);
    world.m_score[0] = 100;
    world.m_score[1] = 200;
    world.m_score[2] = 300;
    world.m_score[3] = 400;

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    EXPECT_EQ(world.tick_count_, snapshot.tick_count);
    EXPECT_EQ(world.rng_.state_, snapshot.rng_state);
    EXPECT_EQ(world.level_tick_count(), snapshot.level_tick_count);
    EXPECT_EQ(world.level_done, snapshot.level_done);
    EXPECT_EQ(world.game_ended, snapshot.game_ended);
    EXPECT_EQ(world.end, snapshot.end);
    EXPECT_EQ(world.retry, snapshot.retry);
    EXPECT_EQ(world.next_level, snapshot.next_level);
    EXPECT_EQ(world.ending, snapshot.ending);
    EXPECT_EQ(world.enemy_freeze, snapshot.enemy_freeze);
    EXPECT_EQ(world.timer_wait, snapshot.timer_wait);
    EXPECT_EQ(world.living_count, snapshot.living_count);
    EXPECT_FLOAT_EQ(world.control_hp, snapshot.control_hp);
    EXPECT_EQ(world.withdraw_requested, snapshot.withdraw_requested);
    EXPECT_EQ(world.withdraw_level, snapshot.withdraw_level);
    EXPECT_EQ(world.guy_id_counter, snapshot.guy_id_counter);
    EXPECT_EQ(world.current_palette_id, snapshot.current_palette_id);
    EXPECT_EQ(world.pending_exit_prompt, snapshot.pending_exit_prompt);
    EXPECT_EQ(world.paused, snapshot.paused);
    EXPECT_EQ(world.pause_player_index, snapshot.pause_player_index);
    EXPECT_EQ(static_cast<std::uint8_t>(world.weather()), snapshot.weather);
    EXPECT_EQ(world.m_score[0], snapshot.m_score[0]);
    EXPECT_EQ(world.m_score[3], snapshot.m_score[3]);

    EXPECT_EQ(world.oblist.size(), snapshot.oblist.size());
    EXPECT_EQ(world.fxlist.size(), snapshot.fxlist.size());
    EXPECT_EQ(world.weaplist.size(), snapshot.weaplist.size());
    ASSERT_EQ(1u, snapshot.guy_snapshots.size());
    expect_guy_snapshot_matches(*actor->myguy, snapshot.guy_snapshots.front());

    const og::sim::EntitySnapshot* actor_snapshot =
        find_entity_snapshot(snapshot.oblist, actor->entity_id());
    const og::sim::EntitySnapshot* weapon_snapshot =
        find_entity_snapshot(snapshot.weaplist, weapon->entity_id());
    const og::sim::EntitySnapshot* fx_snapshot =
        find_entity_snapshot(snapshot.fxlist, fx_entity->entity_id());
    ASSERT_NE(nullptr, actor_snapshot);
    ASSERT_NE(nullptr, weapon_snapshot);
    ASSERT_NE(nullptr, fx_snapshot);

    EXPECT_EQ(guy_id, actor_snapshot->guy_id);
    EXPECT_EQ(foe->entity_id(), actor_snapshot->foe_id);
    EXPECT_EQ(leader->entity_id(), actor_snapshot->leader_id);
    EXPECT_EQ(owner->entity_id(), actor_snapshot->owner_id);
    EXPECT_EQ(collide->entity_id(), actor_snapshot->collide_ob_id);
    EXPECT_EQ(controller->entity_id(), actor_snapshot->controller_id);

    expect_entity_snapshot_matches(*actor, *actor_snapshot, actor_expected_dirty);
    expect_entity_snapshot_matches(*weapon, *weapon_snapshot,
                                   clean_expected_dirty);
    expect_entity_snapshot_matches(*fx_entity, *fx_snapshot,
                                   clean_expected_dirty);

    EXPECT_NE(snapshot.removed_entity_ids.end(),
              std::find(snapshot.removed_entity_ids.begin(),
                        snapshot.removed_entity_ids.end(),
                        removed_id));
    EXPECT_TRUE(world.removed_entity_ids().empty());

    EXPECT_EQ(0ULL, actor->dirty_mask_word(0));
    EXPECT_EQ(0ULL, actor->dirty_mask_word(1));
    EXPECT_EQ(0ULL, weapon->dirty_mask_word(0));
    EXPECT_EQ(0ULL, weapon->dirty_mask_word(1));
    EXPECT_EQ(0ULL, fx_entity->dirty_mask_word(0));
    EXPECT_EQ(0ULL, fx_entity->dirty_mask_word(1));
}

TEST(WorldSnapshot, keyframe_capture_marks_all_fields_and_sends_full_grid)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        if (order == Order::Weapon)
        {
            auto entity = std::make_unique<SnapshotWeapon>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
            return entity;
        }

        auto entity = std::make_unique<SnapshotWalker>();
        entity->set_order_family(order, static_cast<char>(family));
        entity->set_sizex(16);
        entity->set_sizey(16);
        return entity;
    };

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->clear_dirty();

    const og::sim::WorldSnapshot snapshot = og::sim::capture_keyframe_snapshot(world);
    ASSERT_EQ(1u, snapshot.oblist.size());
    EXPECT_EQ(~0ULL, snapshot.oblist.front().dirty_mask[0]);
    EXPECT_EQ(~0ULL, snapshot.oblist.front().dirty_mask[1]);
    EXPECT_TRUE(snapshot.grid_dirty);
    EXPECT_TRUE(snapshot.grid_full_resend);
    EXPECT_EQ(static_cast<std::size_t>(world.grid.w) * world.grid.h,
              snapshot.full_grid_data.size());
    EXPECT_EQ(0ULL, actor->dirty_mask_word(0));
    EXPECT_EQ(0ULL, actor->dirty_mask_word(1));
}

TEST(WorldSnapshot, capture_snapshot_preserves_hurt_flash_state)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->set_hurt_flash(true);

    const og::sim::WorldSnapshot first = og::sim::capture_snapshot(world);
    const og::sim::EntitySnapshot* first_actor =
        find_entity_snapshot(first.oblist, actor->entity_id());
    ASSERT_NE(nullptr, first_actor);
    EXPECT_EQ(1u, first_actor->hurt_flash);
    EXPECT_TRUE(actor->hurt_flash());
    EXPECT_EQ(0ULL, actor->dirty_mask_word(0));
    EXPECT_EQ(0ULL, actor->dirty_mask_word(1));
}

TEST(WorldSnapshot, weapon_ordered_base_walker_captures_do_bounce_as_zero)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.entity_factory = [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
        auto entity = std::make_unique<SnapshotWalker>();
        entity->set_order_family(order, static_cast<char>(family));
        entity->set_sizex(16);
        entity->set_sizey(16);
        return entity;
    };

    walker* weapon_like = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, weapon_like);
    ASSERT_EQ(Order::Weapon, weapon_like->query_order());

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    ASSERT_EQ(1u, snapshot.weaplist.size());
    EXPECT_EQ(0, snapshot.weaplist.front().do_bounce);
}

TEST(WorldSnapshot, drain_sim_events_moves_events_out_of_the_log)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 99;
    log.push_sound(7);
    log.push_notification("snapshot");

    const og::sim::SimEventBatch batch = og::sim::drain_sim_events(log);

    EXPECT_EQ(99u, batch.sequence);
    ASSERT_EQ(2u, batch.events.size());
    EXPECT_EQ(og::sim::EventKind::PlaySound, batch.events[0].kind);
    EXPECT_EQ(og::sim::EventKind::Notification, batch.events[1].kind);
    EXPECT_TRUE(log.empty());
}

TEST(WorldSnapshot, split_event_batches_uses_shared_game_flow_rules)
{
    og::sim::SimEventBatch source;
    source.sequence = 17u;
    source.events.push_back({
        .tick = 17u,
        .kind = og::sim::EventKind::EndGame,
        .a = 1u,
        .b = 2u,
        .text = {},
    });
    source.events.push_back({
        .tick = 17u,
        .kind = og::sim::EventKind::RequestRedraw,
        .a = 0u,
        .b = 0u,
        .text = {},
    });
    source.events.push_back({
        .tick = 17u,
        .kind = og::sim::EventKind::ScoreChange,
        .a = 3u,
        .b = 125u,
        .text = {},
    });

    og::sim::SimEventBatch sim_batch;
    og::sim::GameFlowEventBatch game_flow_batch;
    og::sim::split_event_batches(source, sim_batch, game_flow_batch);

    EXPECT_EQ(17u, sim_batch.sequence);
    EXPECT_EQ(17u, game_flow_batch.sequence);
    ASSERT_EQ(1u, sim_batch.events.size());
    ASSERT_EQ(2u, game_flow_batch.events.size());
    EXPECT_EQ(og::sim::EventKind::RequestRedraw, sim_batch.events[0].kind);
    EXPECT_EQ(og::sim::EventKind::ScoreChange, game_flow_batch.events[0].kind);
    EXPECT_EQ(og::sim::EventKind::EndGame, game_flow_batch.events[1].kind);
}

TEST(WorldSnapshot, explosion_death_emits_damage_tile_event)
{
    // Master parity: an exploding effect routes its tile damage through the
    // sim event log (EventKind::DamageTile) instead of mutating the grid
    // directly. The deterministic sim emits the event; the authoritative
    // server (apply_authoritative_event_state) — or master's screen consumer —
    // applies it to the grid. This keeps the headless sim's grid identical to
    // master's headless sim (neither damages the tile inline) while preserving
    // the networking grid-sync once the event is applied.
    TestGameWorld fx;
    GameWorld& world = fx.world();

    walker* explosion = world.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, explosion);

    explosion->set_owner(nullptr);
    explosion->setxy(4 * GRID_SIZE, 5 * GRID_SIZE);
    explosion->set_dead(1);

    const short damage_x =
        static_cast<short>(explosion->xpos() + (explosion->sizex() / 2));
    const short damage_y =
        static_cast<short>(explosion->ypos() + (explosion->sizey() / 2));
    const short tile_x = static_cast<short>(damage_x / GRID_SIZE);
    const short tile_y = static_cast<short>(damage_y / GRID_SIZE);
    const std::size_t tile_index =
        static_cast<std::size_t>(tile_y) * world.grid.w + tile_x;
    world.grid.data[tile_index] = PIX_GRASS1;

    (void)explosion->death();

    // death() emits a DamageTile event and does NOT mutate the grid inline.
    const og::sim::SimEventBatch batch = og::sim::drain_sim_events(fx.events);
    const og::sim::Event* damage_event = nullptr;
    for (const auto& ev : batch.events)
        if (ev.kind == og::sim::EventKind::DamageTile)
            damage_event = &ev;
    ASSERT_NE(nullptr, damage_event) << "explosion death must emit DamageTile";
    EXPECT_EQ(static_cast<std::uint32_t>(damage_x), damage_event->a);
    EXPECT_EQ(static_cast<std::uint32_t>(damage_y), damage_event->b);
    EXPECT_EQ(PIX_GRASS1, world.grid.data[tile_index])
        << "death() must not mutate the grid inline (master parity)";
    EXPECT_TRUE(world.grid_dirty_tiles().empty());

    // Applying the event (the operation the authoritative server performs)
    // damages the tile and records it for the snapshot grid-sync.
    ASSERT_NE(0, world.damage_tile(static_cast<short>(damage_event->a),
                                   static_cast<short>(damage_event->b)));
    EXPECT_EQ(PIX_GRASS1_DAMAGED, world.grid.data[tile_index]);

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);
    EXPECT_TRUE(snapshot.grid_dirty);
    EXPECT_FALSE(snapshot.grid_full_resend);
    ASSERT_EQ(1u, snapshot.grid_dirty_tiles.size());
    EXPECT_EQ(tile_x, snapshot.grid_dirty_tiles.front().x);
    EXPECT_EQ(tile_y, snapshot.grid_dirty_tiles.front().y);
    EXPECT_EQ(PIX_GRASS1_DAMAGED, snapshot.grid_dirty_tiles.front().value);
    EXPECT_TRUE(world.grid_dirty_tiles().empty());
}

TEST(WorldSnapshot, grid_dirty_overflow_falls_back_to_full_grid_send)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();

    int damaged = 0;
    for (short y = 0; y < world.grid.h && damaged <= static_cast<int>(og::sim::MAX_GRID_DIRTY_TILES); ++y)
    {
        for (short x = 0; x < world.grid.w && damaged <= static_cast<int>(og::sim::MAX_GRID_DIRTY_TILES); ++x)
        {
            const std::size_t tile_index =
                static_cast<std::size_t>(y) * world.grid.w + x;
            world.grid.data[tile_index] = PIX_GRASS1;
            world.damage_tile(static_cast<short>(x * GRID_SIZE),
                              static_cast<short>(y * GRID_SIZE));
            ++damaged;
        }
    }

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(world);

    EXPECT_TRUE(snapshot.grid_dirty);
    EXPECT_TRUE(snapshot.grid_full_resend);
    EXPECT_TRUE(snapshot.grid_dirty_tiles.empty());
    EXPECT_EQ(static_cast<std::size_t>(world.grid.w) * world.grid.h,
              snapshot.full_grid_data.size());
    EXPECT_TRUE(world.grid_dirty_tiles().empty());
}

TEST(WorldSnapshot, apply_snapshot_replaces_state_reorders_lists_and_skips_death_side_effects)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = source.add_ob(Order::Living, FAMILY_ORC);
    walker* controller = source.add_ob(Order::Living, FAMILY_ELF);
    walker* slime = source.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    walker* fx_entity = source.add_fx_ob(Order::FX, FAMILY_FLASH);
    walker* weapon = source.add_weap_ob(Order::Weapon, FAMILY_ARROW);

    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, controller);
    ASSERT_NE(nullptr, slime);
    ASSERT_NE(nullptr, fx_entity);
    ASSERT_NE(nullptr, weapon);

    actor->setworldxy(48.5f, 64.25f);
    foe->setworldxy(90.0f, 32.0f);
    controller->setworldxy(120.0f, 40.0f);
    slime->setworldxy(150.0f, 80.0f);
    fx_entity->setworldxy(72.0f, 44.0f);
    weapon->setworldxy(64.0f, 50.0f);

    actor->set_team_num(2);
    actor->set_real_team_num(3);
    actor->set_user(1);
    actor->set_direct_frame(1);
    actor->set_path_check_counter(44);
    actor->set_regen_delay(73);
    actor->set_foe(foe);
    actor->set_leader(slime);
    actor->set_owner(weapon);
    actor->set_collide_ob(fx_entity);
    actor->stats()->set_controller(controller);
    actor->stats()->commands.emplace_back();

    weapon->set_owner(actor);
    static_cast<SnapshotWeapon*>(weapon)->set_do_bounce(7);

    auto player_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    player_guy->name = "Aldo";
    player_guy->strength = 11;
    player_guy->dexterity = 12;
    player_guy->constitution = 13;
    player_guy->intelligence = 14;
    player_guy->armor = 15;
    player_guy->exp = 1234;
    player_guy->kills = 9;
    player_guy->level_kills = 17;
    player_guy->total_damage = 42;
    player_guy->total_hits = 8;
    player_guy->total_shots = 10;
    player_guy->teamnum = 2;
    player_guy->scen_damage = 5.5f;
    player_guy->scen_kills = 6;
    player_guy->scen_damage_taken = 2.0f;
    player_guy->scen_min_hp = 11.0f;
    player_guy->scen_shots = 7;
    player_guy->scen_hits = 4;
    player_guy->level = 3;
    actor->set_owned_myguy(std::move(player_guy));

    source.tick_count_ = 42;
    source.rng_.state_ = 777;
    source.set_level_tick_count(7);
    source.game_ended = true;
    source.level_done = 1;
    source.end = 1;
    source.retry = true;
    source.next_level = 8;
    source.ending = 2;
    source.enemy_freeze = 3;
    source.timer_wait = 4;
    source.control_hp = 15.5f;
    source.withdraw_requested = true;
    source.withdraw_level = 9;
    source.guy_id_counter = 123;
    source.current_palette_id = 1;
    source.pending_exit_prompt = true;
    source.paused = true;
    source.pause_player_index = 2;
    source.set_weather(WeatherKind::Clouds);
    source.m_score[0] = 100;
    source.m_score[1] = 200;
    source.m_score[2] = 300;
    source.m_score[3] = 400;

    actor->clear_dirty();
    foe->clear_dirty();
    controller->clear_dirty();
    slime->clear_dirty();
    fx_entity->clear_dirty();
    weapon->clear_dirty();

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(source);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);

    og::sim::apply_snapshot(mirror, snapshot);
    EXPECT_EQ(snapshot.rng_state, mirror.rng_.state_);

    reverse_entity_list(mirror.oblist);
    reverse_entity_list(mirror.fxlist);
    reverse_entity_list(mirror.weaplist);

    walker* mirror_actor = mirror.find_by_id(actor->entity_id());
    walker* mirror_slime = mirror.find_by_id(slime->entity_id());
    ASSERT_NE(nullptr, mirror_actor);
    ASSERT_NE(nullptr, mirror_slime);

    mirror_actor->stats()->commands.emplace_back();
    mirror_actor->setworldxy(5.0f, 6.0f);

    mirror_slime->set_order_family(Order::Living, FAMILY_SLIME);
    mirror_slime->ani = animation_rows_for_family(FAMILY_SLIME);
    mirror_slime->set_sizex(16);
    mirror_slime->set_sizey(16);

    walker* doomed_generator = mirror.add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, doomed_generator);

    auto stale_dead = mirror.entity_factory(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, stale_dead);
    stale_dead->set_snapshot_entity_id(999999);
    mirror.dead_list.push_back(std::move(stale_dead));

    mirror_fx.events.clear();
    og::sim::apply_snapshot(mirror, snapshot);

    EXPECT_TRUE(mirror_fx.events.empty());
    EXPECT_EQ(snapshot.rng_state, mirror.rng_.state_);
    EXPECT_TRUE(mirror.dead_list.empty());

    mirror_actor = mirror.find_by_id(actor->entity_id());
    mirror_slime = mirror.find_by_id(slime->entity_id());
    walker* mirror_weapon = mirror.find_by_id(weapon->entity_id());
    ASSERT_NE(nullptr, mirror_actor);
    ASSERT_NE(nullptr, mirror_slime);
    ASSERT_NE(nullptr, mirror_weapon);

    EXPECT_TRUE(mirror_actor->stats()->commands.empty());
    EXPECT_EQ(FAMILY_SMALL_SLIME, mirror_slime->family());
    EXPECT_EQ(kSmallSlimeAnimRows, mirror_slime->ani);
    EXPECT_EQ(pixie_for_family(FAMILY_SMALL_SLIME).w, mirror_slime->sizex());
    EXPECT_EQ(pixie_for_family(FAMILY_SMALL_SLIME).h, mirror_slime->sizey());
    ASSERT_NE(nullptr, mirror.myobmap.get());
    EXPECT_TRUE(pile_contains(
        mirror.myobmap->obmap_get_list(mirror_actor->xpos(), mirror_actor->ypos()),
        mirror_actor));

    const og::sim::WorldSnapshot actual = og::sim::capture_snapshot(mirror);
    const std::array<std::uint64_t, og::sim::kEntitySnapshotDirtyMaskWords>
        clean_expected_dirty = {};

    EXPECT_EQ(source.tick_count_, actual.tick_count);
    EXPECT_EQ(source.rng_.state_, actual.rng_state);
    EXPECT_EQ(source.level_tick_count(), actual.level_tick_count);
    EXPECT_EQ(source.level_done, actual.level_done);
    EXPECT_EQ(source.game_ended, actual.game_ended);
    EXPECT_EQ(source.end, actual.end);
    EXPECT_EQ(source.retry, actual.retry);
    EXPECT_EQ(source.next_level, actual.next_level);
    EXPECT_EQ(source.ending, actual.ending);
    EXPECT_EQ(source.enemy_freeze, actual.enemy_freeze);
    EXPECT_EQ(source.timer_wait, actual.timer_wait);
    EXPECT_EQ(source.living_count, actual.living_count);
    EXPECT_FLOAT_EQ(source.control_hp, actual.control_hp);
    EXPECT_EQ(source.withdraw_requested, actual.withdraw_requested);
    EXPECT_EQ(source.withdraw_level, actual.withdraw_level);
    EXPECT_EQ(source.guy_id_counter, actual.guy_id_counter);
    EXPECT_EQ(source.current_palette_id, actual.current_palette_id);
    EXPECT_EQ(source.pending_exit_prompt, actual.pending_exit_prompt);
    EXPECT_EQ(source.paused, actual.paused);
    EXPECT_EQ(source.pause_player_index, actual.pause_player_index);
    EXPECT_EQ(WeatherKind::Clouds, mirror.weather())
        << "apply_snapshot must install the captured weather kind";
    EXPECT_EQ(source.m_score[0], actual.m_score[0]);
    EXPECT_EQ(source.m_score[3], actual.m_score[3]);
    EXPECT_TRUE(actual.grid_dirty_tiles.empty());
    EXPECT_TRUE(actual.removed_entity_ids.empty());

    ASSERT_EQ(snapshot.guy_snapshots.size(), actual.guy_snapshots.size());
    expect_guy_snapshot_matches(*actor->myguy, actual.guy_snapshots.front());

    EXPECT_EQ(snapshot_ids(snapshot.oblist), snapshot_ids(actual.oblist));
    EXPECT_EQ(snapshot_ids(snapshot.fxlist), snapshot_ids(actual.fxlist));
    EXPECT_EQ(snapshot_ids(snapshot.weaplist), snapshot_ids(actual.weaplist));

    const og::sim::EntitySnapshot* actor_snapshot =
        find_entity_snapshot(actual.oblist, actor->entity_id());
    const og::sim::EntitySnapshot* slime_snapshot =
        find_entity_snapshot(actual.oblist, slime->entity_id());
    const og::sim::EntitySnapshot* fx_snapshot =
        find_entity_snapshot(actual.fxlist, fx_entity->entity_id());
    const og::sim::EntitySnapshot* weapon_snapshot =
        find_entity_snapshot(actual.weaplist, weapon->entity_id());
    ASSERT_NE(nullptr, actor_snapshot);
    ASSERT_NE(nullptr, slime_snapshot);
    ASSERT_NE(nullptr, fx_snapshot);
    ASSERT_NE(nullptr, weapon_snapshot);

    expect_entity_snapshot_matches(*actor, *actor_snapshot, clean_expected_dirty);
    expect_entity_snapshot_matches(*slime, *slime_snapshot, clean_expected_dirty);
    expect_entity_snapshot_matches(*fx_entity, *fx_snapshot, clean_expected_dirty);
    expect_entity_snapshot_matches(*weapon, *weapon_snapshot, clean_expected_dirty);
}

TEST(WorldSnapshot,
     apply_snapshot_creates_entities_in_snapshot_source_list_even_when_order_differs)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    configure_snapshot_test_entity(*actor, Order::FX, FAMILY_FLASH);
    apply_snapshot_test_derived_stats(actor, Order::FX, FAMILY_FLASH);
    actor->set_snapshot_entity_id(4242u);

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(source);
    ASSERT_EQ(1u, snapshot.oblist.size());
    ASSERT_TRUE(snapshot.fxlist.empty());
    EXPECT_EQ(Order::FX, snapshot.oblist.front().order);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);

    og::sim::apply_snapshot(mirror, snapshot);

    ASSERT_EQ(1u, mirror.oblist.size());
    EXPECT_TRUE(mirror.fxlist.empty());
    ASSERT_NE(nullptr, mirror.oblist.front().get());
    EXPECT_EQ(snapshot.oblist.front().entity_id,
              mirror.oblist.front()->entity_id());
    EXPECT_EQ(Order::FX, mirror.oblist.front()->query_order());
}

TEST(WorldSnapshot, apply_snapshot_works_for_attached_external_worlds)
{
    og::sim::WorldSnapshot snapshot;
    std::uint32_t actor_id = 0;

    {
        TestGameWorld source_fx;
        GameWorld& source = source_fx.world();
        configure_snapshot_test_services(source);

        walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, actor);
        actor->setworldxy(64.0f, 96.0f);
        actor_id = actor->entity_id();
        source.rng_.state_ = 4242;

        snapshot = og::sim::capture_snapshot(source);
    }

    LevelRuntimeData mirror_level(2201, true);
    SaveData save;
    std::int32_t freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    mirror_level.create_new_grid();
    mirror_level.set_sim_context(&save, &freeze, &events, &rng, &cfg);

    GameWorld external_world;
    mirror_level.attach_world(&external_world);
    configure_snapshot_test_services(mirror_level.world());

    GameplayContext* const previous_game = current_game;
    current_game = nullptr;
    og::sim::apply_snapshot(mirror_level.world(), snapshot);
    current_game = previous_game;

    EXPECT_EQ(snapshot.rng_state, mirror_level.world().rng_.state_);
    EXPECT_NE(nullptr, mirror_level.world().find_by_id(actor_id));
}

TEST(WorldSnapshot, apply_snapshot_clamps_out_of_range_family_and_special)
{
    // Security: family/current_special arrive straight off the wire and are used
    // to index per-family/per-special tables across the sim. A malicious peer can
    // put any value here, so apply_snapshot must clamp them into range.
    og::sim::WorldSnapshot snapshot;
    std::uint32_t actor_id = 0;
    {
        TestGameWorld source_fx;
        GameWorld& source = source_fx.world();
        configure_snapshot_test_services(source);
        walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, actor);
        actor->setworldxy(64.0f, 96.0f);
        actor_id = actor->entity_id();
        snapshot = og::sim::capture_snapshot(source);
    }
    ASSERT_EQ(1u, snapshot.oblist.size());
    // Forge attacker-controlled out-of-range indices.
    snapshot.oblist[0].family = static_cast<std::int8_t>(99);
    snapshot.oblist[0].current_special = static_cast<std::int8_t>(99);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    GameplayContext* const previous_game = current_game;
    current_game = nullptr;
    og::sim::apply_snapshot(mirror, snapshot);
    current_game = previous_game;

    walker* applied = mirror.find_by_id(actor_id);
    ASSERT_NE(nullptr, applied) << "entity should still be applied";
    EXPECT_GE(static_cast<int>(applied->family()), 0);
    EXPECT_LT(static_cast<int>(applied->family()), NUM_FAMILIES)
        << "out-of-range family must be clamped";
    EXPECT_GE(static_cast<int>(applied->current_special()), 0);
    EXPECT_LT(static_cast<int>(applied->current_special()), NUM_SPECIALS)
        << "out-of-range current_special must be clamped";
}

TEST(WorldSnapshot, apply_snapshot_keeps_dead_players_out_of_obmap)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setworldxy(80.0f, 112.0f);
    actor->set_dead(1);
    actor->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    ASSERT_NE(nullptr, source.myobmap.get());
    source.myobmap->remove(actor);
    EXPECT_FALSE(pile_contains(
        source.myobmap->obmap_get_list(actor->xpos(), actor->ypos()), actor));

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(source);
    ASSERT_EQ(1u, snapshot.oblist.size());

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);

    og::sim::apply_snapshot(mirror, snapshot);

    walker* mirror_actor = mirror.find_by_id(actor->entity_id());
    ASSERT_NE(nullptr, mirror_actor);
    EXPECT_TRUE(mirror_actor->dead());
    ASSERT_NE(nullptr, mirror.myobmap.get());
    EXPECT_FALSE(pile_contains(
        mirror.myobmap->obmap_get_list(mirror_actor->xpos(), mirror_actor->ypos()),
        mirror_actor));
}

TEST(WorldSnapshot, apply_snapshot_overwrites_grid_dirty_tiles)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    const short tile_x = 3;
    const short tile_y = 4;
    const std::size_t tile_index =
        static_cast<std::size_t>(tile_y) * source.grid.w + tile_x;
    source.grid.data[tile_index] = PIX_GRASS1;
    source.damage_tile(static_cast<short>(tile_x * GRID_SIZE),
                       static_cast<short>(tile_y * GRID_SIZE));

    const og::sim::WorldSnapshot snapshot = og::sim::capture_snapshot(source);
    ASSERT_TRUE(snapshot.grid_dirty);
    ASSERT_FALSE(snapshot.grid_dirty_tiles.empty());

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    mirror.grid.data[tile_index] = PIX_GRASS1;

    og::sim::apply_snapshot(mirror, snapshot);

    EXPECT_EQ(snapshot.grid_dirty_tiles.front().value, mirror.grid.data[tile_index]);
    const og::sim::WorldSnapshot keyframe = og::sim::capture_keyframe_snapshot(mirror);
    ASSERT_EQ(static_cast<std::size_t>(mirror.grid.w) * mirror.grid.h,
              keyframe.full_grid_data.size());
    EXPECT_EQ(snapshot.grid_dirty_tiles.front().value,
              keyframe.full_grid_data[tile_index]);
}

TEST(WorldSnapshot, serialize_snapshot_roundtrip_preserves_keyframe_and_compresses)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);
    world.resize_grid(128, 128);
    fill_world_grid(world, PIX_GRASS1);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally = world.add_ob(Order::Living, FAMILY_ELF);
    walker* weapon = world.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, ally);
    ASSERT_NE(nullptr, weapon);

    actor->setworldxy(64.0f, 80.0f);
    actor->set_team_num(2);
    actor->set_user(1);
    actor->set_busy(1.5f);
    actor->stats()->set_hitpoints(18.0f);
    actor->stats()->set_magicpoints(7.0f);
    actor->stats()->set_special_cost(0, 9);
    actor->set_foe(ally);
    static_cast<weap*>(weapon)->set_do_bounce(3);
    weapon->set_owner(actor);

    auto player_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    player_guy->name = "SnapshotHero";
    actor->set_owned_myguy(std::move(player_guy));

    world.tick_count_ = 99;
    world.rng_.state_ = 0x12345678u;
    world.current_palette_id = 1;
    world.pending_exit_prompt = true;
    world.paused = true;
    world.pause_player_index = 2;
    world.set_weather(WeatherKind::Clouds);
    world.m_score[0] = 111;
    world.m_score[1] = 222;

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(world);
    ASSERT_FALSE(keyframe.full_grid_data.empty());

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(keyframe);
    ASSERT_GE(bytes.size(), og::sim::kTransportHeaderSize);
    EXPECT_EQ(og::sim::kSnapshotProtocolVersion, bytes[0]);
    EXPECT_EQ(og::sim::kSnapshotMessageType, bytes[1]);
    const std::size_t payload_length = payload_length_from_header_for_test(bytes);
    EXPECT_EQ(bytes.size(), og::sim::kTransportHeaderSize + payload_length);
    const std::vector<std::uint8_t> raw_payload =
        zlib_decompress_for_test(bytes.data() + og::sim::kTransportHeaderSize,
                                 payload_length);
    ASSERT_FALSE(raw_payload.empty());
    EXPECT_EQ(og::sim::kSnapshotFormatVersion, raw_payload.front());
    EXPECT_LT(bytes.size(), keyframe.full_grid_data.size() / 2);

    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);
    expect_world_snapshot_eq(keyframe, decoded);
}

// The per-character owner tags must survive capture and the snapshot wire so
// the server world (seeded from the host's keyframe) and every client mirror
// can tell which characters each peer owns when persisting saves.
TEST(WorldSnapshot, guy_snapshot_roundtrip_preserves_owner_tags)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);
    world.resize_grid(32, 32);
    fill_world_grid(world, PIX_GRASS1);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    auto player_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    player_guy->name = "OwnedHero";
    player_guy->owner_player_index = 2;  // server-global player slot
    player_guy->owner_save_slot = 5;     // its slot in its owner's save0
    actor->set_owned_myguy(std::move(player_guy));

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(world);
    ASSERT_EQ(1u, keyframe.guy_snapshots.size());
    EXPECT_EQ(2u, keyframe.guy_snapshots.front().owner_player_index)
        << "capture must read owner_player_index off the live guy";
    EXPECT_EQ(5u, keyframe.guy_snapshots.front().owner_save_slot);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(keyframe);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_snapshot(bytes);
    ASSERT_EQ(1u, decoded.guy_snapshots.size());
    EXPECT_EQ(2u, decoded.guy_snapshots.front().owner_player_index)
        << "owner_player_index must survive the snapshot wire";
    EXPECT_EQ(5u, decoded.guy_snapshots.front().owner_save_slot);
}

// The per-level weather kind is world state on the wire: an authoritative
// roll must reach a mirror world through the exact capture -> serialize ->
// deserialize -> apply path clients use, and unknown wire values must clamp
// to None instead of being trusted.
TEST(WorldSnapshot, weather_kind_round_trips_from_authoritative_roll_to_mirror)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);
    source.resize_grid(32, 32);
    fill_world_grid(source, PIX_GRASS1);

    // Level id 7 is a pinned Rain roll under the default-0 nonce (see
    // tests/unit/test_weather.cpp).
    source.id = 7;
    source.roll_weather();
    ASSERT_EQ(WeatherKind::Rain, source.weather());

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(source);
    EXPECT_EQ(static_cast<std::uint8_t>(WeatherKind::Rain), keyframe.weather);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(keyframe);
    const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(bytes);
    EXPECT_EQ(keyframe.weather, decoded.weather);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    ASSERT_EQ(WeatherKind::None, mirror.weather())
        << "mirror worlds start un-rolled";
    og::sim::apply_snapshot(mirror, decoded);
    EXPECT_EQ(WeatherKind::Rain, mirror.weather())
        << "the client applies the server's rolled kind via set_weather";

    // Untrusted wire byte beyond the enum: clamp to None on apply.
    og::sim::WorldSnapshot hostile = decoded;
    hostile.weather = 200;
    og::sim::apply_snapshot(mirror, hostile);
    EXPECT_EQ(WeatherKind::None, mirror.weather())
        << "out-of-range weather bytes must clamp to None";
}

// WeatherKind::Snow (wire byte 3, produced only by the terrain override)
// survives the same capture -> serialize -> deserialize -> apply path, and
// the apply-side clamp boundary sits exactly above it: byte 3 is valid,
// byte 4 clamps to None.
TEST(WorldSnapshot, snow_weather_kind_survives_the_wire_and_pins_the_clamp)
{
    // Stay neutral to the process-wide roll sequence (other tests pin rolls
    // at specific sequence values).
    const std::uint32_t saved_seq = og::weather_roll_sequence();

    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);
    source.resize_grid(32, 32);
    fill_world_grid(source, PIX_GRASS1);
    // Blizzard country: enough snow tiles to trip the after-roll override,
    // whatever the base roll was.
    for (int i = 0; i < og::kSnowWeatherTileThreshold; ++i)
        source.grid.data[i] = static_cast<unsigned char>(PIX_SNOW1);
    source.id = 7;
    source.roll_weather();
    og::set_weather_roll_sequence(saved_seq);
    ASSERT_EQ(WeatherKind::Snow, source.weather())
        << "the terrain override must flip a snowy grid to Snow";

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(source);
    EXPECT_EQ(static_cast<std::uint8_t>(WeatherKind::Snow), keyframe.weather);

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_snapshot(keyframe);
    const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(bytes);
    EXPECT_EQ(keyframe.weather, decoded.weather);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    og::sim::apply_snapshot(mirror, decoded);
    EXPECT_EQ(WeatherKind::Snow, mirror.weather())
        << "wire byte 3 (Snow) must apply, not clamp";

    // The clamp boundary: 4 (one past Snow) is the first invalid byte.
    og::sim::WorldSnapshot hostile = decoded;
    hostile.weather = 4;
    og::sim::apply_snapshot(mirror, hostile);
    EXPECT_EQ(WeatherKind::None, mirror.weather())
        << "bytes above WeatherKind::Snow must clamp to None";
}

TEST(WorldSnapshot, serialize_delta_roundtrip_uses_uncompressed_bypass_when_smaller)
{
    og::sim::WorldSnapshot delta;
    delta.tick_count = 77;
    delta.rng_state = 0x10203040u;
    delta.level_tick_count = 11;
    delta.level_done = 1;
    delta.game_ended = true;
    delta.retry = true;
    delta.next_level = 3;
    delta.ending = 2;
    delta.enemy_freeze = 4;
    delta.timer_wait = 5;
    delta.living_count = 6;
    delta.control_hp = 7.5f;
    delta.withdraw_requested = true;
    delta.withdraw_level = 8;
    delta.guy_id_counter = 9;
    delta.m_score[0] = 101;
    delta.m_score[1] = 202;
    delta.current_palette_id = 1;
    delta.pending_exit_prompt = true;
    delta.paused = true;
    delta.pause_player_index = 3;
    delta.weather = 2;
    delta.grid_width = 9;
    delta.grid_height = 7;
    delta.grid_dirty = true;
    delta.grid_full_resend = true;
    delta.full_grid_data.resize(63);
    for (std::size_t i = 0; i < delta.full_grid_data.size(); ++i)
    {
        delta.full_grid_data[i] =
            static_cast<std::uint8_t>((i * 37U + 11U) & 0xffU);
    }
    og::sim::GuySnapshot guy_snapshot;
    guy_snapshot.guy_id = 42;
    guy_snapshot.name = "Q7mP2xN9cR4tV8bL1kH5sD0fJ6aW3eY";
    guy_snapshot.family = FAMILY_ELF;
    guy_snapshot.strength = 10;
    guy_snapshot.dexterity = 11;
    guy_snapshot.constitution = 12;
    guy_snapshot.intelligence = 13;
    guy_snapshot.armor = 14;
    guy_snapshot.exp = 15;
    guy_snapshot.kills = 16;
    guy_snapshot.level_kills = 17;
    guy_snapshot.total_damage = 18;
    guy_snapshot.total_hits = 19;
    guy_snapshot.total_shots = 20;
    guy_snapshot.teamnum = 21;
    guy_snapshot.scen_damage = 22.0f;
    guy_snapshot.scen_kills = 23;
    guy_snapshot.scen_damage_taken = 24.0f;
    guy_snapshot.scen_min_hp = 25.0f;
    guy_snapshot.scen_shots = 26;
    guy_snapshot.scen_hits = 27;
    guy_snapshot.level = 28;
    delta.guy_snapshots.push_back(guy_snapshot);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    ASSERT_GE(bytes.size(), og::sim::kTransportHeaderSize +
                               og::sim::kDeltaPayloadHeaderSize);
    EXPECT_EQ(og::sim::kSnapshotProtocolVersion, bytes[0]);
    EXPECT_EQ(og::sim::kDeltaSnapshotMessageType, bytes[1]);

    const bool payload_is_uncompressed =
        delta_payload_is_uncompressed_for_test(bytes);
    const std::vector<std::uint8_t> raw_payload =
        decode_delta_payload_for_test(bytes);
    const std::vector<std::uint8_t> recompressed =
        zlib_compress_for_test(raw_payload);
    if (payload_is_uncompressed)
        EXPECT_GE(recompressed.size(), raw_payload.size());
    else
        EXPECT_LT(recompressed.size(), raw_payload.size());

    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);
    expect_world_snapshot_eq(delta, decoded);
}

TEST(WorldSnapshot, empty_delta_roundtrip_preserves_world_state_without_entities)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    const std::uint32_t actor_id = actor->entity_id();

    og::sim::WorldSnapshot baseline = og::sim::capture_keyframe_snapshot(world);
    og::sim::PerClientState client_state;
    og::sim::seed_client_snapshot_baseline(client_state, baseline);

    world.tick_count_ = baseline.tick_count + 1;
    world.rng_.state_ = 0x55667788u;
    world.current_palette_id = 1;
    const og::sim::WorldSnapshot current = og::sim::capture_snapshot(world);
    og::sim::accumulate_snapshot_for_client(client_state, current);

    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(client_state, current);
    ASSERT_EQ(1u, delta.oblist.size());
    EXPECT_EQ(actor_id, delta.oblist.front().entity_id);
    EXPECT_EQ(1ULL << og::dirty::BIT_ENTITY_ID,
              delta.oblist.front().dirty_mask[0]);
    EXPECT_EQ(0ULL, delta.oblist.front().dirty_mask[1]);
    EXPECT_TRUE(delta.fxlist.empty());
    EXPECT_TRUE(delta.weaplist.empty());
    EXPECT_TRUE(delta.removed_entity_ids.empty());

    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);
    ASSERT_EQ(1u, decoded.oblist.size());
    EXPECT_EQ(actor_id, decoded.oblist.front().entity_id);
    EXPECT_EQ(1ULL << og::dirty::BIT_ENTITY_ID,
              decoded.oblist.front().dirty_mask[0]);
    EXPECT_EQ(0ULL, decoded.oblist.front().dirty_mask[1]);
    EXPECT_TRUE(decoded.fxlist.empty());
    EXPECT_TRUE(decoded.weaplist.empty());
    EXPECT_TRUE(decoded.removed_entity_ids.empty());
    EXPECT_EQ(current.tick_count, decoded.tick_count);
    EXPECT_EQ(current.rng_state, decoded.rng_state);
    EXPECT_EQ(current.current_palette_id, decoded.current_palette_id);

    og::sim::apply_delta(baseline, decoded);
    const og::sim::WorldSnapshot current_keyframe =
        og::sim::capture_keyframe_snapshot(world);
    expect_world_snapshot_eq(current_keyframe, baseline, false);
}

TEST(WorldSnapshot, serialize_delta_roundtrip_preserves_zero_mask_removal_sentinel)
{
    og::sim::WorldSnapshot delta;
    delta.tick_count = 41;
    delta.rng_state = 0x1234abcdU;

    og::sim::EntitySnapshot removed;
    removed.entity_id = 77;
    removed.guy_id = og::sim::kNoGuyId;
    delta.oblist.push_back(removed);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    ASSERT_EQ(1u, decoded.oblist.size());
    EXPECT_EQ(77u, decoded.oblist.front().entity_id);
    EXPECT_TRUE(is_removed_entity_sentinel(decoded.oblist.front()));
    EXPECT_EQ(std::vector<std::uint32_t>({77u}), decoded.removed_entity_ids);
}

TEST(WorldSnapshot, deserialize_snapshot_rejects_bad_headers_and_format_version)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);
    ASSERT_NE(nullptr, world.add_ob(Order::Living, FAMILY_SOLDIER));

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(world);
    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(keyframe);

    std::vector<std::uint8_t> bad_protocol = bytes;
    bad_protocol[0] = static_cast<std::uint8_t>(og::sim::kSnapshotProtocolVersion + 1);
    try
    {
        (void)og::sim::deserialize_snapshot(bad_protocol);
        FAIL() << "expected protocol mismatch";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("unsupported protocol version"),
                  std::string::npos);
    }

    std::vector<std::uint8_t> bad_type = bytes;
    bad_type[1] = og::sim::kDeltaSnapshotMessageType;
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_type),
        std::runtime_error);

    std::vector<std::uint8_t> bad_length = bytes;
    bad_length[2] = 0;
    bad_length[3] = 0;
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(bad_length),
        std::runtime_error);

    const std::size_t payload_length = payload_length_from_header_for_test(bytes);
    std::vector<std::uint8_t> payload =
        zlib_decompress_for_test(bytes.data() + og::sim::kTransportHeaderSize,
                                 payload_length);
    payload[0] = static_cast<std::uint8_t>(og::sim::kSnapshotFormatVersion + 1);
    const std::vector<std::uint8_t> corrupted_payload =
        zlib_compress_for_test(payload);

    std::vector<std::uint8_t> bad_format;
    bad_format.reserve(og::sim::kTransportHeaderSize + corrupted_payload.size());
    bad_format.push_back(og::sim::kSnapshotProtocolVersion);
    bad_format.push_back(og::sim::kSnapshotMessageType);
    bad_format.push_back(static_cast<std::uint8_t>(corrupted_payload.size() & 0xffu));
    bad_format.push_back(static_cast<std::uint8_t>((corrupted_payload.size() >> 8) & 0xffu));
    bad_format.insert(bad_format.end(),
                      corrupted_payload.begin(), corrupted_payload.end());

    try
    {
        (void)og::sim::deserialize_snapshot(bad_format);
        FAIL() << "expected snapshot format mismatch";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string(error.what()).find("please update"),
                  std::string::npos);
    }

    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(truncated),
        std::runtime_error);
}

TEST(WorldSnapshot, deserialize_delta_rejects_bad_headers_and_malformed_payloads)
{
    og::sim::WorldSnapshot delta;
    delta.tick_count = 12;
    delta.rng_state = 34;
    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);

    std::vector<std::uint8_t> bad_protocol = bytes;
    bad_protocol[0] = static_cast<std::uint8_t>(og::sim::kSnapshotProtocolVersion + 1);
    EXPECT_THROW(
        (void)og::sim::deserialize_delta(bad_protocol),
        std::runtime_error);

    std::vector<std::uint8_t> bad_type = bytes;
    bad_type[1] = og::sim::kSnapshotMessageType;
    EXPECT_THROW(
        (void)og::sim::deserialize_delta(bad_type),
        std::runtime_error);

    std::vector<std::uint8_t> bad_length = bytes;
    bad_length[2] = 0;
    bad_length[3] = 0;
    EXPECT_THROW(
        (void)og::sim::deserialize_delta(bad_length),
        std::runtime_error);

    const bool payload_is_uncompressed =
        delta_payload_is_uncompressed_for_test(bytes);
    std::vector<std::uint8_t> payload = decode_delta_payload_for_test(bytes);
    payload[0] = static_cast<std::uint8_t>(og::sim::kSnapshotFormatVersion + 1);
    const std::vector<std::uint8_t> corrupted_payload = payload_is_uncompressed
        ? payload
        : zlib_compress_for_test(payload);
    std::vector<std::uint8_t> bad_format;
    const std::size_t bad_payload_length =
        og::sim::kDeltaPayloadHeaderSize + corrupted_payload.size();
    bad_format.reserve(og::sim::kTransportHeaderSize + bad_payload_length);
    bad_format.push_back(og::sim::kSnapshotProtocolVersion);
    bad_format.push_back(og::sim::kDeltaSnapshotMessageType);
    bad_format.push_back(static_cast<std::uint8_t>(bad_payload_length & 0xffu));
    bad_format.push_back(static_cast<std::uint8_t>((bad_payload_length >> 8) & 0xffu));
    bad_format.push_back(payload_is_uncompressed
                             ? og::sim::kDeltaPayloadUncompressedFlag
                             : 0U);
    bad_format.insert(bad_format.end(),
                      corrupted_payload.begin(), corrupted_payload.end());
    EXPECT_THROW(
        (void)og::sim::deserialize_delta(bad_format),
        std::runtime_error);

    std::vector<std::uint8_t> truncated = bytes;
    truncated.pop_back();
    EXPECT_THROW(
        (void)og::sim::deserialize_delta(truncated),
        std::runtime_error);
}

TEST(WorldSnapshot, deserialize_snapshot_and_delta_reject_oversized_payloads_and_counts)
{
    std::vector<std::uint8_t> oversized_payload(
        (4U * 1024U * 1024U) + 1U, 0);
    oversized_payload[0] = og::sim::kSnapshotFormatVersion;
    const std::vector<std::uint8_t> oversized_compressed =
        zlib_compress_for_test(oversized_payload);
    ASSERT_LT(oversized_compressed.size(),
              static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()));

    std::vector<std::uint8_t> oversized_snapshot;
    oversized_snapshot.reserve(og::sim::kTransportHeaderSize +
                               oversized_compressed.size());
    oversized_snapshot.push_back(og::sim::kSnapshotProtocolVersion);
    oversized_snapshot.push_back(og::sim::kSnapshotMessageType);
    oversized_snapshot.push_back(
        static_cast<std::uint8_t>(oversized_compressed.size() & 0xffu));
    oversized_snapshot.push_back(
        static_cast<std::uint8_t>((oversized_compressed.size() >> 8) & 0xffu));
    oversized_snapshot.insert(oversized_snapshot.end(),
                              oversized_compressed.begin(),
                              oversized_compressed.end());
    EXPECT_THROW(
        (void)og::sim::deserialize_snapshot(oversized_snapshot),
        std::runtime_error);

    og::sim::WorldSnapshot delta;
    delta.tick_count = 12;
    const std::vector<std::uint8_t> delta_bytes = og::sim::serialize_delta(delta);
    std::vector<std::uint8_t> raw_payload =
        decode_delta_payload_for_test(delta_bytes);

    // Offset of grid.full_grid_size in a default delta payload: format byte +
    // 195 bytes of world scalars (the CTF block adds 120, the difficulty
    // submenu scalars 4) + 4 grid bytes.
    constexpr std::size_t kEntityCountOffset = 200;
    ASSERT_GE(raw_payload.size(), kEntityCountOffset + sizeof(std::uint32_t));
    raw_payload[kEntityCountOffset + 0] = 0xffu;
    raw_payload[kEntityCountOffset + 1] = 0xffu;
    raw_payload[kEntityCountOffset + 2] = 0xffu;
    raw_payload[kEntityCountOffset + 3] = 0xffu;

    std::vector<std::uint8_t> bad_count_delta;
    const std::size_t bad_payload_length =
        og::sim::kDeltaPayloadHeaderSize + raw_payload.size();
    bad_count_delta.reserve(og::sim::kTransportHeaderSize + bad_payload_length);
    bad_count_delta.push_back(og::sim::kSnapshotProtocolVersion);
    bad_count_delta.push_back(og::sim::kDeltaSnapshotMessageType);
    bad_count_delta.push_back(static_cast<std::uint8_t>(bad_payload_length & 0xffu));
    bad_count_delta.push_back(static_cast<std::uint8_t>((bad_payload_length >> 8) & 0xffu));
    bad_count_delta.push_back(og::sim::kDeltaPayloadUncompressedFlag);
    bad_count_delta.insert(bad_count_delta.end(),
                           raw_payload.begin(), raw_payload.end());
    EXPECT_THROW(
        (void)og::sim::deserialize_delta(bad_count_delta),
        std::runtime_error);
}

TEST(WorldSnapshot, serialize_rejects_invalid_grid_payloads)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.grid_dirty = true;
    snapshot.grid_full_resend = true;
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);

    snapshot.grid_width = 2;
    snapshot.grid_height = 2;
    snapshot.full_grid_data = {1, 2, 3};
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);

    snapshot.grid_full_resend = false;
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);

    snapshot.grid_full_resend = true;
    snapshot.grid_dirty = false;
    snapshot.full_grid_data = {1, 2, 3, 4};
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);

    snapshot.grid_full_resend = false;
    snapshot.full_grid_data.clear();
    snapshot.grid_dirty_tiles.push_back({0, 0, 7});
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);

    snapshot.grid_dirty = true;
    snapshot.grid_dirty_tiles = {{3, 0, 7}};
    EXPECT_THROW((void)og::sim::serialize_snapshot(snapshot), std::runtime_error);
}

TEST(WorldSnapshot, event_batch_roundtrip_and_rejects_malformed_frames)
{
    og::sim::SimEventBatch batch;
    batch.sequence = 7;
    batch.events.push_back(og::sim::Event{
        .tick = 8,
        .kind = og::sim::EventKind::Notification,
        .a = 1,
        .b = 2,
        .text = "event text",
    });
    batch.events.push_back(og::sim::Event{
        .tick = 9,
        .kind = og::sim::EventKind::EndGame,
        .a = 0,
        .b = 3,
        .text = "",
    });

    const std::vector<std::uint8_t> bytes =
        og::sim::serialize_sim_event_batch(batch);
    const og::sim::SimEventBatch decoded =
        og::sim::deserialize_sim_event_batch(bytes);
    EXPECT_EQ(batch.sequence, decoded.sequence);
    EXPECT_EQ(batch.events, decoded.events);

    const std::vector<std::uint8_t> flow_bytes =
        og::sim::serialize_game_flow_event_batch(batch);
    const og::sim::SimEventBatch decoded_flow =
        og::sim::deserialize_game_flow_event_batch(flow_bytes);
    EXPECT_EQ(batch.events, decoded_flow.events);

    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(
                     std::span<const std::uint8_t>{}),
                 std::runtime_error);

    std::vector<std::uint8_t> bad_protocol = bytes;
    bad_protocol[0] =
        static_cast<std::uint8_t>(og::sim::kSnapshotProtocolVersion + 1);
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(bad_protocol),
                 std::runtime_error);

    std::vector<std::uint8_t> bad_type = bytes;
    bad_type[1] = og::sim::kGameFlowEventBatchMessageType;
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(bad_type),
                 std::runtime_error);

    std::vector<std::uint8_t> bad_length = bytes;
    bad_length[2] = 0;
    bad_length[3] = 0;
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(bad_length),
                 std::runtime_error);

    std::vector<std::uint8_t> too_many_events_payload;
    append_u32_for_test(too_many_events_payload, 1);
    append_u32_for_test(too_many_events_payload, 4097);
    const std::vector<std::uint8_t> too_many_events =
        frame_event_batch_payload_for_test(
            og::sim::kSimEventBatchMessageType,
            too_many_events_payload);
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(too_many_events),
                 std::runtime_error);

    std::vector<std::uint8_t> oversized_string_payload;
    append_u32_for_test(oversized_string_payload, 1);
    append_u32_for_test(oversized_string_payload, 1);
    append_u32_for_test(oversized_string_payload, 2);
    append_u32_for_test(oversized_string_payload,
                        static_cast<std::uint32_t>(og::sim::EventKind::Notification));
    append_u32_for_test(oversized_string_payload, 0);
    append_u32_for_test(oversized_string_payload, 0);
    append_u32_for_test(oversized_string_payload, 1025);
    const std::vector<std::uint8_t> oversized_string =
        frame_event_batch_payload_for_test(
            og::sim::kSimEventBatchMessageType,
            oversized_string_payload);
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(oversized_string),
                 std::runtime_error);

    std::vector<std::uint8_t> truncated_string_payload;
    append_u32_for_test(truncated_string_payload, 1);
    append_u32_for_test(truncated_string_payload, 1);
    append_u32_for_test(truncated_string_payload, 3);
    append_u32_for_test(truncated_string_payload,
                        static_cast<std::uint32_t>(og::sim::EventKind::Notification));
    append_u32_for_test(truncated_string_payload, 0);
    append_u32_for_test(truncated_string_payload, 0);
    append_u32_for_test(truncated_string_payload, 4);
    truncated_string_payload.push_back('x');
    const std::vector<std::uint8_t> truncated_string =
        frame_event_batch_payload_for_test(
            og::sim::kSimEventBatchMessageType,
            truncated_string_payload);
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(truncated_string),
                 std::runtime_error);

    std::vector<std::uint8_t> trailing_payload;
    append_u32_for_test(trailing_payload, 1);
    append_u32_for_test(trailing_payload, 0);
    trailing_payload.push_back(0xee);
    const std::vector<std::uint8_t> trailing =
        frame_event_batch_payload_for_test(
            og::sim::kSimEventBatchMessageType,
            trailing_payload);
    EXPECT_THROW((void)og::sim::deserialize_sim_event_batch(trailing),
                 std::runtime_error);

    og::sim::SimEventBatch huge_text;
    huge_text.events.push_back(og::sim::Event{
        .tick = 1,
        .kind = og::sim::EventKind::Notification,
        .a = 0,
        .b = 0,
        .text = std::string(70000, 'x'),
    });
    EXPECT_THROW((void)og::sim::serialize_sim_event_batch(huge_text),
                 std::runtime_error);
}

TEST(WorldSnapshot, apply_delta_covers_grid_materialization_edges)
{
    og::sim::WorldSnapshot baseline;
    og::sim::WorldSnapshot zero_delta;
    zero_delta.grid_width = 0;
    zero_delta.grid_height = 0;
    zero_delta.grid_dirty = true;
    zero_delta.grid_full_resend = true;
    zero_delta.full_grid_data = {1, 2, 3};
    zero_delta.grid_dirty_tiles = {{0, 0, 9}};

    og::sim::apply_delta(baseline, zero_delta);
    EXPECT_FALSE(baseline.grid_dirty);
    EXPECT_FALSE(baseline.grid_full_resend);
    EXPECT_TRUE(baseline.full_grid_data.empty());
    EXPECT_TRUE(baseline.grid_dirty_tiles.empty());

    og::sim::WorldSnapshot sparse_delta;
    sparse_delta.grid_width = 2;
    sparse_delta.grid_height = 2;
    sparse_delta.grid_dirty = true;
    sparse_delta.grid_full_resend = false;
    sparse_delta.grid_dirty_tiles = {{1, 1, 7}};

    og::sim::apply_delta(baseline, sparse_delta);
    EXPECT_TRUE(baseline.grid_dirty);
    EXPECT_FALSE(baseline.grid_full_resend);
    ASSERT_EQ(1u, baseline.grid_dirty_tiles.size());
    EXPECT_EQ(7, baseline.grid_dirty_tiles.front().value);

    baseline.grid_full_resend = true;
    baseline.full_grid_data = {1, 2, 3, 4};
    baseline.grid_dirty_tiles.clear();

    og::sim::WorldSnapshot dirty_delta;
    dirty_delta.grid_width = 2;
    dirty_delta.grid_height = 2;
    dirty_delta.grid_dirty = true;
    dirty_delta.grid_dirty_tiles = {{0, 1, 8}, {5, 5, 9}};

    og::sim::apply_delta(baseline, dirty_delta);
    ASSERT_EQ(4u, baseline.full_grid_data.size());
    EXPECT_EQ(8, baseline.full_grid_data[2]);
    EXPECT_TRUE(baseline.grid_dirty_tiles.empty());
}

TEST(WorldSnapshot, apply_delta_with_all_fields_dirty_matches_current_world_state)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = source.add_ob(Order::Living, FAMILY_ORC);
    walker* weapon = source.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, weapon);

    actor->set_foe(foe);
    weapon->set_owner(actor);
    source.current_palette_id = 1;

    og::sim::WorldSnapshot client_baseline =
        og::sim::capture_keyframe_snapshot(source);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    og::sim::apply_snapshot(mirror, client_baseline);

    actor->setworldxy(120.0f, 144.0f);
    actor->set_busy(2.0f);
    actor->stats()->set_hitpoints(13.0f);
    actor->stats()->set_magicpoints(5.0f);
    static_cast<weap*>(weapon)->set_do_bounce(7);
    source.pending_exit_prompt = true;
    source.paused = true;
    source.pause_player_index = 1;
    source.my_team = 3;
    source.allied_mode = 1;
    source.difficulty = 125;

    const og::sim::WorldSnapshot current = og::sim::capture_snapshot(source);

    og::sim::PerClientState client_state;
    og::sim::seed_client_snapshot_baseline(client_state, client_baseline);
    for (const auto& entity : current.oblist)
        client_state.accumulated_dirty[entity.entity_id] = {~0ULL, ~0ULL};
    for (const auto& entity : current.fxlist)
        client_state.accumulated_dirty[entity.entity_id] = {~0ULL, ~0ULL};
    for (const auto& entity : current.weaplist)
        client_state.accumulated_dirty[entity.entity_id] = {~0ULL, ~0ULL};

    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(client_state, current);
    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    og::sim::apply_delta(client_baseline, decoded);
    const og::sim::WorldSnapshot source_keyframe =
        og::sim::capture_keyframe_snapshot(source);
    expect_world_snapshot_eq(source_keyframe, client_baseline, false);
    og::sim::apply_snapshot(mirror, client_baseline);
    const og::sim::WorldSnapshot mirror_keyframe =
        og::sim::capture_keyframe_snapshot(mirror);
    expect_world_snapshot_eq(source_keyframe, mirror_keyframe);
}

TEST(WorldSnapshot, consume_delta_snapshot_for_client_accumulates_grid_tiles_across_skipped_sends)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);
    source.resize_grid(16, 16);
    fill_world_grid(source, PIX_GRASS1);

    og::sim::WorldSnapshot client_baseline =
        og::sim::capture_keyframe_snapshot(source);
    og::sim::PerClientState client_state;
    og::sim::seed_client_snapshot_baseline(client_state, client_baseline);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    mirror.resize_grid(16, 16);
    fill_world_grid(mirror, PIX_GRASS1);
    og::sim::apply_snapshot(mirror, client_baseline);

    source.tick_count_ = client_baseline.tick_count + 1;
    source.damage_tile(1 * GRID_SIZE, 2 * GRID_SIZE);
    const og::sim::WorldSnapshot tick_one = og::sim::capture_snapshot(source);
    og::sim::accumulate_snapshot_for_client(client_state, tick_one);

    source.tick_count_ = client_baseline.tick_count + 2;
    source.damage_tile(3 * GRID_SIZE, 4 * GRID_SIZE);
    const og::sim::WorldSnapshot tick_two = og::sim::capture_snapshot(source);
    og::sim::accumulate_snapshot_for_client(client_state, tick_two);

    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(client_state, tick_two);
    EXPECT_TRUE(delta.grid_dirty);
    EXPECT_FALSE(delta.grid_full_resend);
    ASSERT_EQ(2u, delta.grid_dirty_tiles.size());

    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    EXPECT_TRUE(decoded.grid_dirty);
    EXPECT_FALSE(decoded.grid_full_resend);
    ASSERT_NE(nullptr, find_grid_dirty_tile(decoded.grid_dirty_tiles, 1, 2));
    ASSERT_NE(nullptr, find_grid_dirty_tile(decoded.grid_dirty_tiles, 3, 4));

    og::sim::apply_delta(client_baseline, decoded);
    const og::sim::WorldSnapshot source_keyframe =
        og::sim::capture_keyframe_snapshot(source);
    expect_world_snapshot_eq(source_keyframe, client_baseline, false);
    og::sim::apply_snapshot(mirror, client_baseline);
    const og::sim::WorldSnapshot mirror_keyframe =
        og::sim::capture_keyframe_snapshot(mirror);
    expect_world_snapshot_eq(source_keyframe, mirror_keyframe);
}

TEST(WorldSnapshot, consume_delta_snapshot_for_client_keeps_later_grid_tiles_after_full_resend)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);
    source.resize_grid(16, 16);
    fill_world_grid(source, PIX_GRASS1);

    og::sim::WorldSnapshot client_baseline =
        og::sim::capture_keyframe_snapshot(source);
    og::sim::PerClientState client_state;
    og::sim::seed_client_snapshot_baseline(client_state, client_baseline);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    mirror.resize_grid(16, 16);
    fill_world_grid(mirror, PIX_GRASS1);
    og::sim::apply_snapshot(mirror, client_baseline);

    source.tick_count_ = client_baseline.tick_count + 1;
    int damaged = 0;
    for (short y = 0; y < source.grid.h &&
                      damaged <= static_cast<int>(og::sim::MAX_GRID_DIRTY_TILES);
         ++y)
    {
        for (short x = 0; x < source.grid.w &&
                          damaged <= static_cast<int>(og::sim::MAX_GRID_DIRTY_TILES);
             ++x)
        {
            source.damage_tile(x * GRID_SIZE, y * GRID_SIZE);
            ++damaged;
        }
    }
    const og::sim::WorldSnapshot overflow_tick = og::sim::capture_snapshot(source);
    ASSERT_TRUE(overflow_tick.grid_full_resend);
    og::sim::accumulate_snapshot_for_client(client_state, overflow_tick);

    source.tick_count_ = client_baseline.tick_count + 2;
    source.damage_tile(15 * GRID_SIZE, 15 * GRID_SIZE);
    const og::sim::WorldSnapshot later_tick = og::sim::capture_snapshot(source);
    ASSERT_TRUE(later_tick.grid_dirty);
    ASSERT_FALSE(later_tick.grid_full_resend);
    og::sim::accumulate_snapshot_for_client(client_state, later_tick);

    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(client_state, later_tick);
    EXPECT_TRUE(delta.grid_dirty);
    EXPECT_TRUE(delta.grid_full_resend);
    ASSERT_FALSE(delta.full_grid_data.empty());

    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    EXPECT_TRUE(decoded.grid_dirty);
    EXPECT_TRUE(decoded.grid_full_resend);
    ASSERT_FALSE(decoded.full_grid_data.empty());
    ASSERT_NE(nullptr, find_grid_dirty_tile(decoded.grid_dirty_tiles, 15, 15));

    og::sim::apply_delta(client_baseline, decoded);
    const og::sim::WorldSnapshot source_keyframe =
        og::sim::capture_keyframe_snapshot(source);
    expect_world_snapshot_eq(source_keyframe, client_baseline, false);
    og::sim::apply_snapshot(mirror, client_baseline);
    const og::sim::WorldSnapshot mirror_keyframe =
        og::sim::capture_keyframe_snapshot(mirror);
    expect_world_snapshot_eq(source_keyframe, mirror_keyframe);
}

TEST(WorldSnapshot, apply_delta_removes_entity_from_baseline_on_zero_mask_sentinel)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);

    og::sim::WorldSnapshot baseline = og::sim::capture_keyframe_snapshot(world);
    const std::uint32_t actor_id = actor->entity_id();
    const std::uint32_t foe_id = foe->entity_id();

    og::sim::WorldSnapshot delta = baseline;
    delta.tick_count = baseline.tick_count + 1;
    delta.oblist.clear();
    delta.fxlist.clear();
    delta.weaplist.clear();
    delta.removed_entity_ids.clear();

    og::sim::EntitySnapshot removed;
    removed.entity_id = foe_id;
    removed.guy_id = og::sim::kNoGuyId;
    delta.oblist.push_back(removed);

    og::sim::apply_delta(baseline, delta);

    EXPECT_NE(nullptr, find_entity_snapshot(baseline.oblist, actor_id));
    EXPECT_EQ(nullptr, find_entity_snapshot(baseline.oblist, foe_id));
    EXPECT_TRUE(baseline.removed_entity_ids.empty());
}

TEST(WorldSnapshot, apply_delta_reorders_entity_lists_even_without_field_changes)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = source.add_ob(Order::Living, FAMILY_ORC);
    walker* slime = source.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, slime);

    og::sim::WorldSnapshot client_baseline =
        og::sim::capture_keyframe_snapshot(source);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    og::sim::apply_snapshot(mirror, client_baseline);

    reverse_entity_list(source.oblist);
    source.tick_count_ = client_baseline.tick_count + 1;
    const og::sim::WorldSnapshot delta = og::sim::capture_snapshot(source);
    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    og::sim::apply_delta(client_baseline, decoded);
    const og::sim::WorldSnapshot source_keyframe =
        og::sim::capture_keyframe_snapshot(source);
    expect_world_snapshot_eq(source_keyframe, client_baseline, false);

    og::sim::apply_snapshot(mirror, client_baseline);
    const og::sim::WorldSnapshot mirror_keyframe =
        og::sim::capture_keyframe_snapshot(mirror);
    expect_world_snapshot_eq(source_keyframe, mirror_keyframe);
}

TEST(WorldSnapshot,
     consume_delta_snapshot_for_client_keeps_authoritative_entity_order_when_only_order_changes)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = source.add_ob(Order::Living, FAMILY_ORC);
    walker* slime = source.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, slime);

    og::sim::WorldSnapshot client_baseline =
        og::sim::capture_keyframe_snapshot(source);
    og::sim::PerClientState client_state;
    og::sim::seed_client_snapshot_baseline(client_state, client_baseline);

    og::sim::WorldSnapshot current = client_baseline;
    current.tick_count = client_baseline.tick_count + 1;
    std::reverse(current.oblist.begin(), current.oblist.end());
    current.snapshot_hash = og::sim::compute_snapshot_hash(current);
    og::sim::accumulate_snapshot_for_client(client_state, current);

    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(client_state, current);
    ASSERT_EQ(current.oblist.size(), delta.oblist.size());
    EXPECT_EQ(snapshot_ids(current.oblist), snapshot_ids(delta.oblist));

    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    og::sim::apply_delta(client_baseline, decoded);
    expect_world_snapshot_eq(current, client_baseline, false);
}

TEST(WorldSnapshot, apply_delta_accumulates_multi_tick_changes_with_spawn_and_removal)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);

    walker* actor = source.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* foe = source.add_ob(Order::Living, FAMILY_ORC);
    walker* weapon = source.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_NE(nullptr, actor);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, weapon);

    actor->set_foe(foe);
    weapon->set_owner(actor);
    source.m_score[0] = 10;

    og::sim::WorldSnapshot client_baseline =
        og::sim::capture_keyframe_snapshot(source);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    og::sim::apply_snapshot(mirror, client_baseline);

    og::sim::PerClientState client_state;
    og::sim::seed_client_snapshot_baseline(client_state, client_baseline);

    actor->setworldxy(96.0f, 112.0f);
    actor->set_busy(1.0f);
    actor->stats()->set_hitpoints(16.0f);
    source.current_palette_id = 1;
    const og::sim::WorldSnapshot tick_one = og::sim::capture_snapshot(source);
    og::sim::accumulate_snapshot_for_client(client_state, tick_one);

    walker* slime = source.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, slime);
    slime->setworldxy(160.0f, 176.0f);
    slime->set_team_num(3);
    slime->stats()->set_hitpoints(12.0f);
    source.m_score[1] = 20;
    const std::uint32_t slime_id = slime->entity_id();
    const og::sim::WorldSnapshot tick_two = og::sim::capture_snapshot(source);
    og::sim::accumulate_snapshot_for_client(client_state, tick_two);

    const std::uint32_t removed_foe_id = foe->entity_id();
    ASSERT_EQ(1, source.remove_ob(foe));
    slime->stats()->set_magicpoints(9.0f);
    slime->set_busy(0.75f);
    source.damage_tile(3 * GRID_SIZE, 4 * GRID_SIZE);
    source.pending_exit_prompt = true;
    source.paused = true;
    source.pause_player_index = 2;
    const og::sim::WorldSnapshot tick_three = og::sim::capture_snapshot(source);
    og::sim::accumulate_snapshot_for_client(client_state, tick_three);

    const og::sim::WorldSnapshot delta =
        og::sim::consume_delta_snapshot_for_client(client_state, tick_three);
    const std::vector<std::uint8_t> bytes = og::sim::serialize_delta(delta);
    const og::sim::WorldSnapshot decoded =
        og::sim::deserialize_delta(bytes);

    EXPECT_NE(decoded.removed_entity_ids.end(),
              std::find(decoded.removed_entity_ids.begin(),
                        decoded.removed_entity_ids.end(),
                        removed_foe_id));
    const og::sim::EntitySnapshot* removed_foe_delta =
        find_entity_snapshot(decoded.oblist, removed_foe_id);
    ASSERT_NE(nullptr, removed_foe_delta);
    EXPECT_TRUE(is_removed_entity_sentinel(*removed_foe_delta));

    const og::sim::EntitySnapshot* slime_delta =
        find_entity_snapshot(decoded.oblist, slime_id);
    ASSERT_NE(nullptr, slime_delta);
    EXPECT_EQ(~0ULL, slime_delta->dirty_mask[0]);
    EXPECT_EQ(~0ULL, slime_delta->dirty_mask[1]);

    og::sim::apply_delta(client_baseline, decoded);
    const og::sim::WorldSnapshot source_keyframe =
        og::sim::capture_keyframe_snapshot(source);
    expect_world_snapshot_eq(source_keyframe, client_baseline, false);
    og::sim::apply_snapshot(mirror, client_baseline);
    const og::sim::WorldSnapshot mirror_keyframe =
        og::sim::capture_keyframe_snapshot(mirror);
    expect_world_snapshot_eq(source_keyframe, mirror_keyframe);
}

// Dormant (delayed-spawn) walkers are excluded from snapshot capture, so
// apply must (1) never reconcile them away when a snapshot lacks them — the
// local transport shadow seeds the SERVER world by applying a display-world
// snapshot at install, which silently deleted every not-yet-spawned walker —
// and (2) clear dormancy on any walker a snapshot DOES contain (presence
// implies awake), so a mirror that loaded the same level reveals its copy
// when the authoritative side wakes.
TEST(WorldSnapshot, apply_preserves_dormant_walkers_and_wakes_on_presence)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    walker* soldier = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    soldier->setxy(80, 80);
    walker* wizard = world.add_ob(Order::Living, FAMILY_ARCHMAGE);
    ASSERT_NE(nullptr, wizard);
    wizard->setxy(160, 120);
    wizard->set_spawn_delay(500);
    wizard->set_dormant(true);
    const std::uint32_t wizard_id = wizard->entity_id();

    // Capture excludes the dormant wizard; applying that snapshot back onto
    // the same world (the install seed pattern) must keep him.
    const og::sim::WorldSnapshot seed = og::sim::capture_keyframe_snapshot(world);
    bool in_snapshot = false;
    for (const auto& g : seed.oblist)
        in_snapshot |= (g.entity_id == wizard_id);
    ASSERT_FALSE(in_snapshot) << "dormant walkers stay out of capture";
    og::sim::apply_snapshot(world, seed);
    walker* survivor = nullptr;
    for (auto& uptr : world.oblist)
        if (walker* w = uptr.get(); w != nullptr && w->entity_id() == wizard_id)
            survivor = w;
    ASSERT_NE(nullptr, survivor)
        << "apply must not reconcile away a dormant walker";
    ASSERT_TRUE(survivor->dormant());
    ASSERT_EQ(500, static_cast<int>(survivor->spawn_delay()));

    // Wake it authoritatively and capture again: a mirror world holding its
    // own dormant copy must be woken by the snapshot's presence.
    survivor->set_dormant(false);
    const og::sim::WorldSnapshot awake = og::sim::capture_keyframe_snapshot(world);
    survivor->set_dormant(true); // pretend this world is the mirror now
    og::sim::apply_snapshot(world, awake);
    walker* woken = nullptr;
    for (auto& uptr : world.oblist)
        if (walker* w = uptr.get(); w != nullptr && w->entity_id() == wizard_id)
            woken = w;
    ASSERT_NE(nullptr, woken);
    ASSERT_FALSE(woken->dormant())
        << "presence in a snapshot implies awake — mirrors must reveal";
}

// Regression: a dormant walker survives apply_snapshot (above) but must stay
// OUT of the collision map. rebuild_obmap used to re-add every non-dead
// entity, so the local transport shadow's install seed (apply a display-world
// capture onto the freshly loaded server world) made every not-yet-spawned
// foe collidable — walking into its spawn cell triggered melee against an
// invisible body, and weapons could strike it. Intangible-until-wake is the
// contract: absent from the obmap (no collide, no melee, no eat dispatch —
// they all consult obmap piles), untargetable, then solid after the wake tick.
TEST(WorldSnapshot, install_seed_apply_keeps_dormant_walkers_out_of_the_obmap)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 1u;
    world.my_team = 0;

    walker* hero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    hero->setxy(3 * GRID_SIZE, 3 * GRID_SIZE);
    hero->set_team_num(0);
    hero->set_real_team_num(0);
    hero->set_act_type(ACT_GUARD);

    walker* lurker = world.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, lurker);
    lurker->setxy(12 * GRID_SIZE, 10 * GRID_SIZE);
    lurker->set_team_num(1);
    lurker->set_real_team_num(1);
    lurker->set_act_type(ACT_GUARD);
    lurker->set_spawn_delay(2);
    lurker->set_dormant(true);
    const std::uint32_t lurker_id = lurker->entity_id();

    ASSERT_NE(nullptr, world.myobmap.get());
    ASSERT_EQ(world.myobmap->walker_to_pos.end(),
              world.myobmap->walker_to_pos.find(lurker))
        << "set_dormant(true) must deregister the walker up front";

    // The install seed pattern: capture (which excludes dormant walkers) and
    // apply straight back onto the same world.
    og::sim::apply_snapshot(world, og::sim::capture_keyframe_snapshot(world));

    walker* survivor = world.find_by_id(lurker_id);
    ASSERT_NE(nullptr, survivor) << "reconcile must keep the dormant walker";
    ASSERT_TRUE(survivor->dormant());

    // THE regression: rebuild_obmap must not re-register a dormant walker.
    EXPECT_EQ(world.myobmap->walker_to_pos.end(),
              world.myobmap->walker_to_pos.find(survivor))
        << "a dormant walker must stay out of the obmap through apply";
    EXPECT_TRUE(world.query_object_passable(
        static_cast<float>(survivor->xpos()),
        static_cast<float>(survivor->ypos()), hero))
        << "its spawn cell must read as free to a walker crossing it";
    walker* rehero = world.find_by_id(hero->entity_id());
    ASSERT_NE(nullptr, rehero);
    EXPECT_EQ(nullptr, world.find_far_foe(rehero))
        << "a dormant walker must not be targetable";

    // NEXT WAVE HUD inputs are untouched by intangibility: the walker is
    // still oblist-resident, dormant, with its authored delay (that is all
    // pending_hostile_wave_counts reads).
    EXPECT_EQ(2u, static_cast<unsigned>(survivor->spawn_delay()));

    // Wake normally: tick past the delay; the wake path re-registers it.
    world.tick(); // level tick 1
    world.tick(); // level tick 2
    world.tick(); // level tick 3 > delay 2: wake
    ASSERT_FALSE(survivor->dormant()) << "the wake tick must still fire";
    EXPECT_NE(world.myobmap->walker_to_pos.end(),
              world.myobmap->walker_to_pos.find(survivor))
        << "an awakened walker re-enters the obmap";
    EXPECT_FALSE(world.query_object_passable(
        static_cast<float>(survivor->xpos()),
        static_cast<float>(survivor->ypos()), rehero))
        << "an awakened walker is solid again";
    EXPECT_EQ(survivor, world.find_far_foe(rehero));
}

// The difficulty-submenu world scalars (respawn_mode / generator_rate) are
// world state on the wire: they must survive the exact capture -> serialize
// -> deserialize -> apply path clients use, and the delta merge.
TEST(WorldSnapshot, difficulty_submenu_world_scalars_round_trip_to_mirror)
{
    TestGameWorld source_fx;
    GameWorld& source = source_fx.world();
    configure_snapshot_test_services(source);
    source.resize_grid(32, 32);
    fill_world_grid(source, PIX_GRASS1);

    source.respawn_mode = 2;
    source.generator_rate = 200;

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(source);
    EXPECT_EQ(2, keyframe.respawn_mode);
    EXPECT_EQ(200, keyframe.generator_rate);

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(keyframe);
    const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(bytes);
    EXPECT_EQ(2, decoded.respawn_mode);
    EXPECT_EQ(200, decoded.generator_rate);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    ASSERT_EQ(0, mirror.respawn_mode);
    ASSERT_EQ(0, mirror.generator_rate);
    og::sim::apply_snapshot(mirror, decoded);
    EXPECT_EQ(2, mirror.respawn_mode)
        << "respawn_mode must reach the mirror world through apply";
    EXPECT_EQ(200, mirror.generator_rate)
        << "generator_rate must reach the mirror world through apply";

    // Delta merge carries the scalars onto a stale baseline.
    og::sim::WorldSnapshot baseline;
    og::sim::apply_delta(baseline, decoded);
    EXPECT_EQ(2, baseline.respawn_mode);
    EXPECT_EQ(200, baseline.generator_rate);
}

// Level-entry spawn points ride the entity snapshot: recorded values must
// survive the wire, and a crafted snapshot with a half-set spawn point must
// collapse to the -1/-1 sentinel on apply.
TEST(WorldSnapshot, spawn_point_fields_round_trip_and_clamp_on_apply)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    configure_snapshot_test_services(world);
    world.resize_grid(32, 32);
    fill_world_grid(world, PIX_GRASS1);

    walker* actor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor);
    actor->setxy(80, 80);
    actor->set_spawn_point(120, 88, 1);
    const std::uint32_t actor_id = actor->entity_id();

    const og::sim::WorldSnapshot keyframe =
        og::sim::capture_keyframe_snapshot(world);
    const og::sim::EntitySnapshot* captured = nullptr;
    for (const auto& entity : keyframe.oblist)
        if (entity.entity_id == actor_id)
            captured = &entity;
    ASSERT_NE(nullptr, captured);
    EXPECT_EQ(120, captured->spawn_x);
    EXPECT_EQ(88, captured->spawn_y);
    EXPECT_EQ(1, static_cast<int>(captured->spawn_floor));

    const std::vector<std::uint8_t> bytes = og::sim::serialize_snapshot(keyframe);
    const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(bytes);

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    configure_snapshot_test_services(mirror);
    og::sim::apply_snapshot(mirror, decoded);
    walker* mirrored = nullptr;
    for (auto& uptr : mirror.oblist)
        if (walker* w = uptr.get(); w != nullptr && w->entity_id() == actor_id)
            mirrored = w;
    ASSERT_NE(nullptr, mirrored);
    EXPECT_EQ(120, static_cast<int>(mirrored->spawn_x()))
        << "spawn_x must survive the snapshot wire";
    EXPECT_EQ(88, static_cast<int>(mirrored->spawn_y()));
    EXPECT_EQ(1, static_cast<int>(mirrored->spawn_floor()));

    // Hostile: a negative half collapses the whole spawn point to the sentinel.
    og::sim::WorldSnapshot hostile = decoded;
    for (auto& entity : hostile.oblist)
    {
        if (entity.entity_id == actor_id)
        {
            entity.spawn_x = -5;
            entity.spawn_y = 200;
        }
    }
    og::sim::apply_snapshot(mirror, hostile);
    walker* clamped = nullptr;
    for (auto& uptr : mirror.oblist)
        if (walker* w = uptr.get(); w != nullptr && w->entity_id() == actor_id)
            clamped = w;
    ASSERT_NE(nullptr, clamped);
    EXPECT_EQ(-1, static_cast<int>(clamped->spawn_x()))
        << "half-set spawn points must collapse to the sentinel";
    EXPECT_EQ(-1, static_cast<int>(clamped->spawn_y()));
}
