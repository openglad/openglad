#pragma once

// InputHardwareState: per-session mutable hardware input state.
// This header is included by input.h AFTER MouseState and JoyData are defined.
// Do NOT include input.h here to avoid circular dependencies.
// Requires: MouseState, JoyData (from input.h).

#include <cstdint>

#include <openglad/interface/input_direction_grace.h>

struct InputHardwareState {
    MouseState mouse{};
    JoyData player_joy[4]{};
    int player_control_modes[4]{};
    // Factory keymap identity follows a profile when seats compact. This lets
    // per-seat RESET restore that profile's own layout after remove/add.
    int player_control_default_profiles[4]{0, 1, 2, 3};
    int player_mode_keys[4][2][17]{};  // [player][mode][key] — key dim == NUM_KEYS
    // Diagonal release retention (input_state_from_sdl); per player.
    DirectionGraceState direction_grace[4]{};
    std::int32_t mouse_buttons{0};
    bool picker_was_left_down{false};
    bool picker_was_right_down{false};

    // Runtime touch-control seam: held-key state from the web DOM overlay or
    // native SDL touch path, OR-ed into isPlayerHoldingKey. key dim == NUM_KEYS.
    bool touch_keystate[4][17]{};

#ifdef USE_TOUCH_INPUT
    bool tapping{false};
    int start_tap_x{0}, start_tap_y{0};
    bool moving{false};
    int moving_touch_x{0}, moving_touch_y{0};
    int moving_touch_target_x{0}, moving_touch_target_y{0};
    std::int64_t movingTouch{0};
    bool firing{false};
    std::int64_t firingTouch{0};
#endif
};
