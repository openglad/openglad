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
    bool* keys = const_cast<bool*>(SDL_GetKeyboardState(&numkeys));
    if (scancode >= 0 && scancode < numkeys)
        keys[scancode] = false;
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
    // Use the UI-canvas-pinned forward map (like test_interact.h does)
    // instead of hand-rolled session viewport math: a preceding
    // zoom/resolution/display-mode test can leave a non-16:10 window, and
    // this injector thread races the main thread's per-frame World<->UI
    // canvas flip — active_canvas_to_window sampled mid-flip mismaps the
    // coordinates. Menus live on the fixed UI canvas.
    const auto [win_x, win_y] = ui_canvas_to_window(
        static_cast<float>(game_x), static_cast<float>(game_y));
    event.motion.x = win_x;
    event.motion.y = win_y;
    SDL_PushEvent(&event);
}

static int count_pixels_of_color(screen& scr, int x0, int y0, int x1, int y1,
                                 int wanted)
{
    int count = 0;
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            int color = 0;
            scr.get_pixel(x, y, &color);
            if (color == wanted)
                ++count;
        }
    }
    return count;
}

static int count_nonzero_pixels(screen& scr, int x0, int y0, int x1, int y1)
{
    int count = 0;
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            int color = 0;
            scr.get_pixel(x, y, &color);
            if (color != 0)
                ++count;
        }
    }
    return count;
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
    bool* keys = const_cast<bool*>(SDL_GetKeyboardState(&numkeys));
    SDL_Scancode q = SDL_GetScancodeFromKey(SDLK_Q, nullptr);
    ASSERT_TRUE(q >= 0 && q < numkeys) << "q scancode should be valid";
    keys[q] = true;
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

// vbutton::vdisplay()/vdisplay(status) paint the face (or the sprite) and,
// when label.size(), the centred label in DARK_BLUE (src/interface/ui/
// button.cpp). Counting ANY ink proves only that the face drew -- the label
// is the half this test exists for, so count DARK_BLUE ink and pair every
// labelled case with the same button built with an EMPTY label, whose face
// must still paint but whose DARK_BLUE count must be zero.
TEST(Menu, graphic_and_alert_buttons_render_their_labels)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);

    const auto label_ink = [&](vbutton& b) {
        return count_pixels_of_color(*scr, b.xloc, b.yloc, b.xend, b.yend,
                                     static_cast<int>(DARK_BLUE));
    };
    const auto any_ink = [&](vbutton& b) {
        return count_nonzero_pixels(*scr, b.xloc, b.yloc, b.xend, b.yend);
    };

    vbutton graphic(12, 12, 20, 10, 0, 0, "GFX", 0,
                    KEYSTATE_UNKNOWN);
    ASSERT_NE(nullptr, graphic.mypixie);
    vbutton graphic_bare(12, 12, 20, 10, 0, 0, "", 0,
                         KEYSTATE_UNKNOWN);
    ASSERT_NE(nullptr, graphic_bare.mypixie);

    scr->clearbuffer();
    graphic_bare.vdisplay();
    const int graphic_sprite_only = label_ink(graphic_bare);
    EXPECT_GT(any_ink(graphic_bare), 0)
        << "the sprite must paint even without a label";

    scr->clearbuffer();
    graphic.vdisplay();
    EXPECT_GT(label_ink(graphic), graphic_sprite_only)
        << "a graphic button must ink its centered label on top of the sprite";

    scr->clearbuffer();
    graphic_bare.vdisplay(1);
    const int graphic_pressed_sprite_only = label_ink(graphic_bare);
    scr->clearbuffer();
    graphic.vdisplay(1);
    EXPECT_GT(label_ink(graphic), graphic_pressed_sprite_only)
        << "a depressed graphic button keeps its label visible";

    vbutton alert(70, 12, 50, 14, 0, 0, "ALERT", KEYSTATE_UNKNOWN);
    vbutton alert_bare(70, 12, 50, 14, 0, 0, "", KEYSTATE_UNKNOWN);
    scr->clearbuffer();
    alert_bare.vdisplay(2);
    const int alert_face_only = label_ink(alert_bare);
    EXPECT_EQ(0, alert_face_only)
        << "the red face itself carries no DARK_BLUE ink";
    EXPECT_GT(any_ink(alert_bare), 0)
        << "status-2 buttons paint the red face even unlabelled";

    scr->clearbuffer();
    alert.vdisplay(2);
    EXPECT_GT(label_ink(alert), alert_face_only)
        << "status-2 buttons should paint the red face and centered label";
}

