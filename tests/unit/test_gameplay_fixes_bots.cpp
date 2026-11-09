/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The mage's, the archmage's and the orc's bots with the Gameplay Fixes
// setting.
//
// In the classic game the mage's one bot rule ("cast when no foe is within
// 110 px, or when more than three are") was written for TELEPORT and applied
// to every slot, so a mage cast WARP SPACE, FREEZE TIME and ENERGY WAVE at
// nobody and held them back in a fight. The archmage had no rule at all for
// its first three slots, and its shift (heartburst or chain lightning, image
// or elemental, teleport or marker) was a coin; the orc asked for a foe
// within 130 px whatever it rolled. With the setting on, every special a bot
// can roll asks the condition under which it pays off, a Shift arm being a
// special of its own whose rule writes the Shift after the coin
// (docs/gameplay-fixes.md, "Bots"). With the setting off every gate answers
// what it answered before, and the shift is left as the coin set it.
//
// The gates are asked directly (og::test::check_special_ai), the way
// living::check_special asks them after the coin; the hit response and the
// casts go through the real hooks and walker::special().

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"
#include "bot_gate_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

using og::test::bot_gates::GameplayFixesBots;

// ---------------------------------------------------------------------------
// The mage (fix B)
// ---------------------------------------------------------------------------

// TELEPORT keeps the classic rule on both values: flee when nobody is near
// or when more than three foes are.
//
// RED (the setting's slot-1 branch taken out): Value of: gate(mage, 1,
// fixes) Actual: false Expected: true "alone, setting 1".
TEST_F(GameplayFixesBots, mage_teleport_keeps_the_classic_rule_on_both_values)
{
    living* mage = add(FAMILY_MAGE, 1, 200, 200, 13, 2000.0f);
    ASSERT_NE(nullptr, mage);
    for (short fixes : {short{0}, short{1}})
        EXPECT_TRUE(gate(mage, 1, fixes)) << "alone, setting " << fixes;

    living* foe = add(FAMILY_SOLDIER, 2, 260, 200, 1);
    ASSERT_NE(nullptr, foe);
    for (short fixes : {short{0}, short{1}})
        EXPECT_FALSE(gate(mage, 1, fixes)) << "one foe, setting " << fixes;

    for (short dy : {short{20}, short{40}, short{60}})
        ASSERT_NE(nullptr, add(FAMILY_SOLDIER, 2, 230, static_cast<short>(200 + dy), 1));
    for (short fixes : {short{0}, short{1}})
        EXPECT_TRUE(gate(mage, 1, fixes)) << "four foes, setting " << fixes;

    // More than three foes is the escape, never the marker: with the setting
    // on the gate writes the plain TELEPORT over a coin that fell on Shift.
    mage->set_shifter_down(1);
    EXPECT_TRUE(gate(mage, 1, 1));
    EXPECT_EQ(0, mage->shifter_down()) << "four foes: the escape is a plain TELEPORT";
}

// TELEPORT MARKER is no longer a coin: with nobody within 110 px the mage
// marks where it stands when no marker of its own stands yet and half the
// magic left after TELEPORT's 15 still covers WARP SPACE's 60, so the wander
// teleports that follow go home to it. A level-7 mage with 600 magic alone
// marks; with its marker standing, with 130 magic ((130 - 15) / 2 = 57 is
// short of 60) or with four foes at its feet (the escape) it teleports
// plainly. With the setting off the coin is left as it fell.
//
// RED (the coin kept with the setting on): Expected equality of these
// values: 1 mage->shifter_down() Which is: 0 "alone, funded, no marker".
TEST_F(GameplayFixesBots, mage_marker_is_placed_once_where_the_mage_will_return)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 600.0f), 16, 16);
    ASSERT_NE(nullptr, mage);

    for (short coin : {short{0}, short{1}}) {
        mage->set_shifter_down(coin);
        EXPECT_TRUE(gate(mage, 1, 0)) << "the classic answer: nobody within 110";
        EXPECT_EQ(coin, mage->shifter_down()) << "setting off: the coin is left as it fell";
    }

    mage->set_shifter_down(0);
    EXPECT_TRUE(gate(mage, 1, 1));
    EXPECT_EQ(1, mage->shifter_down()) << "alone, funded, no marker";

    walker* marker = world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->setxy(600, 600);
    marker->set_owner(mage);
    mage->set_shifter_down(1);
    EXPECT_TRUE(gate(mage, 1, 1));
    EXPECT_EQ(0, mage->shifter_down()) << "its marker stands: a plain TELEPORT goes home";
    marker->set_dead(1);

    mage->stats()->set_magicpoints(130.0f);
    mage->set_shifter_down(1);
    EXPECT_TRUE(gate(mage, 1, 1));
    EXPECT_EQ(0, mage->shifter_down()) << "130 magic: half the spare (57) is short of WARP SPACE's 60";
    mage->stats()->set_magicpoints(600.0f);

    const short at[4][2] = {{220, 200}, {180, 200}, {200, 220}, {200, 180}};
    for (const auto& p : at)
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, p[0], p[1], 1), 16, 16));
    mage->set_shifter_down(1);
    EXPECT_TRUE(gate(mage, 1, 1)) << "four foes: the escape";
    EXPECT_EQ(0, mage->shifter_down()) << "four foes at its feet: no marker";
}

// WARP SPACE fires a fireball along each of the eight headings, and every
// bolt of the ring crawls one pixel a tick: at level 7 with 600 magic each
// gains 12 steps from the spare pool, so the ring reaches a 21 px gap
// straight and 20 on a diagonal. With the setting on it wants what one
// fireball cannot do: two living foes inside that ring, or one the mage does
// not face (its fire heading is UP at spawn, lastx = lasty = 0). The classic
// gate cast it with nobody near and refused it with one to three foes
// within 110 px.
//
// RED (the setting's slot-2 answer on "a foe within 110"): Value of:
// gate(mage, 2, 1) Actual: true Expected: false "one tower above, faced";
// and the same for "a tower below at a 22 px gap: outside the ring".
TEST_F(GameplayFixesBots, mage_warp_space_wants_a_crowd_or_a_foe_behind_in_its_point_blank_ring)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 600.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    mage->set_lastx(0.0f);
    mage->set_lasty(0.0f);  // the spawn heading: a fired bolt leaves UP
    EXPECT_TRUE(gate(mage, 2, 0)) << "the classic answer: nobody within 110";
    EXPECT_FALSE(gate(mage, 2, 1)) << "alone: the ring hits nobody";

    // A tower (25 by 29) above the mage, its bottom edge a 15 px gap off.
    living* above = body(add(FAMILY_TOWER1, 2, 196, 156, 1), 25, 29);
    ASSERT_NE(nullptr, above);
    EXPECT_FALSE(gate(mage, 2, 0)) << "the classic answer: one foe within 110";
    EXPECT_FALSE(gate(mage, 2, 1)) << "one tower above, faced: a fireball is the answer";

    // The same tower below the mage: the mage does not face it.
    above->setxy(196, 231);
    EXPECT_FALSE(gate(mage, 2, 0)) << "the classic answer";
    EXPECT_TRUE(gate(mage, 2, 1)) << "one tower below at a 15 px gap, unfaced";
    above->setxy(196, 237);  // a 21 px gap: the ring's last pixel
    EXPECT_TRUE(gate(mage, 2, 1)) << "a 21 px gap: the edge is in";
    above->setxy(196, 238);
    EXPECT_FALSE(gate(mage, 2, 1)) << "a tower below at a 22 px gap: outside the ring";

    // Two towers in the ring, one of them faced: a crowd.
    above->setxy(196, 156);
    living* below = body(add(FAMILY_TOWER1, 2, 196, 231, 1), 25, 29);
    ASSERT_NE(nullptr, below);
    EXPECT_FALSE(gate(mage, 2, 0)) << "the classic answer: two foes within 110";
    EXPECT_TRUE(gate(mage, 2, 1)) << "two towers at 15 px gaps";
    below->setxy(196, 241);  // a 25 px gap: out of the ring
    EXPECT_FALSE(gate(mage, 2, 1)) << "a tower at a 25 px gap does not count";
}

