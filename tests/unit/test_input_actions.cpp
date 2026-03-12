/*
 * Headless unit tests for the semantic input layer.
 *
 * These tests exercise InputAction, PlayerInput, and InputState without
 * any SDL dependency — the whole point of the abstraction.
 */
#include "unit.h"
#include <openglad/interface/input_action.h>
#include <openglad/platform/game_context.h>
#include <cstring>

// ---------------------------------------------------------------------------
// InputAction enum basics
// ---------------------------------------------------------------------------

TEST(InputActions, input_action_enum_values_match_key_constants)
{
    // InputAction values must align with the legacy KEY_* indices
    ASSERT_TRUE(static_cast<int>(InputAction::MoveUp) == 0);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveUpRight) == 1);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveRight) == 2);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveDownRight) == 3);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveDown) == 4);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveDownLeft) == 5);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveLeft) == 6);
    ASSERT_TRUE(static_cast<int>(InputAction::MoveUpLeft) == 7);
    ASSERT_TRUE(static_cast<int>(InputAction::Fire) == 8);
    ASSERT_TRUE(static_cast<int>(InputAction::Special) == 9);
    ASSERT_TRUE(static_cast<int>(InputAction::SwitchChar) == 10);
    ASSERT_TRUE(static_cast<int>(InputAction::SwitchSpecial) == 11);
    ASSERT_TRUE(static_cast<int>(InputAction::Yell) == 12);
    ASSERT_TRUE(static_cast<int>(InputAction::Shift) == 13);
    ASSERT_TRUE(static_cast<int>(InputAction::OpenPrefs) == 14);
    ASSERT_TRUE(static_cast<int>(InputAction::Cheat) == 15);
    ASSERT_TRUE(static_cast<int>(InputAction::Count) == 16);
    ASSERT_TRUE(kInputActionCount == NUM_INPUT_KEYS);
}

// ---------------------------------------------------------------------------
// PlayerInput held/pressed accessors
// ---------------------------------------------------------------------------

TEST(InputActions, player_input_is_held)
{
    PlayerInput pi = {};
    ASSERT_TRUE(!pi.is_held(InputAction::Fire));

    pi.held[static_cast<int>(InputAction::Fire)] = true;
    ASSERT_TRUE(pi.is_held(InputAction::Fire));
    ASSERT_TRUE(!pi.is_held(InputAction::Special));
}

TEST(InputActions, player_input_was_pressed)
{
    PlayerInput pi = {};
    ASSERT_TRUE(!pi.was_pressed(InputAction::Yell));

    pi.pressed[static_cast<int>(InputAction::Yell)] = true;
    ASSERT_TRUE(pi.was_pressed(InputAction::Yell));
    ASSERT_TRUE(!pi.was_pressed(InputAction::Fire));
}

TEST(InputActions, player_input_held_and_pressed_independent)
{
    PlayerInput pi = {};
    // Key can be held but not pressed (held across frames)
    pi.held[static_cast<int>(InputAction::Fire)] = true;
    pi.pressed[static_cast<int>(InputAction::Fire)] = false;
    ASSERT_TRUE(pi.is_held(InputAction::Fire));
    ASSERT_TRUE(!pi.was_pressed(InputAction::Fire));

    // Key can be both held and pressed (first frame of hold)
    pi.pressed[static_cast<int>(InputAction::Fire)] = true;
    ASSERT_TRUE(pi.is_held(InputAction::Fire));
    ASSERT_TRUE(pi.was_pressed(InputAction::Fire));
}

// ---------------------------------------------------------------------------
// PlayerInput movement derivation
// ---------------------------------------------------------------------------

TEST(InputActions, player_input_move_x_no_keys)
{
    PlayerInput pi = {};
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, player_input_move_x_left)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveLeft)] = true;
    ASSERT_TRUE(pi.move_x() == -1);
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, player_input_move_x_right)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveRight)] = true;
    ASSERT_TRUE(pi.move_x() == 1);
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, player_input_move_y_up)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUp)] = true;
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == -1);
}

TEST(InputActions, player_input_move_y_down)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveDown)] = true;
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == 1);
}

TEST(InputActions, player_input_move_diagonal_up_left)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUpLeft)] = true;
    ASSERT_TRUE(pi.move_x() == -1);
    ASSERT_TRUE(pi.move_y() == -1);
}

TEST(InputActions, player_input_move_diagonal_down_right)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveDownRight)] = true;
    ASSERT_TRUE(pi.move_x() == 1);
    ASSERT_TRUE(pi.move_y() == 1);
}

