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
//screen.cpp

/* ChangeLog
	buffers: 7/31/02: *deleted some redundant headers.
			: *load_scenario now looks for all uppercase files in
			:  levels.001 if lowercase file fails
	buffers: 8/15/02: *load_scenario now checks for uppercase file names in
			   scen/ in case lowercase check fails
*/

#include "graph.h"
#include "smooth.h"
#include "util.h"
#include "input.h"
#include "view_sizes.h"
#include <string>

using namespace std;

// From picker.cpp
extern Sint32 calculate_level(Uint32 temp_exp);

// Screen window boundries
#define MAX_VIEWS 5
#define S_UP 0 //12 //0
#define S_LEFT 0 //12 //0
#define S_DOWN 200 //188 // 200
#define S_RIGHT 320 // 228
#define S_WIDTH (S_RIGHT - S_LEFT)
#define S_HEIGHT (S_DOWN - S_UP)
//#define BUF_SIZE (unsigned) ((S_DOWN-S_UP)*(S_RIGHT-S_LEFT))

#define MAX_SPREAD 10 //this controls find_near_foe
#define TIME_BONUS (Sint32) 5000
#define LEVEL_BONUS (Sint32) 120
//#define query_keyboard dumb
//#define grab_keyboard yuck

short load_version_2(SDL_RWops  *infile, screen * master);
short load_version_3(SDL_RWops  *infile, screen * master); // v.3 scen
short load_version_4(SDL_RWops  *infile, screen * master); // v.4 scen: + names
short load_version_5(SDL_RWops  *infile, screen * master); // v.5 scen: + type
short load_version_6(SDL_RWops  *infile, screen * master, short version=6); // v.6 scen: + title



Uint32 random(Uint32 x)
{
	if (x < 1)
		return 0;
	return (Uint32) ( ((Uint32) rand()) % x);
}

// ************************************************************
//  SCREEN -- graphics routines
//
//  This object is the video graphics object.  All display
//  must pass through this object, and all on-screen objects
//  are found in this object.
// ************************************************************


screen::screen(short howmany)
    : video(), level_data(1)
{
	Sint32 i, j;
	const char *qresult;
	text first_text(this);
	Sint32 left = 66;

	grab_timer();

	timerstart = query_timer_control();
	framecount = 0;
	
	weapfree = NULL;

	//  control = NULL;
	//myradar[0] = myradar[1] = NULL; // very important! :)
	control_hp = 0;

	// Load the palette ..
	load_and_set_palette("our.pal", newpalette);

	// load the pixie graphics data shorto memory
	// shorto memory, you know the !tallo kind

	draw_button(60, 50, 260, 110, 2, 1);
	draw_text_bar(64, 54, 256, 62); // header field
	first_text.write_y(56, "Loading Gladiator..Please Wait", RED, 1);
	draw_text_bar(64, 64, 256, 106); // draw box for text

	first_text.write_xy(left, 70, "Loading Graphics...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);
    // FIXME: Loader used to be created here...  but now it's in level_data.
	first_text.write_xy(left, 70, "Loading Graphics...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 78, "Loading Gameplay Info...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);
	palmode = 0;

	end = 0;
	timer_wait = 6;       // 'moderate' speed setting

	redrawme = 1;
	cyclemode = 1; //color cycling on by default
	
	enemy_freeze = 0;

	level_done = 0;

	// Load map data from a pixie format
	// FIXME: This was moved into level_data
	//load_map_data(pixdata);
	first_text.write_xy(left, 78, "Loading Gameplay Info...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 86, "Initializing Display...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);


	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens
	for (i=0; i < MAX_VIEWS; i++)
		viewob[i] = NULL;
    
    initialize_views();

	first_text.write_xy(left, 86, "Initializing Display...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 94, "Initializing Sound...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);
	

	// Init the sound data
	qresult = cfg.query("sound", "sound");
	if (!qresult || (qresult && !strcmp(qresult, "on"))) // sound is on
	{
		soundp = new soundob();
		first_text.write_xy(left, 94, "Initializing Sound...Done", DARK_BLUE, 1);
	}
	else
	{
		soundp = new soundob(1); // turn sound off
		first_text.write_xy(left, 94, "Initializing Sound...Skipped", DARK_BLUE, 1);
	}
	buffer_to_screen(0, 0, 320, 200);

	// Let's set the special names for all walkers ..
	for (i=0; i < NUM_FAMILIES; i++)
		for (j=0; j < NUM_SPECIALS; j++)
		{
			strcpy(special_name[i][j], "NONE");
			strcpy(alternate_name[i][j], "NONE");
		}

	strcpy(special_name[FAMILY_SOLDIER][1], "CHARGE");
	strcpy(special_name[FAMILY_SOLDIER][2], "BOOMERANG");
	strcpy(special_name[FAMILY_SOLDIER][3], "WHIRLWIND");
	strcpy(special_name[FAMILY_SOLDIER][4], "DISARM");

	strcpy(special_name[FAMILY_BARBARIAN][1], "HURL BOULDER");
	strcpy(special_name[FAMILY_BARBARIAN][2], "EXPLODING BOULDER");

	strcpy(special_name[FAMILY_ELF][1], "ROCKS");
	strcpy(special_name[FAMILY_ELF][2], "BOUNCING ROCKS");
	strcpy(special_name[FAMILY_ELF][3], "LOTS OF ROCKS");
	strcpy(special_name[FAMILY_ELF][4], "MEGA ROCKS");

	strcpy(special_name[FAMILY_ARCHER][1], "FIRE ARROWS");
	strcpy(special_name[FAMILY_ARCHER][2], "BARRAGE");
	strcpy(special_name[FAMILY_ARCHER][3], "EXPLODING BOLT");

	strcpy(special_name[FAMILY_MAGE][1], "TELEPORT");
	strcpy(alternate_name[FAMILY_MAGE][1], "TELEPORT MARKER");
	strcpy(special_name[FAMILY_MAGE][2], "WARP SPACE");
	strcpy(special_name[FAMILY_MAGE][3], "FREEZE TIME");
	strcpy(special_name[FAMILY_MAGE][4], "ENERGY WAVE");
	strcpy(special_name[FAMILY_MAGE][5], "HEARTBURST");

	strcpy(special_name[FAMILY_ARCHMAGE][1], "TELEPORT");
	strcpy(alternate_name[FAMILY_ARCHMAGE][1], "TELEPORT MARKER");
	strcpy(special_name[FAMILY_ARCHMAGE][2], "HEARTBURST");
	strcpy(alternate_name[FAMILY_ARCHMAGE][2], "CHAIN LIGHTNING");
	strcpy(special_name[FAMILY_ARCHMAGE][3], "SUMMON IMAGE");
	strcpy(alternate_name[FAMILY_ARCHMAGE][3], "SUMMON ELEMENTAL");
	strcpy(special_name[FAMILY_ARCHMAGE][4], "MIND CONTROL");
	//strcpy(alternate_name[FAMILY_ARCHMAGE][4], "SUMMON ELEMENTAL");


	strcpy(special_name[FAMILY_CLERIC][1], "HEAL");
	strcpy(alternate_name[FAMILY_CLERIC][1], "MYSTIC MACE");
	strcpy(special_name[FAMILY_CLERIC][2], "RAISE UNDEAD");
	strcpy(alternate_name[FAMILY_CLERIC][2], "TURN UNDEAD");
	strcpy(special_name[FAMILY_CLERIC][3], "RAISE GHOST");
	strcpy(alternate_name[FAMILY_CLERIC][3], "TURN UNDEAD");
	strcpy(special_name[FAMILY_CLERIC][4], "RESURRECT");

	strcpy(special_name[FAMILY_DRUID][1], "GROW TREE");
	strcpy(special_name[FAMILY_DRUID][2], "SUMMON FAERIE");
	strcpy(special_name[FAMILY_DRUID][3], "REVEAL");
	strcpy(special_name[FAMILY_DRUID][4], "PROTECTION");

	strcpy(special_name[FAMILY_THIEF][1], "DROP BOMB");
	strcpy(special_name[FAMILY_THIEF][2], "CLOAK");
	strcpy(special_name[FAMILY_THIEF][3], "TAUNT ENEMY");
	strcpy(alternate_name[FAMILY_THIEF][3], "CHARM OPPONENT");
	strcpy(special_name[FAMILY_THIEF][4], "POISON CLOUD");

	strcpy(special_name[FAMILY_GHOST][1], "SCARE");

	strcpy(special_name[FAMILY_FIREELEMENTAL][1], "STARBURST");

	strcpy(special_name[FAMILY_ORC][1], "HOWL");
	strcpy(special_name[FAMILY_ORC][2], "EAT CORPSE");

	strcpy(special_name[FAMILY_SMALL_SLIME][1], "GROW");

	strcpy(special_name[FAMILY_MEDIUM_SLIME][1], "GROW");

	strcpy(special_name[FAMILY_SLIME][1], "SPLIT");

	strcpy(special_name[FAMILY_SKELETON][1], "TUNNEL");

}

screen::~screen()
{
	release_timer();
	delete soundp;

	soundp = NULL;
	cleanup(1); //make sure we've cleaned up
}

void screen::initialize_views()
{
    // Even though it looks okay here, these positions and sizes are overridden by viewscreen::resize() later.
	if (numviews == 1)
	{
		viewob[0] = new viewscreen( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0, this);
	}
	else if (numviews == 2)
	{
		viewob[0] = new viewscreen( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT, 0, this);
		viewob[1] = new viewscreen( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT, 1, this);
	}
	else if (numviews == 3)
	{
		viewob[0] = new viewscreen( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0, this);
		viewob[1] = new viewscreen( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1, this);
		viewob[2] = new viewscreen( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2, this);
	}
	else if (numviews == 4)
	{
		viewob[0] = new viewscreen( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0, this);
		viewob[1] = new viewscreen( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1, this);
		viewob[2] = new viewscreen( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2, this);
		viewob[3] = new viewscreen( T_LEFT_FOUR, T_UP_FOUR, T_HALF_WIDTH, T_HALF_HEIGHT, 3, this);
	}
	else
    {
        Log("Error initializing screen views.  numviews is %d\n", numviews);
    }
}

void screen::cleanup(short howmany)
{
	Sint32 i;
	walker *who;

    numviews = howmany; // # of viewscreens
    for (i=0; i < MAX_VIEWS; i++)
    {
            delete (viewob[i]);
            viewob[i] = NULL;
    }

    while (weapfree)
    {
            who = weapfree;
            weapfree = weapfree->cachenext; //cachenext points to next ob
            delete who;
            who = NULL;
    }
    weapfree = NULL;
}

void screen::ready_for_battle(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);
    
    initialize_views();

	end = 0;

	redrawme = 1;

	timerstart = query_timer_control();
	framecount = 0;
	enemy_freeze = 0;

	control_hp = 0;

	palmode = 0;

	end = 0;

	redrawme = 1;

}

