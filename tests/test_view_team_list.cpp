#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/view.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

void test_viewscreen_view_team_renders_entries_for_my_team()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen exists");
    if (!vs)
        return;

    // Ensure the level has a grid so view code doesn't depend on prior tests.
    og::runtime::current_session->myscreen_->level_data.create_new_grid();

    // Add a few living walkers on our team and another team.
    walker* w0 = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* w1 = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_ELF);
    walker* w_other = og::runtime::current_session->myscreen_->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(w0 && w1 && w_other, "walkers created");
    if (!(w0 && w1 && w_other))
        return;

    vs->my_team = 1;
    w0->team_num = 1;
    w1->team_num = 1;
    w_other->team_num = 2;

    w0->stats()->name = "A";
    w1->stats()->name = "B";
    w_other->stats()->name = "C";

    // Force a few color branches.
    vs->control = w1;                 // namecolor red for control
    w0->stats()->hitpoints = 1;       // low hp -> LOW_HP_COLOR
    w0->stats()->max_hitpoints = 10;
    w0->stats()->magicpoints = 10;
    w0->stats()->max_magicpoints = 10; // mp == max -> HIGH_MP_COLOR+3

    w1->stats()->hitpoints = 20;       // hp > max -> ORANGE_START
    w1->stats()->max_hitpoints = 10;
    w1->stats()->magicpoints = 0;      // low mp -> LOW_MP_COLOR
    w1->stats()->max_magicpoints = 10;

    vs->view_team();

    vs->control = nullptr;
}
REGISTER_TEST(test_viewscreen_view_team_renders_entries_for_my_team);
