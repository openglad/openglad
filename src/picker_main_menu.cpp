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

#include "graph.h"
#include "button.h"
#include "input.h"
#include "game_context.h"

#include "SDL.h"
#include <format>

#ifndef DISABLE_MULTIPLAYER
constexpr int OPTIONS_BUTTON_INDEX = 10;
constexpr int MAINMENU_BUTTON_COUNT = 11;
#else
constexpr int OPTIONS_BUTTON_INDEX = 5;
constexpr int MAINMENU_BUTTON_COUNT = 6;
#endif

constexpr Sint32 EXIT_VALUE = 1;
constexpr Sint32 REDRAW_VALUE = 2;

extern pixieN *main_title_logo_pix;
extern pixieN *main_columns_pix;
extern char difficulty_names[DIFFICULTY_SETTINGS][80];
extern Sint32 current_difficulty;
extern guy *current_guy;
extern Sint32 numbought[NUM_FAMILIES];
extern vbutton *localbuttons;
extern button mainmenu_buttons[];

void draw_version_number();
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Sint32 create_team_menu(Sint32 arg1);

Sint32 leftmouse(button* buttons);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& localbuttons, button* buttons, int num_buttons, Sint32& retvalue);

void redraw_mainmenu()
{
    screen* game = ctx().game_screen ? ctx().game_screen : myscreen;
    if (!game)
        return;

    int count = 0;
	std::string message;
    
    main_title_logo_pix->set_frame(0);
    main_title_logo_pix->drawMix(15,  8, game->viewob[0].get());
    main_title_logo_pix->set_frame(1);
    main_title_logo_pix->drawMix(151,  8, game->viewob[0].get());
    main_columns_pix->set_frame(0);
    main_columns_pix->drawMix(12,40, game->viewob[0].get());
    main_columns_pix->set_frame(1);
    main_columns_pix->drawMix(242,40, game->viewob[0].get());
    //main_columns_pix->next_frame();
    
    #ifndef DISABLE_MULTIPLAYER
    if (game->save_data.numplayers==4)
    {
        allbuttons[2]->do_outline = 1;
        allbuttons[3]->do_outline = 0;
        allbuttons[4]->do_outline = 0;
        allbuttons[5]->do_outline = 0;
        allbuttons[2]->vdisplay();
        allbuttons[3]->vdisplay();
        allbuttons[4]->vdisplay();
        allbuttons[5]->vdisplay();
    }
    else if (game->save_data.numplayers==3)
    {
        allbuttons[2]->do_outline = 0;
        allbuttons[3]->do_outline = 1;
        allbuttons[4]->do_outline = 0;
        allbuttons[5]->do_outline = 0;
        allbuttons[2]->vdisplay();
        allbuttons[3]->vdisplay();
        allbuttons[4]->vdisplay();
        allbuttons[5]->vdisplay();
    }
    else if (game->save_data.numplayers==2)
    {
        allbuttons[2]->do_outline = 0;
        allbuttons[3]->do_outline = 0;
        allbuttons[4]->do_outline = 1;
        allbuttons[5]->do_outline = 0;
        allbuttons[2]->vdisplay();
        allbuttons[3]->vdisplay();
        allbuttons[4]->vdisplay();
        allbuttons[5]->vdisplay();
    }
    else
    {
        allbuttons[2]->do_outline = 0;
        allbuttons[3]->do_outline = 0;
        allbuttons[4]->do_outline = 0;
        allbuttons[5]->do_outline = 1;
        allbuttons[2]->vdisplay();
        allbuttons[3]->vdisplay();
        allbuttons[4]->vdisplay();
        allbuttons[5]->vdisplay();
    }

    message = std::format("Difficulty: {}", difficulty_names[current_difficulty]);
    allbuttons[6]->label = message;

    // Show the allied mode
    if (game->save_data.allied_mode)
        message = "PVP: Ally";
    else
        message = "PVP: Enemy";
    allbuttons[7]->label = message;
    #else

    message = std::format("Difficulty: {}", difficulty_names[current_difficulty]);
    allbuttons[2]->label = message;
    
    #endif

    count = 0;
    while (allbuttons[count])
    {
        allbuttons[count]->vdisplay();
        count++;
    }
    allbuttons[0]->set_graphic(FAMILY_NORMAL1);
    allbuttons[OPTIONS_BUTTON_INDEX]->set_graphic(FAMILY_WRENCH);

    // On native builds, show the version number on the main menu.
    // On Emscripten/web builds, the version is displayed elsewhere (e.g. in the help UI),
    // so we skip drawing it here to avoid layout/clutter issues.
#ifndef __EMSCRIPTEN__
    draw_version_number();
#endif
}

Sint32 mainmenu(Sint32 arg1)
{
    screen* game = ctx().game_screen ? ctx().game_screen : myscreen;
    if (!game)
        return EXIT_VALUE;

	Sint32 retvalue=0;

	if(arg1)
		arg1 = 1;

	if(localbuttons != nullptr)
		delete localbuttons; //we'll make a new set
    
	button* buttons = mainmenu_buttons;
	int num_buttons = MAINMENU_BUTTON_COUNT;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	
	allbuttons[0]->set_graphic(FAMILY_NORMAL1);
    allbuttons[OPTIONS_BUTTON_INDEX]->set_graphic(FAMILY_WRENCH);
	
	redraw_mainmenu();

	clear_keyboard();
	reset_timer();
	while (query_timer() < 1);
	
	game->fadeblack(1);

	grab_mouse();

	while(!(retvalue & EXIT_VALUE))
	{
	    // Input
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();

        handle_menu_nav(buttons, highlighted_button, retvalue);

        // Reset buttons
        if(reset_buttons(localbuttons, buttons, num_buttons, retvalue))
        {
            allbuttons[0]->set_graphic(FAMILY_NORMAL1);
            allbuttons[OPTIONS_BUTTON_INDEX]->set_graphic(FAMILY_WRENCH);
        }

        // A submenu may have replaced allbuttons — skip draw if exiting
            if(retvalue & EXIT_VALUE)
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
Sint32 beginmenu(Sint32 arg1)
{
    screen* game = ctx().game_screen ? ctx().game_screen : myscreen;
    if (!game)
        return REDRAW_VALUE;

    // Do we have a team already?  Then prompt to reset.
    if(game->save_data.team_size > 0)
    {
        if(!yes_or_no_prompt("NEW GAME", "There is already a game loaded.\nDo you want to restart?", false))
            return REDRAW_VALUE;
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
	game->save_data.reset();
	current_guy = nullptr;
	
	// Clear the labeling counter
	for (int i = 0; i < NUM_FAMILIES; i++)
		numbought[i] = 0;

	return create_team_menu(1);
}
