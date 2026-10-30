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

#include <cstddef>
#include <cstdint>
#include <memory>

using og::test::bot_gates::GameplayFixesBots;

namespace {

// A wall cell at grid (tx, ty): what the quiet probes of CHARGE's front and
// path and of the elf's rock pad meet on a level.
void wall(GameWorld& w, int tx, int ty)
{
    w.grid.data[static_cast<std::size_t>(tx + w.grid.w * ty)] =
        static_cast<std::uint8_t>(PIX_H_WALL1);
}

// The obmap files a body under every cell its box covers when it moves, so
// a body whose size the test set after it was placed is moved once more:
// the quiet probes read the obmap, not positions.
template <class W>
W* settle(W* w)
{
    if (w != nullptr) {
        const short x = w->xpos();
        const short y = w->ypos();
        w->setxy(static_cast<short>(x + 1), y);
        w->setxy(x, y);
    }
    return w;
}

}  // namespace

// ---------------------------------------------------------------------------
// The soldier
// ---------------------------------------------------------------------------

// CHARGE rushes along the soldier's heading: three acts of three steps of
// stepsize px along trunc(lastx / stepsize), trunc(lasty / stepsize), and
// a body it runs into is struck and shoved. With the setting on it wants
// the classic window (a foe one to three squares off, which acquires the
// foe and draws) AND a living foe in that path: the rush lands. The
// soldier's 16 px box at x 200 with stepsize 4 heading east meets a 16 px
// foe within the rush for foe x <= 250 (nine steps of 4 px).
//
// RED (the rush read as "aimed at the window's foe"): the foe at 270, aimed
// at but past the rush, casts; and lastx 1, a heading the rush truncates to
// nothing, casts.
TEST_F(GameplayFixesBots, soldier_charge_wants_a_foe_in_the_rush)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 1, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 240, 200, 1), 16, 16);  // 40 px east
    ASSERT_TRUE(soldier && foe);
    soldier->set_stepsize(4.0f);

    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer: a foe inside the window";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "no heading yet: the rush would go nowhere";

    soldier->set_lastx(4.0f);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(soldier, 1, 1)) << "east: the rush runs into the foe";

    soldier->set_lastx(1.0f);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "lastx under a step: the rush moves nowhere";

    soldier->set_lastx(0.0f);
    soldier->set_lasty(4.0f);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "down: the rush runs past the foe";

    soldier->set_lastx(4.0f);
    soldier->set_lasty(0.0f);
    foe->setxy(250, 200);  // the last x the ninth step meets
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(soldier, 1, 1)) << "met on the rush's ninth step";
    foe->setxy(270, 200);  // aimed at, inside the window, past the rush
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer at 70 px";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "the rush stops 20 px short of the foe";

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

// CHARGE refuses PATH BLOCKED when the one-pixel step along curdir is
// blocked, and the gate asks the same question, quietly, before the rush
// (Gameplay Fixes U and V). The soldier's 16 px box at (192, 192) covers
// grid cell (12, 12) alone; the step east reaches cell 13. A foe 40 px east
// is inside the classic window (20 to 75) and met by the rush's seventh
// step, so only the wall differs.
//
// RED (the gate read from positions alone): the rush finds the foe through
// the wall and the gate says yes with the setting on.
TEST_F(GameplayFixesBots, soldier_charge_refuses_a_wall_in_front)
{
    living* soldier = settle(body(add(FAMILY_SOLDIER, 1, 192, 192, 1, 200.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_SOLDIER, 2, 232, 192, 1), 16, 16));
    ASSERT_TRUE(soldier && foe);
    soldier->set_stepsize(4.0f);
    soldier->set_lastx(4.0f);
    soldier->set_curdir(FACE_RIGHT);
    soldier->set_enddir(FACE_RIGHT);

    EXPECT_TRUE(gate(soldier, 1, 1)) << "open grass: the rush runs into the foe";
    wall(world(), 13, 12);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer: a foe inside the window";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "a wall one pixel ahead: the cast says PATH BLOCKED";
}

