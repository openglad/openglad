#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/data/smooth.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

static void run_smooth_branch_outputs_with_fixed_rng();
static PixieData make_center_pattern(unsigned char fill, unsigned char center,
                                     unsigned char up, unsigned char right,
                                     unsigned char down, unsigned char left,
                                     unsigned char upleft, unsigned char upright,
                                     unsigned char downleft, unsigned char downright);

// ---------------------------------------------------------------------------
// smoother query_x_y
// ---------------------------------------------------------------------------

void test_smooth_query_x_y_no_grid()
{
    smoother s;
    // No grid set - should return PIX_GRASS1
    Sint32 result = s.query_x_y(0, 0);
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)result, "no grid returns PIX_GRASS1");
}
REGISTER_TEST(test_smooth_query_x_y_no_grid);

void test_smooth_query_x_y_negative()
{
    smoother s;
    Sint32 result = s.query_x_y(-1, -1);
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)result, "negative returns PIX_GRASS1");
}
REGISTER_TEST(test_smooth_query_x_y_negative);

void test_smooth_query_x_y_with_grid()
{
    // Use the level's grid
    myscreen->level_data.create_new_grid();
    smoother s;
    s.set_target(myscreen->level_data.grid);

    // Valid query
    Sint32 result = s.query_x_y(0, 0);
    TEST_ASSERT(result >= 0, "valid position returns non-negative");

    // Out of bounds
    result = s.query_x_y(9999, 9999);
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)result, "out of bounds returns PIX_GRASS1");
}
REGISTER_TEST(test_smooth_query_x_y_with_grid);

// ---------------------------------------------------------------------------
// smoother query_genre_x_y
// ---------------------------------------------------------------------------

void test_smooth_query_genre_grass()
{
    // Create a grid with known grass values
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_GRASS1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_GRASS, (int)genre, "grass tile returns TYPE_GRASS");
}
REGISTER_TEST(test_smooth_query_genre_grass);

void test_smooth_query_genre_water()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_WATER1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_WATER, (int)genre, "water tile returns TYPE_WATER");
}
REGISTER_TEST(test_smooth_query_genre_water);

void test_smooth_query_genre_wall()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_H_WALL1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_WALL, (int)genre, "wall tile returns TYPE_WALL");
}
REGISTER_TEST(test_smooth_query_genre_wall);

void test_smooth_query_genre_trees()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_TREE_B1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_TREES, (int)genre, "tree tile returns TYPE_TREES");
}
REGISTER_TEST(test_smooth_query_genre_trees);

void test_smooth_query_genre_dirt()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_DIRT_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_DIRT, (int)genre, "dirt tile returns TYPE_DIRT");
}
REGISTER_TEST(test_smooth_query_genre_dirt);

void test_smooth_query_genre_carpet()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_CARPET_M;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_CARPET, (int)genre, "carpet tile returns TYPE_CARPET");
}
REGISTER_TEST(test_smooth_query_genre_carpet);

void test_smooth_query_genre_cobble()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_COBBLE_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_COBBLE, (int)genre, "cobble tile returns TYPE_COBBLE");
}
REGISTER_TEST(test_smooth_query_genre_cobble);

void test_smooth_query_genre_dark_grass()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_GRASS_DARK_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_GRASS_DARK, (int)genre, "dark grass returns TYPE_GRASS_DARK");
}
REGISTER_TEST(test_smooth_query_genre_dark_grass);

void test_smooth_query_genre_light_grass()
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[i] = PIX_GRASS_LIGHT_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    TEST_ASSERT_EQ(TYPE_GRASS_LIGHT, (int)genre, "light grass returns TYPE_GRASS_LIGHT");
}
REGISTER_TEST(test_smooth_query_genre_light_grass);

// ---------------------------------------------------------------------------
// smoother smooth() - full grid smooth
// ---------------------------------------------------------------------------

void test_smooth_smooth_full_grid()
{
    myscreen->level_data.create_new_grid();
    smoother s;
    s.set_target(myscreen->level_data.grid);

    // Should not crash
    Sint32 result = s.smooth();
    (void)result;
}
REGISTER_TEST(test_smooth_smooth_full_grid);

// ---------------------------------------------------------------------------
// smoother smooth(x,y) - single cell smooth for various terrain types
// ---------------------------------------------------------------------------

void test_smooth_smooth_single_grass()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_GRASS1;

    smoother s;
    s.set_target(pd);

    Sint32 result = s.smooth(2, 2);
    (void)result;
}
REGISTER_TEST(test_smooth_smooth_single_grass);

