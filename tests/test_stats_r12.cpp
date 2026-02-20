#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

namespace {

struct StatsR12Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    StatsR12Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(StatsR12Fixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(96, 96);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_stats_r12_command_and_hit_response_branches)
{
    StatsR12Fixture fx;
    walker* self = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    OG_ASSERT(self && foe);

    self->stats()->add_command(COMMAND_WALK, 1, 9, -9);
    OG_ASSERT(!self->stats()->commands.empty());
    self->stats()->force_command(COMMAND_WALK, 1, -9, 9);

    self->stats()->set_command(COMMAND_RANDOM_WALK, 1);
    self->stats()->try_command(COMMAND_RANDOM_WALK, 1);

    self->stats()->commands.clear();
    self->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    (void)self->stats()->do_command();

    self->foe = foe;
    foe->setxy(97, 96);
    self->stats()->force_command(COMMAND_RIGHT_WALK, 2, 0, 0);
    (void)self->stats()->do_command();

    self->stats()->force_command(COMMAND_SEARCH, 2, 0, 0);
    self->path_check_counter = 1;
    (void)self->stats()->do_command();

    self->stats()->hitpoints = 1.0f;
    self->stats()->max_hitpoints = 100.0f;
    self->yo_delay = 0;
    self->stats()->hit_response(foe);
    OG_ASSERT(self->yo_delay > 0);

    self->stats()->clear_bit_flags();
    self->stats()->set_bit_flags(BIT_FLYING, 1);
    OG_ASSERT(self->stats()->query_bit_flags(BIT_FLYING) != 0);
    self->stats()->set_bit_flags(BIT_FLYING, 0);
}

