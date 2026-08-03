#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/button.h>
#include <openglad/interface/native_input.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

extern cfg_store cfg;

namespace
{
struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old;

    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_), old(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }

    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old;
    }
};

struct ControlModeGuard
{
    int player;
    int old_mode;

    explicit ControlModeGuard(int player_)
        : player(player_), old_mode(get_player_control_mode(player_))
    {}

    ~ControlModeGuard()
    {
        set_player_control_mode(player, old_mode);
    }
};

struct KeyStateGuard
{
    SDL_Scancode sc;
    bool old_value;
    int numkeys;
    bool* keys;

    explicit KeyStateGuard(SDL_Scancode sc_)
        : sc(sc_), old_value(false), numkeys(0), keys(nullptr)
    {
        const bool* read_only = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<bool*>(read_only);
        if (keys && sc >= 0 && sc < numkeys)
            old_value = keys[sc];
    }

    void set(bool pressed)
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = pressed;
    }

    ~KeyStateGuard()
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = old_value;
    }
};

struct ModeKeyBindingGuard
{
    int player;
    int key_enum;
    int old_mode;
    int old_four;
    int old_eight;

    ModeKeyBindingGuard(int player_, int key_enum_)
        : player(player_),
          key_enum(key_enum_),
          old_mode(get_player_control_mode(player_)),
          old_four(get_player_key_binding_for_mode(player_, static_cast<int>(ControlDirectionMode::FourDirection), key_enum_)),
          old_eight(get_player_key_binding_for_mode(player_, static_cast<int>(ControlDirectionMode::EightDirection), key_enum_))
    {}

    ~ModeKeyBindingGuard()
    {
        set_player_control_mode(player, static_cast<int>(ControlDirectionMode::FourDirection));
        set_player_key_binding(player, key_enum, old_four);
        set_player_control_mode(player, static_cast<int>(ControlDirectionMode::EightDirection));
        set_player_key_binding(player, key_enum, old_eight);
        set_player_control_mode(player, old_mode);
    }
};

struct FullControlSnapshotGuard
{
    int modes[4];
    int default_profiles[4];
    int mode4[4][NUM_KEYS];
    int mode8[4][NUM_KEYS];
    int active[4][NUM_KEYS];
    JoyData joy[4];
    DirectionGraceState direction_grace[4];
    bool touch_keystate[4][NUM_KEYS];

    FullControlSnapshotGuard()
    {
        InputHardwareState& hw = input_hardware_state();
        for (int p = 0; p < 4; ++p)
        {
            modes[p] = get_player_control_mode(p);
            default_profiles[p] =
                hw.player_control_default_profiles[p];
            joy[p] = player_joy[p];
            direction_grace[p] = hw.direction_grace[p];
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                mode4[p][k] = get_player_key_binding_for_mode(
                    p, static_cast<int>(ControlDirectionMode::FourDirection), k);
                mode8[p][k] = get_player_key_binding_for_mode(
                    p, static_cast<int>(ControlDirectionMode::EightDirection), k);
                active[p][k] =
                    og::runtime::current_session->player_keys_[p][k];
                touch_keystate[p][k] = hw.touch_keystate[p][k];
            }
        }
    }

    void restore()
    {
        InputHardwareState& hw = input_hardware_state();
        for (int p = 0; p < 4; ++p)
        {
            hw.player_control_default_profiles[p] =
                default_profiles[p];
            set_player_control_mode(p, static_cast<int>(ControlDirectionMode::FourDirection));
            for (int k = 0; k < NUM_KEYS; ++k)
                set_player_key_binding(p, k, mode4[p][k]);
            set_player_control_mode(p, static_cast<int>(ControlDirectionMode::EightDirection));
            for (int k = 0; k < NUM_KEYS; ++k)
                set_player_key_binding(p, k, mode8[p][k]);
            set_player_control_mode(p, modes[p]);
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                og::runtime::current_session->player_keys_[p][k] =
                    active[p][k];
            }
            player_joy[p] = joy[p];
            hw.direction_grace[p] = direction_grace[p];
            for (int k = 0; k < NUM_KEYS; ++k)
                hw.touch_keystate[p][k] = touch_keystate[p][k];
        }
    }

    ~FullControlSnapshotGuard() { restore(); }
};
} // namespace

TEST(InputKeybinds, input_isPlayerHoldingKey_uses_keyboard_state_when_no_joystick_mapping)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_FIRE, SDLK_V);

    const SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_V, nullptr);
    KeyStateGuard ks(sc);

    ks.set(false);
    ASSERT_TRUE(!isPlayerHoldingKey(0, KEY_FIRE)) << "unpressed key should not be held";

    ks.set(true);
    ASSERT_TRUE(isPlayerHoldingKey(0, KEY_FIRE)) << "pressed key should be held";
}


namespace
{
// Zeroes the web-touch-overlay held-key seam on scope exit so a failing
// assertion cannot leak held keys into later tests.
struct TouchKeystateGuard
{
    ~TouchKeystateGuard()
    {
        InputHardwareState& hw = input_hardware_state();
        for (int p = 0; p < 4; ++p)
            for (int k = 0; k < NUM_KEYS; ++k)
                hw.touch_keystate[p][k] = false;
    }
};
} // namespace

TEST(InputKeybinds, input_touch_keystate_registers_as_held_and_clears_without_leaking)
{
    // The web DOM touch overlay writes InputHardwareState::touch_keystate
    // (via openglad_web_touch_set_key); isPlayerHoldingKey must OR it in
    // without any SDL keyboard state, and native behavior must be unchanged
    // when the seam is all-false.
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_FIRE, SDLK_V);
    KeyStateGuard ks(SDL_GetScancodeFromKey(SDLK_V, nullptr));
    ks.set(false);
    TouchKeystateGuard touch_guard;
    InputHardwareState& hw = input_hardware_state();

    ASSERT_TRUE(!isPlayerHoldingKey(0, KEY_FIRE)) << "no keyboard, no touch: not held";

    hw.touch_keystate[0][KEY_FIRE] = true;
    ASSERT_TRUE(isPlayerHoldingKey(0, KEY_FIRE))
        << "touch-held key should register as held without SDL keyboard state";
    ASSERT_TRUE(!isPlayerHoldingKey(1, KEY_FIRE))
        << "touch hold for player 0 must not bleed into other players";

    // Touch is additive, not exclusive: the keyboard path still works while
    // the seam holds a different state.
    ks.set(true);
    hw.touch_keystate[0][KEY_FIRE] = false;
    ASSERT_TRUE(isPlayerHoldingKey(0, KEY_FIRE)) << "keyboard hold should still register";
    ks.set(false);

    ASSERT_TRUE(!isPlayerHoldingKey(0, KEY_FIRE))
        << "cleared touch key must not leak a held state";
}


TEST(InputKeybinds, input_touch_keystate_feeds_input_state_held_and_pressed_edges)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_FIRE, SDLK_UNKNOWN);
    TouchKeystateGuard touch_guard;
    InputHardwareState& hw = input_hardware_state();

    InputState input{};
    input.clear();

    hw.touch_keystate[0][KEY_FIRE] = true;
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_FIRE]) << "touch hold should reach InputState held";
    ASSERT_TRUE(input.players[0].pressed[KEY_FIRE]) << "first sample should carry a press edge";
    ASSERT_TRUE(!input.players[1].held[KEY_FIRE]) << "other players must stay untouched";

    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_FIRE]) << "still held on the second sample";
    ASSERT_TRUE(!input.players[0].pressed[KEY_FIRE]) << "no repeated press edge while held";

    hw.touch_keystate[0][KEY_FIRE] = false;
    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_FIRE]) << "release must clear held state";
    ASSERT_TRUE(!input.players[0].pressed[KEY_FIRE]) << "release must not press";
}


TEST(InputKeybinds, input_touch_diagonals_survive_four_direction_mode_resets)
{
    // Keyboard/joystick diagonal bindings remain a player control-mode choice,
    // but the touch overlay's 8-way stick must keep working after settings or
    // Restore Defaults puts player 0 back in FourDirection mode.
    disablePlayerJoystick(0);
    FullControlSnapshotGuard controls_guard;
    TouchKeystateGuard touch_guard;
    InputHardwareState& hw = input_hardware_state();

    InputState input{};
    input.clear();

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    hw.touch_keystate[0][KEY_UP_RIGHT] = true;
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP_RIGHT])
        << "4-direction mode must not suppress the intrinsically 8-way touch stick";

    reset_default_player_controls();
    ASSERT_EQ(static_cast<int>(ControlDirectionMode::FourDirection),
              get_player_control_mode(0));
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP_RIGHT])
        << "a controls lifecycle reset must not disable touch diagonals";

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP_RIGHT])
        << "8-direction mode should deliver the touch-held diagonal";

    hw.touch_keystate[0][KEY_UP_RIGHT] = false;
    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_UP_RIGHT]) << "cleared diagonal must not leak";
}


TEST(InputKeybinds, native_input_push_text_event_round_trips_through_the_queue)
{
    // The web text-entry overlay delivers typed characters through
    // push_text_event; SDL3 text events borrow their string, so the payload
    // must survive until decode_event copies it at poll time.
    while (og::input_native::poll_event() != nullptr) {}

    og::input_native::push_text_event("ab");
    og::input_native::push_text_event("cd");
    og::input_native::push_text_event("ef");

    const char* expected[] = {"ab", "cd", "ef"};
    for (const char* want : expected)
    {
        const void* ev = og::input_native::wait_event();
        ASSERT_TRUE(ev != nullptr) << "queued text event should be delivered";
        og::input_native::EventData out{};
        ASSERT_TRUE(og::input_native::decode_event(ev, out)) << "text event should decode";
        ASSERT_EQ((int)og::input_native::EventType::TextInput, (int)out.type)
            << "queued text event should remain text input";
        ASSERT_STREQ(want, out.text.data())
            << "text payload should survive the static ring until poll";
    }

    while (og::input_native::poll_event() != nullptr) {}
}


