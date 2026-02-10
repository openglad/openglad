#include "graph.h"
#include "test_framework.h"

extern screen* myscreen;

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
}
REGISTER_TEST(test_smooth_reset);
