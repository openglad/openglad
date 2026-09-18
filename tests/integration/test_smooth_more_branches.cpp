#include <openglad/gameplay/pixie_data.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/interface/game_context.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>

namespace
{
class ExposedSmoother : public smoother
{
public:
    using smoother::set_x_y;
};

struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
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
    return g.data[static_cast<std::size_t>(x + y * g.w)];
}

static void apply_cardinal_mask(PixieData& grid, int cx, int cy,
                                int mask, unsigned char same_genre, unsigned char other)
{
    at(grid, cx, cy) = same_genre;
    at(grid, cx, cy - 1) = (mask & TO_UP) ? same_genre : other;
    at(grid, cx + 1, cy) = (mask & TO_RIGHT) ? same_genre : other;
    at(grid, cx, cy + 1) = (mask & TO_DOWN) ? same_genre : other;
    at(grid, cx - 1, cy) = (mask & TO_LEFT) ? same_genre : other;
}
} // namespace

TEST(SmoothMoreBranches, smooth_dark_grass_rubble_and_corner_branches)
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
    ASSERT_EQ((int)PIX_GRASS_RUBBLE, (int)at(grid, 2, 2)) << "dark grass bottom-middle should place rubble when rng(20)==0";

    // around == (TO_LEFT | TO_DOWN) (top right) selects PIX_GRASS_DARK_LL when right is grass.
    at(grid, 2, 2) = PIX_GRASS_DARK_1; // reset center
    at(grid, 1, 2) = PIX_GRASS_DARK_1; // left dark
    at(grid, 2, 3) = PIX_GRASS_DARK_1; // down dark
    at(grid, 3, 2) = PIX_GRASS1;       // right is grass genre
    at(grid, 2, 1) = PIX_GRASS1;       // up not dark
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)at(grid, 2, 2)) << "dark grass top-right should become LL edge when right is grass";
}


