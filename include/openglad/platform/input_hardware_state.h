#pragma once

// InputHardwareState: per-session mutable hardware input state.
// This header is included by input.h AFTER MouseState and JoyData are defined.
// Do NOT include input.h here to avoid circular dependencies.
// Requires: MouseState, JoyData (from input.h), and integer aliases from base.h.

#include <openglad/legacy/base.h>

struct InputHardwareState {
    MouseState mouse{};
    JoyData player_joy_state[4]{};
    int player_control_modes[4]{};
    int player_mode_keys[4][2][16]{};  // [player][mode][key]
    Sint32 mouse_buttons{0};

#ifdef USE_TOUCH_INPUT
    bool tapping{false};
    int start_tap_x{0}, start_tap_y{0};
    bool moving{false};
    int moving_touch_x{0}, moving_touch_y{0};
    int moving_touch_target_x{0}, moving_touch_target_y{0};
    std::int64_t movingTouch{0};
    bool firing{false};
    std::int64_t firingTouch{0};
    bool touch_keystate[4][16]{};
#endif
};
