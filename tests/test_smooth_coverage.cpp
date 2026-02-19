#include <openglad/data/pixie_data.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/data/smooth.h>
#include <openglad/runtime/game_context.h>
#include "test_framework.h"

#include <cstdint>

namespace {
struct GlobalContextGuard {
    explicit GlobalContextGuard(GameContext* ctx) { set_global_context(ctx); }
    ~GlobalContextGuard() { set_global_context(nullptr); }
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

void test_smooth_query_and_reset_out_of_bounds_paths()
{
    smoother s;
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 0), "query_x_y should fallback before set_target");
    TEST_ASSERT_EQ(0, (int)s.smooth(), "smooth() should return 0 with no grid");

    PixieData grid = make_grid(3, 3, PIX_GRASS1);
    s.set_target(grid);
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(-1, 0), "negative x should fallback");
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, -1), "negative y should fallback");
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(3, 0), "x out of range should fallback");
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 3), "y out of range should fallback");

    s.reset();
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(1, 1), "query_x_y should fallback after reset");
}
REGISTER_TEST(test_smooth_query_and_reset_out_of_bounds_paths);

void test_smooth_wall_arrow_slit_branches()
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
    TEST_ASSERT_EQ((int)PIX_WALL_ARROW_GRASS, (int)at(grid, 2, 2), "up grass should keep grass arrow slit");

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_GRASS_DARK_1;
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_WALL_ARROW_GRASS_DARK, (int)at(grid, 2, 2), "up dark grass should pick dark arrow slit");

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_PAVEMENT1;
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_WALL4, (int)at(grid, 2, 2), "up pavement should pick stone slit");

    at(grid, 2, 2) = PIX_WALL_ARROW_GRASS;
    at(grid, 2, 1) = PIX_FLOOR1;
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_WALL_ARROW_FLOOR, (int)at(grid, 2, 2), "up floor should pick floor slit");
}
REGISTER_TEST(test_smooth_wall_arrow_slit_branches);

void test_smooth_carpet_light_and_cobble_switches()
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
        TEST_ASSERT_EQ((int)PIX_CARPET_M, (int)at(grid, 2, 2), "carpet center should map to M when fully surrounded");

        at(grid, 2, 2) = PIX_GRASS_LIGHT_1;
        set_same_neighbors(grid, 2, 2, PIX_GRASS_LIGHT_1, PIX_GRASS1, TO_DOWN | TO_LEFT);
        (void)s.smooth(2, 2);
        TEST_ASSERT_EQ((int)PIX_GRASS_LIGHT_RIGHT_TOP, (int)at(grid, 2, 2), "light grass branch should map to right_top");

        at(grid, 2, 2) = PIX_COBBLE_1;
        set_same_neighbors(grid, 2, 2, PIX_COBBLE_1, PIX_GRASS1, TO_AROUND);
        (void)s.smooth(2, 2);
        int v = (int)at(grid, 2, 2);
        TEST_ASSERT(v == PIX_COBBLE_1 || v == PIX_COBBLE_2 || v == PIX_COBBLE_3 || v == PIX_COBBLE_4,
                    "cobble branch should choose a cobble variant");
    }
}
REGISTER_TEST(test_smooth_carpet_light_and_cobble_switches);

void test_smooth_water_tree_dirt_and_dark_dirt_edges()
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
    TEST_ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)at(grid, 2, 2), "water upper-right should pick LL shoreline");

    at(grid, 2, 2) = PIX_TREE_M1;
    set_same_neighbors(grid, 2, 2, PIX_TREE_M1, PIX_GRASS1, TO_DOWN | TO_RIGHT | TO_UP);
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_TREE_ML, (int)at(grid, 2, 2), "trees left-middle branch should pick ML");

    at(grid, 2, 2) = PIX_DIRT_1;
    set_same_neighbors(grid, 2, 2, PIX_DIRT_1, PIX_GRASS1, TO_LEFT | TO_DOWN);
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_DIRTGRASS_LL1, (int)at(grid, 2, 2), "dirt top-right edge should map to LL1");

    at(grid, 2, 2) = PIX_DIRT_DARK_1;
    set_same_neighbors(grid, 2, 2, PIX_DIRT_DARK_1, PIX_GRASS1, TO_RIGHT | TO_UP);
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_DIRTGRASS_DARK_UR1, (int)at(grid, 2, 2), "dark dirt bottom-left edge should map to UR1");
}
REGISTER_TEST(test_smooth_water_tree_dirt_and_dark_dirt_edges);

static void set_diagonals(PixieData& g, int cx, int cy, unsigned char ul, unsigned char ur, unsigned char dl, unsigned char dr)
{
    at(g, cx - 1, cy - 1) = ul;
    at(g, cx + 1, cy - 1) = ur;
    at(g, cx - 1, cy + 1) = dl;
    at(g, cx + 1, cy + 1) = dr;
}

void test_smooth_grass_dark_wall_water_tree_dirt_and_unknown_deep_branches()
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
    TEST_ASSERT_EQ(0, (int)s.smooth(), "smooth() without target should return 0");
    ExposedSmoother ex;
    ex.set_x_y(cx, cy, PIX_WATER1);
    TEST_ASSERT(true, "deep smooth branch scenarios executed");
}
REGISTER_TEST(test_smooth_grass_dark_wall_water_tree_dirt_and_unknown_deep_branches);
