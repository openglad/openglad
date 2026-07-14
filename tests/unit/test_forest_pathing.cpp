/* Single-floor forest-pathing regression tests (2026-07-10).
 *
 * Westlands L2 "The Forest Road" RCA follow-up: the multi-floor pathing wave
 * (L24) deliberately kept three classic single-floor defects behind
 * `floor_count() > 1` gates. L2 proved the same defect classes wedge
 * single-floor corridor mazes, so the gates were removed and two new fixes
 * landed. These tests pin each fix class WITHOUT floors:
 *
 *   1. A* no-corner-cut on single-floor grids (gameplay_context.cpp) — the
 *      graph must not emit diagonal cell hops past blocked flanking cells
 *      (pixel-impassable for a full-cell body; chasers seized on them).
 *   2. Follow-path alignment assist on single-floor grids
 *      (walker_pathing.cpp) — a misaligned body at a convex one-lane corner
 *      must slide into alignment instead of oscillating forever.
 *   3. Shove command-theft livelock (living.cpp) — a friendly shove must not
 *      clear the target's command queue to inject a walk the target provably
 *      cannot take (terrain-blocked); the stolen queue froze 3-walker
 *      columns on L2's one-cell grass strips for 350+ ticks.
 *   4. Guard facing gate + wake rule (walker.cpp act_guard +
 *      GameWorld::clear_sight_line) — an ACT_GUARD must not pivot to face a
 *      foe through a tree band, but must keep facing (and thus engaging)
 *      foes with a clear sight ray, including across water (projectiles fly
 *      over it). A genuine sighting (in range AND clear ray) also WAKES the
 *      guard (2026-07-11): unless guard_hold_post() is set, act_guard
 *      converts it to ACT_RANDOM — same tick still runs the classic
 *      face+fire, pursuit starts next tick. Hold-post guards keep the
 *      classic stationary-sentry behavior forever.
 *
 * Decor-blocked cells were investigated and REFUTED as a cause (PathingMap
 * uses the same query_grid_passable predicate movement uses, decor included),
 * so no decor test is needed here; tests/unit/test_decor.cpp covers the
 * decor plane itself.
 */
#include "../test_game_world_fixture.h"

#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/constants.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>

