#include <openglad/interface/input.h>
#include <openglad/platform/game_session.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <array>
#include <cstring>


TEST(InputMore, input_mouse_wheel_sets_scroll_amount_and_keypress_flag)
{
    clear_keyboard();

    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = 1;
    e.wheel.integer_y = 1;
    handle_mouse_event(e);

    ASSERT_EQ(5, (int)get_and_reset_scroll_amount()) << "mouse wheel y=1 should set scroll_amount to 5";
    ASSERT_EQ(0, (int)get_and_reset_scroll_amount()) << "get_and_reset_scroll_amount should reset scroll_amount";
    ASSERT_EQ(1, (int)query_key_press_event()) << "mouse wheel should set key_press_event";
}


TEST(InputMore, input_clear_events_drains_queue)
{
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_A;
    e.key.scancode = SDL_GetScancodeFromKey(SDLK_A, nullptr);
    SDL_PushEvent(&e);

    clear_events();

    SDL_Event out{};
    int had = SDL_PollEvent(&out);
    ASSERT_EQ(0, had) << "clear_events should drain SDL event queue";
}


TEST(InputMore, input_assign_key_polling_escape_keeps_mapping)
{
    int old = og::runtime::current_session->player_keys_[0][KEY_FIRE];

    // In TESTING builds, wait_for_key_event_polling() returns a fake Escape
    // event so tests don't block; Escape means "keep the current binding".
    (void)assignKeyFromWaitEventPolling(0, KEY_FIRE, nullptr);
    ASSERT_EQ(old, (int)og::runtime::current_session->player_keys_[0][KEY_FIRE]) << "fake Escape event should not update mapping";
}


TEST(InputMore, input_send_fake_key_events_flow_through_get_input_events)
{
    clear_keyboard();

    sendFakeKeyDownEvent(SDLK_C);
    get_input_events(POLL);

    ASSERT_EQ((int)SDLK_C, (int)query_key()) << "fake keydown should be handled and set raw_key";
    ASSERT_EQ(1, (int)query_key_press_event()) << "fake keydown should set key_press_event";

    sendFakeKeyUpEvent(SDLK_C);
    get_input_events(POLL);
    ASSERT_EQ(0, og::runtime::current_session->keystates_[SDL_GetScancodeFromKey(SDLK_C, nullptr)])
        << "fake keyup should clear the key state";

    clear_keyboard();
}

TEST(InputMore, input_null_and_wrong_event_paths_are_ignored)
{
    clear_keyboard();

    // Cast explicitly to the native pointer overload.  A bare nullptr would
    // bind the generic EventT reference adapter and test the address of a
    // std::nullptr_t object instead of the public null-event contract.
    const void* const no_event = nullptr;
    handle_events(no_event);
    handle_text_event(no_event);
    ASSERT_TRUE(query_text_input() == nullptr) << "null text event should not set text";

    handle_mouse_event(no_event);
    ASSERT_EQ(0, (int)get_and_reset_scroll_amount()) << "null mouse event should not scroll";

    ASSERT_TRUE(!query_key_event(SDLK_A, no_event)) << "null event should not match key";
    quit_if_quit_event(no_event);

    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "abc";
    ASSERT_TRUE(!query_key_event(SDLK_A, text)) << "text event should not match keydown";

    ASSERT_TRUE(!isKeyboardEvent(nullptr)) << "null event is not keyboard";
    ASSERT_TRUE(!isJoystickEvent(nullptr)) << "null event is not joystick";
    ASSERT_TRUE(!isKeyboardEvent(text)) << "text event is not keyboard";
    ASSERT_TRUE(!isJoystickEvent(text)) << "text event is not joystick";

    SDL_Event up{};
    up.type = SDL_EVENT_KEY_UP;
    up.key.key = SDLK_A;
    ASSERT_TRUE(!isKeyboardEvent(up)) << "keyup is not keyboard input for this predicate";

    SDL_Event quit_event{};
    quit_event.type = SDL_EVENT_QUIT;
    trace_clear();
    quit_if_quit_event(quit_event);
    EXPECT_TRUE(trace_contains("picker", "quit called"))
        << "quit events should reach the picker quit action in TESTING";
}

TEST(InputMore, reset_mouse_click_tracking_discards_old_clicks_and_keeps_fresh_taps)
{
    clear_events();
    mouse_state.left = false;
    mouse_state.right = false;
    input_hardware_state().picker_was_left_down = false;
    input_hardware_state().picker_was_right_down = false;
    reset_mouse_click_tracking();

    auto mouse_event = [](Uint32 type) {
        SDL_Event event{};
        event.type = type;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = og::runtime::current_session->viewport_offset_x_ +
            og::runtime::current_session->viewport_w_ / 2.0f;
        event.button.y = og::runtime::current_session->viewport_offset_y_ +
            og::runtime::current_session->viewport_h_ / 2.0f;
        return event;
    };

    SDL_Event down = mouse_event(SDL_EVENT_MOUSE_BUTTON_DOWN);
    SDL_Event up = mouse_event(SDL_EVENT_MOUSE_BUTTON_UP);

    // A complete click owned by the outgoing surface must not be delivered
    // after the handoff.
    handle_mouse_event(down);
    handle_mouse_event(up);
    reset_mouse_click_tracking();
    EXPECT_FALSE(take_pending_left_click());

    // A press that is still held at the handoff becomes the baseline; its
    // later release must not manufacture a click on the new surface.
    handle_mouse_event(down);
    reset_mouse_click_tracking();
    EXPECT_TRUE(input_hardware_state().picker_was_left_down);
    handle_mouse_event(up);
    EXPECT_FALSE(take_pending_left_click());

    // A genuinely new tap after the old pointer was released still works.
    handle_mouse_event(down);
    handle_mouse_event(up);
    EXPECT_TRUE(take_pending_left_click());
    EXPECT_FALSE(take_pending_left_click());

    reset_mouse_click_tracking();
}

TEST(InputMore, collapsed_right_click_is_delivered_exactly_once)
{
    clear_events();
    mouse_state.left = false;
    mouse_state.right = false;
    input_hardware_state().picker_was_left_down = false;
    input_hardware_state().picker_was_right_down = false;
    reset_mouse_click_tracking();

    auto right_event = [](Uint32 type) {
        SDL_Event event{};
        event.type = type;
        event.button.button = SDL_BUTTON_RIGHT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = og::runtime::current_session->viewport_offset_x_ +
            og::runtime::current_session->viewport_w_ / 2.0f;
        event.button.y = og::runtime::current_session->viewport_offset_y_ +
            og::runtime::current_session->viewport_h_ / 2.0f;
        return event;
    };

    SDL_Event down = right_event(SDL_EVENT_MOUSE_BUTTON_DOWN);
    SDL_Event up = right_event(SDL_EVENT_MOUSE_BUTTON_UP);
    handle_mouse_event(down);
    handle_mouse_event(up);

    EXPECT_TRUE(take_pending_right_click())
        << "a down/up pair in one event pump must survive until the menu polls";
    EXPECT_FALSE(take_pending_right_click())
        << "the collapsed click is a one-shot event";
    reset_mouse_click_tracking();
}