// A foe already pressed against the soldier's front is a blow, not a
// charge: the cast's step meets it and refuses. One pixel farther the step
// is clear and the rush's first step strikes it. The foe is a 25 x 29
// tower 13 px up, so the corner distance (28 and 30 px) is inside the
// classic window on both.
//
// RED (the gate read from positions alone): at gap -1 the rush's first
// step meets the tower and the gate says yes.
TEST_F(GameplayFixesBots, soldier_charge_refuses_a_touching_foe)
{
    living* soldier = settle(body(add(FAMILY_SOLDIER, 1, 192, 192, 1, 200.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_TOWER1, 2, 207, 179, 1), 25, 29));  // gap -1
    ASSERT_TRUE(soldier && foe);
    soldier->set_stepsize(4.0f);
    soldier->set_lastx(4.0f);
    soldier->set_curdir(FACE_RIGHT);
    soldier->set_enddir(FACE_RIGHT);

    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer: 28 px is inside the window";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "gap -1: the step along curdir meets the tower";

    foe->setxy(209, 179);  // gap 1
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(soldier, 1, 1)) << "gap 1: the front is clear and the first step strikes";
}

// An ally pressed against the front blocks the cast's step as a foe does
// (any body blocks a living's step), however clear the way to the foe
// beyond it.
//
// RED (allies not modelled): the rush finds the foe past the ally and the
// gate says yes.
TEST_F(GameplayFixesBots, soldier_charge_refuses_an_ally_in_front)
{
    living* soldier = settle(body(add(FAMILY_SOLDIER, 1, 192, 192, 1, 200.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_SOLDIER, 2, 240, 192, 1), 16, 16));  // 48 px
    ASSERT_TRUE(soldier && foe);
    soldier->set_stepsize(4.0f);
    soldier->set_lastx(4.0f);
    soldier->set_curdir(FACE_RIGHT);
    soldier->set_enddir(FACE_RIGHT);

    EXPECT_TRUE(gate(soldier, 1, 1)) << "nobody in the way: the ninth step strikes the foe";
    living* ally = settle(body(add(FAMILY_SOLDIER, 1, 207, 192, 1), 16, 16));  // gap -1
    ASSERT_NE(nullptr, ally);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "an ally against the front: PATH BLOCKED";
}

// The rush walks its steps and strikes what it bumps: a wall between the
// soldier and its foe stops it first. The front is clear (cell 13 is
// grass); the wall in cell 14 is met by the fifth step, the foe at 240 by
// the ninth.
//
// RED (the gate read from positions alone): the foe behind the wall is
// found and the gate says yes.
TEST_F(GameplayFixesBots, soldier_charge_refuses_a_wall_between_the_soldier_and_its_foe)
{
    living* soldier = settle(body(add(FAMILY_SOLDIER, 1, 192, 192, 1, 200.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_SOLDIER, 2, 240, 192, 1), 16, 16));  // 48 px
    ASSERT_TRUE(soldier && foe);
    soldier->set_stepsize(4.0f);
    soldier->set_lastx(4.0f);
    soldier->set_curdir(FACE_RIGHT);
    soldier->set_enddir(FACE_RIGHT);

    EXPECT_TRUE(gate(soldier, 1, 1)) << "without the wall the ninth step strikes the foe";
    wall(world(), 14, 12);
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "the fifth step meets the wall";
}

