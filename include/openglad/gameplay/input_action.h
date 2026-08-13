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
    // OpenPrefs opened the retired per-player options menu. The slot stays
    // RESERVED — input_state_net packs exactly Count held + Count pressed
    // bits per player, so removing it shifts Cheat and breaks the protocol.
    // It is never bound and never dispatched.
    OpenPrefs     = 14,
    Cheat         = 15,

    Count         = 16
};

inline constexpr int kInputActionCount = static_cast<int>(InputAction::Count);
