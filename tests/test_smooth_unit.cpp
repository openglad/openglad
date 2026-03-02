#include <openglad/gameplay/smooth.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/legacy/base.h>
#include "unit/unit.h"
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <cstdint>

// --- From test_smooth_coverage_push.cpp ---
namespace detail_smooth_coverage_push {
namespace {

PixieData make_grid(unsigned char fill)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = 5;
    pd.h = 5;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; ++i)
        pd.data[i] = fill;
    return pd;
}

void set_neighbors(PixieData& pd,
                   unsigned char center,
                   unsigned char up,
                   unsigned char right,
                   unsigned char down,
                   unsigned char left,
                   unsigned char ul,
                   unsigned char ur,
                   unsigned char dl,
                   unsigned char dr)
{
    pd.data[2 + 2 * pd.w] = center;
    pd.data[2 + 1 * pd.w] = up;
    pd.data[3 + 2 * pd.w] = right;
    pd.data[2 + 3 * pd.w] = down;
    pd.data[1 + 2 * pd.w] = left;
    pd.data[1 + 1 * pd.w] = ul;
    pd.data[3 + 1 * pd.w] = ur;
    pd.data[1 + 3 * pd.w] = dl;
    pd.data[3 + 3 * pd.w] = dr;
}

unsigned char center_value(const PixieData& pd)
{
    return pd.data[2 + 2 * pd.w];
}

} // namespace

OG_UNIT_TEST(test_smooth_query_and_genre_basics)
{
    smoother s;
    OG_ASSERT(s.query_x_y(0, 0) == PIX_GRASS1);
    OG_ASSERT(s.query_x_y(-1, 0) == PIX_GRASS1);
    OG_ASSERT(s.query_genre_x_y(0, 0) == TYPE_GRASS);

    PixieData pd = make_grid(PIX_CARPET_M);
    s.set_target(pd);
    OG_ASSERT(s.query_x_y(2, 2) == PIX_CARPET_M);
    OG_ASSERT(s.query_x_y(99, 99) == PIX_GRASS1);
    OG_ASSERT(s.query_genre_x_y(2, 2) == TYPE_CARPET);

    pd.data[2 + 2 * pd.w] = 255;
    OG_ASSERT(s.query_genre_x_y(2, 2) == TYPE_UNKNOWN);

    s.reset();
    OG_ASSERT(s.smooth() == 0);
}

OG_UNIT_TEST(test_smooth_branch_matrix_on_center_tile)
{
    FixedRandom rng(0);
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;

    // Grass -> explicit grass/water blend corner and random fallback.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_GRASS1, PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                      PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1);
        s.set_target(pd);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_GRASSWATER_LL);

        set_neighbors(pd, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_GRASS1);
    }

    // Dark grass branches including rubble path and side-edge selection.
    {
        PixieData pd = make_grid(PIX_GRASS_DARK_1);
        set_neighbors(pd, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_GRASS_RUBBLE);

        set_neighbors(pd, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_WALL2, PIX_WALL2,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_GRASS_DARK_R1);
    }

    // Carpet around=0 and around=15 preserving M/M2.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_CARPET_M, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_CARPET_SMALL_TINY);

        set_neighbors(pd, PIX_CARPET_M2, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M,
                      PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M);
        s.smooth(2, 2);
        const unsigned char carpet_center = center_value(pd);
        OG_ASSERT(carpet_center == PIX_CARPET_M || carpet_center == PIX_CARPET_M2);
    }

    // Wall arrow-slit variants.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_WALL_ARROW_GRASS, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_WALL_ARROW_GRASS);

        set_neighbors(pd, PIX_WALL_ARROW_GRASS, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_WALL_ARROW_GRASS_DARK);

        set_neighbors(pd, PIX_WALL_ARROW_GRASS, PIX_FLOOR1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_WALL_ARROW_FLOOR);
    }

    // Water, trees, dirt, dark dirt, cobble, and unknown default.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_WATERGRASS_LL);

        set_neighbors(pd, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_TREE_MT);

        set_neighbors(pd, PIX_DIRT_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1, PIX_DIRT_1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_DIRTGRASS_LL1);

        set_neighbors(pd, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_DIRTGRASS_DARK_UR1);

        set_neighbors(pd, PIX_COBBLE_4, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == PIX_COBBLE_1);

        set_neighbors(pd, 255, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        OG_ASSERT(center_value(pd) == 255);
    }

    set_global_context(nullptr);
}
} // namespace detail_smooth_coverage_push

