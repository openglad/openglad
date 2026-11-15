/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Charm and seats under the Gameplay Fixes setting (issues #354, #450 and
// #496).
//
// A seat must never claim a walker a thief or an archmage charmed onto its
// team: the body belongs to the other side and goes back when the charm
// runs out. The rule is two small predicates in sim_control_policy.h, asked
// by every seat scan; with the setting off (the classic game) a seat claims
// whatever the classic scans hand it. A seat that already drives a charmed
// walker keeps it when the charm runs out, on both settings (#450, the
// maintainer's ruling: refuse a charmed walker for the hand-off, keep one
// already controlled).
//
// A summon of our colour (#496): a faerie an enemy druid summoned while our
// side had the druid charmed keeps our colour when the charm runs out. With
// the setting on every walker fights for the colour it wears (Gameplay Fixes
// AZ), so the faerie fights for our side: the hand-off may take it and Switch
// Character may switch to it. Classic, it fights for its druid's side:
// Switch Character refuses it (Jonathan Dearborn's 2013 test asks whose side
// a walker fights for) while the hand-off, which asks only the colour, takes
// it. A seat already on such a walker keeps it.
//
// The predicates first, on hand-made walkers: a charm is told from a fresh
// spawn by its timer, because a freshly spawned walker carries
// real_team_num == team and charm_left 0 until its first act. Then the
// scans, then the hand-off a dead hero's seat takes through its own tick.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "test_gameplay_context_scope.h"

#include <openglad/core/pixdefs.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

// ---------------------------------------------------------------------------
// The hand-off through the seat's own tick (#450). When the walker a seat
// drives dies, sim_process_player_input asks the reacquire verdict, which
// runs the scans above; the seat takes what they hand it, or the game ends.

namespace {

// One tick of player 0's seat on team 0 with nothing pressed.
SimInputResult seat_tick(GameWorld& world, og::sim::SimEventLog& events,
                         walker*& control, SimInputDebounce& debounce)
{
    InputState input;
    input.clear();
    return sim_process_player_input(input.players[0], control, world, 0, 0,
                                    debounce, &events);
}

// Player 0's hero, dead, with the seat's tag still on it.
walker* add_dead_hero(SeatWorld& fx)
{
    walker* hero = fx.add({.team = 0, .user = 0, .hero = true, .owner = 0});
    hero->set_dead(1);
    return hero;
}

// A team-1 soldier our side charmed onto team 0, 100 ticks left. It has no
// character record, so the hand-off meets it in its second pass.
walker* add_charmed_foe(SeatWorld& fx)
{
    return fx.add({.team = 0, .real_team = 1, .charm_left = 100});
}

}  // namespace

// Player 0's hero lies dead; the only other body on team 0 is a foe our side
// charmed, with 41 hit points. The classic game hands the seat the foe and
// shows its 41 hit points; with the setting on there is nobody to hand over
// and the game ends (end-game type 1), the foe left untouched. The legacy
// scan and the owner-locked one (every seat on one deployed machine) answer
// alike.
// RED with the charm test dropped from the second pass of
// sim_find_next_control: with the setting on the seat takes the foe (legacy)
// or follows instead of ending the game (owner-locked). RED with it dropped
// from the owner-locked second pass: owner-locked takes the foe.
TEST(GameplayFixesSeats, a_dead_heros_seat_takes_a_charmed_foe_only_with_the_setting_off)
{
    for (const bool locked : {false, true})
    {
        for (const short fixes : {short{0}, short{1}})
        {
            SeatWorld fx;
            if (locked)
                fx.lock_permissively();
            fx.world().gameplay_fixes = fixes;
            walker* hero = add_dead_hero(fx);
            walker* foe = add_charmed_foe(fx);
            foe->stats()->set_hitpoints(41.0f);
            const int foe_act = foe->act_type();

            walker* control = hero;
            SimInputDebounce debounce{};
            const SimInputResult result =
                seat_tick(fx.world(), fx.events, control, debounce);

            if (fixes == 0)
            {
                EXPECT_EQ(foe, control)
                    << "locked " << locked << ": the classic game hands over the charmed foe";
                EXPECT_EQ(0, foe->user()) << "locked " << locked;
                EXPECT_EQ(ACT_CONTROL, foe->act_type()) << "locked " << locked;
                EXPECT_FALSE(result.endgame_requested) << "locked " << locked;
                EXPECT_TRUE(result.control_hp_changed) << "locked " << locked;
                EXPECT_EQ(41.0f, result.control_hp) << "locked " << locked;
            }
            else
            {
                EXPECT_EQ(nullptr, control)
                    << "locked " << locked << ": with the setting on nobody is handed over";
                EXPECT_EQ(-1, foe->user()) << "locked " << locked;
                EXPECT_EQ(foe_act, foe->act_type()) << "locked " << locked;
                EXPECT_TRUE(result.endgame_requested)
                    << "locked " << locked << ": the game ends rather than follow";
                EXPECT_EQ(1, result.endgame_type) << "locked " << locked;
                EXPECT_FALSE(result.control_hp_changed) << "locked " << locked;
            }
        }
    }
}

// The same dead hero, with a charmed foe and a real teammate (team 0, no
// borrowed team) both on team 0, in both orders of the world's list. The
// real teammate is handed over on both settings; the charmed foe only by the
// classic game, and only when it stands first. Legacy and owner-locked alike.
// RED with the charm test dropped from either second pass: with the setting
// on and the foe first, the seat takes the foe.
TEST(GameplayFixesSeats, a_dead_heros_seat_takes_a_real_teammate_on_both_settings)
{
    for (const bool locked : {false, true})
    {
        for (const bool foe_first : {true, false})
        {
            for (const short fixes : {short{0}, short{1}})
            {
                SeatWorld fx;
                if (locked)
                    fx.lock_permissively();
                fx.world().gameplay_fixes = fixes;
                walker* hero = add_dead_hero(fx);
                walker* foe = nullptr;
                walker* mate = nullptr;
                if (foe_first)
                {
                    foe = add_charmed_foe(fx);
                    mate = fx.add({.team = 0});
                }
                else
                {
                    mate = fx.add({.team = 0});
                    foe = add_charmed_foe(fx);
                }

                walker* control = hero;
                SimInputDebounce debounce{};
                const SimInputResult result =
                    seat_tick(fx.world(), fx.events, control, debounce);

                const bool takes_foe = fixes == 0 && foe_first;
                walker* const taken = takes_foe ? foe : mate;
                walker* const left = takes_foe ? mate : foe;
                const auto where = [&] {
                    return std::string("locked ") + (locked ? "1" : "0") +
                        (foe_first ? ", foe first" : ", teammate first") +
                        ", setting " + (fixes != 0 ? "on" : "off");
                };
                EXPECT_EQ(taken, control) << where();
                EXPECT_EQ(0, taken->user()) << where();
                EXPECT_EQ(ACT_CONTROL, taken->act_type()) << where();
                EXPECT_EQ(-1, left->user()) << where();
                EXPECT_FALSE(result.endgame_requested) << where();
            }
        }
    }
}

// A seat on a walker our side charmed keeps it when the charm runs out
// (#450). Team 0's unclaimed hero stands first in the world's list, so a
// seat that looked for a new body would take it. The charm runs down on the
// walker's own act: 2, then 1, then 0 with the walker back on team 1 and no
// team recorded. The seat looks for a new body only when it has none or its
// body is dead, so it stays on the walker, now team 1's, on both settings.
// RED with a seat that also looks when its body's team is no longer its
// own: once the charm is out the seat jumps to the hero, on both settings.
TEST(GameplayFixesSeats, a_seat_keeps_its_walker_when_the_charm_runs_out)
{
    for (const short fixes : {short{0}, short{1}})
    {
        og::test::mount_core_pack();
        TestGameWorld tw;
        GameWorld& world = tw.world();
        world.resize_grid(32, 32);
        std::fill_n(world.grid.data.get(),
                    static_cast<std::size_t>(world.grid.w) * world.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        world.my_team = 0;
        world.gameplay_fixes = fixes;

        walker* hero = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, hero);
        hero->set_team_num(0);
        hero->set_real_team_num(255);
        hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        hero->myguy->owner_player_index = 0;
        hero->setxy(64, 64);

        walker* borrowed = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, borrowed);
        borrowed->set_team_num(0);
        borrowed->set_real_team_num(1);
        borrowed->set_charm_left(2);
        borrowed->setxy(256, 256);

        ASSERT_TRUE(sim_claim_control(world, *borrowed, 0)) << fixes;
        walker* control = borrowed;
        SimInputDebounce debounce{};

        // One act: the charm holds (1 left), the walker is still team 0's.
        borrowed->act();
        ASSERT_EQ(1, borrowed->charm_left()) << fixes;
        ASSERT_EQ(0, borrowed->team_num()) << fixes;
        SimInputResult result = seat_tick(world, tw.events, control, debounce);
        EXPECT_EQ(borrowed, control) << fixes;
        EXPECT_FALSE(result.endgame_requested) << fixes;

        // The next act: the charm is out, the walker is back on team 1.
        borrowed->act();
        ASSERT_EQ(0, borrowed->charm_left()) << fixes;
        ASSERT_EQ(1, borrowed->team_num()) << fixes;
        ASSERT_EQ(255, borrowed->real_team_num()) << fixes;
        result = seat_tick(world, tw.events, control, debounce);
        EXPECT_EQ(borrowed, control)
            << "setting " << fixes << ": the seat keeps its walker after the charm";
        EXPECT_EQ(0, borrowed->user()) << fixes;
        EXPECT_EQ(ACT_CONTROL, borrowed->act_type()) << fixes;
        EXPECT_EQ(-1, hero->user()) << fixes << ": the hero is not taken";
        EXPECT_FALSE(result.endgame_requested) << fixes;
        EXPECT_EQ(borrowed, result.new_control) << fixes;
    }
}