// ENERGY WAVE runs along the mage's fire heading, through walls, about as
// far as the old 110 px: with the setting on it wants a living foe within
// 110 px that the mage faces. A tower 60 px above a mage heading UP is in
// its path; below it, or 111 px off, it is not. The heading is read from
// lastx / lasty, so turning the mage DOWN turns the answer.
//
// RED (the setting's slot-4 answer on "a foe within 110"): Value of:
// gate(mage, 4, 1) Actual: true Expected: false "a tower 60 px below, unfaced".
TEST_F(GameplayFixesBots, mage_energy_wave_wants_a_faced_foe_within_110)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 600.0f), 16, 16);
    living* tower = body(add(FAMILY_TOWER1, 2, 200, 140, 1), 25, 29);  // 60 px above
    ASSERT_TRUE(mage && tower);
    mage->set_lastx(0.0f);
    mage->set_lasty(0.0f);  // the spawn heading: UP
    EXPECT_FALSE(gate(mage, 4, 0)) << "the classic answer: one foe within 110";
    EXPECT_TRUE(gate(mage, 4, 1)) << "a tower 60 px above, faced";

    tower->setxy(200, 260);  // 60 px below
    EXPECT_FALSE(gate(mage, 4, 0)) << "the classic answer";
    EXPECT_FALSE(gate(mage, 4, 1)) << "a tower 60 px below, unfaced";
    mage->set_lasty(1.0f);  // the mage turned DOWN
    EXPECT_TRUE(gate(mage, 4, 1)) << "turned to face it";

    mage->set_lasty(0.0f);
    tower->setxy(200, 89);  // 111 px above
    EXPECT_TRUE(gate(mage, 4, 0)) << "the classic answer: nobody within 110";
    EXPECT_FALSE(gate(mage, 4, 1)) << "a faced tower 111 px off";
}

// ENERGY WAVE fires the fireball the wave rides on (living-03-mage.lua,
// energy_wave): with a wall under the fireball's pad fire() makes nothing
// and the cast refuses COULD NOT FIRE. With the setting on the gate asks
// the pad (G.pad_clear) and says no. The mage faces RIGHT from (192, 192):
// the pad starts at x 209, inside grid cell (13, 12); the tower stands
// 40 px east, past that cell, faced and within 110.
//
// RED (the gate's pad term removed): gate(mage, 4, 1) is true with the wall
// under the pad.
TEST_F(GameplayFixesBots, mage_energy_wave_refuses_a_pad_on_a_wall)
{
    living* mage = body(add(FAMILY_MAGE, 1, 192, 192, 10, 600.0f), 16, 16);
    living* tower = body(add(FAMILY_TOWER1, 2, 248, 186, 1), 25, 29);  // a 40 px gap east
    ASSERT_TRUE(mage && tower);
    settle_body(mage);  // file the 16 by 16 box the test set
    mage->set_lastx(1.0f);
    mage->set_lasty(0.0f);
    EXPECT_TRUE(gate(mage, 4, 1)) << "open grass: a faced tower within 110, the pad clear";

    pad_wall(world(), 13, 12);
    ASSERT_EQ(walker::PadAnswer::Terrain, mage->fire_pad_answer())
        << "precondition: the wall is under the fireball's pad";
    EXPECT_FALSE(gate(mage, 4, 0)) << "the classic answer: one foe within 110";
    EXPECT_FALSE(gate(mage, 4, 1)) << "a wall under the pad: COULD NOT FIRE";
    tower->setxy(248, 300);  // the classic rule casts with nobody within 110
    EXPECT_TRUE(gate(mage, 4, 0)) << "the classic answer: nobody within 110, the wall ignored";
}

// FREEZE TIME: with the setting on it waits for the allies' fight: at least
// three foes within a blow (a gap of at most 5 px on each axis) of allies
// other than the mage. Where the foes stand relative to the mage is not
// asked, and the classic 110 px count plays no part.
//
// RED (the setting's slot-3 answer on the 110 px rule): Value of:
// gate(mage, 3, 1) Actual: false Expected: true "three foes at blows, far
// from the mage"; and Actual: false Expected: true "back at blows".
TEST_F(GameplayFixesBots, mage_freeze_time_waits_for_three_foes_at_blows_with_allies_with_the_setting_on)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 2000.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    // Three allies 200 px east of the mage, each with a foe whose body
    // starts a 5 px gap past the ally's right edge (ally 400..415, foe from
    // 421): the last pixel a blow lands.
    living* foes[3] = {};
    for (int k = 0; k < 3; ++k) {
        const short y = static_cast<short>(200 + 60 * k);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 1, 400, y, 1), 16, 16));
        foes[k] = body(add(FAMILY_SOLDIER, 2, 421, y, 1), 16, 16);
        ASSERT_NE(nullptr, foes[k]);
    }
    EXPECT_TRUE(gate(mage, 3, 0)) << "the classic answer: nobody within 110 of the mage";
    EXPECT_TRUE(gate(mage, 3, 1)) << "three foes at blows, far from the mage";

    foes[2]->setxy(422, 320);  // a 6 px gap: one pixel out of a blow's reach
    EXPECT_TRUE(gate(mage, 3, 0)) << "the classic answer";
    EXPECT_FALSE(gate(mage, 3, 1)) << "two foes at blows, the third a pixel out of reach";
    foes[2]->setxy(421, 320);
    EXPECT_TRUE(gate(mage, 3, 1)) << "back at blows";

    // The classic count is not consulted: a foe 60 px from the mage, which
    // turns the classic answer to no, changes nothing here.
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 260, 200, 1), 16, 16));
    EXPECT_FALSE(gate(mage, 3, 0)) << "the classic answer: one foe within 110";
    EXPECT_TRUE(gate(mage, 3, 1)) << "three at blows: the foe near the mage changes nothing";
}

// A crowd at the mage's own feet is TELEPORT's business (15 magic, the
// classic flee rule), not a 500-magic freeze: with the setting on FREEZE
// TIME refuses four foes touching the mage when no ally is at blows.
//
// RED (the setting's slot-3 answer on the 110 px rule): Value of:
// gate(mage, 3, 1) Actual: true Expected: false "four foes at the mage's
// feet: FREEZE TIME refuses"; and the same for "an idle ally far away:
// still nobody at blows".
TEST_F(GameplayFixesBots, mage_freeze_time_leaves_a_crowd_at_the_mage_to_teleport_with_the_setting_on)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 2000.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    const short at[4][2] = {{220, 200}, {180, 200}, {200, 220}, {200, 180}};
    for (const auto& p : at)
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, p[0], p[1], 1), 16, 16));
    EXPECT_TRUE(gate(mage, 3, 0)) << "the classic answer: more than three within 110";
    EXPECT_FALSE(gate(mage, 3, 1)) << "four foes at the mage's feet: FREEZE TIME refuses";
    EXPECT_TRUE(gate(mage, 1, 1)) << "and TELEPORT is the answer (the classic flee rule)";

    // An ally far from any foe does not change it.
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 1, 600, 600, 1), 16, 16));
    EXPECT_FALSE(gate(mage, 3, 1)) << "an idle ally far away: still nobody at blows";
}