// A fully surrounded dark-grass tile picks grass_dark_variants[rng(4)]. The
// draw IS the index under FixedRandom, so each pass must land on its own
// variant: a next_random that always answered 0, or a reordered variant table,
// has to show up here.
TEST(SmoothMoreBranches, smooth_dark_grass_to_around_picks_the_rng_indexed_variant)
{
    static constexpr unsigned char kDarkVariants[4] = {
        PIX_GRASS_DARK_1, PIX_GRASS_DARK_2, PIX_GRASS_DARK_3, PIX_GRASS_DARK_4
    };

    for (int fixed = 0; fixed < 4; fixed++)
    {
        SCOPED_TRACE(fixed);
        FixedRandom rng(static_cast<std::uint32_t>(fixed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        PixieData grid = make_grid(3, 3, PIX_GRASS_DARK_1);
        smoother s;
        s.set_target(grid);

        // Center is dark grass, all neighbors dark grass => around == TO_AROUND.
        at(grid, 1, 1) = PIX_GRASS_DARK_1;
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";

        ASSERT_EQ((int)kDarkVariants[fixed], (int)at(grid, 1, 1))
            << "rng " << fixed << " must select grass_dark_variants[" << fixed << "]";
    }
}


TEST(SmoothMoreBranches, smooth_wall_vertical_and_base_cases)
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
    ASSERT_EQ((int)PIX_WALLSIDE_CRACK_C1, (int)at(grid, 2, 2)) << "wall base middle should choose crack when rng(10)==0";

    // Case: around == 5 => vertical wall, and if y+2 is wall selects PIX_WALL2.
    at(grid, 2, 3) = PIX_H_WALL1; // new center wall at (2,3)
    at(grid, 2, 2) = PIX_H_WALL1; // up wall
    at(grid, 2, 4) = PIX_H_WALL1; // down wall
    at(grid, 2, 5) = PIX_H_WALL1; // y+2 wall
    at(grid, 1, 3) = PIX_GRASS1;  // left not wall
    at(grid, 3, 3) = PIX_GRASS1;  // right not wall
    (void)s.smooth(2, 3);
    ASSERT_EQ((int)PIX_WALL2, (int)at(grid, 2, 3)) << "vertical wall should become PIX_WALL2 when wall continues two tiles down";
}


TEST(SmoothMoreBranches, smooth_dirt_and_dark_dirt_to_around_rng_switch_cases)
{
    // Dirt TYPE_DIRT around==TO_AROUND uses rng(3) switch.
    for (int fixed = 0; fixed < 3; fixed++)
    {
        FixedRandom rng(static_cast<std::uint32_t>(fixed));
        GameContext c;
        c.rng = &rng;
        GlobalContextGuard guard(&c);

        PixieData dirt = make_grid(3, 3, PIX_DIRT_1);
        smoother s;
        s.set_target(dirt);
        (void)s.smooth(1, 1);
        ASSERT_EQ((int)PIX_DIRT_1, (int)at(dirt, 1, 1)) << "dirt TO_AROUND selects PIX_DIRT_1";

        PixieData dd = make_grid(3, 3, PIX_DIRT_DARK_1);
        s.set_target(dd);
        (void)s.smooth(1, 1);
        ASSERT_EQ((int)PIX_DIRT_DARK_1, (int)at(dd, 1, 1)) << "dark dirt TO_AROUND selects PIX_DIRT_DARK_1";
    }
}


TEST(SmoothMoreBranches, smooth_wall_water_tree_unknown_and_setxy_guard_paths)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(6, 6, PIX_GRASS1);
    smoother s;
    s.set_target(grid);

    // TYPE_UNKNOWN default branch.
    at(grid, 1, 1) = 255;
    (void)s.smooth(1, 1);
    ASSERT_EQ(255, (int)at(grid, 1, 1)) << "unknown tile should remain unchanged";

    // Wall around==1 and around==9 branches.
    at(grid, 2, 2) = PIX_H_WALL1;
    at(grid, 2, 1) = PIX_H_WALL1; // up only -> around==1
    at(grid, 3, 2) = PIX_GRASS1;
    at(grid, 2, 3) = PIX_GRASS1;
    at(grid, 1, 2) = PIX_GRASS1;
    (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_WALLSIDE_C, (int)at(grid, 2, 2)) << "wall around==1 should map to WALLSIDE_C";

    // Left neighbour only => around == 8, which no case in the wall switch
    // names: the default arm re-writes herepix, leaving the tile alone.
    at(grid, 3, 3) = PIX_H_WALL1;
    at(grid, 3, 2) = PIX_GRASS1;
    at(grid, 4, 3) = PIX_GRASS1;
    at(grid, 3, 4) = PIX_GRASS1;
    at(grid, 2, 3) = PIX_H_WALL1;
    ASSERT_EQ(1, s.smooth(3, 3)) << "smooth() reports it wrote a tile";
    ASSERT_EQ((int)PIX_H_WALL1, (int)at(grid, 3, 3))
        << "wall around==8 has no case, so the default arm must keep herepix";

    at(grid, 4, 2) = PIX_H_WALL1;
    at(grid, 4, 1) = PIX_H_WALL1;
    at(grid, 5, 2) = PIX_GRASS1;
    at(grid, 4, 3) = PIX_GRASS1;
    at(grid, 3, 2) = PIX_H_WALL1; // around==9
    (void)s.smooth(4, 2);
    ASSERT_EQ((int)PIX_WALLSIDE_R, (int)at(grid, 4, 2)) << "wall around==9 should map to WALLSIDE_R";

    // Water around==TO_LEFT and TO_RIGHT random switch branches.
    at(grid, 2, 4) = PIX_WATER1;
    at(grid, 1, 4) = PIX_WATER1;
    at(grid, 2, 3) = PIX_GRASS1;
    at(grid, 3, 4) = PIX_GRASS1;
    at(grid, 2, 5) = PIX_GRASS1;
    ASSERT_EQ(1, s.smooth(2, 4)) << "smooth() reports it wrote a tile";
    ASSERT_EQ((int)PIX_WATERGRASS_UR, (int)at(grid, 2, 4))
        << "water around==TO_LEFT must take watergrass_left[0] under rng 0";

    at(grid, 3, 4) = PIX_WATER1;
    at(grid, 2, 4) = PIX_GRASS1;
    at(grid, 4, 4) = PIX_WATER1;
    at(grid, 3, 3) = PIX_GRASS1;
    at(grid, 3, 5) = PIX_GRASS1;
    ASSERT_EQ(1, s.smooth(3, 4)) << "smooth() reports it wrote a tile";
    ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)at(grid, 3, 4))
        << "water around==TO_RIGHT must take watergrass_right[0] under rng 0";

    // Trees around==TO_DOWN|TO_UP branch.
    at(grid, 1, 3) = PIX_TREE_M1;
    at(grid, 1, 2) = PIX_TREE_M1;
    at(grid, 1, 4) = PIX_TREE_M1;
    at(grid, 0, 3) = PIX_GRASS1;
    at(grid, 2, 3) = PIX_GRASS1;
    ASSERT_EQ(1, s.smooth(1, 3)) << "smooth() reports it wrote a tile";
    ASSERT_EQ((int)PIX_TREE_MT, (int)at(grid, 1, 3)) << "trees vertical branch should map to MT";

    // set_x_y's write guards. The write path must mirror query_x_y's bounds
    // check: in range it stores, out of range it stores nothing at all (issue
    // #12 -- an unguarded store lands in whatever was allocated after the
    // grid).  Note the mygrid_span_.empty() early return above them is
    // subsumed by the maxx/maxy test (reset() zeroes both), so the observable
    // rule is the bounds guard.
    ExposedSmoother ex;
    PixieData guard_grid = make_grid(3, 3, PIX_GRASS1);
    ex.set_target(guard_grid);

    ex.set_x_y(1, 1, PIX_WATER1);
    ASSERT_EQ((int)PIX_WATER1, (int)at(guard_grid, 1, 1))
        << "an in-range set_x_y must store the tile";

    const unsigned char before[9] = {
        at(guard_grid, 0, 0), at(guard_grid, 1, 0), at(guard_grid, 2, 0),
        at(guard_grid, 0, 1), at(guard_grid, 1, 1), at(guard_grid, 2, 1),
        at(guard_grid, 0, 2), at(guard_grid, 1, 2), at(guard_grid, 2, 2),
    };
    ex.set_x_y(-1, 1, PIX_TREE_M1);
    ex.set_x_y(1, -1, PIX_TREE_M1);
    ex.set_x_y(3, 1, PIX_TREE_M1);
    ex.set_x_y(1, 3, PIX_TREE_M1);
    for (int i = 0; i < 9; i++)
        ASSERT_EQ((int)before[i], (int)guard_grid.data[static_cast<std::size_t>(i)])
            << "out-of-range set_x_y must not touch cell " << i;

    // With no target the same store is a no-op too.
    ex.reset();
    ex.set_x_y(1, 1, PIX_COBBLE_1);
    ASSERT_EQ((int)PIX_WATER1, (int)at(guard_grid, 1, 1))
        << "set_x_y without a target must not write through the stale span";
}


