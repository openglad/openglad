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

struct StatsFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    StatsFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(StatsFixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(64, 64);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_stats_commands_and_clamps_paths)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    OG_ASSERT(w != nullptr);

    w->stats()->add_command(COMMAND_DIE, 1, 0, 0);
    OG_ASSERT(w->stats()->delete_me == 1);

    w->stats()->force_command(COMMAND_WALK, 1, 0, 0);
    OG_ASSERT(!w->stats()->commands.empty());
    OG_ASSERT(w->stats()->commands.front().com1 == 1);
    OG_ASSERT(w->stats()->commands.front().com2 == 1);

    w->stats()->commands.clear();
    OG_ASSERT(w->stats()->do_command() == 0);
}

OG_UNIT_TEST(test_stats_follow_attack_and_block_queries)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    OG_ASSERT(w != nullptr);
    OG_ASSERT(foe != nullptr);
    w->foe = foe;
    foe->setxy(96, 64);

    w->stats()->force_command(COMMAND_ATTACK, 2, 0, 0);
    (void)w->stats()->do_command();

    w->leader = foe;
    w->foe = nullptr;
    w->stats()->force_command(COMMAND_FOLLOW, 2, 0, 0);
    (void)w->stats()->do_command();

    for (int d = 0; d < 8; ++d)
    {
        w->curdir = static_cast<char>(d);
        (void)w->stats()->right_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
        (void)w->stats()->forward_blocked();
    }
}

OG_UNIT_TEST(test_stats_walk_helpers_and_hit_response_paths)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    OG_ASSERT(w != nullptr);
    OG_ASSERT(foe != nullptr);
    w->foe = foe;
    foe->setxy(112, 64);

    (void)w->stats()->direct_walk();
    (void)w->stats()->right_walk();
    (void)w->stats()->walk_to_foe();

    w->stats()->hit_response(foe);
    w->stats()->yell_for_help(foe);
    OG_ASSERT(w->yo_delay > 0);
}
