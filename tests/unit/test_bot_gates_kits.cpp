/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The bot gates of the New Specials kits (the ghost, the elemental, the
// skeleton, the orc captain and the faerie): each special a bot can roll,
// asked whether it pays off. A classic slot (the ghost's SCARE, the
// elemental's STARBURST) is asked with the Gameplay Fixes setting off (the
// classic answer) and on; a kit arm with New Specials off (its guard
// answers yes and leaves the coin) and on. The cases use the
// GameplayFixesBots fixture (bot_gate_fixture.h), shared with
// test_gameplay_fixes_bots.cpp.
//
// Bodies are placed by hand with the sizes the sprites have: a fighter 16
// by 16, a tower 25 by 29. A distance quoted is the engine's own, between
// top-left corners, unless it says "gap" (edge to edge).

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

namespace {

using og::test::bot_gates::GameplayFixesBots;

constexpr unsigned char kBots = 1;  // the bot's team
constexpr unsigned char kFoes = 2;  // its foes

// A tent (a generator) of `team` with its top-left at (x, y), 16 by 16.
walker* tent(GameWorld& world, unsigned char team, short x, short y)
{
    walker* w = world.add_ob(Order::Generator, FAMILY_TENT);
    if (w != nullptr) {
        w->set_team_num(team);
        w->setxy(x, y);
        w->set_sizex(16);
        w->set_sizey(16);
    }
    return w;
}

// A blood stain under `w`'s centre, as a corpse leaves one.
walker* stain_under(GameWorld& world, walker* w)
{
    walker* s = world.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (s == nullptr)
        return nullptr;
    s->set_team_num(kFoes);
    s->set_dead(0);
    s->set_floor(w->floor());
    s->center_on(w);
    s->stats()->set_old_family(FAMILY_SOLDIER);
    s->stats()->set_level(3);
    return s;
}

// One segment of `owner`'s bone wall with its top-left at (x, y).
walker* wall_segment(GameWorld& world, walker* owner, short x, short y)
{
    const int family = og::families::resolve_family_string_id(
        Order::Weapon, "core:bone_wall");
    walker* seg = world.add_weap_ob(Order::Weapon, family);
    if (seg == nullptr)
        return nullptr;
    seg->set_team_num(owner->team_num());
    seg->set_owner(owner);
    seg->set_dead(0);
    seg->setxy(x, y);
    return seg;
}

}  // namespace

// ---------------------------------------------------------------------------
// The ghost (rows 37, 38, 41)
// ---------------------------------------------------------------------------

// SCARE frightens only the living foes inside the scare radius, 50 + 10 px
// per level (60 at level 1); the classic gate asked for a foe within 130,
// which the fright reaches only from level 8. A tent is no target (the
// fright moves living foes only), and a resting SCARE is never rolled.
//
// RED (the classic gate with the setting on): Value of: gate(ghost, 1, 1)
// Actual: true Expected: false "100 px: inside 130, outside the scare";
// and Actual: true Expected: false "a tent at 40: nobody to frighten".
TEST_F(GameplayFixesBots, ghost_scare_wants_a_living_foe_inside_the_scare_radius)
{
    world().new_specials = 0;
    living* ghost = body(add(FAMILY_GHOST, kBots, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, kFoes, 300, 200, 1), 25, 29);  // 100 px
    ASSERT_TRUE(ghost && foe);
    EXPECT_TRUE(gate(ghost, 1, 0)) << "100 px: the classic answer (a foe within 130)";
    ghost->set_foe(nullptr);
    EXPECT_FALSE(gate(ghost, 1, 1)) << "100 px: inside 130, outside the scare";

    foe->setxy(260, 200);  // 60 px: the edge of the scare
    ghost->set_foe(nullptr);
    EXPECT_TRUE(gate(ghost, 1, 1)) << "60 px: inside the scare";

    foe->setxy(600, 600);
    walker* t = tent(world(), kFoes, 240, 200);  // 40 px
    ASSERT_NE(nullptr, t);
    ghost->set_foe(nullptr);
    EXPECT_FALSE(gate(ghost, 1, 1)) << "a tent at 40: nobody to frighten";

    t->set_dead(1);
    foe->setxy(240, 200);
    ghost->set_special_rest_slot(1);
    ghost->set_special_rest_left(50);
    ghost->set_foe(nullptr);
    EXPECT_FALSE(gate(ghost, 1, 0)) << "resting: the classic hold";
    EXPECT_FALSE(gate(ghost, 1, 1)) << "resting: SCARE is not rolled";
}

// WAIL is a chain: the puff strikes the nearest living foe inside the scare
// radius and hops to foes within 120 px of the struck one. It pays where
// one foe stands inside the radius and a second within 120 px of it; a
// crowd inside the radius is SCARE's (one fright for all of them), a lone
// foe too, and a ghost that cannot pay WAIL's 50 scares. The coin never
// decides: the gate writes the Shift.
//
// RED (the coin stands): Expected equality of these values: 1
// ghost->shifter_down() Which is: 0 "a chain: WAIL"; and 0 / 1 for "a
// crowd inside the scare: SCARE".
TEST_F(GameplayFixesBots, ghost_wail_takes_a_chain_and_scare_takes_a_crowd)
{
    world().gameplay_fixes = 1;
    living* ghost = body(add(FAMILY_GHOST, kBots, 200, 200, 4, 100.0f), 16, 16);  // radius 90
    living* first = body(add(FAMILY_SOLDIER, kFoes, 260, 200, 1), 16, 16);       // 60 px
    living* second = body(add(FAMILY_SOLDIER, kFoes, 360, 200, 1), 16, 16);      // 100 px past it
    ASSERT_TRUE(ghost && first && second);

    ghost->set_shifter_down(1);
    world().gameplay_fixes = 0;
    EXPECT_TRUE(kit_gate(ghost, 1, 0)) << "New Specials off: the classic answer";
    EXPECT_EQ(1, ghost->shifter_down()) << "New Specials off: the coin stands";
    world().gameplay_fixes = 1;

    ghost->set_shifter_down(0);
    EXPECT_TRUE(kit_gate(ghost, 1)) << "a chain";
    EXPECT_EQ(1, ghost->shifter_down()) << "a chain: WAIL";

    world().gameplay_fixes = 0;
    ghost->set_shifter_down(0);
    EXPECT_TRUE(kit_gate(ghost, 1)) << "a chain, Gameplay Fixes off";
    EXPECT_EQ(1, ghost->shifter_down()) << "WAIL's rule is New Specials', not the coin";
    world().gameplay_fixes = 1;

    second->setxy(200, 260);  // inside the radius too
    ghost->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(ghost, 1)) << "a crowd";
    EXPECT_EQ(0, ghost->shifter_down()) << "a crowd inside the scare: SCARE";

    second->setxy(600, 600);
    ghost->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(ghost, 1)) << "one foe";
    EXPECT_EQ(0, ghost->shifter_down()) << "one foe, nobody near it: SCARE";

    second->setxy(360, 200);
    ghost->stats()->set_magicpoints(40.0f);
    ghost->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(ghost, 1)) << "40 MP";
    EXPECT_EQ(0, ghost->shifter_down()) << "40 MP cannot pay WAIL's 50: SCARE";
}