// The two watergrass_* pairs are chosen by rng, so one rng value cannot tell a
// pair apart from a constant. Take the second entry of each under rng 1.
// (push_test_context/pop_test_context do not nest, hence a separate test.)
TEST(SmoothMoreBranches, water_shoreline_pairs_take_their_second_entry_under_rng_1)
{
    FixedRandom rng1(1);
    GameContext c;
    c.rng = &rng1;
    GlobalContextGuard guard(&c);

    smoother s;

    PixieData left_grid = make_grid(6, 6, PIX_GRASS1);
    s.set_target(left_grid);
    at(left_grid, 2, 4) = PIX_WATER1;
    at(left_grid, 1, 4) = PIX_WATER1; // left only -> around == TO_LEFT
    ASSERT_EQ(1, s.smooth(2, 4)) << "smooth() reports it wrote a tile";
    ASSERT_EQ((int)PIX_WATERGRASS_LR, (int)at(left_grid, 2, 4))
        << "water around==TO_LEFT must take watergrass_left[1] under rng 1";

    PixieData right_grid = make_grid(6, 6, PIX_GRASS1);
    s.set_target(right_grid);
    at(right_grid, 3, 4) = PIX_WATER1;
    at(right_grid, 4, 4) = PIX_WATER1; // right only -> around == TO_RIGHT
    ASSERT_EQ(1, s.smooth(3, 4)) << "smooth() reports it wrote a tile";
    ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)at(right_grid, 3, 4))
        << "water around==TO_RIGHT must take watergrass_right[1] under rng 1";
}


