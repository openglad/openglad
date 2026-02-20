#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

namespace {

struct StatsR14Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    StatsR14Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~StatsR14Fixture()
    {
        set_global_context(nullptr);
    }
};

walker* add_living(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

walker* add_weapon(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Weapon, FAMILY_ARROW);
    fx.level.wire_entity(w.get());
    w->sizex = 8;
    w->sizey = 8;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.weaplist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_stats_r14_lines_122_133_135_155_161_add_force_walk_clamps)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    OG_ASSERT(self != nullptr);

    self->stats()->add_command(COMMAND_FOLLOW, 1, 0, 0);
    self->stats()->add_command(COMMAND_WALK, 1, -9, 9);
    OG_ASSERT(!self->stats()->commands.empty());
    auto& back = self->stats()->commands.back();
    OG_ASSERT(back.com1 == -1);
    OG_ASSERT(back.com2 == 1);

    self->stats()->force_command(COMMAND_WALK, 1, 9, -9);
    OG_ASSERT(!self->stats()->commands.empty());
    auto& front = self->stats()->commands.front();
    OG_ASSERT(front.com1 == 1);
    OG_ASSERT(front.com2 == -1);
}

OG_UNIT_TEST(test_stats_r14_lines_249_255_301_313_319_344_command_switches)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    walker* foe = add_living(fx, 1, 220, 96);
    walker* lead = add_living(fx, 0, 300, 96);
    OG_ASSERT(self && foe && lead);

    self->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    (void)self->stats()->do_command();

    self->set_order_family(Order::Weapon, FAMILY_ARROW);
    self->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    (void)self->stats()->do_command();
    self->set_order_family(Order::Living, FAMILY_SOLDIER);

    self->leader = lead;
    self->foe = nullptr;
    self->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)self->stats()->do_command();

    self->foe = foe;
    self->lastx = 1.0f;
    self->lasty = 0.0f;
    self->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);
    (void)self->stats()->do_command();

    self->foe = foe;
    self->stats()->force_command(COMMAND_ATTACK, 1, 0, 0);
    (void)self->stats()->do_command();

    self->foe = nullptr;
    self->stats()->force_command(COMMAND_SEARCH, 1, 0, 0);
    (void)self->stats()->do_command();
}

OG_UNIT_TEST(test_stats_r14_lines_440_453_468_502_520_591_708_729_750_755_898_direct_and_blocked_paths)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 0, 0);
    walker* foe = add_living(fx, 1, 0, 0);
    walker* owner = add_living(fx, 1, 0, 0);
    walker* proj = add_weapon(fx, 1, 0, 0);
    OG_ASSERT(self && foe && owner && proj);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    self->myguy->name = "R14";

    self->set_act_type(ACT_CONTROL);
    self->stats()->hit_response(foe);
    self->set_act_type(ACT_RANDOM);

    proj->owner = owner;
    self->stats()->max_hitpoints = 100.0f;
    self->stats()->hitpoints = 1.0f;
    self->yo_delay = 0;
    self->stats()->hit_response(proj);

    self->curdir = 127;
    self->enddir = 127;
    self->stats()->right_blocked();
    self->stats()->forward_blocked();
    self->stats()->right_walk();

    self->foe = foe;
    self->setxy(0, 0);
    foe->setxy(0, 0);
    OG_ASSERT(!self->stats()->direct_walk());

    self->setxy(0, 0);
    foe->setxy(64, 0);
    (void)self->stats()->direct_walk();

    self->stats()->last_distance = 10;
    self->stats()->current_distance = 10;
    self->stats()->walk_to_foe();
}
