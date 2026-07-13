/* Z-axis / multi-floor headless mechanics tests.
 *
 * These exercise the multi-floor sim directly (no SDL): per-floor collision
 * separation, fall-through-air, flyer hover, and Z-stair transitions. The
 * single-floor byte-identity of all this is covered by og_test_parity; here we
 * prove the multi-floor behavior itself functions. See docs/z-axis-design.md.
 */
#include "../test_game_world_fixture.h"

#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/constants.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

// Replace floor 1's grid with a grass field of floor 0's dimensions, painting
// `tile` at grid cell (tx,ty). PixieData takes ownership of the heap buffer.
void set_floor1_grid(GameWorld& w, int tx, int ty, unsigned char tile)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh,
              static_cast<unsigned char>(PIX_GRASS1));
    if (tx >= 0 && ty >= 0 && tx < gw && ty < gh)
        buf[tx + ty * gw] = tile;
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
}

// The grid cell under an entity's center (matches apply_z_motion's probe).
void center_cell(const walker* w, int& cx, int& cy)
{
    cx = (w->xpos() + w->sizex() / 2) / GRID_SIZE;
    cy = (w->ypos() + w->sizey() / 2) / GRID_SIZE;
}

// Replace floor 1's grid with all-AIR (a realistic sparse upper floor), then
// hand it back so the caller can paint grass platforms / stairs on top.
unsigned char* set_floor1_all_air(GameWorld& w)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh,
              static_cast<unsigned char>(PIX_AIR));
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    return w.grid_for_floor(1).data.get();
}

// Replace floor 1's grid with a uniform `tile` fill (e.g. all walls); returns
// the buffer so callers can paint individual cells on top.
unsigned char* set_floor1_fill(GameWorld& w, unsigned char tile)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh, tile);
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    return w.grid_for_floor(1).data.get();
}

} // namespace

TEST(ZAxis, floor_count_defaults_to_one)
{
    TestGameWorld tw;
    EXPECT_EQ(1, tw.world().floor_count());
    EXPECT_FALSE(tw.world().is_multifloor());
}

TEST(ZAxis, collision_is_floor_separated)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    ASSERT_EQ(2, w.floor_count());

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    a->set_floor(0);
    a->setxy(100, 100);
    b->set_floor(1);
    b->setxy(100, 100); // same 2D spot, different floor

    EXPECT_TRUE(w.query_object_passable(100, 100, a))
        << "entities on different floors must not collide";

    b->change_floor(0); // bring b down onto a's floor + cell
    EXPECT_EQ(0, b->floor());
    EXPECT_FALSE(w.query_object_passable(100, 100, a))
        << "overlapping entities on the same floor must collide";
}

TEST(ZAxis, walker_falls_through_air)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    a->set_floor(1);
    a->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);

    int cx, cy;
    center_cell(a, cx, cy);
    set_floor1_grid(w, cx, cy, PIX_AIR); // air hole exactly under the walker

    ASSERT_EQ(1, a->floor());
    a->apply_z_motion();
    EXPECT_EQ(0, a->floor())
        << "a non-flyer standing over air must fall to the floor below";
}

TEST(ZAxis, flyer_hovers_over_air)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    a->stats()->set_bit_flags(BIT_FLYING, 1);
    a->set_floor(1);
    a->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);

    int cx, cy;
    center_cell(a, cx, cy);
    set_floor1_grid(w, cx, cy, PIX_AIR);

    a->apply_z_motion();
    EXPECT_EQ(1, a->floor())
        << "a flyer hovers over air and never changes floors";
}

TEST(ZAxis, zstair_up_moves_to_floor_above)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);

    int cx, cy;
    center_cell(a, cx, cy);
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP; // paint stair under it
    // Author the destination floor (all grass): set_floor_count leaves extra
    // floors' grids for the caller, and the landing validation correctly
    // refuses to transition onto a floor with no grid.
    set_floor1_grid(w, cx, cy, PIX_GRASS1);

    a->apply_z_motion();
    EXPECT_EQ(1, a->floor())
        << "stepping on a ZSTAIR_UP should move up one floor";
}

// Cross-floor AI chase (concept level 602 "Glasshouse" in miniature): an enemy
// that has acquired a foe one floor up must route to a Z-stair, climb it, and
// close on the target — NOT run "under" the target on its own floor. This drives
// the FULL per-tick AI act() loop so it exercises the whole chain together:
//   - walk_to_foe()'s cross-floor gate (force A* instead of the floor-blind 2D
//     proximity short-circuit / direct_walk that runs under the foe),
//   - find_path_to_foe()'s stair-edge A* (a path that crosses to floor 1),
//   - follow_path_to_foe()'s center-aligned stair handoff, and
//   - apply_z_motion()'s positional floor change.
// og_test_parity is blind to this (both sides run the same headless sim), so the
// cross-floor behavior is asserted directly here, never via a golden.
TEST(ZAxis, cross_floor_ai_chases_foe_through_stair)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    ASSERT_EQ(2, w.floor_count());

    // Floor 0: ZSTAIR_UP at grid (10,8). Floor 1: grass + ZSTAIR_DOWN at (10,8).
    w.grid.data[10 + 8 * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, 10, 8, PIX_ZSTAIR_DOWN);

    // The fixture's FixedRandom{0} makes walk_to_foe() a no-op every tick (its
    // rng(300) guard returns 0). Drive the world LCG live so the AI think and
    // periodic path checks actually run.
    w.rng_.state_ = 0x9E3779B9u;

    walker* skel = w.add_ob(Order::Living, FAMILY_SKELETON);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(skel, nullptr);
    ASSERT_NE(target, nullptr);

    // Enemy on floor 0, placed 2D-CLOSE to (but not directly on top of) the
    // target — so the floor-blind proximity short-circuit in walk_to_foe() WOULD
    // fire and skip pathing entirely without the cross-floor gate.
    skel->set_team_num(1);
    skel->set_real_team_num(1);
    skel->set_floor(0);
    skel->setxy(5 * GRID_SIZE, 5 * GRID_SIZE);
    skel->set_act_type(ACT_RANDOM);

    // Target on floor 1, away from the stair (it never moves: we never act() it).
    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(1);
    target->setxy(3 * GRID_SIZE, 3 * GRID_SIZE);

    skel->set_foe(target);
    // A long-lived SEARCH so do_command() drives walk_to_foe() every tick.
    skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int max_floor = 0;
    int best_same_floor_dist = 1 << 30;
    for (int t = 0; t < 2000; ++t)
    {
        skel->act();
        if (skel->dead())
            break;
        max_floor = std::max(max_floor, static_cast<int>(skel->floor()));
        if (skel->floor() == target->floor())
            best_same_floor_dist = std::min(
                best_same_floor_dist,
                static_cast<int>(skel->distance_to_ob(target)));
        // Keep the chase alive across foe-pointer resets / command expiry so the
        // test measures pathing, not bookkeeping.
        if (!skel->foe() && !target->dead())
            skel->set_foe(target);
        if (!skel->stats()->has_commands() && skel->foe())
            skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }

    EXPECT_EQ(1, max_floor)
        << "skeleton never climbed the Z-stair to the foe's floor — it ran under "
           "the target on its own floor instead of routing through the stair";
    EXPECT_LT(best_same_floor_dist, 4 * GRID_SIZE)
        << "skeleton reached the foe's floor but never closed in on the target";
}

