#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/core/irandom.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <cstdint>

// --- From test_smooth_coverage_push.cpp ---
namespace detail_smooth_coverage_push {
namespace {

PixieData make_grid(unsigned char fill)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = 5;
    pd.h = 5;
    pd.data = std::make_unique<unsigned char[]>(25);
    for (int i = 0; i < 25; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
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
    pd.data[static_cast<std::size_t>(2 + 2 * pd.w)] = center;
    pd.data[2 + 1 * pd.w] = up;
    pd.data[static_cast<std::size_t>(3 + 2 * pd.w)] = right;
    pd.data[static_cast<std::size_t>(2 + 3 * pd.w)] = down;
    pd.data[static_cast<std::size_t>(1 + 2 * pd.w)] = left;
    pd.data[1 + 1 * pd.w] = ul;
    pd.data[3 + 1 * pd.w] = ur;
    pd.data[static_cast<std::size_t>(1 + 3 * pd.w)] = dl;
    pd.data[static_cast<std::size_t>(3 + 3 * pd.w)] = dr;
}

unsigned char center_value(const PixieData& pd)
{
    return pd.data[static_cast<std::size_t>(2 + 2 * pd.w)];
}

} // namespace

TEST(SmoothUnit, smooth_query_and_genre_basics)
{
    smoother s;
    ASSERT_TRUE(s.query_x_y(0, 0) == PIX_GRASS1);
    ASSERT_TRUE(s.query_x_y(-1, 0) == PIX_GRASS1);
    ASSERT_TRUE(s.query_genre_x_y(0, 0) == TYPE_GRASS);

    PixieData pd = make_grid(PIX_CARPET_M);
    s.set_target(pd);
    ASSERT_TRUE(s.query_x_y(2, 2) == PIX_CARPET_M);
    ASSERT_TRUE(s.query_x_y(99, 99) == PIX_GRASS1);
    ASSERT_TRUE(s.query_genre_x_y(2, 2) == TYPE_CARPET);

    pd.data[static_cast<std::size_t>(2 + 2 * pd.w)] = 255;
    ASSERT_TRUE(s.query_genre_x_y(2, 2) == TYPE_UNKNOWN);

    s.reset();
    ASSERT_TRUE(s.smooth() == 0);
}

TEST(SmoothUnit, smooth_branch_matrix_on_center_tile)
{
    FixedRandom rng(0);
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;

    // Grass -> explicit grass/water blend corner and random fallback.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_GRASS1, PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                      PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1);
        s.set_target(pd);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_GRASSWATER_LL);

        set_neighbors(pd, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_GRASS1);
    }

    // Dark grass branches including rubble path and side-edge selection.
    {
        PixieData pd = make_grid(PIX_GRASS_DARK_1);
        set_neighbors(pd, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_GRASS_RUBBLE);

        set_neighbors(pd, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_WALL2, PIX_WALL2,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_GRASS_DARK_R1);
    }

    // Carpet around=0 and around=15 preserving M/M2.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_CARPET_M, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_CARPET_SMALL_TINY);

        set_neighbors(pd, PIX_CARPET_M2, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M,
                      PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M);
        s.smooth(2, 2);
        const unsigned char carpet_center = center_value(pd);
        ASSERT_TRUE(carpet_center == PIX_CARPET_M || carpet_center == PIX_CARPET_M2);
    }

    // Wall arrow-slit variants.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_WALL_ARROW_GRASS, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_WALL_ARROW_GRASS);

        set_neighbors(pd, PIX_WALL_ARROW_GRASS, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_WALL_ARROW_GRASS_DARK);

        set_neighbors(pd, PIX_WALL_ARROW_GRASS, PIX_FLOOR1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_WALL_ARROW_FLOOR);
    }

    // Water, trees, dirt, dark dirt, cobble, and unknown default.
    {
        PixieData pd = make_grid(PIX_GRASS1);
        set_neighbors(pd, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.set_target(pd);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_WATERGRASS_LL);

        set_neighbors(pd, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_TREE_MT);

        set_neighbors(pd, PIX_DIRT_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1, PIX_DIRT_1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_DIRTGRASS_LL1);

        set_neighbors(pd, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_DIRTGRASS_DARK_UR1);

        set_neighbors(pd, PIX_COBBLE_4, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == PIX_COBBLE_1);

        set_neighbors(pd, 255, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                      PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        s.smooth(2, 2);
        ASSERT_TRUE(center_value(pd) == 255);
    }

    pop_test_context();
}
} // namespace detail_smooth_coverage_push

