/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Charm and seats under the Gameplay Fixes setting (issue #354).
//
// A seat must never claim a walker a thief or an archmage charmed onto its
// team: the body belongs to the other side and goes back when the charm
// runs out. The rule is two small predicates in sim_control_policy.h, asked
// by every seat scan; with the setting off (the classic game) a seat claims
// whatever the classic scans hand it.
//
// The predicates first, on hand-made walkers: a charm is told from a fresh
// spawn by its timer, because a freshly spawned walker carries
// real_team_num == team and charm_left 0 until its first act.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/walker.h>

#include <memory>

namespace {

std::unique_ptr<walker> soldier_on_team(unsigned char team,
                                        unsigned char real_team,
                                        short charm_left)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_team_num(team);
    w->set_real_team_num(real_team);
    w->set_charm_left(charm_left);
    return w;
}

}  // namespace

// charmed_onto_team: a running charm timer AND a borrowed team. A fresh
// spawn (real == team, no timer), a walker on its own team (real 255) and a
// timer whose team was already handed back are not charmed.
TEST(GameplayFixesSeats, charmed_onto_team_needs_the_timer_and_a_borrowed_team)
{
    const auto charmed = soldier_on_team(0, 1, 100);
    EXPECT_TRUE(og::sim::charmed_onto_team(*charmed))
        << "team 0 body of team 1 with 100 ticks of charm left";

    const auto last_tick = soldier_on_team(0, 1, 1);
    EXPECT_TRUE(og::sim::charmed_onto_team(*last_tick))
        << "one tick left is still a charm";

    const auto fresh_spawn = soldier_on_team(0, 0, 0);
    EXPECT_FALSE(og::sim::charmed_onto_team(*fresh_spawn))
        << "a fresh spawn carries real_team_num == team and no timer";

    const auto own_team = soldier_on_team(0, 255, 0);
    EXPECT_FALSE(og::sim::charmed_onto_team(*own_team));

    const auto timer_only = soldier_on_team(0, 255, 40);
    EXPECT_FALSE(og::sim::charmed_onto_team(*timer_only))
        << "a timer with no borrowed team is not a charm";

    const auto borrowed_no_timer = soldier_on_team(0, 1, 0);
    EXPECT_FALSE(og::sim::charmed_onto_team(*borrowed_no_timer))
        << "a borrowed team with no timer reads as a fresh spawn";
}

// seat_may_claim: the classic game (setting off) lets a seat claim anything
// the scans hand it, a charmed walker included; with the setting on a seat
// skips a charmed walker and still claims everything else.
TEST(GameplayFixesSeats, seat_may_claim_skips_a_charmed_walker_only_with_the_setting_on)
{
    GameWorld world;
    const auto charmed = soldier_on_team(0, 1, 100);
    const auto fresh_spawn = soldier_on_team(0, 0, 0);
    const auto own_team = soldier_on_team(0, 255, 0);

    ASSERT_EQ(0, world.gameplay_fixes) << "a world starts as the classic game";
    EXPECT_TRUE(og::sim::seat_may_claim(world, *charmed))
        << "the classic game claims a charmed walker";
    EXPECT_TRUE(og::sim::seat_may_claim(world, *fresh_spawn));
    EXPECT_TRUE(og::sim::seat_may_claim(world, *own_team));

    world.gameplay_fixes = 1;
    EXPECT_FALSE(og::sim::seat_may_claim(world, *charmed))
        << "with the setting on a seat never claims a charmed walker";
    EXPECT_TRUE(og::sim::seat_may_claim(world, *fresh_spawn))
        << "a fresh spawn is still claimable";
    EXPECT_TRUE(og::sim::seat_may_claim(world, *own_team));
}