TEST(SmoothMoreBranches, smooth_tree_dirt_dark_dirt_mask_ladders)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    {
        const std::array<std::pair<int, unsigned char>, 15> expected = {{
            {TO_LEFT | TO_RIGHT | TO_DOWN, PIX_TREE_T1},
            {TO_UP | TO_DOWN | TO_LEFT, PIX_TREE_MR},
            {TO_LEFT | TO_DOWN, PIX_TREE_T1},
            {TO_LEFT | TO_RIGHT | TO_UP, PIX_TREE_B1},
            {TO_LEFT | TO_RIGHT, PIX_TREE_B1},
            {TO_LEFT | TO_UP, PIX_TREE_B1},
            {TO_LEFT, PIX_TREE_B1},
            {TO_DOWN | TO_RIGHT | TO_UP, PIX_TREE_ML},
            {TO_DOWN | TO_RIGHT, PIX_TREE_T1},
            {TO_DOWN | TO_UP, PIX_TREE_MT},
            {TO_DOWN, PIX_TREE_T1},
            {TO_RIGHT | TO_UP, PIX_TREE_B1},
            {TO_RIGHT, PIX_TREE_B1},
            {TO_UP, PIX_TREE_B1},
            {0, PIX_TREE_B1},
        }};

        for (const auto& entry : expected)
        {
            PixieData grid = make_grid(5, 5, PIX_GRASS1);
            smoother s;
            s.set_target(grid);
            apply_cardinal_mask(grid, 2, 2, entry.first, PIX_TREE_M1, PIX_GRASS1);
            (void)s.smooth(2, 2);
            ASSERT_EQ((int)entry.second, (int)at(grid, 2, 2)) << "tree around-mask mapping should match branch table";
        }
    }

    {
        const std::array<std::pair<int, unsigned char>, 15> expected = {{
            {TO_LEFT | TO_RIGHT | TO_DOWN, PIX_DIRT_1},
            {TO_UP | TO_DOWN | TO_LEFT, PIX_DIRT_1},
            {TO_LEFT | TO_DOWN, PIX_DIRTGRASS_LL1},
            {TO_LEFT | TO_RIGHT | TO_UP, PIX_DIRT_1},
            {TO_LEFT | TO_RIGHT, PIX_DIRT_1},
            {TO_LEFT | TO_UP, PIX_DIRTGRASS_UL1},
            {TO_LEFT, PIX_DIRT_1},
            {TO_DOWN | TO_RIGHT | TO_UP, PIX_DIRT_1},
            {TO_DOWN | TO_RIGHT, PIX_DIRTGRASS_LR1},
            {TO_DOWN | TO_UP, PIX_DIRT_1},
            {TO_DOWN, PIX_DIRT_1},
            {TO_RIGHT | TO_UP, PIX_DIRTGRASS_UR1},
            {TO_RIGHT, PIX_DIRT_1},
            {TO_UP, PIX_DIRT_1},
            {0, PIX_DIRT_1},
        }};

        for (const auto& entry : expected)
        {
            PixieData grid = make_grid(5, 5, PIX_GRASS1);
            smoother s;
            s.set_target(grid);
            apply_cardinal_mask(grid, 2, 2, entry.first, PIX_DIRT_1, PIX_GRASS1);
            (void)s.smooth(2, 2);
            ASSERT_EQ((int)entry.second, (int)at(grid, 2, 2)) << "dirt around-mask mapping should match branch table";
        }
    }

    {
        const std::array<std::pair<int, unsigned char>, 15> expected = {{
            {TO_LEFT | TO_RIGHT | TO_DOWN, PIX_DIRT_DARK_1},
            {TO_UP | TO_DOWN | TO_LEFT, PIX_DIRT_DARK_1},
            {TO_LEFT | TO_DOWN, PIX_DIRTGRASS_DARK_LL1},
            {TO_LEFT | TO_RIGHT | TO_UP, PIX_DIRT_DARK_1},
            {TO_LEFT | TO_RIGHT, PIX_DIRT_DARK_1},
            {TO_LEFT | TO_UP, PIX_DIRTGRASS_DARK_UL1},
            {TO_LEFT, PIX_DIRT_DARK_1},
            {TO_DOWN | TO_RIGHT | TO_UP, PIX_DIRT_DARK_1},
            {TO_DOWN | TO_RIGHT, PIX_DIRTGRASS_DARK_LR1},
            {TO_DOWN | TO_UP, PIX_DIRT_DARK_1},
            {TO_DOWN, PIX_DIRT_DARK_1},
            {TO_RIGHT | TO_UP, PIX_DIRTGRASS_DARK_UR1},
            {TO_RIGHT, PIX_DIRT_DARK_1},
            {TO_UP, PIX_DIRT_DARK_1},
            {0, PIX_DIRT_DARK_1},
        }};

        for (const auto& entry : expected)
        {
            PixieData grid = make_grid(5, 5, PIX_GRASS1);
            smoother s;
            s.set_target(grid);
            apply_cardinal_mask(grid, 2, 2, entry.first, PIX_DIRT_DARK_1, PIX_GRASS1);
            (void)s.smooth(2, 2);
            ASSERT_EQ((int)entry.second, (int)at(grid, 2, 2)) << "dark dirt around-mask mapping should match branch table";
        }
    }
}