// PHASE (a hurt ghost with a foe within 48) is the answer only when SCARE,
// the cheaper one, cannot be: SCARE resting, or no living foe inside the
// scare radius. At level 1 the radius is 60, so a foe within 48 is always
// inside it and only the rest opens PHASE.
//
// RED (the kit gate without the rule): Value of: kit_gate(ghost, 4)
// Actual: true Expected: false "SCARE is ready: PHASE waits".
TEST_F(GameplayFixesBots, ghost_phase_defers_to_a_scare_it_could_cast)
{
    living* ghost = body(add(FAMILY_GHOST, kBots, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, kFoes, 240, 200, 1), 16, 16);  // 40 px
    ASSERT_TRUE(ghost && foe);
    ghost->stats()->set_hitpoints(100.0f);  // a quarter: under 30 %
    EXPECT_TRUE(kit_gate(ghost, 4, 0)) << "New Specials off: the guard's yes";
    EXPECT_FALSE(kit_gate(ghost, 4)) << "SCARE is ready: PHASE waits";
    ghost->set_special_rest_slot(1);
    ghost->set_special_rest_left(50);
    EXPECT_TRUE(kit_gate(ghost, 4)) << "SCARE rests: PHASE";
    foe->setxy(260, 200);  // 60 px: outside PHASE's 48
    EXPECT_FALSE(kit_gate(ghost, 4)) << "no foe within 48";
}

