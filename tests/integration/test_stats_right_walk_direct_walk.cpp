#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
// walker/pixie store raw pointers into PixieData buffers; keep the data alive.
static PixieData one_px()
{
    return PixieData(1, 1, 1, new unsigned char[1]{0});
}

static void set_all_tiles(unsigned char tile)
{
    auto& lvl = og::runtime::current_session->myscreen_->level_runtime_data();
    if (!lvl.world().grid.valid())
        lvl.create_new_grid();
    const int size = static_cast<int>(lvl.world().grid.w) * static_cast<int>(lvl.world().grid.h);
    for (int i = 0; i < size; i++)
        lvl.world().grid.data[static_cast<std::size_t>(i)] = tile;
}

static void set_tile(int tx, int ty, unsigned char tile)
{
    auto& lvl = og::runtime::current_session->myscreen_->level_runtime_data();
    if (!lvl.world().grid.valid())
        lvl.create_new_grid();
    if (tx < 0 || ty < 0 || tx >= lvl.world().grid.w || ty >= lvl.world().grid.h)
        return;
    lvl.world().grid.data[static_cast<std::size_t>(ty * lvl.world().grid.w + tx)] = tile;
}
} // namespace

TEST(StatsRightWalkDirectWalk, stats_right_walk_turn_right_adds_walk_command_all_enddirs)
{
    set_all_tiles(PIX_GRASS1);

    // Block only the right-back cell for FACE_UP when placed at (GRID_SIZE-1, GRID_SIZE-1).
    // Coordinate mapping with CHECK_STEP_SIZE=1:
    // right_blocked      checks (x+1, y)   -> tile (1,0)
    // right_forward      checks (x+1, y-1) -> tile (1,0)
    // forward_blocked    checks (x,   y-1) -> tile (0,0)
    // right_back_blocked checks (x+1, y+1) -> tile (1,1)
    set_tile(1, 1, PIX_H_WALL1);

    PixieData px = one_px();
    walker w(px);
    w.set_stepsize(1.0f);
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_curdir(FACE_UP);
    w.set_lastx(0.0f);
    w.set_lasty(0.0f);

    statistics* st = w.stats();
    ASSERT_TRUE(st != nullptr) << "stats exists";

    // For each desired enddir, choose an initial value such that (enddir+2)%8 == desired.
    for (int desired = 0; desired < 8; desired++)
    {
        st->clear_command();
        w.set_enddir(static_cast<char>((desired + 6) % 8)); // desired-2 mod 8
        ASSERT_TRUE(st->right_walk()) << "right_walk returns true on the right_back_blocked turn-right path";

        ASSERT_TRUE(!st->commands.empty()) << "right_walk should add a command when right_back_blocked";

        const command& c = st->commands.front();
        ASSERT_EQ(COMMAND_WALK, static_cast<int>(c.commandtype)) << "right_walk should enqueue COMMAND_WALK";

        int ex = 0;
        int ey = 0;
        switch (desired)
        {
            case FACE_UP: ex = 0; ey = -1; break;
            case FACE_UP_RIGHT: ex = 1; ey = -1; break;
            case FACE_RIGHT: ex = 1; ey = 0; break;
            case FACE_DOWN_RIGHT: ex = 1; ey = 1; break;
            case FACE_DOWN: ex = 0; ey = 1; break;
            case FACE_DOWN_LEFT: ex = -1; ey = 1; break;
            case FACE_LEFT: ex = -1; ey = 0; break;
            case FACE_UP_LEFT: ex = -1; ey = -1; break;
            default: ex = 0; ey = 0; break;
        }
        ASSERT_EQ(ex, static_cast<int>(c.com1)) << "right_walk xdelta matches enddir";
        ASSERT_EQ(ey, static_cast<int>(c.com2)) << "right_walk ydelta matches enddir";
    }
}