// A closed door on the rush's path stops it as a wall does: a living's
// step never passes a door it holds no key to. The door (16 px, cell 14) is
// met by the fifth step, the foe just past it by the ninth.
//
// RED (the gate read from positions alone): the foe past the door is
// found and the gate says yes.
TEST_F(GameplayFixesBots, soldier_charge_refuses_a_door_in_the_rush_path)
{
    living* soldier = settle(body(add(FAMILY_SOLDIER, 1, 192, 192, 1, 200.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_SOLDIER, 2, 240, 192, 1), 16, 16));  // 48 px
    ASSERT_TRUE(soldier && foe);
    soldier->set_stepsize(4.0f);
    soldier->set_lastx(4.0f);
    soldier->set_curdir(FACE_RIGHT);
    soldier->set_enddir(FACE_RIGHT);

    EXPECT_TRUE(gate(soldier, 1, 1)) << "no door: the ninth step strikes the foe";
    walker* door = world().add_weap_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_NE(nullptr, door);
    door->set_team_num(2);
    door->stats()->set_level(1);
    door->setxy(224, 192);
    settle(body(door, 16, 16));
    EXPECT_TRUE(gate(soldier, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(soldier, 1, 1)) << "the fifth step meets the door";
}

// WHIRLWIND and DISARM refuse SPECIAL BUSY while the fire delay runs
// (gf.special_busy, the casts' first guard), so with the setting on their
// gates ask it too (Gameplay Fixes W). The classic gate never did.
//
// RED (the gate without the busy guard): busy 3 with two targets in the
// spin, the gate says yes with the setting on.
TEST_F(GameplayFixesBots, soldier_whirlwind_waits_out_its_busy)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 7, 300.0f), 16, 16);
    living* first = body(add(FAMILY_TOWER1, 2, 230, 200, 1), 25, 29);   // 30 px
    living* second = body(add(FAMILY_TOWER1, 2, 200, 244, 1), 25, 29);  // 44 px
    ASSERT_TRUE(soldier && first && second);

    EXPECT_TRUE(gate(soldier, 3, 1)) << "two in the spin, not busy";
    soldier->set_busy(3.0f);
    EXPECT_TRUE(gate(soldier, 3, 0)) << "the classic answer: a foe inside the window";
    EXPECT_FALSE(gate(soldier, 3, 1)) << "busy 3: the cast would say SPECIAL BUSY";
    soldier->set_busy(-0.5f);
    EXPECT_TRUE(gate(soldier, 3, 1)) << "busy below zero is not busy with the setting on";
}

// RED (the gate without the busy guard): busy 3 with a tower blocking the
// front, the gate says yes with the setting on.
TEST_F(GameplayFixesBots, soldier_disarm_waits_out_its_busy)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 10, 300.0f), 16, 16);
    // A 25 x 29 tower whose bottom edge meets the soldier's top: it blocks
    // the step up, and its corner is 28 px off, inside the classic window.
    living* tower = body(add(FAMILY_TOWER1, 2, 200, 172, 1), 25, 29);
    ASSERT_TRUE(soldier && tower);
    soldier->set_curdir(FACE_UP);

    EXPECT_TRUE(gate(soldier, 4, 1)) << "the tower blocks the front, not busy";
    soldier->set_busy(3.0f);
    EXPECT_TRUE(gate(soldier, 4, 0)) << "the classic answer: 28 px is inside the window";
    EXPECT_FALSE(gate(soldier, 4, 1)) << "busy 3: the cast would say SPECIAL BUSY";
}

// ---------------------------------------------------------------------------
// The elf
// ---------------------------------------------------------------------------

// ROCKS: the classic elf had no gate. With the setting on the elf's nearest
// living foe must be faced (the rocks leave along lastx / lasty) and inside
// the rocks' flight: step 5, range 8; at level 1 a 56 px gap straight and
// 35 px on a diagonal.
TEST_F(GameplayFixesBots, elf_rocks_want_the_nearest_foe_faced_inside_the_flight)
{
    living* elf = body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 272, 200, 1), 25, 29);  // a 56 px gap east
    ASSERT_TRUE(elf && foe);
    elf->set_lastx(1.0f);
    elf->set_lasty(0.0f);

    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer: always";
    EXPECT_TRUE(gate(elf, 1, 1)) << "a 56 px gap: the rocks reach it";

    foe->setxy(273, 200);
    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 1, 1)) << "a 57 px gap: the rocks fall short";

    foe->setxy(272, 200);
    elf->set_lastx(0.0f);
    elf->set_lasty(-1.0f);
    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 1, 1)) << "aimed up: the rocks fly away from the foe";

    elf->set_lastx(1.0f);
    elf->set_lasty(1.0f);
    foe->setxy(251, 251);  // a 35 px gap on both axes
    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(elf, 1, 1)) << "on the diagonal at 35 px";
    foe->setxy(252, 252);
    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 1, 1)) << "on the diagonal at 36 px";
}

