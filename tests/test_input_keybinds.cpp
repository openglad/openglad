#include <openglad/gameplay/input_action.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/interface/input.h>
#include <openglad/interface/button.h>
#include <openglad/interface/native_input.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL.h>

#include <cstring>

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
    Uint8 old_value;
    int numkeys;
    Uint8* keys;

    explicit KeyStateGuard(SDL_Scancode sc_)
        : sc(sc_), old_value(0), numkeys(0), keys(nullptr)
    {
        const Uint8* read_only = SDL_GetKeyboardState(&numkeys);
        keys = const_cast<Uint8*>(read_only);
        if (keys && sc >= 0 && sc < numkeys)
            old_value = keys[sc];
    }

    void set(bool pressed)
    {
        if (keys && sc >= 0 && sc < numkeys)
            keys[sc] = pressed ? 1 : 0;
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
    int mode4[4][NUM_KEYS];
    int mode8[4][NUM_KEYS];

    FullControlSnapshotGuard()
    {
        for (int p = 0; p < 4; ++p)
        {
            modes[p] = get_player_control_mode(p);
            for (int k = 0; k < NUM_KEYS; ++k)
            {
                mode4[p][k] = get_player_key_binding_for_mode(
                    p, static_cast<int>(ControlDirectionMode::FourDirection), k);
                mode8[p][k] = get_player_key_binding_for_mode(
                    p, static_cast<int>(ControlDirectionMode::EightDirection), k);
            }
        }
    }

    ~FullControlSnapshotGuard()
    {
        for (int p = 0; p < 4; ++p)
        {
            set_player_control_mode(p, static_cast<int>(ControlDirectionMode::FourDirection));
            for (int k = 0; k < NUM_KEYS; ++k)
                set_player_key_binding(p, k, mode4[p][k]);
            set_player_control_mode(p, static_cast<int>(ControlDirectionMode::EightDirection));
            for (int k = 0; k < NUM_KEYS; ++k)
                set_player_key_binding(p, k, mode8[p][k]);
            set_player_control_mode(p, modes[p]);
        }
    }
};
} // namespace

TEST(InputKeybinds, input_isPlayerHoldingKey_uses_keyboard_state_when_no_joystick_mapping)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_FIRE, SDLK_v);

    const SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_v);
    KeyStateGuard ks(sc);

    ks.set(false);
    ASSERT_TRUE(!isPlayerHoldingKey(0, KEY_FIRE)) << "unpressed key should not be held";

    ks.set(true);
    ASSERT_TRUE(isPlayerHoldingKey(0, KEY_FIRE)) << "pressed key should be held";
}


TEST(InputKeybinds, input_didPlayerPressKey_matches_keydown_and_ignores_repeats)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_SPECIAL, SDLK_x);

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_x;
    e.key.repeat = 0;
    ASSERT_TRUE(didPlayerPressKey(0, KEY_SPECIAL, e)) << "matching keydown should be recognized";

    e.key.repeat = 1;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "repeat keydown should be ignored";

    e.key.repeat = 0;
    e.key.keysym.sym = SDLK_y;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "non-matching keydown should be ignored";

    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_x;
    ASSERT_TRUE(!didPlayerPressKey(0, KEY_SPECIAL, e)) << "keyup should not be treated as press";
}


TEST(InputKeybinds, input_didPlayerReleaseKey_matches_keyup)
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(1, KEY_YELL, SDLK_q);

    SDL_Event e{};
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_q;
    ASSERT_TRUE(didPlayerReleaseKey(1, KEY_YELL, e)) << "matching keyup should be recognized";

    e.key.keysym.sym = SDLK_w;
    ASSERT_TRUE(!didPlayerReleaseKey(1, KEY_YELL, e)) << "non-matching keyup should be ignored";

    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_q;
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
    ASSERT_EQ((int)SDL_KEYDOWN, (int)e.type) << "wait_for_key_event should return keydown in test mode";
    ASSERT_EQ((int)SDLK_ESCAPE, (int)e.key.keysym.sym) << "wait_for_key_event should return escape in test mode";
}


