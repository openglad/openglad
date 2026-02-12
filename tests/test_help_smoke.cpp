#include "graph.h"
#include "input/input.h"
#include "test_framework.h"
#include "test_input_helpers.h"

#include <unistd.h>

extern screen* myscreen;

// From help.cpp
short read_campaign_intro(screen* myscreen);
Sint32 show_general_help();

struct ViewportGuard
{
    float ow, oh, ovw, ovh, ox, oy;

    ViewportGuard()
    {
        ow = window_w;
        oh = window_h;
        ovw = viewport_w;
        ovh = viewport_h;
        ox = viewport_offset_x;
        oy = viewport_offset_y;
    }

    ~ViewportGuard()
    {
        window_w = ow;
        window_h = oh;
        viewport_w = ovw;
        viewport_h = ovh;
        viewport_offset_x = ox;
        viewport_offset_y = oy;
    }
};

static int help_injector_thread(void* data)
{
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

void test_help_show_general_help_smoke_exits_on_escape()
{
    ViewportGuard guard;
    // Force 1:1 event coords to simplify tab click injection.
    window_w = 320;
    window_h = 200;
    viewport_offset_x = 0;
    viewport_offset_y = 0;
    viewport_w = 320;
    viewport_h = 200;

    SDL_Thread* thread = SDL_CreateThread(help_injector_thread, "help_injector", nullptr);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    (void)show_general_help();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
}
REGISTER_TEST(test_help_show_general_help_smoke_exits_on_escape);

static int intro_injector_thread(void* data)
{
    (void)data;
    SDL_Delay(100);

    // Trigger early exit via input_continue (Escape sets it in handle_key_event).
    inject_key_press(SDLK_ESCAPE, 10);
    return 0;
}

void test_help_read_campaign_intro_smoke_exits_on_input()
{
    myscreen->save_data.current_campaign = "org.openglad.gladiator";

    SDL_Thread* thread = SDL_CreateThread(intro_injector_thread, "intro_injector", nullptr);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    (void)read_campaign_intro(myscreen);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
}
REGISTER_TEST(test_help_read_campaign_intro_smoke_exits_on_input);
