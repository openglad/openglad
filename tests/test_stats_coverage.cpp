#include <openglad/core/stats.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

void test_stats_constructor_null_controller_defaults_and_no_command_guard()
{
    statistics s(nullptr);
    TEST_ASSERT(s.controller == nullptr, "constructor should preserve null controller");
    TEST_ASSERT_EQ((int)Order::Living, (int)s.old_order, "null-controller constructor should default old_order");
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)s.old_family, "null-controller constructor should default family");
    TEST_ASSERT_EQ(0, (int)s.do_command(), "do_command should early-return when controller is null");
}
REGISTER_TEST(test_stats_constructor_null_controller_defaults_and_no_command_guard);

void test_stats_add_and_force_command_walk_clamps_inputs()
{
    walker w;
    statistics s(&w);

    s.add_command(COMMAND_WALK, 3, 5, -5);
    TEST_ASSERT(!s.commands.empty(), "add_command should append command");
    TEST_ASSERT_EQ(1, (int)s.commands.back().com1, "walk command com1 should clamp to +1");
    TEST_ASSERT_EQ(-1, (int)s.commands.back().com2, "walk command com2 should clamp to -1");

    s.force_command(COMMAND_WALK, 2, 0, 0);
    TEST_ASSERT_EQ(1, (int)s.commands.front().com1, "force_command should rewrite 0,0 to 1,1");
    TEST_ASSERT_EQ(1, (int)s.commands.front().com2, "force_command should rewrite 0,0 to 1,1");
}
REGISTER_TEST(test_stats_add_and_force_command_walk_clamps_inputs);

void test_stats_set_and_try_command_random_walk_paths()
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    set_global_context(&c);

    walker w;
    statistics s(&w);

    s.try_command(COMMAND_RANDOM_WALK, 1);
    TEST_ASSERT(!s.commands.empty(), "try_command random walk should enqueue walk command");
    TEST_ASSERT_EQ((int)COMMAND_WALK, (int)s.commands.back().commandtype, "random walk should map to walk");

    s.set_command(COMMAND_RANDOM_WALK, 2);
    TEST_ASSERT(!s.commands.empty(), "set_command random walk should force a command");
    TEST_ASSERT_EQ((int)COMMAND_WALK, (int)s.commands.front().commandtype, "set_command random should map to walk");

    s.add_command(COMMAND_DIE, 1, 0, 0);
    TEST_ASSERT_EQ(1, (int)s.delete_me, "COMMAND_DIE add should set delete_me immediately");

    set_global_context(nullptr);
}
REGISTER_TEST(test_stats_set_and_try_command_random_walk_paths);

void test_stats_batch2_command_edge_paths_smoke()
{
    walker w;
    statistics s(&w);

    w.default_weapon = FAMILY_KNIFE;
    w.current_weapon = FAMILY_ARROW;
    w.team_num = 1;
    w.real_team_num = 0;

    // clear_command branch that restores weapon/team and clears leader.
    s.clear_command();
    TEST_ASSERT_EQ((int)FAMILY_KNIFE, (int)w.current_weapon, "clear_command should restore default weapon");
    TEST_ASSERT_EQ(0, (int)w.team_num, "clear_command should restore real team");
    TEST_ASSERT_EQ(255, (int)w.real_team_num, "clear_command should reset real team marker");

    // Add follow command (logging branch) and walk clamping branches.
    s.add_command(COMMAND_FOLLOW, 1, 0, 0);
    s.add_command(COMMAND_WALK, 1, 9, 9);
    s.add_command(COMMAND_WALK, 1, -9, -9);
    s.force_command(COMMAND_WALK, 1, 0, 0);
    TEST_ASSERT(!s.commands.empty(), "commands should be enqueued");

    // set_command COMMAND_DIE logging branch.
    s.set_command(COMMAND_DIE, 1, 0, 0);
    TEST_ASSERT(!s.commands.empty(), "set_command should enqueue command");
}
REGISTER_TEST(test_stats_batch2_command_edge_paths_smoke);

void test_stats_round6_block_query_switches_all_directions()
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->setxy(GRID_SIZE * 5, GRID_SIZE * 5);

    for (int dir = 0; dir < 8; dir++)
    {
        w->curdir = static_cast<char>(dir);
        (void)w->stats()->right_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
        (void)w->stats()->forward_blocked();
    }

    // Invalid dir defaults.
    w->curdir = static_cast<char>(120);
    (void)w->stats()->right_blocked();
    (void)w->stats()->right_forward_blocked();
    (void)w->stats()->right_back_blocked();
    (void)w->stats()->forward_blocked();
}
REGISTER_TEST(test_stats_round6_block_query_switches_all_directions);