TEST(InputKeybinds, native_input_decode_event_ignores_malformed_scancode_payload)
{
    og::input_native::EventData null_out{};
    ASSERT_TRUE(!og::input_native::decode_event(nullptr, null_out))
        << "decode_event should reject null native event pointers";

    SDL_Event e{};
    std::memset(&e, 0x01, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_q;
    e.key.repeat = 0;

    og::input_native::EventData out{};
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept keydown payloads";
    ASSERT_EQ((int)og::input_native::EventType::KeyDown, (int)out.type) << "decoded event type should be keydown";
    ASSERT_EQ((int)SDLK_q, out.key_sym) << "decoded key symbol should match payload";
    ASSERT_EQ((int)SDL_GetScancodeFromKey(SDLK_q), out.key_scancode) << "decoded scancode should derive from key symbol, not raw enum payload";
}


TEST(InputKeybinds, native_input_decode_event_covers_non_keyboard_variants)
{
    SDL_Event e{};
    og::input_native::EventData out{};

    e.type = SDL_TEXTINPUT;
    SDL_strlcpy(e.text.text, "abc", sizeof(e.text.text));
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept text input";
    ASSERT_EQ((int)og::input_native::EventType::TextInput, (int)out.type) << "text input type should decode";
    ASSERT_STREQ("abc", out.text.data()) << "text payload should decode";

    e = SDL_Event{};
    e.type = SDL_MOUSEWHEEL;
    e.wheel.y = -3;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept mouse wheel";
    ASSERT_EQ((int)og::input_native::EventType::MouseWheel, (int)out.type) << "mouse wheel type should decode";
    ASSERT_EQ(-3, out.wheel_y) << "mouse wheel delta should decode";

    e = SDL_Event{};
    e.type = SDL_MOUSEMOTION;
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
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = 44;
    e.button.y = 55;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept mouse button events";
    ASSERT_EQ((int)og::input_native::EventType::MouseButtonDown, (int)out.type) << "mouse button type should decode";
    ASSERT_EQ((int)SDL_BUTTON_LEFT, out.button) << "mouse button should decode";
    ASSERT_EQ(44, out.button_x) << "mouse button x should decode";
    ASSERT_EQ(55, out.button_y) << "mouse button y should decode";

    e = SDL_Event{};
    e.type = SDL_FINGERDOWN;
    e.tfinger.x = 0.25f;
    e.tfinger.y = 0.5f;
    e.tfinger.dx = 0.1f;
    e.tfinger.dy = -0.2f;
    e.tfinger.fingerId = 77;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept finger events";
    ASSERT_EQ((int)og::input_native::EventType::FingerDown, (int)out.type) << "finger type should decode";
    ASSERT_FLOAT_EQ(0.25f, out.finger_x) << "finger x should decode";
    ASSERT_FLOAT_EQ(0.5f, out.finger_y) << "finger y should decode";
    ASSERT_FLOAT_EQ(0.1f, out.finger_dx) << "finger dx should decode";
    ASSERT_FLOAT_EQ(-0.2f, out.finger_dy) << "finger dy should decode";
    ASSERT_EQ(77, out.finger_id) << "finger id should decode";

    e = SDL_Event{};
    e.type = SDL_JOYAXISMOTION;
    e.jaxis.which = 2;
    e.jaxis.axis = 1;
    e.jaxis.value = 12000;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept joystick axis events";
    ASSERT_EQ((int)og::input_native::EventType::JoyAxisMotion, (int)out.type) << "joy axis type should decode";
    ASSERT_EQ(2, out.joy_axis_which) << "joy axis joystick should decode";
    ASSERT_EQ(1, out.joy_axis_axis) << "joy axis index should decode";
    ASSERT_EQ(12000, out.joy_axis_value) << "joy axis value should decode";

    e = SDL_Event{};
    e.type = SDL_JOYBUTTONDOWN;
    e.jbutton.which = 3;
    e.jbutton.button = 4;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept joystick button events";
    ASSERT_EQ((int)og::input_native::EventType::JoyButtonDown, (int)out.type) << "joy button type should decode";
    ASSERT_EQ(3, out.joy_button_which) << "joy button joystick should decode";
    ASSERT_EQ(4, out.joy_button_button) << "joy button index should decode";

    e = SDL_Event{};
    e.type = SDL_JOYHATMOTION;
    e.jhat.which = 5;
    e.jhat.hat = 1;
    e.jhat.value = SDL_HAT_RIGHT;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept joystick hat events";
    ASSERT_EQ((int)og::input_native::EventType::JoyHatMotion, (int)out.type) << "joy hat type should decode";
    ASSERT_EQ(5, out.joy_hat_which) << "joy hat joystick should decode";
    ASSERT_EQ(1, out.joy_hat_hat) << "joy hat index should decode";
    ASSERT_EQ((int)SDL_HAT_RIGHT, out.joy_hat_value) << "joy hat value should decode";

    e = SDL_Event{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_MINIMIZED;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window minimize";
    ASSERT_EQ((int)og::input_native::WindowEventType::Minimized, (int)out.window_event) << "window minimize should decode";

    e = SDL_Event{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_RESIZED;
    e.window.data1 = 640;
    e.window.data2 = 400;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window events";
    ASSERT_EQ((int)og::input_native::EventType::Window, (int)out.type) << "window type should decode";
    ASSERT_EQ((int)og::input_native::WindowEventType::Resized, (int)out.window_event) << "window resize should decode";
    ASSERT_EQ(640, out.window_data1) << "window data1 should decode";
    ASSERT_EQ(400, out.window_data2) << "window data2 should decode";

    e = SDL_Event{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_CLOSE;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window close";
    ASSERT_EQ((int)og::input_native::WindowEventType::Close, (int)out.window_event) << "window close should decode";

    e = SDL_Event{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_RESTORED;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept window restored";
    ASSERT_EQ((int)og::input_native::WindowEventType::Restored, (int)out.window_event) << "window restore should decode";

    e = SDL_Event{};
    e.type = SDL_WINDOWEVENT;
    e.window.event = 0x7f;
    ASSERT_TRUE(og::input_native::decode_event(&e, out)) << "decode_event should accept unknown window events";
    ASSERT_EQ((int)og::input_native::WindowEventType::Unknown, (int)out.window_event) << "unknown window event should map to Unknown";

    e = SDL_Event{};
    e.type = SDL_USEREVENT;
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

    const std::uint32_t ticks_before = og::input_native::ticks_ms();
    ASSERT_TRUE(og::input_native::keyboard_state() != nullptr) << "keyboard_state should return SDL state buffer";
    ASSERT_TRUE(og::input_native::scancode_from_key(SDLK_q) >= 0) << "scancode lookup should succeed";
    ASSERT_TRUE(og::input_native::key_name(SDLK_q) != nullptr) << "key_name should return a string";
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
    og::input_native::push_key_event(true, SDLK_q);
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
    KeyStateGuard ks(SDL_GetScancodeFromKey(SDLK_v));
    ControlModeGuard mode_guard(0);

    InputState input{};
    input.clear();

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_v);
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


TEST(InputKeybinds, input_control_settings_cfg_roundtrip)
{
    cfg_store config;
    config.load_settings();

    const int old_yell = og::runtime::current_session->player_keys_[0][KEY_YELL];
    ControlModeGuard mode_guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    og::runtime::current_session->player_keys_[0][KEY_YELL] = SDLK_z;
    save_player_control_settings_to_cfg(config);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    og::runtime::current_session->player_keys_[0][KEY_YELL] = SDLK_UNKNOWN;
    load_player_control_settings_from_cfg(config);

    ASSERT_EQ(static_cast<int>(ControlDirectionMode::EightDirection), get_player_control_mode(0)) << "control mode should reload from config";
    ASSERT_EQ(static_cast<int>(SDLK_z), og::runtime::current_session->player_keys_[0][KEY_YELL]) << "keybind should reload from config";

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
    config.apply_setting("controls", "player1_key0", "invalid");
    config.apply_setting("controls", "player1_mode4_key4", "invalid");
    config.apply_setting("controls", "player1_mode8_key5", "invalid");

    load_player_control_settings_from_cfg(config);

    ASSERT_EQ(default_mode, get_player_control_mode(0)) << "invalid control mode should fall back to default";
    ASSERT_EQ(default_legacy, get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "invalid legacy key should fall back to default";
    ASSERT_EQ(default_mode4, get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_FIRE)) << "invalid 4-direction key should fall back to default";
    ASSERT_EQ(default_mode8, get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_SPECIAL)) << "invalid 8-direction key should fall back to default";
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

    cfg.apply_setting("graphics", "render", old_render);
}


TEST(InputKeybinds, eight_direction_defaults_p1_clockwise_from_up)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 8-dir defaults clockwise from Up: W, E, D, C, X, Z, A, Q
    ASSERT_EQ(static_cast<int>(SDLK_w), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P1 8-dir Up should be W";
    ASSERT_EQ(static_cast<int>(SDLK_e), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P1 8-dir Up-Right should be E";
    ASSERT_EQ(static_cast<int>(SDLK_d), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT)) << "P1 8-dir Right should be D";
    ASSERT_EQ(static_cast<int>(SDLK_c), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN_RIGHT)) << "P1 8-dir Down-Right should be C";
    ASSERT_EQ(static_cast<int>(SDLK_x), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN)) << "P1 8-dir Down should be X";
    ASSERT_EQ(static_cast<int>(SDLK_z), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN_LEFT)) << "P1 8-dir Down-Left should be Z";
    ASSERT_EQ(static_cast<int>(SDLK_a), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_LEFT)) << "P1 8-dir Left should be A";
    ASSERT_EQ(static_cast<int>(SDLK_q), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_LEFT)) << "P1 8-dir Up-Left should be Q";
    ASSERT_EQ(static_cast<int>(SDLK_s), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P1 8-dir Yell should be S";
}


