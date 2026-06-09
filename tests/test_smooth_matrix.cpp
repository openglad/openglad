#include <openglad/resources/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/platform/game_context.h>

#include <gtest/gtest.h>

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
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

static bool is_dark_grass_branch_tile(int pix)
{
    switch (pix)
    {
        case PIX_GRASS1:
        case PIX_GRASS_DARK_1:
        case PIX_GRASS_DARK_2:
        case PIX_GRASS_DARK_3:
        case PIX_GRASS_DARK_4:
        case PIX_GRASS_DARK_LL:
        case PIX_GRASS_DARK_UR:
        case PIX_GRASS_DARK_B1:
        case PIX_GRASS_DARK_B2:
        case PIX_GRASS_DARK_BR:
        case PIX_GRASS_DARK_R1:
        case PIX_GRASS_DARK_R2:
        case PIX_GRASS_RUBBLE:
            return true;
        default:
            return false;
    }
}

static void set_neighbors_for_mask(PixieData& g, int cx, int cy, unsigned char same_type, unsigned char other_type, int mask)
{
    at(g, cx, cy - 1) = (mask & 1) ? same_type : other_type;  // up
    at(g, cx + 1, cy) = (mask & 2) ? same_type : other_type;  // right
    at(g, cx, cy + 1) = (mask & 4) ? same_type : other_type;  // down
    at(g, cx - 1, cy) = (mask & 8) ? same_type : other_type;  // left

    // Keep diagonals on a stable "other" type so branch decisions stay deterministic.
    at(g, cx - 1, cy - 1) = other_type;
    at(g, cx + 1, cy - 1) = other_type;
    at(g, cx - 1, cy + 1) = other_type;
    at(g, cx + 1, cy + 1) = other_type;
}
} // namespace

TEST(SmoothMatrix, covers_carpet_and_light_grass_masks)
{
    FixedRandom rng(1);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    smoother s;

    for (int mask = 0; mask < 16; mask++)
    {
        PixieData carpet = make_grid(5, 5, PIX_GRASS1);
        at(carpet, 2, 2) = PIX_CARPET_M;
        set_neighbors_for_mask(carpet, 2, 2, PIX_CARPET_M, PIX_GRASS1, mask);
        s.set_target(carpet);
        ASSERT_EQ(1, s.smooth(2, 2));

        const unsigned char cv = at(carpet, 2, 2);
        ASSERT_TRUE(cv >= PIX_CARPET_LL && cv <= PIX_CARPET_SMALL_TINY) << "carpet mask smoothing should emit a carpet tile variant";

        PixieData light = make_grid(5, 5, PIX_GRASS1);
        at(light, 2, 2) = PIX_GRASS_LIGHT_1;
        set_neighbors_for_mask(light, 2, 2, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.set_target(light);
        ASSERT_EQ(1, s.smooth(2, 2));

        const unsigned char lv = at(light, 2, 2);
        ASSERT_TRUE(lv >= PIX_GRASS_LIGHT_1 && lv <= PIX_GRASS_LIGHT_LEFT_TOP) << "light-grass mask smoothing should emit a light-grass variant";
    }
}


TEST(SmoothMatrix, covers_water_tree_dirt_and_dark_dirt_masks)
{
    FixedRandom rng(2);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    smoother s;

    for (int mask = 0; mask < 16; mask++)
    {
        PixieData water = make_grid(5, 5, PIX_GRASS1);
        at(water, 2, 2) = PIX_WATER1;
        set_neighbors_for_mask(water, 2, 2, PIX_WATER1, PIX_GRASS1, mask);
        s.set_target(water);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_WATER, s.query_genre_x_y(2, 2));

        PixieData tree = make_grid(5, 5, PIX_GRASS1);
        at(tree, 2, 2) = PIX_TREE_M1;
        set_neighbors_for_mask(tree, 2, 2, PIX_TREE_M1, PIX_GRASS1, mask);
        s.set_target(tree);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_TREES, s.query_genre_x_y(2, 2));

        PixieData dirt = make_grid(5, 5, PIX_GRASS1);
        at(dirt, 2, 2) = PIX_DIRT_1;
        set_neighbors_for_mask(dirt, 2, 2, PIX_DIRT_1, PIX_GRASS1, mask);
        s.set_target(dirt);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_DIRT, s.query_genre_x_y(2, 2));

        PixieData dark_dirt = make_grid(5, 5, PIX_GRASS1);
        at(dark_dirt, 2, 2) = PIX_DIRT_DARK_1;
        set_neighbors_for_mask(dark_dirt, 2, 2, PIX_DIRT_DARK_1, PIX_GRASS1, mask);
        s.set_target(dark_dirt);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_DIRT_DARK, s.query_genre_x_y(2, 2));
    }
}