// --- From test_smooth_r11.cpp ---
namespace detail_smooth_r11 {
namespace {

PixieData make_grid(unsigned char fill)
{
    PixieData pd;
    pd.frames = 1;
    pd.w = 7;
    pd.h = 7;
    pd.data = std::make_unique<unsigned char[]>(49);
    for (int i = 0; i < 49; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
}

[[maybe_unused]] unsigned char get_at(const PixieData& pd, int x, int y)
{
    return pd.data[static_cast<std::size_t>(x + y * pd.w)];
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

TEST(SmoothUnit, smooth_r11_out_of_range_neighbours_read_as_grass_and_writes_are_clamped)
{
    FixedRandom rng0(0);
    smoother s;
    s.set_rng(&rng0);
    ASSERT_EQ(PIX_GRASS1, s.query_x_y(-2, 0)) << "no target yet: every query answers PIX_GRASS1";
    ASSERT_EQ(PIX_GRASS1, s.query_x_y(0, -2)) << "no target yet: every query answers PIX_GRASS1";

    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);
    ASSERT_EQ(PIX_GRASS1, s.query_x_y(100, 100)) << "past maxx/maxy reads PIX_GRASS1, never grid memory";

    // The corner tile is the only place the out-of-range rule is observable:
    // (-1,-1)..(0,-1) all read PIX_GRASS1, so the all-grass surround takes the
    // grass_variants arm and FixedRandom(0) picks variant 0 == PIX_GRASS1.
    s.smooth(0, 0);
    ASSERT_EQ(PIX_GRASS1, s.query_x_y(0, 0))
        << "smooth(0,0): out-of-range neighbours count as grass, so the corner stays PIX_GRASS1";

    // set_x_y mirrors that bounds check: a smooth outside the grid writes nothing.
    s.smooth(-1, -1);
    s.smooth(100, 100);
    for (int i = 0; i < 49; ++i)
        ASSERT_EQ(PIX_GRASS1, static_cast<int>(pd.data[static_cast<std::size_t>(i)]))
            << "out-of-range smooth must not write into the grid, cell " << i;

    s.reset();
    ASSERT_EQ(0, s.smooth()) << "reset() drops the target, so the full-map smooth is a no-op";
}

TEST(SmoothUnit, smooth_r11_dark_grass_and_wall_and_water_branches)
{
    FixedRandom rng0(0);
    GameContext gc;
    gc.rng = &rng0;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    // Every case below sets the centre tile at (3,3) plus its eight neighbours
    // and pins the variant smooth() writes back. FixedRandom(0) makes every
    // next_random() draw 0, so each table lookup is the first entry.

    // around == TO_LEFT, right neighbour is grass -> lower-left edge.
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_DARK_LL, s.query_x_y(3, 3))
        << "dark grass, around==TO_LEFT with grass to the right -> PIX_GRASS_DARK_LL";

    // Same mask, right neighbour is water instead of grass -> bottom edge.
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_WATER1, PIX_GRASS1, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_DARK_B1, s.query_x_y(3, 3))
        << "dark grass, around==TO_LEFT with non-grass to the right -> PIX_GRASS_DARK_B1";

    // around == (TO_UP|TO_DOWN) -> vertical centre, right-edge variant 0.
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_DARK_R1, s.query_x_y(3, 3))
        << "dark grass, around==TO_UP|TO_DOWN -> grass_dark_right[0] == PIX_GRASS_DARK_R1";

