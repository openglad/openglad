#include <openglad/gameplay/smooth.h>
#include <openglad/legacy/base.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/game_context.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

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

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

class SequenceRandom : public IRandom {
public:
    explicit SequenceRandom(std::initializer_list<Uint32> vals) : vals_(vals), idx_(0) {}
    Uint32 next(Uint32 max_exclusive) override {
        if (vals_.empty())
            return 0;
        Uint32 v = vals_[idx_++ % vals_.size()];
        return max_exclusive ? (v % max_exclusive) : 0;
    }
private:
    std::vector<Uint32> vals_;
    std::size_t idx_;
};

static void set_neighbors(unsigned char* g, int w, unsigned char center, unsigned char same, unsigned char other, int mask)
{
    // Layout:
    // (0,0) (1,0) (2,0)
    // (0,1) (1,1) (2,1)
    // (0,2) (1,2) (2,2)
    g[1 + 1 * w] = center;
    g[1 + 0 * w] = (mask & 1) ? same : other; // up
    g[2 + 1 * w] = (mask & 2) ? same : other; // right
    g[1 + 2 * w] = (mask & 4) ? same : other; // down
    g[0 + 1 * w] = (mask & 8) ? same : other; // left
}
} // namespace

TEST(Smoother, query_genre_maps_known_tiles)
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

    ASSERT_EQ(TYPE_GRASS, s.query_genre_x_y(0, 0)) << "grass should map to TYPE_GRASS";
    ASSERT_EQ(TYPE_WATER, s.query_genre_x_y(1, 0)) << "water should map to TYPE_WATER";
    ASSERT_EQ(TYPE_DIRT, s.query_genre_x_y(2, 0)) << "dirt should map to TYPE_DIRT";
    ASSERT_EQ(TYPE_DIRT_DARK, s.query_genre_x_y(3, 0)) << "dark dirt should map to TYPE_DIRT_DARK";
    ASSERT_EQ(TYPE_CARPET, s.query_genre_x_y(4, 0)) << "carpet should map to TYPE_CARPET";
    ASSERT_EQ(TYPE_WALL, s.query_genre_x_y(5, 0)) << "wall should map to TYPE_WALL";
    ASSERT_EQ(TYPE_TREES, s.query_genre_x_y(6, 0)) << "trees should map to TYPE_TREES";
    ASSERT_EQ(TYPE_COBBLE, s.query_genre_x_y(7, 0)) << "cobble should map to TYPE_COBBLE";
    ASSERT_EQ(TYPE_GRASS_DARK, s.query_genre_x_y(8, 0)) << "dark grass should map to TYPE_GRASS_DARK";
    ASSERT_EQ(TYPE_GRASS_LIGHT, s.query_genre_x_y(9, 0)) << "light grass should map to TYPE_GRASS_LIGHT";
}

TEST(Smoother, every_passability_water_tile_maps_to_water_genre)
{
    constexpr std::array<unsigned char, 11> water_tiles = {
        PIX_WATER1,        PIX_WATER2,        PIX_WATER3,
        PIX_WATERGRASS_LL, PIX_WATERGRASS_LR, PIX_WATERGRASS_UL,
        PIX_WATERGRASS_UR, PIX_WATERGRASS_U,  PIX_WATERGRASS_L,
        PIX_WATERGRASS_R,  PIX_WATERGRASS_D,
    };
    PixieData grid = make_grid(static_cast<unsigned char>(water_tiles.size()),
                               1, PIX_GRASS1);
    std::copy(water_tiles.begin(), water_tiles.end(), grid.data.get());

    smoother s;
    s.set_target(grid);

    for (std::size_t x = 0; x < water_tiles.size(); ++x)
    {
        EXPECT_EQ(TYPE_WATER,
                  s.query_genre_x_y(static_cast<Sint32>(x), 0))
            << "water tile " << static_cast<int>(water_tiles[x]);
    }
}


TEST(Smoother, surrounds_bitmask_counts_neighbors)
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
    ASSERT_EQ(1 + 8, mask) << "surrounds should return bitmask of matching neighbors";
}