// BOUNCING ROCKS fly half again as far (the fired rock's range * 3 / 2): 84
// px straight and 55 on a diagonal at level 1.
TEST_F(GameplayFixesBots, elf_bouncing_rocks_reach_half_again)
{
    living* elf = body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 300, 200, 1), 25, 29);  // an 84 px gap east
    ASSERT_TRUE(elf && foe);
    elf->set_lastx(1.0f);

    EXPECT_TRUE(gate(elf, 2, 0)) << "the classic answer";
    EXPECT_TRUE(gate(elf, 2, 1)) << "an 84 px gap";
    EXPECT_FALSE(gate(elf, 1, 1)) << "out of the plain rocks' flight";
    foe->setxy(301, 200);
    EXPECT_TRUE(gate(elf, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 2, 1)) << "an 85 px gap";

    elf->set_lasty(1.0f);
    foe->setxy(271, 271);  // 55 px on the diagonal
    EXPECT_TRUE(gate(elf, 2, 1)) << "a 55 px gap on the diagonal";
    foe->setxy(272, 272);
    EXPECT_TRUE(gate(elf, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 2, 1)) << "a 56 px gap on the diagonal";
}

// LOTS OF ROCKS (range * 2: 120 px straight at level 1) wants the nearest
// foe faced in flight and a second living foe in flight anywhere around.
TEST_F(GameplayFixesBots, elf_lots_of_rocks_wants_two_foes_in_flight)
{
    living* elf = body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16);
    living* near_foe = body(add(FAMILY_TOWER1, 2, 246, 200, 1), 25, 29);  // a 30 px gap east
    ASSERT_TRUE(elf && near_foe);
    elf->set_lastx(1.0f);

    EXPECT_TRUE(gate(elf, 3, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 3, 1)) << "one foe in flight";

    living* behind = body(add(FAMILY_TOWER1, 2, 55, 200, 1), 25, 29);  // a 120 px gap west
    ASSERT_NE(nullptr, behind);
    EXPECT_TRUE(gate(elf, 3, 0)) << "the classic answer";
    EXPECT_TRUE(gate(elf, 3, 1)) << "a second foe in flight behind the elf";
    behind->setxy(54, 200);
    EXPECT_FALSE(gate(elf, 3, 1)) << "the second a pixel out of flight";
}

// MEGA ROCKS (range * 5 / 2: 148 px straight at level 1): the same rule.
TEST_F(GameplayFixesBots, elf_mega_rocks_wants_two_foes_in_flight)
{
    living* elf = body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16);
    living* near_foe = body(add(FAMILY_TOWER1, 2, 246, 200, 1), 25, 29);  // a 30 px gap east
    ASSERT_TRUE(elf && near_foe);
    elf->set_lastx(1.0f);

    EXPECT_TRUE(gate(elf, 4, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 4, 1)) << "one foe in flight";

    living* behind = body(add(FAMILY_TOWER1, 2, 27, 200, 1), 25, 29);  // a 148 px gap west
    ASSERT_NE(nullptr, behind);
    EXPECT_TRUE(gate(elf, 4, 0)) << "the classic answer";
    EXPECT_TRUE(gate(elf, 4, 1)) << "a second foe in flight behind the elf";
    EXPECT_FALSE(gate(elf, 3, 1)) << "out of LOTS OF ROCKS' shorter flight";
    behind->setxy(26, 200);
    EXPECT_FALSE(gate(elf, 4, 1)) << "the second a pixel out of flight";
}

// The rocks leave from a pad one pixel outside the elf on its fire heading
// (fire(): weapon_pad), and a rock whose pad is blocked never flies: the
// cast refuses COULD NOT FIRE. With the setting on the gate probes that pad
// quietly first (Gameplay Fixes V). The elf's 16 px box at (192, 192)
// covers cell (12, 12); its pad east, at x 209, lies in cell 13.
//
// RED (the gate without the pad): a wall under the pad, the foe faced and
// in flight beyond it, and the gate says yes with the setting on.
TEST_F(GameplayFixesBots, elf_rocks_refuse_a_pad_on_a_wall)
{
    living* elf = settle(body(add(FAMILY_ELF, 1, 192, 192, 1, 100.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_TOWER1, 2, 230, 186, 1), 25, 29));  // a 22 px gap east
    ASSERT_TRUE(elf && foe);
    elf->set_lastx(1.0f);
    elf->set_lasty(0.0f);

    EXPECT_TRUE(gate(elf, 1, 1)) << "open grass: faced, in flight, the pad clear";
    wall(world(), 13, 12);
    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer: always";
    EXPECT_FALSE(gate(elf, 1, 1)) << "a wall under the pad: COULD NOT FIRE";
}

