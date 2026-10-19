/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The thief bot's shift with the Gameplay Fixes setting.
//
// In the classic game a bot thief that rolled TAUNT ENEMY tosses a coin
// (living::check_special) to pick the taunt or its alternate, CHARM
// OPPONENT, and the gate then only asks whether some foe stands inside the
// chosen reach. So the bot charms foes that cannot be charmed (a higher
// level always resists and hits back for free) and gives up a taunt it
// could have made because the coin picked the short charm reach. With the
// setting on the coin is still drawn, and the gate picks on purpose: CHARM
// only when a foe the thief can take (never charmed, at or below its level,
// not a hero a player drives) stands inside charm reach, else TAUNT. The
// classic lines then measure the reach of what was picked.
//
// A level-7 thief: charm reach 16 + 4 * 7 = 44 px, taunt reach
// 80 + 4 * 7 = 108 px.
//
// With the setting on a bot taunts only with an ally inside the taunt's
// reach too (foes pulled onto a thief nobody backs are the thief's own
// danger): the taunting cases stand a level-1 orc of the thief's team
// 60 px west of it (add_backer), the payoff a taunt is cast for.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

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

// The thief's ally, 60 px west: inside the taunt's 108 px, outside the
// charm's 44 (a charm counts foes only).
living* add_backer(TestGameWorld& tw)
{
    return add_living(tw, FAMILY_ORC, 1, 40, 100, 1);
}

living* add_thief(TestGameWorld& tw)
{
    living* thief = add_living(tw, FAMILY_THIEF, 1, 100, 100, 7);
    if (thief != nullptr)
        thief->set_act_type(ACT_RANDOM);
    return thief;
}

struct GateOutcome {
    bool answer = false;
    int shift = -1;
};

// The thief's gate on slot 3 from the coin's side `preset`.
GateOutcome gate(living* thief, short preset)
{
    const FamilyDescriptor& fd = *get_family_descriptor(FAMILY_THIEF);
    thief->set_current_special(3);
    thief->set_shifter_down(preset);
    GateOutcome out;
    out.answer = og::test::check_special_ai(fd, thief);
    out.shift = static_cast<int>(thief->shifter_down());
    return out;
}

}  // namespace

// A level-9 foe 30 px away: inside charm reach, but a higher level always
// resists. The classic gate keeps the coin's charm; with the setting on the
// thief taunts it instead (and a foe 30 px away is inside taunt reach).
// RED on the classic gate: with the setting on the shift stays 1.
TEST(GameplayFixesThiefBot, taunts_a_foe_it_cannot_charm)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* thief = add_thief(tw);
        ASSERT_NE(nullptr, thief);
        ASSERT_NE(nullptr, add_backer(tw));
        ASSERT_NE(nullptr, add_living(tw, FAMILY_ORC, 2, 130, 100, 9));

        const GateOutcome g = gate(thief, 1);
        EXPECT_TRUE(g.answer) << "setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 0 : 1, g.shift)
            << "a higher-level foe, the coin said charm; setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// An equal-level foe 30 px away can be charmed: with the setting on the
// thief charms even when the coin said taunt. The classic gate keeps the
// coin's taunt.
// RED on the classic gate: with the setting on the shift stays 0.
TEST(GameplayFixesThiefBot, charms_an_equal_level_foe_in_reach)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* thief = add_thief(tw);
        ASSERT_NE(nullptr, thief);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_ORC, 2, 130, 100, 7));

        const GateOutcome g = gate(thief, 0);
        EXPECT_TRUE(g.answer) << "setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 1 : 0, g.shift)
            << "an equal-level foe, the coin said taunt; setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A hero a player drives (a user tag and ACT_CONTROL) and a foe some charm
// already holds are not charmable: with the setting on the thief taunts.
// RED on the classic gate: with the setting on the shift stays 1.
TEST(GameplayFixesThiefBot, taunts_a_seated_hero_and_a_charmed_foe)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* thief = add_thief(tw);
        ASSERT_NE(nullptr, thief);
        ASSERT_NE(nullptr, add_backer(tw));
        living* hero = add_living(tw, FAMILY_SOLDIER, 2, 130, 100, 1);
        ASSERT_NE(nullptr, hero);
        hero->set_user(0);
        hero->set_act_type(ACT_CONTROL);
        living* charmed = add_living(tw, FAMILY_ORC, 2, 100, 130, 1);
        ASSERT_NE(nullptr, charmed);
        charmed->set_real_team_num(0);  // a charm took it from team 0

        const GateOutcome g = gate(thief, 1);
        EXPECT_TRUE(g.answer) << "setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 0 : 1, g.shift)
            << "nobody the thief could take; setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A foe 80 px away: outside charm reach, inside taunt reach. The classic
// gate measures the charm reach the coin picked and says no; with the
// setting on the thief taunts, and the taunt reaches it. With nobody inside
// either reach, the gate says no on both values.
// RED on the classic gate: with the setting on the answer is false.
TEST(GameplayFixesThiefBot, taunts_a_foe_beyond_charm_reach)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* thief = add_thief(tw);
        ASSERT_NE(nullptr, thief);
        ASSERT_NE(nullptr, add_backer(tw));
        living* foe = add_living(tw, FAMILY_ORC, 2, 180, 100, 1);
        ASSERT_NE(nullptr, foe);
        ASSERT_EQ(80, static_cast<int>(thief->distance_to_ob(foe)));

        const GateOutcome g = gate(thief, 1);
        EXPECT_EQ(fixes != 0, g.answer)
            << "a foe in taunt reach only; setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 0 : 1, g.shift) << "setting " << fixes;

        foe->setxy(260, 100);  // 160 px: outside both reaches
        for (const short preset : {short{0}, short{1}}) {
            const GateOutcome far = gate(thief, preset);
            EXPECT_FALSE(far.answer)
                << "nobody in reach; coin " << preset << ", setting " << fixes;
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