void test_smooth_smooth_water_surrounded()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    // Fill with water
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_WATER1;

    smoother s;
    s.set_target(pd);
    Sint32 result = s.smooth(2, 2);
    (void)result;
}
REGISTER_TEST(test_smooth_smooth_water_surrounded);

void test_smooth_smooth_wall_surrounded()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_H_WALL1;

    smoother s;
    s.set_target(pd);
    Sint32 result = s.smooth(2, 2);
    (void)result;
}
REGISTER_TEST(test_smooth_smooth_wall_surrounded);

void test_smooth_smooth_grass_water_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    // Top half grass, bottom half water
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++)
            pd.data[j*5+i] = (j < 3) ? PIX_GRASS1 : PIX_WATER1;

    smoother s;
    s.set_target(pd);
    // Smooth the border cells
    s.smooth(2, 2);
    s.smooth(2, 3);
}
REGISTER_TEST(test_smooth_smooth_grass_water_border);

void test_smooth_smooth_tree_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    // Grass with a tree in the middle
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_GRASS1;
    pd.data[12] = PIX_TREE_B1;

    smoother s;
    s.set_target(pd);
    s.smooth(2, 2);
}
REGISTER_TEST(test_smooth_smooth_tree_border);

void test_smooth_smooth_dirt_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    // Left half dirt, right half grass
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++)
            pd.data[j*5+i] = (i < 3) ? PIX_DIRT_1 : PIX_GRASS1;

    smoother s;
    s.set_target(pd);
    s.smooth(2, 2);
    s.smooth(3, 2);
}
REGISTER_TEST(test_smooth_smooth_dirt_border);

void test_smooth_smooth_carpet_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    // All carpet except edges
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_CARPET_M;
    // Grass border
    for (int i = 0; i < 5; i++) {
        pd.data[i] = PIX_GRASS1;
        pd.data[20+i] = PIX_GRASS1;
        pd.data[i*5] = PIX_GRASS1;
        pd.data[i*5+4] = PIX_GRASS1;
    }

    smoother s;
    s.set_target(pd);
    s.smooth(2, 2);
    s.smooth(1, 1);
    s.smooth(3, 3);
}
REGISTER_TEST(test_smooth_smooth_carpet_border);

void test_smooth_smooth_cobble_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_COBBLE_1;
    pd.data[0] = PIX_GRASS1;

    smoother s;
    s.set_target(pd);
    s.smooth(0, 0);
    s.smooth(1, 0);
    s.smooth(0, 1);
}
REGISTER_TEST(test_smooth_smooth_cobble_border);

void test_smooth_smooth_dark_grass_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++)
            pd.data[j*5+i] = (j < 3) ? PIX_GRASS_DARK_1 : PIX_GRASS1;

    smoother s;
    s.set_target(pd);
    s.smooth(2, 2);
    s.smooth(2, 3);
}
REGISTER_TEST(test_smooth_smooth_dark_grass_border);

void test_smooth_smooth_light_grass_border()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 5; i++)
            pd.data[j*5+i] = (j < 3) ? PIX_GRASS_LIGHT_1 : PIX_GRASS1;

    smoother s;
    s.set_target(pd);
    s.smooth(2, 2);
    s.smooth(2, 3);
}
REGISTER_TEST(test_smooth_smooth_light_grass_border);

void test_smooth_smooth_all_edges()
{
    PixieData pd;
    pd.w = 5;
    pd.h = 5;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; i++)
        pd.data[i] = PIX_GRASS1;

    smoother s;
    s.set_target(pd);

    // Smooth every cell including edges
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            s.smooth(x, y);
}
REGISTER_TEST(test_smooth_smooth_all_edges);

void test_smooth_smooth_mixed_terrain()
{
    PixieData pd;
    pd.w = 7;
    pd.h = 7;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(49);
    // Create a checkerboard of different terrain types
    unsigned char types[] = { PIX_GRASS1, PIX_WATER1, PIX_DIRT_1, PIX_H_WALL1 };
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++)
            pd.data[j*7+i] = types[(i+j)%4];

    smoother s;
    s.set_target(pd);

    // Smooth the entire grid
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++)
            s.smooth(x, y);
}
REGISTER_TEST(test_smooth_smooth_mixed_terrain);

// ---------------------------------------------------------------------------
// smoother reset
// ---------------------------------------------------------------------------

void test_smooth_reset()
{
    smoother s;
    myscreen->level_data.create_new_grid();
    s.set_target(myscreen->level_data.grid);

    Sint32 before = s.query_x_y(0, 0);
    (void)before;

    s.reset();
    Sint32 after = s.query_x_y(0, 0);
    TEST_ASSERT_EQ((int)PIX_GRASS1, (int)after, "after reset returns PIX_GRASS1");

    run_smooth_branch_outputs_with_fixed_rng();
}
REGISTER_TEST(test_smooth_reset);