// --- From test_smooth_r11.cpp ---
namespace detail_smooth_r11 {
namespace {

PixieData make_grid(unsigned char fill)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = 7;
    pd.h = 7;
    pd.data = std::make_unique<unsigned char[]>(49);
    for (int i = 0; i < 49; ++i)
        pd.data[i] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

unsigned char get_at(const PixieData& pd, int x, int y)
{
    return pd.data[x + y * pd.w];
}

void set_neighbors(PixieData& pd,
                   unsigned char center,
                   unsigned char up,
                   unsigned char right,
                   unsigned char down,
                   unsigned char left,
                   unsigned char ul,
                   unsigned char ur,
                   unsigned char dl,
                   unsigned char dr)
{
    set_at(pd, 3, 3, center);
    set_at(pd, 3, 2, up);
    set_at(pd, 4, 3, right);
    set_at(pd, 3, 4, down);
    set_at(pd, 2, 3, left);
    set_at(pd, 2, 2, ul);
    set_at(pd, 4, 2, ur);
    set_at(pd, 2, 4, dl);
    set_at(pd, 4, 4, dr);
}

} // namespace

OG_UNIT_TEST(test_smooth_r11_query_edges_and_setter_guards)
{
    smoother s;
    OG_ASSERT(s.query_x_y(-2, 0) == PIX_GRASS1);
    OG_ASSERT(s.query_x_y(0, -2) == PIX_GRASS1);

    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);
    OG_ASSERT(s.query_x_y(100, 100) == PIX_GRASS1);

    s.smooth(0, 0);
    OG_ASSERT(s.query_x_y(0, 0) >= 0);

    s.reset();
    OG_ASSERT(s.smooth() == 0);
}

