#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

static void run_smooth_branch_outputs_with_fixed_rng();
static PixieData make_center_pattern(unsigned char fill, unsigned char center,
                                     unsigned char up, unsigned char right,
                                     unsigned char down, unsigned char left,
                                     unsigned char upleft, unsigned char upright,
                                     unsigned char downleft, unsigned char downright);

static PixieData make_uniform_grid(int w, int h, unsigned char fill);
static void set_tile(PixieData& pd, int x, int y, unsigned char v);

// smoother::next_random() prefers gameplay_rng_override() over the smoother's
// own borrowed rng_ (smooth.cpp), so pinning an EXACT autotile byte means
// binding both to the same fixed generator for the life of the block.
namespace {
class ScopedFixedRng
{
public:
    explicit ScopedFixedRng(Uint32 value) : rng_(value)
    {
        ctx_.rng = &rng_;
        push_test_context(&ctx_);
    }
    ~ScopedFixedRng() { pop_test_context(); }
    ScopedFixedRng(const ScopedFixedRng&) = delete;
    ScopedFixedRng& operator=(const ScopedFixedRng&) = delete;

    void bind(smoother& s) { s.set_rng(&rng_); }

private:
    FixedRandom rng_;
    GameContext ctx_;
};
} // namespace

// ---------------------------------------------------------------------------
// smoother query_x_y
// ---------------------------------------------------------------------------

TEST(SmoothOps, smooth_query_x_y_no_grid)
{
    smoother s;
    // No grid set - should return PIX_GRASS1
    Sint32 result = s.query_x_y(0, 0);
    ASSERT_EQ((int)PIX_GRASS1, (int)result) << "no grid returns PIX_GRASS1";
}


TEST(SmoothOps, smooth_query_x_y_negative)
{
    smoother s;
    Sint32 result = s.query_x_y(-1, -1);
    ASSERT_EQ((int)PIX_GRASS1, (int)result) << "negative returns PIX_GRASS1";
}


TEST(SmoothOps, smooth_query_x_y_with_grid)
{
    // Use the level's grid
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    smoother s;
    s.set_target(world.grid);

    // Exact bytes, one of them on a later row, so a constant return or a
    // mis-strided index is caught (">= 0" accepted any of those).
    world.grid.data[0] = PIX_WATER1;
    world.grid.data[1] = PIX_DIRT_1;
    world.grid.data[static_cast<std::size_t>(world.grid.w)] = PIX_COBBLE_1;

    ASSERT_EQ((int)PIX_WATER1, (int)s.query_x_y(0, 0)) << "query_x_y(0,0) returns data[0]";
    ASSERT_EQ((int)PIX_DIRT_1, (int)s.query_x_y(1, 0)) << "query_x_y(1,0) returns data[1]";
    ASSERT_EQ((int)PIX_COBBLE_1, (int)s.query_x_y(0, 1))
        << "query_x_y reads data[x + y*maxx]";

    // Out of bounds
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(9999, 9999)) << "out of bounds returns PIX_GRASS1";
}


// ---------------------------------------------------------------------------
// smoother query_genre_x_y
// ---------------------------------------------------------------------------

TEST(SmoothOps, smooth_query_genre_grass)
{
    // Create a grid with known grass values
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_GRASS1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_GRASS, (int)genre) << "grass tile returns TYPE_GRASS";
}


TEST(SmoothOps, smooth_query_genre_water)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_WATER1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_WATER, (int)genre) << "water tile returns TYPE_WATER";
}


TEST(SmoothOps, smooth_query_genre_wall)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_H_WALL1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_WALL, (int)genre) << "wall tile returns TYPE_WALL";
}


TEST(SmoothOps, smooth_query_genre_trees)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_TREE_B1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_TREES, (int)genre) << "tree tile returns TYPE_TREES";
}


TEST(SmoothOps, smooth_query_genre_dirt)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_DIRT_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_DIRT, (int)genre) << "dirt tile returns TYPE_DIRT";
}


TEST(SmoothOps, smooth_query_genre_carpet)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_CARPET_M;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_CARPET, (int)genre) << "carpet tile returns TYPE_CARPET";
}