    // around == TO_DOWN with grass to the right -> lower-left edge.
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_DARK_LL, s.query_x_y(3, 3))
        << "dark grass, around==TO_DOWN with grass right/up -> PIX_GRASS_DARK_LL";

    // around == TO_RIGHT with grass to the left -> upper-right edge.
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_DARK_UR, s.query_x_y(3, 3))
        << "dark grass, around==TO_RIGHT with grass to the left -> PIX_GRASS_DARK_UR";

    // around == TO_UP with water below -> bottom edge (not the UR corner).
    set_neighbors(pd, PIX_GRASS_DARK_1,
                  PIX_GRASS_DARK_1, PIX_GRASS1, PIX_WATER1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_DARK_B1, s.query_x_y(3, 3))
        << "dark grass, around==TO_UP with non-grass below -> PIX_GRASS_DARK_B1";

    // Carpet fully surrounded -> carpet_by_surround[15].
    set_neighbors(pd, PIX_CARPET_M2,
                  PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M,
                  PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M, PIX_CARPET_M);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_CARPET_M, s.query_x_y(3, 3))
        << "carpet, around==15 -> carpet_by_surround[15] == PIX_CARPET_M";

    // Plain wall, around == TO_UP -> vertical-wall side end.
    set_neighbors(pd, PIX_WALL2,
                  PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALLSIDE_C, s.query_x_y(3, 3))
        << "wall, around==1 -> PIX_WALLSIDE_C";

    // Plain wall, around == 11 (middle base): next_random(10)==0 cracks it.
    set_neighbors(pd, PIX_WALL2,
                  PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_WALL2,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALLSIDE_CRACK_C1, s.query_x_y(3, 3))
        << "wall, around==11 with a 0 draw -> PIX_WALLSIDE_CRACK_C1";

    // The four water corner masks.
    set_neighbors(pd, PIX_WATER1,
                  PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WATERGRASS_LL, s.query_x_y(3, 3))
        << "water, around==TO_UP|TO_RIGHT -> PIX_WATERGRASS_LL";

    set_neighbors(pd, PIX_WATER1,
                  PIX_WATER1, PIX_GRASS1, PIX_GRASS1, PIX_WATER1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WATERGRASS_LR, s.query_x_y(3, 3))
        << "water, around==TO_UP|TO_LEFT -> PIX_WATERGRASS_LR";

    set_neighbors(pd, PIX_WATER1,
                  PIX_GRASS1, PIX_WATER1, PIX_WATER1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WATERGRASS_UL, s.query_x_y(3, 3))
        << "water, around==TO_DOWN|TO_RIGHT -> PIX_WATERGRASS_UL";

    set_neighbors(pd, PIX_WATER1,
                  PIX_GRASS1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WATERGRASS_UR, s.query_x_y(3, 3))
        << "water, around==TO_DOWN|TO_LEFT -> PIX_WATERGRASS_UR";

    // Fully-surrounded trees pick their edge from the DIAGONAL neighbours.
    set_neighbors(pd, PIX_TREE_B1,
                  PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,
                  PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_TREE_MR, s.query_x_y(3, 3))
        << "trees, around==15 with a non-tree down-right -> PIX_TREE_MR";

    set_neighbors(pd, PIX_TREE_B1,
                  PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,
                  PIX_GRASS1, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_TREE_ML, s.query_x_y(3, 3))
        << "trees, around==15 with tree diagonals on the right but not the left -> PIX_TREE_ML";

    // Dirt and dark dirt share the same 16-entry mask table shape.
    set_neighbors(pd, PIX_DIRT_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1, PIX_DIRT_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_DIRTGRASS_LL1, s.query_x_y(3, 3))
        << "dirt, around==TO_DOWN|TO_LEFT -> dirt_by_surround[12] == PIX_DIRTGRASS_LL1";

    set_neighbors(pd, PIX_DIRT_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_DIRTGRASS_DARK_LL1, s.query_x_y(3, 3))
        << "dark dirt, around==TO_DOWN|TO_LEFT -> dirt_dark_by_surround[12]";

    // A tile above PIX_MAX has no genre, so smooth() leaves it exactly as it was.
    set_neighbors(pd, 250,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                  PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(250, s.query_x_y(3, 3)) << "unknown genre -> the tile is written back unchanged";

    ASSERT_EQ(1, s.smooth()) << "full-map smooth reports it ran";

    pop_test_context();
}
} // namespace detail_smooth_r11

