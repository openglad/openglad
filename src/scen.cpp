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
#include "scen.h"
#include "input.h"
#include "util.h"

/* Changelog
 * 	8/8/02: Zardus: added scrolling-by-minimap
 * 		Zardus: added scrolling-by-keyboard
 */

#include <string>
using namespace std;
#include <stdlib.h>
#define MINIMUM_TIME 0

// From picker // just emulate these so other files are happy
// Difficulty settings .. in percent, so 100 == normal
long current_difficulty = 1; // setting 'normal'
long difficulty_level[DIFFICULTY_SETTINGS] =
    {
        50,
        100,
        200,
    };  // end of difficulty settings

FILE * open_misc_file(char *, char *, char *);

long do_load(screen *ascreen);  // load a scenario or grid
long do_save(screen *ascreen);  // save a scenario or grid
long score_panel(screen *myscreen);
long save_scenario(char * filename, screen * master, char *gridname);
void info_box(walker  *target, screen * myscreen);
void set_facing(walker *target, screen *myscreen);
void set_name(walker  *target, screen * myscreen);
void scenario_options(screen * myscreen);
long quit(long);

screen *myscreen;  // global for scen?

// To appease the linker, we'll fake these from picker.cpp
long statcosts[NUM_FAMILIES][6];
long costlist[NUM_FAMILIES];

long calculate_level(unsigned long howmuch)
{
	return (long) (howmuch/10);
}

long *mymouse;
char *mykeyboard;
//scenario *myscen = new scenario;
long currentmode = OBJECT_MODE;
unsigned long currentlevel = 1;
char scen_name[10] = "test";
char grid_name[10] = "test";

unsigned char  *mypixdata[PIX_MAX+1];
char scenpalette[768];
long backcount=0, forecount = 0;
long myorder = ORDER_LIVING;
char currentteam = 0;
long event = 1;  // need to redraw?
long levelchanged = 0;  // has level changed?
long cyclemode = 0;      // for color cycling
long grid_aligned = 1;  // aligned by grid, default is on
//buffers: PORT: changed start_time to start_time_s to avoid conflict with
//input.cpp
long start_time_s; // for timer ops

smoother  *mysmoother = NULL;

long backgrounds[] = {
                         PIX_GRASS1, PIX_GRASS2, PIX_GRASS_DARK_1, PIX_GRASS_DARK_2,
                         //PIX_GRASS_DARK_B1, PIX_GRASS_DARK_BR, PIX_GRASS_DARK_R1, PIX_GRASS_DARK_R2,
                         PIX_BOULDER_1, PIX_GRASS_DARK_LL, PIX_GRASS_DARK_UR, PIX_GRASS_RUBBLE,

                         PIX_GRASS_LIGHT_LEFT_TOP, PIX_GRASS_LIGHT_1,
                         PIX_GRASS_LIGHT_RIGHT_TOP, PIX_WATER1,

                         PIX_WATERGRASS_U, PIX_WATERGRASS_D,
                         PIX_WATERGRASS_L, PIX_WATERGRASS_R,

                         PIX_DIRTGRASS_UR1, PIX_DIRT_1, PIX_DIRT_1, PIX_DIRTGRASS_LL1,
                         PIX_DIRTGRASS_LR1, PIX_DIRT_DARK_1, PIX_DIRT_DARK_1, PIX_DIRTGRASS_UL1,

                         PIX_DIRTGRASS_DARK_UR1, PIX_DIRTGRASS_DARK_LL1,
                         PIX_DIRTGRASS_DARK_LR1, PIX_DIRTGRASS_DARK_UL1,

                         PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2,
                         PIX_JAGGED_GROUND_3, PIX_JAGGED_GROUND_4,

                         PIX_PATH_1, PIX_PATH_2, PIX_PATH_3, PIX_PATH_4,
                         PIX_COBBLE_1, PIX_COBBLE_2, PIX_COBBLE_3, PIX_COBBLE_4,

                         //PIX_WALL2, PIX_WALL3, PIX_WALL4, PIX_WALL5,

                         PIX_WALL4, PIX_WALL_ARROW_GRASS,
                         PIX_WALL_ARROW_FLOOR, PIX_WALL_ARROW_GRASS_DARK,

                         PIX_WALL2, PIX_WALL3, PIX_H_WALL1, PIX_WALL_LL,

                         PIX_WALLSIDE_L, PIX_WALLSIDE_C, PIX_WALLSIDE_R, PIX_WALLSIDE1,

                         PIX_WALLSIDE_CRACK_C1, PIX_WALLSIDE_CRACK_C1,
                         PIX_TORCH1, PIX_VOID1,

                         //PIX_VOID1, PIX_FLOOR1, PIX_VOID1, PIX_VOID1,

                         PIX_CARPET_SMALL_TINY, PIX_CARPET_M2, PIX_PAVEMENT1, PIX_FLOOR1,

                         //PIX_PAVEMENT1, PIX_PAVEMENT2, PIX_PAVEMENT3, PIX_PAVEMENT3,
                         PIX_FLOOR_PAVEL, PIX_FLOOR_PAVEU, PIX_FLOOR_PAVED, PIX_FLOOR_PAVED,

                         PIX_WALL_LL,
                         PIX_WALLTOP_H,
                         PIX_PAVESTEPS1,
                         PIX_BRAZIER1,

                         PIX_PAVESTEPS2L, PIX_PAVESTEPS2, PIX_PAVESTEPS2R, PIX_PAVESTEPS1,
                         //PIX_TORCH1, PIX_TORCH2, PIX_TORCH3, PIX_TORCH3,

                         PIX_COLUMN1, PIX_COLUMN2, PIX_COLUMN2, PIX_COLUMN2,

                         PIX_TREE_T1, PIX_TREE_T1, PIX_TREE_T1, PIX_TREE_T1,
                         PIX_TREE_ML, PIX_TREE_M1, PIX_TREE_MT, PIX_TREE_MR,
                         PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1, PIX_TREE_B1,

                         PIX_CLIFF_BACK_L, PIX_CLIFF_BACK_1, PIX_CLIFF_BACK_2, PIX_CLIFF_BACK_R,
                         PIX_CLIFF_LEFT, PIX_CLIFF_BOTTOM, PIX_CLIFF_TOP, PIX_CLIFF_RIGHT,
                         PIX_CLIFF_LEFT, PIX_CLIFF_TOP_L, PIX_CLIFF_TOP_R, PIX_CLIFF_RIGHT,
                     };

long rowsdown = 0;
long maxrows = ((sizeof(backgrounds)/4) / 4);
text *scentext;