TEST(InputKeybinds, native_input_push_text_cancel_key_requires_active_text_input)
{
    while (og::input_native::poll_event() != nullptr) {}

    // Outside a text prompt the cancel seam must push nothing: a stray
    // Escape here would back out of whatever menu is active.
    og::input_native::stop_text_input();
    og::input_native::push_text_cancel_key();
    ASSERT_EQ(nullptr, og::input_native::poll_event())
        << "cancel outside text input should push no events";

    // During a text prompt it pushes the Escape down+up pair that
    // input_string's cancel path (restore original, return null) consumes.
    og::input_native::start_text_input();
    og::input_native::push_text_cancel_key();
    // A double tap/click must not queue another Escape that leaks into the
    // menu after the prompt has already closed.
    og::input_native::push_text_cancel_key();
    og::input_native::stop_text_input();

    og::input_native::EventData out{};
    const void* down_event = og::input_native::wait_event();
    ASSERT_TRUE(down_event != nullptr) << "cancel should push a keydown";
    ASSERT_TRUE(og::input_native::decode_event(down_event, out))
        << "pushed cancel keydown should decode";
    ASSERT_EQ((int)og::input_native::EventType::KeyDown, (int)out.type)
        << "first pushed cancel event should be keydown";
    ASSERT_EQ((int)SDLK_ESCAPE, out.key_sym)
        << "cancel keydown should carry Escape";

    const void* up_event = og::input_native::wait_event();
    ASSERT_TRUE(up_event != nullptr) << "cancel should push a keyup";
    ASSERT_TRUE(og::input_native::decode_event(up_event, out))
        << "pushed cancel keyup should decode";
    ASSERT_EQ((int)og::input_native::EventType::KeyUp, (int)out.type)
        << "second pushed cancel event should be keyup";
    ASSERT_EQ((int)SDLK_ESCAPE, out.key_sym)
        << "cancel keyup should carry Escape";
    ASSERT_EQ(nullptr, og::input_native::poll_event())
        << "duplicate cancel requests should be debounced";

    while (og::input_native::poll_event() != nullptr) {}
}

TEST(InputKeybinds, input_didPlayerPressKey_matches_keydown_and_ignores_repeats)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_SPECIAL, SDLK_X);

    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_X;
    e.key.repeat = 0;
    ASSERT_TRUE(didPlayerPressKey(0, KEY_SPECIAL, e)) << "matching keydown should be recognized";

    e.key.repeat = 1;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "repeat keydown should be ignored";

    e.key.repeat = 0;
    e.key.key = SDLK_Y;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "non-matching keydown should be ignored";

    e.type = SDL_EVENT_KEY_UP;
    e.key.key = SDLK_X;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "keyup should not be treated as press";
}


TEST(InputKeybinds, input_didPlayerReleaseKey_matches_keyup)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(1, KEY_YELL, SDLK_Q);

    SDL_Event e{};
    e.type = SDL_EVENT_KEY_UP;
    e.key.key = SDLK_Q;
    ASSERT_TRUE(didPlayerReleaseKey(1, KEY_YELL, e)) << "matching keyup should be recognized";

    e.key.key = SDLK_W;
    ASSERT_TRUE(!didPlayerReleaseKey(1, KEY_YELL, e)) << "non-matching keyup should be ignored";

    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_Q;
    ASSERT_TRUE(!didPlayerReleaseKey(1, KEY_YELL, e)) << "keydown should not be treated as release";
}


TEST(InputKeybinds, input_key_binding_helpers_isAnyPlayerKey_and_isPlayerKey)
{
    KeyBindingGuard bind0(2, KEY_SWITCH, SDLK_KP_0);
    KeyBindingGuard bind1(3, KEY_SWITCH, SDLK_KP_1);

    ASSERT_TRUE(isAnyPlayerKey(SDLK_KP_0)) << "isAnyPlayerKey should find mapped key";
    ASSERT_TRUE(isPlayerKey(2, SDLK_KP_0)) << "isPlayerKey should find mapping for specific player";
    ASSERT_TRUE(!isPlayerKey(0, SDLK_KP_0)) << "isPlayerKey should not report mapping for other player";

    ASSERT_TRUE(isAnyPlayerKey(SDLK_KP_1)) << "isAnyPlayerKey should find second mapped key";
    ASSERT_TRUE(isPlayerKey(3, SDLK_KP_1)) << "isPlayerKey should find mapping for specific player";
}


TEST(InputKeybinds, input_wait_for_key_event_returns_fake_escape_in_test_mode)
{
    const SDL_Event& e = *static_cast<const SDL_Event*>(wait_for_key_event());
    ASSERT_EQ((int)SDL_EVENT_KEY_DOWN, (int)e.type) << "wait_for_key_event should return keydown in test mode";
    ASSERT_EQ((int)SDLK_ESCAPE, (int)e.key.key) << "wait_for_key_event should return escape in test mode";
}


TEST(InputKeybinds, native_input_decode_event_ignores_malformed_scancode_payload)
{
    og::input_native::EventData null_out{};
    ASSERT_TRUE(!og::input_native::decode_event(nullptr, null_out))
        << "decode_event should reject null native event pointers";

    SDL_Event e{};
    std::memset(&e, 0x01, sizeof(e));
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_Q;
    e.key.repeat = 0;

    og::input_native::EventData out{};
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept keydown payloads";
    ASSERT_EQ((int)og::input_native::EventType::KeyDown, (int)out.type) << "decoded event type should be keydown";
    ASSERT_EQ((int)SDLK_Q, out.key_sym) << "decoded key symbol should match payload";
    ASSERT_EQ((int)SDL_GetScancodeFromKey(SDLK_Q, nullptr), out.key_scancode) << "decoded scancode should derive from key symbol, not raw enum payload";
}


TEST(InputKeybinds, native_input_decode_event_covers_non_keyboard_variants)
{
    SDL_Event e{};
    og::input_native::EventData out{};

    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = "abc";
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept text input";
    ASSERT_EQ((int)og::input_native::EventType::TextInput, (int)out.type) << "text input type should decode";
    ASSERT_STREQ("abc", out.text.data()) << "text payload should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = -3;
    e.wheel.integer_y = -3;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept mouse wheel";
    ASSERT_EQ((int)og::input_native::EventType::MouseWheel, (int)out.type) << "mouse wheel type should decode";
    ASSERT_EQ(-3, out.wheel_y) << "mouse wheel delta should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = 12;
    e.motion.y = 34;
    e.motion.xrel = -5;
    e.motion.yrel = 6;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept mouse motion";
    ASSERT_EQ((int)og::input_native::EventType::MouseMotion, (int)out.type) << "mouse motion type should decode";
    ASSERT_EQ(12, out.motion_x) << "mouse motion x should decode";
    ASSERT_EQ(34, out.motion_y) << "mouse motion y should decode";
    ASSERT_EQ(-5, out.motion_dx) << "mouse motion dx should decode";
    ASSERT_EQ(6, out.motion_dy) << "mouse motion dy should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = 44;
    e.button.y = 55;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept mouse button events";
    ASSERT_EQ((int)og::input_native::EventType::MouseButtonDown, (int)out.type) << "mouse button type should decode";
    ASSERT_EQ((int)SDL_BUTTON_LEFT, out.button) << "mouse button should decode";
    ASSERT_EQ(44, out.button_x) << "mouse button x should decode";
    ASSERT_EQ(55, out.button_y) << "mouse button y should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_FINGER_DOWN;
    e.tfinger.x = 0.25f;
    e.tfinger.y = 0.5f;
    e.tfinger.dx = 0.1f;
    e.tfinger.dy = -0.2f;
    e.tfinger.fingerID = 77;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept finger events";
    ASSERT_EQ((int)og::input_native::EventType::FingerDown, (int)out.type) << "finger type should decode";
    ASSERT_FLOAT_EQ(0.25f, out.finger_x) << "finger x should decode";
    ASSERT_FLOAT_EQ(0.5f, out.finger_y) << "finger y should decode";
    ASSERT_FLOAT_EQ(0.1f, out.finger_dx) << "finger dx should decode";
    ASSERT_FLOAT_EQ(-0.2f, out.finger_dy) << "finger dy should decode";
    ASSERT_EQ(77, out.finger_id) << "finger id should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    e.jaxis.which = 2;
    e.jaxis.axis = 1;
    e.jaxis.value = 12000;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept joystick axis events";
    ASSERT_EQ((int)og::input_native::EventType::JoyAxisMotion, (int)out.type) << "joy axis type should decode";
    ASSERT_EQ(2, out.joy_axis_which) << "joy axis joystick should decode";
    ASSERT_EQ(1, out.joy_axis_axis) << "joy axis index should decode";
    ASSERT_EQ(12000, out.joy_axis_value) << "joy axis value should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = 3;
    e.jbutton.button = 4;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept joystick button events";
    ASSERT_EQ((int)og::input_native::EventType::JoyButtonDown, (int)out.type) << "joy button type should decode";
    ASSERT_EQ(3, out.joy_button_which) << "joy button joystick should decode";
    ASSERT_EQ(4, out.joy_button_button) << "joy button index should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    e.jhat.which = 5;
    e.jhat.hat = 1;
    e.jhat.value = SDL_HAT_RIGHT;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept joystick hat events";
    ASSERT_EQ((int)og::input_native::EventType::JoyHatMotion, (int)out.type) << "joy hat type should decode";
    ASSERT_EQ(5, out.joy_hat_which) << "joy hat joystick should decode";
    ASSERT_EQ(1, out.joy_hat_hat) << "joy hat index should decode";
    ASSERT_EQ((int)SDL_HAT_RIGHT, out.joy_hat_value) << "joy hat value should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_WINDOW_MINIMIZED;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window minimize";
    ASSERT_EQ((int)og::input_native::WindowEventType::Minimized, (int)out.window_event) << "window minimize should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_WINDOW_RESIZED;
    e.window.data1 = 640;
    e.window.data2 = 400;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window events";
    ASSERT_EQ((int)og::input_native::EventType::Window, (int)out.type) << "window type should decode";
    ASSERT_EQ((int)og::input_native::WindowEventType::Resized, (int)out.window_event) << "window resize should decode";
    ASSERT_EQ(640, out.window_data1) << "window data1 should decode";
    ASSERT_EQ(400, out.window_data2) << "window data2 should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window close";
    ASSERT_EQ((int)og::input_native::WindowEventType::Close, (int)out.window_event) << "window close should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_WINDOW_RESTORED;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window restored";
    ASSERT_EQ((int)og::input_native::WindowEventType::Restored, (int)out.window_event) << "window restore should decode";

    e = SDL_Event{};
    e.type = SDL_EVENT_WINDOW_OCCLUDED; // in the window-event range but unmapped by decode
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept unknown window events";
    ASSERT_EQ((int)og::input_native::WindowEventType::Unknown, (int)out.window_event) << "unknown window event should map to Unknown";

    e = SDL_Event{};
    e.type = SDL_EVENT_USER;
    e.user.code = 42;
    e.user.data1 = reinterpret_cast<void*>(static_cast<std::intptr_t>(1234));
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept user events";
    ASSERT_EQ(42, out.user_code) << "user event code should decode";
    ASSERT_EQ(1234, out.user_data1) << "user event payload should decode";

    e = SDL_Event{};
    e.type = 0x7fffffff;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept unknown raw event types";
    ASSERT_EQ((int)og::input_native::EventType::Unknown, (int)out.type) << "unknown raw event should map to Unknown";
}


