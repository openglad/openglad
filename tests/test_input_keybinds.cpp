#include <openglad/input/input.h>
#include <openglad/input/button.h>
#include <openglad/runtime/game_context.h>
#include <openglad/data/gparser.h>
#include "test_framework.h"

extern int player_keys[4][NUM_KEYS];
extern cfg_store cfg;

namespace
{
struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old;

    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_), key_enum(key_enum_), old(player_keys[player_][key_enum_])
    {
        player_keys[player][key_enum] = new_key;
    }

    ~KeyBindingGuard()
    {
        player_keys[player][key_enum] = old;
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

void test_input_isPlayerHoldingKey_uses_keyboard_state_when_no_joystick_mapping()
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_FIRE, SDLK_v);

    const SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_v);
    KeyStateGuard ks(sc);

    ks.set(false);
    TEST_ASSERT(!isPlayerHoldingKey(0, KEY_FIRE), "unpressed key should not be held");

    ks.set(true);
    TEST_ASSERT(isPlayerHoldingKey(0, KEY_FIRE), "pressed key should be held");
}
REGISTER_TEST(test_input_isPlayerHoldingKey_uses_keyboard_state_when_no_joystick_mapping);

void test_input_didPlayerPressKey_matches_keydown_and_ignores_repeats()
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(0, KEY_SPECIAL, SDLK_x);

    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_x;
    e.key.repeat = 0;
    TEST_ASSERT(didPlayerPressKey(0, KEY_SPECIAL, e), "matching keydown should be recognized");

    e.key.repeat = 1;
    TEST_ASSERT(!didPlayerPressKey(0, KEY_SPECIAL, e), "repeat keydown should be ignored");

    e.key.repeat = 0;
    e.key.keysym.sym = SDLK_y;
    TEST_ASSERT(!didPlayerPressKey(0, KEY_SPECIAL, e), "non-matching keydown should be ignored");

    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_x;
    TEST_ASSERT(!didPlayerPressKey(0, KEY_SPECIAL, e), "keyup should not be treated as press");
}
REGISTER_TEST(test_input_didPlayerPressKey_matches_keydown_and_ignores_repeats);

void test_input_didPlayerReleaseKey_matches_keyup()
{
    disablePlayerJoystick(0);
    KeyBindingGuard bind(1, KEY_YELL, SDLK_q);

    SDL_Event e{};
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_q;
    TEST_ASSERT(didPlayerReleaseKey(1, KEY_YELL, e), "matching keyup should be recognized");

    e.key.keysym.sym = SDLK_w;
    TEST_ASSERT(!didPlayerReleaseKey(1, KEY_YELL, e), "non-matching keyup should be ignored");

    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_q;
    TEST_ASSERT(!didPlayerReleaseKey(1, KEY_YELL, e), "keydown should not be treated as release");
}
REGISTER_TEST(test_input_didPlayerReleaseKey_matches_keyup);

void test_input_key_binding_helpers_isAnyPlayerKey_and_isPlayerKey()
{
    KeyBindingGuard bind0(2, KEY_SWITCH, SDLK_KP_0);
    KeyBindingGuard bind1(3, KEY_SWITCH, SDLK_KP_1);

    TEST_ASSERT(isAnyPlayerKey(SDLK_KP_0), "isAnyPlayerKey should find mapped key");
    TEST_ASSERT(isPlayerKey(2, SDLK_KP_0), "isPlayerKey should find mapping for specific player");
    TEST_ASSERT(!isPlayerKey(0, SDLK_KP_0), "isPlayerKey should not report mapping for other player");

    TEST_ASSERT(isAnyPlayerKey(SDLK_KP_1), "isAnyPlayerKey should find second mapped key");
    TEST_ASSERT(isPlayerKey(3, SDLK_KP_1), "isPlayerKey should find mapping for specific player");
}
REGISTER_TEST(test_input_key_binding_helpers_isAnyPlayerKey_and_isPlayerKey);