int main(int argc, char **argv)
{
	cfg.commandline(argc, argv);
	long i,j;
	long extra;
	//  unsigned char input;
	//  char soundpath[80];
	long windowx, windowy;
	walker  *newob;
	long mx, my;
	char mystring[80]; //, someletter;
	//  long pos;
	short count;
	// char buffer[80];

	//Zardus: add: init the input
	init_input();

	// Zardus: create dirs
	create_dataopenglad();

	string homecfg(get_file_path("openglad.cfg"));
	cfg.parse(homecfg.c_str());

	// For informational purposes..
	if (argc > 1 && !strcmp(argv[1], "/?") )
	{
		printf("\nScenario Editor version %s\n", GLAD_VER);

		// Free memory ..
		meminfo Memory;
		//buffers: PORT: union REGS regs;
		//buffers: PORT: struct SREGS sregs;
		long bytes;

		//buffers: PORT: regs.x.eax = 0x00000500;
		//buffers: PORT: memset( &sregs, 0, sizeof(sregs) );
		//buffers: PORT: sregs.es = FP_SEG( &Memory );
		//buffers: regs.x.edi = FP_OFF( &Memory );

		//buffers: int386x( DPMI_INT, &regs, &regs, &sregs );
		bytes = Memory.FreeLinAddrSpace * 4096;
		printf("\nMemory available: %ld bytes.\n", bytes);

		exit (0);
	}

	myscreen = new screen(1);

	scentext = new text(myscreen);
	// Set the un-set text to empty ..
	for (i=0; i < 60; i ++)
		myscreen->scentext[i][0] = 0;

	// Install our masking timer interrupt
	grab_timer();

	// Now install the keyboard interrupt
	grab_keyboard();
	// Clear the keyboard state
	clear_keyboard();

	// Now do the mouse ..
	grab_mouse();

	// Get our pixie data ..
	load_map_data(mypixdata);
	load_and_set_palette("our.pal", scenpalette);

	// Set our default par value ..
	myscreen->par_value = 1;
	load_scenario("test", myscreen);

	myscreen->clearfontbuffer();
	myscreen->redraw();
	myscreen->refresh();

	//******************************
	// Keyboard loop
	//******************************

	grab_mouse();
	mykeyboard = query_keyboard();

	//
	// This is the main program loop
	//
	while(1)
	{
		// Reset the timer count to zero ...
		reset_timer();

		if (myscreen->end)
			break;

		//buffers: get keys and stuff
		get_input_events(POLL);

		// Zardus: COMMENT: I went through and replaced dumbcounts with get_input_events.

		// Delete all with ^D
		if (mykeyboard[SDLK_d] && mykeyboard[SDLK_LCTRL])
		{
			remove_all_objects(myscreen);
			event = 1;
		}

		// Change teams ..
		if (mykeyboard[SDLK_0])
		{
			currentteam = 0;
			event = 1;
		}
		if (mykeyboard[SDLK_1])
		{
			currentteam = 1;
			event = 1;
		}
		if (mykeyboard[SDLK_2])
		{
			currentteam = 2;
			event = 1;
		}
		if (mykeyboard[SDLK_3])
		{
			currentteam = 3;
			event = 1;
		}
		if (mykeyboard[SDLK_4])
		{
			currentteam = 4;
			event = 1;
		}
		if (mykeyboard[SDLK_5])
		{
			currentteam = 5;
			event = 1;
		}
		if (mykeyboard[SDLK_6])
		{
			currentteam = 6;
			event = 1;
		}
		if (mykeyboard[SDLK_7])
		{
			currentteam = 7;
			event = 1;
		}

		// Toggle grid alignment
		if (mykeyboard[SDLK_g])
		{
			grid_aligned = (grid_aligned+1)%3;
			event = 1;
			while (mykeyboard[SDLK_g])
				//buffers: dumbcount++;
				get_input_events(WAIT);
		}

		// Show help
		if (mykeyboard[SDLK_h])
		{
			release_mouse();
			do_help(myscreen);
			myscreen->clearfontbuffer();
			grab_mouse();
			event = 1;
		}

		if (mykeyboard[SDLK_KP_MULTIPLY]) // options menu
		{
			release_mouse();
			scenario_options(myscreen);
			grab_mouse();
			event = 1; // redraw screen
		}

		// Load scenario, etc. ..
		if (mykeyboard[SDLK_l])
		{
			if (levelchanged)
			{
				myscreen->draw_button(30, 15, 220, 25, 1, 1);
				scentext->write_xy(32, 17, "Save level first? [Y/N]", DARK_BLUE, 1);
				myscreen->buffer_to_screen(0, 0, 320, 200);
				while ( !mykeyboard[SDLK_y] && !mykeyboard[SDLK_n])
					get_input_events(WAIT);
				if (mykeyboard[SDLK_y]) // save first
					do_save(myscreen);
			}
			myscreen->draw_button(30, 15, 220, 25, 1, 1);
			scentext->write_xy(32, 17, "Loading Level...", DARK_BLUE, 1);
			do_load(myscreen);
			myscreen->clearfontbuffer();
		}

		// Save scenario or grid..
		if (mykeyboard[SDLK_s])
		{
			do_save(myscreen);
		}  // end of saving routines


		// Switch modes ..
		if (mykeyboard[SDLK_m])        // switch to map or guys ..
		{
			event = 1;
			currentmode = (currentmode+1) %2;
			while (mykeyboard[SDLK_m])
				get_input_events(WAIT);
		}

		// New names
		if (mykeyboard[SDLK_n])
		{
			event = 1;
			//gotoxy(1, 23);
			myscreen->draw_button(50, 30, 200, 40, 1, 1);
			scentext->write_xy(52, 32, "New name [G/S] : ", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			while ( !mykeyboard[SDLK_g] && !mykeyboard[SDLK_s] )
				get_input_events(WAIT);
			if (mykeyboard[SDLK_s])
			{
				myscreen->draw_button(50, 30, 200, 40, 1, 1);
				myscreen->buffer_to_screen(0, 0, 320, 200);
				new_scenario_name();
				while (mykeyboard[SDLK_s])
					get_input_events(WAIT);
			} // end new scenario name
			else if (mykeyboard[SDLK_g])
			{
				myscreen->draw_button(50, 30, 200, 40, 1, 1);
				myscreen->buffer_to_screen(0, 0, 320, 200);
				new_grid_name();
				while (mykeyboard[SDLK_g])
					get_input_events(WAIT);
			} // end new grid name
			myscreen->clearfontbuffer(50,30,150,10);
		}

		// Enter scenario text ..
		if (mykeyboard[SDLK_t])
		{
#define TEXT_DOWN(x)  (14+((x)*7))
   #define TL 4
			//gotoxy(1, 1);
			myscreen->draw_button(0, 10, 200, 200, 2, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			scentext->write_xy(TL, TEXT_DOWN(0), "Enter new scenario text;", DARK_BLUE, 1);
			scentext->write_xy(TL, TEXT_DOWN(1), " PERIOD (.) alone to end.", DARK_BLUE, 1);
			scentext->write_xy(TL, TEXT_DOWN(2), "*--------*---------*---------*", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			myscreen->scentextlines = 0;
			count = 2;
			for (i=0; i < 23; i++)
				if (strlen(myscreen->scentext[i]))
					scentext->write_xy(TL, TEXT_DOWN(i+3), myscreen->scentext[i], DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			extra = 1;
			for (i=0; i < 60; i++)
			{
				count++;
				mystring[0] = 0;
				if (! (count%26) )
				{
					myscreen->draw_box(TL, TEXT_DOWN(3), 196, 196, 27, 1, 1);
					myscreen->buffer_to_screen(0, 0, 320, 200);
					for (j=0; j < 23; j++)
					{
						count = j+(23*extra);
						if (count < 60)
							if (strlen(myscreen->scentext[count]))
								scentext->write_xy(TL, TEXT_DOWN(j+3), myscreen->scentext[count], DARK_BLUE, 1);
					}
					count = 3;
					extra++;
					myscreen->buffer_to_screen(0, 0, 320, 200);
				}
				strcpy(mystring, scentext->input_string(TL, TEXT_DOWN(count), 30, myscreen->scentext[i]) );
				strcpy(myscreen->scentext[i], mystring);
				if (!strcmp(".", mystring)) // says end ..
				{
					i = 70;
					myscreen->draw_box(0, 10, 200, 200, 0, 1, 1);
					myscreen->buffer_to_screen(0, 0, 320, 200);
					myscreen->scentext[i][0] = 0;
					event = 1;
				}
				else
					myscreen->scentextlines++;
			}
			myscreen->draw_box(0, 10, 200, 200, 0, 1, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			event = 1;
		}

		// Display the scenario help..
		if (mykeyboard[SDLK_SLASH] && (mykeyboard[SDLK_RSHIFT] || mykeyboard[SDLK_LSHIFT]))
		{
			read_scenario(myscreen);
			myscreen->clearfontbuffer();
			event = 1;
		}

		// Change level of current guy being placed ..
		if (mykeyboard[SDLK_RIGHTBRACKET])
		{
			currentlevel++;
			//while (mykeyboard[SDLK_RIGHTBRACKET])
			//  dumbcount++;
			event = 1;
		}
		if (mykeyboard[SDLK_LEFTBRACKET] && currentlevel > 1)
		{
			currentlevel--;
			//while (mykeyboard[SDLK_LEFTBRACKET])
			//  dumbcount++;
			event = 1;
		}

		// Change between generator and living orders
		if (mykeyboard[SDLK_o])        // this is letter o
		{
			if (myorder == ORDER_LIVING)
			{
				myorder = ORDER_GENERATOR;
				forecount = FAMILY_TENT;
			}
			else if (myorder == ORDER_GENERATOR)
				myorder = ORDER_SPECIAL;   // for placing team guys ..
			else if (myorder == ORDER_SPECIAL)
			{
				myorder = ORDER_TREASURE;
				forecount = FAMILY_DRUMSTICK;
			}
			else if (myorder == ORDER_TREASURE)
				myorder = ORDER_WEAPON;
			else if (myorder == ORDER_WEAPON)
				myorder = ORDER_LIVING;
			currentmode = OBJECT_MODE;
			event = 1; // change score panel
			while (mykeyboard[SDLK_o])
				get_input_events(WAIT);
		}

		// Slide tile selector down ..
		if (mykeyboard[SDLK_DOWN])
		{
			rowsdown++;
			event = 1;
			if (rowsdown >= maxrows)
				rowsdown -= maxrows;
			score_panel(myscreen);
			while (mykeyboard[SDLK_DOWN])
				get_input_events(WAIT);
		}

		// Slide tile selector up ..
		if (mykeyboard[SDLK_UP])
		{
			rowsdown--;
			event = 1;
			if (rowsdown < 0)
				rowsdown += maxrows;
			if (rowsdown <0 || rowsdown >= maxrows) // bad case
				rowsdown = 0;
			score_panel(myscreen);
			while (mykeyboard[SDLK_UP])
				get_input_events(WAIT);
		}

		// Smooth current map, F5
		if (mykeyboard[SDLK_F5])
		{
			if (mysmoother)
				delete mysmoother;
			mysmoother = new smoother();
			mysmoother->set_target(myscreen);
			mysmoother->smooth();
			while (mykeyboard[SDLK_F5])
				get_input_events(WAIT);
			event = 1;
			levelchanged = 1;
		}

		// Change to new palette ..
		if (mykeyboard[SDLK_F9])
		{
			load_and_set_palette("our.pal", scenpalette);
			while (mykeyboard[SDLK_F9])
				get_input_events(WAIT);
		}

		// Toggle color cycling
		if (mykeyboard[SDLK_F10])
		{
			cyclemode++;
			cyclemode %= 2;
			while (mykeyboard[SDLK_F10])
				get_input_events(WAIT);
		}
		// Now perform color cycling if selected
		if (cyclemode)
		{
			cycle_palette(scenpalette, WATER_START, WATER_END, 1);
			cycle_palette(scenpalette, ORANGE_START, ORANGE_END, 1);
		}

		// Mouse stuff ..
		mymouse = query_mouse();

		// Scroll the screen ..
		// Zardus: ADD: added scrolling by keyboard
		// Zardus: PORT: disabled mouse scrolling
		if ((mykeyboard[SDLK_KP8] || mykeyboard[SDLK_KP7] || mykeyboard[SDLK_KP9]) // || mymouse[MOUSE_Y]< 2)
		        && myscreen->topy >= 0) // top of the screen
			set_screen_pos(myscreen, myscreen->topx,
			               myscreen->topy-SCROLLSIZE);
		if ((mykeyboard[SDLK_KP2] || mykeyboard[SDLK_KP1] || mykeyboard[SDLK_KP3]) // || mymouse[MOUSE_Y]> 198)
		        && myscreen->topy <= (GRID_SIZE*myscreen->maxy)-18) // scroll down
			set_screen_pos(myscreen, myscreen->topx,
			               myscreen->topy+SCROLLSIZE);
		if ((mykeyboard[SDLK_KP4] || mykeyboard[SDLK_KP7] || mykeyboard[SDLK_KP1]) // || mymouse[MOUSE_X]< 2)
		        && myscreen->topx >= 0) // scroll left
			set_screen_pos(myscreen, myscreen->topx-SCROLLSIZE,
			               myscreen->topy);
		if ((mykeyboard[SDLK_KP6] || mykeyboard[SDLK_KP3] || mykeyboard[SDLK_KP9]) // || mymouse[MOUSE_X] > 318)
		        && myscreen->topx <= (GRID_SIZE*myscreen->maxx)-18) // scroll right
			set_screen_pos(myscreen, myscreen->topx+SCROLLSIZE,
			               myscreen->topy);

		if (mymouse[MOUSE_LEFT])       // put or remove the current guy
		{
			event = 1;
			mx = mymouse[MOUSE_X];
			my = mymouse[MOUSE_Y];

			// Zardus: ADD: can move map by clicking on minimap
			if (mx > myscreen->viewob[0]->endx - myscreen->viewob[0]->myradar->xview - 4
			        && my > myscreen->viewob[0]->endy - myscreen->viewob[0]->myradar->yview - 4
			        && mx < myscreen->viewob[0]->endx - 4 && my < myscreen->viewob[0]->endy - 4)
			{
				mx -= myscreen->viewob[0]->endx - myscreen->viewob[0]->myradar->xview - 4;
				my -= myscreen->viewob[0]->endy - myscreen->viewob[0]->myradar->yview - 4;

				// Zardus: above set_screen_pos doesn't take into account that minimap scrolls too. This one does.
				set_screen_pos (myscreen, myscreen->viewob[0]->myradar->radarx * GRID_SIZE + mx * GRID_SIZE - 160,
				                myscreen->viewob[0]->myradar->radary * GRID_SIZE + my * GRID_SIZE - 100);
			}
			else if ( (mx >= S_LEFT) && (mx <= S_RIGHT) &&
			          (my >= S_UP) && (my <= S_DOWN) )      // in the main window
			{
				windowx = mymouse[MOUSE_X] + myscreen->topx - myscreen->viewob[0]->xloc; // - S_LEFT
				if (grid_aligned==1)
					windowx -= (windowx%GRID_SIZE);
				windowy = mymouse[MOUSE_Y] + myscreen->topy - myscreen->viewob[0]->yloc; // - S_UP
				if (grid_aligned==1)
					windowy -= (windowy%GRID_SIZE);
				if (mykeyboard[SDLK_i]) // get info on current object
				{
					newob = myscreen->add_ob(ORDER_LIVING, FAMILY_ELF);
					newob->setxy(windowx, windowy);
					if (some_hit(windowx, windowy, newob, myscreen))
						info_box(newob->collide_ob,myscreen);
					myscreen->remove_ob(newob,0);
					continue;
				}  // end of info mode
				if (mykeyboard[SDLK_f]) // set facing of current object
				{
					newob = myscreen->add_ob(ORDER_LIVING, FAMILY_ELF);
					newob->setxy(windowx, windowy);
					if (some_hit(windowx, windowy, newob, myscreen))
						set_facing(newob->collide_ob,myscreen);
					myscreen->remove_ob(newob,0);
					continue;
				}  // end of set facing

				if (mykeyboard[SDLK_r]) // (re)name the current object
				{
					newob = myscreen->add_ob(ORDER_LIVING, FAMILY_ELF);
					newob->setxy(windowx, windowy);
					if (some_hit(windowx, windowy, newob, myscreen))
					{
						set_name(newob->collide_ob,myscreen);
						levelchanged = 1;
					}
					myscreen->remove_ob(newob,0);
				}  // end of info mode
				else if (currentmode == OBJECT_MODE)
				{
					newob = myscreen->add_ob(myorder, forecount);
					newob->setxy(windowx, windowy);
					newob->team_num = currentteam;
					newob->stats->level = currentlevel;
					newob->dead = 0; // just in case
					newob->collide_ob = 0;
					if ( (grid_aligned==1) && some_hit(windowx, windowy, newob, myscreen))
					{
						if (mykeyboard[SDLK_LCTRL] &&    // are we holding the erase?
						        newob->collide_ob )                    // and hit a guy?
						{
							myscreen->remove_ob(newob->collide_ob,0);
							while (mymouse[MOUSE_LEFT])
							{
								mymouse = query_mouse();
							}
							levelchanged = 1;
						} // end of deleting guy
						if (newob)
						{
							myscreen->remove_ob(newob,0);
							newob = NULL;
						}
					}  // end of failure to put guy
					else if (grid_aligned == 2)
					{
						newob->draw(myscreen->viewob[0]);
						myscreen->buffer_to_screen(0, 0, 320, 200);
						start_time_s = query_timer();
						while ( mymouse[MOUSE_LEFT] && (query_timer()-start_time_s) < 36 )
						{
							mymouse = query_mouse();
						}
						levelchanged = 1;
					}
					if (mykeyboard[SDLK_LCTRL] && newob)
					{
						myscreen->remove_ob(newob,0);
						newob = NULL;
					}
					//       while (mymouse[MOUSE_LEFT])
					//         mymouse = query_mouse();
				}  // end of putting a guy
				if (currentmode == MAP_MODE)
				{
					windowx /= GRID_SIZE;  // get the map position ..
					windowy /= GRID_SIZE;
					// Set to our current selection
					myscreen->grid[windowy*(myscreen->maxx)+windowx] = some_pix(backcount);
					levelchanged = 1;
					if (!mykeyboard[SDLK_LCTRL]) // smooth a few squares, if not control
					{
						if (mysmoother)
						{
							delete mysmoother;
							mysmoother = new smoother();
							mysmoother->set_target(myscreen);
						}
						for (i=windowx-1; i <= windowx+1; i++)
							for (j=windowy-1; j <=windowy+1; j++)
								if (i >= 0 && i < myscreen->maxx &&
								        j >= 0 && j < myscreen->maxy)
									mysmoother->smooth(i, j);
					}
					else if (mysmoother) // update smoother anyway
					{
						delete mysmoother;
						mysmoother = new smoother();
						mysmoother->set_target(myscreen);
					}
					myscreen->viewob[0]->myradar->update();
				}  // end of setting grid square
			} // end of main window
			//    if ( (mx >= PIX_LEFT) && (mx <= PIX_RIGHT) &&
			//        (my >= PIX_TOP) && (my <= PIX_BOTTOM) ) // grid menu
			if (mx >= S_RIGHT && my >= PIX_TOP && my <= PIX_BOTTOM)
			{
				//windowx = (mx - PIX_LEFT) / GRID_SIZE;
				windowx = (mx-S_RIGHT) / GRID_SIZE;
				windowy = (my - PIX_TOP) / GRID_SIZE;
				backcount = backgrounds[ (windowx + ((windowy+rowsdown) * PIX_OVER))
				                         % (sizeof(backgrounds)/4)];
				backcount %= NUM_BACKGROUNDS;
				currentmode = MAP_MODE;
			} // end of background grid window

		}      // end of left mouse button

		if (mymouse[MOUSE_RIGHT])      // cycle through things ...
		{
			event = 1;
			if (currentmode == OBJECT_MODE)
			{
				if (myorder == ORDER_LIVING)
					forecount = (forecount+1) % NUM_FAMILIES;
				else if (myorder == ORDER_TREASURE)
					forecount = (forecount+1) % (MAX_TREASURE+1);
				else if (myorder == ORDER_GENERATOR)
					forecount = (forecount+1) % 4;
				else if (myorder == ORDER_WEAPON)
					forecount = (forecount+1) % (FAMILY_DOOR+1); // use largest weapon
				else
					forecount = 0;
			} // end of if object mode
			if (currentmode == MAP_MODE)
			{
				windowx = mymouse[MOUSE_X] + myscreen->topx - myscreen->viewob[0]->xloc; // - S_LEFT
				windowx -= (windowx%GRID_SIZE);
				windowy = mymouse[MOUSE_Y] + myscreen->topy - myscreen->viewob[0]->yloc; // - S_UP
				windowy -= (windowy%GRID_SIZE);
				windowx /= GRID_SIZE;
				windowy /= GRID_SIZE;
				backcount = myscreen->grid[windowy*(myscreen->maxx)+windowx];
			}
			while (mymouse[MOUSE_RIGHT])
			{
				mymouse = query_mouse();
			}
		}

		if (event)
		{
			release_mouse();
			myscreen->redraw();
			score_panel(myscreen);
			myscreen->refresh();
			//    score_panel(myscreen);
			grab_mouse();
		}
		event = 0;

		if (mykeyboard[SDLK_ESCAPE])
			quit(1);

	}

	return 0;
}

long quit(long num)
{
	int i;

	for (i = 0; i < PIX_MAX+1; i++)
		if (mypixdata[i]) delete mypixdata[i];

	// Release the mouse ..
	release_mouse();

	// Release the keyboard interrupt
	release_keyboard();

	// And release the timer ..
	release_timer();

	// And stop input
	stop_input();

	// And delete myscreen
	delete myscreen;

	// Delete scentext
	delete scentext;

	SDL_Quit();

	exit(num);
	return num;
}

long score_panel(screen *myscreen)
{
	char message[50];
	long i, j; // for loops
	//   static long family=-1, hitpoints=-1, score=-1, act=-1;
	static long numobs = myscreen->numobs;
	static long lm = 245;
	long curline = 0;
	long whichback;
	static char treasures[20][NUM_FAMILIES] =
	    { "BLOOD", "DRUMSTICK", "GOLD", "SILVER",
	      "MAGIC", "INVIS", "INVULN", "FLIGHT",
	      "EXIT", "TELEPORTER", "LIFE GEM", "KEY", "SPEED", "CC",
	    };
	static char weapons[20][NUM_FAMILIES] =
	    { "KNIFE", "ROCK", "ARROW", "FIREBALL",
	      "TREE", "METEOR", "SPRINKLE", "BONE",
	      "BLOOD", "BLOB", "FIRE ARROW", "LIGHTNING",
	      "GLOW", "WAVE 1", "WAVE 2", "WAVE 3",
	      "PROTECTION", "HAMMER", "DOOR",
	    };

	static char livings[NUM_FAMILIES][20] =
	    {  "SOLDIER", "ELF", "ARCHER", "MAGE",
	       "SKELETON", "CLERIC", "ELEMENTAL",
	       "FAERIE", "L SLIME", "S SLIME", "M SLIME",
	       "THIEF", "GHOST", "DRUID", "ORC",
	       "ORC CAPTAIN", "BARBARIAN", "ARCHMAGE",
	       "GOLEM", "G SKELETON", "TOWER1",
	    };

	// Hide the mouse ..
	release_mouse();

	// Draw the bounding box
	//myscreen->draw_dialog(lm-4, L_D(-1), 310, L_D(8), "Info");
	myscreen->draw_button(lm-4, L_D(-1)+4, 315, L_D(7)-2, 1, 1);

	// Show scenario and grid info
	strcpy(message, scen_name);
	uppercase(message);

	//myscreen->fastbox(lm, S_UP, 70, 8*5, 27, 1);
	scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);

	strcpy(message, grid_name);
	uppercase(message);
	scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);

	if (currentmode==MAP_MODE)
		scentext->write_xy(lm,L_D(curline++), "MODE: MAP", DARK_BLUE, 1);
	else if (currentmode==OBJECT_MODE)
		scentext->write_xy(lm,L_D(curline++), "MODE: OBS", DARK_BLUE, 1);

	// Get team number ..
	sprintf(message, "%d:", currentteam);
	if (myorder == ORDER_LIVING)
		strcat(message, livings[forecount]);
	else if (myorder == ORDER_GENERATOR)
		switch (forecount)      // who are we?
		{
			case FAMILY_TENT:
				strcat(message, "TENT");
				break;
			case FAMILY_TOWER:
				strcat(message, "TOWER");
				break;
			case FAMILY_BONES:
				strcat(message, "BONEPILE");
				break;
			case FAMILY_TREEHOUSE:
				strcat(message, "TREEHOUSE");
				break;
			default:
				strcat(message, "GENERATOR");
				break;
		}
	else if (myorder == ORDER_SPECIAL)
		strcat(message, "PLAYER");
	else if (myorder == ORDER_TREASURE)
		strcat(message, treasures[forecount]);
	else if (myorder == ORDER_WEAPON)
		strcat(message, weapons[forecount]);
	else
		strcat(message, "UNKNOWN");
	scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);

	// Level display
	sprintf(message, "LVL: %d", currentlevel);
	//myscreen->fastbox(lm,L_D(curline),55,7,27, 1);
	scentext->write_xy(lm, L_D(curline++), message, DARK_BLUE, 1);

	// Is grid alignment on?
	//myscreen->fastbox(lm, L_D(curline),65, 7, 27, 1);
	if (grid_aligned==1)
		scentext->write_xy(lm, L_D(curline++), "ALIGN: ON", DARK_BLUE, 1);
	else if (grid_aligned==2)
		scentext->write_xy(lm, L_D(curline++), "ALIGN: STACK", DARK_BLUE, 1);
	else
		scentext->write_xy(lm, L_D(curline++), "ALIGN: OFF", DARK_BLUE, 1);

	numobs = myscreen->numobs;
	//myscreen->fastbox(lm,L_D(curline),55,7,27, 1);
	sprintf(message, "OB: %d", numobs);
	scentext->write_xy(lm,L_D(curline++),message, DARK_BLUE, 1);

	// Show the background grid ..
	myscreen->putbuffer(lm+40, PIX_TOP-16, GRID_SIZE, GRID_SIZE,
	                    0, 0, 320, 200, mypixdata[backcount]+3);

	//   rowsdown = (NUM_BACKGROUNDS / 4) + 1;
	//   rowsdown = 0; // hack for now
	for (i=0; i < PIX_OVER; i++)
	{
		for (j=0; j < 4; j++)
		{
			//myscreen->back[i]->draw( S_RIGHT+(i*8), S_UP+100);
			//myscreen->back[0]->draw(64, 64);
			whichback = (i+(j+rowsdown)*4) % (sizeof(backgrounds)/4);
			myscreen->putbuffer(S_RIGHT+i*GRID_SIZE, PIX_TOP+j*GRID_SIZE,
			                    GRID_SIZE, GRID_SIZE,
			                    0, 0, 320, 200,
			                    mypixdata[ backgrounds[whichback] ]+3);
		}
	}
	myscreen->draw_box(S_RIGHT, PIX_TOP,
	                   S_RIGHT+4*GRID_SIZE, PIX_TOP+4*GRID_SIZE, 0, 0, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	// Restore the mouse
	grab_mouse();

	return 1;
}


void set_screen_pos(screen *myscreen, long x, long y)
{
	myscreen->topx = x;
	myscreen->topy = y;
	event = 1;
}

void remove_all_objects(screen *master)
{
	oblink *fx = master->fxlist;

	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = master->oblist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = master->weaplist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	master->numobs = 0;
} // end remove_all_objects

void remove_first_ob(screen *master)
{
	oblink  *here;

	here = master->oblist;

	while (here)
	{
		if (here->ob)
		{
			delete here->ob;
			return;
		}
		else
			here = here->next;
	}
}

long save_map_file(char  * filename, screen *master)
{
	// File data in form:
	// <# of frames>      1 byte
	// <x size>                   1 byte
	// <y size>                   1 byte
	// <pixie data>               <x*y*frames> bytes

	char numframes, x, y;
	//  char  *newpic;
	string fullpath(filename);
	FILE  *outfile;

	// Create the full pathname for the pixie file
	fullpath += ".pix";

	lowercase (fullpath);

	if ( (outfile = open_misc_file((char *)fullpath.c_str(), "pix/", "wb")) == NULL )
	{
		master->draw_button(30, 30, 220, 60, 1, 1);
		scentext->write_xy(32, 32, "Error in saving map file", DARK_BLUE, 1);
		scentext->write_xy(32, 42, fullpath.c_str(), DARK_BLUE, 1);
		scentext->write_xy(32, 52, "Press SPACE to continue", DARK_BLUE, 1);
		master->buffer_to_screen(0, 0, 320, 200);
		while (!mykeyboard[SDLK_SPACE])
			get_input_events(WAIT);
		return 0;
	}

	x = master->maxx;
	y = master->maxy;
	numframes = 1;
	fwrite(&numframes, 1, 1, outfile);
	fwrite(&x, 1, 1, outfile);
	fwrite(&y, 1, 1, outfile);

	fwrite(master->grid, 1, (x*y), outfile);

	fclose(outfile);        // Close the data file
	return 1;

} // End of map-saving routine

long load_new_grid(screen *master)
{
	string tempstring;
	//char tempstring[80];

	scentext->write_xy(52, 32, "Grid name: ", DARK_BLUE, 1);
	tempstring = scentext->input_string(115, 32, 8, grid_name);
	//NORMAL_KEYBOARD(SDLKf("%s", tempstring);)
	if (tempstring.empty())
	{
		//buffers: our grid files are all lowercase...
		lowercase(tempstring);

		tempstring += grid_name;
	}

	//printf("DB: loading %s\n", grid_name);

	//buffers: PORT: changed .PIX to .pix
	tempstring += ".pix";
	master->grid = read_pixie_file(tempstring.c_str());
	master->maxx = master->grid[1];
	master->maxy = master->grid[2];
	master->grid = master->grid + 3;
	//printf("DB: loaded data to grid\n");
	//master->viewob[0]->myradar = new radar(master->viewob[0],
	//  master, 0);

	master->viewob[0]->myradar->start();
	master->viewob[0]->myradar->update();

	//printf("DB: made new radar\n");
	return 1;
}

long new_scenario_name()
{
	char tempstring[80];

	scentext->write_xy(52, 32, "Scenario name: ", DARK_BLUE, 1);
	strcpy(tempstring, scentext->input_string(135, 32, 8, scen_name));
	//NORMAL_KEYBOARD(SDLKf("%s", tempstring);)
	if (strlen(tempstring))
	{
		strcpy(scen_name, tempstring);
		//buffers: all our files are lowercase....
		lowercase(scen_name);
	}

	return 1;
}

long new_grid_name()
{
	char tempstring[80];

	scentext->write_xy(52, 32, "Grid name: ", DARK_BLUE, 1);
	strcpy(tempstring, scentext->input_string(117, 32, 8, grid_name));
	//NORMAL_KEYBOARD(SDLKf("%s", tempstring);)
	if (strlen(tempstring))
		strcpy(grid_name, tempstring);

	return 1;
}

void do_help(screen * myscreen)
{
	text *helptext = new text(myscreen);
	long lm = S_LEFT+4+32, tm=S_UP+4;  // left and top margins
	long lines = 0;

	myscreen->draw_button(S_LEFT+32,S_UP,S_RIGHT-1+16,S_DOWN-1,2, 1);

	helptext->write_xy(lm + L_W(10), tm, "**HELP**", DARK_BLUE, 1);

	helptext->write_xy(lm, tm+L_H(++lines), "G : TOGGLE GRID ALIGNMENT", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "H : HELP", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "I : INFO ON CLICKED OBJECT", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "L : LOAD NEW SCEN OR GRID", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "M : TOGGLE OBJECT OR MAP MODE", DARK_BLUE, 1);
	helptext->write_xy(lm, tm+L_H(++lines), "N : NEW SCEN OR GRID NAME", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "O : TOGGLE LIVING/TENT/ETC", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "R : RENAME OBJECT", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "S : SAVE SCEN OR GRID", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "T : ENTER SCEN TEXT", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "* : TOGGLE SCENARIO OPTIONS", DARK_BLUE, 1);

	//lines +=1;
	helptext->write_xy(lm, tm+L_H(++lines), "ESC         : QUIT", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "LEFT CLICK  : PUT OB OR BACKGD", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "CTRL + LEFT : Remove Object", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "CTRL + D    : Remove all Obs", DARK_BLUE,1);

	helptext->write_xy(lm, tm+L_H(++lines), "RIGHT CLICK : CYCLE THRU OBS", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "KEYS 0-7    : CYCLE TEAM NUMBER", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "F5          : SMOOTH MAP TILES", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "[,]         : LOWER/RAISE OB LEVEL", DARK_BLUE,1);
	helptext->write_xy(lm, tm+L_H(++lines), "?           : DISPLAY SCEN TEXT", DARK_BLUE,1);

	myscreen->buffer_to_screen(0, 0, 320, 200);

	wait_for_key(SDLK_SPACE);

	delete helptext;
}

char some_pix(long whatback)
{
	long i;

	i = random(4);  // max # of types of any particular ..

	switch (whatback)
	{
		case PIX_GRASS1:
			switch (i)
			{
				case 0:
					return PIX_GRASS1;
				case 1:
					return PIX_GRASS2;
				case 2:
					return PIX_GRASS3;
				case 3:
					return PIX_GRASS4;
				default:
					return PIX_GRASS1;
			}
			//break;
		case PIX_GRASS_DARK_1:
			switch (i)
			{
				case 0:
					return PIX_GRASS_DARK_1;
				case 1:
					return PIX_GRASS_DARK_2;
				case 2:
					return PIX_GRASS_DARK_3;
				case 3:
					return PIX_GRASS_DARK_4;
				default:
					return PIX_GRASS_DARK_1;
			}
			//break;
		case PIX_GRASS_DARK_B1:
		case PIX_GRASS_DARK_B2:
			switch (i)
			{
				case 0:
				case 1:
					return PIX_GRASS_DARK_B1;
				case 2:
				case 3:
				default:
					return PIX_GRASS_DARK_B2;
			}
			//break;
		case PIX_GRASS_DARK_R1:
		case PIX_GRASS_DARK_R2:
			switch (i)
			{
				case 0:
				case 1:
					return PIX_GRASS_DARK_R1;
				case 2:
				case 3:
				default:
					return PIX_GRASS_DARK_R2;
			}
			//break;
		case PIX_WATER1:
			switch (i)
			{
				case 0:
					return PIX_WATER1;
				case 1:
					return PIX_WATER2;
				case 2:
					return PIX_WATER3;
				default:
					return PIX_WATER1;
			}
			//break;
		case PIX_PAVEMENT1:
			switch (random(12))
			{
				case 0:
					return PIX_PAVEMENT1;
				case 1:
					return PIX_PAVEMENT2;
				case 2:
					return PIX_PAVEMENT3;
				default:
					return PIX_PAVEMENT1;
			}
			//break;
		case PIX_COBBLE_1:
			switch (random(i))
			{
				case 0:
					return PIX_COBBLE_1;
				case 1:
					return PIX_COBBLE_2;
				case 2:
					return PIX_COBBLE_3;
				case 3:
					return PIX_COBBLE_4;
				default:
					return PIX_COBBLE_1;
			}
			//break;
		case PIX_BOULDER_1:
			switch (random(i))
			{
				case 0:
					return PIX_BOULDER_1;
				case 1:
					return PIX_BOULDER_2;
				case 2:
					return PIX_BOULDER_3;
				case 3:
					return PIX_BOULDER_4;
				default:
					return PIX_BOULDER_1;
			}
			//break;
		case PIX_JAGGED_GROUND_1:
			switch (i)
			{
				case 0:
					return PIX_JAGGED_GROUND_1;
				case 1:
					return PIX_JAGGED_GROUND_2;
				case 2:
					return PIX_JAGGED_GROUND_3;
				case 3:
					return PIX_JAGGED_GROUND_4;
				default:
					return PIX_JAGGED_GROUND_1;
			}
		default:
			return whatback;
	}
}

long save_scenario(char * filename, screen * master, char *gridname)
{
	long currentx, currenty;
	char temporder, tempfamily;
	char tempteam, tempfacing, tempcommand;
	char templevel;
	short shortlevel;
	char filler[20] = "MSTRMSTRMSTRMSTR"; // for RESERVED
	FILE  *outfile;
	char temptext[10] = "FSS";
	char temp_grid[20] = "grid";  // default grid
	char temp_scen_type = master->scenario_type;
	oblink  * head = master->oblist;
	long listsize;
	long i;
	char temp_version = VERSION_NUM;
	char temp_filename[80];
	char numlines, tempwidth;
	char oneline[80];
	char tempname[12];
	char buffer[200];
	char scentitle[30];
	short temp_par;

	// Format of a scenario object list file is: (ver. 8)
	// 3-byte header: 'FSS'
	// 1-byte version number (from graph.h)
	// 8-byte grid file name
	// 30-byte scenario title
	// 1-byte scenario_type
	// 2-bytes par-value for level
	// 2-bytes (long) = total objects to follow
	// List of n objects, each of 20-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte long xpos
	// 2-byte long ypos
	// 1-byte TEAM
	// 1-byte current facing
	// 1-byte current command
	// 1-byte level // this is 2 bytes in version 7+
	// 12-bytes name
	// ---
	// 10 bytes RESERVED
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line

	// Zardus: PORT: no longer need to put in scen/ in this part
	//strcpy(temp_filename, scen_directory);
	strcpy(temp_filename, filename);
	//buffers: PORT: changed .FSS to .fss
	strcat(temp_filename, ".fss");

	if ( (outfile = open_misc_file(temp_filename, "scen/", "wb")) == NULL ) // open for write
	{
		//gotoxy(1, 22);
		printf("Error in writing file %s\n", filename);

		master->draw_button(30, 30, 220, 60, 1, 1);
		sprintf(buffer, "Error in saving scenario file");
		scentext->write_xy(32, 32, buffer, DARK_BLUE, 1);
		sprintf(buffer, "%s", temp_filename);
		scentext->write_xy(32, 42, buffer, DARK_BLUE, 1);
		sprintf(buffer, "Press SPACE to continue");
		scentext->write_xy(32, 52, buffer, DARK_BLUE, 1);
		master->buffer_to_screen(0, 0, 320, 200);
		while (!mykeyboard[SDLK_SPACE])
			get_input_events(WAIT);

		return 0;
	}

	// Write id header
	fwrite(temptext, 3, 1, outfile);

	// Write version number
	fwrite(&temp_version, 1, 1, outfile);

	// Write name of current grid...
	strcpy(temp_grid, gridname);  // Do NOT include extension

	// Set any chars under 8 not used to 0 ..
	for (i=strlen(temp_grid); i < 8; i++)
		temp_grid[i] = 0;
	fwrite(temp_grid, 8, 1, outfile);

	// Write the scenario title, if it exists
	for (i=0; i < strlen(scentitle); i++)
		scentitle[i] = 0;
	strcpy(scentitle, master->scenario_title);
	fwrite(scentitle, 30, 1, outfile);

	// Write the scenario type info
	fwrite(&temp_scen_type, 1, 1, outfile);

	// Write our par value (version 8+)
	temp_par = master->par_value;
	fwrite(&temp_par, 2, 1, outfile);

	// Determine size of object list ...
	listsize = 0;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of oblist-size check

	// Also check the fx list ..
	head = master->fxlist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of fxlist-size check

	// And the weapon list ..
	head = master->weaplist;
	while (head)
	{
		if (head->ob)
			listsize++;
		head = head->next;
	} // end of weaplist-size check

	fwrite(&listsize, 2, 1, outfile);

	// Okay, we've written header .. now dump the data ..
	head = master->oblist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
				return 0;  // Something wrong! Too few objects..
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			//templevel = head->ob->stats->level;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
			fwrite(&temporder, 1, 1, outfile);
			fwrite(&tempfamily, 1, 1, outfile);
			fwrite(&currentx, 2, 1, outfile);
			fwrite(&currenty, 2, 1, outfile);
			fwrite(&tempteam, 1, 1, outfile);
			fwrite(&tempfacing, 1, 1, outfile);
			fwrite(&tempcommand, 1, 1, outfile);
			fwrite(&shortlevel, 2, 1, outfile);
			fwrite(tempname, 12, 1, outfile);
			fwrite(filler, 10, 1, outfile);
		}
		// Advance to next object ..
		head = head->next;
	}

	// Now dump the fxlist data ..
	head = master->fxlist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
				return 0;  // Something wrong! Too few objects..
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			//templevel = head->ob->stats->level;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
			fwrite(&temporder, 1, 1, outfile);
			fwrite(&tempfamily, 1, 1, outfile);
			fwrite(&currentx, 2, 1, outfile);
			fwrite(&currenty, 2, 1, outfile);
			fwrite(&tempteam, 1, 1, outfile);
			fwrite(&tempfacing, 1, 1, outfile);
			fwrite(&tempcommand, 1, 1, outfile);
			fwrite(&shortlevel, 2, 1, outfile);
			fwrite(tempname, 12, 1, outfile);
			fwrite(filler, 10, 1, outfile);
		}
		// Advance to next object ..
		head = head->next;
	}

	// Now dump the weaplist data ..
	head = master->weaplist;  // back to head of list
	while (head)
	{
		if (head->ob)
		{
			if (!head)
				return 0;  // Something wrong! Too few objects..
			temporder = head->ob->query_order();
			tempfacing= head->ob->curdir;
			tempfamily= head->ob->query_family();
			tempteam  = head->ob->team_num;
			tempcommand=head->ob->query_act_type();
			currentx  = head->ob->xpos;
			currenty  = head->ob->ypos;
			shortlevel = head->ob->stats->level;
			strcpy(tempname, head->ob->stats->name);
			fwrite(&temporder, 1, 1, outfile);
			fwrite(&tempfamily, 1, 1, outfile);
			fwrite(&currentx, 2, 1, outfile);
			fwrite(&currenty, 2, 1, outfile);
			fwrite(&tempteam, 1, 1, outfile);
			fwrite(&tempfacing, 1, 1, outfile);
			fwrite(&tempcommand, 1, 1, outfile);
			fwrite(&shortlevel, 2, 1, outfile);
			fwrite(tempname, 12, 1, outfile);
			fwrite(filler, 10, 1, outfile);
		}
		// Advance to next object ..
		head = head->next;
	}

	numlines = master->scentextlines;
	//printf("saving %d lines\n", numlines);

	fwrite(&numlines, 1, 1, outfile);
	for (i=0; i < numlines; i++)
	{
		strcpy(oneline, master->scentext[i]);
		tempwidth = strlen(oneline);
		fwrite(&tempwidth, 1, 1, outfile);
		fwrite(oneline, tempwidth, 1, outfile);
	}

	fclose(outfile);

	return 1;
}