// ---------------------------------------------------------------------------
// A summon of our colour (#496). A team-1 druid our side charmed onto team 0
// summons a faerie: the faerie takes the druid's colour of the moment (team
// 0) and the druid as its owner (living-13-druid.lua). When the charm runs
// out the druid goes back to team 1 and the faerie stays on team 0 with no
// charm of its own. With the setting on (Gameplay Fixes AZ) it fights for team
// 0, the colour it wears; classic, for the head of its owner chain, the druid,
// on team 1.

namespace {

struct FoesSummon
{
    walker* druid = nullptr;   // team 1: the charm on it has run out
    walker* faerie = nullptr;  // team 0, owned by the druid, never charmed
};

// The shape the druid's charm leaves when it runs out. SeatWorld's bodies
// are plain walkers: the scans read no family, only the team, the charm and
// the owner chain.
FoesSummon add_foes_summon(SeatWorld& fx)
{
    FoesSummon s;
    s.druid = fx.add({.team = 1});
    s.faerie = fx.add({.team = 0});
    s.faerie->set_owner(s.druid);
    return s;
}

int notices(const og::sim::SimEventLog& events, const std::string& text)
{
    int n = 0;
    for (const og::sim::Event& e : events.events())
    {
        if (e.kind == og::sim::EventKind::Notification && e.text == text)
            ++n;
    }
    return n;
}

// An open field of grass for the tests that run the core pack's families.
void grass_field(GameWorld& world, short fixes)
{
    world.resize_grid(32, 32);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.my_team = 0;
    world.gameplay_fixes = fixes;
}

// Player 0's hero (team 0, a character record tagged for player 0).
walker* add_player_hero(GameWorld& world, short x, short y)
{
    walker* hero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (hero == nullptr)
        return nullptr;
    hero->set_team_num(0);
    hero->set_real_team_num(255);
    hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    hero->myguy->owner_player_index = 0;
    hero->setxy(x, y);
    return hero;
}

// Player 0 presses Switch Character once (the key released first).
void press_switch(SeatWorld& fx, walker*& control, SimInputDebounce& debounce)
{
    (void)seat_tick(fx.world(), fx.events, control, debounce);
    InputState input;
    input.clear();
    input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    (void)sim_process_player_input(input.players[0], control, fx.world(), 0, 0,
                                   debounce, &fx.events);
}

}  // namespace

