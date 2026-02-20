#include <openglad/data/smooth.h>
#include <openglad/data/pixie_data.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <cstdint>

#include "unit/unit.h"

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