TEST(InputKeybinds, native_input_push_helpers_and_wrappers_smoke)
{
    while (og::input_native::poll_event() != nullptr) {}

    const bool* const native_keys_before =
        og::input_native::keyboard_state();
    ASSERT_NE(nullptr, native_keys_before);
    int native_key_count = 0;
    const bool* const sdl_keys_before =
        SDL_GetKeyboardState(&native_key_count);
    ASSERT_EQ(sdl_keys_before, native_keys_before);
    ASSERT_GE(native_key_count, SDL_SCANCODE_COUNT);
    std::array<bool, SDL_SCANCODE_COUNT> native_key_snapshot{};
    std::copy_n(native_keys_before, native_key_snapshot.size(),
                native_key_snapshot.begin());

    og::input_native::set_virtual_back_key(true);
    const bool* const native_keys_while_set =
        og::input_native::keyboard_state();
    ASSERT_NE(nullptr, native_keys_while_set);
    EXPECT_TRUE(std::equal(native_key_snapshot.begin(),
                           native_key_snapshot.end(),
                           native_keys_while_set))
        << "the web-only virtual BACK state must not alter native keys";
    EXPECT_EQ(nullptr, og::input_native::poll_event())
        << "the native no-op must not synthesize an input event";

    og::input_native::set_virtual_back_key(false);
    const bool* const native_keys_after =
        og::input_native::keyboard_state();
    ASSERT_NE(nullptr, native_keys_after);
    EXPECT_TRUE(std::equal(native_key_snapshot.begin(),
                           native_key_snapshot.end(), native_keys_after));
    EXPECT_EQ(nullptr, og::input_native::poll_event());

    const std::uint32_t ticks_before = og::input_native::ticks_ms();
    ASSERT_TRUE(og::input_native::keyboard_state() != nullptr) << "keyboard_state should return SDL state buffer";
    ASSERT_TRUE(og::input_native::scancode_from_key(SDLK_Q) >= 0) << "scancode lookup should succeed";
    ASSERT_TRUE(og::input_native::key_name(SDLK_Q) != nullptr) << "key_name should return a string";
    ASSERT_TRUE(og::input_native::num_joysticks() >= 0) << "num_joysticks should be non-negative";
    ASSERT_EQ(nullptr, og::input_native::joystick_open(-1)) << "opening an invalid joystick should fail cleanly";
    ASSERT_LE(og::input_native::joystick_num_axes(nullptr), 0) << "null joystick should not report axes";
    ASSERT_LE(og::input_native::joystick_num_buttons(nullptr), 0) << "null joystick should not report buttons";
    ASSERT_LE(og::input_native::joystick_num_hats(nullptr), 0) << "null joystick should not report hats";
    ASSERT_EQ(0, og::input_native::joystick_get_axis(nullptr, 0)) << "null joystick axis read should be neutral";
    ASSERT_EQ(0, og::input_native::joystick_get_button(nullptr, 0)) << "null joystick button read should be unpressed";
    ASSERT_EQ(0, og::input_native::joystick_get_hat(nullptr, 0)) << "null joystick hat read should be centered";

    const bool joy_before = og::input_native::joystick_subsystem_initialized();
    og::input_native::joystick_init_subsystem();
    ASSERT_TRUE(og::input_native::joystick_subsystem_initialized()) << "joystick_init_subsystem should enable the subsystem";
    og::input_native::joystick_set_event_state(true);

    og::input_native::show_cursor(true);
    og::input_native::show_cursor(false);
    og::input_native::start_text_input();
    og::input_native::stop_text_input();
    og::input_native::sleep_ms(1);
    ASSERT_TRUE(og::input_native::ticks_ms() >= ticks_before) << "ticks_ms should advance monotonically";

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_key_event(true, SDLK_Q);
    const void* key_event = og::input_native::wait_event();
    ASSERT_TRUE(key_event != nullptr) << "wait_event should return the queued key event";
    og::input_native::EventData out{};
    ASSERT_TRUE(og::input_native::decode_event(key_event, out)) << "queued key event should decode";
    ASSERT_EQ((int)og::input_native::EventType::KeyDown, (int)out.type) << "queued key event should remain keydown";

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_mouse_button_event(true, SDL_BUTTON_LEFT, 12, 34);
    const void* mouse_event = og::input_native::wait_event();
    ASSERT_TRUE(mouse_event != nullptr) << "wait_event should return the queued mouse event";
    ASSERT_TRUE(og::input_native::decode_event(mouse_event, out)) << "queued mouse event should decode";
    ASSERT_EQ((int)og::input_native::EventType::MouseButtonDown, (int)out.type) << "queued mouse event should remain mouse button down";

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_mouse_button_event_css(
        false, SDL_BUTTON_RIGHT, 17, 19, 0, 0);
    const void* css_mouse_event = og::input_native::wait_event();
    ASSERT_NE(nullptr, css_mouse_event);
    ASSERT_TRUE(og::input_native::decode_event(css_mouse_event, out));
    EXPECT_EQ(og::input_native::EventType::MouseButtonUp, out.type);
    EXPECT_EQ(SDL_BUTTON_RIGHT, out.button);
    EXPECT_EQ(17, out.button_x);
    EXPECT_EQ(19, out.button_y);

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_touch_event(og::input_native::EventType::FingerDown, 0.25f, 0.5f, 0.1f, -0.2f, 77);
    const void* touch_event = og::input_native::wait_event();
    ASSERT_TRUE(touch_event != nullptr) << "wait_event should return the queued touch event";
    ASSERT_TRUE(og::input_native::decode_event(touch_event, out)) << "queued touch event should decode";
    ASSERT_EQ((int)og::input_native::EventType::FingerDown, (int)out.type) << "queued touch event should remain finger down";

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_touch_event(og::input_native::EventType::FingerMotion, 0.3f, 0.6f, 0.2f, 0.1f, 78);
    const void* touch_motion_event = og::input_native::wait_event();
    ASSERT_TRUE(touch_motion_event != nullptr) << "wait_event should return the queued touch motion";
    ASSERT_TRUE(og::input_native::decode_event(touch_motion_event, out)) << "queued touch motion should decode";
    ASSERT_EQ((int)og::input_native::EventType::FingerMotion, (int)out.type) << "queued touch motion should remain finger motion";

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_touch_event(og::input_native::EventType::FingerUp, 0.4f, 0.7f, -0.1f, -0.2f, 79);
    const void* touch_up_event = og::input_native::wait_event();
    ASSERT_TRUE(touch_up_event != nullptr) << "wait_event should return the queued touch up";
    ASSERT_TRUE(og::input_native::decode_event(touch_up_event, out)) << "queued touch up should decode";
    ASSERT_EQ((int)og::input_native::EventType::FingerUp, (int)out.type) << "queued touch up should remain finger up";

    while (og::input_native::poll_event() != nullptr) {}
    og::input_native::push_touch_event(og::input_native::EventType::Unknown, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    ASSERT_EQ(nullptr, og::input_native::poll_event()) << "invalid touch helper type should not queue an event";

    if (!joy_before)
        og::input_native::joystick_quit_subsystem();
}


TEST(InputKeybinds, input_state_from_sdl_respects_four_direction_mode)
{
    disablePlayerJoystick(0);
    ModeKeyBindingGuard bind_diag(0, KEY_UP_RIGHT);
    KeyStateGuard ks(SDL_GetScancodeFromKey(SDLK_V, nullptr));
    ControlModeGuard mode_guard(0);

    InputState input{};
    input.clear();

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_V);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    ks.set(true);
    input_state_from_sdl(input);
    ASSERT_TRUE(!input.players[0].held[KEY_UP_RIGHT]) << "4-direction should suppress held diagonal input";
    ASSERT_TRUE(!input.players[0].pressed[KEY_UP_RIGHT]) << "4-direction should suppress pressed diagonal input";

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    input_state_from_sdl(input);
    ASSERT_TRUE(input.players[0].held[KEY_UP_RIGHT]) << "8-direction should keep held diagonal input";
    ASSERT_TRUE(input.players[0].pressed[KEY_UP_RIGHT]) << "8-direction should allow pressed diagonal edges";
}