TEST(SmoothMoreBranches, dark_grass_and_wall_clusters_select_their_exact_piece)
{
    FixedRandom rng1(1);
    GameContext c;
    c.rng = &rng1;
    GlobalContextGuard guard(&c);

    // Dark grass branch cluster around lines ~338-448.
    {
        PixieData grid = make_grid(5, 5, PIX_GRASS1);
        smoother s;
        s.set_target(grid);

        apply_cardinal_mask(grid, 2, 2, TO_UP | TO_DOWN | TO_LEFT, PIX_GRASS_DARK_1, PIX_GRASS1);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_R2, (int)at(grid, 2, 2)) << "dark grass right-middle mask should choose R2 with rng==1";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, TO_LEFT | TO_DOWN, PIX_GRASS_DARK_1, PIX_GRASS1);
        at(grid, 3, 2) = PIX_H_WALL1;
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_B2, (int)at(grid, 2, 2)) << "dark grass top-right mask with non-grass right should choose B2";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, TO_LEFT | TO_UP, PIX_GRASS_DARK_1, PIX_GRASS1);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_BR, (int)at(grid, 2, 2)) << "dark grass bottom-right mask should map to BR";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, TO_DOWN, PIX_GRASS_DARK_1, PIX_GRASS1);
        at(grid, 2, 1) = PIX_H_WALL1;
        at(grid, 3, 2) = PIX_H_WALL1;
        (void)s.smooth(2, 2);
    ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)at(grid, 2, 2)) << "up+right wall adjacency should take UR override before TO_DOWN branch";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, TO_RIGHT | TO_UP, PIX_GRASS_DARK_1, PIX_GRASS1);
        at(grid, 1, 2) = PIX_H_WALL1;
        at(grid, 2, 3) = PIX_H_WALL1;
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_R2, (int)at(grid, 2, 2)) << "dark grass bottom-left mask without grass support should follow right-middle rng branch";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, TO_RIGHT, PIX_GRASS_DARK_1, PIX_GRASS1);
        at(grid, 1, 2) = PIX_H_WALL1;
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)at(grid, 2, 2)) << "dark grass left-alone mask without grass support should choose B1";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, TO_UP, PIX_GRASS_DARK_1, PIX_GRASS1);
        at(grid, 2, 3) = PIX_H_WALL1;
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)at(grid, 2, 2)) << "dark grass bottom-alone mask without grass support should choose B1";

        grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, 2, 2, 0, PIX_GRASS_DARK_1, PIX_GRASS1);
        (void)s.smooth(2, 2);
        ASSERT_EQ((int)PIX_GRASS_DARK_1, (int)at(grid, 2, 2)) << "dark grass with no same-genre neighbors should hit default case";
    }

    // Wall branch cluster around lines ~557-648.
    {
        PixieData wall = make_grid(7, 7, PIX_GRASS1);
        smoother s;
        s.set_target(wall);

        at(wall, 3, 3) = PIX_WALL_ARROW_GRASS;
        at(wall, 3, 2) = PIX_PAVEMENT1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALL4, (int)at(wall, 3, 3)) << "arrow slit over pavement should map to stone wall";

        at(wall, 3, 3) = PIX_WALL_ARROW_GRASS;
        at(wall, 3, 2) = PIX_FLOOR1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALL_ARROW_FLOOR, (int)at(wall, 3, 3)) << "arrow slit over floor should map to floor arrow wall";

        // surrounds() weights up=1, right=2, down=4, left=8, so "up + right
        // walls" is around == 3 -- the lower-left base case, whatever sits two
        // rows down. Pinning the genre let every PIX_WALL* through; pin the piece.
        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 2) = PIX_H_WALL1;
        at(wall, 4, 3) = PIX_H_WALL1;
        at(wall, 3, 5) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALLSIDE_L, (int)at(wall, 3, 3))
            << "wall around==3 is the lower-left base piece even with a wall at y+2";

        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 2) = PIX_H_WALL1;
        at(wall, 4, 3) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALLSIDE_L, (int)at(wall, 3, 3))
            << "wall around==3 is the lower-left base piece with nothing at y+2";

        // The real around==12 arm (down + left walls): WALL3 when the run
        // continues two rows down, the plain horizontal face otherwise.
        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 4) = PIX_H_WALL1;
        at(wall, 2, 3) = PIX_H_WALL1;
        at(wall, 3, 5) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALL3, (int)at(wall, 3, 3))
            << "wall around==12 with a wall at y+2 must be WALL3";

        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 4) = PIX_H_WALL1;
        at(wall, 2, 3) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_H_WALL1, (int)at(wall, 3, 3))
            << "wall around==12 without a wall at y+2 must be the horizontal face";

        // around==15: the sub-selection is (wall at y+2?) x (wall at x-1,y+1?).
        // All four corners of that pair must land on a different piece.
        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 2) = PIX_H_WALL1;
        at(wall, 4, 3) = PIX_H_WALL1;
        at(wall, 3, 4) = PIX_H_WALL1;
        at(wall, 2, 3) = PIX_H_WALL1;
        at(wall, 3, 5) = PIX_H_WALL1;
        at(wall, 2, 4) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALL3, (int)at(wall, 3, 3))
            << "around==15 with y+2 and x-1,y+1 walls must be WALL3";

        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 2) = PIX_H_WALL1;
        at(wall, 4, 3) = PIX_H_WALL1;
        at(wall, 3, 4) = PIX_H_WALL1;
        at(wall, 2, 3) = PIX_H_WALL1;
        at(wall, 3, 5) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALL2, (int)at(wall, 3, 3))
            << "around==15 with only the y+2 wall must be WALL2";

        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 2) = PIX_H_WALL1;
        at(wall, 4, 3) = PIX_H_WALL1;
        at(wall, 3, 4) = PIX_H_WALL1;
        at(wall, 2, 3) = PIX_H_WALL1;
        at(wall, 2, 4) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_H_WALL1, (int)at(wall, 3, 3))
            << "around==15 with only the x-1,y+1 wall must be the horizontal face";

        wall = make_grid(7, 7, PIX_GRASS1);
        s.set_target(wall);
        at(wall, 3, 3) = PIX_H_WALL1;
        at(wall, 3, 2) = PIX_H_WALL1;
        at(wall, 4, 3) = PIX_H_WALL1;
        at(wall, 3, 4) = PIX_H_WALL1;
        at(wall, 2, 3) = PIX_H_WALL1;
        (void)s.smooth(3, 3);
        ASSERT_EQ((int)PIX_WALL_LL, (int)at(wall, 3, 3))
            << "around==15 with neither continuation must be WALL_LL";
    }
}


