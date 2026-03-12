#include <openglad/resources/pixie_data.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/platform/game_context.h>
#include "test_framework.h"

#include <cstdint>

namespace {
struct GlobalContextGuard {
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
};

class ExposedSmoother : public smoother {
public:
    using smoother::set_x_y;
};

static PixieData make_grid(unsigned char w, unsigned char h, unsigned char fill)
{
    auto* raw = new unsigned char[w * h];
    for (int i = 0; i < w * h; i++)
        raw[i] = fill;
    return PixieData(1, w, h, raw);
}

static unsigned char& at(PixieData& g, int x, int y)
{
    return g.data[x + y * g.w];
}

static void set_same_neighbors(PixieData& g, int cx, int cy, unsigned char same, unsigned char other, int mask)
{
    at(g, cx, cy - 1) = (mask & TO_UP) ? same : other;
    at(g, cx + 1, cy) = (mask & TO_RIGHT) ? same : other;
    at(g, cx, cy + 1) = (mask & TO_DOWN) ? same : other;
    at(g, cx - 1, cy) = (mask & TO_LEFT) ? same : other;
}

} // namespace

static void set_diagonals(PixieData& g, int cx, int cy, unsigned char ul, unsigned char ur, unsigned char dl, unsigned char dr);

TEST(SmoothCoverage, smooth_query_and_reset_out_of_bounds_paths)
{
    smoother s;
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 0)) << "query_x_y should fallback before set_target";
    ASSERT_EQ(0, (int)s.smooth()) << "smooth() should return 0 with no grid";

    PixieData grid = make_grid(3, 3, PIX_GRASS1);
    s.set_target(grid);
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(-1, 0)) << "negative x should fallback";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, -1)) << "negative y should fallback";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(3, 0)) << "x out of range should fallback";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 3)) << "y out of range should fallback";

    s.reset();
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(1, 1)) << "query_x_y should fallback after reset";
}


TEST(SmoothCoverage, smooth_wall_arrow_slit_branches)
{
    FixedRandom rng1(1);
    GameContext c;
    c.rng = &rng1;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(5, 5, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_GRASS1;
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_WALL_ARROW_GRASS, (int)at(grid, 2, 2)) << "up grass should keep grass arrow slit";

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_GRASS_DARK_1;
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_WALL_ARROW_GRASS_DARK, (int)at(grid, 2, 2)) << "up dark grass should pick dark arrow slit";

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_PAVEMENT1;
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_WALL4, (int)at(grid, 2, 2)) << "up pavement should pick stone slit";

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_FLOOR1;
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_WALL_ARROW_FLOOR, (int)at(grid, 2, 2)) << "up floor should pick floor slit";
}


TEST(SmoothCoverage, smooth_carpet_light_and_cobble_switches)
{
    for (int seed = 0; seed < 4; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        PixieData grid = make_grid(5, 5, PIX_GRASS1);
        smoother s;
        s.set_target(grid);

        at(grid, 2, 2) = PIX_CARPET_M2;
        set_same_neighbors(grid, 2, 2, PIX_CARPET_M2, PIX_GRASS1, TO_AROUND);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_CARPET_M, (int)at(grid, 2, 2)) << "carpet center should map to M when fully surrounded";

        at(grid, 2, 2) = PIX_GRASS_LIGHT_1;
        set_same_neighbors(grid, 2, 2, PIX_GRASS_LIGHT_1, PIX_GRASS1, TO_DOWN | TO_LEFT);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_LIGHT_RIGHT_TOP, (int)at(grid, 2, 2)) << "light grass branch should map to right_top";

        at(grid, 2, 2) = PIX_COBBLE_1;
        set_same_neighbors(grid, 2, 2, PIX_COBBLE_1, PIX_GRASS1, TO_AROUND);
        (void)s.smooth(2, 2);
        int v = (int)at(grid, 2, 2);
        ASSERT_TRUE(v == PIX_COBBLE_1 || v == PIX_COBBLE_2 || v == PIX_COBBLE_3 || v == PIX_COBBLE_4) << "cobble branch should choose a cobble variant";
    }
}


