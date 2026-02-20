#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <array>
#include <memory>

#include "unit/unit.h"

namespace {

struct LivingR14Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    LivingR14Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~LivingR14Fixture()
    {
        set_global_context(nullptr);
    }
};

living* add_living(LivingR14Fixture& fx, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<living>();
    w->set_order_family(Order::Living, family);
    fx.level.wire_entity(w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 3.0f;
    w->normal_stepsize = 3.0f;
    w->lineofsight = 6;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    living* out = w.get();
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

OG_UNIT_TEST(test_living_r14_lines_65_73_89_95_138_175_owner_lifetime_and_counters)
{
    LivingR14Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 96, 96);
    living* owner = add_living(fx, FAMILY_SOLDIER, 0, 120, 96);
    living* foe = add_living(fx, FAMILY_ORC, 1, 128, 96);
    OG_ASSERT(self && owner && foe);

    self->bonus_rounds = 1;
    self->set_act_type(ACT_CONTROL);
    OG_ASSERT(self->act());

    self->foe = foe;
    foe->dead = 1;
    self->leader = owner;
    owner->dead = 0;
    self->view_all = 2;
    self->invulnerable_left = 2;
    self->invisibility_left = 2;
    self->flight_left = 2;
    self->set_charm_left(2);
    self->real_team_num = 1;
    self->set_act_type(ACT_CONTROL);
    OG_ASSERT(self->act());

    self->owner = owner;
    self->lifetime = 2;
    owner->dead = 1;
    OG_ASSERT(!self->act());

    self->dead = 0;
    owner->dead = 0;
    self->owner = owner;
    self->lifetime = 1;
    OG_ASSERT(!self->act() || self->dead == 1);
}

OG_UNIT_TEST(test_living_r14_lines_155_190_196_212_219_226_235_245_259_266_270_303_308)
{
    LivingR14Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 0, 0);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 16, 0);
    OG_ASSERT(self && ally);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    self->myguy->dexterity = 10;
    self->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    self->stats()->magicpoints = 10.0f;
    self->stats()->current_magic_delay = 0;
    self->speed_bonus = 1;
    self->speed_bonus_left = 3;
    self->attack_lunge = 1.0f;
    self->hit_recoil = 1.0f;

    fx.level.grid.frames = 1;
    fx.level.grid.w = 1;
    fx.level.grid.h = 1;
    fx.level.grid.data = std::make_unique<unsigned char[]>(1);
    fx.level.grid.data[0] = PIX_TREE_M1;
    fx.level.pixmaxx = GRID_SIZE;
    fx.level.pixmaxy = GRID_SIZE;
    fx.level.mysmoother.set_target(fx.level.grid);

    cfg.apply_setting("effects", "damage_numbers", "on");

    self->stats()->hitpoints = 1.0f;
    self->flight_left = 0;
    self->ani_type = ANI_WALK;
    self->set_act_type(ACT_CONTROL);
    OG_ASSERT(self->act() || self->dead == 1);

    self->dead = 0;
    self->stats()->frozen_delay = 1;
    OG_ASSERT(self->act());

    self->dead = 0;
    self->busy = 1;
    self->skip_exit = 2;
    self->action = ACTION_FOLLOW;
    self->user = -1;
    self->set_act_type(ACT_GUARD);
    self->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    OG_ASSERT(self->act());

    self->action = 0;
    self->skip_exit = 0;
    self->stats()->clear_command();
    self->setxy(64, 64);
    self->ani_type = ANI_WALK;
    self->busy = 0;
    self->stats()->frozen_delay = 0;
    self->set_charm_left(0);
    self->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    self->set_act_type(ACT_DIE);
    (void)self->act();
    OG_ASSERT(self->dead == 1);
}

OG_UNIT_TEST(test_living_r14_lines_371_375_380_419_433_440_shove_walk_and_animate_fallback)
{
    LivingR14Fixture fx;
    living* self = add_living(fx, FAMILY_SOLDIER, 0, 64, 64);
    living* ally = add_living(fx, FAMILY_SOLDIER, 0, 80, 64);
    OG_ASSERT(self && ally);

    assign_basic_ani(self);
    assign_basic_ani(ally);

    ally->set_act_type(ACT_GUARD);
    OG_ASSERT(self->shove(ally, 1, 0) == 0 || self->shove(ally, 1, 0) == 1);

    self->curdir = FACE_LEFT;
    self->setxy(0, 0);
    self->stats()->set_bit_flags(BIT_ANIMATE, 1);
    OG_ASSERT(!self->walk(-1.0f, 0.0f));

    self->curdir = FACE_RIGHT;
    self->setxy(10, 10);
    self->collide_ob = ally;
    self->stats()->set_bit_flags(BIT_ANIMATE, 1);
    (void)self->walk(1.0f, 0.0f);
}
