#include <SDL3/SDL.h>
#include <openglad/resources/gparser.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <openglad/interface/button.h>
#include "test_interact.h"
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
Sint32 yes_or_no(Sint32 arg);
void toggle_rendering_engine();
void toggle_effect(const std::string& category, const std::string& setting);
Sint32 leftmouse(button* buttons);

static int release_scancode_after_delay(void* data)
{
    og::runtime::ensure_thread_session();
    SDL_Scancode scancode = *static_cast<SDL_Scancode*>(data);
    SDL_Delay(20);
    int numkeys = 0;
    Uint8* keys = const_cast<Uint8*>(SDL_GetKeyboardState(&numkeys));
    if (scancode >= 0 && scancode < numkeys)
        keys[scancode] = 0;
    return 0;
}

static Sint32 passthrough_cb(Sint32 arg)
{
    return arg;
}

static void push_mouse_motion_game_coords(int game_x, int game_y)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = static_cast<int>(og::runtime::current_session->viewport_offset_x_ + (static_cast<float>(game_x) * og::runtime::current_session->viewport_w_ / 320.0f));
    event.motion.y = static_cast<int>(og::runtime::current_session->viewport_offset_y_ + (static_cast<float>(game_y) * og::runtime::current_session->viewport_h_ / 200.0f));
    SDL_PushEvent(&event);
}

TEST(Menu, mainmenu_buttons) {
    // Create a simple button array using the button struct constructor
    button test_buttons[3] = {
        button("begin", "BEGIN",   SDLK_B, 80, 60,  80, 20, 0, 0, MenuNav{}),
        button("options", "OPTIONS", SDLK_O, 80, 90,  80, 20, 0, 0, MenuNav{}),
        button("quit", "QUIT",    SDLK_Q, 80, 120, 80, 20, 0, 0, MenuNav{}),
    };

    trace_clear();
    vbutton* result = init_buttons(test_buttons, 3);
    ASSERT_TRUE(result != nullptr) << "init_buttons should return non-nullptr";
    ASSERT_TRUE(trace_contains("menu", "init_buttons")) << "init_buttons trace should be logged";
    ASSERT_TRUE(trace_contains("menu", "count=3")) << "button count should be in trace";

    // Verify IDs are propagated through init_buttons to allbuttons/vbuttons
    ASSERT_TRUE(has_interactable("begin")) << "BEGIN should be interactable";
    ASSERT_TRUE(has_interactable("options")) << "OPTIONS should be interactable";
    ASSERT_TRUE(has_interactable("quit")) << "QUIT should be interactable";

    // Clean up allocated vbuttons to avoid leaking into other tests
    clear_allbuttons();
}