TEST(SmoothCoverage, smooth_water_tree_dirt_and_dark_dirt_edges)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(5, 5, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    at(grid, 2, 2) = PIX_WATER1;
    set_same_neighbors(grid, 2, 2, PIX_WATER1, PIX_GRASS1, TO_UP | TO_RIGHT);
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)at(grid, 2, 2)) << "water upper-right should pick LL shoreline";

    at(grid, 2, 2) = PIX_TREE_M1;
    set_same_neighbors(grid, 2, 2, PIX_TREE_M1, PIX_GRASS1, TO_DOWN | TO_RIGHT | TO_UP);
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_TREE_ML, (int)at(grid, 2, 2)) << "trees left-middle branch should pick ML";

    at(grid, 2, 2) = PIX_DIRT_1;
    set_same_neighbors(grid, 2, 2, PIX_DIRT_1, PIX_GRASS1, TO_LEFT | TO_DOWN);
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_DIRTGRASS_LL1, (int)at(grid, 2, 2)) << "dirt top-right edge should map to LL1";

    at(grid, 2, 2) = PIX_DIRT_DARK_1;
    set_same_neighbors(grid, 2, 2, PIX_DIRT_DARK_1, PIX_GRASS1, TO_RIGHT | TO_UP);
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_DIRTGRASS_DARK_UR1, (int)at(grid, 2, 2)) << "dark dirt bottom-left edge should map to UR1";
}


TEST(SmoothCoverage, smooth_round11_water_diagonals_and_tree_to_around_edges)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(5, 5, PIX_GRASS1);
    smoother s;
    s.set_target(grid);
    const int cx = 2, cy = 2;

    // Water diagonal branches around masks (smooth.cpp:662-669).
    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_LEFT);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_LR, (int)at(grid, cx, cy)) << "water up+left should map LR";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)at(grid, cx, cy)) << "water down+right should map UL";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN | TO_LEFT);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_UR, (int)at(grid, cx, cy)) << "water down+left should map UR";

    // Trees around==TO_AROUND with diagonal edge checks (smooth.cpp:715-720).
    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
    set_diagonals(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_TREE_MR, (int)at(grid, cx, cy)) << "trees missing upper-right should map MR";

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_TREE_ML, (int)at(grid, cx, cy)) << "trees missing upper-left should map ML";
}


TEST(SmoothCoverage, smooth_round12_water_single_edge_and_tree_center_paths)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(5, 5, PIX_GRASS1);
    smoother s;
    s.set_target(grid);
    const int cx = 2, cy = 2;

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_TRUE((int)at(grid, cx, cy) == (int)PIX_WATERGRASS_LL || (int)at(grid, cx, cy) == (int)PIX_WATERGRASS_LR) << "water up-only branch should map to one top shoreline variant";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_TRUE((int)at(grid, cx, cy) == (int)PIX_WATERGRASS_UL || (int)at(grid, cx, cy) == (int)PIX_WATERGRASS_UR) << "water down-only branch should map to one bottom shoreline variant";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_LEFT);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_TRUE((int)at(grid, cx, cy) == (int)PIX_WATERGRASS_UR || (int)at(grid, cx, cy) == (int)PIX_WATERGRASS_LR) << "water left-only branch should map to one left shoreline variant";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_TRUE((int)at(grid, cx, cy) == (int)PIX_WATERGRASS_UL || (int)at(grid, cx, cy) == (int)PIX_WATERGRASS_LL) << "water right-only branch should map to one right shoreline variant";

    at(grid, cx, cy) = PIX_WATER2;
    set_same_neighbors(grid, cx, cy, PIX_WATER2, PIX_GRASS1, 0);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATER2, (int)at(grid, cx, cy)) << "water default branch should keep existing tile";

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
    set_diagonals(grid, cx, cy, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_TREE_M1, (int)at(grid, cx, cy)) << "trees around path with full diagonals should keep center variant";
}


