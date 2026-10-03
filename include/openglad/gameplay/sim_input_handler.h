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

// Ticks one silent-failure cue (#222) silences the next one. Deliberately
// longer than STANDARD_TEXT_TIME (75), the feed's own display time: at a
// shorter window a mashed key stacks two or three copies of the same line in
// the five-slot feed and evicts everything else.
inline constexpr short kSimCueThrottleTicks = 80;

// Per-player debounce state for input handling.
// Persists across frames, owned by the viewscreen or caller.
struct SimInputDebounce
{
    short changedchar = 0;
    short changedspec = 0;
    // Throttle for the silent-failure cues (#222). The held-Special path
    // re-casts every frame, so a failing cast would otherwise write one feed
    // line per frame. It lives here, not on the walker: walker members ride
    // the snapshot wire and the parity dump, and a cosmetic cue may not move
    // either.
    short cue_delay = 0;
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
//   sim_events   - delivery channel for the yell sound and the yell /
//                  summon / release notifications (nullable; emission is a
//                  no-op on a null log). The authoritative server passes its
//                  own log so these cues ride the per-tick event batch out to
//                  every client mirror.
//
// Switch Special: the special a press lands on is decided by the LIVE family
// registry (get_family_descriptor) for every registered family, core or
// pack; a slot named NONE, an out-of-range slot, or a level below
// (special-1)*3+1 wraps to special 1 (issue #321).
//
// Returns a SimInputResult describing what happened (for render layer to act on).
SimInputResult sim_process_player_input(
    const PlayerInput& pi,
    walker*& control,
    GameWorld& level,
    short player_num,
    short my_team,
    SimInputDebounce& debounce,
    og::sim::SimEventLog* sim_events);

// Find the next available control walker for a player.
// Searches level_data.oblist for unclaimed player chars on my_team, then any
// unclaimed team member; never another team (returns nullptr instead).
walker* sim_find_next_control(GameWorld& level, short my_team);

// Advance a walker's current special by one and wrap to special 1 when the slot
// is out of range, unnamed/NONE in the LIVE family registry, or above the level
// gate (special-1)*3+1 (issue #321). Precondition: control.stats() != nullptr.
// The ONE home of the cycling rule: sim_process_player_input (debounced) and the
// parity harness (scenario_runtime.cpp) both call it.
void sim_advance_current_special(walker& control);

// The ONE home of the SwitchChar rule: sim_process_player_input (debounced,
// Cheat-gated) and the parity harness (scenario_runtime.cpp) both call it.
// Releases `control` when `player_num` holds it (restore_act_type, user -1),
// then returns the next walker after `anchor` in oblist order (before it when
// `reverse`), wrapping, that is alive, not dormant, Living, friendly to
// `anchor`, on `my_team`, real_team 255, unclaimed (user -1) and
// control_claim_allowed for `player_num`; nullptr when nobody qualifies or
// `anchor` is not in the oblist. The result is NOT claimed here: the caller
// claims it with sim_claim_control in the same call.
walker* sim_switch_control(GameWorld& level, walker& control, walker* anchor,
                           short my_team, short player_num, bool reverse);

// The ONE home of the control claim: sim_process_player_input (the per-tick
// claim and the SwitchChar claim) and the parity harness (scenario_runtime.cpp)
// both call it. When control_claim_allowed lets `player_num` hold `control`,
// sets ACT_CONTROL, user = player_num and clears the command queue with
// clear_command_for_control_switch (forced fright and charm survive) and
// returns true; otherwise touches nothing and returns false.
// Precondition: control.stats() != nullptr.
bool sim_claim_control(GameWorld& level, walker& control, short player_num);

// Cycle through the oblist starting after `current`, wrapping around,
// returning the first living walker that satisfies `pred`.
// If `reverse` is true, iterates backward.  Returns nullptr if none found.
walker* sim_cycle_next_character(
    const std::list<std::unique_ptr<walker>>& oblist,
    walker* current,
    bool reverse,
    const std::function<bool(const walker*)>& pred);