namespace {

// Paint a solid tree wall cell (blocks ground walkers, flyers pass crowns,
// weapons blocked — the Westlands corridor material).
void tree_at(GameWorld& w, int cx, int cy)
{
    w.grid.data[cx + cy * w.grid.w] = PIX_TREE_M1;
}

void water_at(GameWorld& w, int cx, int cy)
{
    w.grid.data[cx + cy * w.grid.w] = PIX_WATER1;
}

// Fill the whole default grid with uniform grass so maze geometry is exact.
void all_grass(GameWorld& w)
{
    const int n = w.grid.w * w.grid.h;
    for (int i = 0; i < n; ++i)
        w.grid.data[i] = PIX_GRASS1;
}

walker* spawn_living(GameWorld& w, int family, int team, int cx, int cy)
{
    walker* ob = w.add_ob(Order::Living, family);
    if (ob == nullptr)
        return nullptr;
    ob->set_team_num(static_cast<unsigned char>(team));
    ob->set_real_team_num(static_cast<unsigned char>(team));
    ob->setxy(static_cast<short>(cx * GRID_SIZE),
              static_cast<short>(cy * GRID_SIZE));
    ob->set_act_type(ACT_RANDOM);
    return ob;
}

// Drive `chaser` at `target` with COMMAND_SEARCH until within `stop_dist`
// pixels or `max_ticks` elapse. Records how often each grid cell is
// re-entered (the oscillation signature: a corner-wedged wanderer re-enters
// the same 2 cells hundreds of times). Returns the best distance reached.
int chase(walker* chaser, walker* target, int max_ticks, int stop_dist,
          int* max_cell_reentries)
{
    std::map<std::pair<int, int>, int> entries;
    int prev_cx = static_cast<int>(chaser->xpos()) / GRID_SIZE;
    int prev_cy = static_cast<int>(chaser->ypos()) / GRID_SIZE;
    entries[{prev_cx, prev_cy}] = 1;

    int best = 1 << 30;
    for (int t = 0; t < max_ticks; ++t)
    {
        chaser->act();
        if (chaser->dead())
            break;
        best = std::min(best,
                        static_cast<int>(chaser->distance_to_ob(target)));
        if (best < stop_dist)
            break;
        if (!chaser->foe() && !target->dead())
            chaser->set_foe(target);
        if (!chaser->stats()->has_commands() && chaser->foe())
            chaser->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

        const int cx = static_cast<int>(chaser->xpos()) / GRID_SIZE;
        const int cy = static_cast<int>(chaser->ypos()) / GRID_SIZE;
        if (cx != prev_cx || cy != prev_cy)
        {
            ++entries[{cx, cy}];
            prev_cx = cx;
            prev_cy = cy;
        }
    }

    if (max_cell_reentries != nullptr)
    {
        int worst = 0;
        for (const auto& kv : entries)
            worst = std::max(worst, kv.second);
        *max_cell_reentries = worst;
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------------------
// Fix class 1: single-floor A* no-corner-cut.
// ---------------------------------------------------------------------------

// Twin of ZAxis.multifloor_astar_never_emits_diagonals_past_blocked_flanks
// with NO extra floors: the rule must hold on plain single-floor levels now.
TEST(SingleFloorPathing, astar_never_emits_diagonals_past_blocked_flanks)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    ASSERT_EQ(1, w.floor_count());
    all_grass(w);

    // X-pinch at (10,10): west and north are trees, so the NW diagonal to
    // (9,9) is pixel-impassable at every offset; the only real route is
    // around, through row 11.
    tree_at(w, 9, 10);
    tree_at(w, 10, 9);

    w.rng_.state_ = 0x9E3779B9u;

    walker* orc = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* target = spawn_living(w, FAMILY_SOLDIER, 0, 3, 10);
    ASSERT_NE(orc, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(16, orc->sizex());
    if (target->stats())
        target->stats()->set_hitpoints(30000); // outlive the whole chase

    orc->set_foe(target);
    orc->find_path_to_foe();
    ASSERT_FALSE(orc->path_to_foe.empty())
        << "a walkable detour around the X-pinch exists; A* must find it";

    // The path must never contain a diagonal hop past a blocked flank.
    int prev_cx = static_cast<int>(orc->xpos()) / GRID_SIZE;
    int prev_cy = static_cast<int>(orc->ypos()) / GRID_SIZE;
    for (void* state : orc->path_to_foe)
    {
        const int cx = GET_STATE_X(state) / GRID_SIZE;
        const int cy = GET_STATE_Y(state) / GRID_SIZE;
        if (std::abs(cx - prev_cx) == 1 && std::abs(cy - prev_cy) == 1)
        {
            EXPECT_TRUE(w.query_grid_passable(
                static_cast<float>(cx * GRID_SIZE),
                static_cast<float>(prev_cy * GRID_SIZE), orc, 0))
                << "diagonal hop (" << prev_cx << "," << prev_cy << ")->("
                << cx << "," << cy << ") cuts a blocked x-flank corner";
            EXPECT_TRUE(w.query_grid_passable(
                static_cast<float>(prev_cx * GRID_SIZE),
                static_cast<float>(cy * GRID_SIZE), orc, 0))
                << "diagonal hop (" << prev_cx << "," << prev_cy << ")->("
                << cx << "," << cy << ") cuts a blocked y-flank corner";
        }
        prev_cx = cx;
        prev_cy = cy;
    }

    // End to end: the orc must actually get around the pinch to its foe.
    orc->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    const int best = chase(orc, target, 2000, 4 * GRID_SIZE, nullptr);
    EXPECT_LT(best, 4 * GRID_SIZE)
        << "orc never made it around the X-pinch to its foe — it is seized "
           "on an impossible diagonal edge";
}

// ---------------------------------------------------------------------------
// Fix class 2: single-floor follow-path alignment assist.
// ---------------------------------------------------------------------------

// Twin of ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner with NO
// extra floors, walled in trees. Pre-fix: walkstep's fixed fallback bounced
// the misaligned body out of alignment every other tick — a deterministic
// oscillation that pinned the chaser at the corner forever.
TEST(SingleFloorPathing, misaligned_walker_aligns_into_a_one_lane_tree_gap)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    ASSERT_EQ(1, w.floor_count());
    all_grass(w);

    // A single-file lane along row 10: rows 9 and 11 are solid tree wall,
    // except (10,11) — the cell the chaser's misaligned body spills into.
    for (int x = 0; x < w.grid.w; ++x)
    {
        tree_at(w, x, 9);
        if (x != 10)
            tree_at(w, x, 11);
    }

    w.rng_.state_ = 0x9E3779B9u;

    walker* orc = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* target = spawn_living(w, FAMILY_SOLDIER, 0, 4, 10);
    ASSERT_NE(orc, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(16, orc->sizex());
    ASSERT_EQ(16, orc->sizey());

    // 14px below the lane line: body spans rows 10 AND 11. Every westward
    // step is pixel-blocked by the tree at (9,11) until the body is EXACTLY
    // aligned into row 10.
    orc->setxy(static_cast<short>(10 * GRID_SIZE),
               static_cast<short>(10 * GRID_SIZE + 14));
    if (target->stats())
        target->stats()->set_hitpoints(30000);

    orc->set_foe(target);
    orc->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int reentries = 0;
    const int best = chase(orc, target, 1500, 4 * GRID_SIZE, &reentries);
    EXPECT_LT(best, 4 * GRID_SIZE)
        << "orc never aligned into the one-tile tree gap — the corner-wedge "
           "oscillation is back";
    EXPECT_LE(reentries, 10)
        << "orc re-entered a single cell " << reentries
        << " times — corner oscillation signature";
}

// Task-level end-to-end pin: an orc that spawns behind a tree wall must
// navigate a two-baffle tree maze (turns, a one-lane gap, and an X-pinch at
// the second gap) and reach a foe on the far side within budget, without the
// cell-revisit signature of a corner wedge.
TEST(SingleFloorPathing, orc_behind_tree_wall_reaches_foe_across_the_maze)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    ASSERT_EQ(1, w.floor_count());
    all_grass(w);

    // Corridor box rows 7..21, cols 3..21 (tree border), with two vertical
    // baffles: x=15 open only at row 20 (south gap), x=9 open only at row 8
    // (north gap). The north gap is an X-pinch: (8,7) and (10,7) borders plus
    // trees at (8,9) and (10,9) leave diagonal-looking shortcuts that a
    // full-cell body cannot take.
    for (int x = 3; x <= 21; ++x)
    {
        tree_at(w, x, 7);
        tree_at(w, x, 21);
    }
    for (int y = 7; y <= 21; ++y)
    {
        tree_at(w, 3, y);
        tree_at(w, 21, y);
    }
    for (int y = 8; y <= 21; ++y)
        if (y != 20)
            tree_at(w, 15, y);
    for (int y = 9; y <= 21; ++y)
        tree_at(w, 9, y);
    tree_at(w, 8, 9);
    tree_at(w, 10, 9);

    w.rng_.state_ = 0x9E3779B9u;

    walker* orc = spawn_living(w, FAMILY_ORC, 1, 18, 9);
    walker* target = spawn_living(w, FAMILY_SOLDIER, 0, 5, 12);
    ASSERT_NE(orc, nullptr);
    ASSERT_NE(target, nullptr);
    if (target->stats())
        target->stats()->set_hitpoints(30000);

    orc->set_foe(target);
    orc->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int reentries = 0;
    const int best = chase(orc, target, 2500, 4 * GRID_SIZE, &reentries);
    EXPECT_LT(best, 4 * GRID_SIZE)
        << "orc failed to cross the tree maze to its foe (best distance "
        << best << "px)";
    EXPECT_LE(reentries, 10)
        << "orc re-entered a single cell " << reentries
        << " times crossing the maze — oscillation signature";
}

// ---------------------------------------------------------------------------
// Fix class 3: shove command-theft livelock.
// ---------------------------------------------------------------------------

// Direct pin: a friendly shove whose injected walk is terrain-blocked must
// NOT clear the target's queue. Geometry from the L2 s1337 capture: target
// flush against a tree wall below and to the right (the one-cell grass strip
// at bend-1), shover marching down into it every tick.
TEST(ShoveLivelock, wall_blocked_shove_does_not_steal_the_targets_queue)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    // The strip: trees below (26,20) and east (27,19) of the target.
    tree_at(w, 26, 20);
    tree_at(w, 27, 19);

    w.rng_.state_ = 0x9E3779B9u;

    walker* shover = spawn_living(w, FAMILY_SOLDIER, 1, 26, 18);
    walker* target = spawn_living(w, FAMILY_SOLDIER, 1, 26, 19);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 20, 19);
    ASSERT_NE(shover, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(foe, nullptr);

    // The target runs its own search; the queue must survive the shove storm.
    target->set_foe(foe);
    target->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    ASSERT_FALSE(target->stats()->commands.empty());
    ASSERT_EQ(COMMAND_SEARCH, target->stats()->commands.front().commandtype);

    shover->set_curdir(static_cast<signed char>(FACE_DOWN));
    for (int t = 0; t < 30; ++t)
    {
        // March down into the target: blocked -> living::walk() -> shove().
        // rng passes the 2/3 shove roll ~20 times over 30 ticks; classic
        // stole the queue on the first pass.
        shover->walkstep(0, 1);
        ASSERT_FALSE(target->stats()->commands.empty());
        EXPECT_EQ(COMMAND_SEARCH, target->stats()->commands.front().commandtype)
            << "tick " << t
            << ": wall-blocked shove replaced the target's own search with "
               "an untakeable COMMAND_WALK (the L2 column livelock)";
    }
}

// Teeth check for the probe: on OPEN ground the classic shove behavior must
// be preserved byte-for-byte — the queue IS stolen and a walk injected
// (crowds still scoot; only provably-untakeable injections are skipped).
TEST(ShoveLivelock, open_ground_shove_still_shoves)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    w.rng_.state_ = 0x9E3779B9u;