// The faerie's side, on hand-made walkers, on both settings: with the setting
// on it fights for team 0, the colour it wears, and is the friend of team 0's
// hero; classic it fights for team 1, its druid's. A seat may claim it on both
// settings (it is not charmed); a walker our side charmed is refused only with
// the setting on. A dead hero is nobody's friend.
// Break: alliance_head's `return this;` -> `return chain_head();` (on: the
// faerie fights for team 1).
TEST(GameplayFixesSeats, a_summon_of_our_colour_fights_for_it_with_the_setting_on)
{
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        SeatWorld fx;
        fx.world().gameplay_fixes = fixes;
        walker* hero = fx.add({.team = 0, .user = 0, .hero = true, .owner = 0});
        walker* dead_hero = fx.add({.team = 0, .hero = true, .owner = 1});
        dead_hero->set_dead(1);
        const FoesSummon s = add_foes_summon(fx);
        walker* mate = fx.add({.team = 0});
        walker* charmed = fx.add({.team = 0, .real_team = 1, .charm_left = 100});

        EXPECT_EQ(fixes != 0 ? 1 : 0, s.faerie->is_friendly_to_team(0));
        EXPECT_EQ(fixes != 0 ? 0 : 1, s.faerie->is_friendly_to_team(1));
        EXPECT_EQ(fixes != 0 ? 1 : 0, s.faerie->is_friendly(hero));
        EXPECT_TRUE(og::sim::seat_may_claim(fx.world(), *s.faerie)) << "it is not charmed";
        EXPECT_TRUE(og::sim::seat_may_claim(fx.world(), *mate));
        EXPECT_EQ(fixes == 0, og::sim::seat_may_claim(fx.world(), *charmed))
            << "a walker our side charmed is refused only with the setting on";
        EXPECT_EQ(1, mate->is_friendly(hero));
        EXPECT_EQ(0, mate->is_friendly(dead_hero)) << "a dead hero is nobody's friend";
    }
}

