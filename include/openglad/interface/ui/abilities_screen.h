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

#include <openglad/interface/ui/menu_screen_spec.h>

#include <string>

struct button;

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

// The open screen's status line (og::ui::ability_status_line for the
// selected row, or the read-only notice); "" when no ABILITIES screen is
// open. What the screen draws at kAbilitiesStatusY.
std::string abilities_screen_status_line();

}  // namespace og::ui

// The D3 accessor pair (buttons() fills what count() reads).
button* picker_abilities_buttons();
int picker_abilities_button_count();