void screen::reset(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);

	if (numviews == 1)
	{
		viewob[0] = new viewscreen( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0, this);
	}
	else if (numviews == 2)
	{
		viewob[1] = new viewscreen( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1, this);
		viewob[0] = new viewscreen( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0, this);
	}
	else if (numviews == 3)
	{
		viewob[1] = new viewscreen( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1, this);
		viewob[0] = new viewscreen( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0, this);
		viewob[2] = new viewscreen( 112, 16, 100, 168, 2, this);
	}
	else if (numviews == 4)
	{
		viewob[1] = new viewscreen( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1, this);
		viewob[0] = new viewscreen( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0, this);
		viewob[2] = new viewscreen( 112, 16, 100, 168, 2, this);
		viewob[3] = new viewscreen( 112, 16, 100, 168, 3, this);
	}

	end = 0;

	redrawme = 1;
	
	save_data.reset();
	level_data.clear();

	timerstart = query_timer_control();
	framecount = 0;
	enemy_freeze = 0;

	control_hp = 0;

	palmode = 0;

	end = 0;

	redrawme = 1;

}

short screen::query_grid_passable(short x, short y, walker  *ob)
{
	Sint32 i,j;
	//  short xsize=ob->sizex, ysize=ob->sizey;
	Sint32 xtrax = 1;
	Sint32 xtray = 1;
	Sint32 xtarg; //the for loop target
	Sint32 ytarg; //the for loop target
	Sint32 dist;
	// NOTE: we're going to shrink dimensions by one in each..
	//Sint32 xover = (Sint32) (x+ob->sizex-1), yover = (Sint32) (y+ob->sizey-1);
	Sint32 xover = (Sint32) (x+ob->sizex), yover = (Sint32) (y+ob->sizey);

	// Again, this is for shrinking ...
	//x+=1;
	//y+=1;

	if (x < 0 || y < 0 || xover >= level_data.pixmaxx || yover >= level_data.pixmaxy)
		return 0;

	// Are we ethereal?
	if (ob->stats->query_bit_flags(BIT_ETHEREAL) )
		return 1; //moved up to avoid unneeded calculation

	// Zardus: PORT: Does the grid exist?
	if (!level_data.grid.valid())
		return 0;

	// Check if our butt hangs over shorto next grid square
	if (!((xover)%GRID_SIZE))
		xtrax = 0; //this should be the rare case
	if (!((yover)%GRID_SIZE))
		xtray = 0; //this should be the rare case


	// Check grid squares by simulated grid coords.

	xtarg = (xover/GRID_SIZE) + xtrax;
	ytarg = (yover/GRID_SIZE) + xtray;

	for (i = x/GRID_SIZE; i < xtarg; i++)
		for (j = y/GRID_SIZE; j < ytarg; j++)

		{
			// Check if item in background grid
			switch ((unsigned char)level_data.grid.data[i+level_data.grid.w*j])
			{
				case PIX_GRASS1:  // grass is pass..
				case PIX_GRASS2:
				case PIX_GRASS3:
				case PIX_GRASS4:
				case PIX_GRASS_DARK_1:
				case PIX_GRASS_DARK_2:
				case PIX_GRASS_DARK_3:
				case PIX_GRASS_DARK_4:
				case PIX_GRASS_DARK_LL:
				case PIX_GRASS_DARK_UR:
				case PIX_GRASS_DARK_B1: // shadowed edges
				case PIX_GRASS_DARK_B2:
				case PIX_GRASS_DARK_BR:
				case PIX_GRASS_DARK_R1:
				case PIX_GRASS_DARK_R2:
				case PIX_GRASS_RUBBLE:
				case PIX_GRASS1_DAMAGED:
				case PIX_GRASS_LIGHT_1: // lighter grass
				case PIX_GRASS_LIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT_TOP:
				case PIX_GRASS_LIGHT_RIGHT:
				case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT_BOTTOM:
				case PIX_GRASS_LIGHT_LEFT:
				case PIX_GRASS_LIGHT_LEFT_TOP:
				case PIX_GRASSWATER_LL: // mostly grass
				case PIX_GRASSWATER_LR:
				case PIX_GRASSWATER_UL:
				case PIX_GRASSWATER_UR:
				case PIX_PAVEMENT1:   // floor ok
				case PIX_PAVEMENT2:
				case PIX_PAVEMENT3:
				case PIX_COBBLE_1:    // Cobblestone
				case PIX_COBBLE_2:
				case PIX_COBBLE_3:
				case PIX_COBBLE_4:
				case PIX_FLOOR_PAVEL: // wood/tile ok
				case PIX_FLOOR_PAVER:
				case PIX_FLOOR_PAVEU:
				case PIX_FLOOR_PAVED:
				case PIX_PAVESTEPS1:  // steps
				case PIX_PAVESTEPS2:
				case PIX_PAVESTEPS2L:
				case PIX_PAVESTEPS2R:
				case PIX_FLOOR1:
				case PIX_CARPET_LL:   // carpet ok
				case PIX_CARPET_B:
				case PIX_CARPET_LR:
				case PIX_CARPET_UR:
				case PIX_CARPET_U:
				case PIX_CARPET_UL:
				case PIX_CARPET_L:
				case PIX_CARPET_M:
				case PIX_CARPET_M2:
				case PIX_CARPET_R:
				case PIX_CARPET_SMALL_HOR:
 				case PIX_CARPET_SMALL_VER:
				case PIX_CARPET_SMALL_CUP:
				case PIX_CARPET_SMALL_CAP:
				case PIX_CARPET_SMALL_LEFT:
				case PIX_CARPET_SMALL_RIGHT:
				case PIX_CARPET_SMALL_TINY:
				case PIX_DIRT_1:    // Dirt paths
				case PIX_DIRTGRASS_UL1:
				case PIX_DIRTGRASS_UR1:
				case PIX_DIRTGRASS_LL1:
				case PIX_DIRTGRASS_LR1:
				case PIX_DIRT_DARK_1:        // shadowed dirt/grass
				case PIX_DIRTGRASS_DARK_UL1:
				case PIX_DIRTGRASS_DARK_UR1:
				case PIX_DIRTGRASS_DARK_LL1:
				case PIX_DIRTGRASS_DARK_LR1:
				case PIX_PATH_1:
				case PIX_PATH_2:
				case PIX_PATH_3:
				case PIX_PATH_4:
					break;
				case PIX_TREE_M1:  // trees are usually bad, but
				case PIX_TREE_ML:  // we can fly over them
				case PIX_TREE_MR:
				case PIX_TREE_MT:
				case PIX_TREE_T1:
					if (ob->stats->query_bit_flags(BIT_FORESTWALK) )
						break;
					else if (ob->stats->query_bit_flags(BIT_FLYING) || ob->flight_left)
						break;
					else
						return 0;
				case PIX_TREE_B1:  // Tree bottoms
					{
						if (ob->query_order() == ORDER_WEAPON
						        || ob->stats->query_bit_flags(BIT_FORESTWALK) )
							break;
						else if (ob->stats->query_bit_flags(BIT_FLYING) || ob->flight_left)
							break;
						else
							return 0;
					}

				case PIX_H_WALL1: // walls bad, but we can "ethereal"
				case PIX_WALL2:   // through them by default
				case PIX_WALL3:
				case PIX_WALL_LL:
				case PIX_WALLTOP_H:
					return 0;// break;

				case PIX_WALL4:  // Arrow slits
				case PIX_WALL5:
				case PIX_WALL_ARROW_GRASS:
				case PIX_WALL_ARROW_FLOOR:
				case PIX_WALL_ARROW_GRASS_DARK:
					{
						//if (!ob->owner)
						if (ob->query_order()==ORDER_LIVING)
							return 0;

						if (abs(ob->xpos - ob->owner->xpos)>
						        abs(ob->ypos - ob->owner->ypos))
							dist = abs(ob->xpos - ob->owner->xpos);
						else
							dist = abs(ob->ypos - ob->owner->ypos);
						dist -= (GRID_SIZE/2);
						if (dist < GRID_SIZE)
							dist += GRID_SIZE;
						if (random(dist/GRID_SIZE))
							return 0;
					}
				case PIX_WATER1:      // Water
				case PIX_WATER2:
				case PIX_WATER3:
				case PIX_WATERGRASS_LL:
				case PIX_WATERGRASS_LR:
				case PIX_WATERGRASS_UL:
				case PIX_WATERGRASS_UR:
				case PIX_WATERGRASS_U:
				case PIX_WATERGRASS_L:
				case PIX_WATERGRASS_R:
				case PIX_WATERGRASS_D:
				case PIX_WALLSIDE_L:  // v. walls
				case PIX_WALLSIDE1:
				case PIX_WALLSIDE_R:
				case PIX_WALLSIDE_C:
				case PIX_WALLSIDE_CRACK_C1:
				case PIX_TORCH1:
				case PIX_TORCH2:
				case PIX_TORCH3:
				case PIX_BRAZIER1:            // brazier
				case PIX_COLUMN1:             //Columns
				case PIX_COLUMN2:
				case PIX_BOULDER_1: // Rocks
				case PIX_BOULDER_2:
				case PIX_BOULDER_3:
				case PIX_BOULDER_4:
					{
						if (ob->query_order() == ORDER_WEAPON)
							break;
						else if (ob->stats->query_bit_flags(BIT_FLYING) || ob->flight_left)
							break;
						else
							return 0;
					}
				default:
					return 0;
			}

		}
	return 1;
}