// Player 0's hero lies dead beside the druid and its faerie (41 hit points),
// the only other body on team 0. On both settings the seat is handed the
// faerie and shows its 41 hit points: classic because the hand-off asks only
// the colour, with the setting on because the faerie fights for the colour it
// wears. A twin world with no faerie is a wiped-out team: the seat asks for
// the end of the game. The legacy scan and the owner-locked one (every seat
// on one deployed machine) answer alike.
// Break: the four seat scans (both passes of sim_find_next_control and of
// sim_find_next_control_owned) asking the owner chain again,
// `seat_may_claim(level, *w) && (level.gameplay_fixes == 0 ||
// w->chain_head()->team_num() == my_team)` (on: the
// faerie's chain heads at its team-1 druid and nobody is handed over).
TEST(GameplayFixesSeats, a_dead_heros_seat_takes_a_summon_of_its_colour_on_both_settings)
{
    for (const bool locked : {false, true})
    {
        for (const short fixes : {short{0}, short{1}})
        {
            const std::string where = std::string("locked ") + (locked ? "1" : "0") +
                ", setting " + (fixes != 0 ? "on" : "off");
            {
                SeatWorld fx;
                if (locked)
                    fx.lock_permissively();
                fx.world().gameplay_fixes = fixes;
                walker* hero = add_dead_hero(fx);
                const FoesSummon s = add_foes_summon(fx);
                s.faerie->stats()->set_hitpoints(41.0f);

                walker* control = hero;
                SimInputDebounce debounce{};
                const SimInputResult result =
                    seat_tick(fx.world(), fx.events, control, debounce);

                EXPECT_EQ(s.faerie, control) << where << ": the faerie is handed over";
                EXPECT_EQ(0, s.faerie->user()) << where;
                EXPECT_EQ(ACT_CONTROL, s.faerie->act_type()) << where;
                EXPECT_FALSE(result.endgame_requested) << where;
                EXPECT_TRUE(result.control_hp_changed) << where;
                EXPECT_EQ(41.0f, result.control_hp) << where;
            }

            // The wipe: the same seat with the druid and no faerie.
            SeatWorld wipe;
            if (locked)
                wipe.lock_permissively();
            wipe.world().gameplay_fixes = fixes;
            walker* wiped_hero = add_dead_hero(wipe);
            wipe.add({.team = 1});
            walker* control = wiped_hero;
            SimInputDebounce debounce{};
            const SimInputResult wiped =
                seat_tick(wipe.world(), wipe.events, control, debounce);
            EXPECT_EQ(nullptr, control) << where << ", the wipe";
            EXPECT_TRUE(wiped.endgame_requested) << where << ", the wipe";
            EXPECT_EQ(1, wiped.endgame_type) << where << ", the wipe";
            EXPECT_FALSE(wiped.control_hp_changed) << where << ", the wipe";
        }
    }
}

