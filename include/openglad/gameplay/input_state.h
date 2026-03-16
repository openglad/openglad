/*
 * PlayerInput / InputState — per-frame input snapshot.
 *
 * SDL-independent data structures for player input.
 */
#pragma once

#include <cstdint>

#include <openglad/gameplay/input_action.h>

inline constexpr int MAX_PLAYERS = 4;
inline constexpr int NUM_INPUT_KEYS = 16;
inline constexpr std::int8_t kNoTimerWaitRequest = -1;

// Indices match KEY_UP..KEY_CHEAT from input.h.
enum class InputKey : int {
    Up = 0, UpRight = 1, Right = 2, DownRight = 3,
    Down = 4, DownLeft = 5, Left = 6, UpLeft = 7,
    Fire = 8, Special = 9, Switch = 10, SpecialSwitch = 11,
    Yell = 12, Shifter = 13, Prefs = 14, Cheat = 15
};

struct PlayerInput {
    // Held state: true while the key is physically down.
    bool held[NUM_INPUT_KEYS] = {};

    // Pressed this frame: true only on the frame the key transitions down.
    bool pressed[NUM_INPUT_KEYS] = {};

    bool is_held(InputAction action) const {
        return held[static_cast<int>(action)];
    }
    bool was_pressed(InputAction action) const {
        return pressed[static_cast<int>(action)];
    }

    // Derived movement direction from held directional keys (-1, 0, or 1).
    int move_x() const;
    int move_y() const;
};

struct InputState {
    PlayerInput players[MAX_PLAYERS] = {};
    bool quit_requested = false;
    std::int8_t timer_wait_request = kNoTimerWaitRequest;

    void clear();
};