TEST(InputKeybinds, eight_direction_defaults_other_players)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P2: arrows, no diagonals
    ASSERT_EQ(static_cast<int>(SDLK_UP), get_player_key_binding_for_mode(1, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P2 8-dir Up should be Up arrow";
    ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN), get_player_key_binding_for_mode(1, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P2 8-dir Up-Right should be UNKNOWN";

    // P3: clockwise I/O/L/./,/M/J/U, Yell=K
    ASSERT_EQ(static_cast<int>(SDLK_i), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P3 8-dir Up should be I";
    ASSERT_EQ(static_cast<int>(SDLK_o), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P3 8-dir Up-Right should be O";
    ASSERT_EQ(static_cast<int>(SDLK_l), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT)) << "P3 8-dir Right should be L";
    ASSERT_EQ(static_cast<int>(SDLK_k), get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P3 8-dir Yell should be K";

    // P4: clockwise T/Y/H/N/B/V/F/R, Yell=G
    ASSERT_EQ(static_cast<int>(SDLK_t), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP)) << "P4 8-dir Up should be T";
    ASSERT_EQ(static_cast<int>(SDLK_y), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P4 8-dir Up-Right should be Y";
    ASSERT_EQ(static_cast<int>(SDLK_h), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT)) << "P4 8-dir Right should be H";
    ASSERT_EQ(static_cast<int>(SDLK_g), get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P4 8-dir Yell should be G";
}


TEST(InputKeybinds, eight_direction_defaults_differ_from_four_direction)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 4-dir Yell should be E, but 8-dir Yell should be S
    ASSERT_EQ(static_cast<int>(SDLK_e), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_YELL)) << "P1 4-dir Yell should be E";
    ASSERT_EQ(static_cast<int>(SDLK_s), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL)) << "P1 8-dir Yell should be S";

    // P1 4-dir diagonals should be UNKNOWN
    ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP_RIGHT)) << "P1 4-dir Up-Right should be UNKNOWN";
    // P1 8-dir diagonals should be set
    ASSERT_EQ(static_cast<int>(SDLK_e), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT)) << "P1 8-dir Up-Right should be E";
}