// REPRODUCTION (moving target): same geometry as the stationary test above, but
// the floor-1 target patrols back and forth like a real player. Measures how long
// the skeleton stays on the WRONG floor (its own, floor 0) while the foe is on
// floor 1, before it finally climbs the Z-stair.
TEST(ZAxis, cross_floor_ai_chases_MOVING_foe_through_stair)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    ASSERT_EQ(2, w.floor_count());

    // Floor 0: ZSTAIR_UP at grid (10,8). Floor 1: grass + ZSTAIR_DOWN at (10,8).
    w.grid.data[10 + 8 * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, 10, 8, PIX_ZSTAIR_DOWN);

    w.rng_.state_ = 0x9E3779B9u;

    walker* skel = w.add_ob(Order::Living, FAMILY_SKELETON);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(skel, nullptr);
    ASSERT_NE(target, nullptr);

    skel->set_team_num(1);
    skel->set_real_team_num(1);
    skel->set_floor(0);
    skel->setxy(5 * GRID_SIZE, 5 * GRID_SIZE);
    skel->set_act_type(ACT_RANDOM);

    // Target on floor 1; it will PATROL (we move it every tick).
    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(1);
    target->setxy(3 * GRID_SIZE, 3 * GRID_SIZE);

    skel->set_foe(target);
    skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int max_floor = 0;
    int first_cross_tick = -1;
    int wrong_floor_ticks = 0; // skel on floor 0 while foe on floor 1
    int tx = 3 * GRID_SIZE;
    int tdir = +1;
    const int TICKS = 400;
    for (int t = 0; t < TICKS; ++t)
    {
        // Move the target like a player: patrol horizontally across floor 1,
        // bouncing between grid x=2 and x=18 (avoiding the stair column at x=10
        // only incidentally; staying on grass row y=3).
        tx += tdir * 2;
        if (tx >= 18 * GRID_SIZE) { tx = 18 * GRID_SIZE; tdir = -1; }
        if (tx <= 2 * GRID_SIZE) { tx = 2 * GRID_SIZE; tdir = +1; }
        if (target->floor() == 1)
            target->setxy(tx, 3 * GRID_SIZE);

        skel->act();
        if (skel->dead())
            break;

        max_floor = std::max(max_floor, static_cast<int>(skel->floor()));
        if (skel->floor() == 1 && first_cross_tick < 0)
            first_cross_tick = t;
        if (skel->floor() == 0 && target->floor() == 1)
            ++wrong_floor_ticks;

        if (!skel->foe() && !target->dead())
            skel->set_foe(target);
        if (!skel->stats()->has_commands() && skel->foe())
            skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }

    // The bug: skeleton tracks the moving foe on floor 0 for many ticks before
    // climbing. A correct chase heads for the stair immediately (a handful of
    // ticks to walk from (5,5) to the stair at (10,8), then climb).
    EXPECT_GE(first_cross_tick, 0)
        << "skeleton NEVER crossed to the foe's floor in " << TICKS << " ticks";
    EXPECT_LT(first_cross_tick, 40)
        << "skeleton mirrored the moving foe on the WRONG floor for "
        << first_cross_tick << " ticks before climbing the Z-stair";
}

// Build a realistic SPARSE upper floor (floor 1 = mostly air) with two grass
// platforms: a "player platform" (left) and a "stair platform" (right, holding a
// ZSTAIR_DOWN over the floor-0 ZSTAIR_UP). `connect` adds a grass bridge between
// them. Returns nothing; paints w.grid_for_floor(1).
static void build_air_floor1(GameWorld& w, bool connect)
{
    unsigned char* f1 = set_floor1_all_air(w);
    const int gw = w.grid.w;
    auto paint = [&](int x0, int y0, int x1, int y1, unsigned char tile) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                f1[x + y * gw] = tile;
    };
    // Player platform (left): cols 2..8, rows 2..6 (thick enough to stand on).
    paint(2, 2, 8, 6, PIX_GRASS1);
    // Stair platform (right/down): cols 14..20, rows 7..11.
    paint(14, 7, 20, 11, PIX_GRASS1);
    f1[16 + 8 * gw] = PIX_ZSTAIR_DOWN; // matches floor-0 ZSTAIR_UP at (16,8)
    // Optional ground bridge connecting the two platforms.
    if (connect)
        paint(8, 4, 16, 9, PIX_GRASS1);
}

// REGRESSION (was the reported bug): a realistic sparse upper floor where the
// moving floor-1 foe stands on a platform NOT ground-connected to the Z-stair
// landing, so A* cannot reach the foe's exact cell. The OLD behavior: the enemy
// fell to direct_walk/right_walk and MIRRORED the foe's x/y on its own floor 0,
// never climbing (max_floor stayed 0) for the whole run. The FIX: when the
// cross-floor foe is A*-unreachable, head for the nearest Z-stair and climb.
TEST(ZAxis, cross_floor_ai_MOVING_foe_air_unreachable_still_climbs_toward_stair)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    w.grid.data[16 + 8 * w.grid.w] = PIX_ZSTAIR_UP; // floor 0 stair
    build_air_floor1(w, /*connect=*/false);
    w.rng_.state_ = 0x9E3779B9u;

    walker* skel = w.add_ob(Order::Living, FAMILY_SKELETON);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(skel, nullptr);
    ASSERT_NE(target, nullptr);

    skel->set_team_num(1);
    skel->set_real_team_num(1);
    skel->set_floor(0);
    skel->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    skel->set_act_type(ACT_RANDOM);

    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(1);
    target->setxy(4 * GRID_SIZE, 3 * GRID_SIZE); // on the disconnected platform

    skel->set_foe(target);
    skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int max_floor = 0;
    int first_cross_tick = -1;
    int tx = 4 * GRID_SIZE;
    int tdir = +1;
    const int TICKS = 300;
    for (int t = 0; t < TICKS; ++t)
    {
        tx += tdir * 2;
        if (tx >= 8 * GRID_SIZE) { tx = 8 * GRID_SIZE; tdir = -1; }
        if (tx <= 2 * GRID_SIZE) { tx = 2 * GRID_SIZE; tdir = +1; }
        if (target->floor() == 1)
            target->setxy(tx, 3 * GRID_SIZE);

        skel->act();
        if (skel->dead())
            break;

        max_floor = std::max(max_floor, static_cast<int>(skel->floor()));
        if (skel->floor() == 1 && first_cross_tick < 0)
            first_cross_tick = t;

        if (!skel->foe() && !target->dead())
            skel->set_foe(target);
        if (!skel->stats()->has_commands() && skel->foe())
            skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }

    // After the fix: the enemy no longer mirrors the foe on the wrong floor. It
    // routes to the Z-stair and CLIMBS toward the foe's floor, promptly.
    EXPECT_EQ(1, max_floor)
        << "enemy never climbed toward the foe's floor — it mirrored on floor 0";
    EXPECT_GE(first_cross_tick, 0);
    EXPECT_LT(first_cross_tick, 60)
        << "enemy took too long to head for the stair (wrong-floor mirroring)";
}

