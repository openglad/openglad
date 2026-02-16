#include <openglad/core/stats.h>
#include <openglad/data/pixie_data.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <array>
#include <memory>

extern screen* myscreen;

namespace
{
// walker/pixie store raw pointers into PixieData buffers; keep the data alive.
static PixieData one_px()
{
    return PixieData(1, 1, 1, new unsigned char[1]{0});
}

static void set_all_tiles(unsigned char tile)
{
    auto& lvl = myscreen->level_data;
    if (!lvl.grid.valid())
        lvl.create_new_grid();
    const int size = static_cast<int>(lvl.grid.w) * static_cast<int>(lvl.grid.h);
    for (int i = 0; i < size; i++)
        lvl.grid.data[i] = tile;
}

static void set_tile(int tx, int ty, unsigned char tile)
{
    auto& lvl = myscreen->level_data;
    if (!lvl.grid.valid())
        lvl.create_new_grid();
    if (tx < 0 || ty < 0 || tx >= lvl.grid.w || ty >= lvl.grid.h)
        return;
    lvl.grid.data[ty * lvl.grid.w + tx] = tile;
}
} // namespace

void test_stats_right_walk_turn_right_adds_walk_command_all_enddirs()
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
    w.sim_rng = ctx().rng;
    w.sim_config = ctx().config;
    w.stepsize = 1.0f;
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.curdir = FACE_UP;
    w.lastx = 0.0f;
    w.lasty = 0.0f;

    statistics* st = w.stats();
    TEST_ASSERT(st != nullptr, "stats exists");
    if (!st)
        return;

    // For each desired enddir, choose an initial value such that (enddir+2)%8 == desired.
    for (int desired = 0; desired < 8; desired++)
    {
        st->clear_command();
        w.enddir = static_cast<char>((desired + 6) % 8); // desired-2 mod 8
        (void)st->right_walk();

        TEST_ASSERT(!st->commands.empty(), "right_walk should add a command when right_back_blocked");
        if (st->commands.empty())
            continue;

        const command& c = st->commands.front();
        TEST_ASSERT_EQ(COMMAND_WALK, static_cast<int>(c.commandtype), "right_walk should enqueue COMMAND_WALK");

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
        TEST_ASSERT_EQ(ex, static_cast<int>(c.com1), "right_walk xdelta matches enddir");
        TEST_ASSERT_EQ(ey, static_cast<int>(c.com2), "right_walk ydelta matches enddir");
    }
}
REGISTER_TEST(test_stats_right_walk_turn_right_adds_walk_command_all_enddirs);

void test_stats_direct_walk_grid_passability_branches()
{
    // Create a minimal, deterministic grid layout around the controller.
    set_all_tiles(PIX_GRASS1);

    PixieData px = one_px();
    walker w(px);
    walker foe(px);

    // The branch refactored walker methods to use the per-instance sim_level
    // pointer instead of the global myscreen->level_data.  Wire it up so
    // fire_check -> create_weapon -> sim_level->add_ob() doesn't segfault.
    w.sim_level = &myscreen->level_data;
    w.sim_rng = ctx().rng;
    w.sim_config = ctx().config;
    foe.sim_level = &myscreen->level_data;
    foe.sim_rng = ctx().rng;
    foe.sim_config = ctx().config;

    w.stepsize = 1.0f;
    w.setxy(GRID_SIZE - 1, GRID_SIZE - 1);
    w.foe = &foe;

    statistics* st = w.stats();
    TEST_ASSERT(st != nullptr, "stats exists");
    if (!st)
        return;

    // A) Diagonal blocked, x blocked, y blocked -> return 0.
    set_all_tiles(PIX_GRASS1);
    set_tile(1, 1, PIX_H_WALL1); // (x+1,y+1)
    set_tile(1, 0, PIX_H_WALL1); // (x+1,y)
    set_tile(0, 1, PIX_H_WALL1); // (x,y+1)
    foe.setxy(w.xpos + 200, w.ypos + 200);
    TEST_ASSERT_EQ(0, static_cast<int>(st->direct_walk()), "direct_walk should fail when all direct tiles blocked");

    // B) Diagonal blocked, x blocked, y ok, but ydelta==0 -> return 0 via (!ydelta) branch.
    set_all_tiles(PIX_GRASS1);
    set_tile(1, 0, PIX_H_WALL1); // (x+1,y) blocks diagonal (y=0) and x
    foe.setxy(static_cast<Sint32>(w.xpos) + 200, static_cast<Sint32>(w.ypos)); // ydelta==0
    TEST_ASSERT_EQ(0, static_cast<int>(st->direct_walk()), "direct_walk should fail when ydelta==0 and x blocked");

    // C) Diagonal blocked, x ok, but xdelta==0 -> return 0 via (!xdelta) branch.
    set_all_tiles(PIX_GRASS1);
    set_tile(0, 1, PIX_H_WALL1); // (x,y+1) blocks diagonal when xdelta==0
    foe.setxy(static_cast<Sint32>(w.xpos), static_cast<Sint32>(w.ypos) + 200); // xdelta==0
    TEST_ASSERT_EQ(0, static_cast<int>(st->direct_walk()), "direct_walk should fail when xdelta==0 and diagonal blocked");

    // D) Diagonal ok but xdelta==ydelta==0 -> return 0 via (!xdelta && !ydelta) branch.
    set_all_tiles(PIX_GRASS1);
    foe.setxy(static_cast<Sint32>(w.xpos), static_cast<Sint32>(w.ypos));
    TEST_ASSERT_EQ(0, static_cast<int>(st->direct_walk()), "direct_walk should return 0 when foe at same position");
}
REGISTER_TEST(test_stats_direct_walk_grid_passability_branches);