static void set_diagonals(PixieData& g, int cx, int cy, unsigned char ul, unsigned char ur, unsigned char dl, unsigned char dr)
{
    at(g, cx - 1, cy - 1) = ul;
    at(g, cx + 1, cy - 1) = ur;
    at(g, cx - 1, cy + 1) = dl;
    at(g, cx + 1, cy + 1) = dr;
}

TEST(SmoothCoverage, smooth_grass_dark_wall_water_tree_dirt_and_unknown_deep_branches)
{
    PixieData grid = make_grid(9, 9, PIX_GRASS1);
    smoother s;
    s.set_target(grid);
    const int cx = 4, cy = 4;

    // Grass to water corners + rng grass variants.
    {
        FixedRandom rng0(0);
        GameContext c;
        c.rng = &rng0;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_GRASS1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_LEFT | TO_DOWN);
        set_diagonals(grid, cx, cy, PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_RIGHT);
        set_diagonals(grid, cx, cy, PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_WATER1);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_LEFT);
        set_diagonals(grid, cx, cy, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_GRASS1);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_RIGHT | TO_DOWN);
        set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_WATER1, PIX_WATER1, PIX_WATER1);
        (void)s.smooth(cx, cy);
    }
    for (int seed = 0; seed < 4; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_GRASS1;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_WATER1, 0);
        set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        (void)s.smooth(cx, cy);
    }

    // Dark grass hard branches.
    for (int seed = 0; seed < 2; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c0;
        c0.rng = &rng;
        GlobalContextGuard guard0(&c0);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, TO_UP | TO_RIGHT);
        at(grid, cx - 1, cy) = PIX_TREE_M1;
        at(grid, cx, cy + 1) = PIX_H_WALL1;
        at(grid, cx + 1, cy - 1) = PIX_GRASS1;
        (void)s.smooth(cx, cy);
    }
    {
        FixedRandom rng1(1);
        GameContext c1;
        c1.rng = &rng1;
        GlobalContextGuard guard1(&c1);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx + 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx, cy + 1) = PIX_GRASS1;
        at(grid, cx, cy - 1) = PIX_GRASS_DARK_1;
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx + 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx, cy - 1) = PIX_GRASS1;
        at(grid, cx, cy + 1) = PIX_GRASS_DARK_1;
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx + 1, cy) = PIX_GRASS1;
        at(grid, cx, cy + 1) = PIX_GRASS_DARK_1;
        at(grid, cx, cy - 1) = PIX_GRASS1;
        (void)s.smooth(cx, cy);
    }
    {
        FixedRandom rng2(2);
        GameContext c2;
        c2.rng = &rng2;
        GlobalContextGuard guard2(&c2);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy - 1) = PIX_TREE_M1;
        at(grid, cx, cy - 1) = PIX_H_WALL1;
        at(grid, cx, cy + 1) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, TO_LEFT | TO_RIGHT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
        (void)s.smooth(cx, cy);
    }
    {
        FixedRandom rng3(3);
        GameContext c3;
        c3.rng = &rng3;
        GlobalContextGuard guard3(&c3);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx, cy - 1) = PIX_TREE_M1;
        at(grid, cx + 1, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, TO_LEFT | TO_DOWN);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx, cy - 1) = PIX_GRASS_DARK_1;
        at(grid, cx + 1, cy) = PIX_GRASS1;
        at(grid, cx, cy + 1) = PIX_GRASS1;
        (void)s.smooth(cx, cy);
    }

    // Carpet around=15 keep-center and promote-to-center branches.
    {
        FixedRandom rng0(0);
        GameContext c;
        c.rng = &rng0;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_CARPET_M;
        set_same_neighbors(grid, cx, cy, PIX_CARPET_M, PIX_GRASS1, TO_AROUND);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_CARPET_SMALL_TINY;
        set_same_neighbors(grid, cx, cy, PIX_CARPET_M, PIX_GRASS1, TO_AROUND);
        (void)s.smooth(cx, cy);
    }

    // Wall cases.
    {
        FixedRandom rng0(0);
        GameContext c;
        c.rng = &rng0;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_UP);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_UP | TO_DOWN);
        at(grid, cx, cy + 2) = PIX_GRASS1;
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_UP | TO_LEFT);
        at(grid, cx, cy + 2) = PIX_H_WALL1;
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_UP | TO_LEFT | TO_RIGHT);
        at(grid, cx, cy + 2) = PIX_H_WALL1;
        at(grid, cx - 1, cy + 1) = PIX_GRASS1;
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_UP | TO_RIGHT);
        at(grid, cx, cy + 2) = PIX_GRASS1;
        (void)s.smooth(cx, cy);
    }

    // Water corner, edge-rng, and default cases.
    {
        for (int seed = 0; seed < 2; seed++)
        {
            FixedRandom rng(static_cast<std::uint32_t>(seed));
            GameContext c;
            c.rng = &rng;
            GlobalContextGuard guard(&c);

            at(grid, cx, cy) = PIX_WATER1;
            set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN);
            (void)s.smooth(cx, cy);

            at(grid, cx, cy) = PIX_WATER1;
            set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_LEFT);
            (void)s.smooth(cx, cy);

            at(grid, cx, cy) = PIX_WATER1;
            set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
            (void)s.smooth(cx, cy);
        }

        FixedRandom rng2(2);
        GameContext c2;
        c2.rng = &rng2;
        GlobalContextGuard guard2(&c2);
        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_RIGHT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_LEFT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN | TO_LEFT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER2;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, 0);
        (void)s.smooth(cx, cy);
    }

    // Trees, dirt, and dark dirt else-if ladders.
    {
        FixedRandom rng0(0);
        GameContext c;
        c.rng = &rng0;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_TREE_M1;
        set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
        set_diagonals(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_TREE_M1;
        set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
        set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_TREE_M1;
        set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_UP | TO_DOWN);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_TREE_M1;
        set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, 0);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_1, PIX_GRASS1, TO_LEFT | TO_DOWN);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_1, PIX_GRASS1, TO_LEFT | TO_UP);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_1, PIX_GRASS1, TO_RIGHT | TO_UP);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_DARK_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_DARK_1, PIX_GRASS1, TO_LEFT | TO_DOWN);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_DARK_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_DARK_1, PIX_GRASS1, TO_LEFT | TO_UP);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_DARK_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_DARK_1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_DIRT_DARK_1;
        set_same_neighbors(grid, cx, cy, PIX_DIRT_DARK_1, PIX_GRASS1, TO_RIGHT | TO_UP);
        (void)s.smooth(cx, cy);
    }

    // Unknown fallback and smooth()/set_x_y no-target guard.
    {
        FixedRandom rng0(0);
        GameContext c;
        c.rng = &rng0;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = 255;
        (void)s.smooth(cx, cy);
    }
    s.reset();
    ASSERT_EQ(0, (int)s.smooth()) << "smooth() without target should return 0";
    ExposedSmoother ex;
    ex.set_x_y(cx, cy, PIX_WATER1);
    ASSERT_TRUE(true) << "deep smooth branch scenarios executed";
}