TEST(Menu, button_dispatches_hotkey_and_right_click_actions)
{
    SDL_Scancode q = SDL_GetScancodeFromKey(SDLK_Q, nullptr);
    int numkeys = 0;
    bool* const keys = const_cast<bool*>(SDL_GetKeyboardState(&numkeys));
    ASSERT_GE(q, 0);
    ASSERT_LT(q, numkeys);

    vbutton hotkey_button(
        10, 10, 30, 10, button_action_id(ButtonAction::YesOrNo), 73,
        "HOT", q);
    keys[q] = true;
    SDL_Thread* raw_releaser = SDL_CreateThread(
        release_scancode_after_delay, "release_q_for_action", &q);
    ASSERT_NE(nullptr, raw_releaser);
    EXPECT_EQ(73, hotkey_button.leftclick(1))
        << "the configured action and argument should run from its hotkey";
    int release_result = 0;
    SDL_WaitThread(raw_releaser, &release_result);
    EXPECT_EQ(0, release_result);

    vbutton right_button(10, 30, 40, 12, 9999, 0, "RIGHT",
                         KEYSTATE_UNKNOWN);
    clear_events();
    push_mouse_motion_game_coords(20, 35);
    EXPECT_EQ(4, right_button.rightclick(0))
        << "right clicks should dispatch through the right-action table";
}

// vbutton::rightclick(button*) walks allbuttons_ in order, skips rows the
// descriptor hid, skips rows that answer -1 (pointer outside their face), and
// returns the FIRST non-(-1) answer; when nothing matches it returns its own
// 0 (src/interface/ui/button.cpp). 0 is therefore ambiguous, so row 1 gets an
// action do_call_right does not handle -- its default answer, 4, can only
// come from row 1 actually running.
TEST(Menu, rightclick_search_skips_misses_and_draw_tolerates_empty_slots)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);

    button descriptors[2] = {
        button("miss", "MISS", SDLK_M, 100, 100, 30, 12, 9999, 0, MenuNav{}),
        button("hit", "HIT", SDLK_H, 10, 10, 30, 12, 9999, 0, MenuNav{}),
    };
    vbutton* const first = init_buttons(descriptors, 2);
    ASSERT_NE(nullptr, first);

    clear_events();
    push_mouse_motion_game_coords(20, 15);
    EXPECT_EQ(4, first->rightclick(descriptors))
        << "the search must pass the first miss and run the second row's "
           "right action (4 is do_call_right's unhandled-action answer)";

    // Negative control: the pointer sits in neither face, so nobody answers
    // and the walk falls through to its own 'none worked' 0.
    clear_events();
    push_mouse_motion_game_coords(200, 150);
    EXPECT_EQ(0, first->rightclick(descriptors))
        << "with the pointer outside every face nothing may dispatch";

    // A descriptor-hidden row is skipped even with the pointer inside it.
    clear_events();
    push_mouse_motion_game_coords(20, 15);
    descriptors[1].hidden = true;
    EXPECT_EQ(0, first->rightclick(descriptors))
        << "a row the frame's gate pass hid must not answer a right click";
    descriptors[1].hidden = false;

    // draw_buttons paints the row it has a vbutton for...
    const button& row1 = descriptors[1];
    scr->clearbuffer();
    draw_buttons(descriptors, 2);
    const int drawn = count_nonzero_pixels(*scr, row1.x, row1.y,
                                           row1.x + row1.sizex,
                                           row1.y + row1.sizey);
    EXPECT_GT(drawn, 0) << "an installed row must paint its face";

    // ...and tolerates an emptied allbuttons_ slot by drawing nothing at all
    // (rather than dereferencing the null slot).
    clear_allbuttons();
    ASSERT_EQ(nullptr, og::runtime::current_session->allbuttons_[0]);
    scr->clearbuffer();
    draw_buttons(descriptors, 2);
    EXPECT_EQ(0, count_nonzero_pixels(*scr, row1.x, row1.y,
                                      row1.x + row1.sizex,
                                      row1.y + row1.sizey))
        << "an empty allbuttons_ slot must draw nothing, not crash";
    clear_allbuttons();
}

