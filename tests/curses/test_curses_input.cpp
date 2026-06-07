/* Unit tests for CursesInput.
 *
 * Two things are locked in here:
 *  1. Keys resolve through THE GAME'S OWN KEYBINDINGS (player_keys_[0]), never a
 *     hardcoded map — with the default 8-direction cluster 'q' is up-left and 'x'
 *     is down, the config is the source of truth, and the configured 4/8-direction
 *     mode is honored.
 *  2. Input is exact press/release (Kitty protocol): a Press marks an action held
 *     and pressed-this-frame, a Release clears it, no decay/clock — so Ctrl/Alt,
 *     held-fire-while-moving, and focus-loss all behave precisely.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_input.h>

#include <openglad/gameplay/input_action.h>
#include <openglad/interface/input.h>          // KEYCODE_*, KEY_*, control-mode API
#include <openglad/interface/session_state.h>  // current_session->player_keys_
#include <openglad/resources/gparser.h>        // cfg_store, cfg

using namespace og::curses;

namespace {

int act(InputAction a) { return static_cast<int>(a); }

// A keycode table indexed by InputAction (== KEY_*). Mirrors the player-1
// 8-direction default cluster (W/E/D/C/X/Z/A/Q, Fire=LeftCtrl, Special=LeftAlt) so
// hermetic tests exercise a realistic, fully-populated binding set.
struct Bindings {
    int k[kInputActionCount] = {};
    Bindings()
    {
        k[KEY_UP] = KEYCODE_w;        k[KEY_UP_RIGHT] = KEYCODE_e;
        k[KEY_RIGHT] = KEYCODE_d;     k[KEY_DOWN_RIGHT] = KEYCODE_c;
        k[KEY_DOWN] = KEYCODE_x;      k[KEY_DOWN_LEFT] = KEYCODE_z;
        k[KEY_LEFT] = KEYCODE_a;      k[KEY_UP_LEFT] = KEYCODE_q;
        k[KEY_FIRE] = KEYCODE_LCTRL;  k[KEY_SPECIAL] = KEYCODE_LALT;
        k[KEY_SWITCH] = KEYCODE_BACKQUOTE;
        k[KEY_SPECIAL_SWITCH] = KEYCODE_TAB;
        k[KEY_YELL] = KEYCODE_s;      k[KEY_SHIFTER] = KEYCODE_LSHIFT;
        k[KEY_PREFS] = KEYCODE_1;     k[KEY_CHEAT] = KEYCODE_F5;
    }
};

// Restores the global control settings (bindings + mode) to the suite baseline
// when it leaves scope, so a test that rebinds or switches mode cannot leak into a
// shuffled neighbor.
struct RestoreControls {
    ~RestoreControls() { load_player_control_settings_from_cfg(cfg); }
};

Key press(char32_t c) { return Key::character(c); }
Key release(char32_t c) { return Key::character(c, KeyEvent::Release); }

} // namespace

// --- keycode_for_key: terminal key -> SDL keycode (so it CAN match a binding) ---

TEST(CursesInput, keycode_for_key_maps_terminal_keys_to_sdl_keycodes)
{
    EXPECT_EQ(CursesInput::keycode_for_key(Key::character(U'q')), KEYCODE_q);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::character(U'x')), KEYCODE_x);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::character(U'Q')), KEYCODE_q); // case folds
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::Up)), KEYCODE_UP);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::Tab)), KEYCODE_TAB);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::Escape)), KEYCODE_ESCAPE);
    // The modifier keys the Kitty protocol now delivers map to their SDL keycodes.
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::LeftCtrl)), KEYCODE_LCTRL);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::LeftAlt)), KEYCODE_LALT);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::LeftShift)), KEYCODE_LSHIFT);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::F5)), KEYCODE_F5);
    // No-keycode keys never match a binding.
    EXPECT_EQ(CursesInput::keycode_for_key(Key::none()), KEYCODE_UNKNOWN);
    EXPECT_EQ(CursesInput::keycode_for_key(Key::special(KeyCode::FocusOut)), KEYCODE_UNKNOWN);
}

TEST(CursesInput, escape_is_the_only_meta_key)
{
    EXPECT_EQ(CursesInput::meta_for_key(Key::special(KeyCode::Escape)), MetaAction::OpenGameMenu);
    EXPECT_EQ(CursesInput::meta_for_key(Key::character(U'q')), MetaAction::None);
    EXPECT_EQ(CursesInput::meta_for_key(Key::character(U'p')), MetaAction::None);
}

// === THE REGRESSION LOCK ===
// With the player-1 8-direction cluster, 'q' is up-left and 'x' is down — the two
// keys the user reported broken when they were hardcoded. Movement comes from the
// bindings, key by key, for all 8 directions.
TEST(CursesInput, keys_resolve_through_bindings_not_a_hardcoded_map)
{
    const Bindings b;
    struct Case { char32_t key; int dx; int dy; const char* what; };
    const Case cases[] = {
        {U'q', -1, -1, "q is up-left"},
        {U'x',  0, +1, "x is down"},
        {U'w',  0, -1, "w is up"},
        {U'd', +1,  0, "d is right"},
        {U'a', -1,  0, "a is left"},
        {U'e', +1, -1, "e is up-right"},
        {U'c', +1, +1, "c is down-right"},
        {U'z', -1, +1, "z is down-left"},
    };
    for (const Case& c : cases) {
        CursesInput input(b.k);
        input.feed(press(c.key));
        const InputState s = input.sample();
        EXPECT_EQ(s.players[0].move_x(), c.dx) << c.what;
        EXPECT_EQ(s.players[0].move_y(), c.dy) << c.what;
    }
}

// === EXACT PRESS / RELEASE (no decay) ===
// A press marks held + pressed-this-frame; the next sample with no new event keeps
// it held (forever, until release) but no longer pressed; a release clears it.
TEST(CursesInput, press_then_release_tracks_held_exactly)
{
    const Bindings b;
    CursesInput input(b.k);

    input.feed(press(U'd'));
    InputState s = input.sample();
    EXPECT_TRUE(s.players[0].held[act(InputAction::MoveRight)]);
    EXPECT_TRUE(s.players[0].pressed[act(InputAction::MoveRight)]);

    // No new event: still held (no decay), but not pressed.
    s = input.sample();
    EXPECT_TRUE(s.players[0].held[act(InputAction::MoveRight)]) << "held until release";
    EXPECT_FALSE(s.players[0].pressed[act(InputAction::MoveRight)]);

    // Many frames later, still held — there is no timeout.
    for (int i = 0; i < 100; ++i)
        s = input.sample();
    EXPECT_TRUE(s.players[0].held[act(InputAction::MoveRight)]) << "no decay window exists";

    input.feed(release(U'd'));
    s = input.sample();
    EXPECT_FALSE(s.players[0].held[act(InputAction::MoveRight)]) << "release clears held";
}

TEST(CursesInput, repeat_keeps_held_without_a_new_pressed_edge)
{
    const Bindings b;
    CursesInput input(b.k);
    input.feed(press(U'w'));
    (void)input.sample(); // consume the pressed edge
    input.feed(Key::character(U'w', KeyEvent::Repeat));
    const InputState s = input.sample();
    EXPECT_TRUE(s.players[0].held[act(InputAction::MoveUp)]);
    EXPECT_FALSE(s.players[0].pressed[act(InputAction::MoveUp)]) << "a repeat is not a fresh press";
}

// === Ctrl/Alt now work === (the documented limitation this whole change removes)
// Fire is bound to LeftCtrl; a standalone LeftCtrl press/release drives it.
TEST(CursesInput, modifier_bound_actions_fire_via_standalone_modifier_keys)
{
    const Bindings b; // Fire=LeftCtrl, Special=LeftAlt
    CursesInput input(b.k);

    input.feed(Key::special(KeyCode::LeftCtrl)); // press
    InputState s = input.sample();
    EXPECT_TRUE(s.players[0].held[act(InputAction::Fire)]) << "LeftCtrl fires (Fire binding)";

    input.feed(Key::special(KeyCode::LeftCtrl, KeyEvent::Release));
    s = input.sample();
    EXPECT_FALSE(s.players[0].held[act(InputAction::Fire)]);

    input.feed(Key::special(KeyCode::LeftAlt)); // press
    EXPECT_TRUE(input.sample().players[0].held[act(InputAction::Special)])
        << "LeftAlt triggers Special";
}

// Holding fire while moving: both stay held simultaneously (impossible to do
// reliably under the legacy decay hack).
TEST(CursesInput, can_hold_fire_and_move_at_once)
{
    const Bindings b;
    CursesInput input(b.k);
    input.feed(press(U'd'));                     // move right
    input.feed(Key::special(KeyCode::LeftCtrl)); // fire
    const InputState s = input.sample();
    EXPECT_TRUE(s.players[0].held[act(InputAction::MoveRight)]);
    EXPECT_TRUE(s.players[0].held[act(InputAction::Fire)]);
    EXPECT_EQ(s.players[0].move_x(), +1);
}

TEST(CursesInput, two_held_cardinals_produce_a_diagonal)
{
    const Bindings b;
    CursesInput input(b.k);
    input.feed(press(U'w')); // up
    input.feed(press(U'd')); // right
    const InputState s = input.sample();
    EXPECT_EQ(s.players[0].move_x(), +1);
    EXPECT_EQ(s.players[0].move_y(), -1);
    // Releasing one leaves the other held.
    input.feed(release(U'w'));
    const InputState s2 = input.sample();
    EXPECT_EQ(s2.players[0].move_y(), 0) << "up released";
    EXPECT_EQ(s2.players[0].move_x(), +1) << "right still held";
}

// Focus loss must drop every held key so movement can't get stuck down while the
// release goes to another window.
TEST(CursesInput, focus_out_clears_held_keys)
{
    const Bindings b;
    CursesInput input(b.k);
    input.feed(press(U'w'));
    ASSERT_TRUE(input.sample().players[0].held[act(InputAction::MoveUp)]);
    input.feed(Key::special(KeyCode::FocusOut));
    EXPECT_FALSE(input.sample().players[0].held[act(InputAction::MoveUp)]);
}

TEST(CursesInput, rebinding_changes_which_terminal_key_triggers_the_action)
{
    Bindings b;
    b.k[KEY_DOWN] = KEYCODE_j; // rebind Down: j instead of x

    CursesInput x_input(b.k);
    x_input.feed(press(U'x'));
    EXPECT_EQ(x_input.sample().players[0].move_y(), 0) << "x no longer bound to down";

    CursesInput j_input(b.k);
    j_input.feed(press(U'j'));
    EXPECT_EQ(j_input.sample().players[0].move_y(), +1) << "j now moves down";
}

TEST(CursesInput, unbound_key_produces_no_action)
{
    const Bindings b;
    CursesInput input(b.k);
    input.feed(press(U'@')); // bound to nothing
    const InputState s = input.sample();
    EXPECT_EQ(s.players[0].move_x(), 0);
    EXPECT_EQ(s.players[0].move_y(), 0);
    for (int a = 0; a < kInputActionCount; ++a)
        EXPECT_FALSE(s.players[0].held[a]);
}

TEST(CursesInput, reset_clears_state)
{
    const Bindings b;
    CursesInput input(b.k);
    input.feed(press(U'a'));
    input.reset();
    const InputState s = input.sample();
    EXPECT_FALSE(s.players[0].held[act(InputAction::MoveLeft)]);
    EXPECT_FALSE(s.players[0].pressed[act(InputAction::MoveLeft)]);
}

// === CONFIG SOURCE-OF-TRUTH (8-direction) ===
// The bindings come from the config, read by the game's real loader. Non-default
// keys make "just hardcode the defaults" impossible.
TEST(CursesInputConfig, reads_player1_eight_direction_bindings_from_config)
{
    RestoreControls restore;

    cfg_store config;
    config.apply_setting("controls", "player1_mode", "8");
    config.apply_setting("controls", "player1_mode8_key7", std::to_string(KEYCODE_j)); // UpLeft
    config.apply_setting("controls", "player1_mode8_key4", std::to_string(KEYCODE_k)); // Down
    load_player_control_settings_from_cfg(config);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));

    {
        CursesInput input; // -> current_session->player_keys_[0]
        input.feed(press(U'j'));
        const InputState s = input.sample();
        EXPECT_EQ(s.players[0].move_x(), -1) << "configured up-left key 'j'";
        EXPECT_EQ(s.players[0].move_y(), -1);
    }
    {
        CursesInput input;
        input.feed(press(U'k'));
        EXPECT_EQ(input.sample().players[0].move_y(), +1) << "configured down key 'k'";
    }
    {
        CursesInput input;
        input.feed(press(U'q')); // the DEFAULT up-left, now rebound away
        EXPECT_EQ(input.sample().players[0].move_x(), 0)
            << "the config replaced the default binding";
    }
}

// === DIRECTION MODE IS HONORED ===
// The curses client now honors the player's 4/8 setting (the Kitty protocol makes
// 4-direction viable). In 4-direction mode only the four cardinals are bound; the
// diagonal-only key 'q' does nothing. In 8-direction mode 'q' is up-left.
TEST(CursesInputConfig, honors_four_direction_mode_from_config)
{
    RestoreControls restore;

    cfg_store config;
    config.apply_setting("controls", "player1_mode", "4"); // FOUR-direction
    load_player_control_settings_from_cfg(config);

    CursesInput up_input;
    up_input.feed(press(U'w')); // default 4-dir up
    EXPECT_EQ(up_input.sample().players[0].move_y(), -1) << "'w' moves up in 4-direction mode";

    CursesInput q_input;
    q_input.feed(press(U'q')); // a diagonal key — not bound in 4-direction mode
    const InputState s = q_input.sample();
    EXPECT_EQ(s.players[0].move_x(), 0) << "'q' (a diagonal) is unbound in 4-direction mode";
    EXPECT_EQ(s.players[0].move_y(), 0);
}

TEST(CursesInputConfig, honors_eight_direction_mode_from_config)
{
    RestoreControls restore;

    cfg_store config;
    config.apply_setting("controls", "player1_mode", "8"); // EIGHT-direction
    load_player_control_settings_from_cfg(config);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));

    CursesInput input;
    input.feed(press(U'q'));
    const InputState s = input.sample();
    EXPECT_EQ(s.players[0].move_x(), -1) << "'q' is up-left in 8-direction mode";
    EXPECT_EQ(s.players[0].move_y(), -1);
}
