#include <openglad/interface/render/view.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/colors.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// compute_hp_color tests
// ---------------------------------------------------------------------------

void test_compute_hp_color_full()
{
    unsigned char c = compute_hp_color(100.0f, 100.0f);
    TEST_ASSERT_EQ((int)(HIGH_HP_COLOR+2), (int)c, "full HP should return HIGH_HP_COLOR+2");
}
REGISTER_TEST(test_compute_hp_color_full);

void test_compute_hp_color_slightly_damaged()
{
    unsigned char c = compute_hp_color(80.0f, 100.0f);
    TEST_ASSERT_EQ((int)(MAX_HP_COLOR+4), (int)c, "slightly damaged HP should return MAX_HP_COLOR+4");
}
REGISTER_TEST(test_compute_hp_color_slightly_damaged);

void test_compute_hp_color_half()
{
    unsigned char c = compute_hp_color(50.0f, 100.0f);
    TEST_ASSERT_EQ((int)(MID_HP_COLOR-3), (int)c, "half HP should return MID_HP_COLOR-3");
}
REGISTER_TEST(test_compute_hp_color_half);

void test_compute_hp_color_low()
{
    unsigned char c = compute_hp_color(20.0f, 100.0f);
    TEST_ASSERT_EQ((int)LOW_HP_COLOR, (int)c, "low HP should return LOW_HP_COLOR");
}
REGISTER_TEST(test_compute_hp_color_low);

void test_compute_hp_color_over_max()
{
    unsigned char c = compute_hp_color(150.0f, 100.0f);
    TEST_ASSERT_EQ((int)ORANGE_START, (int)c, "over-max HP should return ORANGE_START");
}
REGISTER_TEST(test_compute_hp_color_over_max);

// ---------------------------------------------------------------------------
// compute_mp_color tests
// ---------------------------------------------------------------------------

void test_compute_mp_color_full()
{
    unsigned char c = compute_mp_color(100.0f, 100.0f);
    TEST_ASSERT_EQ((int)(HIGH_MP_COLOR+3), (int)c, "full MP should return HIGH_MP_COLOR+3");
}
REGISTER_TEST(test_compute_mp_color_full);

void test_compute_mp_color_slightly_used()
{
    unsigned char c = compute_mp_color(80.0f, 100.0f);
    TEST_ASSERT_EQ((int)MAX_MP_COLOR, (int)c, "slightly used MP should return MAX_MP_COLOR");
}
REGISTER_TEST(test_compute_mp_color_slightly_used);

void test_compute_mp_color_half()
{
    unsigned char c = compute_mp_color(50.0f, 100.0f);
    TEST_ASSERT_EQ((int)MID_MP_COLOR, (int)c, "half MP should return MID_MP_COLOR");
}
REGISTER_TEST(test_compute_mp_color_half);

void test_compute_mp_color_low()
{
    unsigned char c = compute_mp_color(20.0f, 100.0f);
    TEST_ASSERT_EQ((int)LOW_MP_COLOR, (int)c, "low MP should return LOW_MP_COLOR");
}
REGISTER_TEST(test_compute_mp_color_low);

void test_compute_mp_color_over_max()
{
    unsigned char c = compute_mp_color(150.0f, 100.0f);
    TEST_ASSERT_EQ((int)WATER_START, (int)c, "over-max MP should return WATER_START");
}
REGISTER_TEST(test_compute_mp_color_over_max);

// ---------------------------------------------------------------------------
// viewscreen text management tests
// ---------------------------------------------------------------------------

void test_viewscreen_set_display_text()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen 0 should exist");

    // Clear first
    vs->clear_text();

    vs->set_display_text("Hello", 10);
    TEST_ASSERT(!vs->textlist[0].empty(), "textlist[0] should not be empty after set_display_text");
    TEST_ASSERT_EQ(10, (int)vs->textcycles[0], "textcycles[0] should be 10");
}
REGISTER_TEST(test_viewscreen_set_display_text);