// ---------------------------------------------------------------------------
// The orc captain (rows 48, 50, 51, 52)
// ---------------------------------------------------------------------------

// HOWL stuns every body inside the yell, 160 + 20 px per level (180 at
// level 1), a tent included (a stunned tent stops spawning). The classic
// 130 is not the howl's reach.
//
// RED (the 130 px gate): Value of: kit_gate(cap, 1) Actual: false
// Expected: true "a tower at 160: inside the yell".
TEST_F(GameplayFixesBots, captain_howl_wants_a_target_inside_the_yell)
{
    living* cap = body(add(FAMILY_BIG_ORC, kBots, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, kFoes, 360, 200, 1), 25, 29);  // 160 px
    ASSERT_TRUE(cap && foe);
    EXPECT_TRUE(kit_gate(cap, 1, 0)) << "New Specials off: the guard's yes";
    cap->set_foe(nullptr);
    EXPECT_TRUE(kit_gate(cap, 1)) << "a tower at 160: inside the yell";
    foe->setxy(381, 200);  // 181 px
    cap->set_foe(nullptr);
    EXPECT_FALSE(kit_gate(cap, 1)) << "181 px: outside the yell";
    foe->setxy(380, 200);  // 180 px: the edge
    cap->set_foe(nullptr);
    EXPECT_TRUE(kit_gate(cap, 1)) << "180 px: the edge is in";
    foe->setxy(800, 800);
    ASSERT_NE(nullptr, tent(world(), kFoes, 300, 200));  // 100 px
    cap->set_foe(nullptr);
    EXPECT_TRUE(kit_gate(cap, 1)) << "a tent at 100: the howl stuns it";
}

// HOOK BLADE spirals out to 60 px: it wants a living foe within 60 on the
// captain's floor, no blade out, and the 40 it costs (the engine keeps the
// slot for a pool of 30 to 39, the fan's price, and does not check again).
//
// RED (the 90 px gate): Expected equality of these values:
// (std::pair<bool,int>{false, 9}) ... Which is: (true, 0) "61 px: past
// the blade"; and (true, 0) for "35 MP: the hook is not affordable".
TEST_F(GameplayFixesBots, captain_hook_wants_a_foe_at_the_blades_reach_and_the_price)
{
    living* cap = body(add(FAMILY_BIG_ORC, kBots, 200, 200, 7, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, kFoes, 250, 200, 1), 16, 16);  // 50 px
    ASSERT_TRUE(cap && foe);
    auto ask = [&](short ns) {
        cap->set_shifter_down(9);
        const bool yes = kit_gate(cap, 3, ns);
        return std::pair<bool, int>{yes, cap->shifter_down()};
    };
    EXPECT_EQ((std::pair<bool, int>{true, 9}), ask(0)) << "New Specials off: the coin stands";
    EXPECT_EQ((std::pair<bool, int>{true, 0}), ask(1)) << "50 px: the hook";
    foe->setxy(261, 200);  // 61 px
    EXPECT_EQ((std::pair<bool, int>{false, 9}), ask(1)) << "61 px: past the blade";
    foe->setxy(260, 200);  // 60 px: the edge
    EXPECT_EQ((std::pair<bool, int>{true, 0}), ask(1)) << "60 px: the blade's reach";
    cap->stats()->set_magicpoints(35.0f);
    EXPECT_EQ((std::pair<bool, int>{false, 9}), ask(1)) << "35 MP: the hook is not affordable";
}