TEST(InputKeybinds, four_direction_defaults_unchanged_wasd)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 4-dir should still be WASD
    ASSERT_EQ(static_cast<int>(SDLK_w), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP)) << "P1 4-dir Up should be W";
    ASSERT_EQ(static_cast<int>(SDLK_a), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_LEFT)) << "P1 4-dir Left should be A";
    ASSERT_EQ(static_cast<int>(SDLK_s), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_DOWN)) << "P1 4-dir Down should be S";
    ASSERT_EQ(static_cast<int>(SDLK_d), get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_RIGHT)) << "P1 4-dir Right should be D";
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
    ASSERT_EQ(static_cast<int>(SDLK_v), get_player_key_binding_for_mode(0, kFour, KEY_LOOKUP))
        << "P1 4-dir look-up should default to V";
    ASSERT_EQ(static_cast<int>(SDLK_v), get_player_key_binding_for_mode(0, kEight, KEY_LOOKUP))
        << "P1 8-dir look-up should default to V";
    ASSERT_EQ(static_cast<int>(SDLK_RCTRL), get_player_key_binding_for_mode(1, kFour, KEY_LOOKUP))
        << "P2 4-dir look-up should default to Right Ctrl";
    ASSERT_EQ(static_cast<int>(SDLK_RCTRL), get_player_key_binding_for_mode(1, kEight, KEY_LOOKUP))
        << "P2 8-dir look-up should default to Right Ctrl";
    ASSERT_EQ(static_cast<int>(SDLK_p), get_player_key_binding_for_mode(2, kFour, KEY_LOOKUP))
        << "P3 4-dir look-up should default to P";
    ASSERT_EQ(static_cast<int>(SDLK_p), get_player_key_binding_for_mode(2, kEight, KEY_LOOKUP))
        << "P3 8-dir look-up should default to P";
    ASSERT_EQ(static_cast<int>(SDLK_b), get_player_key_binding_for_mode(3, kFour, KEY_LOOKUP))
        << "P4 4-dir look-up should default to B";
    ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN), get_player_key_binding_for_mode(3, kEight, KEY_LOOKUP))
        << "P4 8-dir look-up should default unbound (own cluster uses B for DOWN)";

    // The default control mode is 4-direction; the active keymap must carry
    // each player's 4-direction look-up default.
    ASSERT_EQ(static_cast<int>(SDLK_v), og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
        << "the active keymap should carry the P1 default";
    ASSERT_EQ(static_cast<int>(SDLK_RCTRL), og::runtime::current_session->player_keys_[1][KEY_LOOKUP])
        << "the active keymap should carry the P2 default";
    ASSERT_EQ(static_cast<int>(SDLK_p), og::runtime::current_session->player_keys_[2][KEY_LOOKUP])
        << "the active keymap should carry the P3 default";
    ASSERT_EQ(static_cast<int>(SDLK_b), og::runtime::current_session->player_keys_[3][KEY_LOOKUP])
        << "the active keymap should carry the P4 default";
}


