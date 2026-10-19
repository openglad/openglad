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
