/*
 * Semantic input action enum.
 *
 * Provides strongly-typed, SDL-independent action names for gameplay input.
 * Code that processes player actions should use InputAction instead of the
 * raw KEY_* integer constants or SDL scancodes.
 *
 * The enum values are intentionally aligned with the legacy KEY_* indices
 * (KEY_UP=0 .. KEY_CHEAT=15) so that static_cast<int>(action) yields the
 * same index used by PlayerInput::held[] / pressed[].
 */
#pragma once

#include <cstdint>

enum class InputAction : int {
    // Movement (8 directions)
    MoveUp        = 0,
    MoveUpRight   = 1,
    MoveRight     = 2,
    MoveDownRight = 3,
    MoveDown      = 4,
    MoveDownLeft  = 5,
    MoveLeft      = 6,
    MoveUpLeft    = 7,

    // Combat
    Fire          = 8,
    Special       = 9,

    // Character management
    SwitchChar    = 10,
    SwitchSpecial = 11,

    // Communication
    Yell          = 12,

    // Modifiers
    Shift         = 13,

    // Meta
    OpenPrefs     = 14,
    Cheat         = 15,

    Count         = 16
};

inline constexpr int kInputActionCount = static_cast<int>(InputAction::Count);
