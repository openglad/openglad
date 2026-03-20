#include <openglad/gameplay/input_state.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/colors.h>
#include <SDL.h>
#include <gtest/gtest.h>

#include "test_input_helpers.h"

// myscreen is now a macro defined in base.h (via game_session.h)

namespace {

struct SpeedWarningDialogState {
    bool started = false;
    bool finished = false;
};

int speed_warning_dialog_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<SpeedWarningDialogState*>(data);
    state->started = true;
    SDL_Delay(50);
    inject_key_press(SDLK_SPACE, 10);
    state->finished = true;
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// compute_hp_color tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, compute_hp_color_full)
{
    unsigned char c = compute_hp_color(100.0f, 100.0f);
    ASSERT_EQ((int)(HIGH_HP_COLOR+2), (int)c) << "full HP should return HIGH_HP_COLOR+2";
}


TEST(ViewFuncs, compute_hp_color_slightly_damaged)
{
    unsigned char c = compute_hp_color(80.0f, 100.0f);
    ASSERT_EQ((int)(MAX_HP_COLOR+4), (int)c) << "slightly damaged HP should return MAX_HP_COLOR+4";
}


TEST(ViewFuncs, compute_hp_color_half)
{
    unsigned char c = compute_hp_color(50.0f, 100.0f);
    ASSERT_EQ((int)(MID_HP_COLOR-3), (int)c) << "half HP should return MID_HP_COLOR-3";
}


TEST(ViewFuncs, compute_hp_color_low)
{
    unsigned char c = compute_hp_color(20.0f, 100.0f);
    ASSERT_EQ((int)LOW_HP_COLOR, (int)c) << "low HP should return LOW_HP_COLOR";
}


TEST(ViewFuncs, compute_hp_color_over_max)
{
    unsigned char c = compute_hp_color(150.0f, 100.0f);
    ASSERT_EQ((int)ORANGE_START, (int)c) << "over-max HP should return ORANGE_START";
}


// ---------------------------------------------------------------------------
// compute_mp_color tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, compute_mp_color_full)
{
    unsigned char c = compute_mp_color(100.0f, 100.0f);
    ASSERT_EQ((int)(HIGH_MP_COLOR+3), (int)c) << "full MP should return HIGH_MP_COLOR+3";
}


TEST(ViewFuncs, compute_mp_color_slightly_used)
{
    unsigned char c = compute_mp_color(80.0f, 100.0f);
    ASSERT_EQ((int)MAX_MP_COLOR, (int)c) << "slightly used MP should return MAX_MP_COLOR";
}


TEST(ViewFuncs, compute_mp_color_half)
{
    unsigned char c = compute_mp_color(50.0f, 100.0f);
    ASSERT_EQ((int)MID_MP_COLOR, (int)c) << "half MP should return MID_MP_COLOR";
}


TEST(ViewFuncs, compute_mp_color_low)
{
    unsigned char c = compute_mp_color(20.0f, 100.0f);
    ASSERT_EQ((int)LOW_MP_COLOR, (int)c) << "low MP should return LOW_MP_COLOR";
}


TEST(ViewFuncs, compute_mp_color_over_max)
{
    unsigned char c = compute_mp_color(150.0f, 100.0f);
    ASSERT_EQ((int)WATER_START, (int)c) << "over-max MP should return WATER_START";
}


// ---------------------------------------------------------------------------
// viewscreen text management tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, viewscreen_set_display_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    // Clear first
    vs->clear_text();

    vs->set_display_text("Hello", 10);
    ASSERT_TRUE(!vs->textlist[0].empty()) << "textlist[0] should not be empty after set_display_text";
    ASSERT_EQ(10, (int)vs->textcycles[0]) << "textcycles[0] should be 10";
}


TEST(ViewFuncs, viewscreen_clear_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->set_display_text("Test", 20);
    vs->clear_text();

    for (int i = 0; i < MAX_MESSAGES; i++) {
        ASSERT_TRUE(vs->textlist[i].empty()) << "all text should be cleared";
    }
}


TEST(ViewFuncs, viewscreen_set_display_text_multiple)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->clear_text();
    vs->set_display_text("Line 1", 10);
    vs->set_display_text("Line 2", 15);
    vs->set_display_text("Line 3", 20);

    ASSERT_TRUE(!vs->textlist[0].empty()) << "slot 0 should have text";
    ASSERT_TRUE(!vs->textlist[1].empty()) << "slot 1 should have text";
    ASSERT_TRUE(!vs->textlist[2].empty()) << "slot 2 should have text";
}


