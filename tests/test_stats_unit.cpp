#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include <gtest/gtest.h>
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
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(64, 64);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(StatsUnit, stats_commands_and_clamps_paths)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    ASSERT_TRUE(w != nullptr);

    w->stats()->add_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_TRUE(w->stats()->delete_me() == 1);

    w->stats()->force_command(COMMAND_WALK, 1, 0, 0);
    ASSERT_TRUE(!w->stats()->commands.empty());
    ASSERT_TRUE(w->stats()->commands.front().com1 == 1);
    ASSERT_TRUE(w->stats()->commands.front().com2 == 1);

    w->stats()->commands.clear();
    ASSERT_TRUE(w->stats()->do_command() == 0);
}

TEST(StatsUnit, stats_follow_attack_and_block_queries)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_TRUE(w != nullptr);
    ASSERT_TRUE(foe != nullptr);
    w->set_foe(foe);
    foe->setxy(96, 64);

    w->stats()->force_command(COMMAND_ATTACK, 2, 0, 0);
    (void)w->stats()->do_command();

    w->set_leader(foe);
    w->set_foe(nullptr);
    w->stats()->force_command(COMMAND_FOLLOW, 2, 0, 0);
    (void)w->stats()->do_command();

    for (int d = 0; d < 8; ++d)
    {
        w->set_curdir(static_cast<char>(d));
        (void)w->stats()->right_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
        (void)w->stats()->forward_blocked();
    }
}

TEST(StatsUnit, stats_walk_helpers_and_hit_response_paths)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_TRUE(w != nullptr);
    ASSERT_TRUE(foe != nullptr);
    w->set_foe(foe);
    foe->setxy(112, 64);

    (void)w->stats()->direct_walk();
    (void)w->stats()->right_walk();
    (void)w->stats()->walk_to_foe();

    w->stats()->hit_response(foe);
    w->stats()->yell_for_help(foe);
    ASSERT_TRUE(w->yo_delay() > 0);
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
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(96, 96);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

int deterministic_path_check_counter_roll(std::uint32_t seed)
{
    StatsFixture fx;
    walker* actor = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    if (actor == nullptr || foe == nullptr)
        return 0;

    actor->setxy(96, 96);
    foe->setxy(112, 96);
    actor->set_foe(foe);
    actor->set_path_check_counter(0);
    fx.level.world().rng_.state_ = seed;
    (void)actor->stats()->walk_to_foe();
    return actor->path_check_counter();
}

} // namespace

TEST(StatsUnit, stats_r11_clear_command_and_blocked_direction_defaults)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* leader = add_living(fx, 0);
    ASSERT_TRUE(w != nullptr);
    ASSERT_TRUE(leader != nullptr);

    w->set_leader(leader);
    w->set_team_num(1);
    w->set_real_team_num(0);
    w->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    w->stats()->clear_command();
    ASSERT_TRUE(w->team_num() == 0);
    ASSERT_TRUE(w->real_team_num() == 255);
    ASSERT_TRUE(w->leader() == nullptr);

    w->set_curdir(127);
    ASSERT_TRUE(!w->stats()->right_forward_blocked());
    ASSERT_TRUE(!w->stats()->right_back_blocked());
}

TEST(StatsUnit, stats_r11_right_walk_branch_matrix)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    ASSERT_TRUE(w != nullptr);

    // Case: right_blocked true, forward open => walkstep normalization path.
    walker* blocker_right = add_living(fx, 1);
    blocker_right->setxy(97, 96); // FACE_UP right side probe
    w->set_curdir(FACE_UP);
    w->set_enddir(FACE_UP);
    w->set_lastx(2.0f);
    w->set_lasty(0.0f);
    ASSERT_TRUE(w->stats()->right_walk());

    // Case: right_blocked and forward_blocked => turn left branch.
    walker* blocker_forward = add_living(fx, 1);
    blocker_forward->setxy(96, 95);
    w->set_curdir(FACE_UP);
    w->set_enddir(FACE_UP);
    ASSERT_TRUE(w->stats()->right_walk());

    // Remove blockers so forward_blocked branch can be forced separately.
    blocker_right->set_dead(1);
    blocker_forward->set_dead(1);
    walker* blocker_forward_only = add_living(fx, 1);
    blocker_forward_only->setxy(96, 95);
    w->set_curdir(FACE_UP);
    w->set_enddir(FACE_UP);
    ASSERT_TRUE(w->stats()->right_walk());

    // right_back_blocked branch with command enqueue + direction switch table (803-838 fallback as well)
    blocker_forward_only->set_dead(1);
    walker* blocker_back = add_living(fx, 1);
    blocker_back->setxy(97, 97);
    for (int dir = 0; dir < 8; ++dir)
    {
        w->set_curdir(static_cast<char>(dir));
        w->set_enddir(static_cast<char>(dir));
        w->stats()->commands.clear();
        ASSERT_TRUE(w->stats()->right_walk());
    }

    // direct_walk()==false fallback switch, all directions (no foe, no blockers)
    blocker_back->set_dead(1);
    for (int dir = 0; dir < 8; ++dir)
    {
        w->set_curdir(static_cast<char>(dir));
        w->set_enddir(static_cast<char>(dir));
        w->set_foe(nullptr);
        ASSERT_TRUE(w->stats()->right_walk());
    }
}