    walker* shover = spawn_living(w, FAMILY_SOLDIER, 1, 26, 18);
    walker* target = spawn_living(w, FAMILY_SOLDIER, 1, 26, 19);
    ASSERT_NE(shover, nullptr);
    ASSERT_NE(target, nullptr);

    // Idle target (no commands): the injected COMMAND_WALK is visible.
    shover->set_curdir(static_cast<signed char>(FACE_DOWN));
    bool shoved = false;
    for (int t = 0; t < 50 && !shoved; ++t)
    {
        shover->walkstep(0, 1);
        shoved = !target->stats()->commands.empty() &&
                 target->stats()->commands.front().commandtype == COMMAND_WALK;
        // The injected walk moves the target; keep the shover pressing.
    }
    EXPECT_TRUE(shoved)
        << "shove never injected a walk on open ground — the passability "
           "probe is over-blocking and has neutered crowd scooting";
}

// Emergent pin: the L2 s1337 column wedge, reduced to its load-bearing
// three bodies. The pocketed walker sits on the one-cell grass strip with
// trees below and east and the open road west (bend-1 geometry); an allied
// presser marches down into it EVERY tick, starting before the pocketed
// walker's own AI has ever run (the capture ordering). Classic: each blocked
// walk shoved the pocketed walker with a wall-blocked COMMAND_WALK,
// clearing/refilling its queue every tick, so its own search never ran and
// it froze in place for 350+ ticks. Fixed: the theft is skipped, its own AI
// acquires the foe, re-paths onto the road, and it walks out immediately.
TEST(ShoveLivelock, pressed_column_against_tree_wall_unwedges)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    // The pocket: trees below (26,20) and east (27,19); (25,19) is the road.
    tree_at(w, 26, 20);
    tree_at(w, 27, 19);

    w.rng_.state_ = 0x9E3779B9u;

    walker* presser = spawn_living(w, FAMILY_SOLDIER, 1, 26, 18);
    walker* pocketed = spawn_living(w, FAMILY_SOLDIER, 1, 26, 19);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 22, 19);
    ASSERT_NE(presser, nullptr);
    ASSERT_NE(pocketed, nullptr);
    ASSERT_NE(foe, nullptr);
    if (foe->stats())
        foe->stats()->set_hitpoints(30000);

    presser->set_curdir(static_cast<signed char>(FACE_DOWN));
    presser->set_enddir(static_cast<char>(FACE_DOWN));

    int escape_tick = -1;
    int best = 1 << 30;
    for (int t = 0; t < 400; ++t)
    {
        // The presser's blocked march lands first each tick — under classic
        // rules its shove steals the pocketed walker's queue before that
        // walker ever gets to act.
        presser->walkstep(0, 1);
        if (!pocketed->dead())
        {
            pocketed->act();
            if (!pocketed->foe() && !foe->dead())
                pocketed->set_foe(foe);
        }
        if (escape_tick < 0 &&
            (static_cast<int>(pocketed->xpos()) / GRID_SIZE != 26 ||
             static_cast<int>(pocketed->ypos()) / GRID_SIZE != 19))
        {
            escape_tick = t;
        }
        best = std::min(best,
                        static_cast<int>(pocketed->distance_to_ob(foe)));
        if (best < 3 * GRID_SIZE)
            break;
    }

    EXPECT_GE(escape_tick, 0)
        << "the pocketed walker never left its cell — shove command theft "
           "has re-frozen the column";
    // With its queue intact the pocketed walker's own AI walks it out almost
    // immediately; under classic theft it stays frozen while the presser
    // refills the untakeable walk every tick.
    EXPECT_LT(escape_tick, 60)
        << "the pocketed walker took " << escape_tick
        << " ticks to leave the pocket — command theft signature";
    // Engagement range: a soldier closes to knife range (~44px), not bump
    // range. The frozen classic body never leaves 64px (its pocket cell).
    EXPECT_LT(best, 3 * GRID_SIZE)
        << "the pocketed walker never reached its foe down the road (best "
        << best << "px)";
}