TEST(InputActions, player_input_move_up_right_key)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUpRight)] = true;
    ASSERT_TRUE(pi.move_x() == 1);
    ASSERT_TRUE(pi.move_y() == -1);
}

TEST(InputActions, player_input_move_down_left_key)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveDownLeft)] = true;
    ASSERT_TRUE(pi.move_x() == -1);
    ASSERT_TRUE(pi.move_y() == 1);
}

TEST(InputActions, player_input_conflicting_directions_cancel)
{
    // Holding both left and right should cancel out
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveLeft)] = true;
    pi.held[static_cast<int>(InputAction::MoveRight)] = true;
    ASSERT_TRUE(pi.move_x() == 0);
}

TEST(InputActions, player_input_conflicting_vertical_cancel)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUp)] = true;
    pi.held[static_cast<int>(InputAction::MoveDown)] = true;
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, player_input_cardinal_plus_diagonal_x)
{
    // MoveUp + MoveUpRight should give x=1, y=-1
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUp)] = true;
    pi.held[static_cast<int>(InputAction::MoveUpRight)] = true;
    ASSERT_TRUE(pi.move_x() == 1);
    ASSERT_TRUE(pi.move_y() == -1);
}

// ---------------------------------------------------------------------------
// InputState multi-player isolation
// ---------------------------------------------------------------------------

TEST(InputActions, input_state_player_isolation)
{
    InputState state = {};

    // Set actions for different players
    state.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    state.players[1].held[static_cast<int>(InputAction::Special)] = true;
    state.players[2].held[static_cast<int>(InputAction::MoveUp)] = true;
    state.players[3].held[static_cast<int>(InputAction::Yell)] = true;

    // Each player should only see their own actions
    ASSERT_TRUE(state.players[0].is_held(InputAction::Fire));
    ASSERT_TRUE(!state.players[0].is_held(InputAction::Special));
    ASSERT_TRUE(!state.players[0].is_held(InputAction::MoveUp));
    ASSERT_TRUE(!state.players[0].is_held(InputAction::Yell));

    ASSERT_TRUE(!state.players[1].is_held(InputAction::Fire));
    ASSERT_TRUE(state.players[1].is_held(InputAction::Special));

    ASSERT_TRUE(!state.players[2].is_held(InputAction::Fire));
    ASSERT_TRUE(state.players[2].is_held(InputAction::MoveUp));
    ASSERT_TRUE(state.players[2].move_y() == -1);

    ASSERT_TRUE(!state.players[3].is_held(InputAction::Fire));
    ASSERT_TRUE(state.players[3].is_held(InputAction::Yell));
}

TEST(InputActions, input_state_all_four_players_move)
{
    InputState state = {};

    // Each player moves a different direction
    state.players[0].held[static_cast<int>(InputAction::MoveUp)] = true;
    state.players[1].held[static_cast<int>(InputAction::MoveRight)] = true;
    state.players[2].held[static_cast<int>(InputAction::MoveDown)] = true;
    state.players[3].held[static_cast<int>(InputAction::MoveLeft)] = true;

    ASSERT_TRUE(state.players[0].move_y() == -1);
    ASSERT_TRUE(state.players[0].move_x() == 0);

    ASSERT_TRUE(state.players[1].move_x() == 1);
    ASSERT_TRUE(state.players[1].move_y() == 0);

    ASSERT_TRUE(state.players[2].move_y() == 1);
    ASSERT_TRUE(state.players[2].move_x() == 0);

    ASSERT_TRUE(state.players[3].move_x() == -1);
    ASSERT_TRUE(state.players[3].move_y() == 0);
}

// ---------------------------------------------------------------------------
// InputState clear
// ---------------------------------------------------------------------------

TEST(InputActions, input_state_clear)
{
    InputState state = {};
    state.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    state.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    state.players[1].held[static_cast<int>(InputAction::MoveUp)] = true;
    state.quit_requested = true;

    state.clear();

    ASSERT_TRUE(!state.players[0].is_held(InputAction::Fire));
    ASSERT_TRUE(!state.players[0].was_pressed(InputAction::Fire));
    ASSERT_TRUE(!state.players[1].is_held(InputAction::MoveUp));
    ASSERT_TRUE(!state.quit_requested);
}

// ---------------------------------------------------------------------------
// Key combination patterns used in gameplay
// ---------------------------------------------------------------------------

TEST(InputActions, shift_plus_yell_combo)
{
    // Shift+Yell triggers "summon defense" behavior
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Shift)] = true;
    pi.pressed[static_cast<int>(InputAction::Yell)] = true;

    ASSERT_TRUE(pi.is_held(InputAction::Shift));
    ASSERT_TRUE(pi.was_pressed(InputAction::Yell));
    ASSERT_TRUE(!pi.is_held(InputAction::Cheat));
}