// Contrast: identical sparse floor 1 but WITH a ground bridge, so A* can reach
// the foe. The enemy must climb and cross.
TEST(ZAxis, cross_floor_ai_MOVING_foe_air_REACHABLE_crosses)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    w.grid.data[16 + 8 * w.grid.w] = PIX_ZSTAIR_UP;
    build_air_floor1(w, /*connect=*/true);
    w.rng_.state_ = 0x9E3779B9u;

    walker* skel = w.add_ob(Order::Living, FAMILY_SKELETON);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(skel, nullptr);
    ASSERT_NE(target, nullptr);

    skel->set_team_num(1);
    skel->set_real_team_num(1);
    skel->set_floor(0);
    skel->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    skel->set_act_type(ACT_RANDOM);

    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(1);
    target->setxy(4 * GRID_SIZE, 3 * GRID_SIZE);

    skel->set_foe(target);
    skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int max_floor = 0;
    int first_cross_tick = -1;
    int tx = 4 * GRID_SIZE, tdir = +1;
    const int TICKS = 400;
    for (int t = 0; t < TICKS; ++t)
    {
        tx += tdir * 2;
        if (tx >= 8 * GRID_SIZE) { tx = 8 * GRID_SIZE; tdir = -1; }
        if (tx <= 2 * GRID_SIZE) { tx = 2 * GRID_SIZE; tdir = +1; }
        if (target->floor() == 1)
            target->setxy(tx, 3 * GRID_SIZE);
        skel->act();
        if (skel->dead())
            break;
        max_floor = std::max(max_floor, static_cast<int>(skel->floor()));
        if (skel->floor() == 1 && first_cross_tick < 0)
            first_cross_tick = t;
        if (!skel->foe() && !target->dead())
            skel->set_foe(target);
        if (!skel->stats()->has_commands() && skel->foe())
            skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }
    EXPECT_EQ(1, max_floor);
}

// REGRESSION (exact reported symptom): foe starts on an unreachable (disconnected)
// platform, then wanders onto the reachable stair platform at tick 150. OLD
// behavior: the enemy mirrored on the wrong floor until the foe became reachable
// (~150 ticks / several seconds), only then crossing. FIXED behavior: the enemy
// heads for the stair immediately and crosses WELL BEFORE the foe becomes
// reachable — it no longer waits, mirroring, on the wrong floor.
TEST(ZAxis, cross_floor_ai_MOVING_foe_climbs_without_waiting_for_reachability)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    w.grid.data[16 + 8 * w.grid.w] = PIX_ZSTAIR_UP;
    build_air_floor1(w, /*connect=*/false); // two DISCONNECTED platforms
    w.rng_.state_ = 0x9E3779B9u;

    walker* skel = w.add_ob(Order::Living, FAMILY_SKELETON);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(skel, nullptr);
    ASSERT_NE(target, nullptr);

    skel->set_team_num(1);
    skel->set_real_team_num(1);
    skel->set_floor(0);
    skel->setxy(6 * GRID_SIZE, 6 * GRID_SIZE);
    skel->set_act_type(ACT_RANDOM);

    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(1);
    target->setxy(4 * GRID_SIZE, 3 * GRID_SIZE); // disconnected platform

    skel->set_foe(target);
    skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int first_cross_tick = -1;
    const int MOVE_TO_REACHABLE = 150; // tick the foe wanders onto stair platform
    const int TICKS = 400;
    for (int t = 0; t < TICKS; ++t)
    {
        // For the first 150 ticks the foe patrols the unreachable platform; then
        // it relocates onto the stair platform (cols 14..20, row 9 = reachable).
        if (target->floor() == 1)
        {
            if (t < MOVE_TO_REACHABLE)
                target->setxy((4 + (t / 8) % 4) * GRID_SIZE, 3 * GRID_SIZE);
            else
                target->setxy(17 * GRID_SIZE, 9 * GRID_SIZE);
        }
        skel->act();
        if (skel->dead())
            break;
        if (skel->floor() == 1 && first_cross_tick < 0)
            first_cross_tick = t;
        if (!skel->foe() && !target->dead())
            skel->set_foe(target);
        if (!skel->stats()->has_commands() && skel->foe())
            skel->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }
    // After the fix the enemy proactively climbs toward the foe's floor instead
    // of waiting (mirroring) until the foe wanders somewhere A*-reachable.
    EXPECT_GE(first_cross_tick, 0)
        << "enemy never crossed at all";
    EXPECT_LT(first_cross_tick, MOVE_TO_REACHABLE)
        << "enemy waited on the wrong floor until the foe became reachable "
           "instead of heading for the stair (the reported mirroring bug)";
}

TEST(ZAxis, cylinder_zoverlap_lets_high_projectile_pass)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    // Single floor is fine; this tests the cylinder z-overlap, not floors.
    walker* ground = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* proj = w.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(ground, nullptr);
    ASSERT_NE(proj, nullptr);

    // Opposing teams so the weapon does not friendly-pass the target; this
    // isolates the cylinder z-overlap as the only thing gating collision.
    ground->set_team_num(1);
    ground->set_real_team_num(1);
    proj->set_team_num(0);
    proj->set_real_team_num(0);

    ground->setxy(100, 100);
    ground->set_sizez(16); // 1-tile-tall ground unit at z [0,16]
    proj->setxy(100, 100);
    proj->set_sizez(4);

    // Projectile at the ground unit's height collides.
    proj->set_worldz(0.0f);
    EXPECT_FALSE(w.query_object_passable(100, 100, proj))
        << "projectile at ground height should collide";

    // Projectile flying high above the ground unit's cylinder passes over.
    proj->set_worldz(40.0f);
    EXPECT_TRUE(w.query_object_passable(100, 100, proj))
        << "projectile above the target's cylinder should pass over";
}

// --- Floor-transition destination validation (bugs A2-A5) -------------------
//
// walker::apply_z_motion validates every landing with
// GameWorld::floor_landing_clear (grid-passable + no blocking entity, no-eat)
// before change_floor. These pin the four reported failure modes: climbing
// into an occupied cell (A2), stepping off a stair onto a walker (A3), a
// runner crossing a stair whose destination is inside walls (A4), and an air
// fall onto a wall top (A5).

