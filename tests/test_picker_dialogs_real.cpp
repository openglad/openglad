#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"

// picker_dialogs.cpp symbols
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
bool no_or_yes_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
void picker_testing_set_force_real_dialogs(bool enabled);

namespace
{
struct ViewportGuard
{
    float ow = 0.0f;
    float oh = 0.0f;
    float ovw = 0.0f;
    float ovh = 0.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;

        og::runtime::current_session->window_w_ = 320;
        og::runtime::current_session->window_h_ = 200;
        og::runtime::current_session->viewport_offset_x_ = 0;
        og::runtime::current_session->viewport_offset_y_ = 0;
        og::runtime::current_session->viewport_w_ = 320;
        og::runtime::current_session->viewport_h_ = 200;
    }

    ~ViewportGuard()
    {
        og::runtime::current_session->window_w_ = ow;
        og::runtime::current_session->window_h_ = oh;
        og::runtime::current_session->viewport_w_ = ovw;
        og::runtime::current_session->viewport_h_ = ovh;
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
    }
};

struct RealDialogsGuard
{
    RealDialogsGuard() { picker_testing_set_force_real_dialogs(true); }
    ~RealDialogsGuard() { picker_testing_set_force_real_dialogs(false); }
};

struct DialogThreadState
{
    bool started = false;
    bool finished = false;
    int x = 0;
    int y = 0;
};

static int dialog_click_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DialogThreadState* st = static_cast<DialogThreadState*>(data);
    st->started = true;

    SDL_Delay(100);
    inject_click(st->x, st->y, 20);

    st->finished = true;
    return 0;
}
} // namespace

TEST(PickerDialogsReal, picker_dialogs_yes_or_no_queued_override_paths)
{
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    picker_testing_yes_or_no_queue_push(false);

    ASSERT_TRUE(yes_or_no_prompt("Delete", "Proceed with deletion?", false)) << "queued true override should force yes result";
    ASSERT_TRUE(!yes_or_no_prompt("Delete", "Proceed with deletion?", true)) << "queued false override should force no result";

    picker_testing_yes_or_no_queue_clear();
}


TEST(PickerDialogsReal, picker_dialogs_no_or_yes_default_paths)
{
    ASSERT_TRUE(no_or_yes_prompt("Reset", "Reset current progress?", true)) << "no_or_yes_prompt should return default=true in test mode";
    ASSERT_TRUE(!no_or_yes_prompt("Reset", "Reset current progress?", false)) << "no_or_yes_prompt should return default=false in test mode";
}


TEST(PickerDialogsReal, picker_dialogs_popup_dialog_testmode_noop)
{
    vbutton* buttons_before = og::runtime::current_session->localbuttons_;
    popup_dialog("Information", "This is a test popup\\nwith two lines.");
    ASSERT_EQ(buttons_before, og::runtime::current_session->localbuttons_)
        << "test-mode popup should return before installing dialog buttons";
}


TEST(PickerDialogsReal, picker_dialogs_yes_or_no_real_dialog_click_yes)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;

    DialogThreadState st{false, false, 95, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_yes_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create yes dialog injector";

    const bool accepted = yes_or_no_prompt("Delete", "Proceed with deletion?\nThis cannot be undone.", false);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "yes dialog injector should run";
    ASSERT_TRUE(accepted) << "YES click should accept the dialog";
}


TEST(PickerDialogsReal, picker_dialogs_no_or_yes_real_dialog_click_no)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;

    DialogThreadState st{false, false, 95, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_no_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create no dialog injector";

    const bool accepted = no_or_yes_prompt("Reset", "Reset current progress?\nThis cannot be undone.", true);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "no dialog injector should run";
    ASSERT_TRUE(!accepted) << "NO click should reject the dialog";
}


TEST(PickerDialogsReal, picker_dialogs_popup_dialog_real_click_ok)
{
    ViewportGuard viewport_guard;
    RealDialogsGuard real_dialogs_guard;

    DialogThreadState st{false, false, 160, 140};
    SDL_Thread* thread = SDL_CreateThread(dialog_click_injector, "picker_popup_dialog", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create popup dialog injector";

    popup_dialog("Information", "This is a real dialog\nwith two lines.");

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started && st.finished) << "popup dialog injector should run";
}