// The count is of foes, not of blows: a foe at blows with two allies is one
// foe. An ally at blows that is under a quarter of its health is a cue by
// itself; a quarter exactly is not under it, a hurt ally with nobody at
// blows is no cue, and the mage's own health never is. The bodies' sizes
// are read: a tower (25 by 29) touching an ally from the left is at blows
// although its top-left corner is 26 px away.
//
// RED (the setting's slot-3 answer on the 110 px rule): Value of:
// gate(mage, 3, 1) Actual: false Expected: true "a third foe: the cue";
// Actual: false Expected: true "an ally at blows under a quarter"; and
// Actual: true Expected: false "the mage's own danger is TELEPORT's" (the
// 110 px rule sees the foe at the mage and says yes).
TEST_F(GameplayFixesBots, mage_freeze_time_counts_foes_once_and_takes_a_dying_ally_as_the_cue_with_the_setting_on)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 2000.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    // Two allies stacked and one foe touching both: three bodies at blows,
    // one foe. A second pair stands apart.
    living* a1 = body(add(FAMILY_SOLDIER, 1, 400, 200, 1), 16, 16);
    living* a2 = body(add(FAMILY_SOLDIER, 1, 400, 216, 1), 16, 16);
    living* shared = body(add(FAMILY_SOLDIER, 2, 420, 208, 1), 16, 16);
    living* a3 = body(add(FAMILY_SOLDIER, 1, 400, 300, 1), 16, 16);
    living* f3 = body(add(FAMILY_SOLDIER, 2, 420, 300, 1), 16, 16);
    ASSERT_TRUE(a1 && a2 && shared && a3 && f3);
    EXPECT_FALSE(gate(mage, 3, 1)) << "three allies at blows but two foes";

    // The third foe is a tower touching an ally from the left: its corner
    // is 26 px from the ally's, its body 1 px.
    living* a4 = body(add(FAMILY_SOLDIER, 1, 400, 400, 1), 16, 16);
    living* tower = body(add(FAMILY_TOWER1, 2, 374, 400, 1), 25, 29);
    ASSERT_TRUE(a4 && tower);
    EXPECT_TRUE(gate(mage, 3, 1)) << "a third foe: the cue";
    tower->setxy(369, 400);  // a 6 px gap: out of a blow's reach
    EXPECT_FALSE(gate(mage, 3, 1)) << "the tower a gap too far";
    EXPECT_TRUE(gate(mage, 3, 0)) << "the classic answer throughout: nobody within 110 of the mage";

    // The quarter-health arm, on the pair that stands apart (400 max HP).
    a3->stats()->set_hitpoints(100.0f);  // exactly a quarter
    EXPECT_FALSE(gate(mage, 3, 1)) << "a quarter exactly is not under it";
    a3->stats()->set_hitpoints(99.0f);
    EXPECT_TRUE(gate(mage, 3, 1)) << "an ally at blows under a quarter";
    f3->setxy(425, 300);  // its foe steps a blow out of reach
    EXPECT_FALSE(gate(mage, 3, 1)) << "a hurt ally with nobody at blows";
    f3->setxy(420, 300);
    a3->stats()->set_hitpoints(400.0f);
    mage->stats()->set_hitpoints(99.0f);
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 220, 200, 1), 16, 16));  // a foe at the mage
    EXPECT_FALSE(gate(mage, 3, 1)) << "the mage's own danger is TELEPORT's";
}

// HEARTBURST wants a foe inside its own reach, 80 + 2 px per level: 106 px
// at level 13, a little short of the teleport rule's 110.
//
// RED (the classic gate): Value of: gate(mage, 5, 1) Actual: true Expected:
// false "120 px: outside the burst"; Actual: false Expected: true "60 px".
TEST_F(GameplayFixesBots, mage_heartburst_wants_a_foe_in_its_own_reach_with_the_setting_on)
{
    living* mage = add(FAMILY_MAGE, 1, 200, 200, 13, 2000.0f);
    living* foe = add(FAMILY_SOLDIER, 2, 320, 200, 1);  // 120 px
    ASSERT_TRUE(mage && foe);
    EXPECT_TRUE(gate(mage, 5, 0)) << "120 px: the classic answer (nobody within 110)";
    EXPECT_FALSE(gate(mage, 5, 1)) << "120 px: outside the burst";

    foe->setxy(307, 200);  // 107 px: inside 110, outside 106
    EXPECT_FALSE(gate(mage, 5, 0)) << "107 px: the classic answer";
    EXPECT_FALSE(gate(mage, 5, 1)) << "107 px: one past the burst's reach";

    foe->setxy(306, 200);  // 106 px: the edge of the burst
    EXPECT_TRUE(gate(mage, 5, 1)) << "106 px: the edge is in";

    foe->setxy(260, 200);  // 60 px
    EXPECT_FALSE(gate(mage, 5, 0)) << "60 px: the classic answer";
    EXPECT_TRUE(gate(mage, 5, 1)) << "60 px";
}

// ---------------------------------------------------------------------------
// The archmage (fixes B and C)
// ---------------------------------------------------------------------------

// TELEPORT: the mage's rule with the setting on; the classic archmage had no
// gate, so every answer is yes with it off.
//
// RED (no gate on the row): Value of: gate(arch, 1, 1) Actual: true
// Expected: false "one foe at 60 px: stay and fight".
TEST_F(GameplayFixesBots, archmage_teleport_takes_the_mage_rule_with_the_setting_on)
{
    living* arch = add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 2000.0f);
    ASSERT_NE(nullptr, arch);
    EXPECT_TRUE(gate(arch, 1, 0));
    EXPECT_TRUE(gate(arch, 1, 1)) << "alone: flee";

    living* foe = add(FAMILY_SOLDIER, 2, 260, 200, 1);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(gate(arch, 1, 0)) << "the classic archmage had no gate";
    EXPECT_FALSE(gate(arch, 1, 1)) << "one foe at 60 px: stay and fight";

    foe->setxy(311, 200);  // 111 px: out of the rule's reach
    EXPECT_TRUE(gate(arch, 1, 1)) << "nobody within 110: flee";
    foe->setxy(310, 200);  // 110 px: the edge is in
    EXPECT_FALSE(gate(arch, 1, 1)) << "one foe at 110 px";

    for (short dy : {short{20}, short{40}, short{60}})
        ASSERT_NE(nullptr, add(FAMILY_SOLDIER, 2, 230, static_cast<short>(200 + dy), 1));
    EXPECT_TRUE(gate(arch, 1, 0));
    EXPECT_TRUE(gate(arch, 1, 1)) << "four foes: flee";
}

// The archmage's TELEPORT MARKER takes the mage's rule, with HEARTBURST (80)
// as the special the halved pool must still cover: a level-10 archmage with
// 170 magic keeps (170 - 10) / 2 = 80 and marks, with 168 it keeps 79 and
// does not. With the setting off the coin is left as it fell.
//
// RED (the coin kept with the setting on): Expected equality of these
// values: 1 arch->shifter_down() Which is: 0 "alone, funded, no marker".
TEST_F(GameplayFixesBots, archmage_marker_is_placed_once_where_the_archmage_will_return)
{
    living* arch = body(add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 600.0f), 16, 16);
    ASSERT_NE(nullptr, arch);

    for (short coin : {short{0}, short{1}}) {
        arch->set_shifter_down(coin);
        EXPECT_TRUE(gate(arch, 1, 0)) << "the classic archmage had no gate";
        EXPECT_EQ(coin, arch->shifter_down()) << "setting off: the coin is left as it fell";
    }

    arch->set_shifter_down(0);
    EXPECT_TRUE(gate(arch, 1, 1));
    EXPECT_EQ(1, arch->shifter_down()) << "alone, funded, no marker";

    walker* marker = world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, marker);
    marker->setxy(600, 600);
    marker->set_owner(arch);
    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 1, 1));
    EXPECT_EQ(0, arch->shifter_down()) << "its marker stands";
    marker->set_dead(1);

    arch->stats()->set_magicpoints(170.0f);
    arch->set_shifter_down(0);
    EXPECT_TRUE(gate(arch, 1, 1));
    EXPECT_EQ(1, arch->shifter_down()) << "170 magic: half the spare (80) just covers HEARTBURST";
    arch->stats()->set_magicpoints(168.0f);
    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 1, 1));
    EXPECT_EQ(0, arch->shifter_down()) << "168 magic: half the spare (79) is short";
}

