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
        lvl.world().grid.data[i] = tile;
}

static void set_tile(int tx, int ty, unsigned char tile)
{
    auto& lvl = og::runtime::current_session->myscreen_->level_runtime_data();
    if (!lvl.world().grid.valid())
        lvl.create_new_grid();
    if (tx < 0 || ty < 0 || tx >= lvl.world().grid.w || ty >= lvl.world().grid.h)
        return;
    lvl.world().grid.data[ty * lvl.world().grid.w + tx] = tile;
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
    if (!st)
        return;

    // For each desired enddir, choose an initial value such that (enddir+2)%8 == desired.
    for (int desired = 0; desired < 8; desired++)
    {
        st->clear_command();
        w.set_enddir(static_cast<char>((desired + 6) % 8)); // desired-2 mod 8
        (void)st->right_walk();

        ASSERT_TRUE(!st->commands.empty()) << "right_walk should add a command when right_back_blocked";
        if (st->commands.empty())
            continue;

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
    if (!st)
        return;

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


TEST(StatsRightWalkDirectWalk, stats_right_walk_forward_normalization_and_forward_blocked_turn_branch)
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
    ASSERT_TRUE(st != nullptr) << "stats exists";
    if (!st)
        return;

    // Condition: (right_blocked || right_forward_blocked) && !forward_blocked.
    // For FACE_UP at (GRID_SIZE-1, GRID_SIZE-1):
    // right/right_forward probe tile (1,0), forward probes (0,0).
    set_all_tiles(PIX_GRASS1);
    set_tile(1, 0, PIX_H_WALL1);

    w.set_lastx(10.0f);
    w.set_lasty(1.0f);
    ASSERT_TRUE(st->right_walk()) << "right_walk should take forward branch when right side is blocked";

    w.set_lastx(1.0f);
    w.set_lasty(10.0f);
    ASSERT_TRUE(st->right_walk()) << "right_walk should normalize steep-y forward branch";

    // Force explicit forward_blocked branch: block forward tile too.
    set_tile(0, 0, PIX_H_WALL1);
    ASSERT_TRUE(st->right_walk()) << "right_walk should still succeed via turn-left when forward is blocked";
}


TEST(StatsRightWalkDirectWalk, stats_blocked_direction_switch_tables_all_cases_round6)
{
    set_all_tiles(PIX_GRASS1);

    PixieData px = one_px();
    walker w(px);
    w.set_stepsize(1.0f);
    w.setxy(GRID_SIZE * 4, GRID_SIZE * 4);

    statistics* st = w.stats();
    ASSERT_TRUE(st != nullptr) << "stats exists";
    if (!st)
        return;

    const std::array<char, 9> dirs = {
        FACE_UP, FACE_UP_RIGHT, FACE_RIGHT, FACE_DOWN_RIGHT,
        FACE_DOWN, FACE_DOWN_LEFT, FACE_LEFT, FACE_UP_LEFT,
        static_cast<char>(99)
    };
    for (char d : dirs)
    {
        SCOPED_TRACE(static_cast<int>(d));
        w.set_curdir(d);
        EXPECT_FALSE(st->right_blocked());
        EXPECT_FALSE(st->right_forward_blocked());
        EXPECT_FALSE(st->right_back_blocked());
        EXPECT_FALSE(st->forward_blocked());
    }
}