TEST(InputKeybinds, yell_key_defaults_per_player_per_mode)
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    constexpr int kFour = static_cast<int>(ControlDirectionMode::FourDirection);
    constexpr int kEight = static_cast<int>(ControlDirectionMode::EightDirection);

    constexpr int expected_four[4] = {SDLK_e, SDLK_BACKSLASH, SDLK_u, SDLK_y};
    constexpr int expected_eight[4] = {SDLK_s, SDLK_BACKSLASH, SDLK_k, SDLK_g};
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

    constexpr int expected_four[4] = {SDLK_v, SDLK_RCTRL, SDLK_p, SDLK_b};
    constexpr int expected_eight[4] = {SDLK_v, SDLK_RCTRL, SDLK_p, SDLK_UNKNOWN};
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
    set_player_key_binding(0, KEY_LOOKUP, SDLK_b);
    save_player_control_settings_to_cfg(config);

    // Wipe the binding, then reload: the saved value must come back.
    reset_default_player_controls();
    ASSERT_EQ(static_cast<int>(SDLK_v),
              og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
        << "reset should restore the default before the reload";
    load_player_control_settings_from_cfg(config);
    ASSERT_EQ(static_cast<int>(SDLK_b),
              og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
        << "the look-up binding should reload from config";
    ASSERT_EQ(static_cast<int>(SDLK_b),
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::FourDirection),
                  KEY_LOOKUP))
        << "the 4-direction keymap should hold the reloaded binding";
}
