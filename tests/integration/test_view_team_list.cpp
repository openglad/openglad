#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// TESTING seam (view.cpp): one-shot pass budget for the compiled-out wait
// loop, driving the optional poll callback like real wait passes.
void view_team_testing_set_poll_passes(int passes);

TEST(ViewTeamList, viewscreen_view_team_renders_entries_for_my_team)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    // Ensure the level has a grid so view code doesn't depend on prior tests.
    og::runtime::current_session->myscreen_->world().create_new_grid();

    // Add a few living walkers on our team and another team.
    walker* w0 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* w1 = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ELF);
    walker* w_other = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(w0 && w1 && w_other) << "walkers created";
    if (!(w0 && w1 && w_other))
        return;

    vs->my_team = 1;
    w0->set_team_num(1);
    w1->set_team_num(1);
    w_other->set_team_num(2);

    w0->stats()->name = "A";
    w1->stats()->name = "B";
    w_other->stats()->name = "C";

    // Force a few color branches.
    vs->control = w1;                 // namecolor red for control
    w0->stats()->set_hitpoints(1);       // low hp -> LOW_HP_COLOR
    w0->stats()->set_max_hitpoints(10);
    w0->stats()->set_magicpoints(10);
    w0->stats()->set_max_magicpoints(10); // mp == max -> HIGH_MP_COLOR+3

    w1->stats()->set_hitpoints(20);       // hp > max -> ORANGE_START
    w1->stats()->set_max_hitpoints(10);
    w1->stats()->set_magicpoints(0);      // low mp -> LOW_MP_COLOR
    w1->stats()->set_max_magicpoints(10);

    vs->view_team();

    vs->control = nullptr;
}

namespace
{
int s_poll_count = 0;
bool counting_poll()
{
    // True for the first two passes, false on the third: the wait must end
    // on the first false.
    return ++s_poll_count < 3;
}
} // namespace

// The optional poll callback (design §7.2 VIEW TEAM hosting): invoked once
// per wait pass, false ends the wait. Under TESTING the blocking loop is
// compiled out; the one-shot pass seam drives the same contract.
TEST(ViewTeamList, viewscreen_view_team_poll_runs_per_pass_and_false_ends_wait)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;
    og::runtime::current_session->myscreen_->world().create_new_grid();

    // A 10-pass budget, but the callback denies on pass 3: exactly 3 polls.
    view_team_testing_set_poll_passes(10);
    s_poll_count = 0;
    vs->view_team(&counting_poll);
    EXPECT_EQ(3, s_poll_count)
        << "the wait must end on the first false return from the poll";

    // The pass budget is one-shot: a plain call afterwards never polls.
    s_poll_count = 0;
    vs->view_team(&counting_poll);
    EXPECT_EQ(0, s_poll_count) << "the TESTING pass budget must not persist";
}

