#include <openglad/data/smooth.h>
#include <openglad/data/pixie_data.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>

#include "unit/unit.h"

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
