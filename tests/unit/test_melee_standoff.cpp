/* F1 regression tests: the guard-standoff melee deadlock
 * (docs/GAMEPLAY_FIXES_FROM_CLASSIC.md, 2026-07-07).
 *
 * The classic COMMAND_ATTACK loop answered EVERY denied fire_check() with
 * "walk toward the foe". With the foe's own body blocking that step,
 * walkstep()'s blocked-NPC branch set curdir PERPENDICULAR (the wall-slide),
 * the next tick's approach delta re-collapsed to a different octant, and two
 * adjacent fighters could face-dance forever without one swing landing —
 * Westlands L25's eternal last foe and L2's companies parked mid-road.
 *
 * The fix (three cooperating deterministic rules, no RNG):
 *  - COMMAND_ATTACK snap-face: when the shot at an in-reach foe is denied
 *    only by orientation (Facing, or NoRanged at bump range), face the foe
 *    via walker::face_delta() instead of walking (stats.cpp);
 *  - interpenetration escape (obmap.cpp): a transient pass-through that
 *    leaves two livings overlapped was an ABSORBING trap (every exit step
 *    still "collided" with the body around the mover) that turned pairs into
 *    unkillable shields; strictly separating moves now pass;
 *  - coincident clinch breaker (stats.cpp): a foe at EXACTLY our position
 *    collapses the approach delta to (0,0), which the classic loop answered
 *    with walkstep(0,0) — a no-op freezing the clinch; step along curdir.
 *
 * Tests here:
 *  1. the primitive: a Facing denial snaps curdir/enddir onto the foe in one
 *     tick without moving (pre-fix: one 45-degree turn step, or a slide);
 *  2. non-Facing denials still walk (the fix must not overreach);
 *  3. the brief's headline: adjacent soldier vs guard-orc RESOLVES;
 *  4. the real level (Wave E repro): Westlands L2's crew fights past
 *     mid-road and consumes the bend-1 picket instead of parking on it.
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"
#include "westlands_sim_fixture.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Synthetic fixtures
// ---------------------------------------------------------------------------

struct StandoffPair
{
    walker* attacker = nullptr;
    walker* guard = nullptr;
};

// A production-stat soldier (team 0, brawler AI) and a guard-orc (team 2,
// ACT_GUARD) placed dx/dy apart (attacker at the 10,10 tile).
StandoffPair make_pair(GameWorld& w, float dx, float dy)
{
    StandoffPair p;
    p.attacker = w.add_ob(Order::Living, FAMILY_SOLDIER);
    p.guard = w.add_ob(Order::Living, FAMILY_ORC);
    if (p.attacker == nullptr || p.guard == nullptr)
        return p;

    p.attacker->setxy(10 * GRID_SIZE, 10 * GRID_SIZE);
    p.attacker->set_team_num(0);
    p.attacker->set_real_team_num(0);
    p.attacker->set_act_type(ACT_RANDOM);
    p.attacker->stats()->set_level(5);
    p.attacker->set_difficulty(5); // production-accurate derived stats

    p.guard->setxy(10 * GRID_SIZE + dx, 10 * GRID_SIZE + dy);
    p.guard->set_team_num(2);
    p.guard->set_real_team_num(2);
    p.guard->set_act_type(ACT_GUARD);
    // These tests pin the F1 standoff fix against a STATIONARY guard post
    // (the wedge geometry). The newer guard wake rule (walker.cpp act_guard,
    // 2026-07-11) would convert a sighted guard to ACT_RANDOM pursuit and
    // dissolve the post, so author the hold-post policy bit — the classic
    // sentry the F1 fix was measured against.
    p.guard->set_guard_hold_post(true);
    p.guard->stats()->set_level(2);
    p.guard->set_difficulty(2);
    return p;
}

// ---------------------------------------------------------------------------
// Westlands campaign fixture: shared with test_westlands_calibration.cpp
// (tests/unit/westlands_sim_fixture.h) — mount + load + text-protocol-equal
// crew deployment.
// ---------------------------------------------------------------------------
using WestlandsStandoffTest = westlands_fixture::WestlandsCampaignTest;
using LoadedStandoffLevel = westlands_fixture::LoadedWestlandsLevel;
using westlands_fixture::alive_livings_on_team;
using westlands_fixture::deploy_crew;

} // namespace

// ---------------------------------------------------------------------------
// (1) The primitive: a Facing-denied COMMAND_ATTACK snaps onto the foe in ONE
// tick, without moving. Pre-fix the same tick either turned a single
// 45-degree step (walk's changed-direction branch) or slid perpendicular, so
// curdir after the tick never equalled the foe octant from two steps away.
// ---------------------------------------------------------------------------
TEST(MeleeStandoff, facing_denial_snaps_to_face_the_foe_in_one_tick)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 1u;
    w.my_team = 0;

    StandoffPair p = make_pair(w, 18.0f, 0.0f); // foe due RIGHT, in reach
    ASSERT_NE(nullptr, p.attacker);
    ASSERT_NE(nullptr, p.guard);

    // Point the attacker two octants off the foe, mid-attack.
    p.attacker->set_curdir(static_cast<signed char>(FACE_UP));
    p.attacker->set_enddir(static_cast<char>(FACE_UP));
    p.attacker->set_foe(p.guard);
    p.attacker->stats()->force_command(COMMAND_ATTACK, 30, 1, 0);

    const float x0 = p.attacker->xpos();
    const float y0 = p.attacker->ypos();
    w.tick();

    EXPECT_EQ(FACE_RIGHT, static_cast<int>(p.attacker->curdir()))
        << "a Facing denial at melee range must snap curdir onto the foe";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(p.attacker->enddir()))
        << "enddir must snap too, or act()'s pre-turn undoes the face";
    EXPECT_EQ(x0, p.attacker->xpos()) << "face_delta never moves the walker";
    EXPECT_EQ(y0, p.attacker->ypos()) << "face_delta never moves the walker";

    // And the swing lands: the very next attack cycle damages the guard.
    const float guard_hp = p.guard->stats()->hitpoints();
    for (int t = 0; t < 30 && !p.guard->dead(); ++t)
        w.tick();
    EXPECT_LT(p.guard->stats()->hitpoints(), guard_hp)
        << "facing the foe must convert into a landed hit";
}

// ---------------------------------------------------------------------------
// (2) The fix must not overreach: any non-Facing denial (here OutOfRange)
// keeps the classic walk-toward-the-foe response.
// ---------------------------------------------------------------------------
TEST(MeleeStandoff, out_of_range_denial_still_walks_toward_the_foe)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.rng_.state_ = 1u;
    w.my_team = 0;

    StandoffPair p = make_pair(w, 120.0f, 0.0f); // far beyond knife reach
    ASSERT_NE(nullptr, p.attacker);
    ASSERT_NE(nullptr, p.guard);

    p.attacker->set_curdir(static_cast<signed char>(FACE_RIGHT));
    p.attacker->set_enddir(static_cast<char>(FACE_RIGHT));
    p.attacker->set_foe(p.guard);
    p.attacker->stats()->force_command(COMMAND_ATTACK, 30, 1, 0);

    const float x0 = p.attacker->xpos();
    for (int t = 0; t < 6; ++t)
        w.tick();
    EXPECT_GT(p.attacker->xpos(), x0)
        << "an out-of-reach foe must still be approached on foot";
}

// ---------------------------------------------------------------------------
// (3) The brief's headline: an adjacent attacker-vs-guard pair RESOLVES.
// Every adjacency (cardinal, diagonal, touching) must end with the guard
// dead within 400 ticks — no eternal 2px standoff.
// ---------------------------------------------------------------------------
TEST(MeleeStandoff, adjacent_soldier_vs_guard_orc_resolves)
{
    const struct
    {
        const char* label;
        float dx;
        float dy;
    } probes[] = {
        {"right", 18.0f, 0.0f},          {"up", 0.0f, -18.0f},
        {"diag down-right", 18.0f, 18.0f}, {"diag up-left", -18.0f, -18.0f},
        {"touching diag", 16.0f, 16.0f},
    };
    for (const auto& probe : probes)
    {
        SCOPED_TRACE(probe.label);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.rng_.state_ = 1u;
        w.my_team = 0;

        StandoffPair p = make_pair(w, probe.dx, probe.dy);
        ASSERT_NE(nullptr, p.attacker);
        ASSERT_NE(nullptr, p.guard);

        int resolved_at = -1;
        for (int t = 0; t < 400; ++t)
        {
            w.tick();
            if (p.guard->dead())
            {
                resolved_at = t;
                break;
            }
        }
        EXPECT_NE(-1, resolved_at)
            << "wedged: the guard still lives after 400 ticks (hp "
            << p.guard->stats()->hitpoints() << ")";
        EXPECT_FALSE(p.attacker->dead())
            << "the level-5 soldier must win this matchup";
    }
}

// ---------------------------------------------------------------------------
// (4) The real level (the brief's L2 repro): Westlands scen2 "The Forest
// Road" is a fight-through gauntlet whose Wave E report documented AI
// companies parking forever at the first guard post they could not shift —
// the repro pair being the bend-1 picket wolf at tile (23,17) = pixel
// (368,272) with a crew soldier wedged 2px off it (and eventually merged
// INTO it via the interpenetration trap). Post-fix, an entry-power 4-soldier
// crew must fight past mid-road quickly, exterminate the road (picket
// included), and keep everyone alive. Pre-fix baselines for these seeds:
// seed 42 crew WIPED at maxx=896 with 9 foes left; seeds 1337/2025 crossed
// mid-road late (t=872+) with a soldier lost each — every seed fails at
// least one clause below without the fix.
// ---------------------------------------------------------------------------
TEST_F(WestlandsStandoffTest, l2_forest_road_crew_fights_past_mid_road)
{
    constexpr float kMidRoadX = 720.0f;   // 45 of 90 tiles
    constexpr int kMidRoadDeadline = 1200; // post-fix: 538-673 across seeds
    constexpr int kSweepDeadline = 8000;   // post-fix: full road swept

    for (std::uint32_t seed : {42u, 1337u, 2025u})
    {
        SCOPED_TRACE("seed " + std::to_string(seed));
        LoadedStandoffLevel fx(2, seed);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();

        // The backlog's repro coordinates: the bend-1 picket guard post.
        // The shipped level authors its team-2 guards as WAKING ambushers
        // (guard wake rule, walker.cpp act_guard 2026-07-11): on a genuine
        // sighting they convert to ACT_RANDOM and charge. This test pins the
        // F1 standoff fix against POSTED guards — the crew must consume a
        // stationary picket instead of parking on it, at the timings the
        // pre-wake baselines were measured under — so re-post every road
        // guard as a hold-post sentry before the sim runs.
        walker* picket = nullptr;
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->query_order() != Order::Living)
                continue;
            if (ob->team_num() == 2 && ob->act_type() == ACT_GUARD)
                ob->set_guard_hold_post(true);
            if (ob->team_num() == 2 && ob->xpos() == 23 * GRID_SIZE &&
                ob->ypos() == 17 * GRID_SIZE)
                picket = ob;
        }
        ASSERT_NE(nullptr, picket) << "the bend-1 picket at (368,272)";
        ASSERT_EQ(ACT_GUARD, picket->act_type());
        ASSERT_TRUE(picket->guard_hold_post())
            << "the picket must be re-posted for the wedge geometry to exist";

        std::vector<walker*> crew =
            deploy_crew(fx.level, world, {0, 0, 0, 0}, 2);
        ASSERT_EQ(4u, crew.size());

        float maxx = 0.0f;
        int past_mid_at = -1;
        int swept_at = -1;
        for (int t = 0; t < kSweepDeadline; ++t)
        {
            world.tick();
            for (walker* w : crew)
                if (w != nullptr && !w->dead() &&
                    static_cast<float>(w->xpos()) > maxx)
                    maxx = static_cast<float>(w->xpos());
            if (past_mid_at < 0 && maxx > kMidRoadX)
                past_mid_at = t;
            if (swept_at < 0 && alive_livings_on_team(world, 2) == 0)
            {
                swept_at = t;
                break;
            }
        }

        EXPECT_TRUE(past_mid_at >= 0 && past_mid_at <= kMidRoadDeadline)
            << "the crew parked before mid-road (past_mid_at=" << past_mid_at
            << ", maxx=" << maxx << ") — the guard-standoff wedge is back";
        EXPECT_NE(-1, swept_at)
            << "the road was never swept: a foe is wedged (picket hp "
            << picket->stats()->hitpoints() << ")";
        EXPECT_TRUE(picket->dead())
            << "the bend-1 picket at (368,272) survived — the exact Wave E "
               "wedge signature";
        int alive = 0;
        for (walker* w : crew)
            if (w != nullptr && !w->dead())
                alive++;
        EXPECT_EQ(4, alive)
            << "resolved fights must not cost the crew members it never "
               "lost with the fix in place";
    }
}
