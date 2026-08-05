#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(StatsCoverage, stats_constructor_null_controller_defaults_and_no_command_guard)
{
    statistics s(nullptr);
    ASSERT_TRUE(s.controller() == nullptr) << "constructor should preserve null controller";
    ASSERT_EQ((int)Order::Living, (int)s.old_order()) << "null-controller constructor should default old_order";
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)s.old_family()) << "null-controller constructor should default family";
    ASSERT_EQ(0, (int)s.do_command()) << "do_command should early-return when controller is null";
}


TEST(StatsCoverage, stats_add_and_force_command_walk_clamps_inputs)
{
    walker w;
    statistics s(&w);

    s.add_command(COMMAND_WALK, 3, 5, -5);
    ASSERT_TRUE(!s.commands.empty()) << "add_command should append command";
    ASSERT_EQ(1, (int)s.commands.back().com1) << "walk command com1 should clamp to +1";
    ASSERT_EQ(-1, (int)s.commands.back().com2) << "walk command com2 should clamp to -1";

    s.force_command(COMMAND_WALK, 2, 0, 0);
    ASSERT_EQ(1, (int)s.commands.front().com1) << "force_command should rewrite 0,0 to 1,1";
    ASSERT_EQ(1, (int)s.commands.front().com2) << "force_command should rewrite 0,0 to 1,1";
}


TEST(StatsCoverage, stats_set_and_try_command_random_walk_paths)
{
    FixedRandom rng0(0);
    GameContext c;
    c.rng = &rng0;
    push_test_context(&c);

    walker w;
    statistics s(&w);

    s.try_command(COMMAND_RANDOM_WALK, 1);
    ASSERT_TRUE(!s.commands.empty()) << "try_command random walk should enqueue walk command";
    ASSERT_EQ((int)COMMAND_WALK, (int)s.commands.back().commandtype) << "random walk should map to walk";

    s.set_command(COMMAND_RANDOM_WALK, 2);
    ASSERT_TRUE(!s.commands.empty()) << "set_command random walk should force a command";
    ASSERT_EQ((int)COMMAND_WALK, (int)s.commands.front().commandtype) << "set_command random should map to walk";

    s.add_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_EQ(1, (int)s.delete_me()) << "COMMAND_DIE add should set delete_me immediately";

    pop_test_context();
}


TEST(StatsCoverage, stats_batch2_command_edge_paths_smoke)
{
    walker w;
    statistics s(&w);

    w.set_default_weapon(FAMILY_KNIFE);
    w.set_current_weapon(FAMILY_ARROW);
    w.set_team_num(1);
    w.set_real_team_num(0);

    // clear_command branch that restores weapon/team and clears leader.
    s.clear_command();
    ASSERT_EQ((int)FAMILY_KNIFE, (int)w.current_weapon()) << "clear_command should restore default weapon";
    ASSERT_EQ(0, (int)w.team_num()) << "clear_command should restore real team";
    ASSERT_EQ(255, (int)w.real_team_num()) << "clear_command should reset real team marker";

    // Add follow command (logging branch) and walk clamping branches.
    s.add_command(COMMAND_FOLLOW, 1, 0, 0);
    s.add_command(COMMAND_WALK, 1, 9, 9);
    s.add_command(COMMAND_WALK, 1, -9, -9);
    s.force_command(COMMAND_WALK, 1, 0, 0);
    ASSERT_TRUE(!s.commands.empty()) << "commands should be enqueued";

    // set_command COMMAND_DIE logging branch.
    s.set_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_TRUE(!s.commands.empty()) << "set_command should enqueue command";
}


TEST(StatsCoverage, stats_round6_block_query_switches_all_directions)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->setxy(GRID_SIZE * 5, GRID_SIZE * 5);

    for (int dir = 0; dir < 8; dir++)
    {
        w->set_curdir(static_cast<char>(dir));
        (void)w->stats()->right_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
        (void)w->stats()->forward_blocked();
    }

    // Invalid dir defaults.
    w->set_curdir(static_cast<char>(120));
    (void)w->stats()->right_blocked();
    (void)w->stats()->right_forward_blocked();
    (void)w->stats()->right_back_blocked();
    (void)w->stats()->forward_blocked();
}