TEST(InputKeybinds, input_mode_key_binding_updates_are_isolated_by_control_mode)
{
    ModeKeyBindingGuard bind_guard(0, KEY_UP);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_1);
    ASSERT_EQ(static_cast<int>(SDLK_1), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "4-direction keymap should update in 4-direction mode";

    const int eight_before = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    ASSERT_EQ(eight_before, og::runtime::current_session->player_keys_[0][KEY_UP]) << "switching to 8-direction should restore that mode's key binding";
    set_player_key_binding(0, KEY_UP, SDLK_2);

    ASSERT_EQ(static_cast<int>(SDLK_1), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "editing in 8-direction mode must not mutate 4-direction binding";
    ASSERT_EQ(static_cast<int>(SDLK_2), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "8-direction binding should update when editing in 8-direction mode";

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    ASSERT_EQ(static_cast<int>(SDLK_1), og::runtime::current_session->player_keys_[0][KEY_UP]) << "switching back to 4-direction should restore 4-direction binding";
}


TEST(InputKeybinds, reset_default_controls_for_player_restores_only_that_player)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);
    int expected_mode4[NUM_KEYS];
    int expected_mode8[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        expected_mode4[k] = get_player_key_binding_for_mode(1, kFour, k);
        expected_mode8[k] = get_player_key_binding_for_mode(1, kEight, k);
    }

    set_player_control_mode(1, kFour);
    set_player_key_binding(1, KEY_UP, SDLK_F1);
    set_player_control_mode(1, kEight);
    set_player_key_binding(1, KEY_UP_RIGHT, SDLK_F2);

    // Keep another player deliberately non-default so a per-player reset
    // cannot pass by falling back to the existing reset-all behavior.
    set_player_control_mode(0, kFour);
    set_player_key_binding(0, KEY_FIRE, SDLK_F3);
    set_player_control_mode(0, kEight);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_F4);
    int untouched_mode4[NUM_KEYS];
    int untouched_mode8[NUM_KEYS];
    int untouched_active[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        untouched_mode4[k] = get_player_key_binding_for_mode(0, kFour, k);
        untouched_mode8[k] = get_player_key_binding_for_mode(0, kEight, k);
        untouched_active[k] = og::runtime::current_session->player_keys_[0][k];
    }

    ASSERT_TRUE(reset_default_player_controls_for_player(1));
    ASSERT_EQ(kFour, get_player_control_mode(1));
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        ASSERT_EQ(expected_mode4[k], get_player_key_binding_for_mode(1, kFour, k))
            << "P2 4-direction key " << k << " should return to its default";
        ASSERT_EQ(expected_mode8[k], get_player_key_binding_for_mode(1, kEight, k))
            << "P2 8-direction key " << k << " should return to its default";
        ASSERT_EQ(expected_mode4[k], og::runtime::current_session->player_keys_[1][k])
            << "P2 active key " << k << " should carry the default mode map";

        ASSERT_EQ(untouched_mode4[k], get_player_key_binding_for_mode(0, kFour, k))
            << "P1 4-direction key " << k << " must remain untouched";
        ASSERT_EQ(untouched_mode8[k], get_player_key_binding_for_mode(0, kEight, k))
            << "P1 8-direction key " << k << " must remain untouched";
        ASSERT_EQ(untouched_active[k], og::runtime::current_session->player_keys_[0][k])
            << "P1 active key " << k << " must remain untouched";
    }
    ASSERT_EQ(kEight, get_player_control_mode(0));
}


TEST(InputKeybinds, reset_default_controls_for_player_rejects_invalid_index)
{
    FullControlSnapshotGuard guard;
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_F5);

    const int mode_before = get_player_control_mode(0);
    const int binding_before = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT);
    const int active_before = og::runtime::current_session->player_keys_[0][KEY_UP_RIGHT];

    ASSERT_TRUE(!reset_default_player_controls_for_player(-1));
    ASSERT_TRUE(!reset_default_player_controls_for_player(4));
    ASSERT_EQ(mode_before, get_player_control_mode(0));
    ASSERT_EQ(binding_before, get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT));
    ASSERT_EQ(active_before, og::runtime::current_session->player_keys_[0][KEY_UP_RIGHT]);
}


TEST(InputKeybinds, compact_controls_after_middle_player_removal_preserves_profiles)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);

    // The removed Player 2 profile is intentionally customized. It belongs
    // in the inactive tail after compaction, ready for a later added seat.
    set_player_control_mode(1, kFour);
    set_player_key_binding(1, KEY_DOWN, SDLK_F8);
    set_player_control_mode(1, kEight);
    set_player_key_binding(1, KEY_UP_LEFT, SDLK_F9);
    og::runtime::current_session->player_keys_[1][KEY_FIRE] = SDLK_F10;
    int removed_mode4[NUM_KEYS];
    int removed_mode8[NUM_KEYS];
    int removed_active[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        removed_mode4[k] = get_player_key_binding_for_mode(1, kFour, k);
        removed_mode8[k] = get_player_key_binding_for_mode(1, kEight, k);
        removed_active[k] =
            og::runtime::current_session->player_keys_[1][k];
    }
    removed_mode8[KEY_FIRE] = SDLK_F10;

    // Player 1 precedes the removed profile and must not move or reset.
    set_player_control_mode(0, kFour);
    set_player_key_binding(0, KEY_UP, SDLK_F1);
    set_player_control_mode(0, kEight);
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_F2);
    int earlier_mode4[NUM_KEYS];
    int earlier_mode8[NUM_KEYS];
    int earlier_active[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        earlier_mode4[k] = get_player_key_binding_for_mode(0, kFour, k);
        earlier_mode8[k] = get_player_key_binding_for_mode(0, kEight, k);
        earlier_active[k] = og::runtime::current_session->player_keys_[0][k];
    }

    // Player 3 is the immediate successor of the removed Player 2 profile.
    set_player_control_mode(2, kFour);
    set_player_key_binding(2, KEY_LEFT, SDLK_F3);
    set_player_control_mode(2, kEight);
    set_player_key_binding(2, KEY_DOWN_LEFT, SDLK_F4);
    og::runtime::current_session->player_keys_[2][KEY_FIRE] = SDLK_F5;
    int successor_mode4[NUM_KEYS];
    int successor_mode8[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        successor_mode4[k] = get_player_key_binding_for_mode(2, kFour, k);
        successor_mode8[k] = get_player_key_binding_for_mode(2, kEight, k);
    }
    successor_mode8[KEY_FIRE] = SDLK_F5;

    // Player 4 must also shift, proving the whole active suffix compacts.
    set_player_control_mode(3, kFour);
    set_player_key_binding(3, KEY_YELL, SDLK_F6);
    set_player_control_mode(3, kEight);
    set_player_key_binding(3, KEY_SPECIAL, SDLK_F7);
    int final_successor_mode4[NUM_KEYS];
    int final_successor_mode8[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        final_successor_mode4[k] = get_player_key_binding_for_mode(3, kFour, k);
        final_successor_mode8[k] = get_player_key_binding_for_mode(3, kEight, k);
    }

    for (int p = 0; p < 4; ++p)
    {
        player_joy[p].index = 40 + p;
        player_joy[p].key_type[KEY_FIRE] = JoyData::BUTTON;
        player_joy[p].key_index[KEY_FIRE] = 10 + p;
    }

    ASSERT_TRUE(compact_player_controls_after_removal(1, 4));

    ASSERT_EQ(kEight, get_player_control_mode(1));
    ASSERT_EQ(kEight, get_player_control_mode(2));
    ASSERT_EQ(kEight, get_player_control_mode(3));
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        ASSERT_EQ(earlier_mode4[k], get_player_key_binding_for_mode(0, kFour, k));
        ASSERT_EQ(earlier_mode8[k], get_player_key_binding_for_mode(0, kEight, k));
        ASSERT_EQ(earlier_active[k], og::runtime::current_session->player_keys_[0][k]);

        ASSERT_EQ(successor_mode4[k], get_player_key_binding_for_mode(1, kFour, k))
            << "the clicked middle seat's successor must retain 4-dir key " << k;
        ASSERT_EQ(successor_mode8[k], get_player_key_binding_for_mode(1, kEight, k))
            << "the clicked middle seat's successor must retain 8-dir key " << k;
        ASSERT_EQ(successor_mode8[k], og::runtime::current_session->player_keys_[1][k])
            << "the shifted successor must activate its retained mode";

        ASSERT_EQ(final_successor_mode4[k], get_player_key_binding_for_mode(2, kFour, k));
        ASSERT_EQ(final_successor_mode8[k], get_player_key_binding_for_mode(2, kEight, k));
        ASSERT_EQ(final_successor_mode8[k], og::runtime::current_session->player_keys_[2][k]);

        ASSERT_EQ(removed_mode4[k], get_player_key_binding_for_mode(3, kFour, k))
            << "the freed profile must retain its 4-dir key " << k;
        ASSERT_EQ(removed_mode8[k], get_player_key_binding_for_mode(3, kEight, k))
            << "the freed profile must retain its 8-dir key " << k;
        ASSERT_EQ(removed_active[k], og::runtime::current_session->player_keys_[3][k])
            << "the freed profile must retain and activate its mode";
    }
    EXPECT_EQ(40, player_joy[0].index);
    EXPECT_EQ(42, player_joy[1].index);
    EXPECT_EQ(43, player_joy[2].index);
    EXPECT_EQ(41, player_joy[3].index);
    EXPECT_EQ(11, player_joy[3].key_index[KEY_FIRE]);
    EXPECT_EQ(0, input_hardware_state()
                     .player_control_default_profiles[0]);
    EXPECT_EQ(2, input_hardware_state()
                     .player_control_default_profiles[1]);
    EXPECT_EQ(3, input_hardware_state()
                     .player_control_default_profiles[2]);
    EXPECT_EQ(1, input_hardware_state()
                     .player_control_default_profiles[3]);
    ASSERT_EQ(kEight, get_player_control_mode(0));
}

TEST(InputKeybinds, compact_controls_after_first_of_four_keeps_readded_mapping_unique)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour =
        static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight =
        static_cast<int>(ControlDirectionMode::EightDirection);
    InputHardwareState& hw = input_hardware_state();
    for (int p = 0; p < 4; ++p)
    {
        player_joy[p].index = 20 + p;
        player_joy[p].key_type[KEY_YELL] = JoyData::BUTTON;
        player_joy[p].key_index[KEY_YELL] = p;
        hw.direction_grace[p] = {
            .prev_raw = 3, .hold_mask = 3, .ticks_left = 2};
        for (int k = 0; k < NUM_KEYS; ++k)
            hw.touch_keystate[p][k] = true;
    }

    ASSERT_TRUE(compact_player_controls_after_removal(0, 4));

    // Simulating the next [+] simply makes the inactive tail active again:
    // arrows / IJKL / THGF / WASD, never two THGF profiles.
    constexpr int expected_up[4] = {SDLK_UP, SDLK_I, SDLK_T, SDLK_W};
    constexpr int expected_right[4] = {
        SDLK_RIGHT, SDLK_L, SDLK_H, SDLK_D};
    constexpr int expected_joy[4] = {21, 22, 23, 20};
    for (int p = 0; p < 4; ++p)
    {
        EXPECT_EQ(expected_up[p],
                  get_player_key_binding_for_mode(p, kFour, KEY_UP));
        EXPECT_EQ(expected_up[p],
                  get_player_key_binding_for_mode(p, kEight, KEY_UP));
        EXPECT_EQ(expected_right[p],
                  get_player_key_binding_for_mode(p, kFour, KEY_RIGHT));
        EXPECT_EQ(expected_up[p],
                  og::runtime::current_session->player_keys_[p][KEY_UP]);
        EXPECT_EQ(expected_joy[p], player_joy[p].index);
        EXPECT_EQ(
            (p + 1) % 4,
            hw.player_control_default_profiles[p]);
        EXPECT_EQ(0, hw.direction_grace[p].prev_raw);
        EXPECT_EQ(0, hw.direction_grace[p].hold_mask);
        EXPECT_EQ(0, hw.direction_grace[p].ticks_left);
        for (int k = 0; k < NUM_KEYS; ++k)
            EXPECT_FALSE(hw.touch_keystate[p][k]);
    }
    EXPECT_NE(
        get_player_key_binding_for_mode(2, kFour, KEY_UP),
        get_player_key_binding_for_mode(3, kFour, KEY_UP));
}

