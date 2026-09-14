#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/render/view.h>
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


TEST(StatsCoverage, stats_round6_walk_clamp_extremes_and_empty_queue_paths)
{
    walker w;
    statistics s(&w);

    s.commands.clear();
    s.add_command(COMMAND_WALK, 1, -99, 99);
    ASSERT_FALSE(s.commands.empty()) << "add_command should append walk command";
    ASSERT_EQ(-1, (int)s.commands.back().com1) << "add_command should clamp com1 to -1";
    ASSERT_EQ(1, (int)s.commands.back().com2) << "add_command should clamp com2 to +1";

    s.force_command(COMMAND_WALK, 1, -88, 88);
    ASSERT_FALSE(s.commands.empty()) << "force_command should prepend walk command";
    ASSERT_EQ(-1, (int)s.commands.front().com1) << "force_command should clamp com1 to -1";
    ASSERT_EQ(1, (int)s.commands.front().com2) << "force_command should clamp com2 to +1";

    // set_command's non-random branch forwards the command type unchanged
    // (the random-walk branch would have rewritten it to COMMAND_WALK).
    s.commands.clear();
    s.set_command(COMMAND_SET_WEAPON, 1);
    ASSERT_FALSE(s.commands.empty()) << "set_command should enqueue a non-random command";
    ASSERT_EQ((int)COMMAND_SET_WEAPON, (int)s.commands.front().commandtype)
        << "set_command must keep a non-random command's type, not rewrite it to WALK";

    s.commands.clear();
    ASSERT_EQ(0, (int)s.do_command()) << "do_command should return 0 for empty queue";
}


// COMMAND_FOLLOW has two give-up arms and they are NOT interchangeable: with a
// foe in hand the follower drops the leader and reports 0, and with no leader
// available at all it also zeroes the count and reports 0. Both used to be
// `(void)do_command()`. COMMAND_DIE's delete_me tail rounds the test out.
TEST(StatsCoverage, follow_gives_up_for_a_foe_or_no_leader_and_die_marks_delete_me)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    walker* actor = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, actor) << "actor created";
    actor->setxy(64, 64);

    // With every view control cleared, find_follow_leader() has nothing to
    // return: the command count is zeroed, the queue drains, result is 0.
    screen* const scr = og::runtime::current_session->myscreen_;
    const short saved_numviews = scr->numviews;
    walker* const saved_control = scr->viewob[0] ? scr->viewob[0]->control : nullptr;
    ASSERT_NE(nullptr, scr->viewob[0]) << "view 0 must exist to steer find_follow_leader";
    scr->viewob[0]->control = nullptr;
    scr->numviews = 1;

    actor->stats()->clear_command();
    actor->set_foe(nullptr);
    actor->set_leader(nullptr);
    actor->stats()->force_command(COMMAND_FOLLOW, 3, 0, 0);
    ASSERT_EQ(0, (int)actor->stats()->do_command())
        << "COMMAND_FOLLOW with no leader available must report failure";
    ASSERT_EQ(nullptr, actor->leader()) << "no leader was adopted";
    ASSERT_TRUE(actor->stats()->commands.empty())
        << "the no-leader arm zeroes commandcount, so the command is popped even though it asked for 3 rounds";

    // With a foe the follower refuses to follow at all: leader dropped,
    // count zeroed, result 0 -- even when a leader IS available.
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(1);
    foe->setxy(96, 64);

    walker* leader = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, leader) << "leader created";
    leader->setxy(400, 400);
    scr->viewob[0]->control = leader;

    actor->stats()->clear_command();  // note: this clears the leader too
    actor->set_foe(foe);
    actor->set_leader(leader);
    ASSERT_EQ(leader, actor->leader()) << "the foe arm starts WITH a leader in hand";
    actor->stats()->force_command(COMMAND_FOLLOW, 3, 0, 0);
    ASSERT_EQ(0, (int)actor->stats()->do_command())
        << "COMMAND_FOLLOW with a foe in hand must report failure";
    ASSERT_EQ(nullptr, actor->leader()) << "the foe arm drops the leader";
    ASSERT_TRUE(actor->stats()->commands.empty())
        << "the foe arm zeroes commandcount, so the command is popped";

    actor->set_foe(nullptr);
    scr->viewob[0]->control = saved_control;
    scr->numviews = saved_numviews;

    // COMMAND_DIE with commandcount < 2 marks the walker for deletion.
    actor->set_dead(0);
    actor->stats()->set_delete_me(0);
    actor->stats()->clear_command();
    actor->stats()->force_command(COMMAND_DIE, 1, 0, 0);
    ASSERT_EQ(1, (int)actor->stats()->do_command()) << "COMMAND_DIE reports success";
    ASSERT_EQ(1, (int)actor->stats()->delete_me()) << "COMMAND_DIE do_command should set delete_me";

    // ... and a COMMAND_DIE with more rounds left does NOT, yet.
    actor->stats()->set_delete_me(0);
    actor->stats()->clear_command();
    actor->stats()->force_command(COMMAND_DIE, 5, 0, 0);
    ASSERT_EQ(1, (int)actor->stats()->do_command()) << "COMMAND_DIE reports success";
    ASSERT_EQ(0, (int)actor->stats()->delete_me())
        << "COMMAND_DIE only deletes on its LAST round (commandcount < 2)";
}