TEST(ViewFuncs, viewscreen_set_display_text_overflow)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->clear_text();
    // Fill all slots plus one more with distinct messages so the overflow
    // path is exercised rather than same-text refresh coalescing.
    for (int i = 0; i < MAX_MESSAGES + 1; i++) {
        vs->set_display_text("Msg " + std::to_string(i), 10);
    }
    // Should have shifted text up, last slot should have the overflow message
    ASSERT_TRUE(!vs->textlist[MAX_MESSAGES-1].empty()) << "last slot should have overflow text";
}


TEST(ViewFuncs, viewscreen_shift_text)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    vs->clear_text();
    vs->set_display_text("First", 10);
    vs->set_display_text("Second", 15);
    vs->set_display_text("Third", 20);

    vs->shift_text(0);
    // After shifting from 0, "Second" should now be in slot 0
    ASSERT_EQ(15, (int)vs->textcycles[0]) << "shift should move second to first";
}


// ---------------------------------------------------------------------------
// viewscreen change_speed tests
// ---------------------------------------------------------------------------

TEST(ViewFuncs, viewscreen_change_speed_report)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    Sint32 speed = vs->change_speed(0);
    ASSERT_TRUE(speed >= 1 && speed <= 11) << "speed should be in valid range";
}


TEST(ViewFuncs, viewscreen_change_speed_increase)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    Sint32 speed1 = vs->change_speed(0);
    Sint32 speed2 = vs->change_speed(1);
    ASSERT_TRUE(speed2 >= speed1) << "increasing speed should give >= current speed";
    // Reset by decreasing
    vs->change_speed(-1);
}

TEST(ViewFuncs, viewscreen_change_speed_shows_relay_warning_only_below_threshold)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen 0 should exist";

    auto& session = *og::runtime::current_session;
    const bool previous_relay_active = session.relay_transport_active_;
    const bool previous_warning_shown = session.relay_speed_warning_shown_;
    const std::int8_t previous_pending_timer_wait =
        session.pending_timer_wait_request_;
    const signed char previous_timer_wait = session.myscreen_->world().timer_wait;

    session.relay_transport_active_ = true;
    session.relay_speed_warning_shown_ = false;
    session.pending_timer_wait_request_ = kNoTimerWaitRequest;
    session.myscreen_->world().timer_wait = 4;

    SpeedWarningDialogState state;
    SDL_Thread* thread = SDL_CreateThread(
        speed_warning_dialog_injector,
        "speed_warning_dialog_injector",
        &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create warning injector thread";

    const Sint32 faster_speed = vs->change_speed(1);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    EXPECT_TRUE(state.started);
    EXPECT_TRUE(state.finished);
    EXPECT_EQ(2, session.myscreen_->world().timer_wait);
    EXPECT_EQ(2, session.pending_timer_wait_request_);
    EXPECT_TRUE(session.relay_speed_warning_shown_);
    EXPECT_EQ(10, faster_speed);

    const Sint32 slower_speed = vs->change_speed(-1);
    EXPECT_EQ(4, session.myscreen_->world().timer_wait);
    EXPECT_EQ(4, session.pending_timer_wait_request_);
    EXPECT_FALSE(session.relay_speed_warning_shown_);
    EXPECT_EQ(9, slower_speed);

    session.relay_transport_active_ = previous_relay_active;
    session.relay_speed_warning_shown_ = previous_warning_shown;
    session.pending_timer_wait_request_ = previous_pending_timer_wait;
    session.myscreen_->world().timer_wait = previous_timer_wait;
}


// ---------------------------------------------------------------------------
// viewscreen resize tests (covers the large resize function)
// ---------------------------------------------------------------------------

TEST(ViewFuncs, viewscreen_resize_full)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_FULL);
    // Full screen mode for 1 player
    ASSERT_TRUE(vs->xview > 200) << "full view width should be > 200";
    ASSERT_TRUE(vs->yview > 150) << "full view height should be > 150";

    og::runtime::current_session->myscreen_->numviews = old_numviews;
}


TEST(ViewFuncs, viewscreen_resize_panels)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_PANELS);
    ASSERT_EQ(232, (int)vs->xview) << "panels mode width should be 232";
    ASSERT_EQ(176, (int)vs->yview) << "panels mode height should be 176";

    // Restore
    vs->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->numviews = old_numviews;
}


TEST(ViewFuncs, viewscreen_resize_small_modes)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;

    vs->resize(PREF_VIEW_1);
    ASSERT_EQ(192, (int)vs->xview) << "mode 1 width should be 192";

    vs->resize(PREF_VIEW_2);
    ASSERT_EQ(148, (int)vs->xview) << "mode 2 width should be 148";

    vs->resize(PREF_VIEW_3);
    ASSERT_EQ(108, (int)vs->xview) << "mode 3 width should be 108";

    // Restore
    vs->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->numviews = old_numviews;
}