// --- From test_smooth_r12.cpp ---
namespace detail_smooth_r12 {
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
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
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

TEST(SmoothUnit, smooth_r12_light_grass_wall_tree_dirt_and_cobble_variant_selection)
{
    FixedRandom rng(0);
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    // Only the centre and its four orthogonal neighbours are ever written, so
    // the four diagonals stay PIX_GRASS1 for the whole test; every expectation
    // below is the tile smooth() must leave at (3,3). FixedRandom(0) makes each
    // next_random() draw 0.

    // Light grass: a straight 16-entry table indexed by the around mask.
    set_neighbors(pd, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_LIGHT_RIGHT, s.query_x_y(3, 3)) << "light grass, around==0";
    set_neighbors(pd, PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_LIGHT_RIGHT_BOTTOM, s.query_x_y(3, 3)) << "light grass, around==TO_UP";
    set_neighbors(pd, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS_LIGHT_1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_GRASS_LIGHT_TOP, s.query_x_y(3, 3)) << "light grass, around==TO_RIGHT|TO_LEFT";

    // Wall around-masks 1, 4, 6, 11, 12 and 13. Cases 4/6/12/13 also consult the
    // tile two rows down (grass here), which is what picks the LL/H variants.
    set_neighbors(pd, PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALLSIDE_C, s.query_x_y(3, 3)) << "wall, around==1";
    set_neighbors(pd, PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_WALL2, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALL_LL, s.query_x_y(3, 3)) << "wall, around==4 with no wall two rows down";
    set_neighbors(pd, PIX_WALL2, PIX_GRASS1, PIX_WALL2, PIX_WALL2, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALL_LL, s.query_x_y(3, 3)) << "wall, around==6 with no wall two rows down";
    set_neighbors(pd, PIX_WALL2, PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_WALL2);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALLSIDE_CRACK_C1, s.query_x_y(3, 3)) << "wall, around==11 with a 0 draw cracks";
    set_neighbors(pd, PIX_WALL2, PIX_GRASS1, PIX_GRASS1, PIX_WALL2, PIX_WALL2);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_H_WALL1, s.query_x_y(3, 3)) << "wall, around==12 with no wall two rows down";
    set_neighbors(pd, PIX_WALL2, PIX_WALL2, PIX_GRASS1, PIX_WALL2, PIX_WALL2);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_WALL_LL, s.query_x_y(3, 3))
        << "wall, around==13 with neither the tile two down nor the down-left a wall";

    // Trees.
    set_neighbors(pd, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_TREE_MR, s.query_x_y(3, 3)) << "trees, around==15 with grass diagonals -> right edge";
    set_neighbors(pd, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_TREE_B1, s.query_x_y(3, 3)) << "trees, around==11 -> bottom middle";
    set_neighbors(pd, PIX_TREE_B1, PIX_GRASS1, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_TREE_T1, s.query_x_y(3, 3)) << "trees, around==TO_RIGHT|TO_DOWN -> top left";

    // Dirt / dark dirt table entries 4 and 9.
    set_neighbors(pd, PIX_DIRT_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_DIRT_1, s.query_x_y(3, 3)) << "dirt_by_surround[4]";
    set_neighbors(pd, PIX_DIRT_1, PIX_DIRT_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_DIRTGRASS_UL1, s.query_x_y(3, 3)) << "dirt_by_surround[9]";
    set_neighbors(pd, PIX_DIRT_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_DARK_1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_DIRT_DARK_1, s.query_x_y(3, 3)) << "dirt_dark_by_surround[4]";
    set_neighbors(pd, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_DIRT_DARK_1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_DIRTGRASS_DARK_UL1, s.query_x_y(3, 3)) << "dirt_dark_by_surround[9]";

    // Cobble ignores the mask entirely and re-rolls a variant.
    set_neighbors(pd, PIX_COBBLE_2, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(PIX_COBBLE_1, s.query_x_y(3, 3)) << "cobble -> cobble_variants[0] under a 0 draw";

    // Unknown genre (>= PIX_MAX) is written straight back.
    set_neighbors(pd, 254, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
    s.smooth(3, 3);
    ASSERT_EQ(254, s.query_x_y(3, 3)) << "unknown genre is left untouched";

    ASSERT_EQ(1, s.smooth()) << "full-map smooth reports it ran";
    pop_test_context();
}

TEST(SmoothUnit, smooth_r12_light_grass_and_carpet_mask_tables_and_random_variants)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    // Independent copies of the two 16-entry autotiler tables in
    // src/gameplay/smooth.cpp. Index == the around mask
    // (TO_UP 1 | TO_RIGHT 2 | TO_DOWN 4 | TO_LEFT 8).
    const int expected_light[16] = {
        PIX_GRASS_LIGHT_RIGHT,        PIX_GRASS_LIGHT_RIGHT_BOTTOM,
        PIX_GRASS_LIGHT_LEFT_TOP,     PIX_GRASS_LIGHT_LEFT_BOTTOM,
        PIX_GRASS_LIGHT_RIGHT_TOP,    PIX_GRASS_LIGHT_RIGHT,
        PIX_GRASS_LIGHT_LEFT_TOP,     PIX_GRASS_LIGHT_LEFT,
        PIX_GRASS_LIGHT_RIGHT_TOP,    PIX_GRASS_LIGHT_RIGHT_BOTTOM,
        PIX_GRASS_LIGHT_TOP,          PIX_GRASS_LIGHT_BOTTOM,
        PIX_GRASS_LIGHT_RIGHT_TOP,    PIX_GRASS_LIGHT_RIGHT,
        PIX_GRASS_LIGHT_TOP,          PIX_GRASS_LIGHT_1,
    };
    const int expected_carpet[16] = {
        PIX_CARPET_SMALL_TINY,  PIX_CARPET_SMALL_CUP,
        PIX_CARPET_SMALL_LEFT,  PIX_CARPET_LL,
        PIX_CARPET_SMALL_CAP,   PIX_CARPET_SMALL_VER,
        PIX_CARPET_UL,          PIX_CARPET_L,
        PIX_CARPET_SMALL_RIGHT, PIX_CARPET_LR,
        PIX_CARPET_SMALL_HOR,   PIX_CARPET_B,
        PIX_CARPET_UR,          PIX_CARPET_R,
        PIX_CARPET_U,           PIX_CARPET_M,
    };

    for (int mask = 0; mask <= 15; ++mask)
    {
        SCOPED_TRACE(mask);
        set_neighbors_mask(pd, PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.smooth(3, 3);
        ASSERT_EQ(expected_light[mask], s.query_x_y(3, 3))
            << "light grass mask -> grass_light_by_surround entry";
    }

    for (int mask = 0; mask <= 15; ++mask)
    {
        SCOPED_TRACE(mask);
        set_neighbors_mask(pd, PIX_CARPET_M, PIX_CARPET_M2, PIX_GRASS1, mask);
        s.smooth(3, 3);
        ASSERT_EQ(expected_carpet[mask], s.query_x_y(3, 3))
            << "carpet mask -> carpet_by_surround entry";
    }

    // Surrounded water re-rolls one of three variants; the draw index is the
    // only thing that varies, so drive it explicitly.
    const int expected_water[3] = {PIX_WATER1, PIX_WATER2, PIX_WATER3};
    set_neighbors(pd, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_WATER1, PIX_WATER1);
    for (int draw = 0; draw < 3; ++draw)
    {
        SCOPED_TRACE(draw);
        rng.n = static_cast<std::uint32_t>(draw);
        s.smooth(3, 3);
        ASSERT_EQ(expected_water[draw], s.query_x_y(3, 3))
            << "water, around==15 -> water_variants[draw]";
    }

    // Surrounded dark grass re-rolls one of four variants.
    const int expected_dark[4] = {PIX_GRASS_DARK_1, PIX_GRASS_DARK_2,
                                  PIX_GRASS_DARK_3, PIX_GRASS_DARK_4};
    for (int draw = 0; draw < 4; ++draw)
    {
        SCOPED_TRACE(draw);
        set_neighbors(pd, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                      PIX_GRASS_DARK_1, PIX_GRASS_DARK_1);
        rng.n = static_cast<std::uint32_t>(draw);
        s.smooth(3, 3);
        ASSERT_EQ(expected_dark[draw], s.query_x_y(3, 3))
            << "dark grass, around==15 -> grass_dark_variants[draw]";
    }

    // Grass with water up, right and on three diagonals: the upper-right
    // grass/water blend corner (no random draw at all).
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
    ASSERT_EQ(PIX_GRASSWATER_UR, s.query_x_y(3, 3))
        << "grass with water up/right/up-left/up-right/down-right -> PIX_GRASSWATER_UR";

    pop_test_context();
}
} // namespace detail_smooth_r12

