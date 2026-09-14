#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/interface/game_context.h>

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
    return g.data[static_cast<std::size_t>(x + y * g.w)];
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
// The autotile tables are the product rule: smooth() indexes them with the
// surround mask (up 1, right 2, down 4, left 8), so the test owns its own copy
// and a rotated / reordered table in smooth.cpp must turn these red.
static constexpr unsigned char kCarpetBySurround[16] = {
    PIX_CARPET_SMALL_TINY, PIX_CARPET_SMALL_CUP, PIX_CARPET_SMALL_LEFT, PIX_CARPET_LL,
    PIX_CARPET_SMALL_CAP,  PIX_CARPET_SMALL_VER, PIX_CARPET_UL,         PIX_CARPET_L,
    PIX_CARPET_SMALL_RIGHT,PIX_CARPET_LR,        PIX_CARPET_SMALL_HOR,  PIX_CARPET_B,
    PIX_CARPET_UR,         PIX_CARPET_R,         PIX_CARPET_U,          PIX_CARPET_M,
};

static constexpr unsigned char kLightGrassBySurround[16] = {
    PIX_GRASS_LIGHT_RIGHT,     PIX_GRASS_LIGHT_RIGHT_BOTTOM, PIX_GRASS_LIGHT_LEFT_TOP,  PIX_GRASS_LIGHT_LEFT_BOTTOM,
    PIX_GRASS_LIGHT_RIGHT_TOP, PIX_GRASS_LIGHT_RIGHT,        PIX_GRASS_LIGHT_LEFT_TOP,  PIX_GRASS_LIGHT_LEFT,
    PIX_GRASS_LIGHT_RIGHT_TOP, PIX_GRASS_LIGHT_RIGHT_BOTTOM, PIX_GRASS_LIGHT_TOP,       PIX_GRASS_LIGHT_BOTTOM,
    PIX_GRASS_LIGHT_RIGHT_TOP, PIX_GRASS_LIGHT_RIGHT,        PIX_GRASS_LIGHT_TOP,       PIX_GRASS_LIGHT_1,
};
} // namespace

// Every carpet / light-grass mask resolves to ONE tile out of the genre's
// table; a "some carpet variant" oracle would survive any permutation of it.
TEST(SmoothMatrix, carpet_and_light_grass_masks_each_select_their_table_tile)
{
    FixedRandom rng(1);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    smoother s;

    for (int mask = 0; mask < 16; mask++)
    {
        SCOPED_TRACE(mask);

        PixieData carpet = make_grid(5, 5, PIX_GRASS1);
        at(carpet, 2, 2) = PIX_CARPET_M;
        set_neighbors_for_mask(carpet, 2, 2, PIX_CARPET_M, PIX_GRASS1, mask);
        s.set_target(carpet);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kCarpetBySurround[mask], (int)at(carpet, 2, 2))
            << "carpet mask " << mask << " must pick carpet_by_surround[mask]";

        PixieData light = make_grid(5, 5, PIX_GRASS1);
        at(light, 2, 2) = PIX_GRASS_LIGHT_1;
        set_neighbors_for_mask(light, 2, 2, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.set_target(light);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kLightGrassBySurround[mask], (int)at(light, 2, 2))
            << "light-grass mask " << mask << " must pick grass_light_by_surround[mask]";
    }
}