TEST(SmoothOps, smooth_query_genre_cobble)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_COBBLE_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_COBBLE, (int)genre) << "cobble tile returns TYPE_COBBLE";
}


TEST(SmoothOps, smooth_query_genre_dark_grass)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_GRASS_DARK_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_GRASS_DARK, (int)genre) << "dark grass returns TYPE_GRASS_DARK";
}


TEST(SmoothOps, smooth_query_genre_light_grass)
{
    PixieData pd;
    pd.w = 4;
    pd.h = 4;
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(16);
    for (int i = 0; i < 16; i++)
        pd.data[static_cast<std::size_t>(i)] = PIX_GRASS_LIGHT_1;

    smoother s;
    s.set_target(pd);

    Sint32 genre = s.query_genre_x_y(0, 0);
    ASSERT_EQ(TYPE_GRASS_LIGHT, (int)genre) << "light grass returns TYPE_GRASS_LIGHT";
}


// ---------------------------------------------------------------------------
// smoother smooth() - full grid smooth
// ---------------------------------------------------------------------------

TEST(SmoothOps, smooth_smooth_full_grid)
{
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();

    // Seed a grass variant the autotiler will NOT write back, so a smooth()
    // that returns 1 without touching a tile is visible.
    const int cells = world.grid.w * world.grid.h;
    for (int i = 0; i < cells; ++i)
        world.grid.data[static_cast<std::size_t>(i)] = PIX_GRASS3;

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(world.grid);

    ASSERT_EQ(1, (int)s.smooth()) << "smooth() with a target autotiles the grid and returns 1";
    for (int y = 0; y < world.grid.h; ++y)
        for (int x = 0; x < world.grid.w; ++x)
            ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(x, y))
                << "every cell of an all-grass grid becomes grass_variants[0] at (" << x << "," << y << ")";

    smoother untargeted;
    ASSERT_EQ(0, (int)untargeted.smooth()) << "smooth() with no target returns 0";
}


// ---------------------------------------------------------------------------
// smoother smooth(x,y) - single cell smooth for various terrain types
// ---------------------------------------------------------------------------

TEST(SmoothOps, smooth_smooth_single_grass)
{
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS3);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(2, 2))
        << "grass with no water corner pattern takes grass_variants[rng(4)] == GRASS1";
    ASSERT_EQ((int)PIX_GRASS3, (int)s.query_x_y(1, 2))
        << "smooth(x,y) touches only the named cell";
}


TEST(SmoothOps, smooth_smooth_water_surrounded)
{
    PixieData pd = make_uniform_grid(5, 5, PIX_WATER3);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_WATER1, (int)s.query_x_y(2, 2))
        << "water with around==TO_AROUND is body water: water_variants[rng(3)] == WATER1, never a shoreline tile";
    ASSERT_EQ((int)PIX_WATER3, (int)s.query_x_y(1, 1))
        << "smooth(x,y) touches only the named cell";
}


TEST(SmoothOps, smooth_smooth_wall_surrounded)
{
    PixieData pd = make_uniform_grid(5, 5, PIX_H_WALL1);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_WALL3, (int)s.query_x_y(2, 2))
        << "wall around==15 with wall at (x,y+2) AND wall at (x-1,y+1) selects WALL3";
    ASSERT_EQ((int)PIX_H_WALL1, (int)s.query_x_y(1, 1))
        << "smooth(x,y) touches only the named cell";
}


TEST(SmoothOps, smooth_smooth_grass_water_border)
{
    // Top three rows grass, bottom two water -- seeded with variants neither
    // arm writes back, so a no-op is visible on both sides of the seam.
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS3);
    for (int j = 3; j < 5; ++j)
        for (int i = 0; i < 5; ++i)
            set_tile(pd, i, j, PIX_WATER2);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(2, 2))
        << "grass one row off the shore matches no corner-water pattern: grass_variants[0]";

    ASSERT_EQ(1, (int)s.smooth(2, 3)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_WATER1, (int)s.query_x_y(2, 3))
        << "water with around==(TO_DOWN|TO_LEFT|TO_RIGHT) is in the body list: water_variants[0]";

    ASSERT_EQ((int)PIX_GRASS3, (int)s.query_x_y(0, 0)) << "unsmoothed grass cell is untouched";
    ASSERT_EQ((int)PIX_WATER2, (int)s.query_x_y(0, 4)) << "unsmoothed water cell is untouched";
}


