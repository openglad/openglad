#include <openglad/legacy/base.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <cstddef>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

// The sim RNG behind stats.cpp's rng() helper is GameWorld::rng_, not the
// GameContext one -- two independent streams. ScopedSimRandom
// (tests/test_sim_random_scope.h) is the way to script the sim one.
namespace
{
// walker/pixie store raw pointers into PixieData buffers; keep the data alive.
PixieData one_px()
{
    return PixieData(1, 1, 1, new unsigned char[1]{0});
}

void set_all_tiles(unsigned char tile)
{
    auto& lvl = og::runtime::current_session->myscreen_->level_runtime_data();
    if (!lvl.world().grid.valid())
        lvl.create_new_grid();
    const int size = static_cast<int>(lvl.world().grid.w) * static_cast<int>(lvl.world().grid.h);
    for (int i = 0; i < size; i++)
        lvl.world().grid.data[static_cast<std::size_t>(i)] = tile;
}

void set_tile(int tx, int ty, unsigned char tile)
{
    auto& lvl = og::runtime::current_session->myscreen_->level_runtime_data();
    if (!lvl.world().grid.valid())
        lvl.create_new_grid();
    if (tx < 0 || ty < 0 || tx >= lvl.world().grid.w || ty >= lvl.world().grid.h)
        return;
    lvl.world().grid.data[static_cast<std::size_t>(ty * lvl.world().grid.w + tx)] = tile;
}
} // namespace

// ---------------------------------------------------------------------------
// force_command tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_force_command_prepends)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    w->stats()->force_command(COMMAND_FIRE, 3, 0, 0);

    ASSERT_TRUE(w->stats()->commands.size() >= 2) << "should have at least 2 commands";
    ASSERT_EQ(COMMAND_FIRE, w->stats()->commands.front().commandtype) << "force_command should prepend";

}


TEST(StatsExtended, statistics_force_command_walk_clamps)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->force_command(COMMAND_WALK, 10, 50, -50);

    const command& c = w->stats()->commands.front();
    ASSERT_EQ(COMMAND_WALK, c.commandtype) << "should be walk command";
    ASSERT_EQ(1, c.com1) << "com1 should be clamped to 1";
    ASSERT_EQ(-1, c.com2) << "com2 should be clamped to -1";

}


// ---------------------------------------------------------------------------
// has_commands tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_has_commands_empty)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    ASSERT_TRUE(!w->stats()->has_commands()) << "empty queue should have no commands";

}


TEST(StatsExtended, statistics_has_commands_nonempty)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_TRUE(w->stats()->has_commands()) << "non-empty queue should have commands";

}


// ---------------------------------------------------------------------------
// do_command: the branches this file used to call and discard
//
// StatsExtended.statistics_do_command_walk lived here and asserted nothing.
// The COMMAND_WALK rule (one queued walk = one walkstep of stepsize, then the
// tail decrement pops the entry) is pinned exactly by the baseline block of
// StatsMorePaths.multido_runs_the_next_two_queued_commands_in_one_round, so
// the smoke copy is gone rather than duplicated.
// ---------------------------------------------------------------------------

TEST(StatsExtended, do_command_fire_denied_by_fire_check_returns_0_and_drops_the_command)
{
    // Known terrain, so which gate denies first cannot depend on the grid a
    // neighbouring test happened to leave behind.
    set_all_tiles(PIX_GRASS1);
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->setxy(50, 50);
    w->set_busy(0.0f);

    // No foe => fire_check() denies with NoFoe (walker.cpp:1492).
    w->set_foe(nullptr);
    w->stats()->clear_command();
    w->stats()->add_command(COMMAND_FIRE, 3, 0, 0);

    ASSERT_EQ(0, w->stats()->do_command())
        << "COMMAND_FIRE returns 0 when fire_check denies the shot";
    EXPECT_FALSE(w->stats()->has_commands())
        << "the denial zeroes commandcount, so the tail decrement pops the entry";
    EXPECT_FLOAT_EQ(0.0f, w->busy())
        << "a denied fire must not start a swing";
}