TEST(StatsUnit, stats_r11_direct_walk_and_walk_to_foe_tail_branches)
{
    StatsFixture fx;
    walker* w = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_TRUE(w != nullptr && foe != nullptr);

    // direct_walk no foe early return line 861
    w->set_foe(nullptr);
    ASSERT_TRUE(!w->stats()->direct_walk());

    // walk_to_foe short-circuit with near foe and no nearby foes list => commandcount zero path
    w->set_foe(foe);
    foe->set_dead(1);
    w->stats()->force_command(COMMAND_WALK, 5, 1, 0);
    w->set_path_check_counter(0);
    fx.level.world().rng_.state_ = 1;
    ASSERT_TRUE(w->stats()->walk_to_foe());
    ASSERT_TRUE(w->stats()->commands.empty() || w->stats()->commands.front().commandcount >= 0);

    // close foe => tempdistance < 30 tail branch line 1032
    foe->set_dead(0);
    foe->setxy(100, 96);
    w->stats()->force_command(COMMAND_SEARCH, 5, 0, 0);
    w->set_path_check_counter(1);
    fx.level.world().rng_.state_ = 1;
    ASSERT_TRUE(w->stats()->walk_to_foe());
    ASSERT_TRUE(w->stats()->commands.empty() || w->stats()->commands.front().commandcount >= 0);
}

TEST(StatsUnit, stats_r11_path_check_counter_roll_is_seed_deterministic)
{
    const int first = deterministic_path_check_counter_roll(1u);
    const int second = deterministic_path_check_counter_roll(1u);
    const int third = deterministic_path_check_counter_roll(2u);

    ASSERT_EQ(11, first);
    ASSERT_EQ(first, second);
    ASSERT_EQ(12, third);
    ASSERT_NE(first, third);
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
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(96, 96);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(StatsUnit, stats_r12_command_and_hit_response_branches)
{
    StatsR12Fixture fx;
    walker* self = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_TRUE(self && foe);

    self->stats()->add_command(COMMAND_WALK, 1, 9, -9);
    ASSERT_TRUE(!self->stats()->commands.empty());
    self->stats()->force_command(COMMAND_WALK, 1, -9, 9);

    self->stats()->set_command(COMMAND_RANDOM_WALK, 1);
    self->stats()->try_command(COMMAND_RANDOM_WALK, 1);

    self->stats()->commands.clear();
    self->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    (void)self->stats()->do_command();

    self->set_foe(foe);
    foe->setxy(97, 96);
    self->stats()->force_command(COMMAND_RIGHT_WALK, 2, 0, 0);
    (void)self->stats()->do_command();

    self->stats()->force_command(COMMAND_SEARCH, 2, 0, 0);
    self->set_path_check_counter(1);
    (void)self->stats()->do_command();

    self->stats()->set_hitpoints(1.0f);
    self->stats()->set_max_hitpoints(100.0f);
    self->set_yo_delay(0);
    self->stats()->hit_response(foe);
    ASSERT_TRUE(self->yo_delay() > 0);

    self->stats()->clear_bit_flags();
    self->stats()->set_bit_flags(BIT_FLYING, 1);
    ASSERT_TRUE(self->stats()->query_bit_flags(BIT_FLYING) != 0);
    self->stats()->set_bit_flags(BIT_FLYING, 0);
}

TEST(StatsUnit, stats_r12_extra_command_switch_and_null_controller_paths)
{
    StatsR12Fixture fx;
    walker* self = add_living(fx, 0);
    walker* foe = add_living(fx, 1);
    ASSERT_TRUE(self && foe);

    // Null-controller constructor + do_command early return.
    statistics null_stats(nullptr);
    ASSERT_TRUE(null_stats.do_command() == 0);

    self->set_default_weapon(FAMILY_KNIFE);
    self->set_current_weapon(FAMILY_ARROW);

    // COMMAND_SET_WEAPON / COMMAND_RESET_WEAPON.
    self->stats()->force_command(COMMAND_SET_WEAPON, 1, FAMILY_FIREBALL, 0);
    (void)self->stats()->do_command();
    ASSERT_TRUE(self->current_weapon() == FAMILY_FIREBALL);

    self->stats()->force_command(COMMAND_RESET_WEAPON, 1, 0, 0);
    (void)self->stats()->do_command();
    ASSERT_TRUE(self->current_weapon() == self->default_weapon());

    // COMMAND_DIE debug branch.
    self->set_dead(0);
    self->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    (void)self->stats()->do_command();
    ASSERT_TRUE(self->stats()->delete_me() == 1);

    // COMMAND_FOLLOW branch with foe already set.
    self->set_foe(foe);
    self->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)self->stats()->do_command();

    // COMMAND_FOLLOW branch with no leader found (headless returns null).
    self->set_foe(nullptr);
    self->set_leader(nullptr);
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
        push_test_context(&gc);
    }

    ~StatsR14Fixture()
    {
        pop_test_context();
    }
};