// The same dead hero, with the faerie and a real teammate both on team 0, in
// both orders of the world's list. On both settings the seat takes the first
// of them in the world's list: both wear and (with the setting on) fight for
// team 0. Legacy and owner-locked alike.
// Break: the four seat scans (both passes of sim_find_next_control and of
// sim_find_next_control_owned) asking the owner chain again,
// `seat_may_claim(level, *w) && (level.gameplay_fixes == 0 ||
// w->chain_head()->team_num() == my_team)` (on, the
// faerie first: the teammate is taken).
TEST(GameplayFixesSeats, a_dead_heros_seat_takes_the_first_body_of_its_colour)
{
    for (const bool locked : {false, true})
    {
        for (const bool summon_first : {true, false})
        {
            for (const short fixes : {short{0}, short{1}})
            {
                SeatWorld fx;
                if (locked)
                    fx.lock_permissively();
                fx.world().gameplay_fixes = fixes;
                walker* hero = add_dead_hero(fx);
                walker* summon = nullptr;
                walker* mate = nullptr;
                if (summon_first)
                {
                    summon = add_foes_summon(fx).faerie;
                    mate = fx.add({.team = 0});
                }
                else
                {
                    mate = fx.add({.team = 0});
                    summon = add_foes_summon(fx).faerie;
                }

                walker* control = hero;
                SimInputDebounce debounce{};
                const SimInputResult result =
                    seat_tick(fx.world(), fx.events, control, debounce);

                walker* const taken = summon_first ? summon : mate;
                walker* const left = summon_first ? mate : summon;
                const std::string where = std::string("locked ") + (locked ? "1" : "0") +
                    (summon_first ? ", summon first" : ", teammate first") +
                    ", setting " + (fixes != 0 ? "on" : "off");
                EXPECT_EQ(taken, control) << where;
                EXPECT_EQ(0, taken->user()) << where;
                EXPECT_EQ(ACT_CONTROL, taken->act_type()) << where;
                EXPECT_EQ(-1, left->user()) << where;
                EXPECT_FALSE(result.endgame_requested) << where;
            }
        }
    }
}

// The first pass, which looks for a player character: a team-0 hero with a
// character record owned (by hand) by the team-1 druid, then an unclaimed
// team-0 troop. On both settings the first pass takes the hero: it wears the
// seat's colour and, with the setting on, fights for it whoever owns it. Both
// the classic scan and the scan a seat takes through the control policy,
// legacy and owner-locked.
// Break: the four seat scans (both passes of sim_find_next_control and of
// sim_find_next_control_owned) asking the owner chain again,
// `seat_may_claim(level, *w) && (level.gameplay_fixes == 0 ||
// w->chain_head()->team_num() == my_team)` (on: the
// hero's chain heads at the team-1 druid, so both passes skip it and the
// troop is taken).
TEST(GameplayFixesSeats, the_first_pass_takes_a_hero_of_its_colour_whoever_owns_it)
{
    for (const bool locked : {false, true})
    {
        for (const short fixes : {short{0}, short{1}})
        {
            SeatWorld fx;
            if (locked)
                fx.lock_permissively();
            fx.world().gameplay_fixes = fixes;
            walker* druid = fx.add({.team = 1});
            walker* hero = fx.add({.team = 0, .hero = true, .owner = 0});
            hero->set_owner(druid);
            fx.add({.team = 0});

            const std::string where = std::string("locked ") + (locked ? "1" : "0") +
                ", setting " + (fixes != 0 ? "on" : "off");
            EXPECT_EQ(hero, sim_find_next_control(fx.world(), 0)) << where;
            EXPECT_EQ(hero, og::sim::sim_find_next_control_owned(fx.world(), 0, 0))
                << where;
        }
    }
}

// Switch Character has asked whose side a walker fights for since 2013. From
// a live hero, with the setting on it switches to the faerie (it fights for
// team 0, the colour it wears); classic it skips the faerie (its druid's
// side), keeps the hero and says "NO ONE TO SWITCH TO". With a real teammate
// after the faerie the next press switches to the teammate on both settings.
// The hand-off from a dead hero takes the faerie on both settings, so the two
// agree with the setting on.
// Breaks: alliance_head's `return this;` -> `return chain_head();` (on: the
// switch skips the faerie); the four seat scans asking the owner chain again,
// as above (on: the hand-off takes nobody).
TEST(GameplayFixesSeats, switch_character_and_the_hand_off_agree_on_a_summon_of_our_colour_with_the_setting_on)
{
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        {
            SeatWorld fx;
            fx.world().gameplay_fixes = fixes;
            walker* hero = fx.add({.team = 0, .user = 0, .hero = true, .owner = 0});
            const FoesSummon s = add_foes_summon(fx);

            walker* control = hero;
            SimInputDebounce debounce{};
            InputState input;
            input.clear();
            input.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
            (void)sim_process_player_input(input.players[0], control, fx.world(), 0, 0,
                                           debounce, &fx.events);
            if (fixes != 0)
            {
                EXPECT_EQ(s.faerie, control) << "the switch takes the faerie";
                EXPECT_EQ(0, s.faerie->user());
                EXPECT_EQ(0, notices(fx.events, "NO ONE TO SWITCH TO"));
            }
            else
            {
                EXPECT_EQ(hero, control) << "the switch skips the faerie";
                EXPECT_EQ(0, hero->user());
                EXPECT_EQ(1, notices(fx.events, "NO ONE TO SWITCH TO"));
                EXPECT_EQ(-1, s.faerie->user());
            }

            // A real teammate after the faerie; the key released, then
            // pressed again.
            walker* mate = fx.add({.team = 0});
            press_switch(fx, control, debounce);
            EXPECT_EQ(mate, control) << "the switch takes the teammate";
            EXPECT_EQ(0, mate->user());
            EXPECT_EQ(-1, s.faerie->user());
        }

        SeatWorld fx;
        fx.world().gameplay_fixes = fixes;
        walker* hero = add_dead_hero(fx);
        const FoesSummon s = add_foes_summon(fx);
        walker* control = hero;
        SimInputDebounce debounce{};
        const SimInputResult result = seat_tick(fx.world(), fx.events, control, debounce);
        EXPECT_EQ(s.faerie, control) << "the hand-off takes the faerie";
        EXPECT_FALSE(result.endgame_requested);
    }
}