// KNIFE FAN throws three knives: along the captain's facing and the two
// headings beside it. The fan reads the facing (curdir) as the cast does
// (kit_captain.lua, knife_fan's facing_index), not the last step's heading.
// It wants two living foes on those three lines inside the knife's flight:
// at level 7 a 63 px gap straight, 40 on a diagonal.
//
// RED (the 90 px gate): Expected equality of these values:
// (std::pair<bool,int>{true, 1}) ... Which is: (true, 0) "two on the
// lines: the fan" (the old gate hooked the foe within 90).
TEST_F(GameplayFixesBots, captain_fan_wants_two_foes_on_its_three_lines)
{
    living* cap = body(add(FAMILY_BIG_ORC, kBots, 200, 200, 7, 100.0f), 16, 16);
    // Ahead (up) at a 40 px gap, and ahead-right at 40 px gaps on both axes;
    // both farther than the hook's 60 px.
    living* ahead = body(add(FAMILY_TOWER1, kFoes, 196, 131, 1), 25, 29);
    living* right = body(add(FAMILY_TOWER1, kFoes, 256, 131, 1), 25, 29);
    ASSERT_TRUE(cap && ahead && right);
    auto ask = [&](short ns) {
        cap->set_shifter_down(9);
        const bool yes = kit_gate(cap, 3, ns);
        return std::pair<bool, int>{yes, cap->shifter_down()};
    };
    cap->set_lastx(0.0f);
    cap->set_lasty(-1.0f);
    cap->set_curdir(FACE_UP);
    EXPECT_EQ((std::pair<bool, int>{true, 9}), ask(0)) << "New Specials off: the coin stands";
    EXPECT_EQ((std::pair<bool, int>{true, 1}), ask(1)) << "two on the lines: the fan";

    cap->set_curdir(FACE_DOWN);  // the last step still up: the fan goes down
    EXPECT_EQ((std::pair<bool, int>{false, 9}), ask(1)) << "facing away: the knives fly down";
    cap->set_curdir(FACE_UP);

    right->setxy(196, 276);  // behind, a 60 px gap below (80 px: past the hook)
    EXPECT_EQ((std::pair<bool, int>{false, 9}), ask(1)) << "one ahead, one behind";
    right->setxy(256, 131);

    cap->stats()->set_magicpoints(35.0f);  // the hook's 40 out of reach, the fan's 30 not
    EXPECT_EQ((std::pair<bool, int>{true, 1}), ask(1)) << "35 MP: the fan is affordable";
}

// WAR BANNER heals, holds and frightens for 900 ticks around the flag: it
// pays where a fight is, so beside the two allies and the corpse it wants
// a living foe within 120 px.
//
// RED (the banner without the foe): Value of: kit_gate(cap, 4) Actual:
// true Expected: false "two allies and a corpse, nobody to fight".
TEST_F(GameplayFixesBots, captain_banner_wants_a_foe_near_the_flag)
{
    living* cap = body(add(FAMILY_BIG_ORC, kBots, 200, 200, 10, 200.0f), 16, 16);
    ASSERT_NE(nullptr, cap);
    ASSERT_NE(nullptr, stain_under(world(), cap));
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, kBots, 240, 200, 1), 16, 16));
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, kBots, 200, 240, 1), 16, 16));
    cap->set_shifter_down(9);
    EXPECT_TRUE(kit_gate(cap, 4, 0)) << "New Specials off: the guard's yes";
    EXPECT_FALSE(kit_gate(cap, 4)) << "two allies and a corpse, nobody to fight";
    living* foe = body(add(FAMILY_SOLDIER, kFoes, 300, 200, 1), 16, 16);  // 100 px
    ASSERT_NE(nullptr, foe);
    cap->set_shifter_down(9);
    EXPECT_TRUE(kit_gate(cap, 4)) << "a foe at 100: plant";
    EXPECT_EQ(0, cap->shifter_down()) << "the banner, not the warband";
    foe->setxy(321, 200);  // 121 px
    EXPECT_FALSE(kit_gate(cap, 4)) << "121 px: no fight at the flag";
}