TEST(Smoother, smooth_uses_bound_rng_without_global_gameplay_context)
{
    PixieData grid = make_grid(3, 3, PIX_GRASS1);
    unsigned char* g = grid.data.get();
    g[1 + 1 * 3] = PIX_COBBLE_1;

    FixedRandom rng(2);
    smoother s;
    s.set_rng(&rng);
    s.set_target(grid);

    (void)s.smooth(1, 1);
    ASSERT_EQ(PIX_COBBLE_3, g[1 + 1 * 3]);
}


// This used to be 120 lines of `(void)s.smooth(1,1)` across every genre with
// not one assertion. Each call now pins the tile it writes: the carpet and
// light-grass tables, the four cobble variants, the grass/water corner, the
// tree and dirt corner arms, the two watergrass pairs, and -- unique to this
// test -- all four arrow-slit outcomes, which key off what sits ABOVE the slit.
TEST(Smoother, smooth_writes_the_exact_tile_for_every_genre_and_around_mask)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    GlobalContextGuard guard(&c);

    static constexpr unsigned char kCarpet[16] = {
        PIX_CARPET_SMALL_TINY, PIX_CARPET_SMALL_CUP, PIX_CARPET_SMALL_LEFT, PIX_CARPET_LL,
        PIX_CARPET_SMALL_CAP,  PIX_CARPET_SMALL_VER, PIX_CARPET_UL,         PIX_CARPET_L,
        PIX_CARPET_SMALL_RIGHT,PIX_CARPET_LR,        PIX_CARPET_SMALL_HOR,  PIX_CARPET_B,
        PIX_CARPET_UR,         PIX_CARPET_R,         PIX_CARPET_U,          PIX_CARPET_M,
    };
    static constexpr unsigned char kLight[16] = {
        PIX_GRASS_LIGHT_RIGHT,     PIX_GRASS_LIGHT_RIGHT_BOTTOM, PIX_GRASS_LIGHT_LEFT_TOP,  PIX_GRASS_LIGHT_LEFT_BOTTOM,
        PIX_GRASS_LIGHT_RIGHT_TOP, PIX_GRASS_LIGHT_RIGHT,        PIX_GRASS_LIGHT_LEFT_TOP,  PIX_GRASS_LIGHT_LEFT,
        PIX_GRASS_LIGHT_RIGHT_TOP, PIX_GRASS_LIGHT_RIGHT_BOTTOM, PIX_GRASS_LIGHT_TOP,       PIX_GRASS_LIGHT_BOTTOM,
        PIX_GRASS_LIGHT_RIGHT_TOP, PIX_GRASS_LIGHT_RIGHT,        PIX_GRASS_LIGHT_TOP,       PIX_GRASS_LIGHT_1,
    };

    // Carpet and light grass: every 'around' mask 0..15.
    for (int mask = 0; mask < 16; mask++)
    {
        SCOPED_TRACE(mask);
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;

        set_neighbors(g, 3, PIX_CARPET_M, PIX_CARPET_M, PIX_GRASS1, mask);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kCarpet[mask], (int)g[1 + 1 * 3])
            << "carpet mask " << mask << " must pick carpet_by_surround[mask]";

        set_neighbors(g, 3, PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kLight[mask], (int)g[1 + 1 * 3])
            << "light-grass mask " << mask << " must pick grass_light_by_surround[mask]";
    }

    // Grass with water wrapping its lower-left: the shoreline corner tile.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        g[1 + 1 * 3] = PIX_GRASS1; // center grass
        g[0 + 0 * 3] = PIX_WATER1; // upleft
        g[0 + 1 * 3] = PIX_WATER1; // left
        g[0 + 2 * 3] = PIX_WATER1; // downleft
        g[1 + 2 * 3] = PIX_WATER1; // down
        g[2 + 2 * 3] = PIX_WATER1; // downright
        smoother s;
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_GRASSWATER_LL, (int)g[1 + 1 * 3])
            << "water on upleft/left/downleft/down/downright is the LL shoreline";
    }

    // Trees and dirt/dark dirt corner arms.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;

        set_neighbors(g, 3, PIX_TREE_M1, PIX_TREE_M1, PIX_GRASS1, TO_LEFT | TO_RIGHT | TO_DOWN);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_TREE_T1, (int)g[1 + 1 * 3])
            << "trees with left+right+down are the top-middle canopy";

        // Surrounded, but the diagonals are grass, so this is the right edge.
        set_neighbors(g, 3, PIX_TREE_M1, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_TREE_MR, (int)g[1 + 1 * 3])
            << "trees surrounded cardinally but open on the right diagonals are MR";

        set_neighbors(g, 3, PIX_DIRT_1, PIX_DIRT_1, PIX_GRASS1, TO_LEFT | TO_DOWN);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_DIRTGRASS_LL1, (int)g[1 + 1 * 3])
            << "dirt with left+down is the lower-left dirt/grass corner";

        set_neighbors(g, 3, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, TO_RIGHT | TO_UP);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_DIRTGRASS_DARK_UR1, (int)g[1 + 1 * 3])
            << "dark dirt with right+up is the upper-right dark-dirt/grass corner";
    }

    // Water shoreline pairs; rng 0 takes the first entry of each pair.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;

        set_neighbors(g, 3, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_UP);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)g[1 + 1 * 3])
            << "water with only an upstream neighbour is watergrass_up[0]";

        set_neighbors(g, 3, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
        s.set_target(grid);
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)g[1 + 1 * 3])
            << "water with only a right neighbour is watergrass_right[0]";
    }

    // Arrow slits re-skin themselves to match whatever is directly above.
    {
        // 3x4 so y-1 and y+2 are in-bounds.
        PixieData grid = make_grid(3, 4, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;
        s.set_target(grid);

        struct SlitCase { unsigned char above; unsigned char expect; const char* why; };
        const SlitCase cases[] = {
            {PIX_GRASS1,        PIX_WALL_ARROW_GRASS,      "grass above keeps the grass arrow wall"},
            {PIX_GRASS_DARK_1,  PIX_WALL_ARROW_GRASS_DARK, "dark grass above switches to the dark arrow wall"},
            {PIX_PAVEMENT1,     PIX_WALL4,                 "stone pavement above switches to the stone wall"},
            {PIX_FLOOR1,        PIX_WALL_ARROW_FLOOR,      "wood floor above switches to the floor arrow wall"},
        };
        for (const auto& slit : cases)
        {
            SCOPED_TRACE(slit.why);
            g[1 + 2 * 3] = PIX_WALL_ARROW_GRASS;
            g[1 + 1 * 3] = slit.above;
            ASSERT_EQ(1, s.smooth(1, 2)) << "smooth() reports it wrote a tile";
            ASSERT_EQ((int)slit.expect, (int)g[1 + 2 * 3]) << slit.why;
        }
    }
}


// push_test_context/pop_test_context are NOT nestable (pop clears the override
// outright), so the cobble sweep gets its own context and its own test.
TEST(Smoother, smooth_cobble_walks_every_variant_the_rng_indexes)
{
    static constexpr unsigned char kCobble[4] = {
        PIX_COBBLE_1, PIX_COBBLE_2, PIX_COBBLE_3, PIX_COBBLE_4
    };
    SequenceRandom seq({0, 1, 2, 3});
    GameContext c;
    c.rng = &seq;
    GlobalContextGuard guard(&c);

    PixieData grid = make_grid(3, 3, PIX_GRASS1);
    unsigned char* g = grid.data.get();
    smoother s;
    s.set_target(grid);
    for (int i = 0; i < 4; i++)
    {
        SCOPED_TRACE(i);
        g[1 + 1 * 3] = PIX_COBBLE_1;
        ASSERT_EQ(1, s.smooth(1, 1)) << "smooth() reports it wrote a tile";
        ASSERT_EQ((int)kCobble[i], (int)g[1 + 1 * 3])
            << "cobble rng " << i << " must pick cobble_variants[" << i << "]";
    }
}