TEST(InputKeybinds, compact_controls_after_repeated_removals_preserves_profile_pool)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour =
        static_cast<int>(ControlDirectionMode::FourDirection);
    ASSERT_TRUE(compact_player_controls_after_removal(0, 4));
    ASSERT_TRUE(compact_player_controls_after_removal(0, 3));

    // Two removals followed by two adds expose the whole unchanged pool.
    // A positional tail reset would produce IJKL twice here.
    constexpr int expected_up[4] = {SDLK_I, SDLK_T, SDLK_UP, SDLK_W};
    for (int p = 0; p < 4; ++p)
    {
        EXPECT_EQ(expected_up[p],
                  get_player_key_binding_for_mode(p, kFour, KEY_UP));
    }
}

TEST(InputKeybinds, compact_controls_rotates_every_valid_active_range)
{
    FullControlSnapshotGuard guard;
    constexpr int kFour =
        static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight =
        static_cast<int>(ControlDirectionMode::EightDirection);
    InputHardwareState& hw = input_hardware_state();

    for (int active_count = 1; active_count <= 4; ++active_count)
    {
        for (int removed = 0; removed < active_count; ++removed)
        {
            reset_default_player_controls();
            for (int p = 0; p < 4; ++p)
            {
                set_player_control_mode(p, kFour);
                set_player_key_binding(
                    p, KEY_CHEAT, SDLK_F1 + p);
                set_player_control_mode(p, kEight);
                set_player_key_binding(
                    p, KEY_CHEAT, SDLK_F5 + p);
                set_player_control_mode(
                    p, (p % 2 == 0) ? kFour : kEight);
                player_joy[p].index = 50 + p;
                player_joy[p].key_type[KEY_SPECIAL] =
                    JoyData::BUTTON;
                player_joy[p].key_index[KEY_SPECIAL] = 60 + p;
                hw.direction_grace[p] = {
                    .prev_raw = 3,
                    .hold_mask = 3,
                    .ticks_left = 2,
                };
                for (int k = 0; k < NUM_KEYS; ++k)
                    hw.touch_keystate[p][k] = true;
            }

            std::array<int, 4> expected_sources{0, 1, 2, 3};
            std::rotate(
                expected_sources.begin() + removed,
                expected_sources.begin() + removed + 1,
                expected_sources.begin() + active_count);

            ASSERT_TRUE(compact_player_controls_after_removal(
                removed, active_count));
            for (int p = 0; p < 4; ++p)
            {
                const int source = expected_sources[
                    static_cast<std::size_t>(p)];
                EXPECT_EQ(
                    SDLK_F1 + source,
                    get_player_key_binding_for_mode(
                        p, kFour, KEY_CHEAT))
                    << "active_count=" << active_count
                    << " removed=" << removed << " slot=" << p;
                EXPECT_EQ(
                    SDLK_F5 + source,
                    get_player_key_binding_for_mode(
                        p, kEight, KEY_CHEAT));
                const int expected_mode =
                    (source % 2 == 0) ? kFour : kEight;
                EXPECT_EQ(expected_mode, get_player_control_mode(p));
                EXPECT_EQ(
                    expected_mode == kFour
                        ? SDLK_F1 + source
                        : SDLK_F5 + source,
                    og::runtime::current_session
                        ->player_keys_[p][KEY_CHEAT]);
                EXPECT_EQ(50 + source, player_joy[p].index);
                EXPECT_EQ(
                    source,
                    hw.player_control_default_profiles[p]);
                EXPECT_EQ(
                    60 + source,
                    player_joy[p].key_index[KEY_SPECIAL]);

                const bool affected =
                    p >= removed && p < active_count;
                EXPECT_EQ(
                    affected ? 0 : 3,
                    hw.direction_grace[p].prev_raw);
                EXPECT_EQ(
                    affected ? 0 : 3,
                    hw.direction_grace[p].hold_mask);
                EXPECT_EQ(
                    affected ? 0 : 2,
                    hw.direction_grace[p].ticks_left);
                for (int k = 0; k < NUM_KEYS; ++k)
                {
                    EXPECT_EQ(
                        !affected, hw.touch_keystate[p][k]);
                }
            }
        }
    }
}

TEST(InputKeybinds, compacted_default_profile_identity_survives_cfg_roundtrip)
{
    FullControlSnapshotGuard guard;
    cfg_store config;
    reset_default_player_controls();

    constexpr int kFour =
        static_cast<int>(ControlDirectionMode::FourDirection);
    ASSERT_TRUE(compact_player_controls_after_removal(0, 4));
    save_player_control_settings_to_cfg(config);

    reset_default_player_controls();
    load_player_control_settings_from_cfg(config);
    const int expected_profiles[4] = {1, 2, 3, 0};
    for (int p = 0; p < 4; ++p)
    {
        EXPECT_EQ(
            expected_profiles[p],
            input_hardware_state()
                .player_control_default_profiles[p]);
        ASSERT_TRUE(reset_default_player_controls_for_player(p));
    }

    // RESET after reload restores each rotated profile's own factory map.
    constexpr int expected_up[4] = {SDLK_UP, SDLK_I, SDLK_T, SDLK_W};
    for (int p = 0; p < 4; ++p)
    {
        EXPECT_EQ(
            expected_up[p],
            get_player_key_binding_for_mode(
                p, kFour, KEY_UP));
    }
}


TEST(InputKeybinds, compact_controls_after_removal_rejects_invalid_inputs)
{
    FullControlSnapshotGuard guard;
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_F8);
    InputHardwareState& hw = input_hardware_state();
    player_joy[2].index = 77;
    player_joy[2].key_type[KEY_FIRE] = JoyData::BUTTON;
    player_joy[2].key_index[KEY_FIRE] = 9;
    hw.direction_grace[1] = {
        .prev_raw = 5, .hold_mask = 5, .ticks_left = 3};
    hw.touch_keystate[1][KEY_FIRE] = true;

    int modes[4];
    int default_profiles[4];
    int mode4[4][NUM_KEYS];
    int mode8[4][NUM_KEYS];
    int active[4][NUM_KEYS];
    for (int p = 0; p < 4; ++p)
    {
        modes[p] = get_player_control_mode(p);
        default_profiles[p] =
            hw.player_control_default_profiles[p];
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            mode4[p][k] = get_player_key_binding_for_mode(p,
                static_cast<int>(ControlDirectionMode::FourDirection), k);
            mode8[p][k] = get_player_key_binding_for_mode(p,
                static_cast<int>(ControlDirectionMode::EightDirection), k);
            active[p][k] = og::runtime::current_session->player_keys_[p][k];
        }
    }

    ASSERT_TRUE(!compact_player_controls_after_removal(-1, 3));
    ASSERT_TRUE(!compact_player_controls_after_removal(3, 3));
    ASSERT_TRUE(!compact_player_controls_after_removal(0, 0));
    ASSERT_TRUE(!compact_player_controls_after_removal(0, 5));
    for (int p = 0; p < 4; ++p)
    {
        ASSERT_EQ(modes[p], get_player_control_mode(p));
        ASSERT_EQ(
            default_profiles[p],
            hw.player_control_default_profiles[p]);
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            ASSERT_EQ(mode4[p][k], get_player_key_binding_for_mode(p,
                static_cast<int>(ControlDirectionMode::FourDirection), k));
            ASSERT_EQ(mode8[p][k], get_player_key_binding_for_mode(p,
                static_cast<int>(ControlDirectionMode::EightDirection), k));
            ASSERT_EQ(active[p][k], og::runtime::current_session->player_keys_[p][k]);
        }
    }
    EXPECT_EQ(77, player_joy[2].index);
    EXPECT_EQ(JoyData::BUTTON, player_joy[2].key_type[KEY_FIRE]);
    EXPECT_EQ(9, player_joy[2].key_index[KEY_FIRE]);
    EXPECT_EQ(5, hw.direction_grace[1].prev_raw);
    EXPECT_EQ(5, hw.direction_grace[1].hold_mask);
    EXPECT_EQ(3, hw.direction_grace[1].ticks_left);
    EXPECT_TRUE(hw.touch_keystate[1][KEY_FIRE]);
}


TEST(InputKeybinds, input_control_settings_cfg_roundtrip)
{
    cfg_store config;
    config.load_settings();

    const int old_yell = og::runtime::current_session->player_keys_[0][KEY_YELL];
    ControlModeGuard mode_guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    og::runtime::current_session->player_keys_[0][KEY_YELL] = SDLK_Z;
    save_player_control_settings_to_cfg(config);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    og::runtime::current_session->player_keys_[0][KEY_YELL] = SDLK_UNKNOWN;
    load_player_control_settings_from_cfg(config);

    ASSERT_EQ(static_cast<int>(ControlDirectionMode::EightDirection), get_player_control_mode(0)) << "control mode should reload from config";
    ASSERT_EQ(static_cast<int>(SDLK_Z), og::runtime::current_session->player_keys_[0][KEY_YELL]) << "keybind should reload from config";

    og::runtime::current_session->player_keys_[0][KEY_YELL] = old_yell;
}