// HEARTBURST / CHAIN LIGHTNING at level 10: the burst reaches 80 + 2*10 =
// 100 px, the chain 200 + 5*10 + 2*10 = 270 px. The coin's arm is kept when
// it has a target, swapped when only the other one has, refused when
// neither has. The chain always reaches at least as far as the burst, so a
// chain with no target always ends in a refusal.
//
// RED (no gate on the row): Expected equality of these values: c.answer
// Which is: false gate(arch, 2, 1) Which is: true "271 px, the chain's
// coin: nobody in reach"; c.shift_after Which is: 1 arch->shifter_down()
// Which is: 0 "101 px: only the chain has a target".
TEST_F(GameplayFixesBots, archmage_burst_or_chain_takes_the_arm_with_a_target_with_the_setting_on)
{
    living* arch = add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 2000.0f);
    living* foe = add(FAMILY_SOLDIER, 2, 260, 200, 1);  // 60 px: both reach
    ASSERT_TRUE(arch && foe);

    struct Case {
        short x;
        short shift_before;
        bool answer;
        short shift_after;
        const char* what;
    };
    const Case cases[] = {
        {260, 0, true, 0, "60 px, the burst's coin: kept"},
        {260, 1, true, 1, "60 px, the chain's coin: kept"},
        {300, 0, true, 0, "100 px: the burst's edge is in"},
        {301, 0, true, 1, "101 px: only the chain has a target"},
        {400, 1, true, 1, "200 px, the chain's coin: kept"},
        {470, 0, true, 1, "270 px: the chain's edge is in"},
        {471, 1, false, 0, "271 px, the chain's coin: nobody in reach"},
        {471, 0, false, 0, "271 px, the burst's coin: nobody in reach"},
    };
    for (const Case& c : cases) {
        foe->setxy(c.x, static_cast<short>(200));
        arch->set_shifter_down(c.shift_before);
        EXPECT_EQ(c.answer, gate(arch, 2, 1)) << c.what;
        EXPECT_EQ(c.shift_after, arch->shifter_down()) << c.what;
    }

    // With the setting off the row has no opinion and the coin stands.
    foe->setxy(471, 200);
    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 2, 0)) << "the classic archmage had no gate";
    EXPECT_EQ(1, arch->shifter_down()) << "the coin is left as it fell";
}

// SUMMON ELEMENTAL: the kept rule, a living foe within 200 px and a spare
// pool (magic less the slot's 500) of at least 500; a tent is no foe to set
// a daemon on. Below that pool the slot is SUMMON IMAGE, whose own rule (the
// test after this one) wants two foes hunting the archmage: one stranger at
// 150 px is not that.
//
// RED (the elemental counting generators): Value of: gate(arch, 3, 1)
// Actual: true Expected: false "a tent at 150 px: nobody living".
TEST_F(GameplayFixesBots, archmage_summon_wants_a_foe_and_the_pool_for_an_elemental_with_the_setting_on)
{
    living* arch = body(add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 600.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 350, 200, 1), 16, 16);  // 150 px
    ASSERT_TRUE(arch && foe);
    ASSERT_EQ(500, static_cast<int>(arch->stats()->special_cost(3))) << "the slot's price";

    // With the setting off the row has no opinion and the coin stands.
    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 3, 0)) << "the classic archmage had no gate";
    EXPECT_EQ(1, arch->shifter_down()) << "the coin is left as it fell";

    struct Case {
        float mp;
        short shift_before;
        bool answer;
        short shift_after;
        const char* what;
    };
    const Case cases[] = {
        {600.0f, 1, false, 0, "spare 100: the image, and one stranger is no decoy's crowd"},
        {999.0f, 1, false, 0, "spare 499: the image"},
        {1000.0f, 0, true, 1, "spare 500: the elemental"},
        {1100.0f, 0, true, 1, "spare 600: the elemental"},
    };
    for (const Case& c : cases) {
        arch->stats()->set_magicpoints(c.mp);
        arch->set_shifter_down(c.shift_before);
        EXPECT_EQ(c.answer, gate(arch, 3, 1)) << c.what << ": a foe at 150 px";
        EXPECT_EQ(c.shift_after, arch->shifter_down()) << c.what;
    }

    foe->setxy(400, 200);  // 200 px: the edge is in
    EXPECT_TRUE(gate(arch, 3, 1)) << "200 px";
    foe->setxy(401, 200);  // 201 px
    EXPECT_FALSE(gate(arch, 3, 1)) << "201 px: nobody to summon against";

    foe->setxy(600, 600);
    walker* tent = body(world().add_ob(Order::Generator, FAMILY_TENT), 16, 16);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(2);
    tent->setxy(350, 200);
    EXPECT_FALSE(gate(arch, 3, 1)) << "a tent at 150 px: nobody living";
    EXPECT_EQ(1, arch->shifter_down()) << "still the elemental's pool";
}

// SUMMON IMAGE is a phantom with one hit point: a decoy, worth its 500 when
// it will draw blows that have not landed yet. With the setting on it wants
// at least two living foes within 200 px hunting the archmage (their foe is
// it) and none within a blow of it (there HEARTBURST is the cheaper answer),
// health above the flee threshold (3/8 of 400 for a bot: below it TELEPORT
// is the answer), no phantom of its own standing, and HEARTBURST's 80 left
// after the 500.
//
// RED (the setting's slot-3 answer on "a foe within 200"): Value of:
// gate(arch, 3, 1) Actual: true Expected: false "one hunter".
TEST_F(GameplayFixesBots, archmage_summon_image_wants_two_hunters_not_yet_at_blows_and_a_pool_left)
{
    living* arch = body(add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 600.0f), 16, 16);
    living* east = body(add(FAMILY_SOLDIER, 2, 300, 200, 1), 16, 16);   // 100 px
    living* south = body(add(FAMILY_SOLDIER, 2, 200, 300, 1), 16, 16);  // 100 px
    living* bait = body(add(FAMILY_SOLDIER, 1, 700, 700, 1), 16, 16);   // someone else
    ASSERT_TRUE(arch && east && south && bait);
    east->set_foe(arch);
    south->set_foe(arch);

    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 3, 0)) << "the classic archmage had no gate";
    EXPECT_EQ(1, arch->shifter_down()) << "the coin is left as it fell";

    EXPECT_TRUE(gate(arch, 3, 1)) << "two hunters at 100 px";
    EXPECT_EQ(0, arch->shifter_down()) << "spare 100: the image";

    south->set_foe(nullptr);
    EXPECT_FALSE(gate(arch, 3, 1)) << "one hunter";
    east->set_foe(bait);
    south->set_foe(bait);
    EXPECT_FALSE(gate(arch, 3, 1)) << "two foes hunting someone else";
    east->set_foe(arch);
    south->set_foe(arch);
    ASSERT_TRUE(gate(arch, 3, 1));

    east->setxy(217, 200);  // a 1 px gap: at blows
    EXPECT_FALSE(gate(arch, 3, 1)) << "one of the two at blows: HEARTBURST's business";
    east->setxy(300, 200);

    arch->stats()->set_hitpoints(149.0f);  // under 3/8 of 400
    EXPECT_FALSE(gate(arch, 3, 1)) << "under the flee threshold: TELEPORT's business";
    arch->stats()->set_hitpoints(150.0f);
    EXPECT_TRUE(gate(arch, 3, 1)) << "at the threshold";
    arch->stats()->set_hitpoints(400.0f);

    living* phantom = body(add(FAMILY_ELF, 1, 260, 260, 1), 16, 16);
    ASSERT_NE(nullptr, phantom);
    phantom->stats()->set_max_hitpoints(1.0f);
    phantom->stats()->set_hitpoints(1.0f);
    phantom->set_owner(arch);
    EXPECT_FALSE(gate(arch, 3, 1)) << "its phantom stands";
    phantom->set_dead(1);
    ASSERT_TRUE(gate(arch, 3, 1));

    arch->stats()->set_magicpoints(570.0f);  // spare 70: short of HEARTBURST's 80
    EXPECT_FALSE(gate(arch, 3, 1)) << "nothing left for a HEARTBURST";
    arch->stats()->set_magicpoints(580.0f);
    EXPECT_TRUE(gate(arch, 3, 1)) << "spare 80: just enough";
}