// ---------------------------------------------------------------------------
// The fire elemental (rows 56, 57, 59)
// ---------------------------------------------------------------------------

// STARBURST, the ring of eight meteors, pays where one meteor would not: two
// living foes inside the ring's flight (a level-1 meteor goes 89 px of gap
// straight, 56 on a diagonal), or one the elemental does not face. At spawn
// the fire heading is up.
//
// RED (the classic gate with the setting on): Value of: gate(e, 1, 1)
// Actual: true Expected: false "one foe ahead: the free meteor's job".
TEST_F(GameplayFixesBots, elemental_starburst_wants_a_crowd_or_a_foe_behind)
{
    world().new_specials = 0;
    living* e = body(add(FAMILY_FIREELEMENTAL, kBots, 200, 200, 1, 150.0f), 16, 16);
    living* ahead = body(add(FAMILY_TOWER1, kFoes, 196, 111, 1), 25, 29);  // a 60 px gap above
    ASSERT_TRUE(e && ahead);
    EXPECT_TRUE(gate(e, 1, 0)) << "the classic answer: a foe within 130";
    e->set_foe(nullptr);
    EXPECT_FALSE(gate(e, 1, 1)) << "one foe ahead: the free meteor's job";

    ahead->setxy(196, 276);  // a 60 px gap below: behind
    e->set_foe(nullptr);
    EXPECT_TRUE(gate(e, 1, 1)) << "one foe behind: the ring reaches it";

    ahead->setxy(196, 111);
    living* behind = body(add(FAMILY_TOWER1, kFoes, 196, 276, 1), 25, 29);
    ASSERT_NE(nullptr, behind);
    e->set_foe(nullptr);
    EXPECT_TRUE(gate(e, 1, 1)) << "two in the ring";

    behind->setxy(196, 306);  // a 90 px gap: past the meteor's 89
    e->set_foe(nullptr);
    EXPECT_FALSE(gate(e, 1, 1)) << "the second past the ring's flight";
}

// IMMOLATE burns what touches the elemental: it is lit with a living foe at
// blows (a gap of at most 5 px) and more than 60 magic, and while it burns
// the bot quenches it (a second cast) once no living foe is within 40 px,
// so the drain stops paying for nothing.
//
// RED (the 40 px gate): Value of: kit_gate(e, 2) Actual: true Expected:
// false "a 10 px gap: not at blows"; and Actual: false Expected: true
// "burning alone: quench".
TEST_F(GameplayFixesBots, elemental_immolate_lights_at_blows_and_quenches_alone)
{
    living* e = body(add(FAMILY_FIREELEMENTAL, kBots, 200, 200, 4, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, kFoes, 217, 200, 1), 16, 16);  // a 1 px gap
    ASSERT_TRUE(e && foe);
    EXPECT_TRUE(kit_gate(e, 2, 0)) << "New Specials off: the guard's yes";
    EXPECT_TRUE(kit_gate(e, 2)) << "a foe at blows: light it";
    foe->setxy(226, 200);  // a 10 px gap
    e->set_foe(nullptr);
    EXPECT_FALSE(kit_gate(e, 2)) << "a 10 px gap: not at blows";
    e->stats()->set_magicpoints(60.0f);
    foe->setxy(217, 200);
    EXPECT_FALSE(kit_gate(e, 2)) << "60 MP: not enough to burn";

    e->set_kit_state(KIT_CHANNEL);
    foe->setxy(600, 600);
    EXPECT_TRUE(kit_gate(e, 2)) << "burning alone: quench";
    foe->setxy(220, 200);  // 20 px
    EXPECT_FALSE(kit_gate(e, 2)) << "burning with a foe at 20: keep burning";
}