short screen::query_object_passable(short x, short y, walker  *ob)
{
	if (ob->dead)
		return 1;
	return level_data.myobmap->query_list(ob, x, y);
}

short screen::query_passable(short x,short y,walker  *ob)
{
	if (query_grid_passable(x, y, ob) &&
	        query_object_passable(x, y, ob) )
		return 1;
	return 0;
}

void screen::clear()
{
	unsigned short i;

	//buffers: PORT:  for (i=0;i<64000;i++)
	//buffers: PORT:  {
	//buffers: PORT:         videobuffer[i] = 0;
	//buffers: PORT:  }
	clearbuffer();
	//SDL_FillRect(screen,NULL,SDL_MapRGB(screen->format,0,0,0));

	for (i=0; i < numviews; i ++)
		viewob[i]->clear();
}

// REDRAW -- This function moves through the data on the grid (map)
//           finding which grid squares are on screen.  For each on
//           screen, it pashorts the appropriate graphics pixie onto
//           the screen by calling the function DRAW in PIXIE.
short screen::redraw()
{
	short i;
	for (i=0; i < numviews; i++)
		viewob[i]->redraw();

	return 1;
}


// REFRESH -- refreshes the viewscreens
void screen::refresh()
{
	short i;
	for (i=0; i < numviews; i++)
	{
		viewob[i]->refresh();
	}
}


