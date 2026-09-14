#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// From picker.cpp
void timed_dialog(const char* message, float delay_seconds = 3.0f);

struct TimedDialogState {
    bool started;
    bool finished;
    bool saw_open_trace;
};

static int timed_dialog_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TimedDialogState* st = static_cast<TimedDialogState*>(data);
    st->started = true;

    // picker_dialogs.cpp traces "timed_dialog_open <message>" AFTER
    // clear_key_press_event() and BEFORE the wait loop, exactly so an
    // injector can stop guessing: anything sent once this is up is still
    // pending when the loop polls. Bounded, never a flat delay.
    const Uint64 deadline = SDL_GetTicks() + 2000;
    while (SDL_GetTicks() < deadline)
    {
        if (trace_contains("dialog", "timed_dialog_open test timed dialog"))
        {
            st->saw_open_trace = true;
            break;
        }
        SDL_Delay(5);
    }

    // Any key press must break out early.
    inject_key_press(SDLK_SPACE, 10);

    st->finished = true;
    return 0;
}

// The rule is the early escape at picker_dialogs.cpp:135 -- a key press (or a
// left click) ends the wait before delay_seconds runs out. Timing IS the
// oracle here: without it the call simply sleeps out the full 5 s and every
// flag-based assertion stays green.
TEST(PickerTimedDialog, breaks_on_input)
{
    (void)og::runtime::current_session->myscreen_; // ensure screen exists

    trace_clear();
    TimedDialogState st{false, false, false};
    SDL_Thread* thread = SDL_CreateThread(timed_dialog_injector, "timed_dialog_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    const Uint64 t0 = SDL_GetTicks();
    timed_dialog("test timed dialog", 5.0f);
    const Uint64 elapsed = SDL_GetTicks() - t0;

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    ASSERT_TRUE(st.started) << "injector should have started";
    ASSERT_TRUE(st.finished) << "injector should have finished";
    ASSERT_TRUE(st.saw_open_trace)
        << "the injector must have seen timed_dialog_open before pressing a key";
    ASSERT_TRUE(trace_contains("dialog", "timed_dialog_closed test timed dialog"))
        << "the dialog must close (and say so) rather than be abandoned";
    ASSERT_LT(elapsed, 2000u)
        << "a key press must break the 5 s wait, not run it out (took "
        << elapsed << " ms)";
}