void test_stats_round6_walk_clamp_extremes_and_empty_queue_paths()
{
    walker w;
    statistics s(&w);

    s.commands.clear();
    s.add_command(COMMAND_WALK, 1, -99, 99);
    TEST_ASSERT(!s.commands.empty(), "add_command should append walk command");
    if (!s.commands.empty())
    {
        TEST_ASSERT_EQ(-1, (int)s.commands.back().com1, "add_command should clamp com1 to -1");
        TEST_ASSERT_EQ(1, (int)s.commands.back().com2, "add_command should clamp com2 to +1");
    }

    s.force_command(COMMAND_WALK, 1, -88, 88);
    TEST_ASSERT(!s.commands.empty(), "force_command should prepend walk command");
    if (!s.commands.empty())
    {
        TEST_ASSERT_EQ(-1, (int)s.commands.front().com1, "force_command should clamp com1 to -1");
        TEST_ASSERT_EQ(1, (int)s.commands.front().com2, "force_command should clamp com2 to +1");
    }

    s.commands.clear();
    TEST_ASSERT_EQ(0, (int)s.do_command(), "do_command should return 0 for empty queue");
}
REGISTER_TEST(test_stats_round6_walk_clamp_extremes_and_empty_queue_paths);

void test_stats_round7a_command_clamps_and_direction_switches()
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    w->setxy(GRID_SIZE * 5, GRID_SIZE * 5);

    // Explicitly hit both +/- clamp sides in add/force command.
    w->stats()->add_command(COMMAND_WALK, 1, -9, 9);
    TEST_ASSERT(!w->stats()->commands.empty(), "walk command added");
    if (!w->stats()->commands.empty())
    {
        const command& c = w->stats()->commands.back();
        TEST_ASSERT_EQ(-1, (int)c.com1, "add_command should clamp x to -1");
        TEST_ASSERT_EQ(1, (int)c.com2, "add_command should clamp y to +1");
    }
    w->stats()->force_command(COMMAND_WALK, 1, -8, 8);
    TEST_ASSERT(!w->stats()->commands.empty(), "forced walk command added");
    if (!w->stats()->commands.empty())
    {
        const command& c = w->stats()->commands.front();
        TEST_ASSERT_EQ(-1, (int)c.com1, "force_command should clamp x to -1");
        TEST_ASSERT_EQ(1, (int)c.com2, "force_command should clamp y to +1");
    }

    // set_command non-random branch.
    w->stats()->set_command(COMMAND_SET_WEAPON, 1);
    TEST_ASSERT(!w->stats()->commands.empty(), "set_command should enqueue non-random command");

    // Drive the direction switches using explicit FACE_* constants.
    const char dirs[] = {
        FACE_UP, FACE_UP_RIGHT, FACE_RIGHT, FACE_DOWN_RIGHT,
        FACE_DOWN, FACE_DOWN_LEFT, FACE_LEFT, FACE_UP_LEFT
    };
    for (char dir : dirs)
    {
        w->curdir = dir;
        (void)w->stats()->right_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
        (void)w->stats()->forward_blocked();
    }
}
REGISTER_TEST(test_stats_round7a_command_clamps_and_direction_switches);

void test_stats_round7a_follow_and_die_do_command_paths()
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(actor != nullptr, "actor created");
    if (!actor)
        return;

    actor->setxy(64, 64);

    // Follow with no eligible leader: find_follow_leader() null -> command count zero path.
    actor->stats()->clear_command();
    actor->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)actor->stats()->do_command();

    // Follow with foe set: immediate early stop path in COMMAND_FOLLOW.
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(foe != nullptr, "foe created");
    if (foe)
    {
        foe->team_num = 1;
        foe->setxy(96, 64);
        actor->foe = foe;
        actor->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
        (void)actor->stats()->do_command();
        actor->foe = nullptr;
    }

    // COMMAND_DIE in do_command with commandcount < 2.
    actor->dead = 0;
    actor->stats()->delete_me = 0;
    actor->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    (void)actor->stats()->do_command();
    TEST_ASSERT(actor->stats()->delete_me == 1, "COMMAND_DIE do_command should set delete_me");
}
REGISTER_TEST(test_stats_round7a_follow_and_die_do_command_paths);