// ---------------------------------------------------------------------------
// Fix class 4: guard facing gate.
// ---------------------------------------------------------------------------

// A guard posted beside a tree band must NOT pivot to face the 2D-nearest
// foe through the wall (the "orc nose-to-trees" statue look).
TEST(GuardFacing, guard_does_not_pivot_to_face_foe_through_tree_band)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    // Vertical tree band between guard (10,10) and foe (14,10).
    for (int y = 6; y <= 14; ++y)
        tree_at(w, 12, y);

    w.rng_.state_ = 0x9E3779B9u;

    walker* guard = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 14, 10);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(foe, nullptr);

    guard->set_act_type(ACT_GUARD);
    guard->set_lineofsight(20); // orc family sight range, in cells
    guard->set_curdir(static_cast<signed char>(FACE_LEFT));
    guard->set_enddir(static_cast<char>(FACE_LEFT));

    // With curdir == enddir the living turn step never rotates the guard, so
    // ONLY act_guard's facing pivot could move curdir. It must not — and the
    // wake rule shares the same range+ray test, so a wall-blocked foe must
    // never wake the guard either.
    for (int t = 0; t < 12; ++t)
    {
        guard->act();
        EXPECT_EQ(FACE_LEFT, guard->curdir())
            << "tick " << t
            << ": guard pivoted to face a foe it cannot see through the "
               "tree band";
        EXPECT_EQ(ACT_GUARD, guard->act_type())
            << "tick " << t
            << ": guard woke on a foe it cannot see through the tree band";
    }
    ASSERT_NE(guard->foe(), nullptr)
        << "guard never acquired the foe — the facing gate must not touch "
           "foe acquisition";
}