// --- From test_smooth_r14.cpp ---
namespace detail_smooth_r14 {
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
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

void set_at(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(x + y * pd.w)] = v;
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

TEST(SmoothUnit, smooth_r14_wall_arrow_slit_and_water_edge_variants)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    // An arrow-slit wall takes its variant from the tile ABOVE it: grass and
    // dark grass by genre, pavement and wooden floor by exact tile.
    struct ArrowCase { unsigned char above; int expected; const char* why; };
    const ArrowCase arrow_cases[] = {
        {PIX_GRASS1,       PIX_WALL_ARROW_GRASS,      "grass above -> grass arrow slit"},
        {PIX_GRASS_DARK_1, PIX_WALL_ARROW_GRASS_DARK, "dark grass above -> dark arrow slit"},
        {PIX_PAVEMENT1,    PIX_WALL4,                 "pavement above -> stone arrow slit"},
        {PIX_FLOOR1,       PIX_WALL_ARROW_FLOOR,      "wooden floor above -> floor arrow slit"},
    };
    for (const ArrowCase& c : arrow_cases)
    {
        SCOPED_TRACE(static_cast<int>(c.above));
        set_at(pd, x, y, PIX_WALL_ARROW_GRASS);
        set_at(pd, x, y - 1, c.above);
        s.smooth(x, y);
        ASSERT_EQ(c.expected, s.query_x_y(x, y)) << c.why;
    }