// MIND CONTROL reaches 80 + 4 px per level (120 px at level 10) and takes
// only a foe it can: never charmed, not a hero a player drives, not above the
// archmage's level. With the setting on a bot casts it only at such a foe in
// reach; the classic gate held only the rest.
//
// RED (the rest-only gate): Value of: gate(arch, 4, 1) Actual: true
// Expected: false "121 px: out of reach".
TEST_F(GameplayFixesBots, archmage_mind_control_wants_a_charmable_foe_in_reach)
{
    living* arch = body(add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 600.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 280, 200, 10), 16, 16);  // 80 px
    ASSERT_TRUE(arch && foe);
    EXPECT_TRUE(gate(arch, 4, 0)) << "the classic answer";
    EXPECT_TRUE(gate(arch, 4, 1)) << "a level-10 soldier at 80 px";

    foe->setxy(321, 200);  // 121 px
    EXPECT_TRUE(gate(arch, 4, 0)) << "the classic answer";
    EXPECT_FALSE(gate(arch, 4, 1)) << "121 px: out of reach";
    foe->setxy(320, 200);
    EXPECT_TRUE(gate(arch, 4, 1)) << "120 px: the edge is in";

    foe->stats()->set_level(11);
    EXPECT_FALSE(gate(arch, 4, 1)) << "a level-11 foe resists";
    foe->stats()->set_level(10);

    foe->set_real_team_num(2);
    EXPECT_FALSE(gate(arch, 4, 1)) << "already charmed";
    foe->set_real_team_num(255);

    foe->set_user(0);
    foe->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(og::sim::seat_driven(*foe));
    EXPECT_FALSE(gate(arch, 4, 1)) << "a hero a seat drives resists";
    EXPECT_TRUE(gate(arch, 4, 0)) << "the classic answer";

    // The rest still holds the slot on both values.
    foe->set_user(-1);
    foe->set_act_type(ACT_RANDOM);
    arch->set_special_rest_slot(4);
    arch->set_special_rest_left(10);
    EXPECT_FALSE(gate(arch, 4, 0)) << "resting";
    EXPECT_FALSE(gate(arch, 4, 1)) << "resting";
}

// ---------------------------------------------------------------------------
// The archmage's hit response (fix C)
// ---------------------------------------------------------------------------

namespace {

std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    return state_where_draws_are({bound}, {want});
}

// The world-RNG state after `draws` more draws of next(bound) from `state`.
std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

struct StruckOutcome {
    int elementals = 0;
    int phantoms = 0;
    int explosions = 0;
    int chains = 0;
    float mp = 0.0f;
    int current_special = 0;
    int ani_type = 0;
    std::uint32_t rng_state = 0;
};

int live_fx_of(GameWorld& w, int family)
{
    int n = 0;
    for (const auto* list : {&w.oblist, &w.fxlist}) {
        for (const auto& ob : *list) {
            if (ob != nullptr && ob->dead() == 0 && ob->query_order() == Order::FX
                && ob->family() == family)
                n++;
        }
    }
    return n;
}

StruckOutcome outcome_of(GameWorld& w, walker* arch)
{
    StruckOutcome out;
    for (const auto& ob : w.oblist) {
        if (ob == nullptr || ob->dead() != 0 || ob->owner() != arch)
            continue;
        if (ob->query_order() != Order::Living)
            continue;
        if (std::string(ob->stats()->name) == "Phantom")
            out.phantoms++;
        else if (ob->family() == FAMILY_FIREELEMENTAL)
            out.elementals++;
    }
    out.explosions = live_fx_of(w, FAMILY_EXPLOSION);
    out.chains = live_fx_of(w, FAMILY_CHAIN);
    out.mp = arch->stats()->magicpoints();
    out.current_special = arch->current_special();
    out.ani_type = arch->ani_type();
    out.rng_state = w.rng_.state_;
    return out;
}

class GameplayFixesArchmageHit : public GameplayFixesBots {
protected:
    // A level-10 archmage on team 1 (400 hit points, `hp` of them left) is
    // struck by an orc `orc_x - 200` px away. Its shift is `shift` (what the
    // last coin left, or a player's Shift key), its magic `mp`; `seated`
    // makes it a hero a seat drives; `rng` (when not 0) is the world-RNG
    // state the hit response draws from.
    StruckOutcome struck(short fixes, short shift, float mp, bool seated,
                         short orc_x = 230, float hp = 400.0f, std::uint32_t rng = 0)
    {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* arch = body(add(FAMILY_ARCHMAGE, 1, 200, 200, 10, mp), 16, 16);
        living* orc = body(add(FAMILY_ORC, 2, orc_x, 200, 1), 16, 16);
        EXPECT_TRUE(arch && orc);
        if (!(arch && orc))
            return {};
        arch->stats()->set_hitpoints(hp);
        orc->stats()->set_max_hitpoints(900.0f);
        orc->stats()->set_hitpoints(900.0f);
        if (seated) {
            arch->set_user(0);
            arch->set_act_type(ACT_CONTROL);
        }
        EXPECT_EQ(seated, og::sim::seat_driven(*arch));
        arch->set_shifter_down(shift);
        if (rng != 0)
            world().rng_.state_ = rng;
        og::test::hit_response(*get_family_descriptor(FAMILY_ARCHMAGE),
                               arch->stats(), orc);
        return outcome_of(world(), arch);
    }
};

}  // namespace

// A struck bot archmage answers with ONE HEARTBURST or CHAIN LIGHTNING,
// chosen by the slot-2 rule, and never with a summon. The classic order
// summoned first (whatever arm the last coin left: an elemental here) and
// then cast slot 2 twice ("then leave! :)" never switched to TELEPORT).
// Level 10, 1100 magic, the orc 30 px off: the coin (rand(2), pinned to 0)
// keeps HEARTBURST, which takes its 80 and half the spare (510) once.
//
// RED (the classic order with the pool's summon): Expected equality of
// these values: 0 on.elementals Which is: 1 "no summon".
TEST_F(GameplayFixesArchmageHit, a_struck_bot_bursts_once_and_never_summons)
{
    const StruckOutcome off = struck(0, 1, 1100.0f, false);
    EXPECT_EQ(1, off.elementals) << "the classic game: the stale coin summons the elemental";
    EXPECT_EQ(0, off.phantoms);

    const std::uint32_t coin0 = state_where_next_is(2, 0);
    const StruckOutcome on = struck(1, 1, 1100.0f, false, 230, 400.0f, coin0);
    EXPECT_EQ(0, on.elementals) << "no summon";
    EXPECT_EQ(0, on.phantoms) << "no summon";
    EXPECT_EQ(1, on.explosions) << "one HEARTBURST on the one foe";
    EXPECT_EQ(0, on.chains);
    EXPECT_FLOAT_EQ(510.0f, on.mp) << "80 and half the spare, once";
    EXPECT_EQ(2, on.current_special);
    EXPECT_EQ(state_after(coin0, {2}), on.rng_state) << "the slot-2 coin is the one draw";

    // The coin on CHAIN LIGHTNING keeps the chain: it has a target too.
    const std::uint32_t coin1 = state_where_next_is(2, 1);
    const StruckOutcome chain = struck(1, 0, 1100.0f, false, 230, 400.0f, coin1);
    EXPECT_EQ(0, chain.elementals);
    EXPECT_EQ(0, chain.explosions);
    EXPECT_EQ(1, chain.chains) << "one CHAIN LIGHTNING";
}

