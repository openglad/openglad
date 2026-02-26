#include <openglad/gameplay/guy.h>
#include <openglad/platform/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w.release();
}

// ---------------------------------------------------------------------------
// viewscreen::redraw(LevelData*, bool) - the big grid rendering function
// ---------------------------------------------------------------------------

void test_view_redraw_with_level_data()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    bool result = vs->redraw(&og::runtime::current_session->myscreen_->world(), false);
    TEST_ASSERT(result, "redraw with level data should succeed");
}
REGISTER_TEST(test_view_redraw_with_level_data);

void test_view_redraw_with_control()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    vs->control = w;
    bool result = vs->redraw(&og::runtime::current_session->myscreen_->world(), false);
    TEST_ASSERT(result, "redraw with control should succeed");
    vs->control = nullptr;

}
REGISTER_TEST(test_view_redraw_with_control);

void test_view_redraw_no_control()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);
    og::runtime::current_session->myscreen_->level_visuals_.topx = 50;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 50;

    vs->control = nullptr;
    bool result = vs->redraw(&og::runtime::current_session->myscreen_->world(), false);
    TEST_ASSERT(result, "redraw without control uses level data pos");

    og::runtime::current_session->myscreen_->level_visuals_.topx = 0;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 0;
}
REGISTER_TEST(test_view_redraw_no_control);

void test_view_redraw_negative_pos()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    // Force negative topx/topy by positioning control near edge
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(5, 5); // near edge, topx/topy may go negative

    vs->control = w;
    vs->redraw(&og::runtime::current_session->myscreen_->world(), false);
    vs->control = nullptr;

}
REGISTER_TEST(test_view_redraw_negative_pos);

// ---------------------------------------------------------------------------
// viewscreen::draw_obs(LevelData*)
// ---------------------------------------------------------------------------

void test_view_draw_obs_with_level_data()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    vs->draw_obs(&og::runtime::current_session->myscreen_->world());
}
REGISTER_TEST(test_view_draw_obs_with_level_data);

void test_view_draw_obs_with_entities()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();

    // Add a living entity
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    if (!w) return;
    w->setxy(100, 100);

    vs->draw_obs(&og::runtime::current_session->myscreen_->world());

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}
REGISTER_TEST(test_view_draw_obs_with_entities);

// ---------------------------------------------------------------------------
// viewscreen::clear_text
// ---------------------------------------------------------------------------

void test_view_clear_text()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("Some text", 30);
    vs->clear_text();
    // Exercise the clear_text code path
}
REGISTER_TEST(test_view_clear_text);

// ---------------------------------------------------------------------------
// viewscreen::shift_text
// ---------------------------------------------------------------------------

void test_view_shift_text()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("First message", 30);
    vs->shift_text(0);
}
REGISTER_TEST(test_view_shift_text);

// ---------------------------------------------------------------------------
// viewscreen::display_text
// ---------------------------------------------------------------------------

void test_view_display_text_with_cycles()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("Display me", 5);
    vs->display_text();
    // textcycles should decrement
}
REGISTER_TEST(test_view_display_text_with_cycles);

// ---------------------------------------------------------------------------
// viewscreen::change_gamma
// ---------------------------------------------------------------------------

void test_view_change_gamma()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    Sint32 g0 = vs->change_gamma(0);
    Sint32 g1 = vs->change_gamma(1);
    Sint32 g2 = vs->change_gamma(-1);
    (void)g0; (void)g1; (void)g2;
}
REGISTER_TEST(test_view_change_gamma);
