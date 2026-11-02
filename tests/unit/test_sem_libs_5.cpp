/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch libs-5: the rules the review found in weapon_banner.lua, weapon_knife.lua, weapon_projectiles.lua, weapon_rock.lua, lineup_stage.lua.
// Each test is named after the rule it asserts.
//
// Every test runs headless. The weapon rules run on a plain TestGameWorld
// and reach the class-pack Lua through the engine's own doors (weap::act,
// weap::death, the family on_death dispatch); each fails if a hook errored
// (ScopedHookFailureGuard). The lineup rules run the shipped packs/core
// scripts/lineup_stage.lua on a mode-less world wired to the real loader
// with gladiator mounted (the ClassicLineupTest shape of
// test_staged_rules.cpp): through the tick-1 lazy arm where placement draws
// nothing, and through GameWorld::run_lineup_stage_step() under a scripted
// sim RNG where the teleport draw is the rule, so every landing is exact.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_save_state_guard.h"
#include "../test_sim_random_scope.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/fearless.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// --- the weapon rig --------------------------------------------------------

constexpr unsigned char kUs = 0;
constexpr unsigned char kThem = 1;

int weapon_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::Weapon, id);
}

// A 32x32-tile grass field (512 x 512 px), nothing else.
void open_field(TestGameWorld& tw)
{
    GameWorld& world = tw.world();
    world.resize_grid(32, 32);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.mysmoother.set_target(world.grid);
    world.my_team = 0;
}

void paint_tile(GameWorld& world, int tx, int ty, int pix)
{
    world.grid.data[static_cast<std::size_t>(tx + ty * world.grid.w)] =
        static_cast<std::uint8_t>(pix);
}

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y, short level = 1)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    w->set_busy(0);
    return w;
}

std::vector<walker*> live_of(GameWorld& w, Order order, int family)
{
    std::vector<walker*> out;
    for (const GameWorld::EntityList* list : {&w.oblist, &w.fxlist, &w.weaplist})
        for (const auto& ob : *list)
            if (ob != nullptr && ob->dead() == 0 && ob->query_order() == order &&
                static_cast<int>(static_cast<unsigned char>(ob->family())) == family)
                out.push_back(ob.get());
    return out;
}

// The one entity in `after` that is not in `before`, or nullptr.
walker* only_new(const std::vector<walker*>& before, const std::vector<walker*>& after)
{
    walker* found = nullptr;
    for (walker* w : after) {
        if (std::find(before.begin(), before.end(), w) != before.end())
            continue;
        if (found != nullptr)
            return nullptr;
        found = w;
    }
    return found;
}

const command* front_command(const walker* w)
{
    if (w->stats()->commands.empty())
        return nullptr;
    return &w->stats()->commands.front();
}

int sign_of(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

// A level-10 team-0 captain at (200,200) with 500 MP plants his banner on
// the corpse at his feet through the real special (test_kit_captain.cpp's
// plant_banner shape).
walker* plant_banner(TestGameWorld& tw)
{
    walker* captain = add_living(tw, FAMILY_BIG_ORC, kUs, 200, 200, 10);
    if (captain == nullptr)
        return nullptr;
    captain->stats()->set_max_magicpoints(500.0f);
    captain->stats()->set_magicpoints(500.0f);
    walker* stain = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(kThem);
    stain->set_dead(0);
    stain->set_floor(captain->floor());
    stain->center_on(captain);
    stain->stats()->set_old_family(FAMILY_SOLDIER);
    stain->stats()->set_level(3);
    captain->set_current_special(4);
    captain->set_shifter_down(0);
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    EXPECT_TRUE(captain->special(&why, &reason)) << reason;
    const auto b = live_of(tw.world(), Order::Weapon, weapon_family("core:war_banner"));
    return b.size() == 1 ? b.front() : nullptr;
}

}  // namespace

// ------------------------------------------------------- weapon_banner.lua

// weapon_banner.lua:15. A pulse heals every ally in banner_radius on the
// banner's own floor by banner_regen (1), capped at max hp; an ally standing
// at the same x/y one floor up gets nothing. (New Specials: the captain who
// plants the banner exists only with the setting on; the setting-off twin,
// no banner at all, is KitCaptain.captain_has_no_specials_when_off.)
TEST(SemLibsBanner, a_banner_heals_only_the_allies_on_its_own_floor)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().set_floor_count(2);
    tw.world().new_specials = 1;
    walker* banner = plant_banner(tw);
    ASSERT_NE(nullptr, banner);
    ASSERT_EQ(0, banner->floor());
    walker* below = add_living(tw, FAMILY_ELF, kUs, 240, 200);
    walker* above = add_living(tw, FAMILY_ELF, kUs, 240, 200);
    walker* full = add_living(tw, FAMILY_ELF, kUs, 220, 220);
    ASSERT_TRUE(below && above && full);
    above->set_floor(1);
    below->stats()->set_hitpoints(100.0f);
    above->stats()->set_hitpoints(100.0f);

    tw.world().tick_count_ = 808;  // 808 % banner_pulse (8) == 0
    banner->act();
    EXPECT_FLOAT_EQ(101.0f, below->stats()->hitpoints())
        << "arm: ally in radius, same floor -> hp + banner_regen";
    EXPECT_FLOAT_EQ(100.0f, above->stats()->hitpoints())
        << "arm: ally in radius, other floor -> hp unchanged";
    EXPECT_FLOAT_EQ(200.0f, full->stats()->hitpoints())
        << "arm: same floor, already at max -> clamped at max";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_banner.lua:22-24, the arms that are not issue #475 (the fright is