// Two rules the whole click sweep rests on, each beside the arm that must
// still fire. (1) A face the frame's gate pass hid takes no mouse focus and
// consumes no hotkey — a hidden button that still answered would eat clicks
// meant for whatever the screen drew in its place. (2) The hotkey pass stops
// at the FIRST button that consumes the key, so two rows sharing a hotkey
// run one action, not both.
TEST(Menu, click_sweep_skips_hidden_faces_and_stops_at_the_first_consumer)
{
    const SDL_Scancode q = SDL_GetScancodeFromKey(SDLK_Q, nullptr);
    int numkeys = 0;
    bool* const keys = const_cast<bool*>(SDL_GetKeyboardState(&numkeys));
    ASSERT_GE(q, 0);
    ASSERT_LT(q, numkeys);

    button descriptors[2] = {
        button("first", "FIRST", q, 10, 10, 30, 12,
               button_action_id(ButtonAction::YesOrNo), 11, MenuNav{}),
        button("second", "SECOND", q, 10, 30, 30, 12,
               button_action_id(ButtonAction::YesOrNo), 22, MenuNav{}),
    };
    vbutton* const sweep = init_buttons(descriptors, 2);
    ASSERT_NE(nullptr, sweep);
    vbutton* const first = og::runtime::current_session->allbuttons_[0];
    vbutton* const second = og::runtime::current_session->allbuttons_[1];
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);

    // Mouse focus: the pointer sits inside the FIRST face.
    clear_events();
    push_mouse_motion_game_coords(20, 15);
    EXPECT_EQ(1, first->mouse_on())
        << "the pointer is inside this face";
    EXPECT_EQ(0, second->mouse_on())
        << "and outside that one";
    first->hidden = 1;
    EXPECT_EQ(0, first->mouse_on())
        << "a hidden face takes no focus, pointer or no pointer";
    first->hidden = 0;
    EXPECT_EQ(1, first->mouse_on()) << "and takes it back when shown again";

    // Hotkey sweep: both rows answer to Q, and exactly the first one runs.
    SDL_Scancode release_key = q;
    keys[q] = true;
    SDL_Thread* releaser = SDL_CreateThread(
        release_scancode_after_delay, "release_q_first", &release_key);
    ASSERT_NE(nullptr, releaser);
    EXPECT_EQ(11, sweep->leftclick(descriptors))
        << "the first button that consumes the hotkey ends the sweep";
    SDL_WaitThread(releaser, nullptr);

    // Hide the first the way the runner's gate pass does, and the SAME key
    // reaches the second — proof the sweep really walked past a live row
    // rather than stopping at index 0 by accident.
    first->hidden = 1;
    keys[q] = true;
    releaser = SDL_CreateThread(
        release_scancode_after_delay, "release_q_second", &release_key);
    ASSERT_NE(nullptr, releaser);
    EXPECT_EQ(22, sweep->leftclick(descriptors))
        << "a hidden row consumes nothing and the next row answers";
    SDL_WaitThread(releaser, nullptr);

    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
}

TEST(Menu, legacy_button_actions_reach_their_compatible_dispatchers)
{
    vbutton dispatcher;

    trace_clear();
    EXPECT_EQ(1, dispatcher.do_call(
                     button_action_id(ButtonAction::QuitMenu), 17));
    EXPECT_TRUE(trace_contains("picker", "quit called"));

    const std::string old_render = cfg.get_setting("graphics", "render");
    const std::string expected_render = old_render == "sai"
        ? "eagle"
        : (old_render == "eagle" ? "normal" : "sai");
    EXPECT_EQ(2, dispatcher.do_call(
                     button_action_id(ButtonAction::ToggleRenderingEngine), 0));
    EXPECT_EQ(expected_render, cfg.get_setting("graphics", "render"));
    cfg.apply_setting("graphics", "render", old_render);
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