// **************************
// Useful stuff again
// **************************

short screen::input(const SDL_Event& event)
{
	// static text mytext;
	short i;

	for (i=0; i < numviews; i++)
		viewob[i]->input(event);

	return 1;
}

short screen::continuous_input()
{
	// static text mytext;
	short i;

	for (i=0; i < numviews; i++)
		viewob[i]->continuous_input();

	return 1;
}

short screen::act()
{
	oblink  *here,  *before;
	here = level_data.oblist;
	static char obmessage[80];
	Sint32 printed_time = 0; // have we printed message yet?
	//  static short debug = 0;

	level_done = 2; // unless we find valid foes while looping

	if (enemy_freeze)
		enemy_freeze--;
	if (enemy_freeze == 1)
		set_palette(ourpalette);

	while(here)
	{
		if (!enemy_freeze) // normal functionality
		{
			if (here->ob && !here->ob->dead)
			{
				here->ob->in_act = 1; // Zardus: while acting, in_act is set
				here->ob->act();
				here->ob->in_act = 0;
				if (here->ob && !here->ob->dead)
				{
					if (!here->ob->is_friendly_to_team(save_data.my_team) &&
					        here->ob->query_order() == ORDER_LIVING)
						level_done = 0;
					// Testing .. trying to FORCE foes :)
					if (here->ob->foe == NULL && here->ob->leader == NULL)
						here->ob->foe = myscreen->find_far_foe(here->ob);
				}
			}
			here = here->next;
		}
		else // enemy livings are frozen
		{
			if (!(enemy_freeze%10) && !printed_time)
			{
				sprintf(obmessage, "TIME LEFT: %d", enemy_freeze);
				viewob[0]->set_display_text(obmessage, 10);
				printed_time = 1;
			}
			if (here->ob && !here->ob->dead &&
			        ( (    (here->ob->query_order() != ORDER_LIVING)
			               && (here->ob->query_order() != ORDER_GENERATOR)
			          ) || (here->ob->team_num == 0) )
			   )
			{
				here->ob->act();
				if (here->ob && !here->ob->dead)
				{
					if (!here->ob->is_friendly_to_team(save_data.my_team) &&
					        here->ob->query_order() == ORDER_LIVING)
						level_done = 0;
				}
			}
			here = here->next;
		}

	}

	// Let the weapons act ...
	here = level_data.weaplist;
	while(here)
	{
		if (here->ob && !here->ob->dead)
		{
			here->ob->act();
			if (here->ob && !here->ob->dead)
			{
				if (!here->ob->is_friendly_to_team(save_data.my_team) &&
				        here->ob->query_order() == ORDER_LIVING)
					level_done = 0;
			}
		}
		here = here->next;
	}  // end of weapons acting

	// Quickly check the background for exits, etc.
	here = level_data.fxlist;
	while (here)
	{
		if (here->ob && !here->ob->dead)
		{
			if (here->ob->query_order() == ORDER_TREASURE &&
			        here->ob->query_family() == FAMILY_EXIT &&
			        level_done != 0)
			{
				level_done = 1; // 0 => foes, 1 => no foes but exit, 2 => no foes or exit
			}
		}
		here = here->next;
	}

	if (level_done == 2)
		return endgame(0, level_data.id + 1);  // No exits and no enemies: Go to next sequential level.

	// Make sure we're all pointing to legal targets
	here = level_data.oblist;
	while (here)
	{
		if (here->ob)
		{
			if (here->ob->foe && here->ob->foe->dead)
				here->ob->foe = NULL;
			if (here->ob->leader && here->ob->leader->dead)
				here->ob->leader = NULL;
			if (here->ob->owner && here->ob->owner->dead)
				here->ob->owner = NULL;
			if (here->ob->collide_ob && here->ob->collide_ob->dead)
				here->ob->collide_ob = NULL;
		}
		here = here->next;
	}
	here = level_data.weaplist;
	while (here)
	{
		if (here->ob)
		{
			if (here->ob->foe && here->ob->foe->dead)
				here->ob->foe = NULL;
			if (here->ob->leader && here->ob->leader->dead)
				here->ob->leader = NULL;
			if (here->ob->owner && here->ob->owner->dead)
				here->ob->owner = NULL;
			if (here->ob->collide_ob && here->ob->collide_ob->dead)
				here->ob->collide_ob = NULL;
		}
		here = here->next;
	}


	// Remove dead objects
	here = level_data.oblist;
	while (here)
	{
		if (here->ob && here->ob->dead)
		{
		    // Delete the dead thing safely
		    
			// Is it a player?
			if(here->ob->user != -1)
			{
			    // Remove it from its viewscreen
			    for(int i = 0; i < numviews; i++)
			    {
			        if(here->ob == viewob[i]->control)
                        viewob[i]->control = NULL;
			    }
			}
			
			// Delete it
			delete here->ob;
			here->ob = NULL;
		}
		here = here->next;
	}
	here = level_data.fxlist;
	while (here)
	{
		if (here->ob && here->ob->dead)
		{
			//remove_fx_ob(here->ob);
			delete here->ob;
			here->ob = NULL;
		}
		here = here->next;
	}
	here = level_data.weaplist;
	while (here)
	{
		if (here->ob && here->ob->dead)
		{
			if (!(here->ob->query_order() == ORDER_WEAPON))
			{
				delete here->ob;
				here->ob = NULL;
			}
			else //push the weapon onto the cache list
			{
				here->ob->cachenext = weapfree;
				weapfree = here->ob;
				here->ob = NULL;
			}
		}
		here = here->next;
	}

	// ** Remove empty objects **
	// ** First the normal list **
	here = level_data.oblist;
	// Make first element clean
	while (!here->ob)
	{
		level_data.oblist = level_data.oblist->next;
		delete here;
		here = level_data.oblist;
	}
	// Fix rest of elements
	before = level_data.oblist;
	here = before->next;
	while (here)
	{
		if (!here->ob)  //clean element here
		{
			before->next = here->next;
			delete here;
			here = before->next;
		}
		else  // else advance
		{
			before = here;
			here = before->next;
		}
	}
	// ** Now the weapons list **
	here = level_data.weaplist;
	// Make first element clean
	while (here && !here->ob)
	{
		level_data.weaplist = level_data.weaplist->next;
		delete here;
		here = level_data.weaplist;
	}
	// Fix rest of weapon elements
	before = level_data.weaplist;
	if (before)
		here = before->next;
	else
		here = NULL;
	while (here)
	{
		if (!here->ob)  //clean element here
		{
			before->next = here->next;
			delete here;
			here = before->next;
		}
		else  // else advance
		{
			before = here;
			here = before->next;
		}
	}

	return 1;
}

walker  *screen::add_ob(char order, char family) // atstart == 0
{
	return add_ob(order, family, 0);
}