// A foe's body on the pad: fire() lands a melee blow there instead of a
// rock and the cast still reports COULD NOT FIRE. The tower stands
// touching above the elf, on the spawn heading (lastx = lasty = 0 fires
// UP), so it is faced and in flight.
//
// RED (the gate without the pad): the gate says yes with the setting on.
TEST_F(GameplayFixesBots, elf_rocks_refuse_a_foe_on_the_pad)
{
    living* elf = settle(body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_TOWER1, 2, 196, 171, 1), 25, 29));  // touching above
    ASSERT_TRUE(elf && foe);

    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer: always";
    EXPECT_FALSE(gate(elf, 1, 1)) << "the tower stands on the pad";

    foe->setxy(196, 140);  // a 31 px gap above: off the pad, still in flight
    EXPECT_TRUE(gate(elf, 1, 1)) << "the pad clear, the tower in flight";
}

// A weapon passes its own team's bodies, so an ally on the pad is not in
// the way: the rock flies over it at the foe beyond. The same body on the
// other team is a foe on the pad.
//
// RED (the gate without the pad): with the touching tower on the foes'
// team the gate says yes with the setting on.
TEST_F(GameplayFixesBots, elf_rocks_an_ally_on_the_pad_is_not_in_the_way)
{
    living* elf = settle(body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16));
    living* ally = settle(body(add(FAMILY_TOWER1, 1, 196, 171, 1), 25, 29));  // touching above
    living* foe = settle(body(add(FAMILY_TOWER1, 2, 196, 130, 1), 25, 29));  // a 41 px gap above
    ASSERT_TRUE(elf && ally && foe);

    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer: always";
    EXPECT_TRUE(gate(elf, 1, 1)) << "the ally on the pad: the rock passes it";

    ally->set_team_num(2);
    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(elf, 1, 1)) << "the same body on the foes' team blocks the pad";
}

// A pad off the map: the rock cannot be placed and fire() makes nothing.
// The elf stands six pixels under the map's top edge throwing up-left, so
// its pad (6 x 6, one pixel outside the corner) starts at y -1; one pixel
// lower it starts at 0 and is on the map. The foe is an 8 x 8 body up-left,
// faced, in flight, and off the pad on both.
//
// RED (the gate without the pad): at y 6 the gate says yes with the setting
// on.
TEST_F(GameplayFixesBots, elf_rocks_refuse_a_pad_off_the_map)
{
    living* elf = settle(body(add(FAMILY_ELF, 1, 300, 6, 1, 100.0f), 16, 16));
    living* foe = settle(body(add(FAMILY_TOWER1, 2, 284, 0, 1), 8, 8));
    ASSERT_TRUE(elf && foe);
    elf->set_lastx(-1.0f);
    elf->set_lasty(-1.0f);

    EXPECT_TRUE(gate(elf, 1, 0)) << "the classic answer: always";
    EXPECT_FALSE(gate(elf, 1, 1)) << "the pad starts at y -1, off the map";

    elf->setxy(300, 7);
    foe->setxy(284, 1);
    EXPECT_TRUE(gate(elf, 1, 1)) << "one pixel lower the pad is on the map";
}

// ---------------------------------------------------------------------------
// The archer
// ---------------------------------------------------------------------------