// A seat on the faerie keeps it when the charm on the faerie's caster runs
// out (#496, the #450 ruling). Team 0's unclaimed hero stands first in the
// world's list, so a seat that looked for a new body would take it. The druid
// our side charmed (100 ticks left) has summoned the faerie, which fights for
// team 0 while the charm runs, and the seat drives the faerie. Then the charm
// runs out as living::act's decay leaves it (the druid back on team 1, no team
// recorded, no timer; the decay itself is pinned by
// SemLiving.charm_counts_down_without_touching_the_team_and_an_uncharmed_walker_keeps_its_team).
// With the setting on the faerie, still on team 0, fights for team 0, the
// colour it wears; classic, for team 1, its druid's. The seat looks for a new
// body only when it has none or its body is dead, so it stays on the faerie,
// on both settings.
// RED with a seat that also looks when its body no longer fights for the
// seat's team: once the charm is out, with the setting off, the seat jumps to
// the hero.
TEST(GameplayFixesSeats, a_seat_keeps_a_summon_when_its_casters_charm_runs_out)
{
    for (const short fixes : {short{0}, short{1}})
    {
        SeatWorld fx;
        fx.world().gameplay_fixes = fixes;
        walker* hero = fx.add({.team = 0, .hero = true, .owner = 0});
        walker* druid = fx.add({.team = 0, .real_team = 1, .charm_left = 100});
        walker* faerie = fx.add({.team = 0});
        faerie->set_owner(druid);
        ASSERT_NE(0, faerie->is_friendly_to_team(0))
            << fixes << ": while the charm runs the faerie fights for team 0";

        ASSERT_TRUE(sim_claim_control(fx.world(), *faerie, 0)) << fixes;
        walker* control = faerie;
        SimInputDebounce debounce{};
        SimInputResult result = seat_tick(fx.world(), fx.events, control, debounce);
        EXPECT_EQ(faerie, control) << fixes;
        EXPECT_FALSE(result.endgame_requested) << fixes;

        // The charm on the druid runs out.
        druid->set_team_num(1);
        druid->set_real_team_num(255);
        druid->set_charm_left(0);
        ASSERT_EQ(fixes != 0 ? 1 : 0, faerie->is_friendly_to_team(0))
            << fixes << ": with the setting on the faerie fights for the colour it wears; "
                        "classic, for the druid's";
        ASSERT_EQ(0, faerie->team_num()) << fixes << ": and still wears team 0";

        result = seat_tick(fx.world(), fx.events, control, debounce);
        EXPECT_EQ(faerie, control)
            << "setting " << fixes << ": the seat keeps the summon";
        EXPECT_EQ(0, faerie->user()) << fixes;
        EXPECT_EQ(ACT_CONTROL, faerie->act_type()) << fixes;
        EXPECT_EQ(-1, hero->user()) << fixes << ": the hero is not taken";
        EXPECT_FALSE(result.endgame_requested) << fixes;
        EXPECT_EQ(faerie, result.new_control) << fixes;
    }
}

