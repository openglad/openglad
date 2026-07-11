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

    a->apply_z_motion();
    EXPECT_EQ(1, a->floor())
        << "stepping on a ZSTAIR_UP should move up one floor";
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