TEST(StatsRightWalkDirectWalk, stats_direct_walk_grid_passability_branches)
{
    // Create a minimal, deterministic grid layout around the controller.
    set_all_tiles(PIX_GRASS1);

    PixieData px = one_px();
    walker w(px);
    walker foe(px);

    w.set_stepsize(1.0f);
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_foe(&foe);

    statistics* st = w.stats();
    ASSERT_TRUE(st != nullptr) << "stats exists";

    // A) Diagonal blocked, x blocked, y blocked -> return 0.
    set_all_tiles(PIX_GRASS1);
    set_tile(1, 1, PIX_H_WALL1); // (x+1,y+1)
    set_tile(1, 0, PIX_H_WALL1); // (x+1,y)
    set_tile(0, 1, PIX_H_WALL1); // (x,y+1)
    foe.setxy(w.xpos() + 200, w.ypos() + 200);
    ASSERT_EQ(0, static_cast<int>(st->direct_walk())) << "direct_walk should fail when all direct tiles blocked";

    // B) Diagonal blocked, x blocked, y ok, but ydelta==0 -> return 0 via (!ydelta) branch.
    set_all_tiles(PIX_GRASS1);
    set_tile(1, 0, PIX_H_WALL1); // (x+1,y) blocks diagonal (y=0) and x
    foe.setxy(static_cast<Sint32>(w.xpos()) + 200, static_cast<Sint32>(w.ypos())); // ydelta==0
    ASSERT_EQ(0, static_cast<int>(st->direct_walk())) << "direct_walk should fail when ydelta==0 and x blocked";

    // C) Diagonal blocked, x ok, but xdelta==0 -> return 0 via (!xdelta) branch.
    set_all_tiles(PIX_GRASS1);
    set_tile(0, 1, PIX_H_WALL1); // (x,y+1) blocks diagonal when xdelta==0
    foe.setxy(static_cast<Sint32>(w.xpos()), static_cast<Sint32>(w.ypos()) + 200); // xdelta==0
    ASSERT_EQ(0, static_cast<int>(st->direct_walk())) << "direct_walk should fail when xdelta==0 and diagonal blocked";

    // D) Diagonal ok but xdelta==ydelta==0 -> return 0 via (!xdelta && !ydelta) branch.
    set_all_tiles(PIX_GRASS1);
    foe.setxy(static_cast<Sint32>(w.xpos()), static_cast<Sint32>(w.ypos()));
    ASSERT_EQ(0, static_cast<int>(st->direct_walk())) << "direct_walk should return 0 when foe at same position";
}