// not floor-gated). A pulse frightens a LIVING foe weaker than the banner;
// a weak foe generator is not frightened (the order gate), and a weak foe
// standing inside its OWN side's banner's rally_radius holds (the
// s_force_fright binding skips a fearless walker), while its twin outside
// that radius runs, away from the banner on each axis.
TEST(SemLibsBanner, a_banner_frightens_no_generator_and_no_foe_rallied_by_its_own_banner)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* banner = plant_banner(tw);
    ASSERT_NE(nullptr, banner);
    ASSERT_EQ(10, banner->stats()->level());

    walker* tent = tw.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(kThem);
    tent->set_real_team_num(255);
    tent->setxy(200, 140);
    tent->stats()->set_level(1);
    tent->stats()->clear_command();

    walker* rallied = add_living(tw, FAMILY_SOLDIER, kThem, 260, 200, 3);
    walker* loose = add_living(tw, FAMILY_SOLDIER, kThem, 200, 260, 3);
    ASSERT_TRUE(rallied && loose);
    rallied->stats()->clear_command();
    loose->stats()->clear_command();
    walker* their_banner =
        tw.world().add_weap_ob(Order::Weapon, weapon_family("core:war_banner"));
    ASSERT_NE(nullptr, their_banner);
    their_banner->set_team_num(kThem);
    their_banner->setxy(310, 200);
    their_banner->set_dead(0);
    ASSERT_TRUE(og::sim::fearless(*rallied)) << "staging: inside its own banner's rally";
    ASSERT_FALSE(og::sim::fearless(*loose)) << "staging: outside it";
    ASSERT_LE(banner->distance_to_ob(tent), 96) << "staging: the tent is in reach";
    ASSERT_LE(banner->distance_to_ob(rallied), 96) << "staging: in reach";

    tw.world().tick_count_ = 808;
    banner->act();
    EXPECT_EQ(nullptr, front_command(tent))
        << "arm: generator foe -> no fright (order gate)";
    EXPECT_EQ(nullptr, front_command(rallied))
        << "arm: fearless foe inside its own banner's rally_radius -> no command";
    const command* run = front_command(loose);
    ASSERT_NE(nullptr, run) << "control: the same weak foe outside a rally runs";
    EXPECT_EQ(COMMAND_WALK, run->commandtype);
    EXPECT_EQ(sign_of(loose->xpos() - banner->xpos()), run->com1);
    EXPECT_EQ(1, run->com2) << "away from the banner (south)";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// -------------------------------------------------------- weapon_knife.lua

// weapon_knife.lua:18, the orphan quirk (old game weap::death: `owner->
// query_family() != FAMILY_SOLDIER`, byte 0, is the same gate). A knife whose
// thrower died is re-owned by itself on its next act (weap::act), and the
// knife's own family byte 0 reads as the soldier's has_returning_weapon, so
// it still spawns one knife_back on death, owned by the dead knife, with the
// knife's flight copied.
TEST(SemLibsKnife, a_classic_orphaned_knife_returns_owned_by_itself)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    walker* thrower = add_living(tw, FAMILY_SOLDIER, kUs, 100, 100);
    ASSERT_NE(nullptr, thrower);
    walker* knife = tw.world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    ASSERT_EQ(0, static_cast<int>(knife->family())) << "the knife is weapon family 0";
    knife->set_team_num(kUs);
    knife->set_owner(thrower);
    knife->setxy(200, 200);
    knife->set_lastx(3);
    knife->set_lasty(-2);
    knife->set_stepsize(4);
    knife->set_damage(9.0f);
    thrower->set_dead(1);

    knife->act();
    ASSERT_EQ(knife, knife->owner()) << "weap::act re-owns an orphaned knife to itself";

    knife->set_dead(1);
    const auto before = live_of(tw.world(), Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_TRUE(knife->death());
    const auto after = live_of(tw.world(), Order::FX, FAMILY_KNIFE_BACK);
    ASSERT_EQ(before.size() + 1, after.size())
        << "arm: thrower died, owner defaulted to the knife -> one knife_back";
    walker* blade = only_new(before, after);
    ASSERT_NE(nullptr, blade);
    EXPECT_EQ(knife, blade->owner()) << "the returning blade belongs to the dead knife";
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(blade->ani_type()));
    EXPECT_FLOAT_EQ(3.0f, blade->lastx());
    EXPECT_FLOAT_EQ(-2.0f, blade->lasty());
    EXPECT_FLOAT_EQ(9.0f, blade->damage());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// -------------------------------------------------- weapon_projectiles.lua

// weapon_projectiles.lua:12 (old game: `if (!owner || owner->dead) owner =
// this;`). An armed fire arrow whose archer is dead, or which has no owner
// at all, credits its explosion to itself: the explosion's owner is the
// arrow and its level is the arrow's own (2), not the dead archer's (7).
TEST(SemLibsProjectiles, an_explosion_whose_shooter_is_dead_or_gone_is_credited_to_the_projectile)
{
    for (const bool has_owner : {true, false}) {
        SCOPED_TRACE(has_owner ? "owner dead" : "owner nil");
        og::test::ScopedHookFailureGuard guard;
        TestGameWorld tw;
        open_field(tw);
        walker* archer = add_living(tw, FAMILY_ARCHER, kUs, 100, 100, 7);
        ASSERT_NE(nullptr, archer);
        walker* arrow = tw.world().add_ob(Order::Weapon, FAMILY_FIRE_ARROW);
        ASSERT_NE(nullptr, arrow);
        arrow->setxy(200, 200);
        arrow->set_owner(has_owner ? archer : nullptr);
        arrow->set_skip_exit(1);
        arrow->set_damage(7.0f);
        arrow->stats()->set_level(2);
        archer->set_dead(1);
        arrow->set_dead(1);

        const auto before = live_of(tw.world(), Order::FX, FAMILY_EXPLOSION);
        ASSERT_TRUE(arrow->death());
        const auto after = live_of(tw.world(), Order::FX, FAMILY_EXPLOSION);
        ASSERT_EQ(before.size() + 1, after.size()) << "one explosion";
        walker* boom = only_new(before, after);
        ASSERT_NE(nullptr, boom);
        EXPECT_EQ(arrow, boom->owner())
            << "arm: owner nil or dead -> the explosion is the projectile's";
        EXPECT_EQ(2, static_cast<int>(boom->stats()->level()))
            << "arm: owner nil or dead -> the projectile's own level";
        EXPECT_FLOAT_EQ(14.0f, boom->damage());
        EXPECT_EQ(0u, guard.count()) << guard.message();
    }
}