TEST(StatsCoverage, stats_round6_walk_clamp_extremes_and_empty_queue_paths)
{
    walker w;
    statistics s(&w);

    s.commands.clear();
    s.add_command(COMMAND_WALK, 1, -99, 99);
    ASSERT_TRUE(!s.commands.empty()) << "add_command should append walk command";
    if (!s.commands.empty())
    {
        ASSERT_EQ(-1, (int)s.commands.back().com1) << "add_command should clamp com1 to -1";
        ASSERT_EQ(1, (int)s.commands.back().com2) << "add_command should clamp com2 to +1";
    }

    s.force_command(COMMAND_WALK, 1, -88, 88);
    ASSERT_TRUE(!s.commands.empty()) << "force_command should prepend walk command";
    if (!s.commands.empty())
    {
        ASSERT_EQ(-1, (int)s.commands.front().com1) << "force_command should clamp com1 to -1";
        ASSERT_EQ(1, (int)s.commands.front().com2) << "force_command should clamp com2 to +1";
    }

    s.commands.clear();
    ASSERT_EQ(0, (int)s.do_command()) << "do_command should return 0 for empty queue";
}


TEST(StatsCoverage, stats_round7a_command_clamps_and_direction_switches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->setxy(GRID_SIZE * 5, GRID_SIZE * 5);

    // Explicitly hit both +/- clamp sides in add/force command.
    w->stats()->add_command(COMMAND_WALK, 1, -9, 9);
    ASSERT_TRUE(!w->stats()->commands.empty()) << "walk command added";
    if (!w->stats()->commands.empty())
    {
        const command& c = w->stats()->commands.back();
        ASSERT_EQ(-1, (int)c.com1) << "add_command should clamp x to -1";
        ASSERT_EQ(1, (int)c.com2) << "add_command should clamp y to +1";
    }
    w->stats()->force_command(COMMAND_WALK, 1, -8, 8);
    ASSERT_TRUE(!w->stats()->commands.empty()) << "forced walk command added";
    if (!w->stats()->commands.empty())
    {
        const command& c = w->stats()->commands.front();
        ASSERT_EQ(-1, (int)c.com1) << "force_command should clamp x to -1";
        ASSERT_EQ(1, (int)c.com2) << "force_command should clamp y to +1";
    }

    // set_command non-random branch.
    w->stats()->set_command(COMMAND_SET_WEAPON, 1);
    ASSERT_TRUE(!w->stats()->commands.empty()) << "set_command should enqueue non-random command";

    // Drive the direction switches using explicit FACE_* constants.
    const char dirs[] = {
        FACE_UP, FACE_UP_RIGHT, FACE_RIGHT, FACE_DOWN_RIGHT,
        FACE_DOWN, FACE_DOWN_LEFT, FACE_LEFT, FACE_UP_LEFT
    };
    for (char dir : dirs)
    {
        w->set_curdir(dir);
        (void)w->stats()->right_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
        (void)w->stats()->forward_blocked();
    }
}


TEST(StatsCoverage, stats_round7a_follow_and_die_do_command_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(actor != nullptr) << "actor created";
    if (!actor)
        return;

    actor->setxy(64, 64);

    // Follow with no eligible leader: find_follow_leader() null -> command count zero path.
    actor->stats()->clear_command();
    actor->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)actor->stats()->do_command();

    // Follow with foe set: immediate early stop path in COMMAND_FOLLOW.
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    if (foe)
    {
        foe->set_team_num(1);
        foe->setxy(96, 64);
        actor->set_foe(foe);
        actor->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
        (void)actor->stats()->do_command();
        actor->set_foe(nullptr);
    }

    // COMMAND_DIE in do_command with commandcount < 2.
    actor->set_dead(0);
    actor->stats()->set_delete_me(0);
    actor->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    (void)actor->stats()->do_command();
    ASSERT_TRUE(actor->stats()->delete_me() == 1) << "COMMAND_DIE do_command should set delete_me";
}