    // A water tile with exactly one water neighbour picks a two-entry
    // grass-edge table; the draw index is the only free variable.
    struct WaterCase { int mask; int expected[2]; const char* why; };
    const WaterCase water_cases[] = {
        {TO_UP,    {PIX_WATERGRASS_LL, PIX_WATERGRASS_LR}, "water above only -> watergrass_up"},
        {TO_DOWN,  {PIX_WATERGRASS_UL, PIX_WATERGRASS_UR}, "water below only -> watergrass_down"},
        {TO_LEFT,  {PIX_WATERGRASS_UR, PIX_WATERGRASS_LR}, "water left only -> watergrass_left"},
        {TO_RIGHT, {PIX_WATERGRASS_UL, PIX_WATERGRASS_LL}, "water right only -> watergrass_right"},
    };
    for (const WaterCase& c : water_cases)
    {
        for (int draw = 0; draw < 2; ++draw)
        {
            SCOPED_TRACE(c.mask * 10 + draw);
            set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, c.mask);
            rng.n = static_cast<std::uint32_t>(draw);
            s.smooth(x, y);
            ASSERT_EQ(c.expected[draw], s.query_x_y(x, y)) << c.why;
        }
    }

    // No water neighbour at all: the water default writes the tile back as-is.
    set_neighbors_mask(pd, x, y, PIX_WATER1, PIX_WATER1, PIX_GRASS1, 0);
    s.smooth(x, y);
    ASSERT_EQ(PIX_WATER1, s.query_x_y(x, y)) << "isolated water keeps its own tile";

    // Above PIX_MAX there is no genre, so the tile survives smooth() untouched.
    set_at(pd, x, y, 254);
    s.smooth(x, y);
    ASSERT_EQ(254, s.query_x_y(x, y)) << "unknown genre is written back unchanged";

    pop_test_context();
}

