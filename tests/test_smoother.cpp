#include "smooth.h"
#include "base.h"
#include "pixie_data.h"
#include "test_framework.h"

#include <memory>

static PixieData make_grid(unsigned char w, unsigned char h, unsigned char fill)
{
    auto* raw = new unsigned char[w * h];
    for (int i = 0; i < w * h; i++)
        raw[i] = fill;
    return PixieData(1, w, h, raw);
}

class TestSmoother : public smoother
{
public:
    using smoother::surrounds;
};

void test_smoother_query_genre_maps_known_tiles()
{
    // Place representative tiles for each genre and validate mapping.
    PixieData grid = make_grid(10, 1, PIX_GRASS1);
    unsigned char* g = grid.data.get();

    g[0] = PIX_GRASS1;
    g[1] = PIX_WATER1;
    g[2] = PIX_DIRT_1;
    g[3] = PIX_DIRT_DARK_1;
    g[4] = PIX_CARPET_M;
    g[5] = PIX_H_WALL1;
    g[6] = PIX_TREE_T1;
    g[7] = PIX_COBBLE_1;
    g[8] = PIX_GRASS_DARK_1;
    g[9] = PIX_GRASS_LIGHT_1;

    smoother s;
    s.set_target(grid);

    TEST_ASSERT_EQ(TYPE_GRASS, s.query_genre_x_y(0, 0), "grass should map to TYPE_GRASS");
    TEST_ASSERT_EQ(TYPE_WATER, s.query_genre_x_y(1, 0), "water should map to TYPE_WATER");
    TEST_ASSERT_EQ(TYPE_DIRT, s.query_genre_x_y(2, 0), "dirt should map to TYPE_DIRT");
    TEST_ASSERT_EQ(TYPE_DIRT_DARK, s.query_genre_x_y(3, 0), "dark dirt should map to TYPE_DIRT_DARK");
    TEST_ASSERT_EQ(TYPE_CARPET, s.query_genre_x_y(4, 0), "carpet should map to TYPE_CARPET");
    TEST_ASSERT_EQ(TYPE_WALL, s.query_genre_x_y(5, 0), "wall should map to TYPE_WALL");
    TEST_ASSERT_EQ(TYPE_TREES, s.query_genre_x_y(6, 0), "trees should map to TYPE_TREES");
    TEST_ASSERT_EQ(TYPE_COBBLE, s.query_genre_x_y(7, 0), "cobble should map to TYPE_COBBLE");
    TEST_ASSERT_EQ(TYPE_GRASS_DARK, s.query_genre_x_y(8, 0), "dark grass should map to TYPE_GRASS_DARK");
    TEST_ASSERT_EQ(TYPE_GRASS_LIGHT, s.query_genre_x_y(9, 0), "light grass should map to TYPE_GRASS_LIGHT");
}
REGISTER_TEST(test_smoother_query_genre_maps_known_tiles);

void test_smoother_surrounds_bitmask_counts_neighbors()
{
    // 3x3 grid with center water and water on up+left, others grass.
    PixieData grid = make_grid(3, 3, PIX_GRASS1);
    unsigned char* g = grid.data.get();
    g[1 + 0 * 3] = PIX_WATER1; // up
    g[0 + 1 * 3] = PIX_WATER1; // left
    g[1 + 1 * 3] = PIX_WATER1; // center

    TestSmoother s;
    s.set_target(grid);

    Sint32 mask = s.surrounds(1, 1, TYPE_WATER);
    // up=1, right=2, down=4, left=8
    TEST_ASSERT_EQ(1 + 8, mask, "surrounds should return bitmask of matching neighbors");
}
REGISTER_TEST(test_smoother_surrounds_bitmask_counts_neighbors);
