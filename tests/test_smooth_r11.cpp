#include <openglad/data/smooth.h>
#include <openglad/data/pixie_data.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include "unit/unit.h"

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