// A2 -> B2 (was the "guard seals the staircase" bug): climbing a Z-stair whose
// paired destination cell is occupied used to be DENIED outright, which let a
// guard standing on the stair top PERMANENTLY seal a staircase. Now the
// climber ascends and is nudged to the nearest clear cell of the target floor
// beside the blocker (deterministic ring probe: ring 1's first row-major
// candidate is the north-west neighbor), never overlapping it.
TEST(ZAxis, stair_up_into_occupied_cell_lands_beside_the_blocker)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    // The exact-landing pin below assumes the 1-tile soldier footprint.
    ASSERT_EQ(16, a->sizex());
    ASSERT_EQ(16, a->sizey());

    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    ASSERT_EQ(8, cx);
    ASSERT_EQ(8, cy);
    // Stairs are authored as vertically aligned pairs: UP on floor 0, DOWN at
    // the same cell on floor 1 — with B standing ON the pair cell.
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN);
    b->set_floor(1);
    b->setxy(a->xpos(), a->ypos());

    a->apply_z_motion();
    EXPECT_EQ(1, a->floor())
        << "ascent must SUCCEED beside the blocker (the old outright denial "
           "let a guard on the stair top seal the staircase forever)";
    EXPECT_EQ(7 * GRID_SIZE, a->xpos())
        << "deterministic nudge: ring 1, first row-major candidate (7,7)";
    EXPECT_EQ(7 * GRID_SIZE, a->ypos());
    EXPECT_TRUE(w.query_object_passable(a->xpos(), a->ypos(), a))
        << "arrival must not overlap the blocker";
}

// A3 -> B2 (descent flavor): stepping off a DOWN stair onto a walker standing
// at the aligned cell below used to be DENIED outright (after the original
// entanglement fix); now the arriver descends beside the occupant instead —
// still never overlapping it, so nobody wedges.
TEST(ZAxis, stepping_off_stair_onto_walker_lands_beside_no_entanglement)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(16, a->sizex());
    ASSERT_EQ(16, a->sizey());

    a->set_floor(1);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    ASSERT_EQ(8, cx);
    ASSERT_EQ(8, cy);
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN); // A stands on the DOWN stair
    b->set_floor(0);
    b->setxy(a->xpos(), a->ypos()); // B occupies the landing below

    a->apply_z_motion();
    EXPECT_EQ(0, a->floor())
        << "descent must SUCCEED beside the occupant instead of being denied";
    EXPECT_EQ(7 * GRID_SIZE, a->xpos())
        << "deterministic nudge: ring 1, first row-major candidate (7,7)";
    EXPECT_EQ(7 * GRID_SIZE, a->ypos());
    EXPECT_TRUE(w.query_object_passable(a->xpos(), a->ypos(), a))
        << "arrival must not overlap any other walker";

    // No entanglement: the occupant was never overlapped and can still move.
    EXPECT_TRUE(b->walkstep(1.0f, 0.0f)) << "occupant must not be wedged";
}

// B2 (denial is the last resort, small radius pinned): when the aligned cell
// is occupied AND every cell within the kStairNudgeRadius=2 rings is walled,
// the transition is DENIED — even though clear ground exists at ring 3. The
// stair works again (via the aligned cell) once the occupant steps away.
TEST(ZAxis, stair_with_no_clear_arrival_within_radius_denied_until_clear)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    ASSERT_EQ(8, cx);
    ASSERT_EQ(8, cy);
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;

    // Floor 1: walls on rings 1..2 around the stair, the aligned DOWN stair
    // at the center (occupied by B), clear grass from ring 3 outward.
    unsigned char* f1 = set_floor1_fill(w, PIX_GRASS1);
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            f1[(cx + dx) + (cy + dy) * w.grid.w] = PIX_H_WALL1;
    f1[cx + cy * w.grid.w] = PIX_ZSTAIR_DOWN;
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    b->set_floor(1);
    b->setxy(a->xpos(), a->ypos());

    for (int t = 0; t < 20; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(0, a->floor())
            << "with no clear arrival within the nudge radius the climb must "
               "be denied (tick " << t << ")";
    }

    // The occupant steps off the pair cell (walls everywhere nearby, so park
    // it far away on grass): the aligned arrival clears and the next probe
    // after the cooldown takes the stair.
    b->setxy(14 * GRID_SIZE, 14 * GRID_SIZE);
    for (int t = 0; t < 8 && a->floor() == 0; ++t)
        a->apply_z_motion();
    EXPECT_EQ(1, a->floor())
        << "stair must work again once the aligned far side clears";
    EXPECT_EQ(8 * GRID_SIZE, a->xpos()) << "aligned arrival, no nudge needed";
    EXPECT_EQ(8 * GRID_SIZE, a->ypos());
}

// B2 (avoid-air): the stair nudge must never pick a PIX_AIR cell of the
// target floor — air is grid-passable by design, but landing on it would
// immediately fall back to the source floor (an up-fall loop that defeats
// the ascent). With the aligned cell occupied on a 1-cell platform in open
// sky, the climb is DENIED instead, and resumes aligned once the cell clears.
TEST(ZAxis, stair_nudge_never_lands_on_air)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    ASSERT_EQ(8, cx);
    ASSERT_EQ(8, cy);
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;

    // Floor 1: open sky except the 1-cell stair platform — occupied by B.
    unsigned char* f1 = set_floor1_all_air(w);
    f1[cx + cy * w.grid.w] = PIX_ZSTAIR_DOWN;
    b->set_floor(1);
    b->setxy(a->xpos(), a->ypos());

    for (int t = 0; t < 20; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(0, a->floor())
            << "nudging onto open air would fall straight back — the climb "
               "must be denied instead (tick " << t << ")";
    }

    // The occupant leaves the platform: the aligned arrival clears.
    b->setxy(14 * GRID_SIZE, 14 * GRID_SIZE);
    for (int t = 0; t < 8 && a->floor() == 0; ++t)
        a->apply_z_motion();
    EXPECT_EQ(1, a->floor()) << "aligned climb resumes once the cell clears";
    EXPECT_EQ(8 * GRID_SIZE, a->xpos());
    EXPECT_EQ(8 * GRID_SIZE, a->ypos());
}

// B1 (was the reported "I end up going back down" bug): a stair transition
// lands you standing on the vertically-aligned PAIRED stair tile. The old
// z_cooldown_-only guard expired after 6 ticks and bounced you straight back
// down — even standing perfectly still, and any tiny in-cell movement
// re-triggered it too. The latch holds until the walker's centre FULLY leaves
// the arrival cell.
TEST(ZAxis, arrival_on_paired_stair_does_not_bounce_back)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    // The authored aligned pair: UP on floor 0, DOWN on floor 1.
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN);

    a->apply_z_motion();
    ASSERT_EQ(1, a->floor()) << "the climb itself must still work";

    // Standing still on the arrival stair: NEVER bounces back (the old code
    // returned to floor 0 on tick 7, once the cooldown expired).
    for (int t = 0; t < 30; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(1, a->floor())
            << "standing on the arrival stair must not re-trigger (tick "
            << t << ")";
    }

    // Tiny movement WITHIN the arrival cell (a couple of pixels — the centre
    // cell is unchanged): still latched, still no re-trigger.
    a->setxy(a->xpos() + 2, a->ypos() + 2);
    for (int t = 0; t < 30; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(1, a->floor())
            << "in-cell movement must not re-trigger the stair (tick "
            << t << ")";
    }
}

