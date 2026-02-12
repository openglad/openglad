#include "graph.h"
#include "entities/guy.h"
#include "data/gloader.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    walker* w = g.create_walker(myscreen);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// viewscreen draw operations
// ---------------------------------------------------------------------------

void test_view_redraw_smoke()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    vs->redraw();
}
REGISTER_TEST(test_view_redraw_smoke);

void test_view_display_text()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    vs->set_display_text("Test message", 30);
}
REGISTER_TEST(test_view_display_text);

void test_view_set_display_text_twice()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    vs->set_display_text("First", 30);
    vs->set_display_text("Second", 20);
}
REGISTER_TEST(test_view_set_display_text_twice);

void test_view_resize()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    Sint32 oldw = vs->endx - vs->xloc;
    vs->resize(PREF_VIEW_FULL);
    Sint32 neww = vs->endx - vs->xloc;
    (void)oldw;
    (void)neww;
    // Restore
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize);

// ---------------------------------------------------------------------------
// viewscreen with controlled walker
// ---------------------------------------------------------------------------

void test_view_with_control()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    walker* w = make_walker(FAMILY_SOLDIER);
    if (!w) return;

    vs->control = w;
    vs->redraw();
    vs->control = nullptr;

    delete w;
}
REGISTER_TEST(test_view_with_control);

void test_view_draw_with_entities()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;

    walker* w = make_walker(FAMILY_SOLDIER);
    if (!w) return;

    // Add walker to the level's oblist
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(w));

    vs->control = w;
    vs->redraw();
    vs->control = nullptr;

    // Remove without deleting (unique_ptr will handle it)
    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_view_draw_with_entities);

// ---------------------------------------------------------------------------
// compute_hp_color and compute_mp_color
// ---------------------------------------------------------------------------

unsigned char compute_hp_color(float hp, float maxhp);
unsigned char compute_mp_color(float mp, float maxmp);

void test_view_compute_hp_color_ranges()
{
    unsigned char c;
    c = compute_hp_color(100, 100);
    TEST_ASSERT(c > 0, "full HP should have a color");
    c = compute_hp_color(75, 100);
    TEST_ASSERT(c > 0, "75% HP should have a color");
    c = compute_hp_color(50, 100);
    TEST_ASSERT(c > 0, "50% HP should have a color");
    c = compute_hp_color(25, 100);
    TEST_ASSERT(c > 0, "25% HP should have a color");
    c = compute_hp_color(0, 100);
    (void)c;
}
REGISTER_TEST(test_view_compute_hp_color_ranges);

void test_view_compute_mp_color_ranges()
{
    unsigned char c;
    c = compute_mp_color(100, 100);
    TEST_ASSERT(c > 0, "full MP should have a color");
    c = compute_mp_color(75, 100);
    TEST_ASSERT(c > 0, "75% MP should have a color");
    c = compute_mp_color(50, 100);
    TEST_ASSERT(c > 0, "50% MP should have a color");
    c = compute_mp_color(25, 100);
    TEST_ASSERT(c > 0, "25% MP should have a color");
}
REGISTER_TEST(test_view_compute_mp_color_ranges);

// ---------------------------------------------------------------------------
// viewscreen speed changes
// ---------------------------------------------------------------------------

void test_view_change_speed()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    vs->change_speed(2);
    vs->change_speed(4);
    vs->change_speed(1);
}
REGISTER_TEST(test_view_change_speed);
