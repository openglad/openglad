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
#include <openglad/core/pixdefs.h>
#include <openglad/core/constants.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>

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

// A2: climbing a Z-stair whose paired destination cell is occupied is DENIED —
// the climber stays on its floor (the old code teleported it into the
// occupant, wedged both, then bounced it back down after the cooldown) — and
// the stair works again as soon as the occupant leaves.
TEST(ZAxis, stair_up_into_occupied_cell_denied_until_clear)
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
    // Stairs are authored as vertically aligned pairs: UP on floor 0, DOWN at
    // the same cell on floor 1 — with B standing ON the pair cell.
    w.grid.data[cx + cy * w.grid.w] = PIX_ZSTAIR_UP;
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN);
    b->set_floor(1);
    b->setxy(a->xpos(), a->ypos());

    for (int t = 0; t < 20; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(0, a->floor())
            << "climb into an occupied destination must be denied (tick "
            << t << ")";
    }

    // Occupant leaves: the next probe after the cooldown takes the stair.
    b->setxy(14 * GRID_SIZE, 8 * GRID_SIZE);
    for (int t = 0; t < 8 && a->floor() == 0; ++t)
        a->apply_z_motion();
    EXPECT_EQ(1, a->floor())
        << "stair must work again once the far side clears";
    EXPECT_TRUE(w.query_object_passable(a->xpos(), a->ypos(), a))
        << "arrival must not overlap any other walker";
}

// A3: stepping off a stair (descending) onto a walker standing at the aligned
// cell below is DENIED — the old code dropped the arriver INTO the occupant
// and both wedged permanently (every step of each still overlapped the other).
TEST(ZAxis, stepping_off_stair_onto_walker_denied_no_entanglement)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);

    walker* a = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* b = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    a->set_floor(1);
    a->setxy(8 * GRID_SIZE, 8 * GRID_SIZE);
    int cx, cy;
    center_cell(a, cx, cy);
    set_floor1_grid(w, cx, cy, PIX_ZSTAIR_DOWN); // A stands on the DOWN stair
    b->set_floor(0);
    b->setxy(a->xpos(), a->ypos()); // B occupies the landing below

    for (int t = 0; t < 20; ++t)
    {
        a->apply_z_motion();
        ASSERT_EQ(1, a->floor())
            << "descent onto an occupied landing must be denied (tick "
            << t << ")";
    }

    // No entanglement: the blocked-arrival walker never overlaps B, so B (and
    // A) can still move freely.
    EXPECT_TRUE(b->walkstep(1.0f, 0.0f)) << "occupant must not be wedged";
    b->setxy(14 * GRID_SIZE, 8 * GRID_SIZE);

    for (int t = 0; t < 8 && a->floor() == 1; ++t)
        a->apply_z_motion();
    EXPECT_EQ(0, a->floor())
        << "descent must resume once the landing clears";
    EXPECT_TRUE(w.query_object_passable(a->xpos(), a->ypos(), a))
        << "arrival must not overlap any other walker";
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
