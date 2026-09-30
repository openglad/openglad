#include <gtest/gtest.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <SDL3/SDL.h>
#include <cstring>
#include <mutex>
#include <string>

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

TEST(IntroSmoke, a_latched_key_aborts_on_page_one_and_a_clean_run_shows_every_page)
{
    // intro_main's show() steps abort when query_key_press_event() is true,
    // and nothing in intro.cpp clears the latch — so a key pushed BEFORE
    // intro_main must abort on the very first page. show() traces
    // "intro_state"/"page ready" once per composed page and, on the abort,
    // "intro"/"intro aborted by key" (src/interface/ui/intro.cpp).
    clear_events();
    clear_key_press_event();
    clear_keyboard();
    trace_clear();
    push_any_keypress();
    intro_main(0, nullptr);

    ASSERT_TRUE(trace_contains("intro", "intro aborted by key"))
        << "a latched key must abort the intro";
    ASSERT_EQ(1, trace_count("intro_state"))
        << "the latched key aborts on the FIRST page, so exactly one page "
           "composed";
    ASSERT_EQ(1, trace_count("intro"))
        << "the abort is the only intro event: no click advance";

    // A clean run composes every page intro_main asks for: the seven show()
    // calls in intro_main (intro.cpp:154,170,180,194,224,237,278), none of
    // them aborting.
    clear_events();
    clear_key_press_event();
    clear_keyboard();
    trace_clear();
    intro_main(0, nullptr);

    ASSERT_EQ(0, trace_count("intro"))
        << "an undisturbed intro neither aborts nor advances by click";
    ASSERT_EQ(7, trace_count("intro_state"))
        << "every intro page must compose when nothing interrupts";
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

// ---------------------------------------------------------------------------
// A key aborts the intro on ANY page, not only the first two: for k = 2..5,
// k clicks advance exactly k pages (each click is consumed by the page it
// lands on) and a key pushed on page k+1 aborts there -- intro_main runs
// cleanup and composes no later page. (Page 7 is left out on purpose: its
// abort arm and its fall-through both run cleanup() and return, so no
// observable tells them apart and a test of it could not fail.) Every step waits on the product's
// own "page ready" / "page advanced by click" traces (each page's window is
// SHOW_TIME ticks of 13.6 ms, >= 1.7 s), never on a flat delay.
namespace
{
int count_trace_message(const char* category, const char* substring)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int count = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == category &&
            entry.message.find(substring) != std::string::npos)
            ++count;
    }
    return count;
}

bool wait_for_trace_count(const char* category, const char* substring,
                          int at_least)
{
    for (int waited = 0; waited < 30000; ++waited)
    {
        if (count_trace_message(category, substring) >= at_least)
            return true;
        SDL_Delay(1); // poll tick
    }
    return false;
}

int push_intro_clicks_then_key(void* data)
{
    og::runtime::ensure_thread_session();
    const int clicks = *static_cast<const int*>(data);
    for (int page = 1; page <= clicks; ++page)
    {
        if (!wait_for_trace_count("intro_state", "page ready", page))
            return 1;
        const auto [win_x, win_y] = ui_canvas_to_window(160.0f, 100.0f);
        inject_mouse_down(static_cast<int>(win_x), static_cast<int>(win_y));
        inject_mouse_up(static_cast<int>(win_x), static_cast<int>(win_y));
        if (!wait_for_trace_count("intro", "page advanced by click", page))
            return 1;
    }
    if (!wait_for_trace_count("intro_state", "page ready", clicks + 1))
        return 1;
    push_any_keypress();
    return 0;
}
} // namespace

TEST(IntroSmoke, a_key_aborts_the_intro_on_every_later_page)
{
    for (int clicks = 2; clicks <= 5; ++clicks)
    {
        clear_events();
        clear_key_press_event();
        clear_keyboard();
        trace_clear();

        int plan = clicks;
        SDL_Thread* injector = SDL_CreateThread(
            push_intro_clicks_then_key, "intro_clicks_then_key", &plan);
        ASSERT_TRUE(injector != nullptr);
        intro_main(0, nullptr);
        int injector_result = 1;
        SDL_WaitThread(injector, &injector_result);

        EXPECT_EQ(0, injector_result) << "clicks=" << clicks;
        EXPECT_EQ(clicks, count_trace_message("intro", "page advanced by click"))
            << "each click advances exactly one page (clicks=" << clicks << ")";
        EXPECT_EQ(1, count_trace_message("intro", "intro aborted by key"))
            << "the key aborts (clicks=" << clicks << ")";
        EXPECT_EQ(clicks + 1, trace_count("intro_state"))
            << "the key aborts on page " << clicks + 1
            << ": no later page composes";
    }
    clear_events();
    clear_key_press_event();
    clear_keyboard();
}
