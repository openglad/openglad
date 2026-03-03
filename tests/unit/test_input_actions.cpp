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

OG_UNIT_TEST(test_input_action_enum_values_match_key_constants)
{
    // InputAction values must align with the legacy KEY_* indices
    OG_ASSERT(static_cast<int>(InputAction::MoveUp) == 0);
    OG_ASSERT(static_cast<int>(InputAction::MoveUpRight) == 1);
    OG_ASSERT(static_cast<int>(InputAction::MoveRight) == 2);
    OG_ASSERT(static_cast<int>(InputAction::MoveDownRight) == 3);
    OG_ASSERT(static_cast<int>(InputAction::MoveDown) == 4);
    OG_ASSERT(static_cast<int>(InputAction::MoveDownLeft) == 5);
    OG_ASSERT(static_cast<int>(InputAction::MoveLeft) == 6);
    OG_ASSERT(static_cast<int>(InputAction::MoveUpLeft) == 7);
    OG_ASSERT(static_cast<int>(InputAction::Fire) == 8);
    OG_ASSERT(static_cast<int>(InputAction::Special) == 9);
    OG_ASSERT(static_cast<int>(InputAction::SwitchChar) == 10);
    OG_ASSERT(static_cast<int>(InputAction::SwitchSpecial) == 11);
    OG_ASSERT(static_cast<int>(InputAction::Yell) == 12);
    OG_ASSERT(static_cast<int>(InputAction::Shift) == 13);
    OG_ASSERT(static_cast<int>(InputAction::OpenPrefs) == 14);
    OG_ASSERT(static_cast<int>(InputAction::Cheat) == 15);
    OG_ASSERT(static_cast<int>(InputAction::Count) == 16);
    OG_ASSERT(kInputActionCount == NUM_INPUT_KEYS);
}

// ---------------------------------------------------------------------------
// PlayerInput held/pressed accessors
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_player_input_is_held)
{
    PlayerInput pi = {};
    OG_ASSERT(!pi.is_held(InputAction::Fire));

    pi.held[static_cast<int>(InputAction::Fire)] = true;
    OG_ASSERT(pi.is_held(InputAction::Fire));
    OG_ASSERT(!pi.is_held(InputAction::Special));
}

OG_UNIT_TEST(test_player_input_was_pressed)
{
    PlayerInput pi = {};
    OG_ASSERT(!pi.was_pressed(InputAction::Yell));

    pi.pressed[static_cast<int>(InputAction::Yell)] = true;
    OG_ASSERT(pi.was_pressed(InputAction::Yell));
    OG_ASSERT(!pi.was_pressed(InputAction::Fire));
}

OG_UNIT_TEST(test_player_input_held_and_pressed_independent)
{
    PlayerInput pi = {};
    // Key can be held but not pressed (held across frames)
    pi.held[static_cast<int>(InputAction::Fire)] = true;
    pi.pressed[static_cast<int>(InputAction::Fire)] = false;
    OG_ASSERT(pi.is_held(InputAction::Fire));
    OG_ASSERT(!pi.was_pressed(InputAction::Fire));

    // Key can be both held and pressed (first frame of hold)
    pi.pressed[static_cast<int>(InputAction::Fire)] = true;
    OG_ASSERT(pi.is_held(InputAction::Fire));
    OG_ASSERT(pi.was_pressed(InputAction::Fire));
}

// ---------------------------------------------------------------------------
// PlayerInput movement derivation
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_player_input_move_x_no_keys)
{
    PlayerInput pi = {};
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_player_input_move_x_left)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveLeft)] = true;
    OG_ASSERT(pi.move_x() == -1);
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_player_input_move_x_right)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveRight)] = true;
    OG_ASSERT(pi.move_x() == 1);
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_player_input_move_y_up)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUp)] = true;
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == -1);
}

OG_UNIT_TEST(test_player_input_move_y_down)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveDown)] = true;
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 1);
}

OG_UNIT_TEST(test_player_input_move_diagonal_up_left)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUpLeft)] = true;
    OG_ASSERT(pi.move_x() == -1);
    OG_ASSERT(pi.move_y() == -1);
}

OG_UNIT_TEST(test_player_input_move_diagonal_down_right)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveDownRight)] = true;
    OG_ASSERT(pi.move_x() == 1);
    OG_ASSERT(pi.move_y() == 1);
}

OG_UNIT_TEST(test_player_input_move_up_right_key)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUpRight)] = true;
    OG_ASSERT(pi.move_x() == 1);
    OG_ASSERT(pi.move_y() == -1);
}

