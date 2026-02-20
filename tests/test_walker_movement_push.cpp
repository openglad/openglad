#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>

#include <memory>

#include "unit/unit.h"

namespace {

struct MovementFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    MovementFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(MovementFixture& fx, char family = FAMILY_SOLDIER)
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

} // namespace

OG_UNIT_TEST(test_walker_movement_facing_thresholds)
{
    MovementFixture fx;
    walker* w = add_living(fx);
    OG_ASSERT(w != nullptr);

    OG_ASSERT(w->facing(0, 1) == FACE_DOWN);
    OG_ASSERT(w->facing(0, -1) == FACE_UP);
    OG_ASSERT(w->facing(1, 0) == FACE_RIGHT);
    OG_ASSERT(w->facing(-1, 0) == FACE_LEFT);
    OG_ASSERT(w->facing(1, 1) == FACE_DOWN_RIGHT);
    OG_ASSERT(w->facing(1, -1) == FACE_UP_RIGHT);
    OG_ASSERT(w->facing(-1, 1) == FACE_DOWN_LEFT);
    OG_ASSERT(w->facing(-1, -1) == FACE_UP_LEFT);
}

OG_UNIT_TEST(test_walker_movement_turn_stationary_and_normal)
{
    MovementFixture fx;
    walker* normal = add_living(fx, FAMILY_SOLDIER);
    OG_ASSERT(normal != nullptr);
    normal->stepsize = 2.0f;
    normal->curdir = FACE_UP;
    normal->lastx = 0.0f;
    normal->lasty = -normal->stepsize;
    normal->turn(FACE_RIGHT);
    OG_ASSERT(normal->curdir != FACE_UP);
    OG_ASSERT(!(normal->lastx == 0.0f && normal->lasty == -normal->stepsize));

    walker* tower = add_living(fx, FAMILY_TOWER1);
    OG_ASSERT(tower != nullptr);
    tower->stepsize = 3.0f;
    tower->lastx = 9.0f;
    tower->lasty = -4.0f;
    tower->curdir = FACE_UP;
    tower->turn(FACE_LEFT);
    OG_ASSERT(tower->lastx == 9.0f);
    OG_ASSERT(tower->lasty == -4.0f);
}

OG_UNIT_TEST(test_walker_movement_walk_and_walkstep_edge_paths)
{
    MovementFixture fx;
    walker* w = add_living(fx);
    OG_ASSERT(w != nullptr);

    w->curdir = FACE_RIGHT;
    OG_ASSERT(w->walk(1.0f, 0.0f));

    w->setxy(0, 0);
    w->curdir = FACE_LEFT;
    OG_ASSERT(!w->walk(-1.0f, 0.0f));

    w->user = -1;
    (void)w->walkstep(-1.0f, -1.0f);
    (void)w->walkstep(1.0f, -1.0f);

    w->user = 0;
    w->setxy(0, 10);
    (void)w->walkstep(-1.0f, -1.0f);
    (void)w->walkstep(-1.0f, 1.0f);
}
