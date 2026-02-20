#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <array>
#include <memory>

#include "unit/unit.h"

namespace {

struct WalkerMovementR11Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    WalkerMovementR11Fixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(WalkerMovementR11Fixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(96, 96);
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

void assign_ani(walker* w)
{
    static std::array<std::array<signed char, 3>, 8> seqs{};
    static std::array<signed char*, 8> rows{};
    for (int i = 0; i < 8; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = -1;
        seqs[i][2] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

OG_UNIT_TEST(test_walker_movement_r11_move_worldmove_setxy_and_setworldxy)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    OG_ASSERT(w != nullptr);

    (void)w->move(1, -1);
    w->worldmove(0.5f, -0.5f);
    OG_ASSERT(w->xpos != 0 || w->ypos != 0);

    w->ignore = 1;
    (void)w->setxy(100, 100);
    w->setworldxy(101.0f, 99.0f);
    w->ignore = 0;
}

OG_UNIT_TEST(test_walker_movement_r11_walkstep_npc_and_user_slide_paths)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    OG_ASSERT(w != nullptr);
    assign_ani(w);

    // Force blocked movement by surrounding a tile with non-passable wall tile.
    w->setxy(0, 0);
    w->user = -1;
    for (int d = 0; d < 8; ++d)
    {
        const float dx = (d == FACE_LEFT || d == FACE_UP_LEFT || d == FACE_DOWN_LEFT) ? -1.0f : ((d == FACE_UP || d == FACE_DOWN) ? 0.0f : 1.0f);
        const float dy = (d == FACE_UP || d == FACE_UP_LEFT || d == FACE_UP_RIGHT) ? -1.0f : ((d == FACE_LEFT || d == FACE_RIGHT) ? 0.0f : 1.0f);
        (void)w->walkstep(dx, dy);
    }

    // user branch with diagonal slide checks
    w->user = 0;
    w->setxy(1, 1);
    (void)w->walkstep(1.0f, 1.0f);
    (void)w->walkstep(-1.0f, 1.0f);
    (void)w->walkstep(1.0f, -1.0f);
    (void)w->walkstep(-1.0f, -1.0f);
}

OG_UNIT_TEST(test_walker_movement_r11_walk_turn_and_angles)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    OG_ASSERT(w != nullptr);
    assign_ani(w);

    // walk(0,0) and changed-direction branch
    OG_ASSERT(w->walk(0.0f, 0.0f));
    w->curdir = FACE_UP;
    OG_ASSERT(w->walk(1.0f, 0.0f));

    // blocked path with BIT_ANIMATE branch
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    w->setxy(0, 0);
    (void)w->walk(-1.0f, 0.0f);

    // turn switch and default path
    for (int d = 0; d < 8; ++d)
    {
        w->curdir = static_cast<char>(d);
        (void)w->turn(static_cast<short>((d + 3) % 8));
    }
    w->curdir = 120;
    (void)w->turn(2);

    // stationary turn branch in family descriptor path
    walker* tower = add_living(fx, FAMILY_TOWER1);
    tower->stepsize = 2.0f;
    tower->lastx = 5.0f;
    tower->lasty = 6.0f;
    (void)tower->turn(FACE_LEFT);
}