// B1 (re-arm): once the walker's centre fully leaves the arrival cell the
// latch clears, and stepping back onto the stair is a deliberate act that
// triggers normally — then the return arrival latches too (no down-up bounce).
TEST(ZAxis, leaving_the_stair_cell_rearms_the_transition)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN);

    a->apply_z_motion();
    ASSERT_EQ(1, a->floor());

    // Step fully off the arrival cell (centre leaves), then walk back on.
    a->setxy(10 * GRID_SIZE, 8 * GRID_SIZE);
    for (int t = 0; t < 8; ++t)
        a->apply_z_motion(); // latch clears; grass cell, nothing triggers
    ASSERT_EQ(1, a->floor());

    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE); // deliberately back onto DOWN
    for (int t = 0; t < 8 && a->floor() == 1; ++t)
        a->apply_z_motion();
    EXPECT_EQ(0, a->floor())
        << "stepping back onto the stair after leaving it must descend";

    // And the descent arrival (on the UP stair) latches as well: no bounce.
    for (int t = 0; t < 30; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(0, a->floor())
            << "the descent arrival must not bounce back up (tick " << t
            << ")";
    }
}

// B1 + B2 interplay: a NUDGED arrival latches its own landing cell (beside
// the stair), NOT the stair cell — so deliberately stepping from the nudged
// landing onto the paired stair triggers a normal descent at once.
TEST(ZAxis, nudged_arrival_leaves_the_paired_stair_armed)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    a->set_floor(0);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN);
    b->set_floor(1);
    b->setxy(a->xpos(), a->ypos()); // blocker on the pair cell

    a->apply_z_motion();
    ASSERT_EQ(1, a->floor());
    ASSERT_EQ(7 * GRID_SIZE, a->xpos()) << "nudged beside the blocker";

    // The blocker walks away; A deliberately steps onto the DOWN stair.
    b->setxy(14 * GRID_SIZE, 8 * GRID_SIZE);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    for (int t = 0; t < 8 && a->floor() == 1; ++t)
        a->apply_z_motion();
    EXPECT_EQ(0, a->floor())
        << "the stair cell itself was never latched by a nudged arrival, so "
           "stepping onto it must trigger the descent";
}

// A4: a walker running at full speed across a Z-stair whose destination floor
// is inside walls must NOT transition — the old code keyed only on the stair
// tile under the centre cell and materialized the runner inside PIX_H_WALL*
// tiles on the other floor, where it bled 1 hp/tick, stuck.
TEST(ZAxis, running_over_stair_with_walled_destination_never_lands_in_walls)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    // Floor 0: grass with a ZSTAIR_UP at (8,8). Floor 1: solid walls.
    w.grid.data[8 + 8 * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_fill(w, PIX_H_WALL1);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    a->set_floor(0);
    a->setxy(2 * GRID_SIZE, 8 * GRID_SIZE); // west of the stair, same row

    // Sprint east straight across the stair cell (apply_z_motion first, then
    // the step — the same order as living::act).
    for (int t = 0; t < 400 && a->xpos() < 14 * GRID_SIZE; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(0, a->floor())
            << "transition into a walled destination must be denied (tick "
            << t << ")";
        ASSERT_TRUE(w.query_grid_passable(a->xpos(), a->ypos(), a, a->floor()))
            << "runner must never stand inside an impassable tile (tick "
            << t << ")";
        a->walkstep(1.0f, 0.0f);
    }
    EXPECT_GE(a->xpos(), 10 * GRID_SIZE)
        << "runner should cross the stair cell and keep going on its floor";
    EXPECT_EQ(0, a->flight_left())
        << "runner must never trip the stuck-in-wall flight self-regrant";
}

// A5: falling through PIX_AIR onto a wall top is nudged to the NEAREST clear
// cell of the floor below (deterministic ring probe) instead of landing inside
// the wall. Ring 1's first candidate (row-major) is the north-west neighbor.
TEST(ZAxis, air_fall_over_wall_lands_on_nearest_passable_cell)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    // The exact-landing pin below assumes the 1-tile soldier footprint.
    ASSERT_EQ(16, a->sizex());
    ASSERT_EQ(16, a->sizey());

    a->set_floor(1);
    a->setxy(7 * GRID_SIZE, 7 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    ASSERT_EQ(7, cx);
    ASSERT_EQ(7, cy);

    set_floor1_grid(w, cx, cy, PIX_AIR);            // air hole under the walker
    w.grid.data[cx + cy * w.grid.w] = PIX_H_WALL1;  // wall top directly below

    a->apply_z_motion();
    EXPECT_EQ(0, a->floor()) << "faller must still reach the floor below";
    EXPECT_EQ(6 * GRID_SIZE, a->xpos())
        << "deterministic nudge: ring 1, first row-major candidate (6,6)";
    EXPECT_EQ(6 * GRID_SIZE, a->ypos());
    EXPECT_TRUE(w.query_grid_passable(a->xpos(), a->ypos(), a, a->floor()))
        << "faller must never come to rest inside an impassable tile";
}

// A5 (ring order): with the whole 3x3 block around the fall line walled, the
// landing is ring 2's first row-major candidate — pinning the fixed spiral
// order (no RNG) that keeps replays and parity captures deterministic.
TEST(ZAxis, air_fall_nudge_probes_rings_outward_deterministically)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(16, a->sizex());
    ASSERT_EQ(16, a->sizey());

    a->set_floor(1);
    a->setxy(7 * GRID_SIZE, 7 * GRID_SIZE);
    set_floor1_grid(w, 7, 7, PIX_AIR);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            w.grid.data[(7 + dx) + (7 + dy) * w.grid.w] = PIX_H_WALL1;

    a->apply_z_motion();
    EXPECT_EQ(0, a->floor());
    EXPECT_EQ(5 * GRID_SIZE, a->xpos())
        << "deterministic nudge: ring 2, first row-major candidate (5,5)";
    EXPECT_EQ(5 * GRID_SIZE, a->ypos());
    EXPECT_TRUE(w.query_grid_passable(a->xpos(), a->ypos(), a, a->floor()));
}

// A5 (no landing at all): when every cell within the nudge radius is blocked,
// the faller does NOT fall — it hovers on the air tile (alive, on its own
// floor) instead of materializing inside a wall.
TEST(ZAxis, air_fall_with_no_clear_landing_hovers_instead_of_falling)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);

    a->set_floor(1);
    a->setxy(7 * GRID_SIZE, 7 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    set_floor1_grid(w, cx, cy, PIX_AIR);

    // Floor 0 is solid wall everywhere: nowhere to land.
    const int cells = w.grid.w * w.grid.h;
    std::fill(w.grid.data.get(), w.grid.data.get() + cells,
              static_cast<unsigned char>(PIX_H_WALL1));

    for (int t = 0; t < 30; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(1, a->floor())
            << "with no clear landing below, the faller must hold its floor "
               "(tick " << t << ")";
        ASSERT_FALSE(a->dead());
    }
}

