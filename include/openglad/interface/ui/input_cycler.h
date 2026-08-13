/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The per-seat INPUT cycler shared by Base Camp seat settings and the pause
// menu's player screen (design §2.2): keyboard mapping names first (factory +
// saved customs, excluding names other active seats hold), then JOY{n} for
// each connected joystick not assigned to another seat.
//
// SDL-side only: this composes the joystick assignment API (implemented in
// interface input, absent from headless links) with the SDL-free mapping
// library. Do not add this TU to any headless target source list.

#include <string>
#include <vector>

class cfg_store;

namespace og::ui {

struct InputCycleOption {
    std::string name;        // display name: "WASD", "ARROWS", ..., "JOY1"
    bool is_joystick = false;
    int joystick_device = -1; // 0-based device index when is_joystick
};

// Ordered cycler options for `seat` (0-based) given `active_player_count`
// seats. Always contains at least the seat's current selection.
std::vector<InputCycleOption> input_cycle_options(cfg_store& cfg,
                                                  int seat,
                                                  int active_player_count);

// The seat's current selection (JOY{n} when a joystick is assigned, else the
// derived keyboard mapping name).
InputCycleOption current_input_selection(int seat);

// Apply one option: joystick options assign the device; keyboard options
// clear any assigned joystick and load the named mapping (library entry or
// factory) into the seat. Returns false when nothing changed.
bool apply_input_cycle_selection(cfg_store& cfg,
                                 int seat,
                                 const InputCycleOption& option);

// Advance the seat to the next cycler option (wrapping) and return its name.
// No-op (returns the current name) when the seat is the only option.
std::string cycle_player_input(cfg_store& cfg, int seat, int active_player_count);

// A freshly added seat inherits profile-pool slot `seat`'s live keymap, which
// can duplicate a mapping another ACTIVE seat already answers to (cycle P1 to
// ARROWS, add P2: slot 1's live keymap IS factory arrows). When the seat's
// current keyboard mapping collides with another active seat's, load the
// first non-joystick cycler option that is not the colliding name (the option
// list already excludes names other seats hold) into the seat. Returns
// whether the seat changed; the caller persists. Joystick-driven seats are
// unique by construction and never change.
bool ensure_unique_seat_mapping(cfg_store& cfg, int seat, int active_player_count);

// "INPUT: {short name}" — fits a 98px button face at 16 chars.
std::string input_cycle_button_label(int seat);

} // namespace og::ui
