#include <openglad/interface/input.h>
#include <openglad/platform/game_session.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"

#include <array>
#include <cstring>


TEST(InputMore, input_mouse_wheel_sets_scroll_amount_and_keypress_flag)
{
    clear_keyboard();

    SDL_Event e{};
    e.type = SDL_MOUSEWHEEL;
    e.wheel.y = 1;
    handle_mouse_event(e);

    ASSERT_EQ(5, (int)get_and_reset_scroll_amount()) << "mouse wheel y=1 should set scroll_amount to 5";
    ASSERT_EQ(0, (int)get_and_reset_scroll_amount()) << "get_and_reset_scroll_amount should reset scroll_amount";
    ASSERT_EQ(1, (int)query_key_press_event()) << "mouse wheel should set key_press_event";
}


TEST(InputMore, input_clear_events_drains_queue)
{
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_a;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(SDLK_a);
    SDL_PushEvent(&e);

    clear_events();

    SDL_Event out{};
    int had = SDL_PollEvent(&out);
    ASSERT_EQ(0, had) << "clear_events should drain SDL event queue";
}


TEST(InputMore, input_assignKeyFromWaitEvent_updates_player_keys)
{
    int old = og::runtime::current_session->player_keys_[0][KEY_FIRE];

    // In TESTING builds, wait_for_key_event() returns a fake Escape event
    // so tests don't block on SDL_WaitEvent.
    assignKeyFromWaitEvent(0, KEY_FIRE);
    ASSERT_EQ(old, (int)og::runtime::current_session->player_keys_[0][KEY_FIRE]) << "fake Escape event should not update mapping";
}


TEST(InputMore, input_send_fake_key_events_flow_through_get_input_events)
{
    clear_keyboard();

    sendFakeKeyDownEvent(SDLK_c);
    get_input_events(POLL);

    ASSERT_EQ((int)SDLK_c, (int)query_key()) << "fake keydown should be handled and set raw_key";
    ASSERT_EQ(1, (int)query_key_press_event()) << "fake keydown should set key_press_event";

    sendFakeKeyUpEvent(SDLK_c);
    get_input_events(POLL);
    ASSERT_EQ(0, og::runtime::current_session->keystates_[SDL_GetScancodeFromKey(SDLK_c)])
        << "fake keyup should clear the key state";

    clear_keyboard();
}

TEST(InputMore, input_null_and_wrong_event_paths_are_ignored)
{
    clear_keyboard();

    handle_events(nullptr);
    handle_text_event(nullptr);
    ASSERT_TRUE(query_text_input() == nullptr) << "null text event should not set text";

    handle_mouse_event(nullptr);
    ASSERT_EQ(0, (int)get_and_reset_scroll_amount()) << "null mouse event should not scroll";

    ASSERT_TRUE(!query_key_event(SDLK_a, nullptr)) << "null event should not match key";
    quit_if_quit_event(nullptr);

    SDL_Event text{};
    text.type = SDL_TEXTINPUT;
    std::strncpy(text.text.text, "abc", sizeof(text.text.text) - 1);
    ASSERT_TRUE(!query_key_event(SDLK_a, text)) << "text event should not match keydown";

    ASSERT_TRUE(!isKeyboardEvent(nullptr)) << "null event is not keyboard";
    ASSERT_TRUE(!isJoystickEvent(nullptr)) << "null event is not joystick";
    ASSERT_TRUE(!isKeyboardEvent(text)) << "text event is not keyboard";
    ASSERT_TRUE(!isJoystickEvent(text)) << "text event is not joystick";

    SDL_Event up{};
    up.type = SDL_KEYUP;
    up.key.keysym.sym = SDLK_a;
    ASSERT_TRUE(!isKeyboardEvent(up)) << "keyup is not keyboard input for this predicate";
}
