#include <openglad/gameplay/smooth.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/platform/game_context.h>
#include "test_framework.h"

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


TEST(Smoother, smooth_covers_multiple_genres_and_around_masks)
{
    // smooth() depends on ctx().rng; provide deterministic sequencing.
    SequenceRandom seq_rng({0, 1, 2, 3, 0, 1, 2, 3, 5, 0, 19, 0});
    GameContext c;
    c.rng = &seq_rng;
    GlobalContextGuard guard(&c);

    // Carpet and light grass: cover all 'around' mask cases 0..15.
    for (int mask = 0; mask < 16; mask++)
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();

        // Carpet
        set_neighbors(g, 3, PIX_CARPET_M, PIX_CARPET_M, PIX_GRASS1, mask);
        smoother s;
        s.set_target(grid);
        (void)s.smooth(1, 1);

        // Light grass
        set_neighbors(g, 3, PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, mask);
        s.set_target(grid);
        (void)s.smooth(1, 1);
    }

    // Cobble: exercise rng(4) switch.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        g[1 + 1 * 3] = PIX_COBBLE_1;
        smoother s;
        s.set_target(grid);
        for (int i = 0; i < 4; i++)
            (void)s.smooth(1, 1);
    }

    // Grass: diagonal water edge variants.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        g[1 + 1 * 3] = PIX_GRASS1; // center grass
        // Water cluster lower-left for PIX_GRASSWATER_LL.
        g[0 + 0 * 3] = PIX_WATER1; // upleft
        g[0 + 1 * 3] = PIX_WATER1; // left
        g[0 + 2 * 3] = PIX_WATER1; // downleft
        g[1 + 2 * 3] = PIX_WATER1; // down
        g[2 + 2 * 3] = PIX_WATER1; // downright
        smoother s;
        s.set_target(grid);
        (void)s.smooth(1, 1);
    }

    // Trees and dirt/dark dirt: exercise additional genre-specific branches.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;

        // Trees: top-middle and surrounded-ish cases.
        set_neighbors(g, 3, PIX_TREE_M1, PIX_TREE_M1, PIX_GRASS1, TO_LEFT | TO_RIGHT | TO_DOWN);
        s.set_target(grid);
        (void)s.smooth(1, 1);

        set_neighbors(g, 3, PIX_TREE_M1, PIX_TREE_M1, PIX_GRASS1, TO_AROUND);
        s.set_target(grid);
        (void)s.smooth(1, 1);

        // Dirt: corner variants.
        set_neighbors(g, 3, PIX_DIRT_1, PIX_DIRT_1, PIX_GRASS1, TO_LEFT | TO_DOWN);
        s.set_target(grid);
        (void)s.smooth(1, 1);

        // Dark dirt: corner variants.
        set_neighbors(g, 3, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_GRASS1, TO_RIGHT | TO_UP);
        s.set_target(grid);
        (void)s.smooth(1, 1);
    }

    // Water: exercise edge tile selection.
    {
        PixieData grid = make_grid(3, 3, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;
        // Center water, only up is water => around == TO_UP
        set_neighbors(g, 3, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_UP);
        s.set_target(grid);
        (void)s.smooth(1, 1);

        // Only right is water => around == TO_RIGHT
        set_neighbors(g, 3, PIX_WATER1, PIX_WATER1, PIX_GRASS1, TO_RIGHT);
        s.set_target(grid);
        (void)s.smooth(1, 1);
    }

    // Wall: arrow-slit selection based on what is above (grass/dark/stone/wood).
    {
        // 3x4 so y-1 and y+2 are in-bounds for some wall cases.
        PixieData grid = make_grid(3, 4, PIX_GRASS1);
        unsigned char* g = grid.data.get();
        smoother s;
        s.set_target(grid);

        g[1 + 2 * 3] = PIX_WALL_ARROW_GRASS;

        // Above is grass
        g[1 + 1 * 3] = PIX_GRASS1;
        (void)s.smooth(1, 2);

        // Above is dark grass
        g[1 + 1 * 3] = PIX_GRASS_DARK_1;
        (void)s.smooth(1, 2);

        // Above is stone pavement
        g[1 + 1 * 3] = PIX_PAVEMENT1;
        (void)s.smooth(1, 2);

        // Above is wood floor
        g[1 + 1 * 3] = PIX_FLOOR1;
        (void)s.smooth(1, 2);
    }
}