TEST(SmoothUnit, smooth_r14_tree_dirt_and_dark_dirt_mask_matrix)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;
    PixieData pd = make_grid(PIX_GRASS1);
    s.set_target(pd);

    const int x = 4;
    const int y = 4;

    // Expected tile per around mask (TO_UP 1 | TO_RIGHT 2 | TO_DOWN 4 | TO_LEFT 8),
    // with all four diagonals left as grass. None of these arms draws a random
    // number, so the whole matrix is fixed by the mask alone.
    const int expected_tree[16] = {
        PIX_TREE_B1,  // 0: alone
        PIX_TREE_B1,  // 1: bottom, alone
        PIX_TREE_B1,  // 2: left, alone
        PIX_TREE_B1,  // 3: bottom left
        PIX_TREE_T1,  // 4: top, alone
        PIX_TREE_MT,  // 5: centre vertical
        PIX_TREE_T1,  // 6: top left
        PIX_TREE_ML,  // 7: left middle
        PIX_TREE_B1,  // 8: right, thin
        PIX_TREE_B1,  // 9: bottom right
        PIX_TREE_B1,  // 10: middle, thin
        PIX_TREE_B1,  // 11: bottom middle
        PIX_TREE_T1,  // 12: top right
        PIX_TREE_MR,  // 13: right middle
        PIX_TREE_T1,  // 14: top middle
        PIX_TREE_MR,  // 15: surrounded, non-tree diagonals -> right edge
    };
    const int expected_dirt[16] = {
        PIX_DIRT_1, PIX_DIRT_1, PIX_DIRT_1, PIX_DIRTGRASS_UR1,
        PIX_DIRT_1, PIX_DIRT_1, PIX_DIRTGRASS_LR1, PIX_DIRT_1,
        PIX_DIRT_1, PIX_DIRTGRASS_UL1, PIX_DIRT_1, PIX_DIRT_1,
        PIX_DIRTGRASS_LL1, PIX_DIRT_1, PIX_DIRT_1, PIX_DIRT_1,
    };
    const int expected_dark_dirt[16] = {
        PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRTGRASS_DARK_UR1,
        PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRTGRASS_DARK_LR1, PIX_DIRT_DARK_1,
        PIX_DIRT_DARK_1, PIX_DIRTGRASS_DARK_UL1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1,
        PIX_DIRTGRASS_DARK_LL1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1,
    };

    for (int mask = 0; mask <= 15; ++mask)
    {
        SCOPED_TRACE(mask);
        set_neighbors_mask(pd, x, y, PIX_TREE_B1, PIX_TREE_B1, PIX_GRASS1, mask);
        s.smooth(x, y);
        ASSERT_EQ(expected_tree[mask], s.query_x_y(x, y)) << "tree variant for this around mask";
    }

    for (int mask = 0; mask <= 15; ++mask)
    {
        SCOPED_TRACE(mask);
        set_neighbors_mask(pd, x, y, PIX_DIRT_1, PIX_DIRT_1, PIX_GRASS1, mask);
        s.smooth(x, y);
        ASSERT_EQ(expected_dirt[mask], s.query_x_y(x, y)) << "dirt_by_surround entry";
    }

    for (int mask = 0; mask <= 15; ++mask)
    {
        SCOPED_TRACE(mask);
        set_neighbors_mask(pd, x, y, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, mask);
        s.smooth(x, y);
        ASSERT_EQ(expected_dark_dirt[mask], s.query_x_y(x, y)) << "dirt_dark_by_surround entry";
    }

    pop_test_context();
}

TEST(SmoothUnit, smooth_r14_lines_903_full_smooth_reset_paths)
{
    SeqRandom rng;
    GameContext gc;
    gc.rng = &rng;
    push_test_context(&gc);

    smoother s;

    // !mygrid guards.
    ASSERT_TRUE(s.smooth() == 0);
    PixieData pd = make_grid(PIX_COBBLE_1, 3, 3);
    s.set_target(pd);
    ASSERT_TRUE(s.smooth() == 1);

    s.reset();
    ASSERT_TRUE(s.smooth() == 0);

    pop_test_context();
}
} // namespace detail_smooth_r14