// --------------------------------------------------------- weapon_rock.lua

namespace {

// The bounce-matrix rig (test_weap_behavior.cpp): a bouncing rock at
// (64,64) flying (+16,+16). Every probe cell starts as a wall; the caller
// opens the cells it wants.
struct RockRig {
    TestGameWorld tw;
    weap* rock = nullptr;
    const WeaponFamilyDescriptor* desc = nullptr;

    RockRig()
    {
        open_field(tw);
        for (const auto& [tx, ty] : {std::pair{5, 5}, {3, 5}, {5, 3}, {3, 3}})
            paint_tile(tw.world(), tx, ty, PIX_H_WALL1);
        walker* w = tw.world().add_ob(Order::Weapon, FAMILY_ROCK);
        rock = static_cast<weap*>(w);
        desc = get_weapon_family_descriptor(FAMILY_ROCK);
        if (rock == nullptr)
            return;
        rock->setxy(64, 64);
        rock->set_lastx(16);
        rock->set_lasty(16);
        rock->set_lineofsight(1);
        rock->set_do_bounce(1);
        rock->set_collide_ob(nullptr);
        rock->set_dead(1);
        rock->set_death_called(1);  // as weap::death leaves it
    }
};

}  // namespace

// weapon_rock.lua:8 (old game: `if (!do_bounce || !lineofsight ||
// collide_ob) break;`). A bouncing rock that ran out of range dies where it
// is, even against a wall with an open bounce cell.
TEST(SemLibsRock, a_bouncing_rock_out_of_range_dies_without_bouncing)
{
    og::test::ScopedHookFailureGuard guard;
    RockRig rig;
    ASSERT_TRUE(rig.rock != nullptr && rig.desc != nullptr);
    paint_tile(rig.tw.world(), 3, 5, PIX_GRASS1);  // the X-flip cell is open
    rig.rock->set_lineofsight(0);
    EXPECT_FALSE(og::test::on_death(*rig.desc, rig.rock))
        << "arm: lineofsight 0 -> dies normally";
    EXPECT_EQ(1, static_cast<int>(rig.rock->dead()));
    EXPECT_EQ(64, rig.rock->xpos()) << "arm: lineofsight 0 -> no reposition";
    EXPECT_EQ(64, rig.rock->ypos());
    EXPECT_FLOAT_EQ(16.0f, rig.rock->lastx());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_rock.lua:8. A bouncing rock that struck a walker dies on it, even
// against a wall with an open bounce cell.
TEST(SemLibsRock, a_bouncing_rock_that_hit_a_walker_dies_without_bouncing)
{
    og::test::ScopedHookFailureGuard guard;
    RockRig rig;
    ASSERT_TRUE(rig.rock != nullptr && rig.desc != nullptr);
    paint_tile(rig.tw.world(), 3, 5, PIX_GRASS1);
    walker* orc = add_living(rig.tw, FAMILY_ORC, kThem, 80, 64);
    ASSERT_NE(nullptr, orc);
    rig.rock->set_collide_ob(orc);
    EXPECT_FALSE(og::test::on_death(*rig.desc, rig.rock))
        << "arm: collide_ob set -> dies normally";
    EXPECT_EQ(1, static_cast<int>(rig.rock->dead()));
    EXPECT_EQ(64, rig.rock->xpos()) << "arm: collide_ob set -> no reposition";
    EXPECT_EQ(64, rig.rock->ypos());
    EXPECT_FLOAT_EQ(16.0f, rig.rock->lastx());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_rock.lua:24 (old game weap::death FAMILY_ROCK, same probe order).
// Forward cell walled, X-flip cell open: the rock moves to (x-dx, y+dy) =
// (48,80), turns back on X, and lives on (dead 0, death_called 0).
TEST(SemLibsRock, a_rock_bouncing_off_x_moves_to_the_x_flip_cell_and_lives)
{
    og::test::ScopedHookFailureGuard guard;
    RockRig rig;
    ASSERT_TRUE(rig.rock != nullptr && rig.desc != nullptr);
    paint_tile(rig.tw.world(), 3, 5, PIX_GRASS1);
    EXPECT_TRUE(og::test::on_death(*rig.desc, rig.rock));
    EXPECT_EQ(48, rig.rock->xpos()) << "arm: X-flip -> x - dx";
    EXPECT_EQ(80, rig.rock->ypos()) << "arm: X-flip -> y + dy";
    EXPECT_FLOAT_EQ(-16.0f, rig.rock->lastx());
    EXPECT_FLOAT_EQ(16.0f, rig.rock->lasty());
    EXPECT_EQ(0, static_cast<int>(rig.rock->dead()));
    EXPECT_EQ(0, static_cast<int>(rig.rock->death_called()))
        << "arm: X-flip -> death_called cleared so it can die again";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_rock.lua:33. Forward and X-flip walled, Y-flip open: (x+dx, y-dy)
// = (80,48), Y velocity reversed, alive.
TEST(SemLibsRock, a_rock_bouncing_off_y_moves_to_the_y_flip_cell_and_lives)
{
    og::test::ScopedHookFailureGuard guard;
    RockRig rig;
    ASSERT_TRUE(rig.rock != nullptr && rig.desc != nullptr);
    paint_tile(rig.tw.world(), 5, 3, PIX_GRASS1);
    EXPECT_TRUE(og::test::on_death(*rig.desc, rig.rock));
    EXPECT_EQ(80, rig.rock->xpos()) << "arm: Y-flip -> x + dx";
    EXPECT_EQ(48, rig.rock->ypos()) << "arm: Y-flip -> y - dy";
    EXPECT_FLOAT_EQ(16.0f, rig.rock->lastx());
    EXPECT_FLOAT_EQ(-16.0f, rig.rock->lasty());
    EXPECT_EQ(0, static_cast<int>(rig.rock->dead()));
    EXPECT_EQ(0, static_cast<int>(rig.rock->death_called()));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_rock.lua:42. Forward, X-flip and Y-flip walled, both-flip open:
// (x-dx, y-dy) = (48,48), both velocities reversed, alive.
TEST(SemLibsRock, a_rock_bouncing_off_both_axes_moves_to_the_corner_cell_and_lives)
{
    og::test::ScopedHookFailureGuard guard;
    RockRig rig;
    ASSERT_TRUE(rig.rock != nullptr && rig.desc != nullptr);
    paint_tile(rig.tw.world(), 3, 3, PIX_GRASS1);
    EXPECT_TRUE(og::test::on_death(*rig.desc, rig.rock));
    EXPECT_EQ(48, rig.rock->xpos()) << "arm: both-flip -> x - dx";
    EXPECT_EQ(48, rig.rock->ypos()) << "arm: both-flip -> y - dy";
    EXPECT_FLOAT_EQ(-16.0f, rig.rock->lastx());
    EXPECT_FLOAT_EQ(-16.0f, rig.rock->lasty());
    EXPECT_EQ(0, static_cast<int>(rig.rock->dead()));
    EXPECT_EQ(0, static_cast<int>(rig.rock->death_called()));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ======================================================== lineup_stage.lua

namespace {

constexpr std::int32_t kBotMarkBit = 65536;  // lineup.lua BOT_MARK_BIT

loader& lineup_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

// A mode-less world wired to the real loader (test_staged_rules.cpp's
// ClassicWorld): GameWorld::tick's classic branch runs the shipped lineup
// stage at tick 1, and run_lineup_stage_step() runs the same step by itself.
struct LineupWorld : TestGameWorld
{
    LineupWorld() : TestGameWorld(9800)
    {
        loader* gl = &lineup_loader();
        world().entity_factory = [gl](Order order, std::int32_t family) {
            return gl->create_walker_owned(order, family);
        };
        world().entity_configurator = [gl](walker& entity, Order order,
                                           std::int32_t family) -> const PixieData* {
            gl->set_walker(&entity, order, family);
            return gl->graphics_for(entity.query_order(), entity.family());
        };
        world().entity_derived_stats = [gl](walker* entity, Order order,
                                            std::int32_t family) {
            if (entity != nullptr)
                gl->set_derived_stats(entity, order, family);
        };
    }

    walker* thing(Order order, int family, int team, int x, int y)
    {
        walker* w = world().add_ob(order, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        return w;
    }

    // A sitting authored living (team 4 = wildlife: hostile to all, in no
    // team's census).
    walker* npc(int family, int team, int x, int y)
    {
        walker* w = thing(Order::Living, family, team, x, y);
        if (w == nullptr)
            return nullptr;
        w->set_real_team_num(255);
        w->set_act_type(ACT_SIT);
        return w;
    }

    // A deployed roster fighter (carries a guy).
    walker* hero(int team, int x, int y, int guy_id = 100)
    {
        walker* w = npc(FAMILY_SOLDIER, team, x, y);
        if (w == nullptr)
            return nullptr;
        w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        w->myguy->id = guy_id;
        return w;
    }

    walker* marker(int team, int x, int y)
    {
        return thing(Order::Special, FAMILY_RESERVED_TEAM, team, x, y);
    }

    void fill(int team, std::int16_t code)
    {
        world().ctf_requested_fill[static_cast<std::size_t>(team)] = code;
    }

    void wall(int tx, int ty)
    {
        paint_tile(world(), tx, ty, PIX_H_WALL1);
    }
};

// The FILL squad on `team` (bot-marked, guy-less live livings), in spawn
// order: core:soldier, archer, elf, mage, thief (lineup.BOT_SQUAD).
std::vector<walker*> squad_on(GameWorld& world, int team)
{
    std::vector<walker*> out;
    for (const auto& entry : world.oblist) {
        walker* w = entry.get();
        if (w == nullptr || w->dead() || w->query_order() != Order::Living ||
            w->myguy != nullptr || w->team_num() != static_cast<unsigned char>(team))
            continue;
        if ((w->stats()->bit_flags() & kBotMarkBit) != 0)
            out.push_back(w);
    }
    return out;
}

using Cell = std::pair<int, int>;

Cell cell_of(const walker* w)
{
    return {static_cast<int>(w->xpos()), static_cast<int>(w->ypos())};
}

std::vector<Cell> cells_of(const std::vector<walker*>& ws)
{
    std::vector<Cell> out;
    for (const walker* w : ws)
        out.push_back(cell_of(w));
    return out;
}

// The first member's cell, or (-99,-99) when the squad did not field.
Cell first_member(GameWorld& world, int team)
{
    const auto s = squad_on(world, team);
    if (s.empty() || s.front()->family() != FAMILY_SOLDIER)
        return {-99, -99};
    return cell_of(s.front());
}

// The applied FILL code banked for `team` in the lineup-facts slot
// (mode var 4; ones digit is the announce latch, then two digits a team).
int fact_code(std::int32_t slot_value, int team)
{
    std::int64_t facts = static_cast<std::int64_t>(slot_value) / 10;
    for (int t = 0; t < team; ++t)
        facts /= 100;
    return static_cast<int>(facts % 100);
}

// A scripted sim stream: answers `vals` in order, then `fill` forever, and
// counts every draw. walker::teleport draws next(grid.w) then next(grid.h),
// so each pair below is one landing tile.
class ScriptedDraws : public IRandom
{
public:
    ScriptedDraws(std::vector<std::uint32_t> vals, std::uint32_t fill)
        : vals_(std::move(vals)), fill_(fill)
    {
    }
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        ++calls;
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = idx_ < vals_.size() ? vals_[idx_++] : fill_;
        if (v >= max_exclusive)
            ++out_of_range;
        return v % max_exclusive;
    }
    int calls = 0;
    int out_of_range = 0;

private:
    std::vector<std::uint32_t> vals_;
    std::size_t idx_ = 0;
    std::uint32_t fill_;
};

std::vector<std::uint32_t> landings(const std::vector<Cell>& tiles)
{
    std::vector<std::uint32_t> out;
    for (const auto& [tx, ty] : tiles) {
        out.push_back(static_cast<std::uint32_t>(tx));
        out.push_back(static_cast<std::uint32_t>(ty));
    }
    return out;
}

class SemLineupStage : public ::testing::Test
{
protected:
    og::test::ScopedCampaignMountState mount_restore_;

    void SetUp() override
    {
        restore_default_campaigns();
        restore_default_settings();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("gladiator"));
    }
};

// The lattice scene the site-less rows share: a roster hero on team 0 at
// (96,96) (team 0's centroid) and a team-2 start marker at (480,96) (team
// 2's), team 1 authors nothing. The scan reaches (480+96, 96+96) = (576,192)
// on a 32 px lattice; the score is the squared distance to the NEARER
// centroid, maximal (192^2 + 96^2 = 46080) at (288,0) and (288,192) alike,
// and the row-major first, (288,0), wins.
void lattice_scene(LineupWorld& fx, bool with_marker = true)
{
    fx.hero(0, 96, 96);
    if (with_marker)
        fx.marker(2, 480, 96);
    fx.fill(1, og::sim::kFillStrong);
}

}  // namespace

// lineup_stage.lua:64. The census skips dead and dormant walkers: three
// team-1 soldiers whose centroid snaps to (112,96) anchor the squad there
// when awake, but dormant or dead they are no units, and the squad anchors
// at the team's start marker (320,480).
TEST_F(SemLineupStage, the_census_counts_only_live_awake_walkers)
{
    for (const char* mode : {"awake", "dormant", "dead"}) {
        SCOPED_TRACE(mode);
        LineupWorld fx;
        fx.marker(1, 320, 480);
        for (const auto& [x, y] : {Cell{96, 96}, Cell{128, 96}, Cell{112, 128}}) {
            walker* s = fx.npc(FAMILY_SOLDIER, 1, x, y);
            ASSERT_NE(nullptr, s);
            if (std::string(mode) == "dormant") {
                s->set_spawn_delay(100);  // a delayed spawn, asleep at tick 1
                s->set_dormant(true);
            }
            if (std::string(mode) == "dead")
                s->set_dead(1);
        }
        fx.fill(1, og::sim::kFillStrong);
        fx.world().tick();
        const Cell want = std::string(mode) == "awake" ? Cell{112, 96} : Cell{320, 480};
        EXPECT_EQ(want, first_member(fx.world(), 1))
            << (std::string(mode) == "awake"
                    ? "arm: live and not dormant -> counted, anchor at the units' centroid"
                    : "arm: dead or dormant -> ignored, anchor at the marker");
        EXPECT_TRUE(fx.world().scripts().host().errors().empty());
    }
}

// lineup_stage.lua:69. A team's start marker is its FIRST live marker in
// list order: markers at (320,480) then (560,800) anchor at (320,480).
TEST_F(SemLineupStage, the_first_start_marker_of_a_team_anchors_its_squad)
{
    LineupWorld fx;
    fx.marker(1, 320, 480);
    fx.marker(1, 560, 800);
    fx.fill(1, og::sim::kFillStrong);
    fx.world().tick();
    const auto squad = squad_on(fx.world(), 1);
    ASSERT_EQ(5u, squad.size());
    EXPECT_EQ((Cell{320, 480}), cell_of(squad.front()))
        << "arm: first marker -> the anchor";
    for (const walker* w : squad)
        EXPECT_GT(std::abs(w->xpos() - 560) + std::abs(w->ypos() - 800), 48)
            << "arm: second marker -> ignored (member at " << w->xpos() << ","
            << w->ypos() << ")";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:76. Only livings and generators are troops: a team-1
// stain (treasure) and a team-1 marker effect standing at (96,96)/(128,96)
// are not authored units, so the squad anchors at the team's marker, not
// at their centroid.
TEST_F(SemLineupStage, items_and_effects_on_a_team_are_not_its_units)
{
    LineupWorld fx;
    fx.marker(1, 320, 480);
    ASSERT_NE(nullptr, fx.thing(Order::Treasure, FAMILY_STAIN, 1, 96, 96));
    ASSERT_NE(nullptr, fx.thing(Order::FX, FAMILY_MARKER, 1, 128, 96));
    fx.fill(1, og::sim::kFillStrong);
    fx.world().tick();
    EXPECT_EQ((Cell{320, 480}), first_member(fx.world(), 1))
        << "arm: weapon/fx/treasure -> not counted";
    EXPECT_EQ(5u, squad_on(fx.world(), 1).size());
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:77 and :295. A troop carrying a guy is the team's
// roster, and a turned wheel on a team with a roster fighter fields nothing
// and banks nothing (allies are a match-mode rule). The scene gives the
// squad seam a reason to field on team 0 if the classic gate let it: team
// 0's level-1 hero is far weaker than team 2's level-9 hero, so the
// allies' gap is positive. Team 1's marker beside it fields five and
// banks STRONG (3).
TEST_F(SemLineupStage, a_team_with_a_deployed_fighter_fields_no_squad)
{
    LineupWorld fx;
    walker* weak = fx.hero(0, 96, 96, 100);
    walker* strong = fx.hero(2, 480, 96, 101);
    ASSERT_TRUE(weak && strong);
    strong->stats()->set_level(9);
    strong->stats()->set_max_hitpoints(2000.0f);
    strong->stats()->set_hitpoints(2000.0f);
    fx.marker(1, 320, 480);
    fx.fill(0, og::sim::kFillStrong);
    fx.fill(1, og::sim::kFillStrong);
    fx.world().tick();
    EXPECT_EQ(0u, squad_on(fx.world(), 0).size())
        << "arm: knob set, roster on team -> no squad";
    EXPECT_EQ(0, fact_code(fx.world().mode.vars[4], 0))
        << "arm: knob set, roster on team -> no fact banked";
    EXPECT_EQ(5u, squad_on(fx.world(), 1).size()) << "control: the marker team fields";
    EXPECT_EQ(3000, fx.world().mode.vars[4]) << "only team 1 banks (STRONG)";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:99, :118, :216, :222, :339 on the lattice scene. A team
// with no units and no marker is site-less; the existing teams' centroids
// are the roster hero's (bodies) and the team-2 marker's (no bodies); the
// cell score is the distance to the NEARER of them; the best cell is the
// row-major first of the tied maxima, (288,0); the first member takes it,
// and every later member rings around that same anchor: (304,0), (288,16),
// (272,0), then (320,0) (the north cells are off the map).
TEST_F(SemLineupStage, a_siteless_squad_anchors_farthest_from_every_team_and_rings_there)
{
    LineupWorld fx;
    lattice_scene(fx);
    fx.world().tick();
    const auto squad = squad_on(fx.world(), 1);
    ASSERT_EQ(5u, squad.size());
    ASSERT_EQ(FAMILY_SOLDIER, static_cast<int>(squad.front()->family()));
    EXPECT_EQ((Cell{288, 0}), cell_of(squad.front()))
        << "arms: neither units nor marker -> farthest region; bodies and marker "
           "centroids; nearer-centroid score; first of the tied best cells";
    EXPECT_EQ((std::vector<Cell>{{288, 0}, {304, 0}, {288, 16}, {272, 0}, {320, 0}}),
              cells_of(squad))
        << "arm: later members reuse the first member's anchor";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:118, the nothing arm (and the marker arm by contrast).
// Drop the team-2 marker: only the hero's centroid exists (teams 1-3 have
// nothing and add none), the scan reaches (192,192), and the first of the
// four equally far corners, (0,0), is the anchor.
TEST_F(SemLineupStage, a_team_with_nothing_adds_no_centroid)
{
    LineupWorld fx;
    lattice_scene(fx, /*with_marker=*/false);
    fx.world().tick();
    EXPECT_EQ((Cell{0, 0}), first_member(fx.world(), 1))
        << "arm: nothing -> no centroid";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:222. The best-scoring cell is skipped when the probing
// member cannot stand there (a wall on (288,0)) or it is next to a hostile
// (wildlife at (304,16)); the other best cell, (288,192), wins instead.
TEST_F(SemLineupStage, a_blocked_or_hostile_adjacent_best_cell_is_passed_over)
{
    for (const bool hostile : {false, true}) {
        SCOPED_TRACE(hostile ? "hostile-adjacent" : "blocked");
        LineupWorld fx;
        lattice_scene(fx);
        if (hostile) {
            ASSERT_NE(nullptr, fx.npc(FAMILY_SOLDIER, 4, 304, 16));
        } else {
            fx.wall(18, 0);
        }
        fx.world().tick();
        EXPECT_EQ((Cell{288, 192}), first_member(fx.world(), 1))
            << "arm: better score but hostile-adjacent or blocked -> skipped";
        EXPECT_TRUE(fx.world().scripts().host().errors().empty());
    }
}

// lineup_stage.lua:222, the no-admissible-cell arm. Every lattice cell the
// scan reaches is walled, so the scan answers (-1,-1) and each member takes
// the bounded safe teleport draw: one scripted landing each, far from the
// hero, exactly where the draw put it (ten draws, nothing else drawn).
TEST_F(SemLineupStage, with_no_admissible_cell_the_siteless_squad_teleports)
{
    LineupWorld fx;
    lattice_scene(fx);
    for (int tx = 0; tx <= 36; tx += 2)
        for (int ty = 0; ty <= 12; ty += 2)
            fx.wall(tx, ty);
    const std::vector<Cell> tiles = {{21, 21}, {23, 21}, {25, 21}, {27, 21}, {29, 21}};
    ScriptedDraws draws(landings(tiles), 0);
    {
        ScopedSimRandom scope(&draws);
        ASSERT_TRUE(fx.world().run_lineup_stage_step());
    }
    EXPECT_EQ((std::vector<Cell>{{336, 336}, {368, 336}, {400, 336}, {432, 336}, {464, 336}}),
              cells_of(squad_on(fx.world(), 1)))
        << "arm: no admissible cell -> (-1,-1) -> the safe teleport";
    EXPECT_EQ(10, draws.calls) << "one landing (two draws) per member";
    EXPECT_EQ(0, draws.out_of_range);
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:137. A site-less squad's hostiles are LIVE, AWAKE
// LIVINGS AND GENERATORS of other teams: a dormant or a dead team-2 soldier,
// or a team-2 stain, at (16,16) next to the best cell (0,0) does not refuse
// it; an awake wildlife soldier there does (the anchor moves to (192,0)).
// (The own-team exclusion cannot be staged: a team-1 troop is a census unit
// and makes team 1 sited; see the report.)
TEST_F(SemLineupStage, a_dormant_or_dead_walker_or_an_item_is_no_hostile)
{
    struct Case {
        const char* what;
        Cell want;
    };
    for (const Case c : {Case{"dormant team-2 soldier", {0, 0}},
                         Case{"dead team-2 soldier", {0, 0}},
                         Case{"team-2 stain", {0, 0}},
                         Case{"awake wildlife (control)", {192, 0}}}) {
        SCOPED_TRACE(c.what);
        LineupWorld fx;
        lattice_scene(fx, /*with_marker=*/false);
        const std::string what = c.what;
        walker* w = nullptr;
        if (what == "team-2 stain")
            w = fx.thing(Order::Treasure, FAMILY_STAIN, 2, 16, 16);
        else
            w = fx.npc(FAMILY_SOLDIER, what.find("wildlife") != std::string::npos ? 4 : 2,
                       16, 16);
        ASSERT_NE(nullptr, w);
        if (what.find("dormant") != std::string::npos) {
            w->set_spawn_delay(100);  // a delayed spawn, asleep at tick 1
            w->set_dormant(true);
        }
        if (what.find("dead") != std::string::npos)
            w->set_dead(1);
        fx.world().tick();
        EXPECT_EQ(c.want, first_member(fx.world(), 1))
            << "arm: own team / dead / dormant / non-troop -> not a hostile";
        EXPECT_TRUE(fx.world().scripts().host().errors().empty());
    }
}

// lineup_stage.lua:158. A cell is hostile-adjacent only within one tile on
// BOTH axes: wildlife two tiles right of (0,0), or two tiles below it,
// leaves (0,0) the anchor; one tile diagonally, (16,16), refuses it.
TEST_F(SemLineupStage, a_hostile_two_tiles_away_does_not_refuse_a_cell)
{
    for (const auto& [where, want] : {std::pair{Cell{32, 0}, Cell{0, 0}},
                                      std::pair{Cell{0, 32}, Cell{0, 0}},
                                      std::pair{Cell{16, 16}, Cell{192, 0}}}) {
        SCOPED_TRACE(::testing::Message() << "wildlife at " << where.first << ","
                                          << where.second);
        LineupWorld fx;
        lattice_scene(fx, /*with_marker=*/false);
        ASSERT_NE(nullptr, fx.npc(FAMILY_SOLDIER, 4, where.first, where.second));
        fx.world().tick();
        EXPECT_EQ(want, first_member(fx.world(), 1))
            << "arm: two tiles away on either axis -> allowed; within one -> refused";
        EXPECT_TRUE(fx.world().scripts().host().errors().empty());
    }
}

// lineup_stage.lua:179. The scan reaches only the authored extent plus six
// tiles: on a 100x100-tile field with the hero at (64,64) the anchor is
// (160,160), the far corner of (0..160)^2, never the open ground beyond;
// one wildlife soldier at (1200,1200) widens the reach to (1296,1296) and
// the anchor goes to (1280,1280).
TEST_F(SemLineupStage, the_siteless_scan_reaches_only_the_authored_extent)
{
    for (const bool far_walker : {false, true}) {
        SCOPED_TRACE(far_walker ? "widened" : "authored extent");
        LineupWorld fx;
        fx.world().resize_grid(100, 100);
        fx.hero(0, 64, 64);
        if (far_walker) {
            ASSERT_NE(nullptr, fx.npc(FAMILY_SOLDIER, 4, 1200, 1200));
        }
        fx.fill(1, og::sim::kFillStrong);
        fx.world().tick();
        EXPECT_EQ((far_walker ? Cell{1280, 1280} : Cell{160, 160}),
                  first_member(fx.world(), 1))
            << (far_walker ? "arm: entity farther right/down -> reach widened"
                           : "arm: otherwise -> reach unchanged (max x/y + 96)");
        EXPECT_TRUE(fx.world().scripts().host().errors().empty());
    }
}

// lineup_stage.lua:200. With no team anywhere there is no region to flee:
// every member takes the safe teleport draw, one scripted landing each.
TEST_F(SemLineupStage, a_map_with_no_team_scatters_the_siteless_squad_by_teleport)
{
    LineupWorld fx;
    fx.fill(1, og::sim::kFillStrong);
    const std::vector<Cell> tiles = {{5, 5}, {10, 5}, {15, 5}, {20, 5}, {25, 5}};
    ScriptedDraws draws(landings(tiles), 0);
    {
        ScopedSimRandom scope(&draws);
        ASSERT_TRUE(fx.world().run_lineup_stage_step());
    }
    EXPECT_EQ((std::vector<Cell>{{80, 80}, {160, 80}, {240, 80}, {320, 80}, {400, 80}}),
              cells_of(squad_on(fx.world(), 1)))
        << "arm: no centroids -> (-1,-1) -> members teleport";
    EXPECT_EQ(10, draws.calls);
    EXPECT_EQ(0, draws.out_of_range);
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:247. A sited member takes the anchor tile when it is
// clear; the next ones take the first clear ring cell, clockwise from due
// north: (320,464), (336,480), (320,496), (304,480) round the marker.
TEST_F(SemLineupStage, a_sited_member_takes_the_anchor_then_the_ring_in_order)
{
    LineupWorld fx;
    fx.hero(0, 96, 96);
    fx.marker(1, 320, 480);
    fx.fill(1, og::sim::kFillWeak);
    fx.world().tick();
    const auto squad = squad_on(fx.world(), 1);
    ASSERT_EQ(5u, squad.size());
    EXPECT_EQ((Cell{320, 480}), cell_of(squad.front())) << "arm: anchor clear -> anchor";
    EXPECT_EQ((std::vector<Cell>{{320, 480}, {320, 464}, {336, 480}, {320, 496}, {304, 480}}),
              cells_of(squad))
        << "arm: anchor taken, ring cell clear -> first clear ring cell";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:329. Only a site-less squad avoids hostiles: a sited
// squad takes its marker tile and ring even beside a team-2 soldier at
// (336,496).
TEST_F(SemLineupStage, a_sited_squad_may_spawn_beside_a_hostile)
{
    LineupWorld fx;
    fx.hero(0, 96, 96);
    fx.marker(1, 320, 480);
    ASSERT_NE(nullptr, fx.npc(FAMILY_SOLDIER, 2, 336, 496));
    fx.fill(1, og::sim::kFillWeak);
    fx.world().tick();
    EXPECT_EQ((std::vector<Cell>{{320, 480}, {320, 464}, {336, 480}, {320, 496}, {304, 480}}),
              cells_of(squad_on(fx.world(), 1)))
        << "arm: sited -> plain anchor/ring, no hostile check";
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

namespace {

// The safe-teleport scene: no team anywhere (so every member teleports),
// one wildlife soldier at tile (10,10) = (160,160); its own tile and its
// eight neighbours are hostile-adjacent.
LineupWorld* wildlife_scene(LineupWorld& fx)
{
    fx.npc(FAMILY_SOLDIER, 4, 160, 160);
    fx.fill(1, og::sim::kFillStrong);
    return &fx;
}

// Safe landings for members 2..5.
const std::vector<Cell> kSafeTail = {{24, 20}, {28, 20}, {32, 20}, {36, 20}};
const std::vector<Cell> kSafeTailPx = {{384, 320}, {448, 320}, {512, 320}, {576, 320}};

}  // namespace

// lineup_stage.lua:273 and :278. A site-less member that has to teleport
// re-draws an unsafe landing (tile (11,11), next to the wildlife) and keeps
// the first safe one (tile (20,20)), then stops drawing: twelve draws for
// the squad, two landings for the first member and one for each other.
TEST_F(SemLineupStage, a_siteless_member_redraws_an_unsafe_landing_and_keeps_the_first_safe_one)
{
    LineupWorld fx;
    wildlife_scene(fx);
    std::vector<Cell> tiles = {{11, 11}, {20, 20}};
    tiles.insert(tiles.end(), kSafeTail.begin(), kSafeTail.end());
    ScriptedDraws draws(landings(tiles), 0);
    {
        ScopedSimRandom scope(&draws);
        ASSERT_TRUE(fx.world().run_lineup_stage_step());
    }
    std::vector<Cell> want = {{320, 320}};
    want.insert(want.end(), kSafeTailPx.begin(), kSafeTailPx.end());
    EXPECT_EQ(want, cells_of(squad_on(fx.world(), 1)))
        << "arms: site-less squad -> bounded re-probe; landing unsafe -> draw "
           "again; a landing safe -> stay there";
    EXPECT_EQ(12, draws.calls) << "arm: a landing safe -> stop drawing";
    EXPECT_EQ(0, draws.out_of_range);
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:278. When all eight landings are unsafe the last one
// stands: the first member draws the wildlife's eight neighbours and stays
// on the eighth, tile (11,11) = (176,176).
TEST_F(SemLineupStage, a_siteless_member_with_eight_unsafe_landings_stays_on_the_last)
{
    LineupWorld fx;
    wildlife_scene(fx);
    std::vector<Cell> tiles = {{9, 9},  {10, 9},  {11, 9},  {9, 10},
                               {11, 10}, {9, 11}, {10, 11}, {11, 11}};
    tiles.insert(tiles.end(), kSafeTail.begin(), kSafeTail.end());
    ScriptedDraws draws(landings(tiles), 0);
    {
        ScopedSimRandom scope(&draws);
        ASSERT_TRUE(fx.world().run_lineup_stage_step());
    }
    std::vector<Cell> want = {{176, 176}};
    want.insert(want.end(), kSafeTailPx.begin(), kSafeTailPx.end());
    EXPECT_EQ(want, cells_of(squad_on(fx.world(), 1)))
        << "arm: all eight unsafe -> member at the last landing";
    EXPECT_EQ(24, draws.calls) << "eight landings, then one each";
    EXPECT_EQ(0, draws.out_of_range);
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}

// lineup_stage.lua:278. When every teleport fails (a fully walled map: 200
// refused attempts a draw, eight draws a member) the member is not moved
// from where it was spawned, (0,0).
TEST_F(SemLineupStage, a_siteless_member_whose_teleports_all_fail_stays_where_it_spawned)
{
    LineupWorld fx;
    std::fill_n(fx.world().grid.data.get(),
                static_cast<std::size_t>(fx.world().grid.w) * fx.world().grid.h,
                static_cast<std::uint8_t>(PIX_H_WALL1));
    fx.fill(1, og::sim::kFillStrong);
    ScriptedDraws draws({}, 3);  // every landing tile (3,3): a wall
    {
        ScopedSimRandom scope(&draws);
        ASSERT_TRUE(fx.world().run_lineup_stage_step());
    }
    EXPECT_EQ((std::vector<Cell>{{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}),
              cells_of(squad_on(fx.world(), 1)))
        << "arm: all eight teleports fail -> member not moved";
    EXPECT_EQ(5 * 8 * 200 * 2, draws.calls);
    EXPECT_TRUE(fx.world().scripts().host().errors().empty());
}