OG_UNIT_TEST(test_player_input_move_down_left_key)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveDownLeft)] = true;
    OG_ASSERT(pi.move_x() == -1);
    OG_ASSERT(pi.move_y() == 1);
}

OG_UNIT_TEST(test_player_input_conflicting_directions_cancel)
{
    // Holding both left and right should cancel out
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveLeft)] = true;
    pi.held[static_cast<int>(InputAction::MoveRight)] = true;
    OG_ASSERT(pi.move_x() == 0);
}

OG_UNIT_TEST(test_player_input_conflicting_vertical_cancel)
{
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUp)] = true;
    pi.held[static_cast<int>(InputAction::MoveDown)] = true;
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_player_input_cardinal_plus_diagonal_x)
{
    // MoveUp + MoveUpRight should give x=1, y=-1
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::MoveUp)] = true;
    pi.held[static_cast<int>(InputAction::MoveUpRight)] = true;
    OG_ASSERT(pi.move_x() == 1);
    OG_ASSERT(pi.move_y() == -1);
}

// ---------------------------------------------------------------------------
// InputState multi-player isolation
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_input_state_player_isolation)
{
    InputState state = {};

    // Set actions for different players
    state.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    state.players[1].held[static_cast<int>(InputAction::Special)] = true;
    state.players[2].held[static_cast<int>(InputAction::MoveUp)] = true;
    state.players[3].held[static_cast<int>(InputAction::Yell)] = true;

    // Each player should only see their own actions
    OG_ASSERT(state.players[0].is_held(InputAction::Fire));
    OG_ASSERT(!state.players[0].is_held(InputAction::Special));
    OG_ASSERT(!state.players[0].is_held(InputAction::MoveUp));
    OG_ASSERT(!state.players[0].is_held(InputAction::Yell));

    OG_ASSERT(!state.players[1].is_held(InputAction::Fire));
    OG_ASSERT(state.players[1].is_held(InputAction::Special));

    OG_ASSERT(!state.players[2].is_held(InputAction::Fire));
    OG_ASSERT(state.players[2].is_held(InputAction::MoveUp));
    OG_ASSERT(state.players[2].move_y() == -1);

    OG_ASSERT(!state.players[3].is_held(InputAction::Fire));
    OG_ASSERT(state.players[3].is_held(InputAction::Yell));
}

OG_UNIT_TEST(test_input_state_all_four_players_move)
{
    InputState state = {};

    // Each player moves a different direction
    state.players[0].held[static_cast<int>(InputAction::MoveUp)] = true;
    state.players[1].held[static_cast<int>(InputAction::MoveRight)] = true;
    state.players[2].held[static_cast<int>(InputAction::MoveDown)] = true;
    state.players[3].held[static_cast<int>(InputAction::MoveLeft)] = true;

    OG_ASSERT(state.players[0].move_y() == -1);
    OG_ASSERT(state.players[0].move_x() == 0);

    OG_ASSERT(state.players[1].move_x() == 1);
    OG_ASSERT(state.players[1].move_y() == 0);

    OG_ASSERT(state.players[2].move_y() == 1);
    OG_ASSERT(state.players[2].move_x() == 0);

    OG_ASSERT(state.players[3].move_x() == -1);
    OG_ASSERT(state.players[3].move_y() == 0);
}

// ---------------------------------------------------------------------------
// InputState clear
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_input_state_clear)
{
    InputState state = {};
    state.players[0].held[static_cast<int>(InputAction::Fire)] = true;
    state.players[0].pressed[static_cast<int>(InputAction::Fire)] = true;
    state.players[1].held[static_cast<int>(InputAction::MoveUp)] = true;
    state.quit_requested = true;

    state.clear();

    OG_ASSERT(!state.players[0].is_held(InputAction::Fire));
    OG_ASSERT(!state.players[0].was_pressed(InputAction::Fire));
    OG_ASSERT(!state.players[1].is_held(InputAction::MoveUp));
    OG_ASSERT(!state.quit_requested);
}

// ---------------------------------------------------------------------------
// Key combination patterns used in gameplay
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_shift_plus_yell_combo)
{
    // Shift+Yell triggers "summon defense" behavior
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Shift)] = true;
    pi.pressed[static_cast<int>(InputAction::Yell)] = true;

    OG_ASSERT(pi.is_held(InputAction::Shift));
    OG_ASSERT(pi.was_pressed(InputAction::Yell));
    OG_ASSERT(!pi.is_held(InputAction::Cheat));
}