TEST(InputKeybinds, input_control_settings_cfg_invalid_values_fall_back_to_defaults)
{
    FullControlSnapshotGuard guard;
    cfg_store config;
    config.load_settings();

    reset_default_player_controls();
    const int default_mode = get_player_control_mode(0);
    const int default_legacy = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP);
    const int default_mode4 = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_FIRE);
    const int default_mode8 = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_SPECIAL);

    config.apply_setting("controls", "player1_mode", "invalid");
    config.apply_setting(
        "controls", "player1_default_profile", "invalid");
    config.apply_setting(
        "controls", "player2_default_profile", "2");
    config.apply_setting(
        "controls", "player3_default_profile", "3");
    config.apply_setting(
        "controls", "player4_default_profile", "4");
    config.apply_setting("controls", "player1_key0", "invalid");
    config.apply_setting("controls", "player1_mode4_key4", "invalid");
    config.apply_setting("controls", "player1_mode8_key5", "invalid");

    load_player_control_settings_from_cfg(config);

    ASSERT_EQ(default_mode, get_player_control_mode(0)) << "invalid control mode should fall back to default";
    ASSERT_EQ(default_legacy, get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "invalid legacy key should fall back to default";
    ASSERT_EQ(default_mode4, get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_FIRE)) << "invalid 4-direction key should fall back to default";
    ASSERT_EQ(default_mode8, get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_SPECIAL)) << "invalid 8-direction key should fall back to default";
    for (int p = 0; p < 4; ++p)
    {
        EXPECT_EQ(
            p,
            input_hardware_state()
                .player_control_default_profiles[p])
            << "an invalid persisted permutation must fall back atomically";
    }
}


TEST(InputKeybinds, input_control_settings_cfg_persists_separate_mode_keymaps)
{
    cfg_store config;
    config.load_settings();

    ModeKeyBindingGuard bind_guard(0, KEY_UP);
    ControlModeGuard mode_guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_3);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP, SDLK_4);
    save_player_control_settings_to_cfg(config);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_UNKNOWN);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP, SDLK_UNKNOWN);

    load_player_control_settings_from_cfg(config);
    ASSERT_EQ(static_cast<int>(SDLK_3), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "config load should restore 4-direction binding";
    ASSERT_EQ(static_cast<int>(SDLK_4), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "config load should restore 8-direction binding";
}


TEST(InputKeybinds, controls_reset_defaults_action_resets_controls_only)
{
    FullControlSnapshotGuard guard;

    reset_default_player_controls();
    int expected_mode[4];
    int expected_mode4[4][NUM_KEYS];
    int expected_mode8[4][NUM_KEYS];
    for (int p = 0; p < 4; ++p)
    {
        expected_mode[p] = get_player_control_mode(p);
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            expected_mode4[p][k] = get_player_key_binding_for_mode(
                p, static_cast<int>(ControlDirectionMode::FourDirection), k);
            expected_mode8[p][k] = get_player_key_binding_for_mode(
                p, static_cast<int>(ControlDirectionMode::EightDirection), k);
        }
    }

    for (int p = 0; p < 4; ++p)
    {
        set_player_control_mode(p, static_cast<int>(ControlDirectionMode::FourDirection));
        set_player_key_binding(p, KEY_UP, SDLK_F1 + p);
        set_player_control_mode(p, static_cast<int>(ControlDirectionMode::EightDirection));
        set_player_key_binding(p, KEY_UP_RIGHT, SDLK_F5 + p);
    }

    const std::string old_render = cfg.get_setting("graphics", "render");
    cfg.apply_setting("graphics", "render", "CONTROL_RESET_TEST");

    vbutton b;
    const Sint32 result = b.do_call(button_action_id(ButtonAction::RestoreDefaultControls), -1);
    ASSERT_EQ(2, (int)result) << "restore-default-controls should request redraw";

    for (int p = 0; p < 4; ++p)
    {
        ASSERT_EQ(expected_mode[p], get_player_control_mode(p)) << "control reset should restore default mode";
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            ASSERT_EQ(expected_mode4[p][k], get_player_key_binding_for_mode(p, static_cast<int>(ControlDirectionMode::FourDirection), k)) << "control reset should restore default 4-direction map";
            ASSERT_EQ(expected_mode8[p][k], get_player_key_binding_for_mode(p, static_cast<int>(ControlDirectionMode::EightDirection), k)) << "control reset should restore default 8-direction map";
        }
    }

    ASSERT_STREQ("CONTROL_RESET_TEST", cfg.get_setting("graphics", "render").c_str()) << "controls reset should not modify non-controls settings";

    // The global CONTROLS / RESET ALL action persists immediately: dirty
    // runtime again, reload cfg, and prove the defaults return from disk.
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_F12);
    cfg.load_settings();
    load_player_control_settings_from_cfg(cfg);
    ASSERT_EQ(expected_mode[0], get_player_control_mode(0));
    ASSERT_EQ(expected_mode8[0][KEY_UP_RIGHT],
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::EightDirection),
                  KEY_UP_RIGHT));

    guard.restore();
    cfg.apply_setting("graphics", "render", old_render);
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();
}


TEST(InputKeybinds, settings_reset_preserves_runtime_and_saved_controls)
{
    FullControlSnapshotGuard guard;

    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_F9);
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();

    vbutton b;
    ASSERT_EQ(2, static_cast<int>(b.do_call(
        button_action_id(ButtonAction::RestoreDefaultSettings), -1)));
    EXPECT_EQ(static_cast<int>(ControlDirectionMode::EightDirection),
              get_player_control_mode(0));
    EXPECT_EQ(static_cast<int>(SDLK_F9),
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::EightDirection),
                  KEY_UP_RIGHT));

    // Prove the preserved controls were written back over the freshly copied
    // default config, not merely left alive in this process.
    reset_default_player_controls();
    cfg.load_settings();
    load_player_control_settings_from_cfg(cfg);
    EXPECT_EQ(static_cast<int>(ControlDirectionMode::EightDirection),
              get_player_control_mode(0));
    EXPECT_EQ(static_cast<int>(SDLK_F9),
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::EightDirection),
                  KEY_UP_RIGHT));

    guard.restore();
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();
}


TEST(InputKeybinds, eight_direction_defaults_p1_clockwise_from_up)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 8-dir defaults clockwise from Up: W, E, D, C, X, Z, A, Q
    ASSERT_EQ(static_cast<int>(SDLK_W), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P1 8-dir Up should be W";
    ASSERT_EQ(static_cast<int>(SDLK_E), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P1 8-dir Up-Right should be E";
    ASSERT_EQ(static_cast<int>(SDLK_D), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT)) << "P1 8-dir Right should be D";
    ASSERT_EQ(static_cast<int>(SDLK_C), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN_RIGHT)) << "P1 8-dir Down-Right should be C";
    ASSERT_EQ(static_cast<int>(SDLK_X), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN)) << "P1 8-dir Down should be X";
    ASSERT_EQ(static_cast<int>(SDLK_Z), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN_LEFT)) << "P1 8-dir Down-Left should be Z";
    ASSERT_EQ(static_cast<int>(SDLK_A), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_LEFT)) << "P1 8-dir Left should be A";
    ASSERT_EQ(static_cast<int>(SDLK_Q), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_LEFT)) << "P1 8-dir Up-Left should be Q";
    ASSERT_EQ(static_cast<int>(SDLK_S), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P1 8-dir Yell should be S";
}


TEST(InputKeybinds, eight_direction_defaults_other_players)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P2: arrows, no diagonals
    ASSERT_EQ(static_cast<int>(SDLK_UP), get_player_key_binding_for_mode(1, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P2 8-dir Up should be Up arrow";
    ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN), get_player_key_binding_for_mode(1, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P2 8-dir Up-Right should be UNKNOWN";

    // P3: clockwise I/O/L/./,/M/J/U, Yell=K
    ASSERT_EQ(static_cast<int>(SDLK_I), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P3 8-dir Up should be I";
    ASSERT_EQ(static_cast<int>(SDLK_O), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P3 8-dir Up-Right should be O";
    ASSERT_EQ(static_cast<int>(SDLK_L), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT)) << "P3 8-dir Right should be L";
    ASSERT_EQ(static_cast<int>(SDLK_K), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P3 8-dir Yell should be K";

    // P4: clockwise T/Y/H/N/B/V/F/R, Yell=G
    ASSERT_EQ(static_cast<int>(SDLK_T), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P4 8-dir Up should be T";
    ASSERT_EQ(static_cast<int>(SDLK_Y), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P4 8-dir Up-Right should be Y";
    ASSERT_EQ(static_cast<int>(SDLK_H), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT)) << "P4 8-dir Right should be H";
    ASSERT_EQ(static_cast<int>(SDLK_G), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P4 8-dir Yell should be G";
}


TEST(InputKeybinds, eight_direction_defaults_differ_from_four_direction)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 4-dir Yell should be E, but 8-dir Yell should be S
    ASSERT_EQ(static_cast<int>(SDLK_E), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_YELL)) << "P1 4-dir Yell should be E";
    ASSERT_EQ(static_cast<int>(SDLK_S), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P1 8-dir Yell should be S";

    // P1 4-dir diagonals should be UNKNOWN
    ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP_RIGHT)) << "P1 4-dir Up-Right should be UNKNOWN";
    // P1 8-dir diagonals should be set
    ASSERT_EQ(static_cast<int>(SDLK_E), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P1 8-dir Up-Right should be E";
}


TEST(InputKeybinds, four_direction_defaults_unchanged_wasd)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 4-dir should still be WASD
    ASSERT_EQ(static_cast<int>(SDLK_W), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "P1 4-dir Up should be W";
    ASSERT_EQ(static_cast<int>(SDLK_A), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_LEFT)) << "P1 4-dir Left should be A";
    ASSERT_EQ(static_cast<int>(SDLK_S), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_DOWN)) << "P1 4-dir Down should be S";
    ASSERT_EQ(static_cast<int>(SDLK_D), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_RIGHT)) << "P1 4-dir Right should be D";
}


// ---------------------------------------------------------------------------
// KEY_LOOKUP (the render-time look-up hold): client-side keymap slot 16.
// ---------------------------------------------------------------------------

TEST(InputKeybinds, lookup_key_is_client_side_only_and_not_an_input_action)
{
    // KEY_LOOKUP is appended AFTER the 16 wire-visible actions: the
    // InputAction enum / InputState wire stay untouched at 16.
    static_assert(kInputActionCount == 16,
                  "the InputAction wire enum must stay at 16 actions");
    static_assert(NUM_INPUT_KEYS == 16,
                  "the InputState wire format must stay at 16 keys");
    static_assert(KEY_LOOKUP == kInputActionCount,
                  "KEY_LOOKUP sits just past the wire-visible actions");
    static_assert(NUM_KEYS == NUM_INPUT_KEYS + 1,
                  "the client keymap is the 16 wire actions + KEY_LOOKUP");
    SUCCEED();
}