// FIRE ARROWS throws a ring of eight arrows (step 8, range 12: 147 px
// straight, 88 on a diagonal at level 1). One foe the archer faces is what
// one ordinary arrow hits; the ring pays for two foes in its flight, or one
// the archer does not face.
TEST_F(GameplayFixesBots, archer_fire_arrows_want_a_crowd_or_a_foe_behind)
{
    living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 1, 100.0f), 16, 16);
    living* ahead = body(add(FAMILY_TOWER1, 2, 195, 111, 1), 25, 29);  // a 60 px gap above
    ASSERT_TRUE(archer && ahead);
    // The spawn heading: lastx = lasty = 0 fires UP.

    EXPECT_TRUE(gate(archer, 1, 0)) << "the classic answer: a foe within 130";
    EXPECT_FALSE(gate(archer, 1, 1)) << "one foe, faced: an ordinary arrow does it";

    ahead->setxy(195, 276);  // the same gap below: behind the archer
    EXPECT_TRUE(gate(archer, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(archer, 1, 1)) << "one foe behind the archer";

    ahead->setxy(195, 111);
    living* second = body(add(FAMILY_TOWER1, 2, 75, 71, 1), 25, 29);  // a 100 px gap up-left
    ASSERT_NE(nullptr, second);
    EXPECT_TRUE(gate(archer, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(archer, 1, 1)) << "the second foe is past the diagonal flight of 88";

    second->setxy(115, 200);  // a 60 px gap west
    EXPECT_TRUE(gate(archer, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(archer, 1, 1)) << "two foes in the ring's flight";
}

// BARRAGE sends three arrows along one line: with the setting on the
// archer's foe must be a living body it faces, inside the arrow's flight
// (158 px straight at level 4).
TEST_F(GameplayFixesBots, archer_barrage_wants_its_foe_faced_in_flight)
{
    living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 4, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 200, 84, 1), 16, 16);  // a 100 px gap above
    ASSERT_TRUE(archer && foe);
    archer->set_lasty(-1.0f);

    EXPECT_TRUE(gate(archer, 2, 0)) << "the classic answer: a foe within 130";
    EXPECT_TRUE(gate(archer, 2, 1)) << "faced, in flight";

    archer->set_lasty(1.0f);
    EXPECT_TRUE(gate(archer, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(archer, 2, 1)) << "aimed away from the foe";

    archer->set_lasty(-1.0f);
    foe->set_dead(1);
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(2);
    tent->setxy(200, 84);
    body(tent, 16, 16);
    archer->set_foe(tent);
    EXPECT_TRUE(gate(archer, 2, 0)) << "the classic answer: the tent is within 130";
    EXPECT_FALSE(gate(archer, 2, 1)) << "a generator foe: the barrage is for a fighter";
}

// EXPLODING BOLT goes off on its foe (a blast of 15 + 4 px a level, 43 at
// level 7; allies take half, the archer a quarter). With the setting on the
// foe must be faced and in flight (169 px straight at level 7), and neither
// the archer nor an ally of the archer may stand in the blast.
TEST_F(GameplayFixesBots, archer_exploding_bolt_wants_a_foe_past_its_own_blast_with_no_ally_in_it)
{
    living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 7, 300.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 200, 91, 1), 25, 29);  // an 80 px gap above
    ASSERT_TRUE(archer && foe);

    EXPECT_TRUE(gate(archer, 3, 0)) << "the classic answer: a foe within 130";
    EXPECT_TRUE(gate(archer, 3, 1)) << "faced, in flight, nobody of ours in the blast";

    foe->setxy(200, 170);  // 30 px from the archer: it stands in its own blast
    EXPECT_TRUE(gate(archer, 3, 0)) << "the classic answer";
    EXPECT_FALSE(gate(archer, 3, 1)) << "the archer would take its own blast";

    foe->setxy(200, 91);
    living* ally = body(add(FAMILY_SOLDIER, 1, 220, 91, 1), 16, 16);  // 20 px from the foe
    ASSERT_NE(nullptr, ally);
    EXPECT_TRUE(gate(archer, 3, 0)) << "the classic answer";
    EXPECT_FALSE(gate(archer, 3, 1)) << "an ally of the archer stands in the blast";

    ally->set_dead(1);
    living* foes_friend = body(add(FAMILY_SOLDIER, 2, 220, 91, 1), 16, 16);
    ASSERT_NE(nullptr, foes_friend);
    EXPECT_TRUE(gate(archer, 3, 1)) << "the foe's own friend in the blast is no reason to hold";
    foes_friend->set_dead(1);

    foe->setxy(200, 11);  // a 160 px gap above (189 px corner to corner)
    EXPECT_FALSE(gate(archer, 3, 0)) << "the classic answer: past 130";
    EXPECT_TRUE(gate(archer, 3, 1)) << "a 160 px gap is inside the straight flight of 169";
    foe->setxy(200, 1);
    EXPECT_FALSE(gate(archer, 3, 1)) << "a 170 px gap is past it";
}