void test_input_wait_for_key_event_returns_fake_escape_in_test_mode()
{
    SDL_Event e = wait_for_key_event();
    TEST_ASSERT_EQ((int)SDL_KEYDOWN, (int)e.type, "wait_for_key_event should return keydown in test mode");
    TEST_ASSERT_EQ((int)SDLK_ESCAPE, (int)e.key.keysym.sym, "wait_for_key_event should return escape in test mode");
}
REGISTER_TEST(test_input_wait_for_key_event_returns_fake_escape_in_test_mode);

void test_input_state_from_sdl_respects_four_direction_mode()
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
    TEST_ASSERT(!input.players[0].held[KEY_UP_RIGHT], "4-direction should suppress held diagonal input");
    TEST_ASSERT(!input.players[0].pressed[KEY_UP_RIGHT], "4-direction should suppress pressed diagonal input");

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    input_state_from_sdl(input);
    TEST_ASSERT(input.players[0].held[KEY_UP_RIGHT], "8-direction should keep held diagonal input");
    TEST_ASSERT(input.players[0].pressed[KEY_UP_RIGHT], "8-direction should allow pressed diagonal edges");
}
REGISTER_TEST(test_input_state_from_sdl_respects_four_direction_mode);

void test_input_mode_key_binding_updates_are_isolated_by_control_mode()
{
    ModeKeyBindingGuard bind_guard(0, KEY_UP);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_1);
    TEST_ASSERT_EQ(static_cast<int>(SDLK_1),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP),
        "4-direction keymap should update in 4-direction mode");

    const int eight_before = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    TEST_ASSERT_EQ(eight_before, player_keys[0][KEY_UP],
        "switching to 8-direction should restore that mode's key binding");
    set_player_key_binding(0, KEY_UP, SDLK_2);

    TEST_ASSERT_EQ(static_cast<int>(SDLK_1),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP),
        "editing in 8-direction mode must not mutate 4-direction binding");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_2),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP),
        "8-direction binding should update when editing in 8-direction mode");

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    TEST_ASSERT_EQ(static_cast<int>(SDLK_1), player_keys[0][KEY_UP],
        "switching back to 4-direction should restore 4-direction binding");
}
REGISTER_TEST(test_input_mode_key_binding_updates_are_isolated_by_control_mode);

void test_input_control_settings_cfg_roundtrip()
{
    cfg_store config;
    config.load_settings();

    const int old_yell = player_keys[0][KEY_YELL];
    ControlModeGuard mode_guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    player_keys[0][KEY_YELL] = SDLK_z;
    save_player_control_settings_to_cfg(config);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    player_keys[0][KEY_YELL] = SDLK_UNKNOWN;
    load_player_control_settings_from_cfg(config);

    TEST_ASSERT_EQ(static_cast<int>(ControlDirectionMode::EightDirection), get_player_control_mode(0),
        "control mode should reload from config");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_z), player_keys[0][KEY_YELL],
        "keybind should reload from config");

    player_keys[0][KEY_YELL] = old_yell;
}
REGISTER_TEST(test_input_control_settings_cfg_roundtrip);

void test_input_control_settings_cfg_persists_separate_mode_keymaps()
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
    TEST_ASSERT_EQ(static_cast<int>(SDLK_3),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP),
        "config load should restore 4-direction binding");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_4),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP),
        "config load should restore 8-direction binding");
}
REGISTER_TEST(test_input_control_settings_cfg_persists_separate_mode_keymaps);

void test_controls_reset_defaults_action_resets_controls_only()
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
    TEST_ASSERT_EQ(2, (int)result, "restore-default-controls should request redraw");

    for (int p = 0; p < 4; ++p)
    {
        TEST_ASSERT_EQ(expected_mode[p], get_player_control_mode(p),
            "control reset should restore default mode");
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            TEST_ASSERT_EQ(expected_mode4[p][k],
                get_player_key_binding_for_mode(p, static_cast<int>(ControlDirectionMode::FourDirection), k),
                "control reset should restore default 4-direction map");
            TEST_ASSERT_EQ(expected_mode8[p][k],
                get_player_key_binding_for_mode(p, static_cast<int>(ControlDirectionMode::EightDirection), k),
                "control reset should restore default 8-direction map");
        }
    }

    TEST_ASSERT_STR_EQ("CONTROL_RESET_TEST", cfg.get_setting("graphics", "render").c_str(),
        "controls reset should not modify non-controls settings");

    cfg.apply_setting("graphics", "render", old_render);
}
REGISTER_TEST(test_controls_reset_defaults_action_resets_controls_only);