TEST(InputKeybinds, lookup_key_defaults_per_player_per_mode)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);

    // P1: V in both modes. P2: Right Ctrl in both modes (its arrow layout is
    // identical across modes). P3: P in both modes. P4: B in 4-direction only
    // (its own 8-direction cluster claims 'b' for DOWN, so 8-dir is unbound).
    ASSERT_EQ(static_cast<int>(SDLK_V), get_player_key_binding_for_mode(0, kFour, KEY_LOOKUP))
        << "P1 4-dir look-up should default to V";
    ASSERT_EQ(static_cast<int>(SDLK_V), get_player_key_binding_for_mode(0, kEight, KEY_LOOKUP))
        << "P1 8-dir look-up should default to V";
    ASSERT_EQ(static_cast<int>(SDLK_RCTRL), get_player_key_binding_for_mode(1, kFour, KEY_LOOKUP))
        << "P2 4-dir look-up should default to Right Ctrl";
    ASSERT_EQ(static_cast<int>(SDLK_RCTRL), get_player_key_binding_for_mode(1, kEight, KEY_LOOKUP))
        << "P2 8-dir look-up should default to Right Ctrl";
    ASSERT_EQ(static_cast<int>(SDLK_P), get_player_key_binding_for_mode(2, kFour, KEY_LOOKUP))
        << "P3 4-dir look-up should default to P";
    ASSERT_EQ(static_cast<int>(SDLK_P), get_player_key_binding_for_mode(2, kEight, KEY_LOOKUP))
        << "P3 8-dir look-up should default to P";
    ASSERT_EQ(static_cast<int>(SDLK_B), get_player_key_binding_for_mode(3, kFour, KEY_LOOKUP))
        << "P4 4-dir look-up should default to B";
    ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN), get_player_key_binding_for_mode(3, kEight, KEY_LOOKUP))
        << "P4 8-dir look-up should default unbound (own cluster uses B for DOWN)";

    // The default control mode is 4-direction; the active keymap must carry
    // each player's 4-direction look-up default.
    ASSERT_EQ(static_cast<int>(SDLK_V), og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
        << "the active keymap should carry the P1 default";
    ASSERT_EQ(static_cast<int>(SDLK_RCTRL), og::runtime::current_session->player_keys_[1][KEY_LOOKUP])
        << "the active keymap should carry the P2 default";
    ASSERT_EQ(static_cast<int>(SDLK_P), og::runtime::current_session->player_keys_[2][KEY_LOOKUP])
        << "the active keymap should carry the P3 default";
    ASSERT_EQ(static_cast<int>(SDLK_B), og::runtime::current_session->player_keys_[3][KEY_LOOKUP])
        << "the active keymap should carry the P4 default";
}


TEST(InputKeybinds, yell_key_defaults_per_player_per_mode)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);

    constexpr int expected_four[4] = {SDLK_E, SDLK_BACKSLASH, SDLK_U, SDLK_Y};
    constexpr int expected_eight[4] = {SDLK_S, SDLK_BACKSLASH, SDLK_K, SDLK_G};
    for (int p = 0; p < 4; ++p)
    {
        ASSERT_EQ(expected_four[p], get_player_key_binding_for_mode(p, kFour, KEY_YELL))
            << "P" << (p + 1) << " 4-dir yell default";
        ASSERT_EQ(expected_eight[p], get_player_key_binding_for_mode(p, kEight, KEY_YELL))
            << "P" << (p + 1) << " 8-dir yell default";
        ASSERT_EQ(expected_four[p], og::runtime::current_session->player_keys_[p][KEY_YELL])
            << "P" << (p + 1) << " active keymap should carry the 4-dir yell default";
    }
}


TEST(InputKeybinds, lookup_key_defaults_do_not_collide_with_any_player_mode)
{
    // All four players share one physical keyboard and control modes are
    // per-player, so a look-up default must not appear anywhere else in ANY
    // player's keymap in EITHER mode. Sole grandfathered exception: P1's 'v'
    // overlaps P4's 8-direction DOWN-LEFT 'v' (pre-existing, documented in
    // input_state.cpp). P4's own 4-dir 'b' vs its own 8-dir DOWN 'b' is not
    // a collision — a player's two mode keymaps are never active together.
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kModes[2] = {static_cast<int>(ControlDirectionMode::FourDirection),
                               static_cast<int>(ControlDirectionMode::EightDirection)};
    for (int p = 0; p < 4; ++p)
    {
        for (const int lookup_mode : kModes)
        {
            const int lookup = get_player_key_binding_for_mode(p, lookup_mode, KEY_LOOKUP);
            if (lookup == SDLK_UNKNOWN)
                continue;
            for (int q = 0; q < 4; ++q)
            {
                for (const int other_mode : kModes)
                {
                    if (q == p && other_mode != lookup_mode)
                        continue; // own other-mode map is never co-active
                    for (int k = 0; k < NUM_KEYS; ++k)
                    {
                        if (q == p && k == KEY_LOOKUP)
                            continue; // the binding itself
                        if (p == 0 && q == 3 && k == KEY_DOWN_LEFT &&
                            other_mode == static_cast<int>(ControlDirectionMode::EightDirection))
                            continue; // grandfathered P1 'v' vs P4 8-dir down-left
                        ASSERT_NE(lookup, get_player_key_binding_for_mode(q, other_mode, k))
                            << "P" << (p + 1) << " look-up default (mode " << lookup_mode
                            << ") collides with P" << (q + 1) << " key " << k
                            << " in mode " << other_mode;
                    }
                }
            }
        }
    }
}


TEST(InputKeybinds, lookup_key_defaults_survive_cfg_roundtrip_for_all_players)
{
    FullControlSnapshotGuard guard;
    cfg_store config;
    config.load_settings();

    reset_default_player_controls();
    save_player_control_settings_to_cfg(config);

    // Scribble over every look-up slot, then reload: the saved defaults must
    // come back for every player in both mode keymaps.
    for (int p = 0; p < 4; ++p)
    {
        set_player_control_mode(p, static_cast<int>(ControlDirectionMode::FourDirection));
        set_player_key_binding(p, KEY_LOOKUP, SDLK_F9);
        set_player_control_mode(p, static_cast<int>(ControlDirectionMode::EightDirection));
        set_player_key_binding(p, KEY_LOOKUP, SDLK_F10);
        set_player_control_mode(p, static_cast<int>(ControlDirectionMode::FourDirection));
    }

    load_player_control_settings_from_cfg(config);

    constexpr int expected_four[4] = {SDLK_V, SDLK_RCTRL, SDLK_P, SDLK_B};
    constexpr int expected_eight[4] = {SDLK_V, SDLK_RCTRL, SDLK_P, SDLK_UNKNOWN};
    for (int p = 0; p < 4; ++p)
    {
        ASSERT_EQ(expected_four[p],
                  get_player_key_binding_for_mode(
                      p, static_cast<int>(ControlDirectionMode::FourDirection), KEY_LOOKUP))
            << "P" << (p + 1) << " 4-dir look-up should reload from config";
        ASSERT_EQ(expected_eight[p],
                  get_player_key_binding_for_mode(
                      p, static_cast<int>(ControlDirectionMode::EightDirection), KEY_LOOKUP))
            << "P" << (p + 1) << " 8-dir look-up should reload from config";
    }
}

TEST(InputKeybinds, lookup_key_binding_persists_through_cfg_roundtrip)
{
    FullControlSnapshotGuard guard;
    cfg_store config;
    config.load_settings();

    reset_default_player_controls();
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_LOOKUP, SDLK_B);
    save_player_control_settings_to_cfg(config);

    // Wipe the binding, then reload: the saved value must come back.
    reset_default_player_controls();
    ASSERT_EQ(static_cast<int>(SDLK_V),
              og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
        << "reset should restore the default before the reload";
    load_player_control_settings_from_cfg(config);
    ASSERT_EQ(static_cast<int>(SDLK_B),
              og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
        << "the look-up binding should reload from config";
    ASSERT_EQ(static_cast<int>(SDLK_B),
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::FourDirection),
                  KEY_LOOKUP))
        << "the 4-direction keymap should hold the reloaded binding";
}


// ---------------------------------------------------------------------------
// Browser-safe web control defaults (issue #144). Player 1's native factory
// FIRE/SPECIAL are LCtrl/LAlt, which makes "attack while walking up" the
// browser-reserved Ctrl+W chord. Web builds substitute Z/X (4-dir) and
// Space/[ (8-dir) for profile 0, and a one-shot version-keyed cfg migration
// moves persisted bindings still equal to the old factory default. All of it
// is exercised natively through the web_mode parameter.
// ---------------------------------------------------------------------------

TEST(InputKeybinds, web_defaults_move_p1_fire_and_special_off_ctrl_alt)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();
    for (int p = 0; p < 4; ++p)
        ASSERT_TRUE(reset_default_player_controls_for_player(p, /*web_mode=*/true));

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);

    EXPECT_EQ(static_cast<int>(SDLK_Z), get_player_key_binding_for_mode(0, kFour, KEY_FIRE))
        << "web 4-dir P1 fire should be Z";
    EXPECT_EQ(static_cast<int>(SDLK_X), get_player_key_binding_for_mode(0, kFour, KEY_SPECIAL))
        << "web 4-dir P1 special should be X";
    EXPECT_EQ(static_cast<int>(SDLK_SPACE), get_player_key_binding_for_mode(0, kEight, KEY_FIRE))
        << "web 8-dir P1 fire should be Space (z/x are P1's own diagonals)";
    EXPECT_EQ(static_cast<int>(SDLK_LEFTBRACKET), get_player_key_binding_for_mode(0, kEight, KEY_SPECIAL))
        << "web 8-dir P1 special should be [";

    // Movement and the other action keys are untouched.
    EXPECT_EQ(static_cast<int>(SDLK_W), get_player_key_binding_for_mode(0, kFour, KEY_UP));
    EXPECT_EQ(static_cast<int>(SDLK_A), get_player_key_binding_for_mode(0, kFour, KEY_LEFT));
    EXPECT_EQ(static_cast<int>(SDLK_E), get_player_key_binding_for_mode(0, kFour, KEY_YELL));
    EXPECT_EQ(static_cast<int>(SDLK_LSHIFT), get_player_key_binding_for_mode(0, kFour, KEY_SHIFTER));

    // P2-P4 keep their native fire/special in both modes.
    EXPECT_EQ(static_cast<int>(SDLK_PERIOD), get_player_key_binding_for_mode(1, kFour, KEY_FIRE));
    EXPECT_EQ(static_cast<int>(SDLK_SLASH), get_player_key_binding_for_mode(1, kEight, KEY_SPECIAL));
    EXPECT_EQ(static_cast<int>(SDLK_SPACE), get_player_key_binding_for_mode(2, kFour, KEY_FIRE));
    EXPECT_EQ(static_cast<int>(SDLK_SEMICOLON), get_player_key_binding_for_mode(2, kEight, KEY_SPECIAL));
    EXPECT_EQ(static_cast<int>(SDLK_5), get_player_key_binding_for_mode(3, kFour, KEY_FIRE));
    EXPECT_EQ(static_cast<int>(SDLK_6), get_player_key_binding_for_mode(3, kEight, KEY_SPECIAL));
}