TEST(SmoothMoreBranches, smooth_round9_dark_grass_single_neighbor_branch_pairs)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    smoother s;
    const int cx = 2;
    const int cy = 2;

    auto run = [&](int mask, unsigned char right, unsigned char left, unsigned char up, unsigned char down) {
        PixieData grid = make_grid(5, 5, PIX_GRASS1);
        s.set_target(grid);
        apply_cardinal_mask(grid, cx, cy, mask, PIX_GRASS_DARK_1, PIX_GRASS1);
        at(grid, cx + 1, cy) = right;
        at(grid, cx - 1, cy) = left;
        at(grid, cx, cy - 1) = up;
        at(grid, cx, cy + 1) = down;
        (void)s.smooth(cx, cy);
        return at(grid, cx, cy);
    };

    ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)run(TO_DOWN, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1)) << "TO_DOWN should choose LL when right or up is grass";
    ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)run(TO_DOWN, PIX_H_WALL1, PIX_GRASS1, PIX_H_WALL1, PIX_GRASS_DARK_1)) << "TO_DOWN with up+right wall should hit UR override branch";

    ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)run(TO_RIGHT | TO_UP, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1)) << "TO_RIGHT|TO_UP should choose UR when left or down is grass";
    ASSERT_EQ((int)PIX_GRASS_DARK_R1, (int)run(TO_RIGHT | TO_UP, PIX_GRASS_DARK_1, PIX_H_WALL1, PIX_GRASS_DARK_1, PIX_H_WALL1)) << "TO_RIGHT|TO_UP with left/down walls should follow right-middle rng branch";

    ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)run(TO_RIGHT, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1)) << "TO_RIGHT should choose UR when left is grass";
    ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)run(TO_RIGHT, PIX_GRASS_DARK_1, PIX_H_WALL1, PIX_GRASS1, PIX_GRASS1)) << "TO_RIGHT should choose B1 when left is not grass";

    ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)run(TO_UP, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1)) << "TO_UP should choose UR when down is grass";
    ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)run(TO_UP, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_H_WALL1)) << "TO_UP should choose B1 when down is not grass";

    ASSERT_EQ((int)PIX_GRASS_DARK_1, (int)run(0, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1)) << "no neighbors should use dark-grass default";
}