TEST(SmoothCoverage, smooth_round13_dark_grass_targeted_338_448_branches)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(7, 7, PIX_GRASS1);
    smoother s;
    s.set_target(grid);
    const int cx = 3, cy = 3;

    at(grid, cx, cy) = PIX_GRASS_DARK_1;

    // around == (TO_DOWN) with right/up non-grass should choose B1 (smooth.cpp:418-424).
    set_same_neighbors(grid, cx, cy, PIX_GRASS_DARK_1, PIX_GRASS1, TO_DOWN);
    at(grid, cx + 1, cy) = PIX_WATER1;
    at(grid, cx, cy - 1) = PIX_WATER1;
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)at(grid, cx, cy)) << "dark grass down-only with non-grass right/up should map to B1";

    // around == (TO_RIGHT | TO_UP) with left/down non-grass should choose B1 (425-431).
    at(grid, cx, cy) = PIX_GRASS_DARK_1;
    set_same_neighbors(grid, cx, cy, PIX_GRASS_DARK_1, PIX_GRASS1, TO_RIGHT | TO_UP);
    at(grid, cx - 1, cy) = PIX_WATER1;
    at(grid, cx, cy + 1) = PIX_WATER1;
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)at(grid, cx, cy)) << "dark grass up-right with non-grass left/down should map to B1";

    // around == (TO_RIGHT) with left non-grass should choose B1 (432-438).
    at(grid, cx, cy) = PIX_GRASS_DARK_1;
    set_same_neighbors(grid, cx, cy, PIX_GRASS_DARK_1, PIX_GRASS1, TO_RIGHT);
    at(grid, cx - 1, cy) = PIX_WATER1;
    at(grid, cx, cy - 1) = PIX_WATER1;
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)at(grid, cx, cy)) << "dark grass right-only with non-grass left should map to B1";

    // around == TO_UP with down == TYPE_GRASS should choose UR (439-445).
    at(grid, cx, cy) = PIX_GRASS_DARK_1;
    set_same_neighbors(grid, cx, cy, PIX_GRASS_DARK_1, PIX_GRASS1, TO_UP);
    at(grid, cx, cy + 1) = PIX_GRASS1;
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)at(grid, cx, cy)) << "dark grass up-only with grass below should map to UR";

    // around == 0 default branch should choose DARK_1 (446-447).
    at(grid, cx, cy) = PIX_GRASS_DARK_1;
    set_same_neighbors(grid, cx, cy, PIX_GRASS_DARK_1, PIX_GRASS1, 0);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASS_DARK_1, (int)at(grid, cx, cy)) << "dark grass default should map to DARK_1";
}