// With a clear ray the guard must keep the classic behavior: turn to track.
// The same genuine sighting also wakes it into ACT_RANDOM pursuit.
TEST(GuardFacing, guard_faces_foe_with_clear_sight)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    w.rng_.state_ = 0x9E3779B9u;

    walker* guard = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 14, 10);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(foe, nullptr);

    guard->set_act_type(ACT_GUARD);
    guard->set_lineofsight(20);
    guard->set_curdir(static_cast<signed char>(FACE_LEFT));
    guard->set_enddir(static_cast<char>(FACE_LEFT));

    // act_guard snap-faces the foe on its acting ticks (the living turn step
    // rotates back toward enddir between them — the classic facing flip), so
    // assert the guard EVER faces the foe. The same genuine sighting must
    // also wake the guard: act_guard converts it to ACT_RANDOM (2026-07-11
    // wake rule) so pursuit starts on the next tick.
    bool faced = false;
    for (int t = 0; t < 12 && !faced; ++t)
    {
        guard->act();
        faced = guard->curdir() == FACE_RIGHT;
    }
    ASSERT_NE(guard->foe(), nullptr);
    EXPECT_TRUE(faced)
        << "guard failed to face a foe in plain sight — the gate is "
           "over-blocking";
    EXPECT_EQ(ACT_RANDOM, guard->act_type())
        << "a clear-sight foe in range must wake the guard into ACT_RANDOM";
}