// ---------------------------------------------------------------------------
// Multi-floor pathing fixes (2026-07): flyer cross-floor bypass, A* no-corner-
// cut, follow-path alignment assist, floor-keyed find_near_foe. All four are
// floor_count()>1 gated, so og_test_parity pins the single-floor behavior and
// these tests pin the multi-floor behavior directly (the parity harness is
// blind to it — both sides run the same sim).
// ---------------------------------------------------------------------------

#include <openglad/gameplay/pathfinding_grid.h>

// A flyer can never change floors (apply_z_motion skips it; the A* graph has
// no stair edges for it), so a cross-floor foe is unreachable BY DESIGN.
// Regression: the cross-floor machinery used to route the flyer to a Z-stair
// it could never take and pin it there for the rest of the level (the L24
// caldera-ghost wedge), running a full-map A* exhaustion every tick. Now it
// keeps the classic floor-blind 2D chase: shadow the foe from its own floor.
TEST(ZAxis, flyer_with_cross_floor_foe_shadows_it_and_never_parks_on_stair)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    // Stair pair at (10,8); floor 1 is grass with the paired DOWN stair.
    w.grid.data[10 + 8 * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, 10, 8, PIX_ZSTAIR_DOWN);

    w.rng_.state_ = 0x9E3779B9u; // live LCG: walk_to_foe's rng guards must run

    walker* ghost = w.add_ob(Order::Living, FAMILY_ORC); // melee: must close in
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(ghost, nullptr);
    ASSERT_NE(target, nullptr);
    ghost->stats()->set_bit_flags(BIT_FLYING, 1);

    ghost->set_team_num(1);
    ghost->set_real_team_num(1);
    ghost->set_floor(0);
    ghost->setxy(12 * GRID_SIZE, 10 * GRID_SIZE);
    ghost->set_act_type(ACT_RANDOM);

    // Foe on floor 1, 2D-far from the stair: the shadow spot and the stair
    // cell are cleanly distinguishable.
    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(1);
    target->setxy(3 * GRID_SIZE, 3 * GRID_SIZE);

    ghost->set_foe(target);
    ghost->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int best_2d = 1 << 30;
    int stair_park_ticks = 0;
    const int total_ticks = 2000;
    const int tail_start = total_ticks - 600;
    for (int t = 0; t < total_ticks; ++t)
    {
        ghost->act();
        if (ghost->dead())
            break;
        ASSERT_EQ(0, ghost->floor())
            << "flyers never change floors (tick " << t << ")";
        const int d2d =
            std::abs(ghost->xpos() - target->xpos()) +
            std::abs(ghost->ypos() - target->ypos());
        best_2d = std::min(best_2d, d2d);
        if (t >= tail_start)
        {
            int cx, cy;
            center_cell(ghost, cx, cy);
            if (cx == 10 && cy == 8)
                ++stair_park_ticks;
        }
        if (!ghost->foe() && !target->dead())
            ghost->set_foe(target);
        if (!ghost->stats()->has_commands() && ghost->foe())
            ghost->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }

    EXPECT_LT(best_2d, 4 * GRID_SIZE)
        << "flyer never closed on its cross-floor foe's 2D shadow — the "
           "stair-seek machinery is routing it somewhere it can never go";
    EXPECT_LT(stair_park_ticks, 300)
        << "flyer spent the endgame parked on the Z-stair cell — the "
           "pre-fix caldera-ghost wedge is back";
}

// No corner cutting (multi-floor gate): a full-cell body cannot make a
// diagonal cell transition when either flanking orthogonal cell is blocked —
// its swept box necessarily overlaps the flank. Regression: A* emitted such
// hops; in an X-pinch (wall west + wall north, both flanks of the north-west
// diagonal) the follower seized on the impossible edge forever (the L24
// terrace scree/lava pockets).
TEST(ZAxis, multifloor_astar_never_emits_diagonals_past_blocked_flanks)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2); // arms the multi-floor pathing rules
    set_floor1_grid(w, 0, 0, PIX_GRASS1);

    // X-pinch at (10,10): west and north are walls, so the NW diagonal to
    // (9,9) is pixel-impassable at every offset; the only real route is
    // around, through row 11.
    w.grid.data[9 + 10 * w.grid.w] = PIX_H_WALL1;
    w.grid.data[10 + 9 * w.grid.w] = PIX_H_WALL1;

    w.rng_.state_ = 0x9E3779B9u;

    walker* orc = w.add_ob(Order::Living, FAMILY_ORC);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(orc, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(16, orc->sizex());

    orc->set_team_num(1);
    orc->set_real_team_num(1);
    orc->set_floor(0);
    orc->setxy(10 * GRID_SIZE, 10 * GRID_SIZE);
    orc->set_act_type(ACT_RANDOM);

    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(0);
    target->setxy(3 * GRID_SIZE, 10 * GRID_SIZE);
    if (target->stats())
        target->stats()->set_hitpoints(30000); // outlive the whole chase

    orc->set_foe(target);
    orc->find_path_to_foe();
    ASSERT_FALSE(orc->path_to_foe.empty())
        << "a walkable detour around the X-pinch exists; A* must find it";

    // The path must never contain a diagonal hop past a blocked flank.
    int prev_cx = orc->xpos() / GRID_SIZE;
    int prev_cy = orc->ypos() / GRID_SIZE;
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
    int best = 1 << 30;
    for (int t = 0; t < 2000; ++t)
    {
        orc->act();
        if (orc->dead())
            break;
        best = std::min(best,
                        static_cast<int>(orc->distance_to_ob(target)));
        if (!orc->foe() && !target->dead())
            orc->set_foe(target);
        if (!orc->stats()->has_commands() && orc->foe())
            orc->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }
    EXPECT_LT(best, 4 * GRID_SIZE)
        << "orc never made it around the X-pinch to its foe — it is seized "
           "on an impossible diagonal edge";
}