walker  *screen::add_ob(char order, char family, short atstart)
{
	oblink  *here = NULL;

	/*         ------      ------
	          | ob  |     | ob  |
	oblist -> |-----      |-----
	          |   ------->|  --------> Null
	          ------      ------
	*/
	// Point to end of oblink chain

	if (order == ORDER_WEAPON)
		return add_weap_ob(order, family);

	// Going to force at head of list for now, for speed, if it works
	//if (atstart) // add to the end of the list instead of the end ..
	if (level_data.oblist)
	{
		here = new oblink;
		here->ob = level_data.myloader->create_walker(order, family, this);
		if (!here->ob)
			return NULL;
		here->next = level_data.oblist;
		level_data.oblist = here;
		if (order == ORDER_LIVING)
			level_data.numobs++;
		return here->ob;
	}
	else // we're the first and only ..
	{
		here = new oblink;
		here->ob = level_data.myloader->create_walker(order, family, this);
		if (!here->ob)
			return NULL;
		here->next = NULL;
		level_data.oblist = here;
		if (order == ORDER_LIVING)
			level_data.numobs++;
		return here->ob;
	}

	here = level_data.oblist;

	if (level_data.oblist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		level_data.oblist = here;
	}

	here->next = NULL;
	here->ob = level_data.myloader->create_walker(order, family, this);

	if (order == ORDER_LIVING)
		level_data.numobs++;
	return here->ob;
}

walker  *screen::add_ob(walker  *newob)
{
	oblink  *here = NULL;

	/*         ------      ------
	          | ob  |     | ob  |
	oblist -> |-----      |-----
	          |   ------->|  --------> Null
	          ------      ------
	*/
	// Point to end of oblink chain

	if (newob->query_order() == ORDER_WEAPON)
		return add_weap_ob(newob);

	/*
	  here = oblist;
	  if (oblist)
	  {
	         while(here->next)
	                here = here->next;
	         here->next = new oblink;
	         here = here->next;
	  }
	  else  // oblink is null
	  {
	         here = new oblink;
	         oblist = here;
	  }
	*/
	if (level_data.oblist)
	{
		here = new oblink;
		here->ob = newob;
		here->next = level_data.oblist;
		level_data.oblist = here;
	}
	else // first element on list
	{
		here = new oblink;
		here->ob = newob;
		here->next = NULL;
		level_data.oblist = here;
	}

	//here->next = NULL;
	//here->ob = newob;

	if (newob->query_order() == ORDER_LIVING)
		level_data.numobs++;
	return here->ob;
}

walker  *screen::add_fx_ob(walker  *newob)
{
	oblink  *here = NULL;

	here = level_data.fxlist;
	if (level_data.fxlist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		level_data.fxlist = here;
	}

	here->next = NULL;
	here->ob = newob;

	//numobs++;
	return here->ob;
}

walker  *screen::add_fx_ob(char order, char family)
{
	oblink  *here = NULL;

	here = level_data.fxlist;
	if (level_data.fxlist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		level_data.fxlist = here;
	}

	here->next = NULL;
	here->ob = level_data.myloader->create_walker(order, family, this);

	//numobs++;
	//here->ob->ignore = 1;
	return here->ob;
}
//add an existing weapon to the weapon list
walker  *screen::add_weap_ob(walker  *newob)
{
	oblink *here;

	// We can add to the front, making things faster ..
	here = new oblink;
	here->ob = newob;
	here->next = level_data.weaplist;
	level_data.weaplist = here; // set weaplist to top of list again

	return here->ob;
}

walker  *screen::add_weap_ob(char order, char family)
{
	oblink *here = new oblink;

	here->ob = level_data.myloader->create_walker(order, family, this);
	here->next = level_data.weaplist;
	level_data.weaplist = here;

	return here->ob;
}

//short screen::remove_ob(walker  *ob)
//{
//  return remove_ob(ob, 0); // call with delete allowed
//}
//removed, to force calling with correct parameters

// Delay removal of linked list until end of act
//   so as to prevent linked list problems
short screen::remove_ob(walker  *ob, short no_delete)
{
	oblink  *here, *prev;

	if (ob && ob->query_order() == ORDER_LIVING)
		level_data.numobs--;

	here = level_data.weaplist; //most common case
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			level_data.weaplist = level_data.weaplist->next;
			delete here;
			return 1;
		}

	prev = here;
	while (here)
	{
		if (here->ob && here->ob == ob) //this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			prev->next = here->next; // remove this link
			delete here;
			return 1; //we found it, at least
		}
		prev = here;
		here = here->next;
	}


	here = level_data.fxlist; //less common
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			level_data.fxlist = level_data.fxlist->next;
			delete here;
			return 1;
		}

	prev = here;
	while (here)
	{
		if (here->ob && here->ob == ob) //this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			prev->next = here->next; // remove this link
			delete here;
			return 1; //we found it, at least
		}
		prev = here;
		here = here->next;
	}


	here = level_data.oblist; //less common
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			level_data.oblist = level_data.oblist->next;
			delete here;
			return 1;
		}

	prev = here;
	while (here)
	{
		if (here->ob && here->ob == ob) //this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			prev->next = here->next; // remove this link
			delete here;
			return 1; //we found it, at least
		}
		prev = here;
		here = here->next;
	}

	return 0;
}

//short screen::remove_fx_ob(walker  *ob)
//{
//  return remove_fx_ob(ob, 0);
//}

//short screen::remove_fx_ob(walker  *ob, short no_delete)
//{
//  oblink  *here;
//  here = fxlist;
//  while (here)
//  {
//    if (here->ob && here->ob == ob)
//    {
//      if (!no_delete)
//        delete here->ob;
//      here->ob = NULL;
//      return 1;
//    }
//    here = here->next;
//  }
//  return 0;  // means we failed
//}
//remove_fx_ob functionality has been moved to remove_ob


short screen::endgame(short ending)
{
	return endgame(ending, -1);
}