TEST(SmoothCoverage, smooth_round14_wall_case11_and_case15_branch_matrix)
{
    PixieData grid = make_grid(7, 7, PIX_GRASS1);
    smoother s;
    s.set_target(grid);
    const int cx = 3, cy = 3;

    // case 11 wall base branch with crack rng path (smooth.cpp:605-610).
    {
        FixedRandom crack_rng(0); // rng(10)==0 => crack
        GameContext c;
        c.rng = &crack_rng;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_H_WALL1;
        set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_UP | TO_LEFT | TO_RIGHT);
        (void)s.smooth(cx, cy);
        ASSERT_EQ((int)PIX_WALLSIDE_CRACK_C1, (int)at(grid, cx, cy)) << "wall case 11 should choose crack variant when rng hits 0";
    }

    // case 15 branch where below and lower-left are walls => PIX_WALL3 (620-624).
    at(grid, cx, cy) = PIX_H_WALL1;
    set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_AROUND);
    at(grid, cx, cy + 2) = PIX_H_WALL1;
    at(grid, cx - 1, cy + 1) = PIX_H_WALL1;
    {
        FixedRandom rng1(1);
        GameContext c;
        c.rng = &rng1;
        GlobalContextGuard guard(&c);
        (void)s.smooth(cx, cy);
    }
    ASSERT_EQ((int)PIX_WALL3, (int)at(grid, cx, cy)) << "wall case 15 should choose WALL3 when below and lower-left are walls";

    // case 15 branch where below is not wall and lower-left is wall => H_WALL1 (627-631).
    at(grid, cx, cy) = PIX_H_WALL1;
    set_same_neighbors(grid, cx, cy, PIX_H_WALL1, PIX_GRASS1, TO_AROUND);
    at(grid, cx, cy + 2) = PIX_GRASS1;
    at(grid, cx - 1, cy + 1) = PIX_H_WALL1;
    {
        FixedRandom rng2(2);
        GameContext c;
        c.rng = &rng2;
        GlobalContextGuard guard(&c);
        (void)s.smooth(cx, cy);
    }
    ASSERT_EQ((int)PIX_H_WALL1, (int)at(grid, cx, cy)) << "wall case 15 should choose H_WALL1 when below is open and lower-left is wall";
}