// Follow-path alignment assist (multi-floor gate): a node counts as reached
// while the body still spills up to 15px into the next row, and a cardinal
// hop hugging an impassable cell on the spill side stays pixel-blocked at
// every misaligned offset. Regression: walkstep's fixed fallback bounced the
// walker OUT of alignment each tick — a deterministic oscillation that pinned
// chasers at convex corners forever (the L24 tower-terrace wedge). The assist
// slides toward exact alignment instead; the walker turns the corner.
TEST(ZAxis, misaligned_walker_aligns_and_turns_a_one_lane_corner)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2); // arms the multi-floor pathing rules
    set_floor1_grid(w, 0, 0, PIX_GRASS1);

    // A single-file lane along row 10: rows 9 and 11 are solid wall, except
    // (10,11) — the cell the chaser's misaligned body currently spills into.
    for (int x = 0; x < w.grid.w; ++x)
    {
        w.grid.data[x + 9 * w.grid.w] = PIX_H_WALL1;
        if (x != 10)
            w.grid.data[x + 11 * w.grid.w] = PIX_H_WALL1;
    }

    w.rng_.state_ = 0x9E3779B9u;

    walker* orc = w.add_ob(Order::Living, FAMILY_ORC);
    walker* target = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(orc, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(16, orc->sizex());
    ASSERT_EQ(16, orc->sizey());

    orc->set_team_num(1);
    orc->set_real_team_num(1);
    orc->set_floor(0);
    // 14px below the lane line: body spans rows 10 AND 11. Every westward
    // step is pixel-blocked by the wall at (9,11) until the body is EXACTLY
    // aligned into row 10.
    orc->setxy(static_cast<short>(10 * GRID_SIZE),
               static_cast<short>(10 * GRID_SIZE + 14));
    orc->set_act_type(ACT_RANDOM);

    target->set_team_num(0);
    target->set_real_team_num(0);
    target->set_floor(0);
    target->setxy(4 * GRID_SIZE, 10 * GRID_SIZE);
    if (target->stats())
        target->stats()->set_hitpoints(30000);

    orc->set_foe(target);
    orc->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);

    int best = 1 << 30;
    for (int t = 0; t < 1500; ++t)
    {
        orc->act();
        if (orc->dead())
            break;
        best = std::min(best,
                        static_cast<int>(orc->distance_to_ob(target)));
        if (!orc->foe() && !target->dead())
            orc->set_foe(target);
        if (!orc->stats()->has_commands() && orc->foe())
            orc->stats()->try_command(COMMAND_SEARCH, 100000, 0, 0);
    }
    EXPECT_LT(best, 4 * GRID_SIZE)
        << "orc never aligned into the one-tile lane — the corner-wedge "
           "oscillation is back";
}

// find_near_foe's spiral probes the obmap; its floor parameter used to
// default to 0, so upper-floor walkers were blind to foes standing beside
// them and latched whatever stood on the GROUND floor beneath their 2D
// position instead. It must probe the searcher's own floor. (Cross-floor
// acquisition intentionally remains available via find_far_foe.)
TEST(ZAxis, find_near_foe_probes_the_searchers_floor)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    set_floor1_grid(w, 0, 0, PIX_GRASS1);

    walker* searcher = w.add_ob(Order::Living, FAMILY_ORC);
    walker* below = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* beside = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(searcher, nullptr);
    ASSERT_NE(below, nullptr);
    ASSERT_NE(beside, nullptr);

    searcher->set_team_num(1);
    searcher->set_real_team_num(1);
    searcher->set_floor(1);
    searcher->setxy(100, 100);

    // Ground-floor foe in the FIRST bucket the spiral probes (east +32px):
    // the pre-fix floor-0 default found this one.
    below->set_team_num(0);
    below->set_real_team_num(0);
    below->set_floor(0);
    below->setxy(132, 100);

    // Same-floor foe one bucket further out: the correct near foe.
    beside->set_team_num(0);
    beside->set_real_team_num(0);
    beside->set_floor(1);
    beside->setxy(164, 100);

    walker* found = w.find_near_foe(searcher);
    EXPECT_EQ(beside, found)
        << "find_near_foe must probe the searcher's floor, not floor 0";
}

// ---------------------------------------------------------------------------
// Fall damage (docs/z-axis-design.md "Fall damage"): a fall cascade charges
// min(15% x (stories - 1), 50%) of max HP ONCE at settle. First story free,
// invulnerability respected, armor not applied, flyers exempt, pit death
// unchanged. All damage flows through walker::resolve_fall_landing (zero RNG
// draws — parity streams untouched).
// ---------------------------------------------------------------------------

namespace {

// Replace floor `f`'s (f >= 1) grid with a uniform `tile` fill; returns the
// buffer so callers can paint individual cells on top.
unsigned char* set_upper_floor_fill(GameWorld& w, int f, unsigned char tile)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh, tile);
    w.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(f).set_target(w.grid_for_floor(f));
    return w.grid_for_floor(f).data.get();
}

// N-floor shaft world: floors 1..N-1 all grass except a stacked PIX_AIR
// column at cell (sx, sy) on floors `lowest_air`..N-1. Floor 0 keeps the
// fixture's grass grid. A walker whose centre sits at (sx, sy) on the top
// floor cascades one story per completed probe down to the first non-air
// cell of the column.
void build_shaft(GameWorld& w, int floors, int sx, int sy, int lowest_air)
{
    w.set_floor_count(floors);
    for (int f = 1; f < floors; ++f)
    {
        unsigned char* buf = set_upper_floor_fill(
            w, f, static_cast<unsigned char>(PIX_GRASS1));
        if (f >= lowest_air)
            buf[sx + sy * w.grid.w] = static_cast<unsigned char>(PIX_AIR);
    }
}

// Drive apply_z_motion through its z_cooldown_ pacing until any cascade in
// flight has completed (tick-count driven, no wall clock).
void run_z(walker* a, int probes = 60)
{
    for (int i = 0; i < probes; ++i)
        a->apply_z_motion();
}

// Spawn a soldier with its centre on grid cell (sx, sy) of `floor`.
walker* spawn_on_cell(GameWorld& w, int floor, int sx, int sy)
{
    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    if (a == nullptr)
        return nullptr;
    a->set_floor(static_cast<short>(floor));
    // setxy places the top-left corner; centre cell = (pos + size/2) / GRID.
    a->setxy(sx * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
             sy * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
    return a;
}

// FAMILY_LIFE_GEM heart search across both world entity lists.
bool world_has_heart(GameWorld& w)
{
    for (const auto& uptr : w.oblist)
        if (uptr && uptr->query_order() == Order::Treasure &&
            uptr->family() == FAMILY_LIFE_GEM)
            return true;
    for (const auto& uptr : w.fxlist)
        if (uptr && uptr->query_order() == Order::Treasure &&
            uptr->family() == FAMILY_LIFE_GEM)
            return true;
    return false;
}

} // namespace

TEST(ZAxis, fall_one_story_free)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 2, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 1, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();
    ASSERT_GT(max_hp, 0.0f);

    run_z(a);
    EXPECT_EQ(0, a->floor());
    EXPECT_EQ(max_hp, a->stats()->hitpoints())
        << "a 1-story fall is free — designed traversal must cost nothing";
    EXPECT_TRUE(a->damage_numbers.empty())
        << "a free settle must not push a damage number";
    EXPECT_EQ(0, a->fall_stories_for_test());
}

TEST(ZAxis, fall_two_stories_costs_15pct)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();

    run_z(a);
    EXPECT_EQ(0, a->floor());
    EXPECT_NEAR(max_hp * 0.85f, a->stats()->hitpoints(), 0.01f)
        << "2 stories = 15% of max HP, applied once at settle";
    ASSERT_EQ(1u, a->damage_numbers.size());
    EXPECT_EQ(RED, a->damage_numbers.back().color);
    EXPECT_NEAR(max_hp * 0.15f, a->damage_numbers.back().value, 0.01f);
    EXPECT_EQ(50, a->regen_delay());
}

TEST(ZAxis, fall_caps_at_50pct)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 6, 7, 7, /*lowest_air=*/1); // 5-story shaft to floor 0

    walker* a = spawn_on_cell(w, 5, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();

    run_z(a);
    EXPECT_EQ(0, a->floor());
    EXPECT_NEAR(max_hp * 0.50f, a->stats()->hitpoints(), 0.01f)
        << "5 stories: 15% x 4 = 60% clamps to the 50% cap — a full-HP unit "
           "survives ANY fall";
    EXPECT_FALSE(a->dead());
}

