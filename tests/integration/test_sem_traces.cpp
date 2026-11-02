/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests whose only missing arm is a TRACE line. The game library
// the og_unit_* binaries link is built without TESTING, so TRACE is
// ((void)0) there; the integration groups link the TESTING build, where the
// trace buffer records it. Each test is named after the rule it asserts,
// and each failure message names the arm.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
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
#include <string>

namespace {

using og::sim::SimReacquire;
using og::sim::encode_player_machine;
using og::sim::kControlPolicyOwnerLocked;
using og::sim::kPlayerMachineNone;
using og::sim::kPlayerMachineSlots;
using og::sim::set_control_policy;
using og::sim::sim_find_next_control_owned;
using og::sim::sim_reacquire_control;

// The canonical owner-locked lobby of tests/unit/test_sim_control_policy.cpp:
// players 0 and 1 on machine 0 (deployed), player 2 on machine 2 (deployed),
// player 3 on machine 3 (spectating).
std::array<std::uint8_t, kPlayerMachineSlots> canonical_machine_map()
{
    std::array<std::uint8_t, kPlayerMachineSlots> machines;
    machines.fill(kPlayerMachineNone);
    machines[0] = encode_player_machine(0, true);
    machines[1] = encode_player_machine(0, true);
    machines[2] = encode_player_machine(2, true);
    machines[3] = encode_player_machine(3, false);
    return machines;
}

struct ControlScene
{
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    ControlScene()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &events, &cfg);
    }

    GameWorld& world() { return level.world(); }

    // An unclaimed team-0 hero owned by `owner`.
    walker* hero(std::uint8_t owner, bool dead)
    {
        auto w = std::make_unique<walker>();
        w->set_order_family(Order::Living, FAMILY_SOLDIER);
        w->setxy(80, 80);
        w->set_sizex(16);
        w->set_sizey(16);
        w->set_stepsize(1.0f);
        w->set_team_num(0);
        w->set_real_team_num(255);
        w->set_dead(dead ? 1 : 0);
        w->set_user(-1);
        w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        w->myguy->owner_player_index = owner;
        walker* out = w.get();
        world().oblist.push_back(std::move(w));
        return out;
    }
};

}  // namespace

// sim_control_policy.cpp:238 sim_find_next_control_owned, the "none" arm,
// and sim_control_policy.cpp:280 sim_reacquire_control, the Follow arm.
// Under the owner-locked policy a seat whose own heroes are dead finds no
// claimable walker (the scan says so, "found no claimable walker"), and when
// the legacy scan would still hand it someone else's hero the seat follows
// instead of ending the game ("reacquire follow for player N"). Once the
// last hero dies too, the scan still finds nobody but the verdict is
// EndGame, with no follow trace. A seat that claims its own hero traces
// neither. The verdicts themselves are pinned by
// SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain; this
// pins the trace lines. No classic oracle: the owner-locked policy is the
// current game's networked rule.
//
// RED (the `found no claimable walker` TRACE -> `(void)0;` at
// sim_control_policy.cpp:272): Value of: trace_contains("sim_control",
// "found no claimable walker") Actual: false Expected: true.
// RED (the `reacquire follow` TRACE -> `(void)0;` at
// sim_control_policy.cpp:299): Value of: trace_contains("sim_control",
// "reacquire follow for player 0") Actual: false Expected: true.
TEST(SemTraces, an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows)
{
    ControlScene scene;
    scene.hero(0, true);
    scene.hero(1, true);
    walker* const foreign = scene.hero(2, false);
    set_control_policy(scene.world(), kControlPolicyOwnerLocked, canonical_machine_map());

    // The scan's "none" arm.
    trace_clear();
    EXPECT_EQ(nullptr, sim_find_next_control_owned(scene.world(), 0, 0)) << "none: return";
    EXPECT_TRUE(trace_contains("sim_control", "found no claimable walker")) << "none: trace";

    // The reacquire's Follow arm, for each host seat.
    for (short player : {short{0}, short{1}})
    {
        trace_clear();
        walker* out = foreign;
        EXPECT_EQ(SimReacquire::Follow, sim_reacquire_control(scene.world(), 0, player, out))
            << "player " << player;
        EXPECT_EQ(nullptr, out) << "player " << player;
        const std::string line = "reacquire follow for player " + std::to_string(player);
        EXPECT_TRUE(trace_contains("sim_control", line.c_str()))
            << "follow: trace for player " << player;
        EXPECT_TRUE(trace_contains("sim_control", "found no claimable walker"))
            << "follow: the scan found nobody for player " << player;
    }

    // Control: the owner claims its hero; neither trace.
    trace_clear();
    walker* out = nullptr;
    EXPECT_EQ(SimReacquire::Claimed, sim_reacquire_control(scene.world(), 0, 2, out));
    EXPECT_EQ(foreign, out);
    EXPECT_FALSE(trace_contains("sim_control", "found no claimable walker")) << "claimed: no none trace";
    EXPECT_FALSE(trace_contains("sim_control", "reacquire follow")) << "claimed: no follow trace";

    // A full wipe: nobody, EndGame, the none trace but no follow trace.
    foreign->set_dead(1);
    trace_clear();
    out = foreign;
    EXPECT_EQ(SimReacquire::EndGame, sim_reacquire_control(scene.world(), 0, 0, out));
    EXPECT_EQ(nullptr, out);
    EXPECT_TRUE(trace_contains("sim_control", "found no claimable walker")) << "wipe: none trace";
    EXPECT_FALSE(trace_contains("sim_control", "reacquire follow")) << "wipe: no follow trace";
}