TEST(SmoothMatrix, covers_grass_dark_grass_wall_and_cobble_paths)
{
    FixedRandom rng(0);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    smoother s;

    for (int mask = 0; mask < 16; mask++)
    {
        PixieData grass = make_grid(5, 5, PIX_GRASS1);
        at(grass, 2, 2) = PIX_GRASS1;
        set_neighbors_for_mask(grass, 2, 2, PIX_GRASS1, PIX_DIRT_1, mask);
        s.set_target(grass);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_GRASS, s.query_genre_x_y(2, 2));

        PixieData dark_grass = make_grid(5, 5, PIX_GRASS1);
        at(dark_grass, 2, 2) = PIX_GRASS_DARK_1;
        set_neighbors_for_mask(dark_grass, 2, 2, PIX_GRASS_DARK_1, PIX_GRASS1, mask);
        s.set_target(dark_grass);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_TRUE(is_dark_grass_branch_tile(at(dark_grass, 2, 2)));

        PixieData wall = make_grid(5, 6, PIX_GRASS1);
        at(wall, 2, 2) = PIX_H_WALL1;
        set_neighbors_for_mask(wall, 2, 2, PIX_H_WALL1, PIX_GRASS1, mask);
        at(wall, 2, 4) = PIX_H_WALL1;  // feed y+2 checks for wall cases
        s.set_target(wall);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_WALL, s.query_genre_x_y(2, 2));

        PixieData cobble = make_grid(5, 5, PIX_GRASS1);
        at(cobble, 2, 2) = PIX_COBBLE_1;
        s.set_target(cobble);
        ASSERT_EQ(1, s.smooth(2, 2));
        ASSERT_EQ(TYPE_COBBLE, s.query_genre_x_y(2, 2));
    }
}


TEST(SmoothMatrix, targets_tree_dirt_dark_dirt_large_mask_blocks)
{
    FixedRandom rng(0);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    smoother s;
    struct MaskExpect { int mask; unsigned char expect; };

    const MaskExpect tree_cases[] = {
        {7, PIX_TREE_ML}, {13, PIX_TREE_MR}, {12, PIX_TREE_T1}, {11, PIX_TREE_B1},
        {10, PIX_TREE_B1}, {9, PIX_TREE_B1}, {8, PIX_TREE_B1}, {14, PIX_TREE_T1},
        {6, PIX_TREE_T1}, {5, PIX_TREE_MT}, {4, PIX_TREE_T1}, {3, PIX_TREE_B1},
        {2, PIX_TREE_B1}, {1, PIX_TREE_B1}, {0, PIX_TREE_B1}
    };
    for (const auto& c : tree_cases)
    {
        PixieData tree = make_grid(5, 5, PIX_GRASS1);
        at(tree, 2, 2) = PIX_TREE_M1;
        set_neighbors_for_mask(tree, 2, 2, PIX_TREE_M1, PIX_GRASS1, c.mask);
        s.set_target(tree);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)c.expect, (int)at(tree, 2, 2)) << "tree mask branch should select expected tile";
    }

    const MaskExpect dirt_cases[] = {
        {12, PIX_DIRTGRASS_LL1}, {9, PIX_DIRTGRASS_UL1}, {6, PIX_DIRTGRASS_LR1}, {3, PIX_DIRTGRASS_UR1},
        {7, PIX_DIRT_1}, {13, PIX_DIRT_1}, {11, PIX_DIRT_1}, {10, PIX_DIRT_1},
        {8, PIX_DIRT_1}, {14, PIX_DIRT_1}, {5, PIX_DIRT_1}, {4, PIX_DIRT_1},
        {2, PIX_DIRT_1}, {1, PIX_DIRT_1}, {0, PIX_DIRT_1}
    };
    for (const auto& c : dirt_cases)
    {
        PixieData dirt = make_grid(5, 5, PIX_GRASS1);
        at(dirt, 2, 2) = PIX_DIRT_1;
        set_neighbors_for_mask(dirt, 2, 2, PIX_DIRT_1, PIX_GRASS1, c.mask);
        s.set_target(dirt);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)c.expect, (int)at(dirt, 2, 2)) << "dirt mask branch should select expected tile";
    }

    const MaskExpect dark_cases[] = {
        {12, PIX_DIRTGRASS_DARK_LL1}, {9, PIX_DIRTGRASS_DARK_UL1},
        {6, PIX_DIRTGRASS_DARK_LR1}, {3, PIX_DIRTGRASS_DARK_UR1},
        {7, PIX_DIRT_DARK_1}, {13, PIX_DIRT_DARK_1}, {11, PIX_DIRT_DARK_1}, {10, PIX_DIRT_DARK_1},
        {8, PIX_DIRT_DARK_1}, {14, PIX_DIRT_DARK_1}, {5, PIX_DIRT_DARK_1}, {4, PIX_DIRT_DARK_1},
        {2, PIX_DIRT_DARK_1}, {1, PIX_DIRT_DARK_1}, {0, PIX_DIRT_DARK_1}
    };
    for (const auto& c : dark_cases)
    {
        PixieData dd = make_grid(5, 5, PIX_GRASS1);
        at(dd, 2, 2) = PIX_DIRT_DARK_1;
        set_neighbors_for_mask(dd, 2, 2, PIX_DIRT_DARK_1, PIX_GRASS1, c.mask);
        s.set_target(dd);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)c.expect, (int)at(dd, 2, 2)) << "dark dirt mask branch should select expected tile";
    }
}