// #536: the seat on the faerie of the test above, the charm out, and a team-0
// teammate after it in the world's list. With the setting on the faerie
// fights for team 0, so Switch Character takes the teammate (a friend of the
// body it switches from); classic the faerie fights for its druid's side,
// every team-0 body is its foe, and the switch says "NO ONE TO SWITCH TO"
// once and keeps the faerie.
// Break: alliance_head's `return this;` -> `return chain_head();` (on: no one
// to switch to).
TEST(GameplayFixesSeats, a_seat_on_a_summon_whose_casters_charm_ran_out_switches_home_only_with_the_setting_on)
{
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        SeatWorld fx;
        fx.world().gameplay_fixes = fixes;
        walker* druid = fx.add({.team = 0, .real_team = 1, .charm_left = 100});
        walker* faerie = fx.add({.team = 0});
        faerie->set_owner(druid);
        walker* mate = fx.add({.team = 0});
        ASSERT_TRUE(sim_claim_control(fx.world(), *faerie, 0));
        walker* control = faerie;
        SimInputDebounce debounce{};
        (void)seat_tick(fx.world(), fx.events, control, debounce);
        druid->set_team_num(1);
        druid->set_real_team_num(255);
        druid->set_charm_left(0);

        press_switch(fx, control, debounce);
        if (fixes != 0)
        {
            EXPECT_EQ(mate, control) << "the switch takes the teammate";
            EXPECT_EQ(0, mate->user());
            EXPECT_EQ(0, notices(fx.events, "NO ONE TO SWITCH TO"));
        }
        else
        {
            EXPECT_EQ(faerie, control) << "classic: no one to switch to";
            EXPECT_EQ(0, faerie->user());
            EXPECT_EQ(-1, mate->user());
            EXPECT_EQ(1, notices(fx.events, "NO ONE TO SWITCH TO"));
        }
    }
}

// The issue's staging end to end, with the core pack's druid. Player 0's
// hero lies dead. A level-4 druid of team 1 that our side charmed onto team
// 0 (one act of charm left) casts SUMMON FAERIE on open ground, as
// SemDruid.a_faerie_summoned_on_open_ground_lives_for_its_lifetime casts it:
// the faerie takes the druid's colour of the moment and the druid as its
// owner. The druid's next act runs the charm out: the druid is back on team
// 1 and the faerie stays on team 0. On both settings the dead hero's seat is
// handed the faerie: classic because the hand-off asks only the colour, with
// the setting on (#496) because the faerie stays on the charmer's team and
// fights for it.
// Break: the four seat scans (both passes of sim_find_next_control and of
// sim_find_next_control_owned) asking the owner chain again,
// `seat_may_claim(level, *w) && (level.gameplay_fixes == 0 ||
// w->chain_head()->team_num() == my_team)` (on:
// nobody is handed over and the game ends).
TEST(GameplayFixesSeats, a_charmed_druids_faerie_is_handed_over_on_both_settings)
{
    for (const short fixes : {short{0}, short{1}})
    {
        og::test::mount_core_pack();
        og::test::ScopedHookFailureGuard hook_failures;
        TestGameWorld tw;
        GameWorld& world = tw.world();
        grass_field(world, fixes);

        walker* hero = add_player_hero(world, 64, 64);
        ASSERT_NE(nullptr, hero);
        hero->set_user(0);
        hero->set_dead(1);

        walker* druid = world.add_ob(Order::Living, FAMILY_DRUID);
        ASSERT_NE(nullptr, druid);
        druid->set_team_num(0);
        druid->set_real_team_num(1);
        druid->set_charm_left(1);
        druid->setxy(200, 200);
        druid->set_sizex(16);
        druid->set_sizey(16);
        druid->stats()->set_level(4);
        druid->stats()->set_max_magicpoints(300.0f);
        druid->stats()->set_magicpoints(300.0f);
        druid->stats()->set_magic_per_round(0.0f);
        druid->stats()->set_max_magic_delay(1000000);
        druid->set_busy(0.0f);
        druid->set_lastx(1.0f);
        druid->set_lasty(0.0f);
        druid->set_curdir(static_cast<signed char>(FACE_RIGHT));

        // The cast, seated as SemDruid seats it; then the druid goes back to
        // its own mind.
        druid->set_user(0);
        druid->set_act_type(ACT_CONTROL);
        druid->set_current_special(2);  // SUMMON FAERIE
        std::string reason;
        ASSERT_TRUE(druid->special(nullptr, &reason)) << fixes << ": " << reason;
        druid->set_user(-1);
        druid->set_act_type(ACT_RANDOM);

        walker* faerie = nullptr;
        int faeries = 0;
        for (const auto& ob : world.oblist)
        {
            if (ob && !ob->dead() && ob->query_order() == Order::Living &&
                ob->family() == static_cast<std::uint8_t>(FAMILY_FAERIE))
            {
                faerie = ob.get();
                ++faeries;
            }
        }
        ASSERT_EQ(1, faeries) << fixes;
        ASSERT_EQ(druid, faerie->owner()) << fixes;
        ASSERT_EQ(0, faerie->team_num()) << fixes;

        // The druid's next act runs the charm out (living::act's decay comes
        // before any early return).
        druid->act();
        ASSERT_EQ(1, druid->team_num()) << fixes;
        ASSERT_EQ(255, druid->real_team_num()) << fixes;
        ASSERT_EQ(0, druid->charm_left()) << fixes;
        ASSERT_FALSE(faerie->dead()) << fixes;
        ASSERT_EQ(0, faerie->team_num()) << fixes;
        ASSERT_EQ(druid, faerie->owner()) << fixes;

        walker* control = hero;
        SimInputDebounce debounce{};
        const SimInputResult result = seat_tick(world, tw.events, control, debounce);
        EXPECT_EQ(faerie, control) << fixes << ": the faerie is handed over";
        EXPECT_EQ(0, faerie->user()) << fixes;
        EXPECT_FALSE(result.endgame_requested) << fixes;
        EXPECT_EQ(0u, hook_failures.count()) << hook_failures.message();
    }
}

