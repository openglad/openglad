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
