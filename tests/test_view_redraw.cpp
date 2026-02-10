#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    walker* w = g.create_walker(myscreen);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// viewscreen::redraw(LevelData*, bool) - the big grid rendering function
// ---------------------------------------------------------------------------

void test_view_redraw_with_level_data()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    myscreen->level_data.create_new_grid();
    myscreen->level_data.mysmoother.set_target(myscreen->level_data.grid);

    bool result = vs->redraw(&myscreen->level_data, false);
    TEST_ASSERT(result, "redraw with level data should succeed");
}
REGISTER_TEST(test_view_redraw_with_level_data);

void test_view_redraw_with_control()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    myscreen->level_data.create_new_grid();
    myscreen->level_data.mysmoother.set_target(myscreen->level_data.grid);

    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    vs->control = w;
    bool result = vs->redraw(&myscreen->level_data, false);
    TEST_ASSERT(result, "redraw with control should succeed");
    vs->control = nullptr;

    delete w;
}
REGISTER_TEST(test_view_redraw_with_control);

void test_view_redraw_no_control()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    myscreen->level_data.create_new_grid();
    myscreen->level_data.mysmoother.set_target(myscreen->level_data.grid);
    myscreen->level_data.topx = 50;
    myscreen->level_data.topy = 50;

    vs->control = nullptr;
    bool result = vs->redraw(&myscreen->level_data, false);
    TEST_ASSERT(result, "redraw without control uses level data pos");

    myscreen->level_data.topx = 0;
    myscreen->level_data.topy = 0;
}
REGISTER_TEST(test_view_redraw_no_control);

void test_view_redraw_negative_pos()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    myscreen->level_data.create_new_grid();
    myscreen->level_data.mysmoother.set_target(myscreen->level_data.grid);

    // Force negative topx/topy by positioning control near edge
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(5, 5); // near edge, topx/topy may go negative

    vs->control = w;
    vs->redraw(&myscreen->level_data, false);
    vs->control = nullptr;

    delete w;
}
REGISTER_TEST(test_view_redraw_negative_pos);

// ---------------------------------------------------------------------------
// viewscreen::draw_obs(LevelData*)
// ---------------------------------------------------------------------------

void test_view_draw_obs_with_level_data()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    myscreen->level_data.create_new_grid();
    vs->draw_obs(&myscreen->level_data);
}
REGISTER_TEST(test_view_draw_obs_with_level_data);

void test_view_draw_obs_with_entities()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    myscreen->level_data.create_new_grid();

    // Add a living entity
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    if (!w) return;
    w->setxy(100, 100);

    vs->draw_obs(&myscreen->level_data);

    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_view_draw_obs_with_entities);

// ---------------------------------------------------------------------------
// viewscreen::clear_text
// ---------------------------------------------------------------------------

void test_view_clear_text()
{
    viewscreen* vs = myscreen->viewob[0].get();
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
    viewscreen* vs = myscreen->viewob[0].get();
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
    viewscreen* vs = myscreen->viewob[0].get();
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
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    Sint32 g0 = vs->change_gamma(0);
    Sint32 g1 = vs->change_gamma(1);
    Sint32 g2 = vs->change_gamma(-1);
    (void)g0; (void)g1; (void)g2;
}
REGISTER_TEST(test_view_change_gamma);