// Copy of collide from obmap; used manually .. :(
long check_collide(long x,  long y,  long xsize,  long ysize,
                   long x2, long y2, long xsize2, long ysize2)
{
	if (x < x2)
	{
		if (y < y2)
		{
			if (x2 - x < xsize &&
			        y2 - y < ysize)
				return 1;
		}
		else // y >= y2
		{
			if (x2 - x < xsize &&
			        y - y2 < ysize2)
				return 1;
		}
	}
	else // x >= x2
	{
		if (y < y2)
		{
			if (x - x2 < xsize2 &&
			        y2 - y < ysize)
				return 1;
		}
		else // y >= y2
		{
			if (x - x2 < xsize2 &&
			        y - y2 < ysize2)
				return 1;
		}
	}
	return 0;
}

// The old-fashioned hit check ..
walker * some_hit(long x, long y, walker  *ob, screen *screenp)
{
	oblink  *here;

	here = screenp->oblist;

	while (here)
	{
		if (here->ob && here->ob != ob)
			if (check_collide(x, y, ob->sizex, ob->sizey,
			                  here->ob->xpos, here->ob->ypos,
			                  here->ob->sizex, here->ob->sizey) )
			{
				ob->collide_ob = here->ob;
				return here->ob;
			}
		here = here->next;
	}

	// Also check the fx list ..
	here = screenp->fxlist;
	while (here)
	{
		if (here->ob && here->ob != ob)
			if (check_collide(x, y, ob->sizex, ob->sizey,
			                  here->ob->xpos, here->ob->ypos,
			                  here->ob->sizex, here->ob->sizey) )
			{
				ob->collide_ob = here->ob;
				return here->ob;
			}
		here = here->next;
	}

	// Also check the weapons list ..
	here = screenp->weaplist;
	while (here)
	{
		if (here->ob && !here->ob->dead && here->ob != ob)
			if (check_collide(x, y, ob->sizex, ob->sizey,
			                  here->ob->xpos, here->ob->ypos,
			                  here->ob->sizex, here->ob->sizey) )
			{
				ob->collide_ob = here->ob;
				return here->ob;
			}
		here = here->next;
	}

	ob->collide_ob = NULL;
	return NULL;
}