// The host's side of the same hand-off (#537's untimed shape), through a real
// GameServer over the in-process transport, in a classic world with respawns
// off: player 0's hero bound to seat 0 on team 0, a stationary team-1 druid,
// and the team-0 faerie it owns (the shape the druid's charm leaves when it
// runs out; it has no lifetime). The hero dies. On both settings the seat
// drives the faerie and the level goes on; once the faerie is killed by hand
// the next step ends the level as a wiped-out team's does. A twin with no
// faerie ends on the first step after the hero's death, on both settings.
// Break: the four seat scans (both passes of sim_find_next_control and of
// sim_find_next_control_owned) asking the owner chain again,
// `seat_may_claim(level, *w) && (level.gameplay_fixes == 0 ||
// w->chain_head()->team_num() == my_team)` (on: the
// seat holds nobody after the hero's death).
TEST(GameplayFixesSeats, a_summon_of_our_colour_keeps_the_seat_and_the_level_on_both_settings)
{
    for (const bool with_faerie : {true, false})
    {
        for (const short fixes : {short{0}, short{1}})
        {
            SCOPED_TRACE(std::string(with_faerie ? "faerie" : "no faerie") +
                         ", setting " + (fixes != 0 ? "on" : "off"));
            og::test::mount_core_pack();
            og::test::ScopedHookFailureGuard hook_failures;
            TestGameWorld tw;
            GameWorld& world = tw.world();
            grass_field(world, fixes);
            ASSERT_EQ(0, world.type);
            ASSERT_EQ(0, world.respawn_mode);

            walker* hero = add_player_hero(world, 64, 64);
            ASSERT_NE(nullptr, hero);
            walker* druid = world.add_ob(Order::Living, FAMILY_DRUID);
            ASSERT_NE(nullptr, druid);
            druid->set_team_num(1);
            druid->set_real_team_num(255);
            druid->setxy(400, 400);
            druid->set_act_type(ACT_CONTROL);  // stationary without input
            walker* faerie = nullptr;
            if (with_faerie)
            {
                faerie = world.add_ob(Order::Living, FAMILY_FAERIE);
                ASSERT_NE(nullptr, faerie);
                faerie->set_team_num(0);
                faerie->set_real_team_num(255);
                faerie->setxy(400, 200);
                faerie->set_act_type(ACT_CONTROL);
                faerie->set_owner(druid);
            }

            auto server_transport = og::sim::InProcessTransport::create_server();
            server_transport->accept_connections();
            auto client = server_transport->create_client_transport();
            og::sim::GameServer server(world, tw.events, *server_transport);
            server.set_wall_clock_ms_source([] { return std::uint64_t{0}; });
            server.connect_client(client->local_peer_id());
            server.bind_player(client->local_peer_id(), 0u, 0);
            ASSERT_EQ(hero, server.player_control(0));

            hero->set_dead(1);
            hero->death();
            server.step();
            if (!with_faerie)
            {
                EXPECT_EQ(1, world.ending) << "the wipe ends the level";
            }
            else
            {
                EXPECT_EQ(faerie, server.player_control(0)) << "the seat drives the faerie";
                EXPECT_EQ(0, faerie->user());
                EXPECT_EQ(0, world.ending);

                faerie->set_dead(1);
                faerie->death();
                server.step();
                EXPECT_EQ(1, world.ending) << "the faerie is gone: the level ends as on a wipe";
            }
            // Every leg, the one with no faerie included.
            EXPECT_EQ(0u, hook_failures.count()) << hook_failures.message();
        }
    }
}