// With no foe inside the chain's 270 px the slot-2 rule refuses both arms:
// nothing is cast and nothing spent, on both values (the classic order
// looked for a foe within 200 px and found none). The coin is still drawn
// with the setting on, where the classic and the old game draw it.
//
// RED (the classic order with the setting on): Expected equality of these
// values: state_after(coin0, {2}) ... far.rng_state "the coin is drawn".
TEST_F(GameplayFixesArchmageHit, a_struck_bot_with_no_target_in_reach_casts_nothing)
{
    const std::uint32_t coin0 = state_where_next_is(2, 0);
    const StruckOutcome off = struck(0, 1, 1100.0f, false, 500, 400.0f, coin0);
    EXPECT_EQ(0, off.elementals);
    EXPECT_EQ(0, off.phantoms);
    EXPECT_EQ(0, off.explosions);
    EXPECT_FLOAT_EQ(1100.0f, off.mp) << "the classic order found nobody within 200";

    const StruckOutcome far = struck(1, 1, 1100.0f, false, 500, 400.0f, coin0);
    EXPECT_EQ(0, far.elementals);
    EXPECT_EQ(0, far.phantoms);
    EXPECT_EQ(0, far.explosions);
    EXPECT_EQ(0, far.chains);
    EXPECT_FLOAT_EQ(1100.0f, far.mp) << "300 px: nothing spent";
    EXPECT_EQ(state_after(coin0, {2}), far.rng_state) << "the coin is drawn";
}

// Below the flee threshold (3/8 of 400 for a bot) a struck archmage still
// teleports on rand(3) != 0, on both values. On rand(3) == 0 it falls
// through to the answer: the classic order summons, the fixed one bursts.
//
// RED (the classic order with the setting on): Expected equality of these
// values: 0 stay.elementals Which is: 1 "rand(3) == 0: no summon".
TEST_F(GameplayFixesArchmageHit, a_struck_bot_below_the_threshold_still_teleports)
{
    const std::uint32_t flee = state_where_next_is(3, 1);
    for (short fixes : {short{0}, short{1}}) {
        const StruckOutcome o = struck(fixes, 1, 1100.0f, false, 230, 100.0f, flee);
        EXPECT_EQ(1, o.current_special) << "setting " << fixes;
        EXPECT_EQ(ANI_TELE_OUT, o.ani_type) << "setting " << fixes << ": TELEPORT";
        EXPECT_FLOAT_EQ(1090.0f, o.mp) << "setting " << fixes << ": TELEPORT's 10";
        EXPECT_EQ(0, o.elementals + o.phantoms + o.explosions + o.chains);
    }

    const std::uint32_t stay = state_where_draws_are({3, 2}, {0, 0});
    const StruckOutcome classic = struck(0, 1, 1100.0f, false, 230, 100.0f, stay);
    EXPECT_EQ(1, classic.elementals) << "the classic order summons";
    const StruckOutcome fixed = struck(1, 1, 1100.0f, false, 230, 100.0f, stay);
    EXPECT_EQ(0, fixed.elementals) << "rand(3) == 0: no summon";
    EXPECT_EQ(0, fixed.phantoms);
    EXPECT_EQ(1, fixed.explosions) << "one HEARTBURST";
}

// The hit response runs for a player's archmage too, and a player's shift is
// the Shift key: the setting leaves it alone, so a seated archmage summons
// the same thing on both values.
//
// RED (the seat test taken out of the hit response): Expected equality of
// these values: 1 up.phantoms Which is: 0 "Shift up, spare 600, setting 1";
// and 1 held.elementals Which is: 0 "Shift held, spare 100, setting 1".
TEST_F(GameplayFixesArchmageHit, a_seated_archmage_keeps_its_shift_key)
{
    for (short fixes : {short{0}, short{1}}) {
        const StruckOutcome up = struck(fixes, 0, 1100.0f, true);
        EXPECT_EQ(1, up.phantoms) << "Shift up, spare 600, setting " << fixes;
        EXPECT_EQ(0, up.elementals) << "Shift up, spare 600, setting " << fixes;

        const StruckOutcome held = struck(fixes, 1, 600.0f, true);
        EXPECT_EQ(1, held.elementals) << "Shift held, spare 100, setting " << fixes;
        EXPECT_EQ(0, held.phantoms) << "Shift held, spare 100, setting " << fixes;
    }
}

// ---------------------------------------------------------------------------
// MIND CONTROL and a seated hero (#355)
// ---------------------------------------------------------------------------

namespace {

struct MindControlCast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

}  // namespace

// An enemy archmage's MIND CONTROL takes a hero a player is driving in the
// classic game. With the setting on the hero resists, as it does Possess
// ("HERO RESISTS"): it is skipped before the roll, costs the cast nothing,
// and an NPC beside it is still taken.
//
// RED (the classic eligibility): Value of: c.ok Actual: true Expected: false
// "the hero alone"; Expected equality of these values: 0
// static_cast<int>(hero->team_num()) Which is: 2; mp_before Which is: 1000
// arch->stats()->magicpoints() Which is: 850 "a refusal spends nothing".
TEST_F(GameplayFixesBots, mind_control_skips_a_seated_hero_with_the_setting_on)
{
    // The domination roll (rand(20) != 0) succeeds on every cast below.
    const std::uint32_t roll = state_where_next_is(20, 1);
    auto scene = [&](short fixes, bool with_npc, walker** hero_out,
                     walker** npc_out, walker** arch_out) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* arch = add(FAMILY_ARCHMAGE, 2, 200, 200, 10, 1000.0f);
        living* hero = add(FAMILY_SOLDIER, 0, 230, 200, 1);
        ASSERT_TRUE(arch && hero);
        hero->set_user(0);
        hero->set_act_type(ACT_CONTROL);
        ASSERT_TRUE(og::sim::seat_driven(*hero));
        living* npc = nullptr;
        if (with_npc) {
            npc = add(FAMILY_SOLDIER, 0, 200, 240, 1);
            ASSERT_NE(nullptr, npc);
            ASSERT_FALSE(og::sim::seat_driven(*npc));
        }
        world().rng_.state_ = roll;
        *hero_out = hero;
        *npc_out = npc;
        *arch_out = arch;
    };
    auto cast = [](walker* arch) {
        arch->set_current_special(4);
        arch->set_shifter_down(0);
        MindControlCast c;
        c.ok = arch->special(&c.why, &c.reason);
        return c;
    };

    walker* hero = nullptr;
    walker* npc = nullptr;
    walker* arch = nullptr;

    scene(0, false, &hero, &npc, &arch);
    ASSERT_FALSE(HasFatalFailure());
    MindControlCast c = cast(arch);
    EXPECT_TRUE(c.ok) << "the classic game takes the hero: " << c.reason;
    EXPECT_EQ(2, static_cast<int>(hero->team_num()));
    EXPECT_EQ(0, static_cast<int>(hero->real_team_num()));

    scene(1, false, &hero, &npc, &arch);
    ASSERT_FALSE(HasFatalFailure());
    const float mp_before = arch->stats()->magicpoints();
    c = cast(arch);
    EXPECT_FALSE(c.ok) << "the hero alone";
    EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, c.why);
    EXPECT_EQ("NO VALID TARGET", c.reason);
    EXPECT_EQ(0, static_cast<int>(hero->team_num()));
    EXPECT_EQ(255, static_cast<int>(hero->real_team_num()));
    EXPECT_EQ(0, hero->charm_left());
    EXPECT_FLOAT_EQ(mp_before, arch->stats()->magicpoints()) << "a refusal spends nothing";
    EXPECT_EQ(0, arch->special_rest_left()) << "and starts no rest";
    EXPECT_EQ(roll, world().rng_.state_) << "the hero is skipped before the roll";

    scene(1, true, &hero, &npc, &arch);
    ASSERT_FALSE(HasFatalFailure());
    c = cast(arch);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(0, static_cast<int>(hero->team_num())) << "the hero resists";
    EXPECT_EQ(255, static_cast<int>(hero->real_team_num()));
    EXPECT_EQ(2, static_cast<int>(npc->team_num())) << "the NPC beside it is taken";
    EXPECT_EQ(0, static_cast<int>(npc->real_team_num()));
}

// ---------------------------------------------------------------------------
// The orc
// ---------------------------------------------------------------------------