walker* add_living(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

walker* add_weapon(StatsR14Fixture& fx, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Weapon, FAMILY_ARROW);
    bind_test_entity_sim_context(fx.level, w.get());
    w->set_sizex(8);
    w->set_sizey(8);
    w->set_stepsize(1.0f);
    w->setxy(x, y);
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_dead(0);
    walker* out = w.get();
    fx.level.world().weaplist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(StatsUnit, stats_r14_lines_122_133_135_155_161_add_force_walk_clamps)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    ASSERT_TRUE(self != nullptr);

    self->stats()->add_command(COMMAND_FOLLOW, 1, 0, 0);
    self->stats()->add_command(COMMAND_WALK, 1, -9, 9);
    ASSERT_TRUE(!self->stats()->commands.empty());
    auto& back = self->stats()->commands.back();
    ASSERT_TRUE(back.com1 == -1);
    ASSERT_TRUE(back.com2 == 1);

    self->stats()->force_command(COMMAND_WALK, 1, 9, -9);
    ASSERT_TRUE(!self->stats()->commands.empty());
    auto& front = self->stats()->commands.front();
    ASSERT_TRUE(front.com1 == 1);
    ASSERT_TRUE(front.com2 == -1);
}

TEST(StatsUnit, stats_r14_lines_249_255_301_313_319_344_command_switches)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 96, 96);
    walker* foe = add_living(fx, 1, 220, 96);
    walker* lead = add_living(fx, 0, 300, 96);
    ASSERT_TRUE(self && foe && lead);

    self->stats()->force_command(COMMAND_WALK, 1, 1, 0);
    (void)self->stats()->do_command();

    self->set_order_family(Order::Weapon, FAMILY_ARROW);
    self->stats()->force_command(COMMAND_FIRE, 1, 1, 0);
    (void)self->stats()->do_command();
    self->set_order_family(Order::Living, FAMILY_SOLDIER);

    self->set_leader(lead);
    self->set_foe(nullptr);
    self->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)self->stats()->do_command();

    self->set_foe(foe);
    self->set_lastx(1.0f);
    self->set_lasty(0.0f);
    self->stats()->force_command(COMMAND_QUICK_FIRE, 1, 1, 0);
    (void)self->stats()->do_command();

    self->set_foe(foe);
    self->stats()->force_command(COMMAND_ATTACK, 1, 0, 0);
    (void)self->stats()->do_command();

    self->set_foe(nullptr);
    self->stats()->force_command(COMMAND_SEARCH, 1, 0, 0);
    (void)self->stats()->do_command();
}

TEST(StatsUnit, stats_r14_lines_440_453_468_502_520_591_708_729_750_755_898_direct_and_blocked_paths)
{
    StatsR14Fixture fx;
    walker* self = add_living(fx, 0, 0, 0);
    walker* foe = add_living(fx, 1, 0, 0);
    walker* owner = add_living(fx, 1, 0, 0);
    walker* proj = add_weapon(fx, 1, 0, 0);
    ASSERT_TRUE(self && foe && owner && proj);

    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    self->myguy->name = "R14";

    self->set_act_type(ACT_CONTROL);
    self->stats()->hit_response(foe);
    self->set_act_type(ACT_RANDOM);

    proj->set_owner(owner);
    self->stats()->set_max_hitpoints(100.0f);
    self->stats()->set_hitpoints(1.0f);
    self->set_yo_delay(0);
    self->stats()->hit_response(proj);

    self->set_curdir(127);
    self->set_enddir(127);
    self->stats()->right_blocked();
    self->stats()->forward_blocked();
    self->stats()->right_walk();

    self->set_foe(foe);
    self->setxy(0, 0);
    foe->setxy(0, 0);
    ASSERT_TRUE(!self->stats()->direct_walk());

    self->setxy(0, 0);
    foe->setxy(64, 0);
    (void)self->stats()->direct_walk();

    self->stats()->set_last_distance(10);
    self->stats()->set_current_distance(10);
    self->stats()->walk_to_foe();
}
} // namespace detail_stats_r14