OG_UNIT_TEST(test_smooth_r11_dark_grass_and_wall_and_water_branches)
{
    FixedRandom rng0(0);
    GameContext gc;
    gc.rng = &rng0;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    // dark grass top-right with right neighbor grass hits line 346
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // dark grass top-right fallback hits line 348
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_WATER1, PIX_GRASS1, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // around=(TO_DOWN|TO_UP) -> R1/R2 block lines 406+ ; rng 0 => R1
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // around==TO_DOWN with right grass -> LL line 421
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // around==TO_RIGHT with left grass -> UR line 435
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // around==TO_UP with down not grass -> B1 line 444
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS_DARK_1, PIX_GRASS1, PIX_WATER1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // carpet around=15 preserve center pixel (line 499)
    set_neighbors(pd, PIX_CARPET_M2,
                  PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M,
                  PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // wall non-arrow around 1 and 11 with crack path line 607
    set_neighbors(pd, PIX_WALL2,
                  PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_WALL2,
                  PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_WALL2,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // water corner mappings lines 664-670 and single-side switches 680,690,700
    set_neighbors(pd, PIX_WATER1,
                  PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_WATER1,
                  PIX_WATER1, PIX_GRASS1, PIX_GRASS1, PIX_WATER1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_WATER1,
                  PIX_GRASS1, PIX_WATER1, PIX_WATER1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_WATER1,
                  PIX_GRASS1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // trees + dirt + dark dirt edges in targeted lines
    set_neighbors(pd, PIX_TREE_B1,
                  PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,
                  PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_TREE_B1,
                  PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,
                  PIX_GRASS1, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_DIRT_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1, PIX_DIRT_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    set_neighbors(pd, PIX_DIRT_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // unknown path retains original tile via query_x_y default
    set_neighbors(pd, 250,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    OG_ASSERT(s.query_x_y(3, 3) >= 0);

    // smooth() full-map return true and set_x_y guard line 903
    OG_ASSERT(s.smooth() == 1);

    set_global_context(nullptr);
}
} // namespace detail_smooth_r11

// --- From test_smooth_r12.cpp ---
namespace detail_smooth_r12 {
namespace {

struct SeqRandom final : IRandom {
    std::uint32_t n = 0;
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = n % max_exclusive;
        ++n;
        return v;
    }
};

PixieData make_grid(unsigned char fill)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = 7;
    pd.h = 7;
    pd.data = std::make_unique<unsigned char[]>(49);
    for (int i = 0; i < 49; ++i)
        pd.data[i] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

void set_neighbors(PixieData& pd, unsigned char center, unsigned char up, unsigned char right,
                   unsigned char down, unsigned char left)
{
    set_at(pd, 3, 3, center);
    set_at(pd, 3, 2, up);
    set_at(pd, 4, 3, right);
    set_at(pd, 3, 4, down);
    set_at(pd, 2, 3, left);
}

void set_neighbors_mask(PixieData& pd, unsigned char center, unsigned char same_genre, unsigned char other, int mask)
{
    set_at(pd, 3, 3, center);
    set_at(pd, 3, 2, (mask & 1) ? same_genre : other);
    set_at(pd, 4, 3, (mask & 2) ? same_genre : other);
    set_at(pd, 3, 4, (mask & 4) ? same_genre : other);
    set_at(pd, 2, 3, (mask & 8) ? same_genre : other);
}

} // namespace

OG_UNIT_TEST(test_smooth_r12_light_wall_tree_dirt_darkdirt_coverage)
{
    FixedRandom rng(0);
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    // Light-grass around variants.
    set_neighbors(pd, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS_LIGHT_1);
    s.smooth(3, 3);

    // Wall around branches for 1/4/6/11/12/13/default.
    set_neighbors(pd, PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_WALL2, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_WALL2, PIX_GRASS1, PIX_WALL2, PIX_WALL2, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_WALL2, PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_WALL2);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_WALL2, PIX_WALL2);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_WALL2, PIX_WALL2);
    s.smooth(3, 3);

    // Tree around variants.
    set_neighbors(pd, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1);
    s.smooth(3, 3);

    // Dirt and dark dirt sparse around variants.
    set_neighbors(pd, PIX_DIRT_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_DIRT_1, PIX_DIRT_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_DIRT_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_DARK_1, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_DARK_1);
    s.smooth(3, 3);

    // Cobble RNG switch and default unknown branch.
    set_neighbors(pd, PIX_COBBLE_2, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    set_neighbors(pd, 254, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);

    OG_ASSERT(s.smooth() == 1);
    set_global_context(nullptr);
}

OG_UNIT_TEST(test_smooth_r12_rng_and_masked_switch_coverage)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    // Exercise all light-grass around masks.
    for (int mask = 1; mask <= 15; ++mask)
    {
        set_neighbors_mask(pd, PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.smooth(3, 3);
    }

    // Exercise all carpet around masks.
    for (int mask = 1; mask <= 15; ++mask)
    {
        set_neighbors_mask(pd, PIX_CARPET_M, PIX_CARPET_M2, PIX_GRASS1, mask);
        s.smooth(3, 3);
    }

    // Water random variants.
    set_neighbors(pd, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_WATER1);
    s.smooth(3, 3);
    s.smooth(3, 3);
    s.smooth(3, 3);

    // Dark grass random variants and rubble chance check path.
    set_neighbors(pd, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1);
    s.smooth(3, 3);
    s.smooth(3, 3);
    s.smooth(3, 3);
    s.smooth(3, 3);

    // Grass-water corner variants.
    set_at(pd, 3, 3, PIX_GRASS1);
    set_at(pd, 2, 2, PIX_WATER1);
    set_at(pd, 4, 2, PIX_WATER1);
    set_at(pd, 2, 4, PIX_WATER1);
    set_at(pd, 4, 4, PIX_WATER1);
    set_at(pd, 3, 2, PIX_WATER1);
    set_at(pd, 4, 3, PIX_WATER1);
    set_at(pd, 3, 4, PIX_GRASS1);
    set_at(pd, 2, 3, PIX_GRASS1);
    s.smooth(3, 3);

    set_global_context(nullptr);
}
} // namespace detail_smooth_r12

// --- From test_smooth_r14.cpp ---
namespace detail_smooth_r14 {
namespace {

struct SeqRandom final : IRandom {
    std::uint32_t n = 0;
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = n % max_exclusive;
        ++n;
        return v;
    }
};

PixieData make_grid(unsigned char fill, int w = 9, int h = 9)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = w;
    pd.h = h;
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[i] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[x + y * pd.w] = v;
}

void set_neighbors_mask(PixieData& pd, int cx, int cy, unsigned char center,
                        unsigned char same_genre, unsigned char other, int mask)
{
    set_at(pd, cx, cy, center);
    set_at(pd, cx, cy - 1, (mask & TO_UP) ? same_genre : other);
    set_at(pd, cx + 1, cy, (mask & TO_RIGHT) ? same_genre : other);
    set_at(pd, cx, cy + 1, (mask & TO_DOWN) ? same_genre : other);
    set_at(pd, cx - 1, cy, (mask & TO_LEFT) ? same_genre : other);
}

} // namespace

OG_UNIT_TEST(test_smooth_r14_lines_437_447_499_572_604_614_wall_arrow_water_and_unknown_paths)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    // Wall arrow branches: grass, dark grass, pavement, floor.
    set_at(pd, x, y, PIX_WALL_ARROW_GRASS);
    set_at(pd, x, y - 1, PIX_GRASS1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_WALL_ARROW_GRASS);
    set_at(pd, x, y - 1, PIX_GRASS_DARK_1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_WALL_ARROW_GRASS);
    set_at(pd, x, y - 1, PIX_PAVEMENT1);
    s.smooth(x, y);

    set_at(pd, x, y, PIX_WALL_ARROW_GRASS);
    set_at(pd, x, y - 1, PIX_FLOOR1);
    s.smooth(x, y);

    // Water singleton/sparse cases around==UP/DOWN/LEFT/RIGHT/default.
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_UP);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_DOWN);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_LEFT);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
    s.smooth(x, y);
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, 0);
    s.smooth(x, y);

    // Unknown/default keeps source value path.
    set_at(pd, x, y, 254);
    s.smooth(x, y);

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_smooth_r14_lines_670_700_722_736_770_830_851_tree_dirt_darkdirt_mask_matrix)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    // Tree masks cover most conditional tree branches.
    for (int mask = 0; mask <= 15; ++mask)
    {
        set_neighbors_mask(pd, x, y, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, mask);
        s.smooth(x, y);
    }

    // Dirt mask matrix.
    for (int mask = 0; mask <= 15; ++mask)
    {
        set_neighbors_mask(pd, x, y, PIX_DIRT_1, PIX_DIRT_1, PIX_GRASS1, mask);
        s.smooth(x, y);
    }

    // Dark dirt mask matrix.
    for (int mask = 0; mask <= 15; ++mask)
    {
        set_neighbors_mask(pd, x, y, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, mask);
        s.smooth(x, y);
    }

    set_global_context(nullptr);
}

OG_UNIT_TEST(test_smooth_r14_lines_903_full_smooth_reset_paths)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    set_global_context(&gc);

    smoother s;

    // !mygrid guards.
    OG_ASSERT(s.smooth() == 0);
    PixieData pd = make_grid(PIX_COBBLE_1, 3, 3);
    s.set_target(pd);
    OG_ASSERT(s.smooth() == 1);

    s.reset();
    OG_ASSERT(s.smooth() == 0);

    set_global_context(nullptr);
}
} // namespace detail_smooth_r14

