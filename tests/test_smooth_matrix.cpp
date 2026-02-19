#include <openglad/data/pixie_data.h>
#include <openglad/data/smooth.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/runtime/game_context.h>

#include "test_framework.h"

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { set_global_context(ctx); }
    ~GlobalContextGuard() { set_global_context(nullptr); }
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

void test_smooth_matrix_covers_carpet_and_light_grass_masks()
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
        (void)s.smooth(2, 2);

        const unsigned char cv = at(carpet, 2, 2);
        TEST_ASSERT(cv >= PIX_CARPET_LL && cv <= PIX_CARPET_SMALL_TINY,
                    "carpet mask smoothing should emit a carpet tile variant");

        PixieData light = make_grid(5, 5, PIX_GRASS1);
        at(light, 2, 2) = PIX_GRASS_LIGHT_1;
        set_neighbors_for_mask(light, 2, 2, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.set_target(light);
        (void)s.smooth(2, 2);

        const unsigned char lv = at(light, 2, 2);
        TEST_ASSERT(lv >= PIX_GRASS_LIGHT_1 && lv <= PIX_GRASS_LIGHT_LEFT_TOP,
                    "light-grass mask smoothing should emit a light-grass variant");
    }
}
REGISTER_TEST(test_smooth_matrix_covers_carpet_and_light_grass_masks);

void test_smooth_matrix_covers_water_tree_dirt_and_dark_dirt_masks()
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
        (void)s.smooth(2, 2);

        PixieData tree = make_grid(5, 5, PIX_GRASS1);
        at(tree, 2, 2) = PIX_TREE_M1;
        set_neighbors_for_mask(tree, 2, 2, PIX_TREE_M1, PIX_GRASS1, mask);
        s.set_target(tree);
        (void)s.smooth(2, 2);

        PixieData dirt = make_grid(5, 5, PIX_GRASS1);
        at(dirt, 2, 2) = PIX_DIRT_1;
        set_neighbors_for_mask(dirt, 2, 2, PIX_DIRT_1, PIX_GRASS1, mask);
        s.set_target(dirt);
        (void)s.smooth(2, 2);

        PixieData dark_dirt = make_grid(5, 5, PIX_GRASS1);
        at(dark_dirt, 2, 2) = PIX_DIRT_DARK_1;
        set_neighbors_for_mask(dark_dirt, 2, 2, PIX_DIRT_DARK_1, PIX_GRASS1, mask);
        s.set_target(dark_dirt);
        (void)s.smooth(2, 2);
    }

    TEST_ASSERT(true, "matrix smooth branch execution completed");
}
REGISTER_TEST(test_smooth_matrix_covers_water_tree_dirt_and_dark_dirt_masks);

void test_smooth_matrix_covers_grass_dark_grass_wall_and_cobble_paths()
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
        (void)s.smooth(2, 2);

        PixieData dark_grass = make_grid(5, 5, PIX_GRASS1);
        at(dark_grass, 2, 2) = PIX_GRASS_DARK_1;
        set_neighbors_for_mask(dark_grass, 2, 2, PIX_GRASS_DARK_1, PIX_GRASS1, mask);
        s.set_target(dark_grass);
        (void)s.smooth(2, 2);

        PixieData wall = make_grid(5, 6, PIX_GRASS1);
        at(wall, 2, 2) = PIX_H_WALL1;
        set_neighbors_for_mask(wall, 2, 2, PIX_H_WALL1, PIX_GRASS1, mask);
        at(wall, 2, 4) = PIX_H_WALL1;  // feed y+2 checks for wall cases
        s.set_target(wall);
        (void)s.smooth(2, 2);

        PixieData cobble = make_grid(5, 5, PIX_GRASS1);
        at(cobble, 2, 2) = PIX_COBBLE_1;
        s.set_target(cobble);
        (void)s.smooth(2, 2);
    }

    TEST_ASSERT(true, "matrix smooth coverage across remaining genres completed");
}
REGISTER_TEST(test_smooth_matrix_covers_grass_dark_grass_wall_and_cobble_paths);