// right_walk's forward branch does two things a bare "it returned true" oracle
// cannot see: it zeroes the minor axis once the major one is more than 3x it,
// then normalizes what is left to a unit step (walkstep re-records that as
// lastx/lasty * stepsize). And when forward is blocked too, it turns LEFT --
// enddir += 6 mod 8, not +2.
TEST(StatsRightWalkDirectWalk, right_walk_normalizes_the_forward_step_and_turns_left_when_blocked)
{
    set_all_tiles(PIX_GRASS1);

    PixieData px = one_px();
    walker w(px);
    w.set_stepsize(1.0f);
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_curdir(FACE_UP);
    w.set_enddir(FACE_UP);
    w.set_foe(nullptr); // keep direct_walk path deterministic when reached

    statistics* st = w.stats();
    ASSERT_NE(nullptr, st) << "a walker always owns its statistics";

    // Condition: (right_blocked || right_forward_blocked) && !forward_blocked.
    // For FACE_UP at (GRID_SIZE-1, GRID_SIZE-1):
    // right/right_forward probe tile (1,0), forward probes (0,0).
    set_all_tiles(PIX_GRASS1);
    set_tile(1, 0, PIX_H_WALL1);

    // 10:1 in x -> y is zeroed, x normalized to a single step right.
    w.set_lastx(10.0f);
    w.set_lasty(1.0f);
    ASSERT_TRUE(st->right_walk()) << "right_walk takes the forward branch when the right side is blocked";
    EXPECT_FLOAT_EQ(1.0f, w.lastx()) << "a 10:1 x-major heading normalizes to a +1 x step";
    EXPECT_FLOAT_EQ(0.0f, w.lasty()) << "a 10:1 x-major heading zeroes the y minor axis";

    // 1:10 in y -> the mirror image.
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_curdir(FACE_UP);
    w.set_enddir(FACE_UP);
    w.set_lastx(1.0f);
    w.set_lasty(10.0f);
    ASSERT_TRUE(st->right_walk()) << "right_walk takes the forward branch when the right side is blocked";
    EXPECT_FLOAT_EQ(0.0f, w.lastx()) << "a 1:10 y-major heading zeroes the x minor axis";
    EXPECT_FLOAT_EQ(1.0f, w.lasty()) << "a 1:10 y-major heading normalizes to a +1 y step";

    // Neither axis dominates 3:1 -> both survive, each normalized to one step.
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_curdir(FACE_UP);
    w.set_enddir(FACE_UP);
    w.set_lastx(-4.0f);
    w.set_lasty(2.0f);
    ASSERT_TRUE(st->right_walk()) << "right_walk takes the forward branch when the right side is blocked";
    EXPECT_FLOAT_EQ(-1.0f, w.lastx()) << "a 2:1 heading keeps x, normalized and signed";
    EXPECT_FLOAT_EQ(1.0f, w.lasty()) << "a 2:1 heading keeps y, normalized and signed";

    // Forward blocked as well: turn LEFT from FACE_UP, i.e. (0 + 6) % 8.
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_curdir(FACE_UP);
    w.set_enddir(FACE_UP);
    set_tile(0, 0, PIX_H_WALL1);
    ASSERT_TRUE(st->right_walk()) << "right_walk still succeeds by turning when forward is blocked";
    EXPECT_EQ(FACE_LEFT, static_cast<int>(w.enddir()))
        << "blocked right AND forward turns left: enddir = (FACE_UP + 6) % 8";

    // The second turn-left arm (right clear, forward blocked) lands on the
    // same rule one step further round the compass.
    set_all_tiles(PIX_GRASS1);
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.set_curdir(FACE_UP);
    w.set_enddir(FACE_UP);
    set_tile(0, 0, PIX_H_WALL1); // forward only
    ASSERT_TRUE(st->right_walk()) << "right_walk turns when only forward is blocked";
    EXPECT_EQ(FACE_LEFT, static_cast<int>(w.enddir()))
        << "forward-only blocked turns left as well";
}


