/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The cleric bot's shift with the Gameplay Fixes setting.
//
// In the classic game a bot cleric that rolled RAISE UNDEAD or RAISE GHOST
// tosses a coin (living::check_special) to pick the raise or its alternate,
// TURN UNDEAD, so half its turns go off at a ring with nothing undead in it
// (20 MP, the sound, nobody turned). With the setting on the coin is still
// drawn, and the cleric's gate then picks on purpose: TURN UNDEAD only with
// an undead foe it could turn inside its reach (walker:undead_foes_in_range,
// the same question the cast asks), else the raise. The gate's answer stays
// "yes" for those slots on both values.
//
// The bot gates (test_bot_gates_casters.cpp) then ask each slot for its
// payoff with the setting on: a raise wants a corpse the raise can find and
// a living foe within 130 px, RESURRECT a friendly corpse, the mace a foe
// within its orbit. The cases here that expect a raise or RESURRECT stage
// that corpse (add_corpse) beside the cleric.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

living* add_living(TestGameWorld& tw, int family, unsigned char team, int x,
                   int y, short level)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(static_cast<short>(x), static_cast<short>(y));
    w->stats()->set_level(level);
    return dynamic_cast<living*>(w);
}

// A level-4 bot cleric at (100, 100): the classic reach is 4 * 4 = 16 px,
// the reach with the setting on is 60 px.
living* add_cleric(TestGameWorld& tw)
{
    living* cleric = add_living(tw, FAMILY_CLERIC, 1, 100, 100, 4);
    if (cleric != nullptr) {
        cleric->set_act_type(ACT_RANDOM);
        cleric->stats()->set_max_magicpoints(500.0f);
        cleric->stats()->set_magicpoints(500.0f);
    }
    return cleric;
}

// A bloodstain of `team` 6 px below the cleric's corner: the corpse
// find_nearest_blood answers (it never answers beyond about 28 px of
// centre), inside every raise's reach and RESURRECT's.
walker* add_corpse(TestGameWorld& tw, unsigned char team)
{
    walker* s = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (s == nullptr)
        return nullptr;
    s->set_team_num(team);
    s->setxy(100, 106);
    return s;
}

// The shift the cleric's gate leaves for `slot`, starting from `preset`
// (the coin's side), and the gate's answer.
struct GateOutcome {
    bool answer = false;
    int shift = -1;
};

GateOutcome gate(living* cleric, int slot, short preset)
{
    const FamilyDescriptor& fd = *get_family_descriptor(FAMILY_CLERIC);
    cleric->set_current_special(static_cast<char>(slot));
    cleric->set_shifter_down(preset);
    GateOutcome out;
    out.answer = og::test::check_special_ai(fd, cleric);
    out.shift = static_cast<int>(cleric->shifter_down());
    return out;
}

// The first world-RNG state from which the next draw of next(bound) is
// `want`: pins the bot's coin without a spy (the sim calls the LCG inline).
std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        if (probe.next(bound) == want)
            return seed;
    }
}

}  // namespace

// A skeleton 50 px away: outside the classic reach (16), inside the fixed
// one (60). The classic gate keeps whatever side the coin landed on; with the
// setting on the cleric turns, both raise slots, whatever the coin said.
// RED on the classic gate: with the setting on the shift stays 0.
TEST(GameplayFixesClericBot, turns_with_an_undead_foe_in_reach)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        living* skeleton = add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1);
        ASSERT_NE(nullptr, skeleton);
        ASSERT_EQ(50, static_cast<int>(cleric->distance_to_ob(skeleton)));

        for (const int slot : {2, 3}) {
            const GateOutcome g = gate(cleric, slot, 0);
            EXPECT_TRUE(g.answer) << "slot " << slot << ", setting " << fixes;
            EXPECT_EQ(fixes != 0 ? 1 : 0, g.shift)
                << "slot " << slot << ": the coin said raise; setting "
                << fixes;
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Only living foes in reach (an orc 50 px away, a generator 10 px away), or
// an undead that cannot be turned (a skeleton under an invulnerability
// potion): there is nothing to turn, so with the setting on the cleric
// raises even when the coin said turn. The classic gate keeps the coin.
// RED on the classic gate: with the setting on the shift stays 1.
TEST(GameplayFixesClericBot, raises_with_nothing_to_turn_in_reach)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_ORC, 2, 150, 100, 1));
        ASSERT_NE(nullptr, add_corpse(tw, 2));  // the raise's corpse
        walker* tower = tw.world().add_ob(Order::Generator, FAMILY_TOWER);
        ASSERT_NE(nullptr, tower);
        tower->set_team_num(2);
        tower->setxy(110, 100);
        living* warded = add_living(tw, FAMILY_SKELETON, 2, 100, 110, 1);
        ASSERT_NE(nullptr, warded);
        warded->set_invulnerable_left(50);

        for (const int slot : {2, 3}) {
            const GateOutcome g = gate(cleric, slot, 1);
            EXPECT_TRUE(g.answer) << "slot " << slot << ", setting " << fixes;
            EXPECT_EQ(fixes != 0 ? 0 : 1, g.shift)
                << "slot " << slot << ": the coin said turn; setting "
                << fixes;
        }

        // Once the ward is gone the skeleton can be turned, and is chosen.
        warded->set_invulnerable_left(0);
        const GateOutcome g = gate(cleric, 2, 0);
        EXPECT_EQ(fixes != 0 ? 1 : 0, g.shift)
            << "the unwarded skeleton, setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 4 (RESURRECT) has no alternate: the turn arm never touches its