TEST(InputKeybinds, native_defaults_still_ctrl_alt)
{
    // Pin the native values explicitly so a future change cannot silently
    // move the desktop defaults (the tables are the single source of truth).
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);
    EXPECT_EQ(static_cast<int>(SDLK_LCTRL), get_player_key_binding_for_mode(0, kFour, KEY_FIRE));
    EXPECT_EQ(static_cast<int>(SDLK_LALT), get_player_key_binding_for_mode(0, kFour, KEY_SPECIAL));
    EXPECT_EQ(static_cast<int>(SDLK_LCTRL), get_player_key_binding_for_mode(0, kEight, KEY_FIRE));
    EXPECT_EQ(static_cast<int>(SDLK_LALT), get_player_key_binding_for_mode(0, kEight, KEY_SPECIAL));
}

TEST(InputKeybinds, web_defaults_do_not_collide_with_any_player_mode)
{
    // Same matrix as the native collision sweep, over the WEB defaults: on a
    // shared keyboard every player's bindings are live at once and modes are
    // per-player. Documented exceptions only:
    //  - the grandfathered P1 look-up 'v' vs P4 8-dir DOWN-LEFT 'v';
    //  - web P1 8-dir FIRE = Space vs P3 FIRE = Space (accepted overlap:
    //    it needs a 3+ player one-keyboard web game with P1 opted into
    //    8-direction mode).
    FullControlSnapshotGuard guard;
    reset_default_player_controls();
    for (int p = 0; p < 4; ++p)
        ASSERT_TRUE(reset_default_player_controls_for_player(p, /*web_mode=*/true));

    struct Binding
    {
        int player;
        int mode;
        int key_enum;
        int keycode;
    };
    std::vector<Binding> bindings;
    const int kModes[2] = {
        static_cast<int>(ControlDirectionMode::FourDirection),
        static_cast<int>(ControlDirectionMode::EightDirection),
    };
    for (int p = 0; p < 4; ++p)
        for (int mode : kModes)
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                const int kc = get_player_key_binding_for_mode(p, mode, k);
                if (kc != KEYCODE_UNKNOWN)
                    bindings.push_back(Binding{p, mode, k, kc});
            }

    const auto is_documented_v_quirk = [](const Binding& a, const Binding& b) {
        const auto matches = [](const Binding& p1, const Binding& p4) {
            return p1.player == 0 && p1.key_enum == KEY_LOOKUP &&
                   p1.keycode == KEYCODE_v &&
                   p4.player == 3 && p4.key_enum == KEY_DOWN_LEFT &&
                   p4.mode == static_cast<int>(ControlDirectionMode::EightDirection) &&
                   p4.keycode == KEYCODE_v;
        };
        return matches(a, b) || matches(b, a);
    };
    const auto is_documented_space_overlap = [](const Binding& a, const Binding& b) {
        const auto matches = [](const Binding& p1, const Binding& p3) {
            return p1.player == 0 && p1.key_enum == KEY_FIRE &&
                   p1.mode == static_cast<int>(ControlDirectionMode::EightDirection) &&
                   p1.keycode == KEYCODE_SPACE &&
                   p3.player == 2 && p3.key_enum == KEY_FIRE &&
                   p3.keycode == KEYCODE_SPACE;
        };
        return matches(a, b) || matches(b, a);
    };

    for (std::size_t i = 0; i < bindings.size(); ++i)
        for (std::size_t j = i + 1; j < bindings.size(); ++j)
        {
            const Binding& a = bindings[i];
            const Binding& b = bindings[j];
            if (a.player == b.player || a.keycode != b.keycode)
                continue;
            if (is_documented_v_quirk(a, b) || is_documented_space_overlap(a, b))
                continue;
            ADD_FAILURE()
                << "web-defaults cross-player key collision: keycode "
                << a.keycode << " is player " << (a.player + 1) << " mode "
                << a.mode << " key-slot " << a.key_enum << " AND player "
                << (b.player + 1) << " mode " << b.mode << " key-slot "
                << b.key_enum;
        }
}

TEST(InputKeybinds, web_cfg_migration_rewrites_stale_ctrl_binding_once)
{
    FullControlSnapshotGuard guard;
    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);

    // A returning web player's persisted cfg: the old factory LCtrl/LAlt
    // values, no version marker (the boot path writes the whole controls
    // block back on every boot, so these are frozen in for everyone who
    // played before the web defaults moved).
    cfg_store config;
    config.apply_setting("controls", "player1_mode4_key8", std::to_string(SDLK_LCTRL));
    config.apply_setting("controls", "player1_mode4_key9", std::to_string(SDLK_LALT));
    config.apply_setting("controls", "player1_mode8_key8", std::to_string(SDLK_LCTRL));
    config.apply_setting("controls", "player1_mode8_key9", std::to_string(SDLK_LALT));

    load_player_control_settings_from_cfg(config, /*web_mode=*/true);
    EXPECT_EQ(static_cast<int>(SDLK_Z), get_player_key_binding_for_mode(0, kFour, KEY_FIRE))
        << "stale 4-dir LCtrl fire must migrate to Z";
    EXPECT_EQ(static_cast<int>(SDLK_X), get_player_key_binding_for_mode(0, kFour, KEY_SPECIAL))
        << "stale 4-dir LAlt special must migrate to X";
    EXPECT_EQ(static_cast<int>(SDLK_SPACE), get_player_key_binding_for_mode(0, kEight, KEY_FIRE))
        << "stale 8-dir LCtrl fire must migrate to Space";
    EXPECT_EQ(static_cast<int>(SDLK_LEFTBRACKET), get_player_key_binding_for_mode(0, kEight, KEY_SPECIAL))
        << "stale 8-dir LAlt special must migrate to [";
    EXPECT_EQ("1", config.get_setting("controls", "web_default_keys_version"))
        << "the migration must stamp its version marker into the cfg";

    // The boot path saves the whole controls block right after loading —
    // that is exactly what freezes values into a returning player's cfg.
    save_player_control_settings_to_cfg(config);

    // The player then DELIBERATELY rebinds fire back to LCtrl. The version
    // marker is current, so the next load must leave it alone.
    config.apply_setting("controls", "player1_mode4_key8", std::to_string(SDLK_LCTRL));
    load_player_control_settings_from_cfg(config, /*web_mode=*/true);
    EXPECT_EQ(static_cast<int>(SDLK_LCTRL), get_player_key_binding_for_mode(0, kFour, KEY_FIRE))
        << "a deliberate LCtrl rebind must survive later loads";
    EXPECT_EQ(static_cast<int>(SDLK_X), get_player_key_binding_for_mode(0, kFour, KEY_SPECIAL))
        << "the untouched slots keep their migrated web defaults";
}

TEST(InputKeybinds, web_cfg_migration_never_touches_user_rebinds_or_native_loads)
{
    FullControlSnapshotGuard guard;
    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);

    {
        // A user's own (non-factory) binding is never migrated, even on the
        // first versionless web load.
        cfg_store config;
        config.apply_setting("controls", "player1_mode4_key8", std::to_string(SDLK_F9));
        load_player_control_settings_from_cfg(config, /*web_mode=*/true);
        EXPECT_EQ(static_cast<int>(SDLK_F9), get_player_key_binding_for_mode(0, kFour, KEY_FIRE))
            << "a user rebind must never be migrated";
    }
    {
        // Native load (default argument): LCtrl preserved, no version key
        // written — native byte-identity.
        cfg_store config;
        config.apply_setting("controls", "player1_mode4_key8", std::to_string(SDLK_LCTRL));
        load_player_control_settings_from_cfg(config);
        EXPECT_EQ(static_cast<int>(SDLK_LCTRL), get_player_key_binding_for_mode(0, kFour, KEY_FIRE))
            << "native loads must not migrate anything";
        EXPECT_TRUE(config.get_setting("controls", "web_default_keys_version").empty())
            << "native loads must not stamp the web version key";
    }
}

TEST(InputKeybinds, web_cfg_migration_follows_the_profile_zero_seat)
{
    // The migration keys off the FACTORY PROFILE (which seat carries the
    // LCtrl/LAlt layout), not the seat index: after seat compaction the
    // profile-0 layout can live on any seat.
    FullControlSnapshotGuard guard;
    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);

    cfg_store config;
    config.apply_setting("controls", "player1_default_profile", "2");
    config.apply_setting("controls", "player2_default_profile", "1");
    config.apply_setting("controls", "player3_default_profile", "3");
    config.apply_setting("controls", "player4_default_profile", "4");
    config.apply_setting("controls", "player2_mode4_key8", std::to_string(SDLK_LCTRL));

    load_player_control_settings_from_cfg(config, /*web_mode=*/true);
    EXPECT_EQ(static_cast<int>(SDLK_Z), get_player_key_binding_for_mode(1, kFour, KEY_FIRE))
        << "the seat holding profile 0 must migrate its stale LCtrl fire";
    EXPECT_EQ(static_cast<int>(SDLK_PERIOD), get_player_key_binding_for_mode(0, kFour, KEY_FIRE))
        << "the seat holding profile 1 keeps the arrows-profile fire";
}