// Water/tree/dirt/dark-dirt: every tile any arm can emit maps back to its own
// genre, so a genre-only oracle accepts the wrong variant. Pin the tile.
TEST(SmoothMatrix, water_tree_dirt_and_dark_dirt_masks_each_select_their_exact_tile)
{
    // FixedRandom(2): next(3) == 2 (water_variants[2] == PIX_WATER3) and
    // next(2) == 0 (the watergrass_* pairs take their first entry).
    FixedRandom rng(2);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    // smooth.cpp TYPE_WATER ladder under rng 2.
    static constexpr unsigned char kWater[16] = {
        PIX_WATER1,        // 0: no water neighbour -> tile kept as-is
        PIX_WATERGRASS_LL, // 1: TO_UP        -> watergrass_up[0]
        PIX_WATERGRASS_UL, // 2: TO_RIGHT     -> watergrass_right[0]
        PIX_WATERGRASS_LL, // 3: TO_UP|TO_RIGHT
        PIX_WATERGRASS_UL, // 4: TO_DOWN      -> watergrass_down[0]
        PIX_WATER3,        // 5: vertical body
        PIX_WATERGRASS_UL, // 6: TO_DOWN|TO_RIGHT
        PIX_WATER3,        // 7: TO_UP|TO_DOWN|TO_RIGHT
        PIX_WATERGRASS_UR, // 8: TO_LEFT      -> watergrass_left[0]
        PIX_WATERGRASS_LR, // 9: TO_UP|TO_LEFT
        PIX_WATER3,        // 10: horizontal body
        PIX_WATER3,        // 11: TO_UP|TO_LEFT|TO_RIGHT
        PIX_WATERGRASS_UR, // 12: TO_DOWN|TO_LEFT
        PIX_WATER3,        // 13: TO_UP|TO_DOWN|TO_LEFT
        PIX_WATER3,        // 14: TO_DOWN|TO_LEFT|TO_RIGHT
        PIX_WATER3,        // 15: all around
    };

    // TYPE_TREES ladder; diagonals are grass here, so mask 15 is the right edge.
    static constexpr unsigned char kTree[16] = {
        PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,
        PIX_TREE_T1, PIX_TREE_MT, PIX_TREE_T1, PIX_TREE_ML,
        PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,
        PIX_TREE_T1, PIX_TREE_MR, PIX_TREE_T1, PIX_TREE_MR,
    };

    static constexpr unsigned char kDirt[16] = {
        PIX_DIRT_1,        PIX_DIRT_1,        PIX_DIRT_1,        PIX_DIRTGRASS_UR1,
        PIX_DIRT_1,        PIX_DIRT_1,        PIX_DIRTGRASS_LR1, PIX_DIRT_1,
        PIX_DIRT_1,        PIX_DIRTGRASS_UL1, PIX_DIRT_1,        PIX_DIRT_1,
        PIX_DIRTGRASS_LL1, PIX_DIRT_1,        PIX_DIRT_1,        PIX_DIRT_1,
    };

    static constexpr unsigned char kDarkDirt[16] = {
        PIX_DIRT_DARK_1,        PIX_DIRT_DARK_1,        PIX_DIRT_DARK_1,        PIX_DIRTGRASS_DARK_UR1,
        PIX_DIRT_DARK_1,        PIX_DIRT_DARK_1,        PIX_DIRTGRASS_DARK_LR1, PIX_DIRT_DARK_1,
        PIX_DIRT_DARK_1,        PIX_DIRTGRASS_DARK_UL1, PIX_DIRT_DARK_1,        PIX_DIRT_DARK_1,
        PIX_DIRTGRASS_DARK_LL1, PIX_DIRT_DARK_1,        PIX_DIRT_DARK_1,        PIX_DIRT_DARK_1,
    };

    smoother s;

    for (int mask = 0; mask < 16; mask++)
    {
        SCOPED_TRACE(mask);

        PixieData water = make_grid(5, 5, PIX_GRASS1);
        at(water, 2, 2) = PIX_WATER1;
        set_neighbors_for_mask(water, 2, 2, PIX_WATER1, PIX_GRASS1, mask);
        s.set_target(water);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kWater[mask], (int)at(water, 2, 2))
            << "water mask " << mask << " must select its exact shoreline/body tile";

        PixieData tree = make_grid(5, 5, PIX_GRASS1);
        at(tree, 2, 2) = PIX_TREE_M1;
        set_neighbors_for_mask(tree, 2, 2, PIX_TREE_M1, PIX_GRASS1, mask);
        s.set_target(tree);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kTree[mask], (int)at(tree, 2, 2))
            << "tree mask " << mask << " must select its exact canopy tile";

        PixieData dirt = make_grid(5, 5, PIX_GRASS1);
        at(dirt, 2, 2) = PIX_DIRT_1;
        set_neighbors_for_mask(dirt, 2, 2, PIX_DIRT_1, PIX_GRASS1, mask);
        s.set_target(dirt);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kDirt[mask], (int)at(dirt, 2, 2))
            << "dirt mask " << mask << " must pick dirt_by_surround[mask]";

        PixieData dark_dirt = make_grid(5, 5, PIX_GRASS1);
        at(dark_dirt, 2, 2) = PIX_DIRT_DARK_1;
        set_neighbors_for_mask(dark_dirt, 2, 2, PIX_DIRT_DARK_1, PIX_GRASS1, mask);
        s.set_target(dark_dirt);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kDarkDirt[mask], (int)at(dark_dirt, 2, 2))
            << "dark dirt mask " << mask << " must pick dirt_dark_by_surround[mask]";
    }
}