void test_smooth_dark_grass_round7_branch_matrix_338_448()
{
    GameContext test_ctx;
    FixedRandom fixed0(0);
    FixedRandom fixed1(1);
    test_ctx.rng = &fixed0;
    set_global_context(&test_ctx);

    // around == (TO_UP | TO_DOWN | TO_LEFT): right-middle branch, rng(2)==1
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        test_ctx.rng = &fixed1;
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_R2, (int)s.query_x_y(1, 1), "right-middle dark-grass branch should choose R2");
    }

    // around == (TO_LEFT | TO_DOWN): top-right branch with right grass / non-grass
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)s.query_x_y(1, 1), "top-right branch with grass right should map to LL");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_B2, (int)s.query_x_y(1, 1), "top-right branch with non-grass right should map to B2");
    }

    // around == (TO_LEFT | TO_RIGHT | TO_UP): bottom-middle branch with rubble path.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        test_ctx.rng = &fixed0; // rng(2)==0 then rng(20)==0 => rubble override
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_RUBBLE, (int)s.query_x_y(1, 1), "bottom-middle branch should allow rubble override");
    }

    // around == TO_LEFT : right-thin branch with grass/non-grass right.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)s.query_x_y(1, 1), "right-thin branch with grass right should map LL");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)s.query_x_y(1, 1), "right-thin branch with non-grass right should map B1");
    }

    // around masks for remaining explicit branches.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP|TO_RIGHT|TO_DOWN
        smoother s;
        s.set_target(pd);
        test_ctx.rng = &fixed1; // pick one of the random dark variants
        s.smooth(1, 1);
        TEST_ASSERT(s.query_x_y(1, 1) >= PIX_GRASS_DARK_1 && s.query_x_y(1, 1) <= PIX_GRASS_DARK_4,
                    "left-middle/top-left branch should choose dark-grass variant");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP|TO_DOWN
        smoother s;
        s.set_target(pd);
        test_ctx.rng = &fixed0;
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_R1, (int)s.query_x_y(1, 1), "center-vertical branch should map to R1/R2");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_DOWN
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)s.query_x_y(1, 1), "top-alone branch should map LL/B1");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP|TO_RIGHT
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)s.query_x_y(1, 1), "bottom-left branch should map UR/B1");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_RIGHT
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)s.query_x_y(1, 1), "left-alone branch should map UR/B1");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)s.query_x_y(1, 1), "bottom-alone branch should map UR/B1");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // around == 0
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_DARK_1, (int)s.query_x_y(1, 1), "default dark-grass branch should map to dark_1");
    }

    set_global_context(nullptr);
}
REGISTER_TEST(test_smooth_dark_grass_round7_branch_matrix_338_448);

static PixieData make_uniform_grid(int w, int h, unsigned char fill)
{
    PixieData pd;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(w * h);
    for (int i = 0; i < w * h; ++i)
        pd.data[i] = fill;
    return pd;
}

static void set_tile(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[y * pd.w + x] = v;
}

static PixieData make_center_pattern(unsigned char fill, unsigned char center,
                                     unsigned char up, unsigned char right,
                                     unsigned char down, unsigned char left,
                                     unsigned char upleft, unsigned char upright,
                                     unsigned char downleft, unsigned char downright)
{
    PixieData pd = make_uniform_grid(3, 3, fill);
    set_tile(pd, 1, 1, center);
    set_tile(pd, 1, 0, up);
    set_tile(pd, 2, 1, right);
    set_tile(pd, 1, 2, down);
    set_tile(pd, 0, 1, left);
    set_tile(pd, 0, 0, upleft);
    set_tile(pd, 2, 0, upright);
    set_tile(pd, 0, 2, downleft);
    set_tile(pd, 2, 2, downright);
    return pd;
}