// Display info about the target object ..
#define INFO_DOWN(x) (25+7*x)
void info_box(walker  *target,screen * myscreen)
{
	text *infotext = new text(myscreen);
	long linesdown = 0;
	long lm = 25+32;
	char message[80];
	treasure  *teleporter, *temp;

	static char *orders[] =
	    { "LIVING", "WEAPON", "TREASURE", "GENERATOR", "FX", "SPECIAL", };
	static char *livings[] =
	    { "SOLDIER", "ELF", "ARCHER", "MAGE",
	      "SKELETON", "CLERIC", "ELEMENTAL",
	      "FAERIE", "L-SLIME", "S-SLIME",
	      "M-SLIME", "THIEF", "GHOST",
	      "DRUID",
	    };
	static char *treasures[] =
	    { "BLOODSTAIN", "DRUMSTICK: FOOD",
	      "GOLD BAR", "SILVER BAR",
	      "MAGIC POTION", "INVISIBILITY POTION",
	      "INVULNERABILITY POTION",
	      "FLIGHT POTION", "EXIT", "TELEPORTER",
	      "LIFE GEM", "KEY", "SPEED", "CC",
	    };

	release_mouse();
	myscreen->draw_button(20+32, 20, 220+32, 170, 1, 1);

	infotext->write_xy(lm, INFO_DOWN(linesdown++), "INFO TEXT", DARK_BLUE,1);
	linesdown++;

	if (strlen(target->stats->name)) // it has a name
	{
		sprintf(message, "Name    : %s", target->stats->name);
		infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);
	}

	sprintf(message, "Order   : %s", orders[target->query_order()] );
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	if (target->query_order() == ORDER_LIVING)
		sprintf(message, "Family  : %s",
		        livings[target->query_family()] );
	else if (target->query_order() == ORDER_TREASURE)
		sprintf(message, "Family  : %s",
		        treasures[target->query_family()] );
	else
		sprintf(message, "Family  : %d", target->query_family());
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	sprintf(message, "Team Num: %d", target->team_num);
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE, 1);

	sprintf(message, "Position: %dx%d (%dx%d)", target->xpos, target->ypos,
	        (target->xpos/GRID_SIZE), (target->ypos/GRID_SIZE) );
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	if (target->query_order() == ORDER_TREASURE &&
	        target->query_family()== FAMILY_EXIT)
		sprintf(message, "Exits to: Level %d", target->stats->level);
	else if (target->query_order() == ORDER_TREASURE &&
	         target->query_family() == FAMILY_TELEPORTER)
	{
		sprintf(message, "Group # : %d", target->stats->level);
		infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);
		temp = (treasure  *) target;
		teleporter = (treasure  *) temp->find_teleport_target();
		if (!teleporter || teleporter == target)
			infotext->write_xy(lm, INFO_DOWN(linesdown++), "Goes to : Itself!", DARK_BLUE,1);
		else
		{
			sprintf(message, "Goes to : %dx%d (%dx%d)", teleporter->xpos,
			        teleporter->ypos, teleporter->xpos/GRID_SIZE, teleporter->ypos/GRID_SIZE);
		}
	}
	else
		sprintf(message, "Level   : %d", target->stats->level);
	infotext->write_xy(lm, INFO_DOWN(linesdown++), message, DARK_BLUE,1);

	linesdown++;
	infotext->write_xy(lm, INFO_DOWN(linesdown++),
	                   "PRESS ESC TO EXIT", DARK_BLUE,1);

	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	// Wait for press and release of ESC
	while (!mykeyboard[SDLK_ESCAPE])
		get_input_events(WAIT);
	while (mykeyboard[SDLK_ESCAPE])
		get_input_events(WAIT);
}

