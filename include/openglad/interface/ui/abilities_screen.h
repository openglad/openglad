/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// ABILITIES (New Ability Select, docs/ability-select-design.md E.1): the
// TRAIN screen's door to which ability sits on which SPEC key. The page's
// words and edit rules are og::ui::ability_page and its edits
// (picker_common.h); this screen is their SDL projection.
//
// Six belt slots (keys 1-3, then SHIFTER + keys 1-3) over the bench. Click
// a slot, then an ability (or another slot to swap); the selected slot
// clicked again, or any slot right-clicked, empties it. DEFAULTS puts the
// class default back. The screen edits a copy and BACK commits it to the
// hero once: one roster push and one autosave per visit.

#include <openglad/gameplay/families/belt.h>
#include <openglad/interface/ui/menu_screen_spec.h>

#include <cstdint>
#include <string>

struct button;
class walker;

namespace og::ui {

const MenuScreenSpec& abilities_menu_screen_spec();

// Open ABILITIES on the hero in save team_list slot `team_slot` (TRAIN's
// current slot). Blocks until the screen closes. A local BACK commits the
// edited layout when it differs (picker_base_camp_after_roster_mutation:
// lobby push, READY clear, autosave) and returns MENU_REDRAW; a host's GO
// while parked here returns the remote start's MENU_EXIT and commits
// nothing. A slot this machine may not edit
// (picker_lobby_save_slot_editable) opens read-only. An empty slot returns
// MENU_REDRAW at once.
Sint32 run_abilities_menu(int team_slot);

// --- The PAUSED menu's copy (#376) -------------------------------------------
//
// The same screen, opened from the pause player screen's ABILITIES door over
// the character the seat drives right now: its name, family and saved layout
// from the mirror walker's guy, its level from the walker, New Specials from
// the world. A body with no guy record (a summon, a raised dead, a possessed
// host) opens read-only and keeps its class default. BACK with a changed
// layout sends it to the server through the host (one BeltChange) and clears
// the seat's SPEC key latch, so a key held through the pause cannot cast the
// old entry after RESUME. The screen closes without sending anything when the
// session ends or the seat starts driving another body.

struct PauseAbilitiesHost {
    int seat = 0;  // the local seat (its view and its input slot)
    // One paused-session pump per frame (transport + pause keep-alive);
    // false = the session ended and the screen closes.
    bool (*pump)(void* context) = nullptr;
    void* pump_context = nullptr;
    // Sends the layout for the walker `entity_id` the seat drives; false when
    // nothing could carry it.
    bool (*set_belt)(int seat, std::uint32_t entity_id,
                     const og::sim::belt::Bytes& belt) = nullptr;
};

struct PauseAbilitiesResult {
    bool opened = false;           // the seat drove a living body
    bool session_ended = false;    // the pump said the session is over
    bool control_changed = false;  // the seat drives another body now
    bool committed = false;        // a changed layout went to the host
};

// The walker local seat `seat` drives (its view's control), or nullptr when
// it drives none (a menu, a dead or empty seat). The SPEC keys and this
// screen read the same one.
walker* seat_driven_walker(int seat);

const MenuScreenSpec& abilities_pause_menu_screen_spec();

// Blocks until BACK, the session ending, or the seat's body changing.
PauseAbilitiesResult run_pause_abilities_screen(const PauseAbilitiesHost& host);

// The open screen's status line (og::ui::ability_status_line for the
// selected row, or the read-only notice); "" when no ABILITIES screen is
// open. What the screen draws at kAbilitiesStatusY.
std::string abilities_screen_status_line();

}  // namespace og::ui

// The D3 accessor pair (buttons() fills what count() reads).
button* picker_abilities_buttons();
int picker_abilities_button_count();