TEST(SmoothOps, smooth_smooth_tree_border)
{
    // Grass with a lone tree in the middle. The planted tile is TREE_M1, NOT
    // the TREE_B1 the default arm writes, so a no-op arm goes red.
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS1);
    set_tile(pd, 2, 2, PIX_TREE_M1);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_TREE_B1, (int)s.query_x_y(2, 2))
        << "a lone tree has around==0 and falls to the tree default arm: TREE_B1";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(1, 2))
        << "smooth(x,y) touches only the named cell";
}


TEST(SmoothOps, smooth_smooth_dirt_border)
{
    // Left three columns dirt, right two grass; both halves seeded with a byte
    // the matching arm does NOT write back.
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS2);
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 3; ++i)
            set_tile(pd, i, j, PIX_DIRTGRASS_UL1);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_DIRT_1, (int)s.query_x_y(2, 2))
        << "dirt with around==(TO_UP|TO_DOWN|TO_LEFT)==13 takes dirt_by_surround[13] == DIRT_1";

    ASSERT_EQ(1, (int)s.smooth(2, 0)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_DIRTGRASS_LL1, (int)s.query_x_y(2, 0))
        << "a top-edge dirt cell reads its out-of-range up neighbour as grass: around==12 => DIRTGRASS_LL1";

    ASSERT_EQ(1, (int)s.smooth(3, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(3, 2))
        << "the grass side of the seam takes grass_variants[0]";
}


TEST(SmoothOps, smooth_smooth_carpet_border)
{
    // Carpet interior seeded CARPET_M2 (a carpet byte the table never writes)
    // inside a grass border, so the surrounded cell's CARPET_M is a real change.
    PixieData pd = make_uniform_grid(5, 5, PIX_CARPET_M2);
    for (int i = 0; i < 5; ++i) {
        set_tile(pd, i, 0, PIX_GRASS1);
        set_tile(pd, i, 4, PIX_GRASS1);
        set_tile(pd, 0, i, PIX_GRASS1);
        set_tile(pd, 4, i, PIX_GRASS1);
    }

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_CARPET_M, (int)s.query_x_y(2, 2))
        << "carpet around==15 takes carpet_by_surround[15] == CARPET_M";

    ASSERT_EQ(1, (int)s.smooth(1, 1)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_CARPET_UL, (int)s.query_x_y(1, 1))
        << "carpet around==(TO_RIGHT|TO_DOWN)==6 takes carpet_by_surround[6] == CARPET_UL";

    ASSERT_EQ(1, (int)s.smooth(3, 3)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_CARPET_LR, (int)s.query_x_y(3, 3))
        << "carpet around==(TO_UP|TO_LEFT)==9 takes carpet_by_surround[9] == CARPET_LR";
}


TEST(SmoothOps, smooth_smooth_cobble_border)
{
    PixieData pd = make_uniform_grid(5, 5, PIX_COBBLE_1);
    set_tile(pd, 0, 0, PIX_GRASS1);

    // rng index 2: both the grass and cobble variant tables then move off the
    // seeded byte, so a rng-ignoring arm (or a no-op) goes red.
    ScopedFixedRng rng2(2);
    smoother s;
    rng2.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(0, 0)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS3, (int)s.query_x_y(0, 0))
        << "grass takes grass_variants[rng(4)==2] == GRASS3";

    ASSERT_EQ(1, (int)s.smooth(1, 0)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_COBBLE_3, (int)s.query_x_y(1, 0))
        << "cobble takes cobble_variants[rng(4)==2] == COBBLE_3";

    ASSERT_EQ(1, (int)s.smooth(0, 1)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_COBBLE_3, (int)s.query_x_y(0, 1))
        << "cobble is surround-independent: the same variant on the other edge cell";
}