// REKINDLE (40) is chosen only when the elemental can pay it: the engine
// kept the slot for SUPERNOVA's 10 on a pool of 10 to 39 and does not check
// again, so a gate that chose REKINDLE there would cast into a refusal.
//
// RED (the kit gate without the price): Value of: kit_gate(e, 4) Actual:
// true Expected: false "25 MP cannot pay REKINDLE".
TEST_F(GameplayFixesBots, elemental_rekindle_is_not_forced_on_a_pool_that_cannot_pay_it)
{
    living* e = body(add(FAMILY_FIREELEMENTAL, kBots, 200, 200, 10, 25.0f), 16, 16);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(150.0f);  // under half of 400
    e->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(e, 4, 0)) << "New Specials off: the guard's yes";
    EXPECT_FALSE(kit_gate(e, 4)) << "25 MP cannot pay REKINDLE";
    e->stats()->set_magicpoints(45.0f);
    e->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(e, 4)) << "45 MP: REKINDLE";
    EXPECT_EQ(0, e->shifter_down()) << "REKINDLE, not SUPERNOVA";
}

// ---------------------------------------------------------------------------
// The skeleton (rows 62, 63, 64, 65, 66)
// ---------------------------------------------------------------------------

// DIG IN counts living foes: a tent cannot step on the burrow or press the
// skeleton.
//
// RED (the count with generators): Value of: kit_gate(skel, 2) Actual:
// true Expected: false "a foe and a tent: one foe".
TEST_F(GameplayFixesBots, skeleton_dig_in_counts_living_foes_only)
{
    living* skel = body(add(FAMILY_SKELETON, kBots, 200, 200, 4, 100.0f), 16, 16);
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, kFoes, 250, 200, 1), 16, 16));
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, tent(world(), kFoes, 200, 250));
    skel->stats()->set_hitpoints(200.0f);  // under 60 %
    EXPECT_TRUE(kit_gate(skel, 2, 0)) << "New Specials off: the guard's yes";
    EXPECT_FALSE(kit_gate(skel, 2)) << "a foe and a tent: one foe";
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, kFoes, 150, 200, 1), 16, 16));
    EXPECT_TRUE(kit_gate(skel, 2)) << "two living foes: dig in";
}

// BONE WALL rises three segments wide across the skeleton's facing (the
// cast reads curdir, kit_skeleton.lua facing_of): it pays when the nearest
// living foe within 60 is on one of the three lines the wall covers.
//
// RED (the count alone): Value of: kit_gate(skel, 3) Actual: true
// Expected: false "a foe at its side: the wall would rise beside it".
TEST_F(GameplayFixesBots, skeleton_wall_rises_across_the_foes_line)
{
    living* skel = body(add(FAMILY_SKELETON, kBots, 200, 200, 4, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, kFoes, 240, 200, 1), 16, 16);  // 40 px east
    ASSERT_TRUE(skel && foe);
    skel->stats()->set_hitpoints(200.0f);  // under 70 %
    skel->set_curdir(FACE_RIGHT);
    skel->set_lastx(0.0f);
    skel->set_lasty(0.0f);  // the fire heading is up; the wall follows curdir
    skel->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(skel, 3, 0)) << "New Specials off: the guard's yes";
    EXPECT_EQ(1, skel->shifter_down()) << "New Specials off: the coin stands";
    EXPECT_TRUE(kit_gate(skel, 3)) << "a foe on the wall's line";
    EXPECT_EQ(0, skel->shifter_down()) << "the wall, not the storm";
    foe->setxy(200, 160);  // 40 px north
    EXPECT_FALSE(kit_gate(skel, 3)) << "a foe at its side: the wall would rise beside it";
    foe->setxy(230, 175);  // 55 px up-right: the next line
    EXPECT_TRUE(kit_gate(skel, 3)) << "the wall's end covers the diagonal";
}

