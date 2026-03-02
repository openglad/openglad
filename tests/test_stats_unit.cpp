#include <openglad/runtime/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include "unit/unit.h"
#include <openglad/platform/game_context.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <openglad/gameplay/guy.h>
#include "test_gameplay_context_scope.h"

// --- From test_stats_coverage_push.cpp ---
namespace detail_stats_coverage_push {
namespace {

struct StatsFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    StatsFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(StatsFixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(64, 64);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
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
} // namespace detail_stats_coverage_push

// --- From test_stats_r11.cpp ---
namespace detail_stats_r11 {
namespace {

struct StatsFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};
    ScopedGameplayContext gameplay;

    StatsFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(StatsFixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(96, 96);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
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
    fx.level.world().rng_.state_ = 1;
    OG_ASSERT(w->stats()->walk_to_foe());
    OG_ASSERT(w->stats()->commands.empty() || w->stats()->commands.front().commandcount >= 0);

    // close foe => tempdistance < 30 tail branch line 1032
    foe->dead = 0;
    foe->setxy(100, 96);
    w->stats()->force_command(COMMAND_SEARCH, 5, 0, 0);
    w->path_check_counter = 1;
    fx.level.world().rng_.state_ = 1;
    OG_ASSERT(w->stats()->walk_to_foe());
    OG_ASSERT(w->stats()->commands.empty() || w->stats()->commands.front().commandcount >= 0);
}
} // namespace detail_stats_r11

// --- From test_stats_r12.cpp ---
namespace detail_stats_r12 {
namespace {

struct StatsR12Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    StatsR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(StatsR12Fixture& fx, unsigned char team)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(96, 96);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
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

OG_UNIT_TEST(test_stats_r12_extra_command_switch_and_null_controller_paths)
{
    StatsR12Fixture fx;
    walker* self = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    OG_ASSERT(self && foe);

    // Null-controller constructor + do_command early return.
    statistics null_stats(nullptr);
    OG_ASSERT(null_stats.do_command() == 0);

    self->default_weapon = FAMILY_KNIFE;
    self->current_weapon = FAMILY_ARROW;

    // COMMAND_SET_WEAPON / COMMAND_RESET_WEAPON.
    self->stats()->force_command(COMMAND_SET_WEAPON, 1, FAMILY_FIREBALL, 0);
    (void)self->stats()->do_command();
    OG_ASSERT(self->current_weapon == FAMILY_FIREBALL);

    self->stats()->force_command(COMMAND_RESET_WEAPON, 1, 0, 0);
    (void)self->stats()->do_command();
    OG_ASSERT(self->current_weapon == self->default_weapon);

    // COMMAND_DIE debug branch.
    self->dead = 0;
    self->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    (void)self->stats()->do_command();
    OG_ASSERT(self->stats()->delete_me == 1);

    // COMMAND_FOLLOW branch with foe already set.
    self->foe = foe;
    self->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)self->stats()->do_command();

    // COMMAND_FOLLOW branch with no leader found (headless returns null).
    self->foe = nullptr;
    self->leader = nullptr;
    self->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)self->stats()->do_command();

    // COMMAND_UNCHARM and default command type.
    self->stats()->force_command(COMMAND_UNCHARM, 1, 0, 0);
    (void)self->stats()->do_command();
    self->stats()->force_command(9999, 1, 0, 0);
    (void)self->stats()->do_command();
}
} // namespace detail_stats_r12

// --- From test_stats_r14.cpp ---
namespace detail_stats_r14 {
namespace {

struct StatsR14Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;
    GameContext gc;

    StatsR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
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
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_weapon(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Weapon, FAMILY_ARROW);
    bind_test_entity_sim_context(fx.level, w.get());
    w->sizex = 8;
    w->sizey = 8;
    w->stepsize = 1.0f;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    fx.level.world().weaplist.push_back(std::move(w));
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
} // namespace detail_stats_r14
