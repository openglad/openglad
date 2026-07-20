/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// The main menu screen itself is engine-hosted: its spec (the unified
// MP/no-MP variant pair), draw hooks, accessor shims, and mainmenu() live in
// menu_screen_specs.cpp (docs/menu-engine.md). This file keeps the
// new-game entry helpers that BEGIN NEW GAME routes through.

#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/base.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/interface/ui/picker_common.h>

#include "picker_sdl_defs.h"

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Sint32 create_team_menu(Sint32 arg1);
void picker_lobby_initialize_from_save();

// Reset game data and go to create_team_menu()
bool picker_prepare_new_game_setup()
{
    screen* game = og::runtime::current_session->myscreen_;
    if (!game)
        return false;

    // Do we have a team already?  Then prompt to reset.
    if(game->save_data.team_size > 0)
    {
        if(!yes_or_no_prompt("NEW GAME", "There is already a game loaded.\nDo you want to restart?", false))
            return false;
    }

	game->clear();

    // Reset the save data so we have a fresh, new team. This happens BEFORE
    // the intro: a new game always starts on the default campaign, so both
    // the intro about to be shown and the mounted package must not be
    // whatever campaign the previous session or match left selected.
	og::ui::reset_for_new_game(game->save_data);
	(void)og::ui::sync_campaign_mount_to_save(game->save_data);
	og::runtime::current_session->current_guy_ = nullptr;
    picker_lobby_initialize_from_save();

	// Starting new game ..
	release_mouse();
	game->clearbuffer();
	game->swap();
	read_campaign_intro(game);
	game->refresh();
	grab_mouse();
	game->clear();

	// Clear the labeling counter
	for (int i = 0; i < NUM_FAMILIES; i++)
		og::runtime::current_session->numbought_[i] = 0;

    return true;
}

Sint32 beginmenu(Sint32 arg1)
{
    (void)arg1;
    if (!picker_prepare_new_game_setup())
        return MENU_REDRAW;

    return create_team_menu(0);
}