TEST(SmoothOps, smooth_smooth_dark_grass_border)
{
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS2);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 5; ++i)
            set_tile(pd, i, j, PIX_GRASS_DARK_1);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS_RUBBLE, (int)s.query_x_y(2, 2))
        << "dark grass around==(TO_LEFT|TO_RIGHT|TO_UP)==11 takes grass_dark_bottom[0], then rng(20)==0 overrides it with rubble";

    ASSERT_EQ(1, (int)s.smooth(2, 3)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(2, 3))
        << "the plain-grass side of the seam takes grass_variants[0]";
}


TEST(SmoothOps, smooth_smooth_light_grass_border)
{
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS2);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 5; ++i)
            set_tile(pd, i, j, PIX_GRASS_LIGHT_1);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    ASSERT_EQ(1, (int)s.smooth(2, 2)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS_LIGHT_BOTTOM, (int)s.query_x_y(2, 2))
        << "light grass around==(TO_UP|TO_RIGHT|TO_LEFT)==11 takes grass_light_by_surround[11] == GRASS_LIGHT_BOTTOM";

    ASSERT_EQ(1, (int)s.smooth(2, 3)) << "smooth(x,y) reports it wrote a tile";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(2, 3))
        << "the plain-grass side of the seam takes grass_variants[0]";
}


TEST(SmoothOps, smooth_smooth_all_edges)
{
    // Seeded GRASS3, so every one of the 25 cells must MOVE to GRASS1: a
    // skipped edge cell, or a write that lands outside the span, goes red.
    PixieData pd = make_uniform_grid(5, 5, PIX_GRASS3);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    // Smooth every cell including edges
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            ASSERT_EQ(1, (int)s.smooth(x, y)) << "smooth reports a write at (" << x << "," << y << ")";

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(x, y))
                << "edge cells clamp out-of-range neighbours to grass, so every cell is GRASS1 at ("
                << x << "," << y << ")";
}


TEST(SmoothOps, smooth_smooth_mixed_terrain)
{
    // Create a checkerboard of different terrain types. Every cell's four
    // cardinals differ from it, so around==0 everywhere; grass and dirt are
    // seeded off their arms' output so a no-op smooth cannot hide.
    const unsigned char types[4]    = { PIX_GRASS3, PIX_WATER2, PIX_DIRTGRASS_UL1, PIX_WALL2 };
    const unsigned char expected[4] = { PIX_GRASS1, PIX_WATER2, PIX_DIRT_1,        PIX_WALL2 };

    PixieData pd = make_uniform_grid(7, 7, PIX_GRASS3);
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++)
            set_tile(pd, i, j, types[(i + j) % 4]);

    ScopedFixedRng rng0(0);
    smoother s;
    rng0.bind(s);
    s.set_target(pd);

    // Smooth the entire grid
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++)
            ASSERT_EQ(1, (int)s.smooth(x, y)) << "smooth reports a write at (" << x << "," << y << ")";

    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 7; x++)
            ASSERT_EQ((int)expected[(x + y) % 4], (int)s.query_x_y(x, y))
                << "around==0 cell (" << x << "," << y << "): grass=>GRASS1, water keeps its tile, "
                << "dirt=>dirt_by_surround[0], wall keeps herepix";
}


// ---------------------------------------------------------------------------
// smoother reset
// ---------------------------------------------------------------------------

TEST(SmoothOps, smooth_reset)
{
    smoother s;
    auto& world = og::runtime::current_session->myscreen_->world();
    world.create_new_grid();
    s.set_target(world.grid);

    // Make the live cell distinguishable from reset()'s PIX_GRASS1 fallback.
    world.grid.data[0] = PIX_WATER1;
    ASSERT_TRUE(s.has_target()) << "set_target installs the non-owning grid view";
    ASSERT_TRUE(s.targets(world.grid)) << "the view points at this grid";
    ASSERT_EQ((int)PIX_WATER1, (int)s.query_x_y(0, 0)) << "reads the live grid before reset";

    s.reset();

    ASSERT_FALSE(s.has_target()) << "reset drops the non-owning grid view (issue #12)";
    ASSERT_FALSE(s.targets(world.grid)) << "a reset smoother no longer targets the grid";
    ASSERT_EQ(0, (int)s.smooth()) << "smooth() with no target is a no-op returning 0";
    ASSERT_EQ((int)PIX_GRASS1, (int)s.query_x_y(0, 0)) << "after reset queries fall back to PIX_GRASS1";
    ASSERT_EQ((int)PIX_WATER1, (int)world.grid.data[0]) << "the reset smoother wrote nothing through the dropped view";

    run_smooth_branch_outputs_with_fixed_rng();
}


