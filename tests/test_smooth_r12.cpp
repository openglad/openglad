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

void set_neighbors(PixieData& pd, unsigned char center, unsigned char up, unsigned char right,
                   unsigned char down, unsigned char left)
{
    set_at(pd, 3, 3, center);
    set_at(pd, 3, 2, up);
    set_at(pd, 4, 3, right);
    set_at(pd, 3, 4, down);
    set_at(pd, 2, 3, left);
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