static void run_smooth_branch_outputs_with_fixed_rng()
{
    FixedRandom fixed0(0);
    GameContext test_ctx;
    test_ctx.rng = &fixed0;
    set_global_context(&test_ctx);

    // Dirt corner case: around == (TO_LEFT | TO_DOWN) => PIX_DIRTGRASS_LL1
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_GRASS1);
        set_tile(pd, 1, 1, PIX_DIRT_1);
        set_tile(pd, 0, 1, PIX_DIRT_1);
        set_tile(pd, 1, 2, PIX_DIRT_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_DIRTGRASS_LL1, (int)s.query_x_y(1, 1), "dirt top-right edge should map to LL transition");
    }

    // Dark dirt corner case: around == (TO_DOWN | TO_RIGHT) => PIX_DIRTGRASS_DARK_LR1
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_GRASS1);
        set_tile(pd, 1, 1, PIX_DIRT_DARK_1);
        set_tile(pd, 2, 1, PIX_DIRT_DARK_1);
        set_tile(pd, 1, 2, PIX_DIRT_DARK_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_DIRTGRASS_DARK_LR1, (int)s.query_x_y(1, 1), "dark dirt top-left edge should map to LR transition");
    }

    // Cobble deterministic RNG branches.
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_COBBLE_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_COBBLE_1, (int)s.query_x_y(1, 1), "cobble rng=0 should choose variant 1");
    }

    FixedRandom fixed3(3);
    test_ctx.rng = &fixed3;
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_COBBLE_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_COBBLE_4, (int)s.query_x_y(1, 1), "cobble rng=3 should choose variant 4");
    }

    // Wall arrow slit variants based on tile above.
    test_ctx.rng = &fixed0;
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_H_WALL1);
        set_tile(pd, 1, 1, PIX_WALL_ARROW_GRASS);
        set_tile(pd, 1, 0, PIX_PAVEMENT1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WALL4, (int)s.query_x_y(1, 1), "arrow slit over pavement should become stone arrow wall");
    }
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_H_WALL1);
        set_tile(pd, 1, 1, PIX_WALL_ARROW_GRASS);
        set_tile(pd, 1, 0, PIX_FLOOR1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WALL_ARROW_FLOOR, (int)s.query_x_y(1, 1), "arrow slit over floor should become floor arrow wall");
    }

    // Wall base crack branch (around == 11 and rng(10) == 0).
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_GRASS1);
        set_tile(pd, 1, 1, PIX_H_WALL1);
        set_tile(pd, 0, 1, PIX_H_WALL1);
        set_tile(pd, 2, 1, PIX_H_WALL1);
        set_tile(pd, 1, 0, PIX_H_WALL1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WALLSIDE_CRACK_C1, (int)s.query_x_y(1, 1), "wall base should choose crack when rng hits 0");
    }

    // Unknown type should remain unchanged.
    {
        PixieData pd = make_uniform_grid(3, 3, 222);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ(222, (int)s.query_x_y(1, 1), "unknown tile type should remain unchanged");
    }

    // Grass to water corner transitions.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                                           PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASSWATER_LL, (int)s.query_x_y(1, 1), "grass-water LL transition");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS1,
                                           PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_WATER1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASSWATER_UR, (int)s.query_x_y(1, 1), "grass-water UR transition");
    }

    // Carpet and light-grass shape selection.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_CARPET_M,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_CARPET_SMALL_TINY, (int)s.query_x_y(1, 1), "isolated carpet should become tiny");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_LIGHT_1,
                                           PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_GRASS_LIGHT_LEFT_BOTTOM, (int)s.query_x_y(1, 1), "light grass up+right mask should map to left-bottom variant");
    }

    // Water edge variants.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_WATER1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)s.query_x_y(1, 1), "water with only up neighbor should map LL/left cap");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)s.query_x_y(1, 1), "water with only right neighbor should map UL/upper cap");
    }

    // Trees: top-middle and center-vertical variants.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_TREE_T1, (int)s.query_x_y(1, 1), "trees top-middle should map to top tile");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_TREE_MT, (int)s.query_x_y(1, 1), "trees vertical should map to trunk tile");
    }

    set_global_context(nullptr);
}

void test_smooth_round11_water_and_tree_edge_masks_662_720()
{
    FixedRandom fixed0(0);
    GameContext test_ctx;
    test_ctx.rng = &fixed0;
    set_global_context(&test_ctx);

    // TYPE_WATER diagonal-corner masks (smooth.cpp:662-669).
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_WATER1, PIX_GRASS1, PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WATERGRASS_LR, (int)s.query_x_y(1, 1), "water up+left should map LR");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_WATER1, PIX_WATER1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)s.query_x_y(1, 1), "water down+right should map UL");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_WATERGRASS_UR, (int)s.query_x_y(1, 1), "water down+left should map UR");
    }

    // TYPE_TREES TO_AROUND edge-side selection (smooth.cpp:715-720).
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_TREE_MR, (int)s.query_x_y(1, 1), "trees with missing upper-right should map to right edge");
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        TEST_ASSERT_EQ((int)PIX_TREE_ML, (int)s.query_x_y(1, 1), "trees with missing upper-left should map to left edge");
    }

    set_global_context(nullptr);
}
REGISTER_TEST(test_smooth_round11_water_and_tree_edge_masks_662_720);