// Set the stats->name value of a walker ..
void set_name(walker  *target, screen * master)
{
	char newname[10];
	char oldname[20];
	char buffer[200];

	//gotoxy(1,20);
	master->draw_button(30, 30, 220, 70, 1, 1);
	sprintf(buffer, "Renaming object");
	scentext->write_xy(32, 32, buffer, DARK_BLUE, 1);
	sprintf(buffer, "Enter '.' to not change.");
	scentext->write_xy(32, 42, buffer, DARK_BLUE, 1);

	if (strlen(target->stats->name))
	{
		sprintf(buffer, "Current name: %s", target->stats->name);
		strcpy(oldname, target->stats->name);
	}
	else
	{
		sprintf(buffer, "Current name: NOT SET");
		strcpy(oldname, "NOT SET");
	}
	scentext->write_xy(32, 52, buffer, DARK_BLUE, 1);
	scentext->write_xy(32, 62, "    New name:", DARK_BLUE, 1);

	master->buffer_to_screen(0, 0, 320, 200);

	// wait for key release
	while (mykeyboard[SDLK_r])
		get_input_events(WAIT);

	strcpy(newname, scentext->input_string(115, 62, 9, oldname) );
	newname[10] = 0;

	if (strcmp(newname, ".")) // didn't type '.'
		strcpy(target->stats->name, newname);

	info_box(target,master);

}

