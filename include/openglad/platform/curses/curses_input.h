/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/belt_input.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/interface/input.h>
#include <openglad/platform/curses/terminal.h>

class GameWorld;

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace og::curses {

// Meta keys handled outside the per-frame InputState (the runtime acts on them).
enum class MetaAction {
    None,
    OpenGameMenu, // Esc: open the in-game PAUSED menu (R/A/K/Q)
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
    // Bind to an explicit NUM_KEYS-entry keycode table (indexed by key slot:
    // InputAction 0..15, then KEY_LOOKUP, KEY_SPEC2, KEY_SPEC3). Used by tests
    // to exercise specific bindings without the global session.
    explicit CursesInput(std::span<const int, NUM_KEYS> keybindings);

    // Apply one terminal key event (press/repeat/release) to the held/pressed set.
    void feed(const Key& k);

    // Produce the InputState for the current frame and clear the pressed edges.
    // Call once per frame after feeding all events read this frame.
    //
    // New Ability Select: SPEC 1/2/3 (slots 9, 17, 18) and SHIFTER go through
    // the same fold as the SDL sampler (og::sim::belt::fold_keys). `belt` is
    // the followed walker's resolved belt (followed_belt below), nullptr when
    // the player drives no walker; with the setting off it is not read.
    InputState sample(const og::sim::belt::Layout* belt = nullptr);

    // Clear all held/pressed state (e.g. when leaving a level).
    void reset();

    // The key table this input resolves (the KEYS page shows it).
    std::span<const int, NUM_KEYS> keybindings() const { return keybindings_; }

    // --- pure mappers (static; unit-tested directly) ---

    // SDL keycode (KEYCODE_*) for a terminal key, or KEYCODE_UNKNOWN (0) if the
    // key has no keycode (so it can never match a binding).
    static int keycode_for_key(const Key& k);

    // Meta action for a key (Esc -> menu), or MetaAction::None. Meta keys are
    // fixed (not part of the game's player bindings).
    static MetaAction meta_for_key(const Key& k);

    // The terminal key a bound keycode is typed with, for the KEYS page:
    // "W", "Space", "Left Ctrl", "F5"; "--" for an unbound slot; a keycode no
    // terminal key produces (keypad, PgUp, the Windows key) says so. The
    // inverse of keycode_for_key: every name is checked against it.
    static std::string key_label(int keycode);

private:
    // The NUM_KEYS SDL keycodes of player_keys_[0], indexed by key slot. The
    // KEY_LOOKUP slot is resolved but sends nothing (an SDL render-time hold).
    std::span<const int, NUM_KEYS> keybindings_;
    std::array<bool, NUM_KEYS> held_{};
    std::array<bool, NUM_KEYS> pressed_edge_{};
    og::sim::belt::Latch belt_latch_{};
};

// The resolved belt of the walker with id `followed_id` in `world` (its guy's
// bytes, or the class default for a walker without a guy), or nullopt when no
// such living walker exists. The curses runtime hands it to sample().
[[nodiscard]] std::optional<og::sim::belt::Layout>
followed_belt(const GameWorld& world, std::uint32_t followed_id);

} // namespace og::curses
