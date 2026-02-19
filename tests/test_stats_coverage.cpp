#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/game_context.h>
#include "test_framework.h"

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