void scenario_options(screen *myscreen)
{
	static text opt_text(myscreen);
	char *opt_keys = query_keyboard();
	short lm, tm;
	char message[80];

	lm = 55;
	tm = 45;

#define OPT_LD(x) (short) (tm + (x*8) )
while (!opt_keys[SDLK_ESCAPE])
        {


	myscreen->draw_button(lm-5, tm-5, 260, 160, 2, 1);

	opt_text.write_xy(lm, OPT_LD(0), "SCENARIO OPTIONS", DARK_BLUE, 1);

	if (myscreen->scenario_type & SCEN_TYPE_CAN_EXIT)
		opt_text.write_xy(lm, OPT_LD(2), "Can Always Exit (E)         : Yes", DARK_BLUE, 1);
	else
		opt_text.write_xy(lm, OPT_LD(2), "Can Always Exit (E)         : No ", DARK_BLUE, 1);

	if (myscreen->scenario_type & SCEN_TYPE_GEN_EXIT)
		opt_text.write_xy(lm, OPT_LD(3), " Kill Generators to Exit (G): Yes", DARK_BLUE, 1);
	else
		opt_text.write_xy(lm, OPT_LD(3), " Kill Generators to Exit (G): No ", DARK_BLUE, 1);

	if (myscreen->scenario_type & SCEN_TYPE_SAVE_ALL)
		opt_text.write_xy(lm, OPT_LD(4), " Must Save Named NPC's (N)  : Yes", DARK_BLUE, 1);
	else
		opt_text.write_xy(lm, OPT_LD(4), " Must Save Named NPC's (N)  : No ", DARK_BLUE, 1);

	sprintf(message, " Level Par Value (+,-)      : %d ", myscreen->par_value);
	opt_text.write_xy(lm, OPT_LD(5), message, DARK_BLUE, 1);


	myscreen->buffer_to_screen(0, 0, 320, 200);

	get_input_events(WAIT);
	if (opt_keys[SDLK_e]) // toggle exit mode
	{
		if (myscreen->scenario_type & SCEN_TYPE_CAN_EXIT) // already set
			myscreen->scenario_type -= SCEN_TYPE_CAN_EXIT;
		else
			myscreen->scenario_type += SCEN_TYPE_CAN_EXIT;
	}
	if (opt_keys[SDLK_g]) // toggle exit mode -- generators
	{
		if (myscreen->scenario_type & SCEN_TYPE_GEN_EXIT) // already set
			myscreen->scenario_type -= SCEN_TYPE_GEN_EXIT;
		else
			myscreen->scenario_type += SCEN_TYPE_GEN_EXIT;
	}
	if (opt_keys[SDLK_n]) // toggle fail mode -- named guys
	{
		if (myscreen->scenario_type & SCEN_TYPE_SAVE_ALL) // already set
			myscreen->scenario_type -= SCEN_TYPE_SAVE_ALL;
		else
			myscreen->scenario_type += SCEN_TYPE_SAVE_ALL;
	}
	if (opt_keys[SDLK_KP_MINUS]) // lower the par value
	{
		if (myscreen->par_value > 1)
			myscreen->par_value--;
	}
	if (opt_keys[SDLK_KP_PLUS]) // raise the par value
	{
		myscreen->par_value++;
	}
}

while (opt_keys[SDLK_ESCAPE])
	get_input_events(WAIT); // wait for key release

	myscreen->clearfontbuffer(lm-5, tm-5, 260-(lm-5), 160-(tm-5));
}

