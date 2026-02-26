#pragma once

// InputHardwareState: per-session mutable hardware input state.
// This header is included by input.h AFTER MouseState and JoyData are defined.
// Do NOT include input.h here to avoid circular dependencies.
// Requires: MouseState, JoyData (from input.h), and integer aliases from base.h.

#include <openglad/legacy/base.h>
#include <openglad/interface/input/input_state.h>
#include "SDL.h"
#include <array>

struct InputHardwareState {
    static constexpr int kMaxNumJoysticks = 10;  // includes possible non-gamepad SDL joystick devices

    MouseState mouse{};
    JoyData player_joy_state[MAX_PLAYERS]{};
    int player_control_modes[MAX_PLAYERS]{};
    int player_mode_keys[MAX_PLAYERS][2][NUM_INPUT_KEYS]{};  // [player][mode][key]
    Sint32 mouse_buttons{0};
    std::array<SDL_Joystick*, kMaxNumJoysticks> joysticks{};

    ~InputHardwareState()
    {
        close_joysticks();
    }

    void close_joysticks()
    {
        for (SDL_Joystick*& joystick : joysticks)
        {
            if (joystick)
            {
                SDL_JoystickClose(joystick);
                joystick = nullptr;
            }
        }
    }

#ifdef USE_TOUCH_INPUT
    bool tapping{false};
    int start_tap_x{0}, start_tap_y{0};
    bool moving{false};
    int moving_touch_x{0}, moving_touch_y{0};
    int moving_touch_target_x{0}, moving_touch_target_y{0};
    std::int64_t movingTouch{0};
    bool firing{false};
    std::int64_t firingTouch{0};
    bool touch_keystate[MAX_PLAYERS][NUM_INPUT_KEYS]{};
#endif
};
