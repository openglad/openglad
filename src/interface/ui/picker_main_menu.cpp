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
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>

#include "picker_sdl_defs.h"

#include <string>

Sint32 create_team_menu(Sint32 arg1);
void picker_lobby_initialize_from_save();
void popup_dialog(const char* title, const char* message);

// Reset game data and go to create_team_menu()
bool picker_prepare_new_game_setup()
{
    screen* game = og::runtime::current_session->myscreen_;
    if (!game)
        return false;

    // §2.1: BEGIN NEW GAME always founds a fresh company — the legacy
    // "There is already a game loaded. Do you want to restart?" prompt is
    // RETIRED. Nothing is destroyed: a new company writes its own file, and
    // the previously active company stays on disk (reopenable via LOAD).

    // §2.2: found the company FIRST — the generated-name screen (REROLL,
    // editable, slug preview). BACK cancels here, before anything is reset or
    // written, so the loaded game survives untouched.
    std::string company_name;
    if (!og::ui::run_new_company_name_entry(company_name))
        return false;

    game->clear();

    // Reset the save data so we have a fresh, new team. This happens BEFORE
    // the intro: a new game always starts on the default campaign, so both
    // the intro about to be shown and the mounted package must not be
    // whatever campaign the previous session or match left selected.
	og::ui::reset_for_new_game(game->save_data);

    // §2.2: the display name lives in the 40-byte save_name; the filename is a
    // derived, collision-probed slug (SaveData::reset does not touch
    // save_name). Repoint the active company to that slug and write the file —
    // creation IS the first autosave. The previous company keeps its own file.
    game->save_data.save_name = company_name;
    const std::string slug = og::data::derive_company_slot(company_name);
    (void)og::data::set_active_company_slot(slug);
    const SaveDataIoError create_error = og::data::company_autosave(
        game->save_data, og::data::CompanyAutosaveKind::BaseCampMutation);
    if (create_error != SaveDataIoError::None)
    {
        // §3.8 "callers surface but don't crash": a failed FIRST write leaves
        // the active slot pointing at a file that does not exist (disk full;
        // browser IndexedDB quota). Tell the user instead of failing
        // silently — the in-memory company still plays, and any later
        // successful autosave creates the file. (Under TESTING popup_dialog
        // is trace-only, so no fault injection is forced on the flows.)
        popup_dialog("NEW COMPANY",
                     og::ui::save_error_string(create_error));
    }

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

	// Clear the playable-family labeling counters.
	og::runtime::current_session->numbought_.fill(0);

    return true;
}

Sint32 beginmenu(Sint32 arg1)
{
    (void)arg1;
    if (!picker_prepare_new_game_setup())
        return MENU_REDRAW;

    return create_team_menu(0);
}
