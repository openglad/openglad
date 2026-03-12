#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// From picker.cpp
void timed_dialog(const char* message, float delay_seconds = 3.0f);

struct TimedDialogState {
    bool started;
    bool finished;
};

static int timed_dialog_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TimedDialogState* st = static_cast<TimedDialogState*>(data);
    st->started = true;
    SDL_Delay(50);

    // Any key press should break out early.
    inject_key_press(SDLK_SPACE, 10);

    st->finished = true;
    return 0;
}

TEST(PickerTimedDialog, breaks_on_input)
{
    (void)og::runtime::current_session->myscreen_; // ensure screen exists

    TimedDialogState st{false, false};
    SDL_Thread* thread = SDL_CreateThread(timed_dialog_injector, "timed_dialog_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    timed_dialog("test timed dialog", 5.0f);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started) << "injector should have started";
    ASSERT_TRUE(st.finished) << "injector should have finished";
}

