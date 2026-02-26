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

#include <openglad/gameplay/guy.h>
#include <openglad/interface/input/button.h>
#include <openglad/interface/input/input.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/picker_ui_state.h>
#include <openglad/interface/ui/picker_common.h>
#include "SDL.h"
#include <array>
#include <memory>

#ifndef DISABLE_MULTIPLAYER
constexpr int OPTIONS_BUTTON_INDEX = 10;
constexpr int MAINMENU_BUTTON_COUNT = 11;
#else
constexpr int OPTIONS_BUTTON_INDEX = 5;
constexpr int MAINMENU_BUTTON_COUNT = 6;
#endif

#include "picker_sdl_defs.h"

static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

// difficulty_names removed — use og::ui::kDifficultyNames from picker_common.h

void draw_version_number();
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Sint32 create_team_menu(Sint32 arg1);

Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons, Sint32& retvalue);

void redraw_mainmenu()
{
    screen* game = og::runtime::current_session->myscreen_;
    if (!game)
        return;

    int count = 0;
    
    pks().main_title_logo_pix->set_frame(0);
    pks().main_title_logo_pix->drawMix(15,  8, game->viewob[0].get());
    pks().main_title_logo_pix->set_frame(1);
    pks().main_title_logo_pix->drawMix(151,  8, game->viewob[0].get());
    pks().main_columns_pix->set_frame(0);
    pks().main_columns_pix->drawMix(12,40, game->viewob[0].get());
    pks().main_columns_pix->set_frame(1);
    pks().main_columns_pix->drawMix(242,40, game->viewob[0].get());
    //pks().main_columns_pix->next_frame();
    
    #ifndef DISABLE_MULTIPLAYER
    if (game->save_data.numplayers==4)
    {
        og::runtime::current_session->allbuttons_[2]->do_outline = 1;
        og::runtime::current_session->allbuttons_[3]->do_outline = 0;
        og::runtime::current_session->allbuttons_[4]->do_outline = 0;
        og::runtime::current_session->allbuttons_[5]->do_outline = 0;
    }
    else if (game->save_data.numplayers==3)
    {
        og::runtime::current_session->allbuttons_[2]->do_outline = 0;
        og::runtime::current_session->allbuttons_[3]->do_outline = 1;
        og::runtime::current_session->allbuttons_[4]->do_outline = 0;
        og::runtime::current_session->allbuttons_[5]->do_outline = 0;
    }
    else if (game->save_data.numplayers==2)
    {
        og::runtime::current_session->allbuttons_[2]->do_outline = 0;
        og::runtime::current_session->allbuttons_[3]->do_outline = 0;
        og::runtime::current_session->allbuttons_[4]->do_outline = 1;
        og::runtime::current_session->allbuttons_[5]->do_outline = 0;
    }
    else if (game->save_data.numplayers==0)
    {
        // Spectator mode: no player count button is highlighted
        og::runtime::current_session->allbuttons_[2]->do_outline = 0;
        og::runtime::current_session->allbuttons_[3]->do_outline = 0;
        og::runtime::current_session->allbuttons_[4]->do_outline = 0;
        og::runtime::current_session->allbuttons_[5]->do_outline = 0;
    }
    else
    {
        og::runtime::current_session->allbuttons_[2]->do_outline = 0;
        og::runtime::current_session->allbuttons_[3]->do_outline = 0;
        og::runtime::current_session->allbuttons_[4]->do_outline = 0;
        og::runtime::current_session->allbuttons_[5]->do_outline = 1;
    }
    og::runtime::current_session->allbuttons_[2]->vdisplay();
    og::runtime::current_session->allbuttons_[3]->vdisplay();
    og::runtime::current_session->allbuttons_[4]->vdisplay();
    og::runtime::current_session->allbuttons_[5]->vdisplay();

    og::runtime::current_session->allbuttons_[6]->label = og::ui::format_difficulty_label(og::runtime::current_session->current_difficulty_);

    // Show the allied mode or spectator label
    if (game->save_data.numplayers == 0)
        og::runtime::current_session->allbuttons_[7]->label = "SPECTATOR";
    else
        og::runtime::current_session->allbuttons_[7]->label = og::ui::format_allied_mode_label(game->save_data);
    #else

    og::runtime::current_session->allbuttons_[2]->label = og::ui::format_difficulty_label(og::runtime::current_session->current_difficulty_);
    
    #endif

    count = 0;
    const Sint32 button_count = static_cast<Sint32>(og::runtime::current_session->active_button_count_);
    while (count < button_count)
    {
        og::runtime::current_session->allbuttons_[count]->vdisplay();
        count++;
    }
    og::runtime::current_session->allbuttons_[0]->set_graphic(FAMILY_NORMAL1);
    og::runtime::current_session->allbuttons_[OPTIONS_BUTTON_INDEX]->set_graphic(FAMILY_WRENCH);

    // On native builds, show the version number on the main menu.
    // On Emscripten/web builds, the version is displayed elsewhere (e.g. in the help UI),
    // so we skip drawing it here to avoid layout/clutter issues.
#ifndef __EMSCRIPTEN__
    draw_version_number();
#endif
}

Sint32 mainmenu(Sint32 arg1)
{
	screen* game = og::runtime::current_session->myscreen_;
	if (!game)
		return MENU_EXIT;

	Sint32 retvalue=0;

	if(arg1)
		arg1 = 1;

	// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	button* buttons = get_mainmenu_buttons();
	int num_buttons = MAINMENU_BUTTON_COUNT;
	int highlighted_button = 1;
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
	
	og::runtime::current_session->allbuttons_[0]->set_graphic(FAMILY_NORMAL1);
    og::runtime::current_session->allbuttons_[OPTIONS_BUTTON_INDEX]->set_graphic(FAMILY_WRENCH);

	clear_keyboard();
	reset_timer();
	while (query_timer() < 1);

    // Match other menu transitions: fade out previous menu first, then fade in this one.
    game->fadeblack(0);
    game->clearbuffer();
    draw_buttons(buttons, num_buttons);
    redraw_mainmenu();
    draw_highlight(buttons[highlighted_button]);
    game->fadeblack(1);

	grab_mouse();

	while(!(retvalue & MENU_EXIT))
	{
        if (!og::runtime::current_session->localbuttons_)
            continue;

	    // Input
		{
			Sint32 click = leftmouse(buttons);
			if(click == 1)
				retvalue = og::runtime::current_session->localbuttons_->leftclick();
			else if(click == 2)
				retvalue = og::runtime::current_session->localbuttons_->rightclick(buttons);
		}

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Reset buttons
        if(reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue))
        {
            og::runtime::current_session->allbuttons_[0]->set_graphic(FAMILY_NORMAL1);
            og::runtime::current_session->allbuttons_[OPTIONS_BUTTON_INDEX]->set_graphic(FAMILY_WRENCH);
        }

        // A submenu may have replaced allbuttons — skip draw if exiting
            if(retvalue & MENU_EXIT)
            break;

		// Draw
		game->clearbuffer();
        draw_buttons(buttons, num_buttons);
        redraw_mainmenu();
        draw_highlight(buttons[highlighted_button]);
        game->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
	return retvalue;
}

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

	// Starting new game ..
	release_mouse();
	game->clearbuffer();
	game->swap();
	read_campaign_intro(game);
	game->refresh();
	grab_mouse();
	game->clear();

    // Reset the save data so we have a fresh, new team
	og::ui::reset_for_new_game(game->save_data);
	og::runtime::current_session->current_guy_ = nullptr;
	
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

    return create_team_menu(1);
}
