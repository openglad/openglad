#include <gtest/gtest.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <SDL3/SDL.h>
#include <cstring>

#include "test_input_helpers.h"

extern void intro_main(Sint32 argc, char** argv);

static void push_any_keypress()
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));

    e.type = SDL_EVENT_KEY_DOWN;
    e.key.down = true;
    e.key.key = SDLK_SPACE;
    e.key.scancode = SDL_SCANCODE_SPACE;
    SDL_PushEvent(&e);

    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    e.key.key = SDLK_SPACE;
    e.key.scancode = SDL_SCANCODE_SPACE;
    SDL_PushEvent(&e);
}

static int push_intro_click_then_key(void*)
{
    og::runtime::ensure_thread_session();
    // Wait for intro_main to finish draining startup input and expose its
    // first page. A fixed delay races asset loading on slower sanitizers.
    for (int attempt = 0;
         attempt < 30000 && !trace_contains("intro_state", "page ready");
         ++attempt)
    {
        SDL_Delay(1);
    }
    // UI-canvas-pinned map: this injector thread races the main thread's
    // World<->UI canvas flip (see test_interact.h).
    const auto [win_x, win_y] = ui_canvas_to_window(160.0f, 100.0f);
    inject_mouse_down(static_cast<int>(win_x), static_cast<int>(win_y));
    inject_mouse_up(static_cast<int>(win_x), static_cast<int>(win_y));
    push_any_keypress();
    return 0;
}

TEST(IntroSmoke, intro_main_aborts_on_keypress)
{
    // intro_main's show() steps abort when query_key_press_event() is true.
    // Push a key so the intro exits quickly but still executes real code paths.
    push_any_keypress();
    intro_main(0, nullptr);

    // Run the full intro path as well to exercise all show()/cleanup branches.
    clear_events();
    clear_key_press_event();
    clear_keyboard();
    intro_main(0, nullptr);
}

TEST(IntroSmoke, click_advances_one_page_and_key_still_aborts)
{
    clear_events();
    clear_key_press_event();
    clear_keyboard();
    trace_clear();

    // One completed tap = exactly one page advance. Inject after intro_main
    // has deliberately discarded startup input; page 1 consumes the click
    // (checked before the key), and page 2 aborts on the still-latched key.
    SDL_Thread* injector = SDL_CreateThread(
        push_intro_click_then_key, "intro_click_then_key", nullptr);
    ASSERT_TRUE(injector != nullptr);

    intro_main(0, nullptr);
    int injector_result = 0;
    SDL_WaitThread(injector, &injector_result);

    ASSERT_TRUE(trace_contains("intro", "page advanced by click"));
    ASSERT_TRUE(trace_contains("intro", "intro aborted by key"));
    // One click trace + one key trace: the single tap advanced a single page.
    ASSERT_EQ(2, trace_count("intro"));

    clear_events();
    clear_key_press_event();
    clear_keyboard();
}