TEST(ZAxis, cascade_no_partial_damage)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();

    a->apply_z_motion(); // exactly one probe: first landing, mid-cascade
    EXPECT_EQ(1, a->floor());
    EXPECT_EQ(1, a->fall_stories_for_test())
        << "mid-cascade the accumulator holds the stories fallen so far";
    EXPECT_EQ(max_hp, a->stats()->hitpoints())
        << "no partial damage mid-cascade — the charge lands once at settle";
    EXPECT_TRUE(a->damage_numbers.empty());
}

TEST(ZAxis, hover_walkoff_silent_reset)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();

    a->apply_z_motion(); // land on floor 1's air cell, cascade in flight
    ASSERT_EQ(1, a->floor());
    ASSERT_EQ(1, a->fall_stories_for_test());

    // The walker moves off the air column onto ordinary ground of the same
    // floor before the cascade resumes (the stale hover-walk-off case).
    a->setxy(10 * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
             10 * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
    run_z(a);
    EXPECT_EQ(1, a->floor());
    EXPECT_EQ(0, a->fall_stories_for_test())
        << "non-air footing silently ends the cascade (forgiving by design)";
    EXPECT_EQ(max_hp, a->stats()->hitpoints());
    EXPECT_TRUE(a->damage_numbers.empty());
}

TEST(ZAxis, stair_reset_next_fall_charges_only_its_own_stories)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);
    // A stair on floor 1 away from the shaft (its floor-2 landing is grass).
    w.grid_for_floor(1).data[10 + 10 * w.grid.w] =
        static_cast<unsigned char>(PIX_ZSTAIR_UP);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();

    a->apply_z_motion(); // mid-cascade on floor 1
    ASSERT_EQ(1, a->fall_stories_for_test());

    // Step onto the Z-stair before the cascade resumes: stair footing resets
    // the accumulator (and the stair triggers normally).
    a->setxy(10 * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
             10 * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
    run_z(a);
    EXPECT_EQ(0, a->fall_stories_for_test());
    EXPECT_EQ(max_hp, a->stats()->hitpoints());

    // A fresh 2-story fall afterwards charges only its own 15% — not the
    // abandoned story from before the stair.
    a->change_floor(2);
    a->setxy(7 * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
             7 * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
    run_z(a);
    EXPECT_EQ(0, a->floor());
    EXPECT_NEAR(max_hp * 0.85f, a->stats()->hitpoints(), 0.01f);
}

TEST(ZAxis, teleport_reset_next_fall_charges_only_its_own_stories)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();

    a->apply_z_motion(); // mid-cascade on floor 1
    ASSERT_EQ(1, a->fall_stories_for_test());

    ASSERT_TRUE(a->teleport());
    EXPECT_EQ(0, a->fall_stories_for_test())
        << "teleport ends any fall cascade uncharged";

    // A fresh 2-story fall afterwards charges only its own 15%.
    a->change_floor(2);
    a->setxy(7 * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
             7 * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
    run_z(a);
    EXPECT_EQ(0, a->floor());
    EXPECT_NEAR(max_hp * 0.85f, a->stats()->hitpoints(), 0.01f);
}

TEST(ZAxis, invulnerable_skip)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* potion = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(potion, nullptr);
    potion->set_invulnerable_left(100);
    const float max_hp = potion->stats()->max_hitpoints();

    run_z(potion);
    EXPECT_EQ(0, potion->floor());
    EXPECT_EQ(max_hp, potion->stats()->hitpoints())
        << "the potion's promise is 'no damage', and fall damage is damage";
    EXPECT_TRUE(potion->damage_numbers.empty());

    walker* innate = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(innate, nullptr);
    innate->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    run_z(innate);
    EXPECT_EQ(0, innate->floor());
    EXPECT_EQ(innate->stats()->max_hitpoints(), innate->stats()->hitpoints())
        << "BIT_INVINCIBLE walkers take no fall damage";
}

TEST(ZAxis, lethal_when_wounded)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    a->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    const float max_hp = a->stats()->max_hitpoints();
    a->stats()->set_hitpoints(max_hp * 0.10f); // wounded below the 15% charge

    run_z(a);
    EXPECT_TRUE(a->dead())
        << "a wounded unit dies to a 2-story fall (15% > 10% remaining)";
    EXPECT_EQ(1u, a->damage_numbers.size());
    EXPECT_TRUE(world_has_heart(w))
        << "a real character's death drops its heart, fall or otherwise";
}

TEST(ZAxis, knockback_off_ledge_kill)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    // The walker starts on solid ground beside the shaft; a shove is
    // indistinguishable from a walk-off at the sim layer (cause-agnostic —
    // fall damage attributes no attacker and awards no kill credit).
    walker* a = spawn_on_cell(w, 2, 9, 7);
    ASSERT_NE(a, nullptr);
    const float max_hp = a->stats()->max_hitpoints();
    a->stats()->set_hitpoints(max_hp * 0.10f);
    run_z(a, 5);
    ASSERT_EQ(2, a->floor()); // solid footing: nothing happens

    // The "shove": displaced over the shaft cell.
    a->setxy(7 * GRID_SIZE + GRID_SIZE / 2 - a->sizex() / 2,
             7 * GRID_SIZE + GRID_SIZE / 2 - a->sizey() / 2);
    run_z(a);
    EXPECT_TRUE(a->dead())
        << "knockback-off-ledge is a finisher on wounded units";
}

TEST(ZAxis, flyer_exempt_hp)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    build_shaft(w, 3, 7, 7, /*lowest_air=*/1);

    walker* a = spawn_on_cell(w, 2, 7, 7);
    ASSERT_NE(a, nullptr);
    a->stats()->set_bit_flags(BIT_FLYING, 1);
    const float max_hp = a->stats()->max_hitpoints();

    run_z(a);
    EXPECT_EQ(2, a->floor()) << "flyers never enter the air branch";
    EXPECT_EQ(max_hp, a->stats()->hitpoints());
    EXPECT_EQ(0, a->fall_stories_for_test());
}

TEST(ZAxis, pit_death_unchanged)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    set_upper_floor_fill(w, 1, static_cast<unsigned char>(PIX_GRASS1));

    walker* a = spawn_on_cell(w, 0, 7, 7);
    ASSERT_NE(a, nullptr);
    a->set_invulnerable_left(100); // invulnerability does NOT save pit death
    // Paint the pit (air on floor 0) in place, under the walker's centre.
    w.grid.data[7 + 7 * w.grid.w] = static_cast<unsigned char>(PIX_AIR);

    run_z(a, 5);
    EXPECT_TRUE(a->dead())
        << "falling past floor 0 stays an unconditional pit death (a void, "
           "not damage)";
    EXPECT_TRUE(a->damage_numbers.empty())
        << "pit death is byte-for-byte the pre-fall-damage branch: no "
           "damage number";
}