TEST(Menu, button_misc_paths)
{
    MenuNav up = MenuNav{.up=7};
    ASSERT_EQ(7, up.up) << "MenuNav::Up should set up";
    ASSERT_EQ(-1, up.down) << "MenuNav::Up should leave down unset";
    ASSERT_EQ(-1, up.left) << "MenuNav::Up should leave left unset";
    ASSERT_EQ(-1, up.right) << "MenuNav::Up should leave right unset";

    ASSERT_EQ(123, yes_or_no(123)) << "yes_or_no should echo its arg";

    cfg.apply_setting("graphics", "render", "sai");
    toggle_rendering_engine();
    ASSERT_STREQ("eagle", cfg.get_setting("graphics", "render").c_str()) << "sai -> eagle";
    toggle_rendering_engine();
    ASSERT_STREQ("normal", cfg.get_setting("graphics", "render").c_str()) << "eagle -> normal";
    toggle_rendering_engine();
    ASSERT_STREQ("sai", cfg.get_setting("graphics", "render").c_str()) << "normal -> sai";
    toggle_effect("effects", "gore");
    toggle_effect("effects", "gore");

    vbutton func_button(2, 3, 24, 12, passthrough_cb, 9, "Fn", KEYSTATE_UNKNOWN);
    ASSERT_TRUE(func_button.fun != nullptr) << "function-pointer constructor should populate fun";
    ASSERT_EQ(0, (int)func_button.myfunc) << "function-pointer constructor should set myfunc=0";

    vbutton b(10, 10, 30, 10, 0, 0, "B", KEYSTATE_q);
    clear_events();
    push_mouse_motion_game_coords(15, 15);
    mouse_state.left = false;
    mouse_state.right = false;

    ASSERT_EQ(1, (int)b.mouse_on()) << "mouse_on should detect in-bounds hover";
    ASSERT_EQ(1, (int)b.mouse_on()) << "mouse_on should stay focused while hovered";
    push_mouse_motion_game_coords(200, 150);
    ASSERT_EQ(0, (int)b.mouse_on()) << "mouse_on should clear focus out of bounds";

    push_mouse_motion_game_coords(15, 15);
    ASSERT_EQ(0, (int)b.rightclick(0)) << "rightclick direct path should succeed with myfunc=0";

    og::runtime::current_session->allbuttons_[0] = &b;
    og::runtime::current_session->allbuttons_[1] = nullptr;
    ASSERT_EQ(0, (int)b.rightclick(static_cast<button*>(nullptr))) << "rightclick(button*) should dispatch";
    og::runtime::current_session->allbuttons_[0] = nullptr;

    int numkeys = 0;
    Uint8* keys = const_cast<Uint8*>(SDL_GetKeyboardState(&numkeys));
    SDL_Scancode q = SDL_GetScancodeFromKey(SDLK_Q);
    ASSERT_TRUE(q >= 0 && q < numkeys) << "q scancode should be valid";
    keys[q] = 1;
    SDL_Thread* releaser = SDL_CreateThread(release_scancode_after_delay, "release_q_for_button", &q);
    ASSERT_TRUE(releaser != nullptr) << "key release helper thread should start";
    ASSERT_EQ(0, (int)b.leftclick(1)) << "leftclick hotkey path should return with myfunc=0";
    int thread_result = 0;
    SDL_WaitThread(releaser, &thread_result);

    b.vdisplay(0);
    ASSERT_EQ(4, (int)b.do_call(9999, 0)) << "do_call unknown should return OK";
    ASSERT_EQ(4, (int)b.do_call_right(9999, 0)) << "do_call_right unknown should return 4";

    b.hidden = true;
    ASSERT_EQ(-1, (int)b.leftclick(2)) << "leftclick should reject hidden buttons";
    ASSERT_EQ(-1, (int)b.rightclick(0)) << "rightclick should reject hidden buttons";
}


TEST(Menu, hover_highlight_draws_without_click_and_persists)
{
    button test_buttons[1] = {
        button("hover", "HOVER", SDLK_H, 10, 10, 30, 10, 0, 0, MenuNav{}),
    };

    vbutton* local_btns = init_buttons(test_buttons, 1);
    ASSERT_TRUE(local_btns != nullptr) << "init_buttons should return first button";
    clear_events();
    mouse_state.left = false;
    mouse_state.right = false;

    clear_events();
    push_mouse_motion_game_coords(15, 15);
    leftmouse(test_buttons);
    og::runtime::current_session->myscreen_->clearbuffer();
    draw_buttons(test_buttons, 1);
    ASSERT_TRUE(local_btns->had_focus) << "hover should set focus without clicking";

    // Without moving the mouse, highlight should persist frame-to-frame.
    leftmouse(test_buttons);
    og::runtime::current_session->myscreen_->clearbuffer();
    draw_buttons(test_buttons, 1);
    ASSERT_TRUE(local_btns->had_focus) << "hover highlight should persist while hovered";

    clear_events();
    push_mouse_motion_game_coords(200, 150);
    leftmouse(test_buttons);
    og::runtime::current_session->myscreen_->clearbuffer();
    draw_buttons(test_buttons, 1);
    ASSERT_TRUE(!local_btns->had_focus) << "hover highlight should clear after leaving button bounds";

    clear_allbuttons();
}

