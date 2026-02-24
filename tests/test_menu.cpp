#include "SDL.h"
#include <openglad/data/gparser.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include <openglad/input/button.h>
#include "test_interact.h"
#include <openglad/input/input.h>
#include <openglad/runtime/screen.h>
extern MouseState mouse_state;

Sint32 yes_or_no(Sint32 arg);
void toggle_rendering_engine();
void toggle_effect(const std::string& category, const std::string& setting);
Sint32 leftmouse(button* buttons);

static int release_scancode_after_delay(void* data)
{
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
    event.type = SDL_MOUSEMOTION;
    event.motion.type = SDL_MOUSEMOTION;
    event.motion.x = static_cast<int>(viewport_offset_x + (static_cast<float>(game_x) * viewport_w / 320.0f));
    event.motion.y = static_cast<int>(viewport_offset_y + (static_cast<float>(game_y) * viewport_h / 200.0f));
    SDL_PushEvent(&event);
}

void test_mainmenu_buttons() {
    // Create a simple button array using the button struct constructor
    button test_buttons[3] = {
        button("begin", "BEGIN",   SDLK_b, 80, 60,  80, 20, 0, 0, MenuNav{}),
        button("options", "OPTIONS", SDLK_o, 80, 90,  80, 20, 0, 0, MenuNav{}),
        button("quit", "QUIT",    SDLK_q, 80, 120, 80, 20, 0, 0, MenuNav{}),
    };

    trace_clear();
    vbutton* result = init_buttons(test_buttons, 3);
    TEST_ASSERT(result != nullptr, "init_buttons should return non-nullptr");
    TEST_ASSERT(trace_contains("menu", "init_buttons"), "init_buttons trace should be logged");
    TEST_ASSERT(trace_contains("menu", "count=3"), "button count should be in trace");

    // Verify IDs are propagated through init_buttons to allbuttons/vbuttons
    TEST_ASSERT(has_interactable("begin"), "BEGIN should be interactable");
    TEST_ASSERT(has_interactable("options"), "OPTIONS should be interactable");
    TEST_ASSERT(has_interactable("quit"), "QUIT should be interactable");

    // Clean up allocated vbuttons to avoid leaking into other tests
    clear_allbuttons();
}
REGISTER_TEST(test_mainmenu_buttons);

void test_menu_button_misc_paths()
{
    MenuNav up = MenuNav{.up=7};
    TEST_ASSERT_EQ(7, up.up, "MenuNav::Up should set up");
    TEST_ASSERT_EQ(-1, up.down, "MenuNav::Up should leave down unset");
    TEST_ASSERT_EQ(-1, up.left, "MenuNav::Up should leave left unset");
    TEST_ASSERT_EQ(-1, up.right, "MenuNav::Up should leave right unset");

    TEST_ASSERT_EQ(123, yes_or_no(123), "yes_or_no should echo its arg");

    cfg.apply_setting("graphics", "render", "sai");
    toggle_rendering_engine();
    TEST_ASSERT_STR_EQ("eagle", cfg.get_setting("graphics", "render").c_str(), "sai -> eagle");
    toggle_rendering_engine();
    TEST_ASSERT_STR_EQ("normal", cfg.get_setting("graphics", "render").c_str(), "eagle -> normal");
    toggle_rendering_engine();
    TEST_ASSERT_STR_EQ("sai", cfg.get_setting("graphics", "render").c_str(), "normal -> sai");
    toggle_effect("effects", "gore");
    toggle_effect("effects", "gore");

    vbutton func_button(2, 3, 24, 12, passthrough_cb, 9, "Fn", KEYSTATE_UNKNOWN);
    TEST_ASSERT(func_button.fun != nullptr, "function-pointer constructor should populate fun");
    TEST_ASSERT_EQ(0, (int)func_button.myfunc, "function-pointer constructor should set myfunc=0");

    vbutton b(10, 10, 30, 10, 0, 0, "B", KEYSTATE_q);
    clear_events();
    push_mouse_motion_game_coords(15, 15);
    mouse_state.left = false;
    mouse_state.right = false;

    TEST_ASSERT_EQ(1, (int)b.mouse_on(), "mouse_on should detect in-bounds hover");
    TEST_ASSERT_EQ(1, (int)b.mouse_on(), "mouse_on should stay focused while hovered");
    push_mouse_motion_game_coords(200, 150);
    TEST_ASSERT_EQ(0, (int)b.mouse_on(), "mouse_on should clear focus out of bounds");

    push_mouse_motion_game_coords(15, 15);
    TEST_ASSERT_EQ(0, (int)b.rightclick(0), "rightclick direct path should succeed with myfunc=0");

    allbuttons[0] = &b;
    allbuttons[1] = nullptr;
    TEST_ASSERT_EQ(0, (int)b.rightclick(static_cast<button*>(nullptr)), "rightclick(button*) should dispatch");
    allbuttons[0] = nullptr;

    int numkeys = 0;
    Uint8* keys = const_cast<Uint8*>(SDL_GetKeyboardState(&numkeys));
    SDL_Scancode q = SDL_GetScancodeFromKey(SDLK_q);
    TEST_ASSERT(q >= 0 && q < numkeys, "q scancode should be valid");
    keys[q] = 1;
    SDL_Thread* releaser = SDL_CreateThread(release_scancode_after_delay, "release_q_for_button", &q);
    TEST_ASSERT(releaser != nullptr, "key release helper thread should start");
    TEST_ASSERT_EQ(0, (int)b.leftclick(1), "leftclick hotkey path should return with myfunc=0");
    int thread_result = 0;
    SDL_WaitThread(releaser, &thread_result);

    b.vdisplay(0);
    TEST_ASSERT_EQ(4, (int)b.do_call(9999, 0), "do_call unknown should return OK");
    TEST_ASSERT_EQ(4, (int)b.do_call_right(9999, 0), "do_call_right unknown should return 4");

    b.hidden = true;
    TEST_ASSERT_EQ(-1, (int)b.leftclick(2), "leftclick should reject hidden buttons");
    TEST_ASSERT_EQ(-1, (int)b.rightclick(0), "rightclick should reject hidden buttons");
}
REGISTER_TEST(test_menu_button_misc_paths);

void test_menu_hover_highlight_draws_without_click_and_persists()
{
    button test_buttons[1] = {
        button("hover", "HOVER", SDLK_h, 10, 10, 30, 10, 0, 0, MenuNav{}),
    };

    vbutton* local_btns = init_buttons(test_buttons, 1);
    TEST_ASSERT(local_btns != nullptr, "init_buttons should return first button");
    clear_events();
    mouse_state.left = false;
    mouse_state.right = false;

    clear_events();
    push_mouse_motion_game_coords(15, 15);
    leftmouse(test_buttons);
    myscreen->clearbuffer();
    draw_buttons(test_buttons, 1);
    TEST_ASSERT(local_btns->had_focus, "hover should set focus without clicking");

    // Without moving the mouse, highlight should persist frame-to-frame.
    leftmouse(test_buttons);
    myscreen->clearbuffer();
    draw_buttons(test_buttons, 1);
    TEST_ASSERT(local_btns->had_focus, "hover highlight should persist while hovered");

    clear_events();
    push_mouse_motion_game_coords(200, 150);
    leftmouse(test_buttons);
    myscreen->clearbuffer();
    draw_buttons(test_buttons, 1);
    TEST_ASSERT(!local_btns->had_focus, "hover highlight should clear after leaving button bounds");

    clear_allbuttons();
}
REGISTER_TEST(test_menu_hover_highlight_draws_without_click_and_persists);