// shift. Slot 1 with no friend in reach and the pool full maces on both
// values: the classic rule at half mana, and with the setting on because the
// skeleton stands 30 px off, inside the mace's 48 px orbit. A friendly
// corpse beside the cleric is RESURRECT's payoff with the setting on.
// RED under the mutation "the fixed arm runs for every slot": slot 4's
// shift flips to 1 with the skeleton in reach.
TEST(GameplayFixesClericBot, leaves_the_resurrect_shift_and_maces_a_foe_in_orbit)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 130, 100, 1));
        ASSERT_NE(nullptr, add_corpse(tw, 1));  // a fallen friend

        const GateOutcome resurrect = gate(cleric, 4, 0);
        EXPECT_TRUE(resurrect.answer);
        EXPECT_EQ(0, resurrect.shift) << "slot 4, setting " << fixes;

        // No friend in reach, full mana: the mace.
        const GateOutcome mace = gate(cleric, 1, 0);
        EXPECT_TRUE(mace.answer);
        EXPECT_EQ(1, mace.shift) << "slot 1 with no friend, setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Through living::check_special, the bot's own door: the coin
// (rng_.next(2)) is drawn on both values, so the world's stream is where the
// classic game leaves it, and with the setting on the gate's choice
// overrides the coin afterwards.
// RED on the classic gate: with the setting on the coin's 0 stays.
TEST(GameplayFixesClericBot, the_coin_is_drawn_and_then_overridden)
{
    og::test::ScopedHookFailureGuard guard;
    const std::uint32_t start = state_where_next_is(2, 0);
    og::sim::SimRandom one_coin(start);
    (void)one_coin.next(2);

    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1));
        cleric->set_current_special(2);
        cleric->set_shifter_down(1);
        tw.world().rng_.state_ = start;
        EXPECT_TRUE(cleric->check_special()) << "setting " << fixes;
        EXPECT_EQ(one_coin.state_, tw.world().rng_.state_)
            << "one coin and nothing else, setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 1 : 0, static_cast<int>(cleric->shifter_down()))
            << "the coin said raise; setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A cleric whose hero has less than the 60 INT a TURN UNDEAD needs: the cast
// refuses it ("60 INT REQUIRED", 5 busy), so with the setting on the gate
// asks the same question and raises instead, even with a skeleton in reach
// and the coin on turn. With 60 INT it turns. The classic gate keeps the coin
// on both INT values.
// RED on the gate without the INT question: with the setting on and 40 INT
// the shift stays 1.
TEST(GameplayFixesClericBot, raises_when_its_hero_lacks_the_int_to_turn)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        for (const short intelligence : {short{40}, short{60}}) {
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            living* cleric = add_cleric(tw);
            ASSERT_NE(nullptr, cleric);
            cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
            ASSERT_NE(nullptr, cleric->myguy);
            cleric->myguy->intelligence = intelligence;
            ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1));
            ASSERT_NE(nullptr, add_corpse(tw, 2));  // the raise's corpse

            const int want = (fixes != 0 && intelligence < 60) ? 0 : 1;
            for (const int slot : {2, 3}) {
                const GateOutcome g = gate(cleric, slot, 1);
                EXPECT_TRUE(g.answer)
                    << "slot " << slot << ", INT " << intelligence
                    << ", setting " << fixes;
                EXPECT_EQ(want, g.shift)
                    << "slot " << slot << ": the coin said turn; INT "
                    << intelligence << ", setting " << fixes;
            }
        }
    }

    // The cast asks the same question and still refuses.
    {
        TestGameWorld tw;
        tw.world().gameplay_fixes = 1;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
        ASSERT_NE(nullptr, cleric->myguy);
        cleric->myguy->intelligence = 40;
        ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1));

        const FamilyDescriptor& desc = *get_family_descriptor(FAMILY_CLERIC);
        cleric->set_current_special(2);
        cleric->set_shifter_down(1);
        cleric->set_busy(0);
        const auto turn = og::script::hooks::do_special(&desc, cleric);
        ASSERT_TRUE(turn.has_value());
        EXPECT_FALSE(turn->succeeded());
        EXPECT_EQ(std::string("60 INT REQUIRED"), turn->reason());
        EXPECT_FLOAT_EQ(5.0f, cleric->busy());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
