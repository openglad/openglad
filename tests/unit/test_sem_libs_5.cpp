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
// (the lineup rules follow)