void test_eight_direction_defaults_p1_clockwise_from_up()
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 8-dir defaults clockwise from Up: W, E, D, C, X, Z, A, Q
    TEST_ASSERT_EQ(static_cast<int>(SDLK_w),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP),
        "P1 8-dir Up should be W");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_e),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT),
        "P1 8-dir Up-Right should be E");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_d),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT),
        "P1 8-dir Right should be D");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_c),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN_RIGHT),
        "P1 8-dir Down-Right should be C");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_x),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN),
        "P1 8-dir Down should be X");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_z),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_DOWN_LEFT),
        "P1 8-dir Down-Left should be Z");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_a),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_LEFT),
        "P1 8-dir Left should be A");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_q),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_LEFT),
        "P1 8-dir Up-Left should be Q");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_s),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL),
        "P1 8-dir Yell should be S");
}
REGISTER_TEST(test_eight_direction_defaults_p1_clockwise_from_up);

void test_eight_direction_defaults_other_players()
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P2: arrows, no diagonals
    TEST_ASSERT_EQ(static_cast<int>(SDLK_UP),
        get_player_key_binding_for_mode(1, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP),
        "P2 8-dir Up should be Up arrow");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN),
        get_player_key_binding_for_mode(1, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT),
        "P2 8-dir Up-Right should be UNKNOWN");

    // P3: clockwise I/O/L/./,/M/J/U, Yell=K
    TEST_ASSERT_EQ(static_cast<int>(SDLK_i),
        get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP),
        "P3 8-dir Up should be I");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_o),
        get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT),
        "P3 8-dir Up-Right should be O");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_l),
        get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT),
        "P3 8-dir Right should be L");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_k),
        get_player_key_binding_for_mode(2, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL),
        "P3 8-dir Yell should be K");

    // P4: clockwise T/Y/H/N/B/V/F/R, Yell=G
    TEST_ASSERT_EQ(static_cast<int>(SDLK_t),
        get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP),
        "P4 8-dir Up should be T");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_y),
        get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT),
        "P4 8-dir Up-Right should be Y");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_h),
        get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_RIGHT),
        "P4 8-dir Right should be H");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_g),
        get_player_key_binding_for_mode(3, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL),
        "P4 8-dir Yell should be G");
}
REGISTER_TEST(test_eight_direction_defaults_other_players);

void test_eight_direction_defaults_differ_from_four_direction()
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 4-dir Yell should be E, but 8-dir Yell should be S
    TEST_ASSERT_EQ(static_cast<int>(SDLK_e),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_YELL),
        "P1 4-dir Yell should be E");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_s),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL),
        "P1 8-dir Yell should be S");

    // P1 4-dir diagonals should be UNKNOWN
    TEST_ASSERT_EQ(static_cast<int>(SDLK_UNKNOWN),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP_RIGHT),
        "P1 4-dir Up-Right should be UNKNOWN");
    // P1 8-dir diagonals should be set
    TEST_ASSERT_EQ(static_cast<int>(SDLK_e),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_UP_RIGHT),
        "P1 8-dir Up-Right should be E");
}
REGISTER_TEST(test_eight_direction_defaults_differ_from_four_direction);

void test_four_direction_defaults_unchanged_wasd()
{
    FullControlSnapshotGuard guard;
    reset_default_player_controls();

    // P1 4-dir should still be WASD
    TEST_ASSERT_EQ(static_cast<int>(SDLK_w),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_UP),
        "P1 4-dir Up should be W");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_a),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_LEFT),
        "P1 4-dir Left should be A");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_s),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_DOWN),
        "P1 4-dir Down should be S");
    TEST_ASSERT_EQ(static_cast<int>(SDLK_d),
        get_player_key_binding_for_mode(0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_RIGHT),
        "P1 4-dir Right should be D");
}
REGISTER_TEST(test_four_direction_defaults_unchanged_wasd);