TEST(SmoothOps, smooth_dark_grass_round7_branch_matrix_338_448)
{
    GameContext test_ctx;
    FixedRandom fixed0(0);
    FixedRandom fixed1(1);
    test_ctx.rng = &fixed0;
    push_test_context(&test_ctx);

    // around == (TO_UP | TO_DOWN | TO_LEFT): right-middle branch, rng(2)==1
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        test_ctx.rng = &fixed1;
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_R2, (int)s.query_x_y(1, 1)) << "right-middle dark-grass branch should choose R2";
    }

    // around == (TO_LEFT | TO_DOWN): top-right branch with right grass / non-grass
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)s.query_x_y(1, 1)) << "top-right branch with grass right should map to LL";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_GRASS_DARK_1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_B2, (int)s.query_x_y(1, 1)) << "top-right branch with non-grass right should map to B2";
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
        ASSERT_EQ((int)PIX_GRASS_RUBBLE, (int)s.query_x_y(1, 1)) << "bottom-middle branch should allow rubble override";
    }

    // around == TO_LEFT : right-thin branch with grass/non-grass right.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)s.query_x_y(1, 1)) << "right-thin branch with grass right should map LL";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_B1, (int)s.query_x_y(1, 1)) << "right-thin branch with non-grass right should map B1";
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
        ASSERT_EQ((int)PIX_GRASS_DARK_2, (int)s.query_x_y(1, 1)) << "left-middle/top-left branch takes grass_dark_variants[rng(4)==1] == DARK_2";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP|TO_DOWN
        smoother s;
        s.set_target(pd);
        test_ctx.rng = &fixed0;
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_R1, (int)s.query_x_y(1, 1)) << "center-vertical branch should map to R1/R2";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_DOWN
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_LL, (int)s.query_x_y(1, 1)) << "top-alone branch should map LL/B1";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP|TO_RIGHT
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)s.query_x_y(1, 1)) << "bottom-left branch should map UR/B1";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_RIGHT
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)s.query_x_y(1, 1)) << "left-alone branch should map UR/B1";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS_DARK_1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // TO_UP
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_UR, (int)s.query_x_y(1, 1)) << "bottom-alone branch should map UR/B1";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_DARK_1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1); // around == 0
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_DARK_1, (int)s.query_x_y(1, 1)) << "default dark-grass branch should map to dark_1";
    }

    pop_test_context();
}


static PixieData make_uniform_grid(int w, int h, unsigned char fill)
{
    PixieData pd;
    pd.w = static_cast<unsigned char>(w);
    pd.h = static_cast<unsigned char>(h);
    pd.frames = 1;
    pd.data = std::make_unique<unsigned char[]>(static_cast<std::size_t>(w * h));
    for (int i = 0; i < w * h; ++i)
        pd.data[static_cast<std::size_t>(i)] = fill;
    return pd;
}