OG_UNIT_TEST(test_cheat_plus_switch_combo)
{
    // Cheat+Switch triggers "change team" behavior
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Cheat)] = true;
    pi.pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    OG_ASSERT(pi.is_held(InputAction::Cheat));
    OG_ASSERT(pi.was_pressed(InputAction::SwitchChar));
}

OG_UNIT_TEST(test_cheat_blocks_normal_switch)
{
    // When Cheat is held, normal switch should be blocked by game logic
    // (The actual blocking logic is in viewscreen::process_input, but
    // the input layer correctly reports both keys)
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Cheat)] = true;
    pi.pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    // Both are correctly reported
    OG_ASSERT(pi.is_held(InputAction::Cheat));
    OG_ASSERT(pi.was_pressed(InputAction::SwitchChar));
}

OG_UNIT_TEST(test_shift_modifies_switch_direction)
{
    // Shift+Switch goes backward through character list
    PlayerInput pi = {};
    pi.held[static_cast<int>(InputAction::Shift)] = true;
    pi.pressed[static_cast<int>(InputAction::SwitchChar)] = true;

    OG_ASSERT(pi.is_held(InputAction::Shift));
    OG_ASSERT(pi.was_pressed(InputAction::SwitchChar));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_no_keys_held)
{
    PlayerInput pi = {};
    for (int i = 0; i < kInputActionCount; i++)
    {
        OG_ASSERT(!pi.held[i]);
        OG_ASSERT(!pi.pressed[i]);
    }
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_all_keys_held)
{
    PlayerInput pi = {};
    for (int i = 0; i < kInputActionCount; i++)
    {
        pi.held[i] = true;
        pi.pressed[i] = true;
    }

    // All actions should be reported as held/pressed
    OG_ASSERT(pi.is_held(InputAction::Fire));
    OG_ASSERT(pi.is_held(InputAction::Special));
    OG_ASSERT(pi.is_held(InputAction::Cheat));
    OG_ASSERT(pi.was_pressed(InputAction::Fire));

    // Movement: all directions held, left+right and up+down cancel
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_all_directions_held_cancel)
{
    // All 8 directional keys held: should cancel to (0,0)
    PlayerInput pi = {};
    for (int i = 0; i <= 7; i++)
        pi.held[i] = true;
    OG_ASSERT(pi.move_x() == 0);
    OG_ASSERT(pi.move_y() == 0);
}

OG_UNIT_TEST(test_input_action_count_matches_num_input_keys)
{
    OG_ASSERT(kInputActionCount == NUM_INPUT_KEYS);
    OG_ASSERT(static_cast<int>(InputAction::Count) == NUM_INPUT_KEYS);
}

// ---------------------------------------------------------------------------
// InputKey enum consistency with InputAction
// ---------------------------------------------------------------------------

OG_UNIT_TEST(test_input_key_and_input_action_alignment)
{
    // InputKey (game_context.h) and InputAction (input_action.h) must
    // have matching integer values so they index into the same arrays.
    OG_ASSERT(static_cast<int>(InputKey::Up) == static_cast<int>(InputAction::MoveUp));
    OG_ASSERT(static_cast<int>(InputKey::Right) == static_cast<int>(InputAction::MoveRight));
    OG_ASSERT(static_cast<int>(InputKey::Down) == static_cast<int>(InputAction::MoveDown));
    OG_ASSERT(static_cast<int>(InputKey::Left) == static_cast<int>(InputAction::MoveLeft));
    OG_ASSERT(static_cast<int>(InputKey::Fire) == static_cast<int>(InputAction::Fire));
    OG_ASSERT(static_cast<int>(InputKey::Special) == static_cast<int>(InputAction::Special));
    OG_ASSERT(static_cast<int>(InputKey::Switch) == static_cast<int>(InputAction::SwitchChar));
    OG_ASSERT(static_cast<int>(InputKey::SpecialSwitch) == static_cast<int>(InputAction::SwitchSpecial));
    OG_ASSERT(static_cast<int>(InputKey::Yell) == static_cast<int>(InputAction::Yell));
    OG_ASSERT(static_cast<int>(InputKey::Shifter) == static_cast<int>(InputAction::Shift));
    OG_ASSERT(static_cast<int>(InputKey::Prefs) == static_cast<int>(InputAction::OpenPrefs));
    OG_ASSERT(static_cast<int>(InputKey::Cheat) == static_cast<int>(InputAction::Cheat));
}