// BONE STORM bursts every standing wall into bones that fly 30 px: it
// counts the foes at the walls, two within 60 px of a standing segment of
// this skeleton's, not the foes at the skeleton.
//
// RED (the count at the skeleton): Value of: kit_gate(skel, 3) Actual:
// false Expected: true "two foes at the wall".
TEST_F(GameplayFixesBots, skeleton_storm_counts_the_foes_at_the_wall)
{
    living* skel = body(add(FAMILY_SKELETON, kBots, 200, 200, 4, 100.0f), 16, 16);
    ASSERT_NE(nullptr, skel);
    ASSERT_NE(nullptr, wall_segment(world(), skel, 280, 200));  // 80 px east
    living* a = body(add(FAMILY_SOLDIER, kFoes, 320, 200, 1), 16, 16);  // 40 px past it
    living* b = body(add(FAMILY_SOLDIER, kFoes, 280, 240, 1), 16, 16);  // 40 px below it
    ASSERT_TRUE(a && b);
    skel->set_shifter_down(0);
    EXPECT_TRUE(kit_gate(skel, 3)) << "two foes at the wall";
    EXPECT_EQ(1, skel->shifter_down()) << "the storm";
    a->setxy(200, 240);  // both 40 px from the skeleton, 120 from the wall
    b->setxy(160, 200);
    EXPECT_FALSE(kit_gate(skel, 3)) << "two foes at the skeleton, none at the wall";
    skel->stats()->set_magicpoints(44.0f);
    a->setxy(320, 200);
    b->setxy(280, 240);
    EXPECT_FALSE(kit_gate(skel, 3)) << "44 MP cannot pay the storm";
}

// REASSEMBLE wards the next death: it pays with a foe near, one within 60,
// not on a skeleton hurt where nobody is fighting it.
//
// RED (no foe asked): Value of: kit_gate(skel, 4) Actual: true Expected:
// false "hurt, nobody near".
TEST_F(GameplayFixesBots, skeleton_reassemble_wants_a_foe_near)
{
    living* skel = body(add(FAMILY_SKELETON, kBots, 200, 200, 10, 100.0f), 16, 16);
    ASSERT_NE(nullptr, skel);
    skel->stats()->set_hitpoints(150.0f);  // under half
    EXPECT_TRUE(kit_gate(skel, 4, 0)) << "New Specials off: the guard's yes";
    EXPECT_FALSE(kit_gate(skel, 4)) << "hurt, nobody near";
    living* foe = body(add(FAMILY_SOLDIER, kFoes, 240, 200, 1), 16, 16);  // 40 px
    ASSERT_NE(nullptr, foe);
    skel->set_shifter_down(1);
    EXPECT_TRUE(kit_gate(skel, 4)) << "hurt, a foe at 40: ward";
    EXPECT_EQ(0, skel->shifter_down()) << "REASSEMBLE";
    foe->setxy(261, 200);  // 61 px
    EXPECT_FALSE(kit_gate(skel, 4)) << "61 px: nobody near";
}

// LEGION raises the foes this skeleton kills, except the undead: it counts
// the three foes within 100 among the living that can rise, never a tent or
// another skeleton.
//
// RED (the count of every foe): Value of: kit_gate(skel, 4) Actual: true
// Expected: false "two that can rise, a skeleton and a tent".
TEST_F(GameplayFixesBots, skeleton_legion_counts_only_foes_that_can_rise)
{
    living* skel = body(add(FAMILY_SKELETON, kBots, 200, 200, 10, 100.0f), 16, 16);
    living* a = body(add(FAMILY_SOLDIER, kFoes, 250, 200, 1), 16, 16);
    living* b = body(add(FAMILY_SOLDIER, kFoes, 200, 250, 1), 16, 16);
    living* undead = body(add(FAMILY_SKELETON, kFoes, 150, 200, 1), 16, 16);
    ASSERT_TRUE(skel && a && b && undead);
    ASSERT_NE(nullptr, tent(world(), kFoes, 200, 150));
    skel->set_shifter_down(0);
    EXPECT_TRUE(kit_gate(skel, 4, 0)) << "New Specials off: the guard's yes";
    EXPECT_FALSE(kit_gate(skel, 4)) << "two that can rise, a skeleton and a tent";
    ASSERT_NE(nullptr, body(add(FAMILY_ORC, kFoes, 150, 150, 1), 16, 16));
    EXPECT_TRUE(kit_gate(skel, 4)) << "three that can rise";
    EXPECT_EQ(1, skel->shifter_down()) << "LEGION";
}