// Each blocked helper probes exactly ONE cell, at (x+dx, y+dy) with
// dx,dy in {-1,0,+1} chosen per facing (stats.cpp's four switch tables). With
// the walker on a tile boundary a 1px offset resolves the SIGN of dx/dy: at
// (GRID_SIZE-1, GRID_SIZE-1) only +1 crosses into the next tile, at
// (GRID_SIZE, GRID_SIZE) only -1 crosses back. Walling one tile at a time and
// demanding the exact true/false per facing pins the whole table -- an
// all-grass board answers false no matter how the tables are permuted.
//
// This also absorbs the (void)-cast facing sweeps that used to live in
// StatsCoverage.stats_round6_block_query_switches_all_directions,
// StatsCoverage.stats_round7a_command_clamps_and_direction_switches and
// StatsNavigation.blocked_helpers_and_follow_fallback.
TEST(StatsRightWalkDirectWalk, blocked_probe_tables_resolve_each_facings_exact_cell)
{
    PixieData px = one_px();
    walker w(px);
    w.set_stepsize(1.0f);

    statistics* st = w.stats();
    ASSERT_NE(nullptr, st) << "a walker always owns its statistics";

    struct Probe { int dx; int dy; bool probes; };

    // stats.cpp:769-934. Index 0..7 is the facing; index 8 is an invalid dir:
    // right_blocked / forward_blocked fall through to a (0,0) self probe while
    // right_forward_blocked / right_back_blocked return false without probing.
    static const Probe kRightBlocked[9] = {
        {+1, 0, true}, {+1, +1, true}, {0, +1, true}, {-1, +1, true},
        {-1, 0, true}, {-1, -1, true}, {0, -1, true}, {+1, -1, true},
        {0, 0, true},
    };
    static const Probe kRightForwardBlocked[9] = {
        {+1, -1, true}, {+1, 0, true}, {+1, +1, true}, {0, +1, true},
        {-1, +1, true}, {-1, 0, true}, {-1, -1, true}, {0, -1, true},
        {0, 0, false},
    };
    static const Probe kRightBackBlocked[9] = {
        {+1, +1, true}, {0, +1, true}, {-1, +1, true}, {-1, 0, true},
        {-1, -1, true}, {0, -1, true}, {+1, -1, true}, {+1, 0, true},
        {0, 0, false},
    };
    static const Probe kForwardBlocked[9] = {
        {0, -1, true}, {+1, -1, true}, {+1, 0, true}, {+1, +1, true},
        {0, +1, true}, {-1, +1, true}, {-1, 0, true}, {-1, -1, true},
        {0, 0, true},
    };

    static const char kDirs[9] = {
        FACE_UP, FACE_UP_RIGHT, FACE_RIGHT, FACE_DOWN_RIGHT,
        FACE_DOWN, FACE_DOWN_LEFT, FACE_LEFT, FACE_UP_LEFT,
        static_cast<char>(99),
    };

    // anchor A sits one pixel inside tile 0; anchor B one pixel into tile 1.
    struct Anchor { int pos; bool high; };
    static const Anchor kAnchors[2] = {{GRID_SIZE - 1, false}, {GRID_SIZE, true}};

    const auto cell_of = [](const Anchor& a, int delta) {
        return a.high ? (delta < 0 ? 0 : 1) : (delta > 0 ? 1 : 0);
    };

    for (const Anchor& anchor : kAnchors)
    {
        // -1 is the control layout: nothing walled, so every probe must be
        // false. It also catches a stray object parked over tiles 0..1.
        for (int walled = -1; walled < 4; walled++)
        {
            const int wall_tx = walled < 0 ? -1 : (walled % 2);
            const int wall_ty = walled < 0 ? -1 : (walled / 2);

            set_all_tiles(PIX_GRASS1);
            if (walled >= 0)
                set_tile(wall_tx, wall_ty, PIX_H_WALL1);
            w.setxy(static_cast<float>(anchor.pos), static_cast<float>(anchor.pos));

            for (int i = 0; i < 9; i++)
            {
                SCOPED_TRACE(testing::Message()
                             << "anchor " << anchor.pos << " wall (" << wall_tx
                             << "," << wall_ty << ") dir " << static_cast<int>(kDirs[i]));
                w.set_curdir(kDirs[i]);

                const auto expect = [&](const Probe& p) {
                    if (!p.probes)
                        return false;
                    return cell_of(anchor, p.dx) == wall_tx &&
                           cell_of(anchor, p.dy) == wall_ty;
                };

                EXPECT_EQ(expect(kRightBlocked[i]), st->right_blocked())
                    << "right_blocked probes (" << kRightBlocked[i].dx << ","
                    << kRightBlocked[i].dy << ") for this facing";
                EXPECT_EQ(expect(kRightForwardBlocked[i]), st->right_forward_blocked())
                    << "right_forward_blocked probes (" << kRightForwardBlocked[i].dx
                    << "," << kRightForwardBlocked[i].dy << ") for this facing";
                EXPECT_EQ(expect(kRightBackBlocked[i]), st->right_back_blocked())
                    << "right_back_blocked probes (" << kRightBackBlocked[i].dx
                    << "," << kRightBackBlocked[i].dy << ") for this facing";
                EXPECT_EQ(expect(kForwardBlocked[i]), st->forward_blocked())
                    << "forward_blocked probes (" << kForwardBlocked[i].dx << ","
                    << kForwardBlocked[i].dy << ") for this facing";
            }
        }
    }

    set_all_tiles(PIX_GRASS1);
}
