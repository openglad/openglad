#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"

#include <string>
#include <unistd.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// From help.cpp
short read_scenario(screen* scr);
short read_campaign_intro(screen* scr);
Sint32 show_general_help();
Sint32 help_testing_exercise_internal_paths();
void help_testing_set_force_scroll_text(bool enabled);

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;

    ViewportGuard()
    {
        ow = og::runtime::current_session->window_w_;
        oh = og::runtime::current_session->window_h_;
        ovw = og::runtime::current_session->viewport_w_;
        ovh = og::runtime::current_session->viewport_h_;
        ox = og::runtime::current_session->viewport_offset_x_;
        oy = og::runtime::current_session->viewport_offset_y_;
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

struct ForceScrollTextGuard
{
    ForceScrollTextGuard() { help_testing_set_force_scroll_text(true); }
    ~ForceScrollTextGuard() { help_testing_set_force_scroll_text(false); }
};

static int help_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(100);

    // Scroll a little.
    SDL_Event wheel{};
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = -1;
    SDL_PushEvent(&wheel);

    SDL_Delay(50);

    // Click roughly where tab 2/3 live (show_general_help tab bar).
    inject_click(120, 22, 10);
    inject_click(182, 22, 10);

    SDL_Delay(50);

    // Exit: show_general_help() polls SDL_GetKeyboardState(), which does not
    // update from SDL_PushEvent(). Flip the scancode bit directly.
    int numkeys = 0;
    const Uint8* keys = SDL_GetKeyboardState(&numkeys);
    Uint8* writable = const_cast<Uint8*>(keys);
    const SDL_Scancode esc = SDL_GetScancodeFromKey(SDLK_ESCAPE);
    if (esc >= 0 && esc < numkeys)
    {
        // SDL_PollEvent() may overwrite the keyboard state each pump, so keep
        // this asserted for a short window.
        for (int i = 0; i < 200; i++)
        {
            writable[esc] = 1;
            SDL_Delay(1);
        }
        writable[esc] = 0;
    }
    return 0;
}

TEST(HelpSmoke, help_show_general_help_smoke_exits_on_escape)
{
    ViewportGuard guard;
    // Force 1:1 event coords to simplify tab click injection.
    og::runtime::current_session->window_w_ = 320;
    og::runtime::current_session->window_h_ = 200;
    og::runtime::current_session->viewport_offset_x_ = 0;
    og::runtime::current_session->viewport_offset_y_ = 0;
    og::runtime::current_session->viewport_w_ = 320;
    og::runtime::current_session->viewport_h_ = 200;

    SDL_Thread* thread = SDL_CreateThread(help_injector_thread, "help_injector", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)show_general_help();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
}


static int intro_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(100);

    // Trigger early exit via input_continue (Escape sets it in handle_key_event).
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

static void hold_keyboard_bit(int keycode, bool held)
{
    int numkeys = 0;
    const Uint8* keys = SDL_GetKeyboardState(&numkeys);
    Uint8* writable = const_cast<Uint8*>(keys);
    const SDL_Scancode scancode = SDL_GetScancodeFromKey(keycode);
    if (scancode >= 0 && scancode < numkeys)
        writable[scancode] = held ? 1 : 0;
}

static int scenario_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    (void)data;
    SDL_Delay(80);

    SDL_Event wheel{};
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = -3;
    SDL_PushEvent(&wheel);

    SDL_Delay(30);
    hold_keyboard_bit(SDLK_PAGEDOWN, true);
    SDL_Delay(40);
    hold_keyboard_bit(SDLK_PAGEDOWN, false);

    SDL_Delay(30);
    wheel.wheel.y = 3;
    SDL_PushEvent(&wheel);

    SDL_Delay(30);
    hold_keyboard_bit(SDLK_PAGEUP, true);
    SDL_Delay(40);
    hold_keyboard_bit(SDLK_PAGEUP, false);

    SDL_Delay(30);
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

TEST(HelpSmoke, help_read_campaign_intro_smoke_exits_on_input)
{
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";

    SDL_Thread* thread = SDL_CreateThread(intro_injector_thread, "intro_injector", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)read_campaign_intro(og::runtime::current_session->myscreen_);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
}

TEST(HelpSmoke, help_read_scenario_scroll_view_exits_on_input)
{
    ForceScrollTextGuard force_scroll;
    auto& description = og::runtime::current_session->myscreen_->level_description();
    const auto saved_description = description;
    description.clear();
    for (int i = 0; i < 40; ++i)
        description.push_back("Scenario line " + std::to_string(i));

    SDL_Thread* thread = SDL_CreateThread(scenario_injector_thread, "scenario_injector", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)read_scenario(og::runtime::current_session->myscreen_);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    hold_keyboard_bit(SDLK_PAGEUP, false);
    hold_keyboard_bit(SDLK_PAGEDOWN, false);
    description = saved_description;
}

TEST(HelpSmoke, help_internal_paths_cover_loading_tabs_and_scroll)
{
    constexpr Sint32 kExpectedInternalHelperChecks = 13;
    EXPECT_EQ(kExpectedInternalHelperChecks,
              help_testing_exercise_internal_paths());
}