// Set an object's facing ..
void set_facing(walker *target, screen *myscreen)
{
	char *setkeys = query_keyboard();

	if (target)
		target = target;  // dummy code

	myscreen->draw_dialog(100, 50, 220, 170, "Set Facing");
	myscreen->buffer_to_screen(0, 0, 320, 200);

	while (setkeys[SDLK_f])
		get_input_events(WAIT);

}


// Load a grid or scenario ..
long do_load(screen *ascreen)
{
	long i;
	text *loadtext = new text(ascreen);
	char buffer[200],temp[200];

	event = 1;
	ascreen->draw_button(50, 30, 200, 40, 1, 1);
	loadtext->write_xy(52, 32, "Load [G/S] : ", DARK_BLUE, 1);
	ascreen->buffer_to_screen(0, 0, 320, 200);
	while ( !mykeyboard[SDLK_g] && !mykeyboard[SDLK_s] )
		get_input_events(WAIT);
	if (mykeyboard[SDLK_s])
	{
		ascreen->draw_button(50, 30, 200, 40, 1, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		new_scenario_name();
		ascreen->clearfontbuffer(50, 30, 150, 10);
		loadtext->write_xy(52, 32, "Loading scenario..", DARK_BLUE, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		remove_all_objects(ascreen);  // kill   current obs
		for (i=0; i < 60; i ++)
			ascreen->scentext[i][0] = 0;
		load_scenario(scen_name, ascreen);
		ascreen->viewob[0]->myradar->start();
		ascreen->viewob[0]->myradar->update();
		strcpy(grid_name, query_my_map_name());
		while (mykeyboard[SDLK_s])
			//buffers: dumbcount++;
			get_input_events(WAIT);
		//buffers: PORT: stricmp isn't compiling... need to find replacement func
		//buffers: workaround: copy scenario_title to new buffer and make it all
		//buffers: lowercase and then compare it to lowercase 'none'
		strcpy(temp,ascreen->scenario_title);
		lowercase(temp);
		if (strlen(ascreen->scenario_title) &&
		        strcmp(temp, "none") )
		{
			ascreen->draw_button(10, 30, 238, 51, 1, 1);
			ascreen->clearfontbuffer(10, 30, 228, 21);
			sprintf(buffer, "Loaded: %s", ascreen->scenario_title);
			loadtext->write_xy(12, 33, buffer, DARK_BLUE, 1);
			loadtext->write_xy(12, 43, "Press space to continue", RED, 1);
			ascreen->buffer_to_screen(0, 0, 320, 200);
			while (!mykeyboard[SDLK_SPACE])
				get_input_events(WAIT);
		}
	} // end load scenario
	else if (mykeyboard[SDLK_g])
	{
		ascreen->draw_button(50, 30, 200, 40, 1, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		load_new_grid(ascreen);
		while (mykeyboard[SDLK_g])
			//dumbcount++;
			get_input_events(WAIT);
	} // end load new grid

	delete loadtext;
	levelchanged = 0;
	return 1;
}

long do_save(screen *ascreen)  // save a scenario or grid
{
	text *savetext = new text(ascreen);
	long result = 1;

	event = 1;
	while (mykeyboard[SDLK_s])
		get_input_events(WAIT);
	ascreen->draw_button(50, 30, 200, 40, 1, 1);
	savetext->write_xy(52, 32, "Save [G/S] ", DARK_BLUE, 1);
	ascreen->buffer_to_screen(0, 0, 320, 200);
	while ( !mykeyboard[SDLK_s] && !mykeyboard[SDLK_g] )
		get_input_events(WAIT);
	if (mykeyboard[SDLK_s]) // save scenario
	{
		while (mykeyboard[SDLK_s])
			get_input_events(WAIT);

		// Allow us to set the title, if desired
		ascreen->draw_button(20, 30, 235, 41, 1, 1);
		savetext->write_xy(22, 33, "Title:", DARK_BLUE, 1);
		ascreen->buffer_to_screen(0, 0, 320, 200);
		strcpy(ascreen->scenario_title,
		       savetext->input_string(58, 33, 29, ascreen->scenario_title) );

		ascreen->clearfontbuffer(20, 30, 215, 15);
		savetext->write_xy(52, 33, "Saving scenario..");
		ascreen->buffer_to_screen(0, 0, 320, 200);
		// Save the map file ..
		if (!save_map_file(grid_name, ascreen) )
			//printf("\nError saving map.\n");
			result = 0;
		else
			save_scenario(scen_name, ascreen, grid_name);

		ascreen->clearfontbuffer();
		clear_keyboard();
	} // end of save scenario
	else if (mykeyboard[SDLK_g]) // save current grid
	{
		ascreen->clearfontbuffer(50, 30, 150, 10);
		savetext->write_xy(52, 32, "Saving grid..");
		ascreen->buffer_to_screen(0, 0, 320, 200);
		if (!save_map_file(grid_name, ascreen) )
			//printf("\nError saving map!\n");
			result = 0;

		ascreen->clearfontbuffer();
		clear_keyboard();
	} // end of save grid

	delete savetext;

	if (result)
		levelchanged = 0;
	return result;
}
