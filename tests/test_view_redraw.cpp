#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

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
// viewscreen::redraw(LevelRuntimeData*, bool) - the big grid rendering function
// ---------------------------------------------------------------------------

TEST(ViewRedraw, with_level_data)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    bool result = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    ASSERT_TRUE(result) << "redraw with level data should succeed";
}


TEST(ViewRedraw, with_control)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    vs->control = w;
    bool result = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    ASSERT_TRUE(result) << "redraw with control should succeed";
    vs->control = nullptr;

}


TEST(ViewRedraw, no_control)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);
    og::runtime::current_session->myscreen_->level_visuals_.topx = 50;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 50;

    vs->control = nullptr;
    bool result = vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    ASSERT_TRUE(result) << "redraw without control uses level data pos";

    og::runtime::current_session->myscreen_->level_visuals_.topx = 0;
    og::runtime::current_session->myscreen_->level_visuals_.topy = 0;
}


TEST(ViewRedraw, negative_pos)
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
    vs->redraw(&og::runtime::current_session->myscreen_->level_runtime_data(), false);
    vs->control = nullptr;

}


// ---------------------------------------------------------------------------
// viewscreen::draw_obs(LevelRuntimeData*)
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_draw_obs_with_level_data)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    vs->draw_obs(&og::runtime::current_session->myscreen_->level_runtime_data());
}


TEST(ViewRedraw, view_draw_obs_with_entities)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    og::runtime::current_session->myscreen_->world().create_new_grid();

    // Add a living entity
    walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    if (!w) return;
    w->setxy(100, 100);

    vs->draw_obs(&og::runtime::current_session->myscreen_->level_runtime_data());

    og::runtime::current_session->myscreen_->world().remove_ob(w);
}


// ---------------------------------------------------------------------------
// viewscreen::clear_text
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_clear_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("Some text", 30);
    vs->clear_text();
    // Exercise the clear_text code path
}


// ---------------------------------------------------------------------------
// viewscreen::shift_text
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_shift_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("First message", 30);
    vs->shift_text(0);
}


// ---------------------------------------------------------------------------
// viewscreen::display_text
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_display_text_with_cycles)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    vs->set_display_text("Display me", 5);
    vs->display_text();
    // textcycles should decrement
}


// ---------------------------------------------------------------------------
// viewscreen::change_gamma
// ---------------------------------------------------------------------------

TEST(ViewRedraw, view_change_gamma)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;

    Sint32 g0 = vs->change_gamma(0);
    Sint32 g1 = vs->change_gamma(1);
    Sint32 g2 = vs->change_gamma(-1);
    (void)g0; (void)g1; (void)g2;
}