static void set_tile(PixieData& pd, int x, int y, unsigned char v)
{
    pd.data[static_cast<std::size_t>(y * pd.w + x)] = v;
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
    push_test_context(&test_ctx);

    // Dirt corner case: around == (TO_LEFT | TO_DOWN) => PIX_DIRTGRASS_LL1
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_GRASS1);
        set_tile(pd, 1, 1, PIX_DIRT_1);
        set_tile(pd, 0, 1, PIX_DIRT_1);
        set_tile(pd, 1, 2, PIX_DIRT_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_DIRTGRASS_LL1, (int)s.query_x_y(1, 1)) << "dirt top-right edge should map to LL transition";
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
        ASSERT_EQ((int)PIX_DIRTGRASS_DARK_LR1, (int)s.query_x_y(1, 1)) << "dark dirt top-left edge should map to LR transition";
    }

    // Cobble deterministic RNG branches.
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_COBBLE_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_COBBLE_1, (int)s.query_x_y(1, 1)) << "cobble rng=0 should choose variant 1";
    }

    FixedRandom fixed3(3);
    test_ctx.rng = &fixed3;
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_COBBLE_1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_COBBLE_4, (int)s.query_x_y(1, 1)) << "cobble rng=3 should choose variant 4";
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
        ASSERT_EQ((int)PIX_WALL4, (int)s.query_x_y(1, 1)) << "arrow slit over pavement should become stone arrow wall";
    }
    {
        PixieData pd = make_uniform_grid(3, 3, PIX_H_WALL1);
        set_tile(pd, 1, 1, PIX_WALL_ARROW_GRASS);
        set_tile(pd, 1, 0, PIX_FLOOR1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_WALL_ARROW_FLOOR, (int)s.query_x_y(1, 1)) << "arrow slit over floor should become floor arrow wall";
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
        ASSERT_EQ((int)PIX_WALLSIDE_CRACK_C1, (int)s.query_x_y(1, 1)) << "wall base should choose crack when rng hits 0";
    }

    // Unknown type should remain unchanged.
    {
        PixieData pd = make_uniform_grid(3, 3, 222);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ(222, (int)s.query_x_y(1, 1)) << "unknown tile type should remain unchanged";
    }

    // Grass to water corner transitions.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                                           PIX_WATER1, PIX_GRASS1, PIX_WATER1, PIX_WATER1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASSWATER_LL, (int)s.query_x_y(1, 1)) << "grass-water LL transition";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS1,
                                           PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_WATER1, PIX_WATER1, PIX_GRASS1, PIX_WATER1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASSWATER_UR, (int)s.query_x_y(1, 1)) << "grass-water UR transition";
    }

    // Carpet and light-grass shape selection.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_CARPET_M,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_CARPET_SMALL_TINY, (int)s.query_x_y(1, 1)) << "isolated carpet should become tiny";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_GRASS_LIGHT_1,
                                           PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_GRASS_LIGHT_LEFT_BOTTOM, (int)s.query_x_y(1, 1)) << "light grass up+right mask should map to left-bottom variant";
    }

    // Water edge variants.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_WATER1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_WATERGRASS_LL, (int)s.query_x_y(1, 1)) << "water with only up neighbor should map LL/left cap";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_WATER1, PIX_GRASS1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)s.query_x_y(1, 1)) << "water with only right neighbor should map UL/upper cap";
    }

    // Trees: top-middle and center-vertical variants.
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_TREE_T1, (int)s.query_x_y(1, 1)) << "trees top-middle should map to top tile";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_TREE_MT, (int)s.query_x_y(1, 1)) << "trees vertical should map to trunk tile";
    }

    pop_test_context();
}

TEST(SmoothOps, smooth_round11_water_and_tree_edge_masks_662_720)
{
    FixedRandom fixed0(0);
    GameContext test_ctx;
    test_ctx.rng = &fixed0;
    push_test_context(&test_ctx);

    // TYPE_WATER diagonal-corner masks (smooth.cpp:662-669).
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_WATER1, PIX_GRASS1, PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_WATERGRASS_LR, (int)s.query_x_y(1, 1)) << "water up+left should map LR";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_WATER1, PIX_WATER1, PIX_GRASS1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_WATERGRASS_UL, (int)s.query_x_y(1, 1)) << "water down+right should map UL";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_WATER1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_WATER1, PIX_WATER1,
                                           PIX_GRASS1, PIX_GRASS1, PIX_GRASS1, PIX_GRASS1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_WATERGRASS_UR, (int)s.query_x_y(1, 1)) << "water down+left should map UR";
    }

    // TYPE_TREES TO_AROUND edge-side selection (smooth.cpp:715-720).
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_TREE_MR, (int)s.query_x_y(1, 1)) << "trees with missing upper-right should map to right edge";
    }
    {
        PixieData pd = make_center_pattern(PIX_GRASS1, PIX_TREE_M1,
                                           PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1,
                                           PIX_GRASS1, PIX_TREE_M1, PIX_TREE_M1, PIX_TREE_M1);
        smoother s;
        s.set_target(pd);
        s.smooth(1, 1);
        ASSERT_EQ((int)PIX_TREE_ML, (int)s.query_x_y(1, 1)) << "trees with missing upper-left should map to left edge";
    }

    pop_test_context();
}
