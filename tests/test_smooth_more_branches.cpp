#include <openglad/legacy/graph.h>
#include <openglad/render/smooth.h>
#include <openglad/runtime/game_context.h>
#include "test_framework.h"

#include <array>
#include <memory>

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
} // namespace

void test_smooth_dark_grass_rubble_and_corner_branches()
{
    FixedRandom rng0(0); // makes rng(20)==0 so rubble is placed deterministically
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    // 5x5 to keep everything in bounds.
    PixieData grid = make_grid(5, 5, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    // Center tile as dark grass.
    at(grid, 2, 2) = PIX_GRASS_DARK_1;

    // Surroundings: around == (TO_LEFT | TO_RIGHT | TO_UP) => bottom middle.
    // This branch places PIX_GRASS_RUBBLE when rng(20) == 0.
    at(grid, 1, 2) = PIX_GRASS_DARK_1; // left
    at(grid, 3, 2) = PIX_GRASS_DARK_1; // right
    at(grid, 2, 1) = PIX_GRASS_DARK_1; // up
    at(grid, 2, 3) = PIX_GRASS1;       // down (not dark grass)
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_GRASS_RUBBLE, (int)at(grid, 2, 2),
        "dark grass bottom-middle should place rubble when rng(20)==0");

    // around == (TO_LEFT | TO_DOWN) (top right) selects PIX_GRASS_DARK_LL when right is grass.
    at(grid, 2, 2) = PIX_GRASS_DARK_1; // reset center
    at(grid, 1, 2) = PIX_GRASS_DARK_1; // left dark
    at(grid, 2, 3) = PIX_GRASS_DARK_1; // down dark
    at(grid, 3, 2) = PIX_GRASS1;       // right is grass genre
    at(grid, 2, 1) = PIX_GRASS1;       // up not dark
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)at(grid, 2, 2),
        "dark grass top-right should become LL edge when right is grass");
}
REGISTER_TEST(test_smooth_dark_grass_rubble_and_corner_branches);

void test_smooth_dark_grass_to_around_rng_switch_cases()
{
    // Cover each rng(4) case in TYPE_GRASS_DARK + around==TO_AROUND.
    for (int fixed = 0; fixed < 4; fixed++)
    {
        FixedRandom rng(static_cast<Uint32>(fixed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        PixieData grid = make_grid(3, 3, PIX_GRASS_DARK_1);
        smoother s;
        s.set_target(grid);

        // Center is dark grass, all neighbors dark grass => around == TO_AROUND.
        at(grid, 1, 1) = PIX_GRASS_DARK_1;
        (void)s.smooth(1, 1);

        const int v = (int)at(grid, 1, 1);
        TEST_ASSERT(v == PIX_GRASS_DARK_1 || v == PIX_GRASS_DARK_2 || v == PIX_GRASS_DARK_3 || v == PIX_GRASS_DARK_4,
            "dark grass TO_AROUND should select a dark grass variant");
    }
}
REGISTER_TEST(test_smooth_dark_grass_to_around_rng_switch_cases);

void test_smooth_wall_vertical_and_base_cases()
{
    FixedRandom rng0(0); // rng(10)==0 for crack selection
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(5, 6, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    // Case: around == 11 => base middle with crack selection.
    // Up/Left/Right are walls; down is not.
    at(grid, 2, 2) = PIX_H_WALL1; // center wall
    at(grid, 2, 1) = PIX_H_WALL1; // up wall
    at(grid, 1, 2) = PIX_H_WALL1; // left wall
    at(grid, 3, 2) = PIX_H_WALL1; // right wall
    at(grid, 2, 3) = PIX_GRASS1;  // down not wall
    (void)s.smooth(2, 2);
    TEST_ASSERT_EQ((int)PIX_WALLSIDE_CRACK_C1, (int)at(grid, 2, 2),
        "wall base middle should choose crack when rng(10)==0");

    // Case: around == 5 => vertical wall, and if y+2 is wall selects PIX_WALL2.
    at(grid, 2, 3) = PIX_H_WALL1; // new center wall at (2,3)
    at(grid, 2, 2) = PIX_H_WALL1; // up wall
    at(grid, 2, 4) = PIX_H_WALL1; // down wall
    at(grid, 2, 5) = PIX_H_WALL1; // y+2 wall
    at(grid, 1, 3) = PIX_GRASS1;  // left not wall
    at(grid, 3, 3) = PIX_GRASS1;  // right not wall
    (void)s.smooth(2, 3);
    TEST_ASSERT_EQ((int)PIX_WALL2, (int)at(grid, 2, 3),
        "vertical wall should become PIX_WALL2 when wall continues two tiles down");
}
REGISTER_TEST(test_smooth_wall_vertical_and_base_cases);

void test_smooth_dirt_and_dark_dirt_to_around_rng_switch_cases()
{
    // Dirt TYPE_DIRT around==TO_AROUND uses rng(3) switch.
    for (int fixed = 0; fixed < 3; fixed++)
    {
        FixedRandom rng(static_cast<Uint32>(fixed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        PixieData dirt = make_grid(3, 3, PIX_DIRT_1);
        smoother s;
        s.set_target(dirt);
        (void)s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_DIRT_1, (int)at(dirt, 1, 1),
            "dirt TO_AROUND selects PIX_DIRT_1");

        PixieData dd = make_grid(3, 3, PIX_DIRT_DARK_1);
        s.set_target(dd);
        (void)s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_DIRT_DARK_1, (int)at(dd, 1, 1),
            "dark dirt TO_AROUND selects PIX_DIRT_DARK_1");
    }
}
REGISTER_TEST(test_smooth_dirt_and_dark_dirt_to_around_rng_switch_cases);

