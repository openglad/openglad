/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The bot gates of the soldier, the elf, the archer and the barbarian: each
// special a bot can roll, asked whether it pays off, with the Gameplay
// Fixes setting off (the classic answer) and on. The cases use the
// GameplayFixesBots fixture (bot_gate_fixture.h), shared with
// test_gameplay_fixes_bots.cpp. Fire headings are staged with lastx /
// lasty (a fired weapon leaves along them, not along curdir); DISARM alone
// reads curdir, because its cast steps along it.

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <memory>

using og::test::bot_gates::GameplayFixesBots;

// ---------------------------------------------------------------------------
// The soldier
// ---------------------------------------------------------------------------

// CHARGE rushes along the soldier's heading (lastx / lasty), so with the
// setting on it wants the classic window (a foe one to three squares off)
// AND a heading that points at that foe: with no heading (the spawn state)
// the rush walks nowhere, and along another heading it runs past the foe.
TEST_F(GameplayFixesBots, soldier_charge_wants_the_window_and_an_aim_at_the_foe)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 1, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 240, 200, 1), 16, 16);  // 40 px east
    ASSERT_TRUE(soldier && foe);

    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer: a foe inside the window";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "no heading yet: the rush would go nowhere";

    soldier->set_lastx(1.0f);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(soldier, 1, 1)) << "aimed east at the foe";

    soldier->set_lastx(0.0f);
    soldier->set_lasty(1.0f);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "aimed down: the rush runs past the foe";

    soldier->set_lastx(1.0f);
    soldier->set_lasty(0.0f);
    foe->setxy(219, 200);  // 19 px: inside the near edge of the window
    EXPECT_FALSE(gate(soldier, 1, 0)) << "the classic answer at 19 px";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "19 px: the window still decides";
    foe->setxy(276, 200);  // 76 px: past the far edge
    EXPECT_FALSE(gate(soldier, 1, 0)) << "the classic answer at 76 px";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "76 px: the window still decides";
}

// BOOMERANG: the blade orbits out to 17 + 6 px a level (41 at level 4), so
// with the setting on it wants a living foe that close and no blade of the
// soldier's already out. A living faerie (living family 7, the boomerang's
// FX number) owned by the soldier is not a blade.
TEST_F(GameplayFixesBots, soldier_boomerang_wants_a_foe_inside_the_orbit_and_no_blade_out)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 300.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 260, 200, 1), 25, 29);  // 60 px
    ASSERT_TRUE(soldier && foe);

    EXPECT_TRUE(gate(soldier, 2, 0)) << "the classic answer: 60 px is inside the window";
    EXPECT_FALSE(gate(soldier, 2, 1)) << "60 px: outside the level-4 orbit of 41";

    foe->setxy(240, 200);  // 40 px
    EXPECT_TRUE(gate(soldier, 2, 0)) << "the classic answer";
    EXPECT_TRUE(gate(soldier, 2, 1)) << "40 px: inside the orbit";

    walker* blade = world().add_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, blade);
    blade->setxy(220, 200);
    blade->set_owner(soldier);
    EXPECT_TRUE(gate(soldier, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 2, 1)) << "a blade of the soldier's is already out";

    blade->set_dead(1);
    living* faerie = body(add(FAMILY_FAERIE, 1, 180, 200, 1), 16, 16);
    ASSERT_NE(nullptr, faerie);
    faerie->set_owner(soldier);
    EXPECT_TRUE(gate(soldier, 2, 1)) << "an owned living faerie is not a blade";
}

// WHIRLWIND strikes everything in its ring (32 + 2 px a level, 46 at level
// 7), generators included; one body in reach is what the free swing hits,
// so with the setting on it wants two.
TEST_F(GameplayFixesBots, soldier_whirlwind_wants_two_targets_inside_the_spin)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 7, 300.0f), 16, 16);
    living* first = body(add(FAMILY_TOWER1, 2, 230, 200, 1), 25, 29);  // 30 px
    ASSERT_TRUE(soldier && first);

    EXPECT_TRUE(gate(soldier, 3, 0)) << "the classic answer: one foe inside the window";
    EXPECT_FALSE(gate(soldier, 3, 1)) << "one body in the spin: the free swing hits it";

    living* second = body(add(FAMILY_TOWER1, 2, 200, 244, 1), 25, 29);  // 44 px
    ASSERT_NE(nullptr, second);
    EXPECT_TRUE(gate(soldier, 3, 0)) << "the classic answer";
    EXPECT_TRUE(gate(soldier, 3, 1)) << "two in the spin";

    second->setxy(200, 247);  // 47 px: one past the ring
    EXPECT_TRUE(gate(soldier, 3, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 3, 1)) << "the second a pixel past the ring";

    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(2);
    tent->setxy(244, 200);  // 44 px
    body(tent, 16, 16);
    EXPECT_TRUE(gate(soldier, 3, 1)) << "a tent in the ring is the second target";
}

// DISARM staggers the foe the soldier faces at close range: the cast asks
// forward_blocked, a 1 px step along curdir that collides with a body
// (both boxes shrunk 1 px a side). With the setting on the gate asks the
// same from positions: a living foe whose box meets the soldier's stepped
// box, within the 28 px stagger reach, and not above the soldier's level.
TEST_F(GameplayFixesBots, soldier_disarm_wants_a_foe_blocking_its_front)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 10, 300.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 200, 186, 1), 16, 16);  // 14 px above
    ASSERT_TRUE(soldier && foe);
    soldier->set_curdir(FACE_UP);

    EXPECT_FALSE(gate(soldier, 4, 0)) << "the classic answer: 14 px is inside the window's near edge";
    EXPECT_TRUE(gate(soldier, 4, 1)) << "the foe blocks the soldier's front";

    foe->setxy(200, 183);  // a 1 px gap: the shrunk boxes miss
    EXPECT_FALSE(gate(soldier, 4, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 4, 1)) << "a 1 px gap: forward_blocked would say no";

    foe->setxy(215, 188);  // 27 px off, 15 px aside: past the shrunk width
    EXPECT_TRUE(gate(soldier, 4, 0)) << "the classic answer: inside the window";
    EXPECT_FALSE(gate(soldier, 4, 1)) << "beside the soldier, not in front";

    foe->setxy(200, 186);
    foe->stats()->set_level(11);
    EXPECT_FALSE(gate(soldier, 4, 1)) << "a foe above the soldier's level is never staggered";

    foe->stats()->set_level(1);
    EXPECT_TRUE(gate(soldier, 4, 1)) << "back to level 1";
    soldier->set_curdir(-1);
    EXPECT_FALSE(gate(soldier, 4, 1)) << "no direction: nothing is in front";
}
