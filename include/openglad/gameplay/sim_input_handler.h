/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <string>
#include <functional>
#include <list>
#include <memory>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/statistics.h>  // NUM_SPECIALS

class walker;
class GameWorld;

namespace og::sim {
class SimEventLog;
}

// Result of processing one player's input through the sim layer.
// The render layer uses this to update display state (HP bars, notifications).
struct SimInputResult
{
    walker* new_control = nullptr;   // If control changed, points to new control
    bool endgame_requested = false;  // Set if no control found (all dead)
    short endgame_type = 0;
    bool control_hp_changed = false; // Set if render should update control HP
    float control_hp = 0.0f;        // New HP value for display
    std::string notify_text;         // Non-empty if a notification should be shown
    walker* notify_source = nullptr; // Source walker for notification
    int play_sound = -1;             // Sound ID to play (-1 = none)
};

// Per-player debounce state for input handling.
// Persists across frames, owned by the viewscreen or caller.
struct SimInputDebounce
{
    short changedchar = 0;
    short changedspec = 0;
};

// Process one player's input for the simulation layer.
// Handles: control setup, bonus rounds, character switching, special switching,
// yell commands, team summon/release, movement, fire, and special abilities.
//
// Parameters:
//   pi           - SDL-independent input state for this player
//   control      - in/out: current controlled walker (may be reassigned)
//   level        - the current level data (for entity iteration)
//   player_num   - player index (0-3)
//   my_team      - team number for this player
//   debounce     - per-player debounce state
//   special_names - special ability names array [NUM_FAMILIES][NUM_SPECIALS]
//   sim_events   - event log for sound/notification emission (nullable)
//
// Returns a SimInputResult describing what happened (for render layer to act on).
SimInputResult sim_process_player_input(
    const PlayerInput& pi,
    walker*& control,
    GameWorld& level,
    short player_num,
    short my_team,
    SimInputDebounce& debounce,
    const std::string (*special_names)[NUM_SPECIALS],
    [[maybe_unused]] og::sim::SimEventLog* sim_events);

// Find the next available control walker for a player.
// Searches level_data.oblist for: player chars, team members, then any
// unclaimed alive player character.
walker* sim_find_next_control(GameWorld& level, short my_team);

// Cycle through the oblist starting after `current`, wrapping around,
// returning the first living walker that satisfies `pred`.
// If `reverse` is true, iterates backward.  Returns nullptr if none found.
walker* sim_cycle_next_character(
    const std::list<std::unique_ptr<walker>>& oblist,
    walker* current,
    bool reverse,
    const std::function<bool(const walker*)>& pred);