// HOWL stuns every body the yell reaches, a tent included (a frozen tent
// makes nothing): 160 px plus 20 per level, 180 at level 1. With the setting
// on a bot howls at a target inside the yell; the classic gate asked for a
// foe within 130 px.
//
// RED (the classic gate with the setting on): Value of: gate(orc, 1, 1)
// Actual: false Expected: true "a tower at 160 px: inside the yell".
TEST_F(GameplayFixesBots, orc_howl_wants_a_target_inside_the_yell)
{
    living* orc = body(add(FAMILY_ORC, 1, 200, 200, 1, 100.0f), 16, 16);
    living* tower = body(add(FAMILY_TOWER1, 2, 360, 200, 1), 25, 29);  // 160 px
    ASSERT_TRUE(orc && tower);
    EXPECT_FALSE(gate(orc, 1, 0)) << "the classic answer: no foe within 130";
    EXPECT_TRUE(gate(orc, 1, 1)) << "a tower at 160 px: inside the yell";

    tower->setxy(380, 200);  // 180 px: the yell's edge
    EXPECT_TRUE(gate(orc, 1, 1)) << "180 px: the edge is in";
    tower->setxy(381, 200);  // 181 px
    EXPECT_FALSE(gate(orc, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(orc, 1, 1)) << "181 px: outside the yell";

    // A tent is something the yell stops.
    tower->set_dead(1);
    walker* tent = body(world().add_ob(Order::Generator, FAMILY_TENT), 16, 16);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(2);
    tent->setxy(300, 200);  // 100 px
    orc->set_foe(nullptr);
    EXPECT_TRUE(gate(orc, 1, 0)) << "the classic answer: the tent at 100 px";
    orc->set_foe(nullptr);
    EXPECT_TRUE(gate(orc, 1, 1)) << "a tent at 100 px";

    // HOWL resting holds the slot on both values.
    orc->set_special_rest_slot(1);
    orc->set_special_rest_left(10);
    EXPECT_FALSE(gate(orc, 1, 1)) << "resting";
}

// EAT CORPSE heals the orc from a stain under it (squared centre distance at
// most 24) and refuses at full health. With the setting on a bot eats when
// it is below 60 percent of its health and a stain is under it, whatever
// the foes are doing; the classic gate asked for a foe within 130 px and
// let the meal refuse.
//
// RED (the classic gate with the setting on): Value of: gate(orc, 2, 1)
// Actual: false Expected: true "hurt, on a corpse, nobody near".
TEST_F(GameplayFixesBots, orc_eats_when_hurt_on_a_corpse_whatever_the_foe)
{
    living* orc = body(add(FAMILY_ORC, 1, 200, 200, 1, 100.0f), 16, 16);
    ASSERT_NE(nullptr, orc);
    walker* stain = body(world().add_fx_ob(Order::Treasure, FAMILY_STAIN), 16, 16);
    ASSERT_NE(nullptr, stain);
    stain->setxy(200, 200);
    orc->stats()->set_hitpoints(200.0f);
    EXPECT_FALSE(gate(orc, 2, 0)) << "the classic answer: no foe within 130";
    EXPECT_TRUE(gate(orc, 2, 1)) << "hurt, on a corpse, nobody near";

    orc->stats()->set_hitpoints(240.0f);  // 60 percent exactly
    EXPECT_FALSE(gate(orc, 2, 1)) << "60 percent is not below it";
    orc->stats()->set_hitpoints(239.0f);
    EXPECT_TRUE(gate(orc, 2, 1)) << "just below 60 percent";

    stain->setxy(208, 200);  // 8 px off centre: squared distance 64
    EXPECT_FALSE(gate(orc, 2, 1)) << "the stain 8 px off centre";
    stain->setxy(200, 200);

    orc->stats()->set_hitpoints(400.0f);
    living* foe = body(add(FAMILY_SOLDIER, 2, 260, 200, 1), 16, 16);  // 60 px
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(gate(orc, 2, 0)) << "the classic answer: a foe within 130";
    EXPECT_FALSE(gate(orc, 2, 1)) << "full health: nothing to eat for";
}

// ---------------------------------------------------------------------------
// Busy casts (fix W1)
// ---------------------------------------------------------------------------

// A cast whose first statement refuses a busy caster (SPECIAL BUSY) is no
// payoff while the caster is busy, so with the setting on its gate asks the
// cast's own guard first: the busy archer, mage marker, archmage, druid,
// thief, barbarian and orc, and a skeleton or a mage in the middle of a
// teleport. With the setting off each gate keeps its classic answer, busy
// or not. The New Specials kits (the captain's HOWL and KNIFE FAN, the
// ghost's SIPHON, the faerie's GLIMMER and her rest after BLINK or SWAP, the
// skeleton's DIG IN) have no classic gate to keep, so their guard holds on
// both values of the setting.
//
// Each row is first asked idle (the gate's yes with the setting on: the
// field is set up for the payoff), then held by the cast's own guard.
//
// RED (no guard in the gates): Value of: gate(w, slot, 1) Actual: true
// Expected: false "ARCHER BARRAGE: held, setting on", and the same for every
// row.
TEST_F(GameplayFixesBots, a_busy_bot_never_rolls_a_busy_cast_with_the_setting_on)
{
    // A classic family's row: `classic` is the setting-off answer, which the
    // guard never changes.
    auto classic_row = [&](living* w, int slot, bool classic, const char* what,
                           auto hold, auto release) {
        ASSERT_NE(nullptr, w) << what;
        release(w);
        EXPECT_TRUE(gate(w, slot, 1)) << what << ": idle, setting on";
        EXPECT_EQ(classic, gate(w, slot, 0)) << what << ": idle, setting off";
        hold(w);
        EXPECT_FALSE(gate(w, slot, 1)) << what << ": held, setting on";
        EXPECT_EQ(classic, gate(w, slot, 0)) << what << ": held, setting off";
        release(w);
    };
    // A kit's row: New Specials on, the Gameplay Fixes setting at both values.
    auto kit_row = [&](living* w, int slot, const char* what, auto hold, auto release) {
        ASSERT_NE(nullptr, w) << what;
        for (short fixes : {short{0}, short{1}}) {
            world().gameplay_fixes = fixes;
            release(w);
            EXPECT_TRUE(kit_gate(w, slot)) << what << ": idle, setting " << fixes;
            hold(w);
            EXPECT_FALSE(kit_gate(w, slot)) << what << ": held, setting " << fixes;
            release(w);
        }
    };
    auto busy = [](living* w) { w->set_busy(3.0f); };
    auto idle = [](living* w) { w->set_busy(0.0f); };
    auto teleporting = [](living* w) { w->set_ani_type(static_cast<char>(ANI_TELE_OUT)); };
    auto walking = [](living* w) { w->set_ani_type(static_cast<char>(ANI_WALK)); };

    // The archer: BARRAGE and EXPLODING BOLT at a foe faced 80 px above.
    {
        living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 7, 300.0f), 16, 16);
        ASSERT_NE(nullptr, body(add(FAMILY_TOWER1, 2, 200, 91, 1), 25, 29));
        classic_row(archer, 2, true, "ARCHER BARRAGE", busy, idle);
        classic_row(archer, 3, true, "ARCHER EXPLODING BOLT", busy, idle);
    }

    // The mage, alone: TELEPORT refuses mid-teleport; the TELEPORT MARKER
    // refuses busy, so a busy mage alone teleports plainly instead.
    fresh_world();
    {
        living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 7, 600.0f), 16, 16);
        classic_row(mage, 1, true, "MAGE TELEPORT", teleporting, walking);
        ASSERT_NE(nullptr, mage);
        mage->set_shifter_down(0);
        EXPECT_TRUE(gate(mage, 1, 1)) << "idle: TELEPORT";
        EXPECT_EQ(1, mage->shifter_down()) << "idle: the marker";
        busy(mage);
        EXPECT_TRUE(gate(mage, 1, 1)) << "busy: the plain TELEPORT";
        EXPECT_EQ(0, mage->shifter_down()) << "busy: no marker";
        mage->set_shifter_down(1);
        EXPECT_TRUE(gate(mage, 1, 0));
        EXPECT_EQ(1, mage->shifter_down()) << "busy, setting off: the coin stands";
        idle(mage);
    }

    // The archmage: TELEPORT and its marker as the mage's, then the three
    // busy slots at a level-10 soldier 80 px away.
    fresh_world();
    {
        living* arch = body(add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 600.0f), 16, 16);
        classic_row(arch, 1, true, "ARCHMAGE TELEPORT", teleporting, walking);
        ASSERT_NE(nullptr, arch);
        arch->set_shifter_down(0);
        EXPECT_TRUE(gate(arch, 1, 1)) << "idle: TELEPORT";
        EXPECT_EQ(1, arch->shifter_down()) << "idle: the marker";
        busy(arch);
        EXPECT_TRUE(gate(arch, 1, 1)) << "busy: the plain TELEPORT";
        EXPECT_EQ(0, arch->shifter_down()) << "busy: no marker";
        idle(arch);

        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 280, 200, 10), 16, 16));
        classic_row(arch, 2, true, "ARCHMAGE HEARTBURST", busy, idle);
        arch->stats()->set_magicpoints(1100.0f);  // the elemental's pool
        classic_row(arch, 3, true, "ARCHMAGE SUMMON ELEMENTAL", busy, idle);
        arch->stats()->set_magicpoints(600.0f);
        classic_row(arch, 4, true, "ARCHMAGE MIND CONTROL", busy, idle);
    }

    // The druid: each of its three castable slots, every one a busy cast.
    fresh_world();
    {
        living* druid = body(add(FAMILY_DRUID, 1, 200, 200, 4, 1250.0f), 16, 16);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 200, 160, 1), 16, 16));  // 40 px ahead
        classic_row(druid, 1, true, "DRUID GROW TREE", busy, idle);
        classic_row(druid, 2, true, "DRUID SUMMON FAERIE", busy, idle);
        ASSERT_NE(nullptr, body(add(FAMILY_ORC, 1, 240, 200, 1), 16, 16));      // an ally at 40
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 257, 200, 1), 16, 16));  // at blows with it
        classic_row(druid, 4, true, "DRUID PROTECTION", busy, idle);
    }

    // The thief: TAUNT (a foe at 60, an ally at 90), CHARM (a level-1 foe
    // 30 px away) and POISON CLOUD (two foes at blows).
    fresh_world();
    {
        living* thief = body(add(FAMILY_THIEF, 1, 200, 200, 7, 600.0f), 16, 16);
        living* foe = body(add(FAMILY_SOLDIER, 0, 260, 200, 1), 16, 16);
        ASSERT_NE(nullptr, body(add(FAMILY_ORC, 1, 110, 200, 1), 16, 16));
        ASSERT_NE(nullptr, foe);
        classic_row(thief, 3, true, "THIEF TAUNT", busy, idle);
        EXPECT_EQ(0, thief->shifter_down()) << "the taunt";
        foe->setxy(230, 200);
        classic_row(thief, 3, true, "THIEF CHARM", busy, idle);
        EXPECT_EQ(1, thief->shifter_down()) << "the charm";
        foe->setxy(217, 200);  // a 1 px gap
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 183, 200, 1), 16, 16));
        classic_row(thief, 4, true, "THIEF POISON CLOUD", busy, idle);
    }

    // The barbarian: HURL BOULDER and EXPLODING BOULDER, at a foe with a
    // 40 px gap ahead (a level-4 boulder rolls 64) and a second one beside it.
    fresh_world();
    {
        living* barbarian = body(add(FAMILY_BARBARIAN, 1, 200, 200, 4, 200.0f), 16, 16);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 200, 144, 1), 16, 16));
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 220, 144, 1), 16, 16));
        classic_row(barbarian, 1, true, "BARBARIAN HURL BOULDER", busy, idle);
        classic_row(barbarian, 2, true, "BARBARIAN EXPLODING BOULDER", busy, idle);
    }

    // The orc: HOWL at a tower 160 px away, inside the yell (the classic
    // gate wants a foe within 130: no).
    fresh_world();
    {
        living* orc = body(add(FAMILY_ORC, 1, 200, 200, 1, 100.0f), 16, 16);
        ASSERT_NE(nullptr, body(add(FAMILY_TOWER1, 2, 360, 200, 1), 25, 29));
        classic_row(orc, 1, false, "ORC HOWL", busy, idle);
    }

    // The skeleton, alone: TUNNEL refuses mid-teleport.
    fresh_world();
    {
        living* skel = body(add(FAMILY_SKELETON, 1, 200, 200, 4, 100.0f), 16, 16);
        classic_row(skel, 1, true, "SKELETON TUNNEL", teleporting, walking);
    }

    // The kits.
    fresh_world();
    {
        living* cap = body(add(FAMILY_BIG_ORC, 1, 200, 200, 7, 100.0f), 16, 16);
        ASSERT_NE(nullptr, cap);
        living* tower = body(add(FAMILY_TOWER1, 2, 360, 200, 1), 25, 29);  // 160 px
        ASSERT_NE(nullptr, tower);
        kit_row(cap, 1, "CAPTAIN HOWL", busy, idle);
        tower->set_dead(1);
        // Two towers on the fan's lines, past the hook's 60 px.
        ASSERT_NE(nullptr, body(add(FAMILY_TOWER1, 2, 196, 131, 1), 25, 29));
        ASSERT_NE(nullptr, body(add(FAMILY_TOWER1, 2, 256, 131, 1), 25, 29));
        cap->set_lastx(0.0f);
        cap->set_lasty(-1.0f);
        cap->set_curdir(FACE_UP);
        kit_row(cap, 3, "CAPTAIN KNIFE FAN", busy, idle);
        cap->set_shifter_down(9);
        busy(cap);
        EXPECT_FALSE(kit_gate(cap, 3));
        EXPECT_EQ(9, cap->shifter_down()) << "busy: neither arm, the coin stands";
        idle(cap);
    }
    fresh_world();
    {
        living* ghost = body(add(FAMILY_GHOST, 1, 200, 200, 4, 100.0f), 16, 16);
        ASSERT_NE(nullptr, ghost);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 216, 200, 1), 16, 16));
        ghost->stats()->set_hitpoints(200.0f);  // under 80 %
        kit_row(ghost, 2, "GHOST SIPHON", busy, idle);
    }
    fresh_world();
    {
        living* f = body(add(FAMILY_FAERIE, 1, 200, 200, 4, 100.0f), 16, 16);
        ASSERT_NE(nullptr, f);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 200, 220, 1), 16, 16));  // 20 px
        f->stats()->set_hitpoints(150.0f);  // under half
        kit_row(f, 2, "FAERIE GLIMMER", busy, idle);
        // BLINK and SWAP rest a hop's while: busy past her attack pause.
        // The pause alone (busy up to fire_frequency) is no rest.
        const float pause = f->fire_frequency();
        ASSERT_GT(pause, 0.0f);
        auto resting = [pause](living* w) { w->set_busy(pause + 1.0f); };
        kit_row(f, 1, "FAERIE BLINK", resting, idle);
        for (short fixes : {short{0}, short{1}}) {
            world().gameplay_fixes = fixes;
            f->set_busy(pause);
            EXPECT_TRUE(kit_gate(f, 1)) << "the plain pause, setting " << fixes;
        }
        idle(f);
    }
    fresh_world();
    {
        living* skel = body(add(FAMILY_SKELETON, 1, 200, 200, 4, 100.0f), 16, 16);
        ASSERT_NE(nullptr, skel);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 250, 200, 1), 16, 16));
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 2, 150, 200, 1), 16, 16));
        skel->stats()->set_hitpoints(200.0f);  // under 60 %
        kit_row(skel, 2, "SKELETON DIG IN", teleporting, walking);
    }
}