short screen::endgame(short ending, short nextlevel)
{
	char temp[50];
	text mytext(this, TEXT_1);
	Uint32 bonuscash[4] = {0, 0, 0, 0};
	oblink *checklist = level_data.oblist;
	walker *target;
	Sint32 test1;
	int  i;
	Uint32 allscore = 0, allbonuscash = 0;

	for (i=0; i < 4; i++)
		allscore += save_data.m_score[i];

	if (ending == 1)  // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
			//buffers: we will port the red pal stuff later
			//buffers: set_palette(redpalette);
			draw_dialog(30, 70, 290, 134, "Defeat!");
			mytext.write_y(92,"YOUR MEN ARE CRUSHED!", DARK_BLUE, 1);
			sprintf(temp,"YOUR SCORE IS %u.\n", allscore);
			mytext.write_y(100,temp, DARK_BLUE, 1);
			mytext.write_y(110,"**" CONTINUE_ACTION_STRING " TO RETURN TO THE MENUS.**", DARK_BLUE, 1);
			buffer_to_screen(0, 0, 320, 200);
			// Zardus: all things should listen to get_input_events() for now until further notice
			clear_keyboard();
			while (!query_input_continue())
				get_input_events(WAIT);
			end = 1;
		}
		else // we're withdrawing to another level
		{
			//buffers: we will port the red pal stuff later
			//buffers: set_palette(redpalette);
			draw_dialog(30, 70, 290, 134, "Retreat!");
			sprintf(temp, "Retreating to Level %d", nextlevel);
			mytext.write_y(92,temp, DARK_BLUE, 1);
			sprintf(temp,"(You may take this field later)");
			mytext.write_y(100,temp, DARK_BLUE, 1);
			mytext.write_y(110,"**" CONTINUE_ACTION_STRING " TO RETURN TO THE MENUS.**", DARK_BLUE, 1);
			buffer_to_screen(0, 0, 320, 200);
			clear_keyboard();
			while (!query_input_continue())
				get_input_events(WAIT);
			//while (query_key() != SDLK_ESCAPE);
			end = 1;
		}

	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		//buffers: we will port the red pal stuff later
		//buffers: set_palette(redpalette);
		//draw_button(30,82,290,122,4);
		draw_dialog(30, 70, 290, 134, "Defeat!");
		mytext.write_y(92,"YOU ARE DEFEATED", DARK_BLUE, 1);
		sprintf(temp,"(YOU FAILED TO KEEP THE NPC'S ALIVE)" );
		mytext.write_y(100,temp, DARK_BLUE, 1);
		mytext.write_y(110,"**" CONTINUE_ACTION_STRING " TO RETURN TO THE MENUS.**", DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);
        clear_keyboard();
        while (!query_input_continue())
            get_input_events(WAIT);
		end = 1;
	}
	else if (ending == 0) // we won
	{
		//buffers: we will port the red pal stuff later
		//buffers: set_palette(redpalette);
		if (save_data.is_level_completed(save_data.scen_num)) // this scenario is completed ..
		{
			draw_dialog(30, 70, 290, 134, "Traveling On..");
			// Zardus: FIX: what the hell is this supposed to mean?
			//      sprintf(temp,"(Field Already Won)", allscore);
			sprintf(temp, "(Field Already Won)");
			mytext.write_y(100,temp, DARK_BLUE, 1);
		}
		else
		{
			draw_dialog(30, 70, 290, 134, "Victory!");
			mytext.write_y(92,"YOU WIN.", DARK_BLUE, 1);
			sprintf(temp,"YOUR SCORE IS %u.\n", allscore);
			mytext.write_y(100,temp, DARK_BLUE, 1);
		}
		mytext.write_y(120,"**" CONTINUE_ACTION_STRING " TO CONTINUE.**", DARK_BLUE, 1);
		// Save the game status to a temp file (savetemp.gtl)
		for (i=0; i < 4; i++)
		{
			save_data.m_totalscore[i] += save_data.m_score[i];
			save_data.m_totalcash[i] += (save_data.m_score[i]*2);
		}
		for (i=0; i < 4; i++)
		{
			bonuscash[i] = (save_data.m_score[i] * (TIME_BONUS + ((Sint32)level_data.par_value * LEVEL_BONUS) - framecount))/(TIME_BONUS + ( ((Sint32)level_data.par_value * LEVEL_BONUS)/2));
			if (bonuscash[i] < 0 || framecount > TIME_BONUS) // || framecount < 0)
				bonuscash[i] = 0;
			save_data.m_totalcash[i] += bonuscash[i];
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for (i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
		sprintf(temp,"YOUR TIME BONUS IS %u.\n",allbonuscash);
		mytext.write_y(110,temp, DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);

		// Save the level to disk ..
		save_data.add_level_completed(save_data.current_campaign, save_data.scen_num); // this scenario is completed ..
		if (nextlevel != -1)
			save_data.scen_num = nextlevel;    // Fake jumping to next level ..
        
        // Autosave because we won
        // Grab our team out of the level
        save_data.update_guys(level_data.oblist);
        // Save it
		save_data.save("save0");

		// Zardus: FIX: get_input_events should really be used instead of query_key while waiting for
		// actions
		clear_keyboard();
		while (!query_input_continue())
			get_input_events(WAIT); // pause

		// Check for guys who have gone up levels
        while (checklist)
        {
            if (checklist->ob)
                target = checklist->ob;
            else
                target = NULL;
            if (target && target->team_num==0
                    && target->query_order()==ORDER_LIVING
                    && !target->dead
                    && target->myguy
                    && target->myguy->level != calculate_level(target->myguy->exp)
               ) // check for living guy on our team, with guy pointer
            {
                //draw_button(30,82,290,132,4);
                if (target->myguy->level < calculate_level(target->myguy->exp))
                {
                    draw_dialog(30, 70, 290, 134, "Congratulations!");
                    sprintf(temp, "%s reached level %d",
                            target->myguy->name,
                            calculate_level(target->myguy->exp) );
                }
                else // we lost levels :>
                {
                    draw_dialog(30, 70, 290, 134, "Alas!");
                    sprintf(temp, "%s fell to level %d",
                            target->myguy->name,
                            calculate_level(target->myguy->exp) );
                }
                mytext.write_y(100,temp, DARK_BLUE, 1);
                test1 = calculate_level(target->myguy->exp) - 1;
                if ( !(test1%3) ) // we're on a special-gaining level
                {
                    test1 = (test1 / 3) + 1; // this is the special #
                    if ( (test1 <= 4) // raise this when we have more than 4 specials
                            && (strcmp(special_name[(int)target->query_family()][test1], "NONE") )
                       )
                    {
                        sprintf(temp, "New Ability: %s!",
                                special_name[(int)target->query_family()][test1]);
                        mytext.write_y(110, temp, DARK_BLUE, 1);
                    }
                }
                mytext.write_y(120, CONTINUE_ACTION_STRING " TO CONTINUE", DARK_BLUE, 1);
                buffer_to_screen(0, 0, 320, 200);
                clear_keyboard();
                while (!query_input_continue())
                    get_input_events(WAIT);
            }
            checklist = checklist->next;
        } // end of while checklist
        // end of full 'check for raised levels' routine

		end = 1;
	}

	return 1;
}

walker *screen::find_near_foe(walker  *ob)
{
	short targx, targy;
	oblink  *here;
	short spread=1,xchange=0;
	short loop=0;
	short resolution = level_data.myobmap->obmapres;

	if (!ob)
	{
		Log("no ob in find near foe.\n");
		return NULL;
	}
	targx = ob->xpos;
	targy = ob->ypos;
	spread = 1;

	while (spread < MAX_SPREAD)
	{
		for (loop=0;loop<spread;loop++)
		{
			if (!(xchange%2))
			{
				targx += resolution; //changex is 0 or a mult of 2
				if (targx<=0)
					return find_far_foe(ob); //left edge of screen
				if (targx>=level_data.pixmaxx)
					return find_far_foe(ob); //right edge of screen
			}
			else
			{
				targy += resolution; //changex is odd
				if (targy<=0)
					return find_far_foe(ob); //top of screen
				if (targy>=level_data.pixmaxy)
					return find_far_foe(ob); //bottom of screen
			}

			here = level_data.myobmap->obmap_get_list(targx,targy);
			while(here) //go through the list we received
			{
				if (here->ob && !(here->ob->dead) && (ob->is_friendly(here->ob)==0)  &&
				        (random(here->ob->invisibility_left/20)==0)
				   )
				{
					if (here->ob->query_order() == ORDER_LIVING ||
					        here->ob->query_order() == ORDER_GENERATOR)
						//done separately since they are logically more significant
						return here->ob; // this should be a valid foe
				}
				here=here->next;
			}//end inner while

		}//end for
		xchange++; //change whether we do x or y in each for loop
		if (!(xchange%2))
		{
			resolution = (short) (-resolution); // reverse direction around the search every other for
			spread++; // increase the search width every other for
		}
	}//end while
	//failure
	return find_far_foe(ob);

}

walker  *screen::find_far_foe(walker  *ob)
{
	//short targx, targy;
	Sint32 distance, tempdistance;
	oblink  *here;
	walker  *foe,  *endfoe;

	if (!ob)
	{
		Log("no ob in find far foe.\n");
		return NULL;
	}
	here = level_data.oblist;  // Get list of all screen objects

	// Get our current coordinates
	//targx = ob->xpos;
	//targy = ob->ypos;

	// Set our 'default' foe to NULL
	endfoe = NULL;
	distance = 10000;
	ob->stats->last_distance = 10000;

	while (here)
	{
		if (here->ob && !here->ob->dead)
			foe = here->ob; // For easier referencing
		else
			foe = NULL;
		// Check for valid objects ..
		if (foe && (ob->is_friendly(foe)==0) )
		{
			if (
			    (foe->query_order() == ORDER_LIVING ||
			     foe->query_order() == ORDER_GENERATOR)  &&
			    (!(random(here->ob->invisibility_left/20)))
			)
			{
				tempdistance = ob->distance_to_ob(foe);
				if (tempdistance < distance)
				{
					distance = tempdistance;
					endfoe = foe;
				}
			}
		}
		here = here->next;
	}
	return endfoe;
}

walker* screen::set_walker(walker *ob, char order, char family)
{
    return level_data.myloader->set_walker(ob, order, family);
}

const char* screen::get_scen_title(const char *filename, screen *master)
{
	SDL_RWops  *infile = NULL;
	char temptext[10] = "XXX";
	char tempfile[80] = "x.x";
	char versionnumber = 0;
	static char buffer[30];

	strcpy(tempfile, filename);
	strcat(tempfile, ".fss");

	// Zardus: first get the file from scen/
	if (!(infile = open_read_file("scen/", tempfile)))
	{
        return "none";
	}

	// Are we a scenario file?
	SDL_RWread(infile, temptext, 3, 1);
	if (strcmp(temptext, "FSS"))
	{
		return "none";
	}

	// Check the version number
	SDL_RWread(infile, &versionnumber, 1, 1);
	if (versionnumber < 6)
		return "none";

	// Discard the grid name ...
	SDL_RWread(infile, buffer, 8, 1);

	// Return the title, 30 bytes
	SDL_RWread(infile, buffer, 30, 1);

	if (infile)
    {
	    SDL_RWclose(infile);
    }
	return buffer;

}


// Look for the first non-dead instance of a given walker ..
walker  * screen::first_of(unsigned char whatorder, unsigned char whatfamily,
                           int team_num)
{
	oblink  * here = level_data.oblist;

	if (!here)
		return NULL;

	while (here)
	{
		if (here->ob && !here->ob->dead)
		{
			if (here->ob->query_order() == whatorder &&
			        here->ob->query_family()== whatfamily)
			{
				if (team_num == -1 || team_num == here->ob->team_num)
					return here->ob;
			}
		}
		here = here->next;
	}
	return NULL;
}

walker  * screen::get_new_control()
{
	oblink  * here;

	here = level_data.oblist;
	while(here)
	{
		if (here->ob &&
		        here->ob->query_order() == ORDER_LIVING &&
		        here->ob->query_act_type() != ACT_CONTROL &&
		        here->ob->team_num == save_data.my_team)
			break;
		here = here->next;
	}

	if (!here)
	{
		endgame(1);
		return NULL;
	}

	return NULL;
}

void screen::draw_panels(short howmany)
{
	short i;

	// Force a memory clear ..
	clearbuffer();

	if (howmany)
		howmany = howmany;
	for (i=0; i < numviews; i++)
		if ( (viewob[i]->prefs[PREF_VIEW] == PREF_VIEW_FULL) ||
		        numviews == 4 )
			; // do nothing
		else
		{
			draw_button(viewob[i]->xloc-4, viewob[i]->yloc-3,
			            viewob[i]->endx+3, viewob[i]->endy+3, 3, 1);
			draw_box(viewob[i]->xloc-1, viewob[i]->yloc-1,
			         viewob[i]->endx, viewob[i]->endy, 0, 0,1);
		}


	redraw(); // repaint the screen area ..
	buffer_to_screen(0, 0, 320, 200);
}

// This can be slow, so don't call it much
walker  * screen::find_nearest_blood(walker  *who)
{
	oblink  *here;
	Sint32 distance, newdistance;
	walker  *returnob = NULL;

	here = level_data.fxlist;

	if (!who)
		return NULL;

	distance = 800;

	while (here)
	{
		if (here->ob && here->ob->query_order() == ORDER_TREASURE &&
		        here->ob->query_family() == FAMILY_STAIN && !here->ob->dead)
		{
			newdistance = (Uint32) who->distance_to_ob_center(here->ob);
			if (newdistance < distance)
			{
				distance = newdistance;
				returnob = here->ob;
			}
		}
		here = here->next;
	}
	return returnob;

}

oblink* screen::find_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob)
{
	oblink *here;
	oblink *newlist, *newhere;
	//short obx, oby;
	Uint32 distance;

	if (!somelist || !ob)
	{
		*howmany = 0;
		return NULL;
	}

	//obx = (short) (ob->xpos + (ob->sizex/2) );  // center of object
	//oby = (short) (ob->ypos + (ob->sizey/2) );

	here = somelist;

	newlist = NULL;
	*howmany = 0;

	while (here)
	{
		if (here->ob && !here->ob->dead)
		{
			distance = (Uint32) ob->distance_to_ob(here->ob);
			if (distance <= (Uint32) range)
			{
				if (newlist) // existing list ..
				{
					newhere = newlist;
					while(newhere->next)
						newhere = newhere->next;
					newhere->next = new oblink;
					newhere = newhere->next;
				}
				else // new list is null, first on the list
				{
					newhere = new oblink;
					newlist = newhere;
				}
				newhere->next = NULL;
				newhere->ob = here->ob;
				*howmany = (short) (*howmany + 1);
			} // end of valid distance check
		} // end of valid here->ob check
		here = here->next;
	}  // end of while loop

	return newlist;
}