TEST(StatsExtended, do_command_fire_that_passes_fire_check_starts_the_shot_and_returns_1)
{
    // fire_check rays and walksteps consult the terrain grid; an absent or
    // leftover grid would deny every shot with WallBlocked.
    set_all_tiles(PIX_GRASS1);
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    auto foe = create_living(FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, foe.get()) << "create foe should succeed";

    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->set_team_num(0);
    foe->set_team_num(1);
    w->setxy(50, 50);
    foe->setxy(58, 50); // inside the knife's reach
    w->set_foe(foe.get());

    // fire_check's facing gate compares facing(com1, com2) with curdir, and
    // set_weapon_heading reads lastx/lasty - keep all three consistent.
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0.0f);
    w->set_busy(0.0f);

    w->stats()->clear_command();
    w->stats()->add_command(COMMAND_FIRE, 3, 1, 0);

    ASSERT_EQ(1, w->stats()->do_command())
        << "a passing fire_check runs init_fire and returns 1";
    EXPECT_FLOAT_EQ(w->fire_frequency(), w->busy())
        << "init_fire charges exactly one fire_frequency of busy time";
    ASSERT_TRUE(w->stats()->has_commands())
        << "a 3-count fire keeps firing next round";
    EXPECT_EQ(2, w->stats()->commands.front().commandcount)
        << "the tail decrement takes the surviving fire from 3 to 2";
}


TEST(StatsExtended, do_command_attack_faces_an_in_reach_foe_without_walking)
{
    // fire_check rays and walksteps consult the terrain grid; an absent or
    // leftover grid would deny every shot with WallBlocked.
    set_all_tiles(PIX_GRASS1);
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    auto foe = create_living(FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, foe.get()) << "create foe should succeed";

    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->set_team_num(0);
    foe->set_team_num(1);
    w->setxy(50, 50);
    foe->setxy(60, 50); // 10px due east: |dx| > 3*|dy|, so deltay collapses to 0
    w->set_foe(foe.get());

    // Pointed the wrong way: fire_check denies with Facing, and the
    // guard-standoff rule (stats.cpp:596-602) faces the foe instead of walking.
    w->set_curdir(static_cast<signed char>(FACE_UP));
    w->set_enddir(static_cast<char>(FACE_UP));
    w->set_lastx(0.0f);
    w->set_lasty(-w->stepsize());
    w->set_busy(0.0f);

    w->stats()->clear_command();
    w->stats()->add_command(COMMAND_ATTACK, 5, 0, 0);

    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_ATTACK reports it acted";
    EXPECT_EQ(50, w->xpos()) << "a Facing denial faces the foe, it does not walk";
    EXPECT_EQ(50, w->ypos()) << "a Facing denial faces the foe, it does not walk";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->curdir()))
        << "face_delta snaps curdir onto the normalized (+1,0) foe delta";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->enddir()))
        << "face_delta snaps enddir too, so act() cannot turn back off the foe";
    EXPECT_FLOAT_EQ(0.0f, w->busy()) << "facing is not a swing";
    ASSERT_TRUE(w->stats()->has_commands());
    EXPECT_EQ(COMMAND_ATTACK, w->stats()->commands.front().commandtype)
        << "the attack survives to the next round";
    EXPECT_EQ(4, w->stats()->commands.front().commandcount)
        << "the tail decrement takes the attack from 5 to 4";
}