// Water is sight-transparent (projectiles fly over it): ranged guards on the
// far bank must keep tracking, or island archers would stop shooting.
TEST(GuardFacing, guard_faces_foe_across_water)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    for (int y = 6; y <= 14; ++y)
        water_at(w, 12, y);

    w.rng_.state_ = 0x9E3779B9u;

    walker* guard = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 14, 10);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(foe, nullptr);

    guard->set_act_type(ACT_GUARD);
    guard->set_lineofsight(20);
    guard->set_curdir(static_cast<signed char>(FACE_LEFT));
    guard->set_enddir(static_cast<char>(FACE_LEFT));

    bool faced = false;
    for (int t = 0; t < 12 && !faced; ++t)
    {
        guard->act();
        faced = guard->curdir() == FACE_RIGHT;
    }
    ASSERT_NE(guard->foe(), nullptr);
    EXPECT_TRUE(faced)
        << "guard refused to face a foe across water — water must stay "
           "sight-transparent";
    EXPECT_EQ(ACT_RANDOM, guard->act_type())
        << "water is sight-transparent, so the sighting must wake the guard "
           "too";
}

// Beyond the family sight range the guard keeps its post facing even on
// open ground (the far-foe facing spins from the L2 film).
TEST(GuardFacing, guard_ignores_foe_beyond_sight_range)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    w.rng_.state_ = 0x9E3779B9u;

    walker* guard = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 10, 40);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(foe, nullptr);

    guard->set_act_type(ACT_GUARD);
    guard->set_lineofsight(20); // 20 cells; the foe is 30 cells away
    guard->set_curdir(static_cast<signed char>(FACE_LEFT));
    guard->set_enddir(static_cast<char>(FACE_LEFT));

    for (int t = 0; t < 12; ++t)
    {
        guard->act();
        if (guard->foe() != nullptr)
        {
            EXPECT_EQ(FACE_LEFT, guard->curdir())
                << "tick " << t
                << ": guard pivoted to face a foe beyond its sight range";
        }
        EXPECT_EQ(ACT_GUARD, guard->act_type())
            << "tick " << t
            << ": guard woke on a foe beyond its sight range — no wake "
               "without a genuine sighting";
    }
}

