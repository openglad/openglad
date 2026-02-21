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

struct MovementR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    MovementR12Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(MovementR12Fixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(64, 64);
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

OG_UNIT_TEST(test_walker_movement_r12_stationary_slope_and_animate_paths)
{
    MovementR12Fixture fx;
    walker* station = add_living(fx, FAMILY_TOWER1);
    walker* mover = add_living(fx, FAMILY_SOLDIER);
    OG_ASSERT(station && mover);

    station->stepsize = 3.0f;
    OG_ASSERT(station->walkstep(1.0f, 0.0f));
    OG_ASSERT(station->lastx == 1.0f);
    OG_ASSERT(station->lasty == 0.0f);

    OG_ASSERT(mover->facing(2, 5) == FACE_DOWN);
    OG_ASSERT(mover->facing(2, 1) == FACE_DOWN_RIGHT);
    OG_ASSERT(mover->facing(2, -1) == FACE_UP_RIGHT);
    OG_ASSERT(mover->facing(2, -5) == FACE_UP);
    OG_ASSERT(mover->facing(-2, 5) == FACE_DOWN);
    OG_ASSERT(mover->facing(-2, 1) == FACE_DOWN_LEFT);
    OG_ASSERT(mover->facing(-2, -1) == FACE_UP_LEFT);
    OG_ASSERT(mover->facing(-2, -5) == FACE_UP);

    assign_basic_ani(mover);
    mover->stats()->set_bit_flags(BIT_ANIMATE, 1);
    mover->setxy(0, 0);
    mover->curdir = FACE_LEFT;
    OG_ASSERT(!mover->walk(-1.0f, 0.0f));
}

OG_UNIT_TEST(test_walker_movement_r12_walkstep_npc_and_user_slide_paths)
{
    MovementR12Fixture fx;
    walker* npc = add_living(fx, FAMILY_SOLDIER);
    walker* user = add_living(fx, FAMILY_SOLDIER);
    OG_ASSERT(npc && user);
    assign_basic_ani(npc);
    assign_basic_ani(user);

    // walk() wrapper
    npc->lastx = 1.0f;
    npc->lasty = 0.0f;
    (void)npc->walk();

    // shove non-living fallback path
    OG_ASSERT(npc->shove(user, 1, 0) == -1);

    npc->setxy(0, 0);
    npc->stepsize = 1.0f;
    npc->user = -1;

    // NPC fallback switch paths from blocked movement.
    npc->curdir = FACE_LEFT;
    (void)npc->walkstep(-1.0f, 0.0f);
    npc->curdir = FACE_UP;
    (void)npc->walkstep(0.0f, -1.0f);
    npc->curdir = FACE_UP_RIGHT;
    (void)npc->walkstep(1.0f, -1.0f);
    npc->curdir = FACE_DOWN_LEFT;
    (void)npc->walkstep(-1.0f, 1.0f);

    // User slide path where diagonal move is blocked but one axis can progress.
    user->setxy(0, 1);
    user->stepsize = 1.0f;
    user->user = 0;
    user->curdir = FACE_UP_LEFT;
    (void)user->walkstep(-1.0f, -1.0f);

    // Invalid BIT_ANIMATE walk branch.
    user->stats()->set_bit_flags(BIT_ANIMATE, 1);
    user->setxy(0, 0);
    user->curdir = FACE_LEFT;
    OG_ASSERT(!user->walk(-1.0f, 0.0f));

    // turn default branch via invalid curdir.
    user->curdir = 127;
    (void)user->turn(FACE_UP);
}
