#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/runtime/game_context.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>

#include "unit/unit.h"

namespace {

struct StatsFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};

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
    w->setxy(96, 96);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_stats_r11_clear_command_and_blocked_direction_defaults)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    OG_ASSERT(w != nullptr);

    w->leader = reinterpret_cast<walker*>(0x1);
    w->team_num = 1;
    w->real_team_num = 0;
    w->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    w->stats()->clear_command();
    OG_ASSERT(w->team_num == 0);
    OG_ASSERT(w->real_team_num == 255);
    OG_ASSERT(w->leader == nullptr);

    w->curdir = 127;
    OG_ASSERT(!w->stats()->right_forward_blocked());
    OG_ASSERT(!w->stats()->right_back_blocked());
}

OG_UNIT_TEST(test_stats_r11_right_walk_branch_matrix)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    OG_ASSERT(w != nullptr);

    // Case: right_blocked true, forward open => walkstep normalization path.
    walker* blocker_right = add_living(fx, 1);
    blocker_right->setxy(97, 96); // FACE_UP right side probe
    w->curdir = FACE_UP;
    w->enddir = FACE_UP;
    w->lastx = 2.0f;
    w->lasty = 0.0f;
    OG_ASSERT(w->stats()->right_walk());

    // Case: right_blocked and forward_blocked => turn left branch.
    walker* blocker_forward = add_living(fx, 1);
    blocker_forward->setxy(96, 95);
    w->curdir = FACE_UP;
    w->enddir = FACE_UP;
    OG_ASSERT(w->stats()->right_walk());

    // Remove blockers so forward_blocked branch can be forced separately.
    blocker_right->dead = 1;
    blocker_forward->dead = 1;
    walker* blocker_forward_only = add_living(fx, 1);
    blocker_forward_only->setxy(96, 95);
    w->curdir = FACE_UP;
    w->enddir = FACE_UP;
    OG_ASSERT(w->stats()->right_walk());

    // right_back_blocked branch with command enqueue + direction switch table (803-838 fallback as well)
    blocker_forward_only->dead = 1;
    walker* blocker_back = add_living(fx, 1);
    blocker_back->setxy(97, 97);
    for (int dir = 0; dir < 8; ++dir)
    {
        w->curdir = static_cast<char>(dir);
        w->enddir = static_cast<char>(dir);
        w->stats()->commands.clear();
        OG_ASSERT(w->stats()->right_walk());
    }

    // direct_walk()==false fallback switch, all directions (no foe, no blockers)
    blocker_back->dead = 1;
    for (int dir = 0; dir < 8; ++dir)
    {
        w->curdir = static_cast<char>(dir);
        w->enddir = static_cast<char>(dir);
        w->foe = nullptr;
        OG_ASSERT(w->stats()->right_walk());
    }
}

OG_UNIT_TEST(test_stats_r11_direct_walk_and_walk_to_foe_tail_branches)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    OG_ASSERT(w != nullptr && foe != nullptr);

    // direct_walk no foe early return line 861
    w->foe = nullptr;
    OG_ASSERT(!w->stats()->direct_walk());

    // walk_to_foe short-circuit with near foe and no nearby foes list => commandcount zero path
    w->foe = foe;
    foe->dead = 1;
    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    w->path_check_counter = 0;
    OG_ASSERT(w->stats()->walk_to_foe());

    // close foe => tempdistance < 30 tail branch line 1032
    foe->dead = 0;
    foe->setxy(100, 96);
    w->stats()->force_command(COMMAND_SEARCH, 5, 0, 0);
    w->path_check_counter = 1;
    OG_ASSERT(w->stats()->walk_to_foe());
    OG_ASSERT(w->stats()->commands.empty() || w->stats()->commands.front().commandcount >= 0);
}