TEST(InputActions, cheat_plus_switch_combo)
{
    // Cheat+Switch triggers "change team" behavior
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Cheat)] = true;
    pi.pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    ASSERT_TRUE(pi.is_held(InputAction::Cheat));
    ASSERT_TRUE(pi.was_pressed(InputAction::SwitchChar));
}

TEST(InputActions, cheat_blocks_normal_switch)
{
    // When Cheat is held, normal switch should be blocked by game logic
    // (The actual blocking logic is in viewscreen::process_input, but
    // the input layer correctly reports both keys)
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Cheat)] = true;
    pi.pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    // Both are correctly reported
    ASSERT_TRUE(pi.is_held(InputAction::Cheat));
    ASSERT_TRUE(pi.was_pressed(InputAction::SwitchChar));
}

TEST(InputActions, shift_modifies_switch_direction)
{
    // Shift+Switch goes backward through character list
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Shift)] = true;
    pi.pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    ASSERT_TRUE(pi.is_held(InputAction::Shift));
    ASSERT_TRUE(pi.was_pressed(InputAction::SwitchChar));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(InputActions, no_keys_held)
{
    PlayerInput pi = {};
    for (int i = 0; i < kInputActionCount; i++)
    {
        ASSERT_TRUE(!pi.held[i]);
        ASSERT_TRUE(!pi.pressed[i]);
    }
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, all_keys_held)
{
    PlayerInput pi = {};
    for (int i = 0; i < kInputActionCount; i++)
    {
        pi.held[i] = true;
        pi.pressed[i] = true;
    }

    // All actions should be reported as held/pressed
    ASSERT_TRUE(pi.is_held(InputAction::Fire));
    ASSERT_TRUE(pi.is_held(InputAction::Special));
    ASSERT_TRUE(pi.is_held(InputAction::Cheat));
    ASSERT_TRUE(pi.was_pressed(InputAction::Fire));

    // Movement: all directions held, left+right and up+down cancel
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, all_directions_held_cancel)
{
    // All 8 directional keys held: should cancel to (0,0)
    PlayerInput pi = {};
    for (int i = 0; i <= 7; i++)
        pi.held[i] = true;
    ASSERT_TRUE(pi.move_x() == 0);
    ASSERT_TRUE(pi.move_y() == 0);
}

TEST(InputActions, input_action_count_matches_num_input_keys)
{
    ASSERT_TRUE(kInputActionCount == NUM_INPUT_KEYS);
    ASSERT_TRUE(static_cast<int>(InputAction::Count) == NUM_INPUT_KEYS);
}

// ---------------------------------------------------------------------------
// InputKey enum consistency with InputAction
// ---------------------------------------------------------------------------

TEST(InputActions, input_key_and_input_action_alignment)
{
    // InputKey (game_context.h) and InputAction (input_action.h) must
    // have matching integer values so they index into the same arrays.
    ASSERT_TRUE(static_cast<int>(InputKey::Up) == static_cast<int>(InputAction::MoveUp));
    ASSERT_TRUE(static_cast<int>(InputKey::Right) == static_cast<int>(InputAction::MoveRight));
    ASSERT_TRUE(static_cast<int>(InputKey::Down) == static_cast<int>(InputAction::MoveDown));
    ASSERT_TRUE(static_cast<int>(InputKey::Left) == static_cast<int>(InputAction::MoveLeft));
    ASSERT_TRUE(static_cast<int>(InputKey::Fire) == static_cast<int>(InputAction::Fire));
    ASSERT_TRUE(static_cast<int>(InputKey::Special) == static_cast<int>(InputAction::Special));
    ASSERT_TRUE(static_cast<int>(InputKey::Switch) == static_cast<int>(InputAction::SwitchChar));
    ASSERT_TRUE(static_cast<int>(InputKey::SpecialSwitch) == static_cast<int>(InputAction::SwitchSpecial));
    ASSERT_TRUE(static_cast<int>(InputKey::Yell) == static_cast<int>(InputAction::Yell));
    ASSERT_TRUE(static_cast<int>(InputKey::Shifter) == static_cast<int>(InputAction::Shift));
    ASSERT_TRUE(static_cast<int>(InputKey::Prefs) == static_cast<int>(InputAction::OpenPrefs));
    ASSERT_TRUE(static_cast<int>(InputKey::Cheat) == static_cast<int>(InputAction::Cheat));
}