TEST(StatsExtended, do_command_attack_that_can_shoot_forces_a_fire_command_and_fires)
{
    // fire_check rays and walksteps consult the terrain grid; an absent or
    // leftover grid would deny every shot with WallBlocked.
    set_all_tiles(PIX_GRASS1);
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    auto foe = create_living(FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, foe.get()) << "create foe should succeed";

    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->set_team_num(0);
    foe->set_team_num(1);
    w->setxy(50, 50);
    foe->setxy(60, 50);
    w->set_foe(foe.get());
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0.0f);
    w->set_busy(0.0f);

    // force_command(COMMAND_FIRE, rng(5), ...) draws from the sim RNG.
    FixedRandom scripted_source(3); // rng(5) == 3
    ScopedSimRandom scripted(&scripted_source);

    w->stats()->clear_command();
    w->stats()->add_command(COMMAND_ATTACK, 5, 0, 0);

    ASSERT_EQ(1, w->stats()->do_command()) << "COMMAND_ATTACK reports it acted";
    EXPECT_EQ(50, w->xpos()) << "shooting does not move the shooter";
    EXPECT_EQ(50, w->ypos()) << "shooting does not move the shooter";
    EXPECT_FLOAT_EQ(w->fire_frequency(), w->busy())
        << "the attack's init_fire charges one fire_frequency of busy time";

    ASSERT_EQ(2u, w->stats()->commands.size())
        << "the forced fire is prepended in front of the surviving attack";
    const command& fired = w->stats()->commands.front();
    EXPECT_EQ(COMMAND_FIRE, fired.commandtype) << "a passing fire_check forces a fire";
    EXPECT_EQ(2, fired.commandcount) << "rng(5)==3, minus the tail decrement";
    EXPECT_EQ(1, fired.com1) << "the fire carries the normalized east delta";
    EXPECT_EQ(0, fired.com2) << "the fire carries the normalized east delta";
    EXPECT_TRUE(fired.forced) << "force_command marks the entry forced";
    EXPECT_EQ(COMMAND_ATTACK, w->stats()->commands.back().commandtype)
        << "the attack itself is untouched behind the forced fire";
    EXPECT_EQ(5, w->stats()->commands.back().commandcount)
        << "the tail decrement hit the forced fire, not the attack";
}


TEST(StatsExtended, do_command_unhandled_type_only_runs_the_decrement_tail)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    w->setxy(50, 50);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_enddir(static_cast<char>(FACE_RIGHT));

    // The 4-arg add_command stores COMMAND_RANDOM_WALK verbatim (only the
    // 2-arg try_command/set_command translate it, stats.cpp:335/350), and
    // do_command's switch has no case for it: it falls to `default: break`
    // and only the shared decrement tail (stats.cpp:634-643) runs.
    w->stats()->clear_command();
    w->stats()->add_command(COMMAND_RANDOM_WALK, 3, 0, 0);

    ASSERT_EQ(1, w->stats()->do_command()) << "an unhandled command still reports 1";
    EXPECT_EQ(50, w->xpos()) << "an unhandled command moves nobody";
    EXPECT_EQ(50, w->ypos()) << "an unhandled command moves nobody";
    ASSERT_EQ(1u, w->stats()->commands.size()) << "still queued";
    EXPECT_EQ(COMMAND_RANDOM_WALK, w->stats()->commands.front().commandtype)
        << "the queued type is not rewritten";
    EXPECT_EQ(2, w->stats()->commands.front().commandcount) << "3 - 1";

    ASSERT_EQ(1, w->stats()->do_command());
    EXPECT_EQ(1, w->stats()->commands.front().commandcount) << "2 - 1";

    ASSERT_EQ(1, w->stats()->do_command());
    EXPECT_FALSE(w->stats()->has_commands())
        << "the last iteration pops the command off the queue";
}


// ---------------------------------------------------------------------------
// forward_blocked / right_blocked / right_forward_blocked / right_back_blocked
//
// Each of the four is an eight-case switch over curdir that picks a probe
// offset of +/-CHECK_STEP_SIZE (1px) and asks query_passable about that point
// (stats.cpp:769-934). The four smoke tests that used to live here ran all
// the facings and (void)-cast every answer.
//
// Oracle: a 1x1 walker sits on a grass grid with exactly ONE wall tile.
// query_grid_passable walks the cells the walker's box covers, so for a 1px
// body at (GRID_SIZE-1, GRID_SIZE-1) the probed tile column is 1 when the x
// offset is +1 and 0 otherwise; at (GRID_SIZE, GRID_SIZE) it is 0 when the x
// offset is -1 and 1 otherwise (same for rows). Running both placements
// against each of the four candidate wall cells therefore pins the SIGN of
// both offsets for every facing of all four functions - an offset that
// changes sign, or a switch case that moves, flips one of these booleans.
// ---------------------------------------------------------------------------

