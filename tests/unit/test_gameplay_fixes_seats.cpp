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
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <array>
#include <cstdint>
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

// ---------------------------------------------------------------------------
// The seat scans. Every scan a seat claims a body through asks
// seat_may_claim: both passes of sim_find_next_control, the owner-tagged
// pass and both owner-locked passes of sim_find_next_control_owned, and so
// sim_reacquire_control's verdict, which re-runs the classic scan.

namespace {

using og::sim::SimReacquire;

struct SeatWorld
{
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    SeatWorld()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &events, &cfg);
    }

    GameWorld& world() { return level.world(); }

    struct Spec
    {
        unsigned char team = 0;
        signed char user = -1;
        bool hero = false;  // attach a myguy
        std::uint8_t owner = guy::kNoOwner;
        unsigned char real_team = 255;
        short charm_left = 0;
    };

    walker* add(const Spec& spec)
    {
        auto w = std::make_unique<walker>();
        w->set_order_family(Order::Living, FAMILY_SOLDIER);
        w->setxy(80, 80);
        w->set_sizex(16);
        w->set_sizey(16);
        w->set_stepsize(1.0f);
        w->set_team_num(spec.team);
        w->set_real_team_num(spec.real_team);
        w->set_charm_left(spec.charm_left);
        w->set_user(spec.user);
        if (spec.hero)
        {
            w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
            w->myguy->owner_player_index = spec.owner;
        }
        walker* out = w.get();
        world().oblist.push_back(std::move(w));
        return out;
    }

    // Owner-locked with every seat on one deployed machine: ownership
    // permits everything, so the owner-locked passes decide alone.
    void lock_permissively()
    {
        std::array<std::uint8_t, og::sim::kPlayerMachineSlots> machines{};
        machines.fill(og::sim::encode_player_machine(0, true));
        og::sim::set_control_policy(world(), og::sim::kControlPolicyOwnerLocked,
                                    machines);
    }
};

}  // namespace

// A team-0 soldier of team 1 that our side charmed (100 ticks left), with
// every other team-0 body already claimed. The classic game hands a seat the
// charmed walker; with the setting on there is nobody to claim. The same for
// a charmed hero (the scan's first pass) beside an unclaimed troop: the
// setting on skips the hero and claims the troop.
// RED on the scans without the conjunct: the charmed walker is returned.
TEST(GameplayFixesSeats, a_seat_skips_a_walker_our_side_charmed)
{
    SeatWorld fx;
    fx.add({.team = 0, .user = 0, .hero = true, .owner = 0});
    walker* charmed = fx.add({.team = 0, .real_team = 1, .charm_left = 100});

    fx.world().gameplay_fixes = 0;
    EXPECT_EQ(charmed, sim_find_next_control(fx.world(), 0))
        << "the classic game claims the charmed walker";
    EXPECT_EQ(charmed, og::sim::sim_find_next_control_owned(fx.world(), 0, 0));
    fx.world().gameplay_fixes = 1;
    EXPECT_EQ(nullptr, sim_find_next_control(fx.world(), 0))
        << "with the setting on a seat never claims a charmed walker";
    EXPECT_EQ(nullptr, og::sim::sim_find_next_control_owned(fx.world(), 0, 0));

    // The first pass: a charmed hero ahead of an unclaimed troop.
    fx.world().oblist.clear();
    walker* charmed_hero = fx.add({.team = 0, .hero = true, .owner = 0,
                                   .real_team = 1, .charm_left = 100});
    walker* troop = fx.add({.team = 0});
    fx.world().gameplay_fixes = 0;
    EXPECT_EQ(charmed_hero, sim_find_next_control(fx.world(), 0))
        << "the classic first pass takes the hero";
    fx.world().gameplay_fixes = 1;
    EXPECT_EQ(troop, sim_find_next_control(fx.world(), 0))
        << "with the setting on the first pass skips the charmed hero";
}

// The reacquire verdict follows the scans: with only a charmed walker left
// on the team the classic game claims it, and with the setting on the game
// ends instead (no seat takes a borrowed body). Owner-locked, the same:
// the owner-locked passes and the classic scan both skip it, so the verdict
// is EndGame, not Follow.
// RED: Claimed with the setting on (no conjunct anywhere), or Follow
// (the conjunct in the owner-locked passes but not in the classic scan).
TEST(GameplayFixesSeats, reacquire_ends_the_game_rather_than_claim_a_charmed_walker)
{
    for (const bool locked : {false, true})
    {
        SeatWorld fx;
        if (locked)
            fx.lock_permissively();
        fx.add({.team = 0, .user = 0, .hero = true, .owner = 0});
        walker* charmed = fx.add({.team = 0, .real_team = 1, .charm_left = 100});

        walker* control = nullptr;
        fx.world().gameplay_fixes = 0;
        EXPECT_EQ(SimReacquire::Claimed,
                  og::sim::sim_reacquire_control(fx.world(), 0, 1, control))
            << "locked " << locked;
        EXPECT_EQ(charmed, control) << "locked " << locked;

        control = nullptr;
        fx.world().gameplay_fixes = 1;
        EXPECT_EQ(SimReacquire::EndGame,
                  og::sim::sim_reacquire_control(fx.world(), 0, 1, control))
            << "locked " << locked;
        EXPECT_EQ(nullptr, control) << "locked " << locked;
    }
}

// A freshly spawned walker carries real_team_num == team and no charm timer
// (the parity rows, the text client's crew, mapgen). It is claimable on
// both values of the setting.
// RED with the timer dropped from charmed_onto_team: nullptr with the
// setting on.
TEST(GameplayFixesSeats, a_fresh_spawn_with_real_team_set_is_still_claimable)
{
    SeatWorld fx;
    walker* fresh = fx.add({.team = 0, .hero = true, .owner = 0, .real_team = 0});
    for (const short fixes : {short{0}, short{1}})
    {
        fx.world().gameplay_fixes = fixes;
        EXPECT_EQ(fresh, sim_find_next_control(fx.world(), 0)) << fixes;
        walker* control = nullptr;
        EXPECT_EQ(SimReacquire::Claimed,
                  og::sim::sim_reacquire_control(fx.world(), 0, 0, control))
            << fixes;
        EXPECT_EQ(fresh, control) << fixes;
    }
}

// In a scripted match the owner-tagged pass claims a seat's own hero
// whatever its team. Our hero, charmed by an enemy, stands on the enemy
// team (team 1, real 0, 50 ticks left): the classic game hands it back to
// its seat while it is still the enemy's; with the setting on the seat
// finds nobody (the classic scan has no team-0 body either).
// RED on the owner-tagged pass without the conjunct: the charmed hero.
TEST(GameplayFixesSeats, owner_tagged_pass_skips_a_charmed_hero)
{
    SeatWorld fx;
    fx.world().type =
        static_cast<char>(fx.world().type | GameWorld::TYPE_SCRIPTED);
    walker* hero = fx.add({.team = 1, .hero = true, .owner = 0,
                           .real_team = 0, .charm_left = 50});
    fx.add({.team = 1});  // the enemy's own troop

    fx.world().gameplay_fixes = 0;
    EXPECT_EQ(hero, og::sim::sim_find_next_control_owned(fx.world(), 0, 0))
        << "the classic owner-tagged pass takes the charmed hero";
    fx.world().gameplay_fixes = 1;
    EXPECT_EQ(nullptr, og::sim::sim_find_next_control_owned(fx.world(), 0, 0))
        << "with the setting on the seat does not take it back early";
}
