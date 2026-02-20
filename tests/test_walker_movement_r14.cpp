#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
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

struct MovementR14Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    MovementR14Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(MovementR14Fixture& fx, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[i][0] = 0;
        seqs[i][1] = 1;
        seqs[i][2] = -1;
        seqs[i][3] = -1;
        rows[i] = seqs[i].data();
    }
    w->ani = rows.data();
}

} // namespace

OG_UNIT_TEST(test_walker_movement_r14_lines_175_186_198_210_npc_fallback_cases)
{
    MovementR14Fixture fx;
    walker* npc = add_living(fx, 0, 0);
    OG_ASSERT(npc != nullptr);

    assign_basic_ani(npc);
    npc->user = -1;

    npc->curdir = FACE_UP;
    (void)npc->walkstep(0.0f, -1.0f);

    npc->setxy(fx.level.pixmaxx - 1, 0);
    npc->curdir = FACE_RIGHT;
    (void)npc->walkstep(1.0f, 0.0f);

    npc->setxy(0, fx.level.pixmaxy - 1);
    npc->curdir = FACE_DOWN;
    (void)npc->walkstep(0.0f, 1.0f);

    npc->setxy(0, fx.level.pixmaxy - 1);
    npc->curdir = FACE_DOWN_RIGHT;
    (void)npc->walkstep(1.0f, 1.0f);

    npc->setxy(0, 0);
    npc->curdir = FACE_UP_LEFT;
    (void)npc->walkstep(-1.0f, -1.0f);
}

OG_UNIT_TEST(test_walker_movement_r14_lines_234_255_268_273_278_285_292_user_slide_and_turn_default)
{
    MovementR14Fixture fx;
    walker* user = add_living(fx, 0, 0);
    OG_ASSERT(user != nullptr);

    assign_basic_ani(user);
    user->user = 0;

    user->curdir = FACE_UP;
    (void)user->walkstep(0.0f, -1.0f);

    user->curdir = FACE_UP_RIGHT;
    (void)user->walkstep(1.0f, -1.0f);

    user->curdir = FACE_DOWN_LEFT;
    (void)user->walkstep(-1.0f, 1.0f);

    user->curdir = 127;
    (void)user->turn(FACE_RIGHT);
}