void test_viewscreen_clear_text()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen 0 should exist");

    vs->set_display_text("Test", 20);
    vs->clear_text();

    for (int i = 0; i < MAX_MESSAGES; i++) {
        TEST_ASSERT(vs->textlist[i].empty(), "all text should be cleared");
    }
}
REGISTER_TEST(test_viewscreen_clear_text);

void test_viewscreen_set_display_text_multiple()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen 0 should exist");

    vs->clear_text();
    vs->set_display_text("Line 1", 10);
    vs->set_display_text("Line 2", 15);
    vs->set_display_text("Line 3", 20);

    TEST_ASSERT(!vs->textlist[0].empty(), "slot 0 should have text");
    TEST_ASSERT(!vs->textlist[1].empty(), "slot 1 should have text");
    TEST_ASSERT(!vs->textlist[2].empty(), "slot 2 should have text");
}
REGISTER_TEST(test_viewscreen_set_display_text_multiple);

void test_viewscreen_set_display_text_overflow()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen 0 should exist");

    vs->clear_text();
    // Fill all slots plus one more
    for (int i = 0; i < MAX_MESSAGES + 1; i++) {
        vs->set_display_text("Msg", 10);
    }
    // Should have shifted text up, last slot should have the overflow message
    TEST_ASSERT(!vs->textlist[MAX_MESSAGES-1].empty(), "last slot should have overflow text");
}
REGISTER_TEST(test_viewscreen_set_display_text_overflow);

void test_viewscreen_shift_text()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen 0 should exist");

    vs->clear_text();
    vs->set_display_text("First", 10);
    vs->set_display_text("Second", 15);
    vs->set_display_text("Third", 20);

    vs->shift_text(0);
    // After shifting from 0, "Second" should now be in slot 0
    TEST_ASSERT_EQ(15, (int)vs->textcycles[0], "shift should move second to first");
}
REGISTER_TEST(test_viewscreen_shift_text);

// ---------------------------------------------------------------------------
// viewscreen change_speed tests
// ---------------------------------------------------------------------------

void test_viewscreen_change_speed_report()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewscreen 0 should exist");

    Sint32 speed = vs->change_speed(0);
    TEST_ASSERT(speed >= 1 && speed <= 11, "speed should be in valid range");
}
REGISTER_TEST(test_viewscreen_change_speed_report);

void test_viewscreen_change_speed_increase()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    Sint32 speed1 = vs->change_speed(0);
    Sint32 speed2 = vs->change_speed(1);
    TEST_ASSERT(speed2 >= speed1, "increasing speed should give >= current speed");
    // Reset by decreasing
    vs->change_speed(-1);
}
REGISTER_TEST(test_viewscreen_change_speed_increase);

// ---------------------------------------------------------------------------
// viewscreen resize tests (covers the large resize function)
// ---------------------------------------------------------------------------

void test_viewscreen_resize_full()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_FULL);
    // Full screen mode for 1 player
    TEST_ASSERT(vs->xview > 200, "full view width should be > 200");
    TEST_ASSERT(vs->yview > 150, "full view height should be > 150");

    og::runtime::current_session->myscreen_->numviews = old_numviews;
}
REGISTER_TEST(test_viewscreen_resize_full);

void test_viewscreen_resize_panels()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT_EQ(232, (int)vs->xview, "panels mode width should be 232");
    TEST_ASSERT_EQ(176, (int)vs->yview, "panels mode height should be 176");

    // Restore
    vs->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->numviews = old_numviews;
}
REGISTER_TEST(test_viewscreen_resize_panels);

void test_viewscreen_resize_small_modes()
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_1);
    TEST_ASSERT_EQ(192, (int)vs->xview, "mode 1 width should be 192");

    vs->resize(PREF_VIEW_2);
    TEST_ASSERT_EQ(148, (int)vs->xview, "mode 2 width should be 148");

    vs->resize(PREF_VIEW_3);
    TEST_ASSERT_EQ(108, (int)vs->xview, "mode 3 width should be 108");

    // Restore
    vs->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->numviews = old_numviews;
}
REGISTER_TEST(test_viewscreen_resize_small_modes);