// Hold-post policy (npc_flags bit 1): a guard_hold_post() guard NEVER wakes,
// no matter how long a clear-sight foe stands in range — it is the classic
// stationary sentry, still facing and still firing from its post.
TEST(GuardFacing, hold_post_guard_keeps_its_post_but_still_faces_and_fires)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    w.rng_.state_ = 0x9E3779B9u;

    walker* guard = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 14, 10);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(foe, nullptr);
    if (foe->stats())
        foe->stats()->set_hitpoints(30000); // outlive the fire cycles

    guard->set_act_type(ACT_GUARD);
    guard->set_guard_hold_post(true);
    guard->set_lineofsight(20);
    guard->set_curdir(static_cast<signed char>(FACE_LEFT));
    guard->set_enddir(static_cast<char>(FACE_LEFT));

    const float post_x = guard->xpos();
    const float post_y = guard->ypos();

    bool faced = false;
    bool tried_fire = false;
    for (int t = 0; t < 40; ++t)
    {
        guard->act();
        faced = faced || guard->curdir() == FACE_RIGHT;
        tried_fire = tried_fire ||
                     (!guard->stats()->commands.empty() &&
                      guard->stats()->commands.front().commandtype ==
                          COMMAND_FIRE);
        ASSERT_EQ(ACT_GUARD, guard->act_type())
            << "tick " << t
            << ": a hold-post guard must NEVER wake, even with a clear-sight "
               "foe in range";
        EXPECT_EQ(post_x, guard->xpos())
            << "tick " << t << ": a hold-post guard must never leave its post";
        EXPECT_EQ(post_y, guard->ypos())
            << "tick " << t << ": a hold-post guard must never leave its post";
    }
    ASSERT_NE(guard->foe(), nullptr);
    EXPECT_TRUE(faced)
        << "hold-post must not disable the classic facing snap";
    EXPECT_TRUE(tried_fire)
        << "hold-post must not disable the classic COMMAND_FIRE attempt";
}

// Wake rule end to end: a plain (non-hold-post) guard that genuinely sights
// a foe converts to ACT_RANDOM and, driven only by its own AI from then on,
// leaves its post and closes to engagement range.
TEST(GuardFacing, woken_guard_pursues_its_foe)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    all_grass(w);

    w.rng_.state_ = 0x9E3779B9u;

    walker* guard = spawn_living(w, FAMILY_ORC, 1, 10, 10);
    walker* foe = spawn_living(w, FAMILY_SOLDIER, 0, 16, 10);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(foe, nullptr);
    if (foe->stats())
        foe->stats()->set_hitpoints(30000); // outlive the whole pursuit

    guard->set_act_type(ACT_GUARD);
    guard->set_lineofsight(20);
    guard->set_curdir(static_cast<signed char>(FACE_LEFT));
    guard->set_enddir(static_cast<char>(FACE_LEFT));

    // The wake tick: act_guard still runs the classic face+fire on the very
    // tick it converts the act type.
    guard->act();
    EXPECT_EQ(FACE_RIGHT, guard->curdir())
        << "the wake tick must still run the classic facing snap";
    EXPECT_TRUE(!guard->stats()->commands.empty() &&
                guard->stats()->commands.front().commandtype == COMMAND_FIRE)
        << "the wake tick must still run the classic fire attempt";
    ASSERT_EQ(ACT_RANDOM, guard->act_type())
        << "a clear-sight foe in range must wake the guard";

    // Pursuit: no injected commands — the woken guard's own ACT_RANDOM AI
    // (search / walk-toward-foe) must leave the post and close in.
    const std::int32_t d0 = guard->distance_to_ob(foe);
    std::int32_t best = d0;
    for (int t = 0; t < 400 && best >= 3 * GRID_SIZE; ++t)
    {
        guard->act();
        if (guard->dead())
            break;
        best = std::min(best, guard->distance_to_ob(foe));
    }
    EXPECT_LT(best, 3 * GRID_SIZE)
        << "the woken guard never pursued its foe (started at " << d0
        << "px, best " << best << "px)";
    EXPECT_TRUE(guard->xpos() != static_cast<float>(10 * GRID_SIZE) ||
                guard->ypos() != static_cast<float>(10 * GRID_SIZE))
        << "pursuit must actually leave the post";
}
