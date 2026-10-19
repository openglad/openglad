/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The bot gates of the cleric, the druid and the thief: each special a bot can roll, asked
// whether it pays off, with the Gameplay Fixes setting off (the classic
// answer) and on. The cases use the GameplayFixesBots fixture
// (bot_gate_fixture.h), shared with test_gameplay_fixes_bots.cpp.
//
// Distances are the engine's: Manhattan between top-left corners
// (walker::distance_to_ob), the finders inclusive. Every body whose gap to
// another is read is given the 16 by 16 size of a common fighter.

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <memory>

using og::test::bot_gates::GameplayFixesBots;

namespace {

int mine_family()
{
    return og::families::resolve_family_string_id(Order::FX, "core:mine");
}

// A bloodstain (the corpse every raise and RESURRECT reads) of `team` with
// its top-left at (x, y).
walker* stain(GameWorld& w, unsigned char team, short x, short y)
{
    walker* s = w.add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (s == nullptr)
        return nullptr;
    s->set_team_num(team);
    s->setxy(x, y);
    s->set_sizex(16);
    s->set_sizey(16);
    return s;
}

// A live effect of `family` owned by `owner`.
walker* owned_fx(GameWorld& w, int family, walker* owner, short x, short y)
{
    walker* fx = w.add_ob(Order::FX, family);
    if (fx == nullptr)
        return nullptr;
    fx->setxy(x, y);
    fx->set_owner(owner);
    return fx;
}

int count_live(GameWorld& w, Order order, int family)
{
    int n = 0;
    for (auto& ob : w.oblist)
        if (ob && ob->query_order() == order && ob->family() == family &&
            !ob->dead())
            ++n;
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// The cleric
// ---------------------------------------------------------------------------

// Row 25. HEAL wants an ally within 60 px below its full health. The
// classic gate heals whenever any friend stands within 60 px.
//
// RED (the classic slot-1 branch on both values): gate(cleric, 1, 1) is true
// with the healthy ally ("a healthy ally: nothing to heal").
TEST_F(GameplayFixesBots, cleric_heal_wants_a_hurt_ally_within_60)
{
    living* cleric = body(add(FAMILY_CLERIC, 1, 200, 200, 4, 100.0f), 16, 16);
    living* ally = body(add(FAMILY_SOLDIER, 1, 232, 200, 1), 16, 16);
    ASSERT_TRUE(cleric && ally);

    cleric->set_shifter_down(1);
    EXPECT_TRUE(gate(cleric, 1, 0)) << "the classic answer: a friend in reach";
    EXPECT_EQ(0, cleric->shifter_down()) << "the classic HEAL";

    EXPECT_FALSE(gate(cleric, 1, 1)) << "a healthy ally: nothing to heal (and no foe for the mace)";

    ally->stats()->set_hitpoints(399.0f);
    cleric->set_shifter_down(1);
    EXPECT_TRUE(gate(cleric, 1, 1)) << "399 of 400: HEAL";
    EXPECT_EQ(0, cleric->shifter_down()) << "HEAL is Shift 0";

    ally->setxy(260, 200);  // 60 px: the edge is in
    EXPECT_TRUE(gate(cleric, 1, 1)) << "60 px";
    ally->setxy(261, 200);  // 61 px
    EXPECT_FALSE(gate(cleric, 1, 1)) << "61 px: out of the heal's reach";
}

// Row 26. MYSTIC MACE wants a living foe within 48 px (the mace orbits on a
// 24 px circle), no mace of this cleric out, the pool at least half full and
// a hero's 50 INT. The classic gate maces whenever no friend is in reach and
// the pool is half full.
//
// RED (the classic slot-1 branch): gate(cleric, 1, 1) is true with nobody on
// the map ("alone: the mace has nobody to strike").
TEST_F(GameplayFixesBots, cleric_mace_wants_a_foe_at_its_orbit_and_no_mace_out)
{
    living* cleric = body(add(FAMILY_CLERIC, 1, 200, 200, 4, 50.0f), 16, 16);
    ASSERT_NE(nullptr, cleric);

    cleric->set_shifter_down(0);
    EXPECT_TRUE(gate(cleric, 1, 0)) << "the classic answer: half the pool, nobody near";
    EXPECT_EQ(1, cleric->shifter_down()) << "the classic mace at nobody";
    EXPECT_FALSE(gate(cleric, 1, 1)) << "alone: the mace has nobody to strike";

    living* foe = body(add(FAMILY_SOLDIER, 0, 240, 200, 1), 16, 16);  // 40 px
    ASSERT_NE(nullptr, foe);
    cleric->set_shifter_down(0);
    EXPECT_TRUE(gate(cleric, 1, 1)) << "a foe at 40 px";
    EXPECT_EQ(1, cleric->shifter_down()) << "the mace is Shift 1";
    foe->setxy(248, 200);
    EXPECT_TRUE(gate(cleric, 1, 1)) << "48 px: the edge is in";
    foe->setxy(249, 200);
    EXPECT_FALSE(gate(cleric, 1, 1)) << "49 px: outside the orbit";
    foe->setxy(240, 200);

    walker* mace = owned_fx(world(), FAMILY_MAGIC_SHIELD, cleric, 200, 224);
    ASSERT_NE(nullptr, mace);
    EXPECT_FALSE(gate(cleric, 1, 1)) << "a mace of this cleric is out";
    mace->set_dead(1);
    // A living cleric (living family 5, the mace's number) the cleric owns is
    // not a mace.
    living* summoned = body(add(FAMILY_CLERIC, 1, 300, 300, 1), 16, 16);
    ASSERT_NE(nullptr, summoned);
    summoned->set_owner(cleric);
    EXPECT_TRUE(gate(cleric, 1, 1)) << "a living cleric of its own is no mace";

    cleric->stats()->set_magicpoints(24.0f);
    EXPECT_FALSE(gate(cleric, 1, 1)) << "24 of 50: under half the pool";
    EXPECT_FALSE(gate(cleric, 1, 0)) << "the classic answer: under half the pool";
    cleric->stats()->set_magicpoints(50.0f);

    cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
    cleric->myguy->intelligence = 49;
    EXPECT_FALSE(gate(cleric, 1, 1)) << "a hero without the 50 INT the mace asks";
    cleric->myguy->intelligence = 50;
    EXPECT_TRUE(gate(cleric, 1, 1)) << "a hero with 50 INT";
}

// The shove hook casts slot 1 with whatever Shift the last coin left: with
// the setting on a shoved cleric writes Shift 0 first and heals (a shove is
// an ally at its side).
//
// RED (the classic hook): with the setting on the coin's Shift 1 summons a
// mace and the hurt ally is not healed.
TEST_F(GameplayFixesBots, cleric_shoved_heals_with_the_setting_on)
{
    for (const short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* cleric = body(add(FAMILY_CLERIC, 1, 200, 200, 4, 100.0f), 16, 16);
        living* ally = body(add(FAMILY_SOLDIER, 1, 232, 200, 1), 16, 16);
        ASSERT_TRUE(cleric && ally);
        ally->stats()->set_hitpoints(200.0f);
        cleric->set_shifter_down(1);
        og::test::on_shoved(*get_family_descriptor(FAMILY_CLERIC), cleric);
        const int maces = count_live(world(), Order::FX, FAMILY_MAGIC_SHIELD);
        if (fixes == 0) {
            EXPECT_EQ(1, maces) << "the classic hook maces on the coin's Shift";
            EXPECT_FLOAT_EQ(200.0f, ally->stats()->hitpoints());
        } else {
            EXPECT_EQ(0, maces) << "a shoved cleric never maces";
            EXPECT_GT(ally->stats()->hitpoints(), 200.0f) << "it heals";
            EXPECT_EQ(0, cleric->shifter_down());
        }
    }
}

// Row 27. RAISE UNDEAD wants the stain find_nearest_blood returns within
// 60 px and a living foe within 130 px. The classic slot-2 gate is yes.
//
// RED (the classic answer on both values): gate(cleric, 2, 1) is true with
// no foe ("a corpse and nobody to fight").
TEST_F(GameplayFixesBots, cleric_raise_undead_wants_a_corpse_and_a_foe)
{
    living* cleric = body(add(FAMILY_CLERIC, 1, 200, 200, 4, 100.0f), 16, 16);
    walker* blood = stain(world(), 2, 210, 200);
    ASSERT_TRUE(cleric && blood);

    EXPECT_TRUE(gate(cleric, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(cleric, 2, 1)) << "a corpse and nobody to fight";

    living* foe = body(add(FAMILY_SOLDIER, 0, 300, 200, 1), 16, 16);  // 100 px
    ASSERT_NE(nullptr, foe);
    cleric->set_shifter_down(1);
    EXPECT_TRUE(gate(cleric, 2, 1)) << "a corpse 10 px off and a foe at 100";
    EXPECT_EQ(0, cleric->shifter_down()) << "the raise, nothing undead to turn";
    foe->setxy(330, 200);
    EXPECT_TRUE(gate(cleric, 2, 1)) << "130 px: the edge is in";
    foe->setxy(331, 200);
    EXPECT_FALSE(gate(cleric, 2, 1)) << "131 px: nobody to fight";
    foe->setxy(300, 200);

    blood->setxy(260, 200);  // beyond what find_nearest_blood answers
    EXPECT_FALSE(gate(cleric, 2, 1)) << "no corpse the raise can find";
    EXPECT_TRUE(gate(cleric, 2, 0)) << "the classic answer";
}

// Row 29. RAISE GHOST: the same rule at RAISE GHOST's 30 px.
//
// RED (the classic answer): gate(cleric, 3, 1) is true with no foe.
TEST_F(GameplayFixesBots, cleric_raise_ghost_wants_a_corpse_within_30_and_a_foe)
{
    living* cleric = body(add(FAMILY_CLERIC, 1, 200, 200, 4, 100.0f), 16, 16);
    walker* blood = stain(world(), 2, 210, 200);
    ASSERT_TRUE(cleric && blood);

    EXPECT_TRUE(gate(cleric, 3, 0)) << "the classic answer";
    EXPECT_FALSE(gate(cleric, 3, 1)) << "a corpse and nobody to fight";

    living* foe = body(add(FAMILY_SOLDIER, 0, 300, 200, 1), 16, 16);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(gate(cleric, 3, 1)) << "a corpse 10 px off and a foe at 100";
    EXPECT_EQ(0, cleric->shifter_down());

    blood->setxy(200, 230);  // 30 px: not under the 30
    EXPECT_FALSE(gate(cleric, 3, 1)) << "a corpse 30 px off";
    blood->setxy(200, 220);  // 20 px
    EXPECT_TRUE(gate(cleric, 3, 1)) << "a corpse 20 px off";
}

// Row 30. RESURRECT wants a friendly stain within 30 px (a foe's stain is
// RAISE GHOST's job at 50). The classic slot-4 gate is yes.
//
// RED (the classic answer): gate(cleric, 4, 1) is true with the hostile
// stain.
TEST_F(GameplayFixesBots, cleric_resurrect_wants_a_friendly_corpse_within_30)
{
    living* cleric = body(add(FAMILY_CLERIC, 1, 200, 200, 10, 600.0f), 16, 16);
    walker* blood = stain(world(), 1, 210, 200);
    ASSERT_TRUE(cleric && blood);

    EXPECT_TRUE(gate(cleric, 4, 1)) << "a stain of the cleric's team 10 px off";
    blood->set_team_num(0);
    EXPECT_TRUE(gate(cleric, 4, 0)) << "the classic answer";
    EXPECT_FALSE(gate(cleric, 4, 1)) << "a hostile stain";
    blood->set_team_num(1);
    blood->setxy(231, 200);  // 31 px
    EXPECT_FALSE(gate(cleric, 4, 1)) << "the friendly stain 31 px off";
    EXPECT_TRUE(gate(cleric, 4, 0)) << "the classic answer";
}

// ---------------------------------------------------------------------------
// The thief
// ---------------------------------------------------------------------------

// Row 31. DROP BOMB wants a living foe inside the bomb's blast around the
// thief (31 px at level 1) and no ally in it. The classic gate bombs a foe
// within 35 px or 130 px and more away.
//
// RED (the classic gate): gate(thief, 1, 1) is true with the foe 32 px and
// 140 px away, and with the ally in the blast.
TEST_F(GameplayFixesBots, thief_bomb_wants_a_foe_inside_its_blast_and_no_ally)
{
    living* thief = body(add(FAMILY_THIEF, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 0, 230, 200, 1), 16, 16);  // 30 px
    ASSERT_TRUE(thief && foe);
    thief->set_foe(foe);

    EXPECT_TRUE(gate(thief, 1, 0)) << "the classic answer: within 35 px";
    thief->set_shifter_down(1);
    EXPECT_TRUE(gate(thief, 1, 1)) << "a foe 30 px off: inside the 31 px blast";
    EXPECT_EQ(0, thief->shifter_down()) << "the bomb is Shift 0";

    foe->setxy(231, 200);
    EXPECT_TRUE(gate(thief, 1, 1)) << "31 px: the edge is in";
    foe->setxy(232, 200);
    EXPECT_TRUE(gate(thief, 1, 0)) << "the classic answer: within 35 px";
    EXPECT_FALSE(gate(thief, 1, 1)) << "32 px: outside the blast";

    foe->setxy(340, 200);  // 140 px
    EXPECT_TRUE(gate(thief, 1, 0)) << "the classic answer: 130 px or more";
    EXPECT_FALSE(gate(thief, 1, 1)) << "140 px: the bomb reaches nobody";

    foe->setxy(230, 200);
    living* ally = body(add(FAMILY_ORC, 1, 180, 200, 1), 16, 16);  // 20 px the other side
    ASSERT_NE(nullptr, ally);
    EXPECT_TRUE(gate(thief, 1, 0)) << "the classic answer";
    EXPECT_FALSE(gate(thief, 1, 1)) << "an ally inside the blast";
}

// Row 32. MINE (New Specials): a level-2 thief with 94 MP lays a mine where
// a foe will walk: a foe past the 31 px blast, within 130 px, hunting the
// thief; fewer than three mines down, no ally in the blast and the 70 in
// hand. Never the coin.
//
// RED (the coin): with New Specials on the gate leaves the coin's Shift 0
// for the hunter at 80 px, and answers false (the classic window).
TEST_F(GameplayFixesBots, thief_mine_waits_for_a_hunter_past_the_blast)
{
    living* thief = body(add(FAMILY_THIEF, 1, 200, 200, 2, 94.0f), 16, 16);
    living* hunter = body(add(FAMILY_SOLDIER, 0, 280, 200, 1), 16, 16);  // 80 px
    ASSERT_TRUE(thief && hunter);
    thief->set_foe(hunter);
    hunter->set_foe(thief);

    world().gameplay_fixes = 0;
    EXPECT_FALSE(kit_gate(thief, 1, 0)) << "the classic answer: a foe at 80 px is refused";

    world().gameplay_fixes = 1;
    for (const short coin : {short{0}, short{1}}) {
        thief->set_shifter_down(coin);
        EXPECT_TRUE(kit_gate(thief, 1)) << "a hunter at 80 px, coin " << coin;
        EXPECT_EQ(1, thief->shifter_down()) << "MINE, whatever the coin said";
    }
    world().gameplay_fixes = 0;
    thief->set_shifter_down(0);
    EXPECT_TRUE(kit_gate(thief, 1)) << "the MINE rule is New Specials', the setting off";
    EXPECT_EQ(1, thief->shifter_down());
    world().gameplay_fixes = 1;

    hunter->set_foe(nullptr);
    thief->set_shifter_down(1);
    EXPECT_FALSE(kit_gate(thief, 1)) << "nobody hunting the thief, and the bomb reaches nobody";
    EXPECT_EQ(0, thief->shifter_down());
    hunter->set_foe(thief);

    hunter->setxy(220, 200);  // 20 px: inside the blast
    EXPECT_TRUE(kit_gate(thief, 1)) << "the hunter inside the blast: the bomb";
    EXPECT_EQ(0, thief->shifter_down());
    hunter->setxy(280, 200);

    for (int i = 0; i < 3; ++i)
        ASSERT_NE(nullptr, owned_fx(world(), mine_family(), thief,
                                    static_cast<short>(400 + 20 * i), 400));
    EXPECT_FALSE(kit_gate(thief, 1)) << "three mines down";
    EXPECT_EQ(0, thief->shifter_down());
    for (auto& ob : world().oblist)
        if (ob && ob->query_order() == Order::FX && ob->family() == mine_family())
            ob->set_dead(1);
    EXPECT_TRUE(kit_gate(thief, 1)) << "the mines are spent";
    EXPECT_EQ(1, thief->shifter_down());

    thief->stats()->set_magicpoints(60.0f);
    EXPECT_FALSE(kit_gate(thief, 1)) << "60 MP: the mine's 70 not in hand";
    EXPECT_EQ(0, thief->shifter_down());
    thief->stats()->set_magicpoints(94.0f);

    ASSERT_NE(nullptr, body(add(FAMILY_ORC, 1, 210, 200, 1), 16, 16));
    EXPECT_FALSE(kit_gate(thief, 1)) << "an ally beside the thief";
    EXPECT_EQ(0, thief->shifter_down());
}

// Row 33. CLOAK wants a living foe within 130 px hunting the thief and no
// invisibility left. The classic slot-2 gate is yes.
//
// RED (the classic answer): gate(thief, 2, 1) is true while cloaked.
TEST_F(GameplayFixesBots, thief_cloak_wants_a_hunter_and_no_cloak_left)
{
    living* thief = body(add(FAMILY_THIEF, 1, 200, 200, 7, 200.0f), 16, 16);
    living* hunter = body(add(FAMILY_SOLDIER, 0, 300, 200, 1), 16, 16);  // 100 px
    ASSERT_TRUE(thief && hunter);
    hunter->set_foe(thief);

    EXPECT_TRUE(gate(thief, 2, 0)) << "the classic answer";
    EXPECT_TRUE(gate(thief, 2, 1)) << "a hunter at 100 px";

    thief->set_invisibility_left(5);
    EXPECT_FALSE(gate(thief, 2, 1)) << "still cloaked";
    EXPECT_TRUE(gate(thief, 2, 0)) << "the classic answer";
    thief->set_invisibility_left(0);

    living* other = body(add(FAMILY_ORC, 1, 400, 400, 1), 16, 16);
    ASSERT_NE(nullptr, other);
    hunter->set_foe(other);
    EXPECT_FALSE(gate(thief, 2, 1)) << "the soldier hunts someone else";
    hunter->set_foe(thief);
    hunter->setxy(331, 200);  // 131 px
    EXPECT_FALSE(gate(thief, 2, 1)) << "the hunter 131 px off";
    hunter->setxy(330, 200);
    EXPECT_TRUE(gate(thief, 2, 1)) << "130 px: the edge is in";

    hunter->set_dead(1);
    EXPECT_FALSE(gate(thief, 2, 1)) << "alone";
    EXPECT_TRUE(gate(thief, 2, 0)) << "the classic answer alone";
}

// Row 34. TAUNT wants a living foe and an ally other than the thief inside
// the taunt's reach (108 px at level 7). The classic gate asks for a foe in
// reach.
//
// RED (the classic gate): gate(thief, 3, 1) is true with no ally.
TEST_F(GameplayFixesBots, thief_taunt_wants_a_foe_and_an_ally_in_reach)
{
    living* thief = body(add(FAMILY_THIEF, 1, 200, 200, 7, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 0, 260, 200, 1), 16, 16);  // 60 px
    living* ally = body(add(FAMILY_ORC, 1, 110, 200, 1), 16, 16);     // 90 px
    ASSERT_TRUE(thief && foe && ally);

    thief->set_shifter_down(1);
    EXPECT_TRUE(gate(thief, 3, 1)) << "a foe at 60 px, an ally at 90";
    EXPECT_EQ(0, thief->shifter_down()) << "TAUNT: nobody to charm in 44 px";

    ally->setxy(92, 200);  // 108 px: the edge is in
    EXPECT_TRUE(gate(thief, 3, 1)) << "the ally at 108 px";
    ally->setxy(91, 200);  // 109 px
    EXPECT_FALSE(gate(thief, 3, 1)) << "the ally at 109 px: nobody backs the thief";

    ally->set_dead(1);
    thief->set_shifter_down(0);
    EXPECT_TRUE(gate(thief, 3, 0)) << "the classic answer: the foe alone";
    EXPECT_FALSE(gate(thief, 3, 1)) << "no ally";

    // A generator is no foe a taunt can pull.
    living* ally2 = body(add(FAMILY_ORC, 1, 150, 200, 1), 16, 16);
    ASSERT_NE(nullptr, ally2);
    foe->set_dead(1);
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(0);
    tent->setxy(260, 200);
    EXPECT_FALSE(gate(thief, 3, 1)) << "a tent and an ally: the taunt pulls nobody";
}

// Row 36. POISON CLOUD wants two living foes at blows with the thief and no
// cloud of the thief's alive. The classic slot-4 gate is yes.
//
// RED (the classic answer): gate(thief, 4, 1) is true with one foe at blows.
TEST_F(GameplayFixesBots, thief_poison_cloud_wants_two_foes_at_blows_and_no_cloud)
{
    living* thief = body(add(FAMILY_THIEF, 1, 200, 200, 10, 600.0f), 16, 16);
    ASSERT_NE(nullptr, thief);
    EXPECT_TRUE(gate(thief, 4, 0)) << "the classic answer alone";
    EXPECT_FALSE(gate(thief, 4, 1)) << "alone";

    living* east = body(add(FAMILY_SOLDIER, 0, 217, 200, 1), 16, 16);  // a 1 px gap
    ASSERT_NE(nullptr, east);
    EXPECT_FALSE(gate(thief, 4, 1)) << "one foe at blows";
    living* west = body(add(FAMILY_SOLDIER, 0, 183, 200, 1), 16, 16);  // a 1 px gap
    ASSERT_NE(nullptr, west);
    EXPECT_TRUE(gate(thief, 4, 1)) << "two foes at blows";
    west->setxy(178, 200);  // a 6 px gap
    EXPECT_FALSE(gate(thief, 4, 1)) << "the second a pixel out of a blow";
    west->setxy(183, 200);

    walker* cloud = owned_fx(world(), FAMILY_CLOUD, thief, 200, 200);
    ASSERT_NE(nullptr, cloud);
    EXPECT_FALSE(gate(thief, 4, 1)) << "a cloud of the thief's alive";
    cloud->set_dead(1);
    // A living slime (living family 8, the cloud's number) the thief owns is
    // not a cloud.
    living* slime = body(add(FAMILY_SLIME, 1, 400, 400, 1), 16, 16);
    ASSERT_NE(nullptr, slime);
    slime->set_owner(thief);
    EXPECT_TRUE(gate(thief, 4, 1)) << "a slime of its own is no cloud";
}