TEST(SmoothMatrix, smooth_query_helpers_and_grass_water_corner_branches)
{
    FixedRandom rng(0);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    smoother s;

    // query_x_y guard rails before target is set.
    ASSERT_EQ(PIX_GRASS1, (int)s.query_x_y(0, 0)) << "query_x_y should default to grass before target is set";

    PixieData base = make_grid(3, 3, PIX_GRASS1);
    s.set_target(base);
    ASSERT_EQ(PIX_GRASS1, (int)s.query_x_y(-1, 0)) << "query_x_y should clamp negative x to grass";
    ASSERT_EQ(PIX_GRASS1, (int)s.query_x_y(0, -1)) << "query_x_y should clamp negative y to grass";
    ASSERT_EQ(PIX_GRASS1, (int)s.query_x_y(3, 1)) << "query_x_y should clamp x>=maxx to grass";
    ASSERT_EQ(PIX_GRASS1, (int)s.query_x_y(1, 3)) << "query_x_y should clamp y>=maxy to grass";
    ASSERT_EQ(TYPE_GRASS, (int)s.query_genre_x_y(1, 1)) << "query_genre_x_y should classify grass tile";

    auto run_grass_case = [&](auto setup, unsigned char expected, const char* msg) {
        PixieData g = make_grid(5, 5, PIX_GRASS1);
        at(g, 2, 2) = PIX_GRASS1;
        setup(g);
        s.set_target(g);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)expected, (int)at(g, 2, 2)) << msg;
    };

    run_grass_case(
        [&](PixieData& g) {
            at(g, 1, 1) = PIX_WATER1; // upleft
            at(g, 3, 3) = PIX_WATER1; // downright
            at(g, 1, 3) = PIX_WATER1; // downleft
            at(g, 2, 3) = PIX_WATER1; // down
            at(g, 1, 2) = PIX_WATER1; // left
        },
        PIX_GRASSWATER_LL,
        "grass-water LL branch should produce PIX_GRASSWATER_LL");

    run_grass_case(
        [&](PixieData& g) {
            at(g, 1, 1) = PIX_WATER1; // upleft
            at(g, 3, 1) = PIX_WATER1; // upright
            at(g, 3, 3) = PIX_WATER1; // downright
            at(g, 2, 1) = PIX_WATER1; // up
            at(g, 3, 2) = PIX_WATER1; // right
        },
        PIX_GRASSWATER_UR,
        "grass-water UR branch should produce PIX_GRASSWATER_UR");

    run_grass_case(
        [&](PixieData& g) {
            at(g, 1, 1) = PIX_WATER1; // upleft
            at(g, 3, 1) = PIX_WATER1; // upright
            at(g, 1, 3) = PIX_WATER1; // downleft
            at(g, 2, 1) = PIX_WATER1; // up
            at(g, 1, 2) = PIX_WATER1; // left
        },
        PIX_GRASSWATER_UL,
        "grass-water UL branch should produce PIX_GRASSWATER_UL");

    run_grass_case(
        [&](PixieData& g) {
            at(g, 3, 1) = PIX_WATER1; // upright
            at(g, 3, 3) = PIX_WATER1; // downright
            at(g, 1, 3) = PIX_WATER1; // downleft
            at(g, 3, 2) = PIX_WATER1; // right
            at(g, 2, 3) = PIX_WATER1; // down
        },
        PIX_GRASSWATER_LR,
        "grass-water LR branch should produce PIX_GRASSWATER_LR");
}