TEST(StatsExtended, blocked_probes_pin_every_facing_offset_of_all_four_tables)
{
    set_all_tiles(PIX_GRASS1);

    PixieData px = one_px();
    walker w(px);
    w.set_stepsize(1.0f);

    statistics* st = w.stats();
    ASSERT_NE(nullptr, st) << "stats exists";

    struct Fn { const char* name; bool (statistics::*call)(); };
    const Fn fns[4] = {
        { "forward_blocked", &statistics::forward_blocked },
        { "right_blocked", &statistics::right_blocked },
        { "right_forward_blocked", &statistics::right_forward_blocked },
        { "right_back_blocked", &statistics::right_back_blocked },
    };

    // Expected probe offsets, straight off the switch tables in stats.cpp.
    // Index [fn][dir]; dir 8 is the out-of-range `default:` arm.
    static const int off[4][9][2] = {
        // forward_blocked (stats.cpp:886-929)
        { {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,0} },
        // right_blocked (stats.cpp:769-814)
        { {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {1,-1}, {0,0} },
        // right_forward_blocked (stats.cpp:818-847)
        { {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {0,0} },
        // right_back_blocked (stats.cpp:850-881)
        { {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1}, {0,-1}, {1,-1}, {1,0}, {0,0} },
    };
    // right_forward_blocked / right_back_blocked return false outright on the
    // default arm instead of probing their own position.
    const bool default_arm_probes[4] = { true, true, false, false };

    const int placements[2] = { GRID_SIZE - 1, GRID_SIZE };

    // Control: no wall anywhere means nothing is ever blocked. This also
    // proves no stray object is parked on the probed cells.
    for (int p = 0; p < 2; p++)
    {
        w.setxy(placements[p], placements[p]);
        for (int dir = 0; dir <= 8; dir++)
        {
            w.set_curdir(static_cast<signed char>(dir));
            for (int f = 0; f < 4; f++)
            {
                // The lookup and the pointer-to-member call are hoisted out
                // of the assertion macro: GCC 13's -fsanitize=bounds reports
                // a phantom out-of-range index on the macro-expanded form
                // (CI ASan+UBSan lane, PR #292), with the same four probes.
                const Fn& fn = fns[f];
                const bool blocked = (st->*(fn.call))();
                EXPECT_FALSE(blocked)
                    << fn.name << " on open grass, dir " << dir
                    << ", placement " << placements[p];
            }
        }
    }

    for (int p = 0; p < 2; p++)
    {
        w.setxy(placements[p], placements[p]);
        for (int cx = 0; cx < 2; cx++)
        {
            for (int cy = 0; cy < 2; cy++)
            {
                set_all_tiles(PIX_GRASS1);
                set_tile(cx, cy, PIX_H_WALL1);

                for (int dir = 0; dir <= 8; dir++)
                {
                    w.set_curdir(static_cast<signed char>(dir));
                    for (int f = 0; f < 4; f++)
                    {
                        const int dx = off[f][dir][0];
                        const int dy = off[f][dir][1];
                        // Which tile that offset lands in, per placement.
                        const int tx = (p == 0) ? (dx > 0 ? 1 : 0) : (dx < 0 ? 0 : 1);
                        const int ty = (p == 0) ? (dy > 0 ? 1 : 0) : (dy < 0 ? 0 : 1);
                        bool expected = (tx == cx && ty == cy);
                        if (dir == 8 && !default_arm_probes[f])
                            expected = false;
                        const Fn& fn = fns[f];
                        const bool blocked = (st->*(fn.call))();
                        EXPECT_EQ(expected, blocked)
                            << fn.name << " dir " << dir << " expects probe offset ("
                            << dx << "," << dy << "); wall at tile (" << cx << "," << cy
                            << "), body at " << placements[p];
                    }
                }
            }
        }
    }

    set_all_tiles(PIX_GRASS1);
}


// ---------------------------------------------------------------------------
// hit_response
//
// The mage copy that used to live here is gone: the teleport branch it set up
// (and never checked) is pinned exactly by
// StatsHitResponseTeleport.stats_hit_response_mage_and_archmage_teleport_branches.
// The archer copy is gone too; its Lua back-away rule is now pinned exactly in
// StatsHitResponseMore.statistics_hit_response_archer_runs_away_and_queues_walk.
// ---------------------------------------------------------------------------

TEST(StatsExtended, hit_response_from_a_new_attacker_retargets_both_ways_and_clears_commands)
{
    auto w = create_living(FAMILY_SOLDIER);
    auto attacker = create_living(FAMILY_SMALL_SLIME);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    ASSERT_NE(nullptr, attacker.get()) << "create attacker should succeed";

    // rng(3) == 1, so check_special()'s `!rng(3)` gate stays shut and no
    // special fires; this test is about the retarget block only.
    FixedRandom scripted_source(1);
    ScopedSimRandom scripted(&scripted_source);

    w->set_team_num(0);
    attacker->set_team_num(1);
    w->setxy(100, 100);
    attacker->setxy(110, 100);
    w->set_foe(nullptr);
    attacker->set_foe(nullptr);

    // Healthy: above the 50%/5-16ths flee threshold, so no yell_for_help.
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(100.0f);
    w->stats()->set_last_distance(7);
    w->stats()->set_current_distance(7);
    w->stats()->clear_command();
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    ASSERT_TRUE(w->stats()->has_commands()) << "a walk is queued before the hit";

    w->stats()->hit_response(attacker.get());

    EXPECT_EQ(attacker.get(), w->foe()) << "we attack our attacker";
    EXPECT_EQ(w.get(), attacker->foe()) << "and the attacker is handed us back";
    EXPECT_EQ(32000u, w->stats()->last_distance())
        << "the distance cache is reset so the AI re-measures the new foe";
    EXPECT_EQ(32000, w->stats()->current_distance())
        << "the distance cache is reset so the AI re-measures the new foe";
    EXPECT_FALSE(w->stats()->has_commands())
        << "the old orders are cleared when a new enemy hits us";

    // Control: a second hit from the SAME attacker is not a retarget, so a
    // freshly queued order survives it.
    w->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    w->stats()->set_current_distance(9);
    w->stats()->hit_response(attacker.get());
    EXPECT_TRUE(w->stats()->has_commands())
        << "a hit from the current foe must not clear the queue";
    EXPECT_EQ(9, w->stats()->current_distance())
        << "a hit from the current foe must not reset the distance cache";
}


// ---------------------------------------------------------------------------
// set_command / try_command tests
// ---------------------------------------------------------------------------

TEST(StatsExtended, statistics_set_command_basic)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->set_command(COMMAND_WALK, 5, 1, 0);

    ASSERT_TRUE(!w->stats()->commands.empty()) << "set_command should add a command";
    ASSERT_EQ(COMMAND_WALK, w->stats()->commands.front().commandtype) << "should be walk";

}


TEST(StatsExtended, statistics_try_command_basic)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->stats()->commands.clear();
    w->stats()->try_command(COMMAND_WALK, 5, 1, 0);

    ASSERT_TRUE(!w->stats()->commands.empty()) << "try_command should add a command";

}


// ---------------------------------------------------------------------------
// command constructor test
// ---------------------------------------------------------------------------

TEST(StatsExtended, command_default_constructor)
{
    command c;
    ASSERT_EQ(0, c.commandtype) << "default commandtype should be 0";
    ASSERT_EQ(0, c.commandcount) << "default commandcount should be 0";
    ASSERT_EQ(0, c.com1) << "default com1 should be 0";
    ASSERT_EQ(0, c.com2) << "default com2 should be 0";
}


