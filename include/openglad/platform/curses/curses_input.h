/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/input_state.h>
#include <openglad/platform/curses/terminal.h>

#include <array>

namespace og::curses {

// Meta keys handled outside the per-frame InputState (the runtime acts on them).
enum class MetaAction {
    None,
    OpenGameMenu, // Esc: open the in-game menu / return to team build
};

// Translates terminal key events into the engine's per-frame InputState for the
// single local player (player 0), using THE GAME'S OWN KEYBINDINGS — there are no
// hardcoded movement/action keys here. A terminal key is converted to a SDL
// KEYCODE_* value (keycode_for_key) and then resolved to an InputAction by looking
// it up in the player's bound keys (current_session->player_keys_[0], loaded from
// the config by load_player_control_settings_from_cfg, in whatever 4/8-direction
// mode the player configured). Rebinding keys in the config (or the SDL keybind
// screen) changes the curses controls too.
//
// Input arrives via the Kitty keyboard protocol, so each event carries a real
// transition type: a Press marks the bound action held (and pressed this frame),
// a Release clears it. No decay heuristics, no clock — holding two keys, holding
// fire while moving, and Ctrl/Alt all work exactly. A focus-out event clears all
// held keys so nothing sticks down when the window loses focus.
class CursesInput
{
public:
    // Bind to the live player-0 keybindings (current_session->player_keys_[0]).
    CursesInput();
    // Bind to an explicit 16-entry keycode table (indexed by InputAction). Used by
    // tests to exercise specific bindings without the global session.
    explicit CursesInput(const int* keybindings);

    // Apply one terminal key event (press/repeat/release) to the held/pressed set.
    void feed(const Key& k);

    // Produce the InputState for the current frame and clear the pressed edges.
    // Call once per frame after feeding all events read this frame.
    InputState sample();

    // Clear all held/pressed state (e.g. when leaving a level).
    void reset();

    // --- pure mappers (static; unit-tested directly) ---

    // SDL keycode (KEYCODE_*) for a terminal key, or KEYCODE_UNKNOWN (0) if the
    // key has no keycode (so it can never match a binding).
    static int keycode_for_key(const Key& k);

    // Meta action for a key (Esc -> menu), or MetaAction::None. Meta keys are
    // fixed (not part of the game's player bindings).
    static MetaAction meta_for_key(const Key& k);

private:
    const int* keybindings_; // 16 SDL keycodes indexed by InputAction (player_keys_[0])
    std::array<bool, kInputActionCount> held_{};
    std::array<bool, kInputActionCount> pressed_edge_{};
};

} // namespace og::curses
