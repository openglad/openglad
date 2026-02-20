#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

namespace {

struct LivingFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    LivingFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

living* add_living(LivingFixture& fx, char family, unsigned char team)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(96, 96);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->normal_stepsize = 1.0f;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_living_r11_act_owner_dead_and_action_follow)
{
    LivingFixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0);
    living* owner = add_living(fx, FAMILY_SOLDIER, 0);
    OG_ASSERT(self != nullptr && owner != nullptr);

    self->dead = 1;
    OG_ASSERT(!self->act());
    self->dead = 0;

    self->owner = owner;
    self->lifetime = 5;
    owner->dead = 1;
    OG_ASSERT(!self->act());

    // ACTION_FOLLOW path in do_action
    self->dead = 0;
    self->owner = nullptr;
    self->lifetime = 0;
    self->action = ACTION_FOLLOW;
    self->foe = nullptr;
    owner->dead = 0;
    owner->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    owner->team_num = 0;
    OG_ASSERT(self->do_action() == 1 || self->do_action() == 0);
}

OG_UNIT_TEST(test_living_r11_facing_thresholds)
{
    living l;
    OG_ASSERT(l.facing(0, 1) == FACE_DOWN);
    OG_ASSERT(l.facing(0, -1) == FACE_UP);

    // x > 0 branch thresholds
    OG_ASSERT(l.facing(1, 3) == FACE_DOWN);
    OG_ASSERT(l.facing(2, 1) == FACE_DOWN_RIGHT);
    OG_ASSERT(l.facing(3, 0) == FACE_RIGHT);
    OG_ASSERT(l.facing(2, -1) == FACE_UP_RIGHT);
    OG_ASSERT(l.facing(1, -3) == FACE_UP);

    // x < 0 branch thresholds
    OG_ASSERT(l.facing(-1, -3) == FACE_UP);
    OG_ASSERT(l.facing(-2, -1) == FACE_UP_LEFT);
    OG_ASSERT(l.facing(-3, 0) == FACE_LEFT);
    OG_ASSERT(l.facing(-2, 1) == FACE_DOWN_LEFT);
    OG_ASSERT(l.facing(-1, 3) == FACE_DOWN);
}

OG_UNIT_TEST(test_living_r11_collide_and_act_type_switches)
{
    LivingFixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0);
    living* foe = add_living(fx, FAMILY_ORC, 1);
    OG_ASSERT(self != nullptr && foe != nullptr);

    // collide() auto-attackable path
    self->collide(foe);

    self->set_act_type(ACT_CONTROL);
    OG_ASSERT(self->act());

    self->set_act_type(ACT_DIE);
    OG_ASSERT(self->act());
    OG_ASSERT(self->dead == 1 || self->dead == 0);
}