walker* screen::find_nearest_player(walker *ob)
{
	oblink *here;
	walker *returnob = NULL;
	Uint32 distance = 32000;
	Uint32 tempdistance;

	if (!ob)
		return NULL;

	here = level_data.oblist;
	while (here)
	{
		if (here->ob && (here->ob->user != -1) )
		{
			tempdistance = ob->distance_to_ob(here->ob);
			if (tempdistance < distance)
			{
				distance = tempdistance;
				returnob = here->ob;
			}
		}
		here = here->next;
	}

	return returnob;
}

oblink* screen::find_foes_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob)
{
	oblink *here;
	oblink *newlist, *newhere;
	Uint32 distance;

	if (!somelist || !ob)
	{
		*howmany = 0;
		return NULL;
	}

	here = somelist;

	newlist = NULL;
	*howmany = 0;

	while (here)
	{
		if (here->ob && !here->ob->dead &&
		        (here->ob->query_order() == ORDER_LIVING ||
		         here->ob->query_order() == ORDER_GENERATOR)
		        && (ob->is_friendly(here->ob) == 0)
		   )
		{
			distance = (Uint32) ob->distance_to_ob(here->ob);
			if (distance <= (Uint32) range)
			{
				if (newlist) // existing list ..
				{
					newhere = newlist;
					while(newhere->next)
						newhere = newhere->next;
					newhere->next = new oblink;
					newhere = newhere->next;
				}
				else // new list is null, first on the list
				{
					newhere = new oblink;
					newlist = newhere;
				}
				newhere->next = NULL;
				newhere->ob = here->ob;
				*howmany = (short) (*howmany + 1);
			} // end of valid distance check
		} // end of valid here->ob check
		here = here->next;
	}  // end of while loop

	return newlist;
}