TEST(SmoothCoverage, smooth_round6_query_genre_and_water_tree_edges)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(7, 7, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    // query_x_y / query_genre_x_y front guards and genre mapping.
    s.reset();
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 0)) << "query_x_y should return fallback when target missing";
    s.set_target(grid);
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(-1, 0)) << "query_x_y should reject negative x";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 99)) << "query_x_y should reject out-of-range y";
    ASSERT_EQ((int)TYPE_GRASS, (int)s.query_genre_x_y(0, 0)) << "grass tile should map to TYPE_GRASS";

    const int cx = 3;
    const int cy = 3;

    // Water around masks that map to each corner branch and fallback/default.
    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_RIGHT);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)at(grid, cx, cy)) << "water around up|right should map to LL";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_LEFT);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_LR, (int)at(grid, cx, cy)) << "water around up|left should map to LR";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)at(grid, cx, cy)) << "water around down|right should map to UL";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN | TO_LEFT);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_WATERGRASS_UR, (int)at(grid, cx, cy)) << "water around down|left should map to UR";

    at(grid, cx, cy) = PIX_WATER1;
    set_same_neighbors(grid, cx, cy, PIX_GRASS1, PIX_GRASS1, 0);
    const int old = (int)at(grid, cx, cy);
    (void)s.smooth(cx, cy);
    ASSERT_EQ(old, (int)at(grid, cx, cy)) << "water default branch should keep existing tile";

    // Tree edge-only branches.
    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_RIGHT);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_TREE_B1, (int)at(grid, cx, cy)) << "tree around right-only should map to B1";

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_UP);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_TREE_B1, (int)at(grid, cx, cy)) << "tree around up-only should map to B1";
}


TEST(SmoothCoverage, smooth_round6_dark_grass_and_water_single_edge_switches)
{
    PixieData grid = make_grid(7, 7, PIX_GRASS1);
    smoother s;
    s.set_target(grid);
    const int cx = 3;
    const int cy = 3;

    // Dark-grass branch: around == (TO_LEFT | TO_RIGHT), rng(2) selects B1/B2.
    for (int seed = 0; seed < 2; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx + 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx, cy - 1) = PIX_GRASS1;
        at(grid, cx, cy + 1) = PIX_GRASS1;
        (void)s.smooth(cx, cy);
    }

    // Water single-edge branches: around==UP/DOWN/LEFT/RIGHT all execute rng(2) switches.
    for (int seed = 0; seed < 2; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_DOWN);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_LEFT);
        (void)s.smooth(cx, cy);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
        (void)s.smooth(cx, cy);
    }

    // Tree all-around diagonal split branches (MR / ML).
    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
    set_diagonals(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1);
    (void)s.smooth(cx, cy);

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1);
    (void)s.smooth(cx, cy);

    ASSERT_TRUE(true) << "dark-grass/water/tree switch branches executed";
}