// ---------------------------------------------------------------------------
// The barbarian
// ---------------------------------------------------------------------------

// HURL BOULDER: the classic barbarian had no gate. The boulder is made, not
// fired: it rolls 9 steps of its own speed (2 px a level for a bot, 1 +
// strength / 7 for a hero, at most 15), and the first only turns it, so it
// stops 8 steps out. With the setting on the barbarian's nearest living foe
// must be faced and inside that.
TEST_F(GameplayFixesBots, barbarian_boulder_wants_a_faced_foe_inside_its_short_flight)
{
    living* barbarian = body(add(FAMILY_BARBARIAN, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 195, 155, 1), 25, 29);  // a 16 px gap above
    ASSERT_TRUE(barbarian && foe);

    EXPECT_TRUE(gate(barbarian, 1, 0)) << "the classic answer: always";
    EXPECT_TRUE(gate(barbarian, 1, 1)) << "a 16 px gap: the level-1 boulder reaches it";
    foe->setxy(195, 154);
    EXPECT_TRUE(gate(barbarian, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barbarian, 1, 1)) << "a 17 px gap: short";

    barbarian->stats()->set_level(5);  // 10 px a step: 80 px
    foe->setxy(195, 101);  // a 70 px gap above
    EXPECT_TRUE(gate(barbarian, 1, 1)) << "level 5: a 70 px gap";
    foe->setxy(195, 286);  // the same gap below
    EXPECT_TRUE(gate(barbarian, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barbarian, 1, 1)) << "the foe is behind the barbarian";

    barbarian->stats()->set_level(1);
    foe->setxy(195, 151);  // a 20 px gap above
    EXPECT_FALSE(gate(barbarian, 1, 1)) << "a bot of level 1: 16 px";
    barbarian->set_owned_myguy(std::make_unique<guy>(FAMILY_BARBARIAN));
    barbarian->myguy->strength = 14;  // 1 + 14 / 7 = 3 px a step: 24 px
    EXPECT_TRUE(gate(barbarian, 1, 0)) << "the classic answer";
    EXPECT_TRUE(gate(barbarian, 1, 1)) << "a hero's boulder rolls by its strength";
}

// EXPLODING BOULDER bursts on that foe (15 + 4 px a level, 31 at level 4).
// With the setting on it wants a second living foe in the blast, and
// neither the barbarian nor an ally of the barbarian in it.
TEST_F(GameplayFixesBots, barbarian_exploding_boulder_wants_a_cluster_with_no_ally_in_it)
{
    living* barbarian = body(add(FAMILY_BARBARIAN, 1, 200, 200, 4, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 195, 131, 1), 25, 29);  // a 40 px gap above
    living* beside = body(add(FAMILY_TOWER1, 2, 215, 131, 1), 25, 29);  // 20 px from it
    ASSERT_TRUE(barbarian && foe && beside);

    EXPECT_TRUE(gate(barbarian, 2, 0)) << "the classic answer: always";
    EXPECT_TRUE(gate(barbarian, 2, 1)) << "two foes in the blast";

    beside->set_dead(1);
    EXPECT_TRUE(gate(barbarian, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barbarian, 2, 1)) << "a lone foe: HURL BOULDER does it";
    EXPECT_TRUE(gate(barbarian, 1, 1)) << "and HURL BOULDER would throw";

    living* second = body(add(FAMILY_TOWER1, 2, 215, 131, 1), 25, 29);
    living* ally = body(add(FAMILY_SOLDIER, 1, 175, 131, 1), 16, 16);  // 20 px from the foe
    ASSERT_TRUE(second && ally);
    EXPECT_TRUE(gate(barbarian, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barbarian, 2, 1)) << "an ally of the barbarian stands in the blast";

    ally->set_dead(1);
    EXPECT_TRUE(gate(barbarian, 2, 1)) << "the ally gone";
    foe->setxy(200, 175);  // 25 px from the barbarian
    second->setxy(220, 175);
    EXPECT_TRUE(gate(barbarian, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barbarian, 2, 1)) << "the barbarian would stand in its own blast";
}
