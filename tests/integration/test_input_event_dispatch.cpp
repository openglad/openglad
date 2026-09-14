#include <openglad/gameplay/input_state.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

namespace {

// handle_events() is pure routing (src/interface/input/input.cpp:1007-1068):
// every branch it owns is pinned below by the state the handler it routes to
// writes, so an `if (true) return;` at the top -- or any arm wired to the
// wrong handler -- turns this test red.
struct ViewportRestore
{
    float ox = og::runtime::current_session->viewport_offset_x_;
    float oy = og::runtime::current_session->viewport_offset_y_;
    float w = og::runtime::current_session->viewport_w_;
    float h = og::runtime::current_session->viewport_h_;
    CanvasTarget canvas = og::runtime::current_session->myscreen_->active_canvas();
    ~ViewportRestore()
    {
        og::runtime::current_session->myscreen_->set_active_canvas(canvas);
        og::runtime::current_session->viewport_offset_x_ = ox;
        og::runtime::current_session->viewport_offset_y_ = oy;
        og::runtime::current_session->viewport_w_ = w;
        og::runtime::current_session->viewport_h_ = h;
    }
};

}  // namespace

TEST(InputEventDispatch, input_handle_events_routes_each_event_type_to_its_handler)
{
    ViewportRestore restore;
    // A 2x window (640x400) over the classic 320x200 UI canvas: window (320,200)
    // is the canvas centre (160,100).
    og::runtime::current_session->myscreen_->set_active_canvas(CanvasTarget::UI);
    og::runtime::current_session->viewport_offset_x_ = 0.0f;
    og::runtime::current_session->viewport_offset_y_ = 0.0f;
    og::runtime::current_session->viewport_w_ = 640.0f;
    og::runtime::current_session->viewport_h_ = 400.0f;

    SDL_Event e{};

    // --- Touch and joystick arms: no device is attached and every handler
    // refuses, so they carry no state of their own. They are pushed first so
    // that the pinned values below are the ones that survive to the end.
    for (const SDL_EventType finger : {SDL_EVENT_FINGER_MOTION,
                                       SDL_EVENT_FINGER_DOWN,
                                       SDL_EVENT_FINGER_UP})
    {
        e = SDL_Event{};
        e.type = finger;
        handle_events(e);
    }
    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    e.jaxis.which = 0;
    e.jaxis.axis = 0;
    e.jaxis.value = 1000;
    handle_events(e);
    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 0;
    e.jbutton.button = 0;
    handle_events(e);
    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
    e.jbutton.which = 0;
    e.jbutton.button = 0;
    handle_events(e);

    // --- KeyDown -> handle_key_event: session key + key_press_event_.
    clear_keyboard();
    e = SDL_Event{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_A;
    handle_events(e);
    ASSERT_EQ((int)SDLK_A, (int)query_key())
        << "a routed keydown must reach handle_key_event's raw_key_ write";
    ASSERT_EQ(1, (int)query_key_press_event())
        << "a routed keydown must raise key_press_event";
    ASSERT_FALSE(query_input_continue())
        << "only Escape sets input_continue";

    // --- KeyUp -> handle_key_event, whose KEY_UP arm deliberately writes
    // nothing: routing a key release through the keydown arm would latch the
    // released key as the current one.
    clear_key_press_event();
    e = SDL_Event{};
    e.type = SDL_EVENT_KEY_UP;
    e.key.key = SDLK_B;
    handle_events(e);
    ASSERT_EQ((int)SDLK_A, (int)query_key())
        << "a key release must not become the current key";
    ASSERT_EQ(0, (int)query_key_press_event())
        << "a key release must not raise key_press_event";

    // --- MouseMotion -> handle_mouse_event: window point scaled into canvas.
    e = SDL_Event{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = 320;
    e.motion.y = 200;
    handle_events(e);
    ASSERT_EQ(160, (int)mouse_state.x)
        << "a routed mouse motion must be scaled into 320-wide canvas coords";
    ASSERT_EQ(100, (int)mouse_state.y)
        << "a routed mouse motion must be scaled into 200-tall canvas coords";

    // --- MouseButtonDown/Up -> handle_mouse_event: the button latch.
    e = SDL_Event{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = 320;
    e.button.y = 200;
    handle_events(e);
    ASSERT_EQ(1, (int)mouse_state.left) << "a routed left press must latch the button";
    ASSERT_TRUE(query_input_continue())
        << "a left press inside the canvas also dismisses press-to-continue waits";
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    handle_events(e);
    ASSERT_EQ(0, (int)mouse_state.left) << "a routed left release must clear the button";

    // --- MouseWheel -> handle_mouse_event: scroll_amount_ = 5 * wheel y.
    input_scroll_amount_ref() = 0;
    clear_key_press_event();
    e = SDL_Event{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = -3.0f;
    e.wheel.integer_y = -3;
    handle_events(e);
    ASSERT_EQ(-15, (int)input_scroll_amount_ref())
        << "a routed wheel notch must scale by 5 and keep its sign";
    ASSERT_EQ(1, (int)query_key_press_event())
        << "a routed wheel notch counts as a key press event";

    // --- Quit -> quit(0), a no-op under TESTING that must disturb nothing.
    e = SDL_Event{};
    e.type = SDL_EVENT_QUIT;
    handle_events(e);
    ASSERT_EQ(160, (int)mouse_state.x) << "quit must not disturb the pointer";
    ASSERT_EQ(100, (int)mouse_state.y) << "quit must not disturb the pointer";
    ASSERT_EQ(-15, (int)input_scroll_amount_ref()) << "quit must not disturb the scroll";
    ASSERT_EQ((int)SDLK_A, (int)query_key()) << "quit must not disturb the current key";

    input_scroll_amount_ref() = 0;
    clear_keyboard();
}