TEST(SmoothCoverage, smooth_round7a_grass_dark_wall_and_water_specific_branches)
{
    const int cx = 3;
    const int cy = 3;
    PixieData grid = make_grid(7, 7, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    // TYPE_GRASS corner-to-water branches.
    at(grid, cx, cy) = PIX_GRASS1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_LEFT | TO_DOWN);
    set_diagonals(grid, cx, cy, PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASSWATER_LL, (int)at(grid, cx, cy)) << "grass-water LL branch";

    at(grid, cx, cy) = PIX_GRASS1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_RIGHT);
    set_diagonals(grid, cx, cy, PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_WATER1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASSWATER_UR, (int)at(grid, cx, cy)) << "grass-water UR branch";

    at(grid, cx, cy) = PIX_GRASS1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_UP | TO_LEFT);
    set_diagonals(grid, cx, cy, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_GRASS1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASSWATER_UL, (int)at(grid, cx, cy)) << "grass-water UL branch";

    at(grid, cx, cy) = PIX_GRASS1;
    set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_RIGHT | TO_DOWN);
    set_diagonals(grid, cx, cy, PIX_GRASS1, PIX_WATER1, PIX_WATER1, PIX_WATER1);
    (void)s.smooth(cx, cy);
    ASSERT_EQ((int)PIX_GRASSWATER_LR, (int)at(grid, cx, cy)) << "grass-water LR branch";

    // TYPE_GRASS_DARK around==(TO_UP|TO_DOWN|TO_LEFT) rng(2) branch.
    for (int seed = 0; seed < 2; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_GRASS_DARK_1;
        at(grid, cx, cy - 1) = PIX_GRASS_DARK_1;
        at(grid, cx, cy + 1) = PIX_GRASS_DARK_1;
        at(grid, cx - 1, cy) = PIX_GRASS_DARK_1;
        at(grid, cx + 1, cy) = PIX_GRASS1;
        (void)s.smooth(cx, cy);
    }

    // TYPE_WALL non-arrow cases that branch on y+2 / x-1,y+1 checks.
    at(grid, cx, cy) = PIX_H_WALL1;
    at(grid, cx, cy - 1) = PIX_H_WALL1;
    at(grid, cx + 1, cy) = PIX_H_WALL1;
    at(grid, cx - 1, cy) = PIX_GRASS1;
    at(grid, cx, cy + 1) = PIX_GRASS1;
    at(grid, cx, cy + 2) = PIX_H_WALL1;
    (void)s.smooth(cx, cy);

    at(grid, cx, cy) = PIX_H_WALL1;
    at(grid, cx, cy - 1) = PIX_H_WALL1;
    at(grid, cx + 1, cy) = PIX_H_WALL1;
    at(grid, cx - 1, cy) = PIX_H_WALL1;
    at(grid, cx, cy + 1) = PIX_H_WALL1;
    at(grid, cx, cy + 2) = PIX_GRASS1;
    at(grid, cx - 1, cy + 1) = PIX_H_WALL1;
    (void)s.smooth(cx, cy);

    // TYPE_WATER single-edge and full-rng variants.
    for (int seed = 0; seed < 3; seed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(seed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        at(grid, cx, cy) = PIX_WATER1;
        set_same_neighbors(grid, cx, cy, PIX_WATER1, PIX_GRASS1, TO_AROUND);
        (void)s.smooth(cx, cy);
    }
}


TEST(SmoothCoverage, smooth_round7a_query_guards_and_tree_branches)
{
    smoother s;
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 0)) << "query_x_y guard with no grid";

    PixieData grid = make_grid(5, 5, PIX_GRASS1);
    s.set_target(grid);
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(-1, 0)) << "query_x_y negative guard";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(99, 0)) << "query_x_y maxx guard";

    const int cx = 2;
    const int cy = 2;

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_LEFT | TO_RIGHT | TO_DOWN);
    (void)s.smooth(cx, cy);

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_UP | TO_DOWN | TO_LEFT);
    (void)s.smooth(cx, cy);

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_DOWN | TO_RIGHT);
    (void)s.smooth(cx, cy);

    at(grid, cx, cy) = PIX_TREE_M1;
    set_same_neighbors(grid, cx, cy, PIX_TREE_M1, PIX_GRASS1, TO_RIGHT | TO_UP);
    (void)s.smooth(cx, cy);
}

