/*
 * Semantic input action enum.
 *
 * Provides strongly-typed, SDL-independent action names for gameplay input.
 * The enum values are intentionally aligned with KEY_* legacy indices.
 */
#pragma once

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