oblink* screen::find_friends_in_range(oblink *somelist, Sint32 range,
                                      short *howmany, walker  *ob)
{
	oblink *here;
	oblink *newlist, *newhere;
	//short obx, oby;
	Uint32 distance;

	if (!somelist || !ob)
	{
		*howmany = 0;
		return NULL;
	}

	//obx = (short) (ob->xpos + (ob->sizex/2) );  // center of object
	//oby = (short) (ob->ypos + (ob->sizey/2) );

	here = somelist;

	newlist = NULL;
	*howmany = 0;

	while (here)
	{
		if (here->ob && !here->ob->dead && here->ob->query_order() == ORDER_LIVING
		        && ( ob->is_friendly(here->ob) )
		   )
		{
			distance = (Uint32) ob->distance_to_ob(here->ob);
			if (distance <= (Uint32) range)
			{
				if (newlist) // existing list ..
				{
					newhere = newlist;
					while(newhere->next)
						newhere = newhere->next;
					newhere->next = new oblink;
					newhere = newhere->next;
				}
				else // new list is null, first on the list
				{
					newhere = new oblink;
					newlist = newhere;
				}
				newhere->next = NULL;
				newhere->ob = here->ob;
				*howmany = (short) (*howmany + 1);
			} // end of valid distance check
		} // end of valid here->ob check
		here = here->next;
	}  // end of while loop

	return newlist;
}

oblink* screen::find_foe_weapons_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob)
{
	oblink *here;
	oblink *newlist, *newhere;
	Uint32 distance;

	if (!somelist || !ob)
	{
		*howmany = 0;
		return NULL;
	}

	here = somelist;

	newlist = NULL;
	*howmany = 0;

	while (here)
	{
		if (here->ob && !here->ob->dead &&
		        (here->ob->query_order() == ORDER_WEAPON)
		        && ( ob->is_friendly(here->ob) )
		   )
		{
			distance = (Uint32) ob->distance_to_ob(here->ob);
			if (distance <= (Uint32) range)
			{
				if (newlist) // existing list ..
				{
					newhere = newlist;
					while(newhere->next)
						newhere = newhere->next;
					newhere->next = new oblink;
					newhere = newhere->next;
				}
				else // new list is null, first on the list
				{
					newhere = new oblink;
					newlist = newhere;
				}
				newhere->next = NULL;
				newhere->ob = here->ob;
				*howmany = (short) (*howmany + 1);
			} // end of valid distance check
		} // end of valid here->ob check
		here = here->next;
	}  // end of while loop

	return newlist;
}


// Uses pixel coordinates
char screen::damage_tile(short xloc, short yloc) // damage the specified tile
{
	short xover, yover;
	short gridloc;

	xover = (short) (xloc / GRID_SIZE);
	yover = (short) (yloc / GRID_SIZE);

	if (xover < 0 || yover < 0)
		return 0;
	if (xover >= level_data.grid.w || yover >= level_data.grid.h)
		return 0;

	gridloc = (short) (yover*level_data.grid.w+xover);

	switch ((unsigned char)level_data.grid.data[gridloc])
	{
		case PIX_GRASS1: // grass
		case PIX_GRASS2:
		case PIX_GRASS3:
		case PIX_GRASS4:
			level_data.grid.data[gridloc] = PIX_GRASS1_DAMAGED;
			break;
		default:
			break;
	}

	return level_data.grid.data[gridloc];
}

void screen::do_notify(const char *message, walker  *who)
{
	short i,sent=0;
	for(i=0;i<numviews;i++)
	{
		if (who && viewob[i]->control == who)
		{
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);
			sent = 1;
		}

	}
	if (!sent)
		for (i=0; i < numviews; i++)
			viewob[i]->set_display_text(message,STANDARD_TEXT_TIME);

}

void screen::report_mem()
{
	meminfo Memory;
	Memory.FreeLinAddrSpace = 0;
	// Zardus: PORT: this is aparently an incomplete type:  union REGS regs;
	// Same here:  struct SREGS sregs;
	char memreport[80];

	// Zardus: PORT: Undeclared because of problems above:  regs.x.eax = 0x00000500;
	// Same here:  memset( &sregs, 0, sizeof(sregs) );

	// See two lines up:  sregs.es = FP_SEG( &Memory );
	// See three lines up:  regs.x.edi = FP_OFF( &Memory );

	// See two lines up: (plus sounds like a dos thing):  int386x( DPMI_INT, &regs, &regs, &sregs );

	// Them:
	//sprintf(memreport, "Largest Block: %lu bytes",
	//  Memory.LargestBlockAvail);
	//viewob[0]->set_display_text(memreport, STANDARD_TEXT_TIME);
	sprintf(memreport, "Free Linear address: %u pages",
	        Memory.FreeLinAddrSpace);
	//  Log(memreport);
	//  Log("\n");
	viewob[0]->set_display_text(memreport, 25);
	/*
	       Log( "Largest available block (in bytes): %lu\n",
	               MemInfo.LargestBlockAvail );
	       Log( "Maximum unlocked page allocation: %lu\n",
	               MemInfo.MaxUnlockedPage );
	       Log( "Pages that can be allocated and locked: "
	               "%lu\n", MemInfo.LargestLockablePage );
	       Log( "Total linear address space including "
	               "allocated pages: %lu\n",
	               MemInfo.LinAddrSpace );
	       Log( "Number of free pages available: %lu\n",
	                MemInfo.NumFreePagesAvail );

	       Log( "Number of physical pages not in use: %lu\n",
	                MemInfo.NumPhysicalPagesFree );
	       Log( "Total physical pages managed by host: %lu\n",
	                MemInfo.TotalPhysicalPages );
	       Log( "Free linear address space (pages): %lu\n",
	                MemInfo.FreeLinAddrSpace );
	       Log( "Size of paging/file partition (pages): %lu\n",
	                MemInfo.SizeOfPageFile );
	 */
}