// Grass / dark grass / wall / cobble, pinned per mask instead of per genre.
TEST(SmoothMatrix, grass_dark_grass_wall_and_cobble_masks_each_select_their_exact_tile)
{
    // FixedRandom(0): every variant draw takes index 0, and !next_random(20)
    // is true, so the dark-grass rubble sprinkle fires deterministically.
    FixedRandom rng(0);
    GameContext ctx;
    ctx.rng = &rng;
    GlobalContextGuard guard(&ctx);

    // smooth.cpp TYPE_GRASS_DARK ladder with grass neighbours/diagonals, rng 0.
    // Mask 14 (left|right|down) is the "top middle" do-nothing arm: newvalue is
    // never assigned, so the PIX_GRASS1 initializer is what gets written. That
    // quirk is the rule here and is worth pinning.
    static constexpr unsigned char kDarkGrass[16] = {
        PIX_GRASS_DARK_1,  PIX_GRASS_DARK_UR, PIX_GRASS_DARK_UR, PIX_GRASS_DARK_UR,
        PIX_GRASS_DARK_LL, PIX_GRASS_DARK_R1, PIX_GRASS_DARK_1,  PIX_GRASS_DARK_1,
        PIX_GRASS_DARK_LL, PIX_GRASS_DARK_BR, PIX_GRASS_RUBBLE,  PIX_GRASS_RUBBLE,
        PIX_GRASS_DARK_LL, PIX_GRASS_DARK_R1, PIX_GRASS1,        PIX_GRASS_DARK_1,
    };

    // TYPE_WALL switch on a 5x6 grid whose (2,4) is a wall, so every "is the
    // wall two rows down?" test answers yes; (1,3) is grass, so the 13/15 arm
    // takes its non-corner branch.
    static constexpr unsigned char kWall[16] = {
        PIX_H_WALL1,    PIX_WALLSIDE_C, PIX_H_WALL1, PIX_WALLSIDE_L,
        PIX_WALL2,      PIX_WALL2,      PIX_WALL2,   PIX_WALL2,
        PIX_H_WALL1,    PIX_WALLSIDE_R, PIX_H_WALL1, PIX_WALLSIDE_CRACK_C1,
        PIX_WALL3,      PIX_WALL2,      PIX_WALL3,   PIX_WALL2,
    };

    smoother s;

    for (int mask = 0; mask < 16; mask++)
    {
        SCOPED_TRACE(mask);

        // Grass with dirt neighbours: no water diagonal, so the plain
        // grass_variants[rng] arm runs and rng 0 means PIX_GRASS1.
        PixieData grass = make_grid(5, 5, PIX_GRASS1);
        at(grass, 2, 2) = PIX_GRASS1;
        set_neighbors_for_mask(grass, 2, 2, PIX_GRASS1, PIX_DIRT_1, mask);
        s.set_target(grass);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_GRASS1, (int)at(grass, 2, 2))
            << "grass mask " << mask << " must take grass_variants[0]";

        PixieData dark_grass = make_grid(5, 5, PIX_GRASS1);
        at(dark_grass, 2, 2) = PIX_GRASS_DARK_1;
        set_neighbors_for_mask(dark_grass, 2, 2, PIX_GRASS_DARK_1, PIX_GRASS1, mask);
        s.set_target(dark_grass);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kDarkGrass[mask], (int)at(dark_grass, 2, 2))
            << "dark grass mask " << mask << " must select its exact edge tile";

        PixieData wall = make_grid(5, 6, PIX_GRASS1);
        at(wall, 2, 2) = PIX_H_WALL1;
        set_neighbors_for_mask(wall, 2, 2, PIX_H_WALL1, PIX_GRASS1, mask);
        at(wall, 2, 4) = PIX_H_WALL1;  // feed the y+2 checks for the wall cases
        s.set_target(wall);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kWall[mask], (int)at(wall, 2, 2))
            << "wall mask " << mask << " must select its exact wall piece";

        PixieData cobble = make_grid(5, 5, PIX_GRASS1);
        at(cobble, 2, 2) = PIX_COBBLE_1;
        s.set_target(cobble);
        ASSERT_EQ(1, s.smooth(2, 2)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_COBBLE_1, (int)at(cobble, 2, 2))
            << "cobble must take cobble_variants[0] under rng 0";
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
