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
#include "gladpack.h"
#include "util.h"
#include "input.h"
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

#define VERSION_NUM 2
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

//const char* get_scen_title(const char *filename, screen *master); // get the title

char  * query_my_map_name();

char my_map_name[40];


// These are globals for the packed files ..
packfile scenpack;

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


screen::screen(short howmany):video()
{
	Sint32 i, j;
	const char *qresult;
	text first_text(this);
	Sint32 left = 66;

	grab_timer();

	grid = NULL;
	mysmoother = new smoother();
	timerstart = query_timer_control();
	framecount = 0;
	oblist = NULL;
	myobmap = new obmap(200*GRID_SIZE, 200*GRID_SIZE);
	fxlist = NULL;
	weaplist = NULL;

	weapfree = NULL;

	//  control = NULL;
	first_guy = NULL;
	//myradar[0] = myradar[1] = NULL; // very important! :)
	control_hp = 0;
	strcpy(current_campaign, "org.openglad.gladiator");
	scen_num = 1; // default scenario
	scenario_type = 0; // default, must kill all

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

	myloader = new loader;
	first_text.write_xy(left, 70, "Loading Graphics...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 78, "Loading Gameplay Info...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);
	palmode = 0;
	my_team = 0;
	topx = 0;
	topy = 0;

	end = 0;
	numobs = 0;
	timer_wait = 6;       // 'moderate' speed setting

	redrawme = 1;
	cyclemode = 1; //color cycling on by default
	score = totalcash = totalscore = 0;
	for (i=0; i < 4; i++)
	{
		m_score[i] = 0;             // For Player-v-Player
		m_totalcash[i] = 0;
		m_totalscore[i] = 0;
	}
	allied_mode = 0;              // allied mode is off by default
	par_value = 1;

	for (i=0; i < 30; i++)
		scentext[i][0] = 0;

	scentextlines = 0;
	enemy_freeze = 0;

	level_done = 0;

	// Load map data from a pixie format
	load_map_data(pixdata);
	first_text.write_xy(left, 78, "Loading Gameplay Info...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 86, "Initializing Display...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);

	// Initialize a pixie for each background piece
	for(i = 0; i < PIX_MAX; i++)
		back[i] = new pixieN(pixdata[i], this,1);

	//buffers: after we set all the tiles to use acceleration, we go
	//through the tiles that have pal cycling to turn of the accel.
	back[PIX_WATER1]->set_accel(0);
	back[PIX_WATER2]->set_accel(0);
	back[PIX_WATER3]->set_accel(0);
	back[PIX_WATERGRASS_LL]->set_accel(0);
	back[PIX_WATERGRASS_LR]->set_accel(0);
	back[PIX_WATERGRASS_UL]->set_accel(0);
	back[PIX_WATERGRASS_UR]->set_accel(0);
	back[PIX_WATERGRASS_U]->set_accel(0);
	back[PIX_WATERGRASS_D]->set_accel(0);
	back[PIX_WATERGRASS_L]->set_accel(0);
	back[PIX_WATERGRASS_R]->set_accel(0);
	back[PIX_GRASSWATER_LL]->set_accel(0);
	back[PIX_GRASSWATER_LR]->set_accel(0);
	back[PIX_GRASSWATER_UL]->set_accel(0);
	back[PIX_GRASSWATER_UR]->set_accel(0);






	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens
	for (i=0; i < MAX_VIEWS; i++)
		viewob[i] = NULL;

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
	int i;

	release_timer();
	delete soundp;
	delete myloader;
	delete mysmoother;

	for (i = 0; i < PIX_MAX; i++)
	{
		if(pixdata[i])
			free(pixdata[i]);
		// Zardus: FIXME: this should be here cause it leaks, but this makes it segfault
		if (back[i])
			delete back[i];
	}

	soundp = NULL;
	cleanup(1); //make sure we've cleaned up
}

void screen::cleanup(short howmany)
{
	Sint32 i;
	oblink *here, *templink;
	walker *who;

        numviews = howmany; // # of viewscreens
        for (i=0; i < MAX_VIEWS; i++)
        {
                if (viewob[i])
                        delete (viewob[i]);
                viewob[i] = NULL;
        }

        if (grid)
        {
                // Zardus: PORT: this segfaults while deleting grid!
                //buffers: commented free() so openglad will cleanly exit
                free(grid-3);  // grid is offset on load
                grid = NULL;
        }

        if (oblist)
        {
                here = oblist;
                while (here->next)
                {
                        templink = here;
                        if (templink->ob)
                        {
                                delete templink->ob;
                                templink->ob = NULL;
                        }
                        here = here->next;
                        delete templink;
                        templink = NULL;
                }
                if (here->ob)
                {
                        delete here->ob;
                        here->ob = NULL;
                }
                delete here;
                here = NULL;
        }
        oblist = NULL;
        if (weaplist)
        {
                here = weaplist;
                while (here->next)
                {
                        templink = here;
                        if (templink->ob)
                        {
                                delete templink->ob;
                                templink->ob = NULL;
                        }
                        here = here->next;
                        delete templink;
                        templink = NULL;
                }
                if (here->ob)
                {
                        delete here->ob;
                        here->ob = NULL;
                }
                delete here;
                here = NULL;
        }
        weaplist = NULL;
        if (fxlist)
        {
                here = fxlist;
                while (here->next)
                {
                        templink = here;
                        if (templink->ob)
                        {
                                delete templink->ob;
                                templink->ob = NULL;
                        }
                        here = here->next;
                        delete templink;
                        templink = NULL;
                }
                if (here->ob)
                {
                        delete here->ob;
                        here->ob = NULL;
                }
                delete here;
                here = NULL;
        }
        fxlist = NULL;

        while (weapfree)
        {
                who = weapfree;
                weapfree = weapfree->cachenext; //cachenext points to next ob
                delete who;
                who = NULL;
        }
        weapfree = NULL;

        if (myobmap)
        {
                delete myobmap;
                myobmap = NULL;
        }

}

void screen::reset(short howmany)
{
	Sint32 i;

	if (!mysmoother)
		mysmoother = new smoother();
	mysmoother->mygrid = NULL;

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
	
	strcpy(current_campaign, "org.openglad.gladiator");
	completed_levels.clear();

	score = totalcash = totalscore = 0;
	for (i=0; i < 4; i++)
	{
		m_score[i] = 0;
		m_totalcash[i] = 0;
		m_totalscore[i] = 0;
	}

	timerstart = query_timer_control();
	framecount = 0;
	enemy_freeze = 0;

	myobmap = new obmap(200*GRID_SIZE, 200*GRID_SIZE);
	fxlist = oblist = NULL;

	first_guy = NULL;
	control_hp = 0;
	scen_num = 1; // default scenario
	scenario_type = 0; // default, must kill all

	palmode = 0;
	my_team = 0;
	topx = 0;
	topy = 0;

	end = 0;
	numobs = 0;

	redrawme = 1;

	score = totalcash = totalscore = 0;
	for (i=0; i < 4; i++)
	{
		m_score[i] = 0;
		m_totalcash[i] = 0;
		m_totalscore[i] = 0;
	}
	allied_mode = 0;              // allied mode is off by default
	par_value = 1;

	for (i=0; i < 30; i++)
		scentext[i][0] = 0;

	scentextlines = 0;

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

	if (x < 0 || y < 0 || xover >= pixmaxx || yover >= pixmaxy)
		return 0;

	// Are we ethereal?
	if (ob->stats->query_bit_flags(BIT_ETHEREAL) )
		return 1; //moved up to avoid unneeded calculation

	// Zardus: PORT: Does the grid exist?
	if (!grid)
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
			switch ((unsigned char)grid[i+maxx*j])
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
	return myobmap->query_list(ob, x, y);
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
	clearscreen();
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
	here = oblist;
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
					if (here->ob->team_num != my_team &&
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
					if (here->ob->team_num != my_team &&
					        here->ob->query_order() == ORDER_LIVING)
						level_done = 0;
				}
			}
			here = here->next;
		}

	}

	// Let the weapons act ...
	here = weaplist;
	while(here)
	{
		if (here->ob && !here->ob->dead)
		{
			here->ob->act();
			if (here->ob && !here->ob->dead)
			{
				if (here->ob->team_num != my_team &&
				        here->ob->query_order() == ORDER_LIVING)
					level_done = 0;
			}
		}
		here = here->next;
	}  // end of weapons acting

	// Quickly check the background for exits, etc.
	here = fxlist;
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
		return endgame(0);

	// Make sure we're all pointing to legal targets
	here = oblist;
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
	here = weaplist;
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
	here = oblist;
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
	here = fxlist;
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
	here = weaplist;
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
	here = oblist;
	// Make first element clean
	while (!here->ob)
	{
		oblist = oblist->next;
		delete here;
		here = oblist;
	}
	// Fix rest of elements
	before = oblist;
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
	here = weaplist;
	// Make first element clean
	while (here && !here->ob)
	{
		weaplist = weaplist->next;
		delete here;
		here = weaplist;
	}
	// Fix rest of weapon elements
	before = weaplist;
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
	if (oblist)
	{
		here = new oblink;
		here->ob = myloader->create_walker(order, family, this);
		if (!here->ob)
			return NULL;
		here->next = oblist;
		oblist = here;
		if (order == ORDER_LIVING)
			numobs++;
		return here->ob;
	}
	else // we're the first and only ..
	{
		here = new oblink;
		here->ob = myloader->create_walker(order, family, this);
		if (!here->ob)
			return NULL;
		here->next = NULL;
		oblist = here;
		if (order == ORDER_LIVING)
			numobs++;
		return here->ob;
	}

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

	here->next = NULL;
	here->ob = myloader->create_walker(order, family, this);

	if (order == ORDER_LIVING)
		numobs++;
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
	if (oblist)
	{
		here = new oblink;
		here->ob = newob;
		here->next = oblist;
		oblist = here;
	}
	else // first element on list
	{
		here = new oblink;
		here->ob = newob;
		here->next = NULL;
		oblist = here;
	}

	//here->next = NULL;
	//here->ob = newob;

	if (newob->query_order() == ORDER_LIVING)
		numobs++;
	return here->ob;
}

walker  *screen::add_fx_ob(walker  *newob)
{
	oblink  *here = NULL;

	here = fxlist;
	if (fxlist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		fxlist = here;
	}

	here->next = NULL;
	here->ob = newob;

	//numobs++;
	return here->ob;
}

walker  *screen::add_fx_ob(char order, char family)
{
	oblink  *here = NULL;

	here = fxlist;
	if (fxlist)
	{
		while(here->next)
			here = here->next;
		here->next = new oblink;
		here = here->next;
	}
	else  // oblink is null
	{
		here = new oblink;
		fxlist = here;
	}

	here->next = NULL;
	here->ob = myloader->create_walker(order, family, this);

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
	here->next = weaplist;
	weaplist = here; // set weaplist to top of list again

	return here->ob;
}

walker  *screen::add_weap_ob(char order, char family)
{
	oblink *here = new oblink;

	here->ob = myloader->create_walker(order, family, this);
	here->next = weaplist;
	weaplist = here;

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
		numobs--;

	here = weaplist; //most common case
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			weaplist = weaplist->next;
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


	here = fxlist; //less common
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			fxlist = fxlist->next;
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


	here = oblist; //less common
	if (here)
		if (here->ob && here->ob == ob) // this is the ob we want
		{
			if (!no_delete)
			{
				delete here->ob;
			}
			oblist = oblist->next;
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
	oblink *checklist = oblist;
	walker *target;
	Sint32 test1;
	int  i;
	Uint32 allscore = 0, allbonuscash = 0;

	for (i=0; i < 4; i++)
		allscore += m_score[i];

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
        while (!query_input_continue())
            get_input_events(WAIT);
		end = 1;
	}
	else if (ending == 0) // we won
	{
		//buffers: we will port the red pal stuff later
		//buffers: set_palette(redpalette);
		if (is_level_completed(scen_num)) // this scenario is completed ..
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
			m_totalscore[i] += m_score[i];
			m_totalcash[i] += (m_score[i]*2);
		}
		for (i=0; i < 4; i++)
		{
			bonuscash[i] = (m_score[i] * (TIME_BONUS + ((Sint32)par_value * LEVEL_BONUS) - framecount))/(TIME_BONUS + ( ((Sint32)par_value * LEVEL_BONUS)/2));
			if (bonuscash[i] < 0 || framecount > TIME_BONUS) // || framecount < 0)
				bonuscash[i] = 0;
			m_totalcash[i] += bonuscash[i];
			allbonuscash += bonuscash[i];
		}
		if (is_level_completed(scen_num)) // already won, no bonus
		{
			for (i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
		sprintf(temp,"YOUR TIME BONUS IS %u.\n",allbonuscash);
		mytext.write_y(110,temp, DARK_BLUE, 1);
		buffer_to_screen(0, 0, 320, 200);

		// Save the level to disk ..
		add_level_completed(current_campaign, scen_num); // this scenario is completed ..
		if (nextlevel != -1)
			scen_num = (short) (nextlevel-1);    // Fake jumping to next level ..
		save_game("save0", this);

		// Zardus: FIX: get_input_events should really be used instead of query_key while waiting for
		// actions
		while (!query_input_continue())
			get_input_events(WAIT); // pause

		// Check for guys who have gone up levels
		if (checklist) // should always be true, but just in case
		{
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
		} // end of full 'check for raised levels' routine

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
	short resolution = myobmap->obmapres;

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
				if (targx>=pixmaxx)
					return find_far_foe(ob); //right edge of screen
			}
			else
			{
				targy += resolution; //changex is odd
				if (targy<=0)
					return find_far_foe(ob); //top of screen
				if (targy>=pixmaxy)
					return find_far_foe(ob); //bottom of screen
			}

			here = myobmap->obmap_get_list(targx,targy);
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
	here = oblist;  // Get list of all screen objects

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


const char* screen::get_scen_title(const char *filename, screen *master)
{
	SDL_RWops  *infile = NULL;
	char temptext[10] = "XXX";
	char tempfile[80] = "x.x";
	char versionnumber = 0;
	enum {notfound, file, pack} gotit = notfound;
	static char buffer[30];

	strcpy(tempfile, filename);
	strcat(tempfile, ".fss");

	// Zardus: first get the file from scen/, then the packfile
	if ((infile = open_read_file("scen/", tempfile)))
		gotit = file;
	else
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

bool screen::is_level_completed(int level_index) const
{
    std::map<std::string, std::set<int> >::const_iterator e = completed_levels.find(current_campaign);
    // Campaign not found?  Then this level is not done.
    if(e == completed_levels.end())
        return false;
    
    // If the level is listed, then it is completed.
    std::set<int>::const_iterator f = e->second.find(level_index);
    return (f != e->second.end());
}

int screen::get_num_levels_completed(const std::string& campaign) const
{
    std::map<std::string, std::set<int> >::const_iterator e = completed_levels.find(campaign);
    // Campaign not found?
    if(e == completed_levels.end())
        return 0;
    
    return e->second.size();
}

void screen::add_level_completed(const std::string& campaign, int level_index)
{
    std::map<std::string, std::set<int> >::iterator e = completed_levels.find(campaign);
    
    // Campaign not found?  Add it in.
    if(e == completed_levels.end())
        e = completed_levels.insert(std::make_pair(campaign, std::set<int>())).first;
    
    // Add the completed level
    e->second.insert(level_index);
}

//buffers: the file finding and loading code is pretty ugly... i should
//buffers: rewrite it...
short load_scenario(const char * filename, screen * master)
{
	SDL_RWops  *infile = NULL;
	char temptext[10] = "XXX";
	char versionnumber = 0;
	Sint32 gotit;
	short tempvalue;
	string thefile(filename);

	//buffers: first look for the scenario in scen/, then in levels.001

	//buffers: the file
	thefile += ".fss";
	lowercase(thefile);

	gotit = 0;

	// Zaradus: much much better this way
	if ( (infile = open_read_file("scen/", thefile.c_str())))
	{
		gotit = 1;
	}
	else
    {
        Log("Cannot open levels resource file %s!", thefile.c_str());
        exit(1);
    }

	// Are we a scenario file?
	SDL_RWread(infile, temptext, 3, 1);
	if (strcmp(temptext, "FSS"))
	{
		Log("File %s is not a scenario!\n", filename);
		SDL_RWclose(infile);
		return 0;
	}

	master->scenario_type = 0; // default unless overridden
	strcpy(master->scenario_title, "none");  // default

	// Check the version number
	SDL_RWread(infile, &versionnumber, 1, 1);
    Log("Loading version %d scenario", versionnumber);
	switch (versionnumber)
	{
		case 2:
			tempvalue = load_version_2(infile, master);
			SDL_RWclose(infile);
			return tempvalue;
			//break;
		case 3:
			tempvalue = load_version_3(infile, master);
            SDL_RWclose(infile);
			return tempvalue;
			//break;
		case 4:
			tempvalue = load_version_4(infile, master);
			SDL_RWclose(infile);
			return tempvalue;
			//break;
		case 5:
			tempvalue = load_version_5(infile, master);
			SDL_RWclose(infile);
			return tempvalue;
		case 6:
			tempvalue = load_version_6(infile, master);
			SDL_RWclose(infile);
			return tempvalue;
		case 7:
			tempvalue = load_version_6(infile, master, 7);
			SDL_RWclose(infile);
			return tempvalue;
		case 8:
			tempvalue = load_version_6(infile, master, 8);
			SDL_RWclose(infile);
			return tempvalue;
		default:
			Log("Scenario %s is version-level %d, and cannot be read.\n",
			       filename, versionnumber);
            SDL_RWclose(infile);
			//return 0;
			break;
	}
	return 1;
}

short load_version_2(SDL_RWops  *infile, screen * master)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[12] = "grid.pix";  // default grid

	// Format of a scenario object list file version 2 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// ---
	// 11 bytes reserved

	//Log("LV2: START\n");

	// Get grid file to load
	SDL_RWread(infile, newgrid, 8, 1);
	newgrid[8] = '\0';
	//buffers: PORT: make sure grid name is lowercase
	lowercase(newgrid);
	strcpy(my_map_name, newgrid);

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);

	//gotoxy(1,1);
	//Log("Objects in file: %d  ", listsize);
	//wait_for_key(SDLK_SPACE);

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, tempreserved, 11, 1);
		if (temporder == ORDER_TREASURE)
			new_guy = master->add_fx_ob(temporder, tempfamily);  // create new object
		else
			new_guy = master->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			wait_for_key(KEYSTATE_SPACE);
			return 0;
		}
		new_guy ->setxy(currentx, currenty);
		//       Log("X: %d  Y: %d  \n", currentx, currenty);
		new_guy ->team_num = tempteam;
	}

	//Log("Read %d objects.\n", listsize);
	//wait_for_key(SDLK_SPACE);

	//fclose(infile);

	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	master->grid = read_pixie_file(newgrid);
	master->maxx = master->grid[1];
	master->maxy = master->grid[2];
	master->pixmaxx = master->maxx * GRID_SIZE;
	master->pixmaxy = master->maxy * GRID_SIZE;
	master->grid += 3;

	//Log("LV2: read grid %s\n", newgrid);
	//wait_for_key(SDLK_SPACE);

	// This is a hack because we don't know where else it is loaded.
	//  if (master->myradar[0])
	//  {
	//       Log("LV2: deleting old radar\n");
	//       delete master->myradar;
	//       Log("LV2: deleted old radar\n");
	//  }
	//  if (master->numviews == 1)
	//         master->myradar[0] = new radar(master->grid, master->maxx, master->maxy,
	//                249, 145, 0, master);
	//  else
	//  {
	//         master->myradar[0] = new radar(master->grid, master->maxx, master->maxy,
	//                249, 10, 0, master);
	//         master->myradar[1] = new radar(master->grid, master->maxx, master->maxy,
	//                249-153, 10, 1, master);
	//  }


	//  Log("Read grid file\n");
	//  wait_for_key(SDLK_SPACE);

	return 1;
}

// Version 3 scenarios have a block of text which can be displayed
// at the start, etc.  Format is
// # of lines,
//  1-byte character width
//  n bytes specified from above
short load_version_3(SDL_RWops  *infile, screen * master)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[12] = "grid.pix";  // default grid
	char oneline[80];
	char numlines, tempwidth;


	// Format of a scenario object list file version 2 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	SDL_RWread(infile, newgrid, 8, 1);
	//buffers: PORT: make sure grid name is lowercase
	lowercase((char *)newgrid);
	strcpy(my_map_name, newgrid);

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			//              new_guy = master->add_fx_ob(temporder, tempfamily);  // create new object
			new_guy = master->add_ob(temporder, tempfamily, 1); // add to top of list
		else
			new_guy = master->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			wait_for_key(KEYSTATE_SPACE);
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		new_guy->stats->level = templevel;
	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);
	master->scentextlines = numlines;
	//master->(*scentext) = new char(numlines);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		SDL_RWread(infile, oneline, tempwidth, 1);
		oneline[(int)tempwidth] = 0;
		strcpy(master->scentext[i], oneline);
	}

	//fclose(infile);

	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	master->grid = read_pixie_file(newgrid);
	master->maxx = master->grid[1];
	master->maxy = master->grid[2];
	master->pixmaxx = master->maxx * GRID_SIZE;
	master->pixmaxy = master->maxy * GRID_SIZE;
	master->grid += 3;

	// This is a hack because we don't know where else it is loaded.
	//  if (master->myradar[0])
	//  {
	//  }
	//  if (master->numviews == 1)
	//         master->myradar[0] = new radar(master->grid, master->maxx, master->maxy,
	//                249, 145, 0, master);
	//  else
	//  {
	//         master->myradar[0] = new radar(master->grid, master->maxx, master->maxy,
	//                249, 10, 0, master);
	//         master->myradar[1] = new radar(master->grid, master->maxx, master->maxy,
	//                249-153, 10, 1, master);
	//  }


	return 1;
}

// Version 4 scenarios include a 12-byte name for EVERY walker..
short load_version_4(SDL_RWops  *infile, screen * master)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[12] = "grid.pix";  // default grid
	char oneline[80];
	char numlines, tempwidth;
	char tempname[12];


	// Format of a scenario object list file version 4 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level
	// 12-bytes name
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	SDL_RWread(infile, newgrid, 8, 1);
	//buffers: PORT: make sure grid name is lowercase
	lowercase((char *)newgrid);
	strcpy(my_map_name, newgrid);

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempname, 12, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			//new_guy = master->add_ob(temporder, tempfamily, 1); // add to top of list
			new_guy = master->add_fx_ob(temporder, tempfamily);
		else
			new_guy = master->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object!\n");
			wait_for_key(KEYSTATE_SPACE);
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		new_guy->stats->level = templevel;
		strcpy(new_guy->stats->name, tempname);
		if (strlen(tempname) > 1)                      //chad 5/25/95
			new_guy->stats->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);
	master->scentextlines = numlines;
	//master->(*scentext) = new char(numlines);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		SDL_RWread(infile, oneline, tempwidth, 1);
		oneline[(int)tempwidth] = 0;
		strcpy(master->scentext[i], oneline);
	}

	//fclose(infile);

	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	master->grid = read_pixie_file(newgrid);
	master->maxx = (unsigned char)master->grid[1];
	master->maxy = (unsigned char)master->grid[2];
	master->pixmaxx = master->maxx * GRID_SIZE;
	master->pixmaxy = master->maxy * GRID_SIZE;
	master->grid += 3;

	// This is a hack because we don't know where else it is loaded.
	//  if (master->myradar[0])
	//  {
	//  }
	//  if (master->numviews == 1)
	//         master->myradar[0] = new radar(master->grid, master->maxx, master->maxy,
	//                249, 145, 0, master);
	//  else
	//  {
	//         master->myradar[0] = new radar(master->grid, master->maxx, master->maxy,
	//                249, 10, 0, master);
	//         master->myradar[1] = new radar(master->grid, master->maxx, master->maxy,
	//                249-153, 10, 1, master);
	//  }


	return 1;
} // end load_version_4

// Version 5 scenarios include a 1-byte 'scenario-type' specifier after
// the grid name.
short load_version_5(SDL_RWops  *infile, screen * master)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[12] = "grid.pix";  // default grid
	char new_scen_type; // read the scenario type
	char oneline[80];
	char numlines, tempwidth;
	char tempname[12];
	oblink *here;

	// Format of a scenario object list file version 5 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 1-byte char = scenario type, default is 0
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level
	// 12-bytes name
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	SDL_RWread(infile, newgrid, 8, 1);
	//buffers: PORT: make sure grid name is lowercase
	lowercase((char *)newgrid);
	strcpy(my_map_name, newgrid);

	// Get the scenario type information
	SDL_RWread(infile, &new_scen_type, 1, 1);
	master->scenario_type = new_scen_type;

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempname, 12, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			new_guy = master->add_fx_ob(temporder, tempfamily);
		else
			new_guy = master->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object! Press Space\n");
			wait_for_key(KEYSTATE_SPACE);
			//fclose(infile);
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		new_guy->stats->level = templevel;
		strcpy(new_guy->stats->name, tempname);
		if (strlen(tempname) > 1)                      //chad 5/25/95
			new_guy->stats->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);
	master->scentextlines = numlines;
	//master->(*scentext) = new char(numlines);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		SDL_RWread(infile, oneline, tempwidth, 1);
		oneline[(int)tempwidth] = 0;
		strcpy(master->scentext[i], oneline);
	}

	//fclose(infile);

	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	master->grid = read_pixie_file(newgrid);
	master->maxx = master->grid[1];
	master->maxy = master->grid[2];
	master->pixmaxx = master->maxx * GRID_SIZE;
	master->pixmaxy = master->maxy * GRID_SIZE;
	master->grid += 3;
	if (!mysmoother)
		mysmoother = new smoother();
	mysmoother->set_target(master);

	// Fix up doors, etc.
	here = master->weaplist;
	while (here)
	{
		if (here->ob && here->ob->query_family()==FAMILY_DOOR)
		{
			if (mysmoother && mysmoother->query_genre_x_y(here->ob->xpos/GRID_SIZE,
			        (here->ob->ypos/GRID_SIZE)-1)==TYPE_WALL)
			{
				here->ob->set_frame(1);  // turn sideways ..
			}
		}
		here = here->next;
	}

	return 1;
} // end load_version_5


// Version 6 includes a 30-byte scenario title after the grid name.
// Also load version 7 and 8 here, since it's a simple change ..
short load_version_6(SDL_RWops  *infile, screen * master, short version)
{
	short currentx, currenty;
	unsigned char temporder, tempfamily;
	unsigned char tempteam;
	char tempfacing, tempcommand;
	char templevel;
	short shortlevel;
	char tempreserved[20];
	short listsize;
	short i;
	walker * new_guy;
	char newgrid[12] = "grid.pix";  // default grid
	char new_scen_type; // read the scenario type
	char oneline[80];
	char numlines, tempwidth;
	char tempname[12];
	oblink *here;
	char scentitle[30];
	memset(scentitle, 0, 30);
	short temp_par;

	// Format of a scenario object list file version 6/7 is:
	// 3-byte header: 'FSS'
	// 1-byte version #
	// ----- (above is already determined by now)
	// 8-byte string = grid name to load
	// 30-byte scenario title (ver 6+)
	// 1-byte char = scenario type, default is 0
	// 2-bytes par-value, v.8+
	// 2-bytes (short) = total objects to follow
	// List of n objects, each of 7-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 2-byte short xpos
	// 2-byte short ypos
	// 1-byte TEAM
	// 1-byte facing
	// 1-byte command
	// 1-byte level // 2 bytes in version 7+
	// 12-bytes name
	// ---
	// 10 bytes reserved
	// 1-byte # of lines of text to load
	// List of n lines of text, each of form:
	// 1-byte character width of line
	// m bytes == characters on this line


	// Get grid file to load
	SDL_RWread(infile, newgrid, 8, 1);
	// Zardus: FIX: make sure they're lowercased
	lowercase((char *)newgrid);
	strcpy(my_map_name, newgrid);

	// Get scenario title, if it exists
	//for (i=0; i < strlen(scentitle); i++)
	//	scentitle[i] = 0;
	SDL_RWread(infile, scentitle, 30, 1);
	strcpy(master->scenario_title, scentitle);

	// Get the scenario type information
	SDL_RWread(infile, &new_scen_type, 1, 1);
	master->scenario_type = new_scen_type;

	if (version >= 8)
	{
		SDL_RWread(infile, &temp_par, 2, 1);
		master->par_value = temp_par;
	}
	// else we're using the value of the level ..

	// Determine number of objects to load ...
	SDL_RWread(infile, &listsize, 2, 1);

	// Now read in the objects one at a time
	for (i=0; i < listsize; i++)
	{
		SDL_RWread(infile, &temporder, 1, 1);
		SDL_RWread(infile, &tempfamily, 1, 1);
		SDL_RWread(infile, &currentx, 2, 1);
		SDL_RWread(infile, &currenty, 2, 1);
		SDL_RWread(infile, &tempteam, 1, 1);
		SDL_RWread(infile, &tempfacing, 1, 1);
		SDL_RWread(infile, &tempcommand, 1, 1);
		if (version >= 7)
			SDL_RWread(infile, &shortlevel, 2, 1);
		else
			SDL_RWread(infile, &templevel, 1, 1);
		SDL_RWread(infile, tempname, 12, 1);
		SDL_RWread(infile, tempreserved, 10, 1);
		if (temporder == ORDER_TREASURE)
			new_guy = master->add_fx_ob(temporder, tempfamily);
		else
			new_guy = master->add_ob(temporder, tempfamily);  // create new object
		if (!new_guy)
		{
			Log("Error creating object! Press Space\n");
			wait_for_key(KEYSTATE_SPACE);
			//fclose(infile);
			return 0;
		}
		new_guy->setxy(currentx, currenty);
		new_guy->team_num = tempteam;
		if (version >= 7)
			new_guy->stats->level = shortlevel;
		else
			new_guy->stats->level = templevel;
		strcpy(new_guy->stats->name, tempname);
		if (strlen(tempname) > 1)                      //chad 5/25/95
			new_guy->stats->set_bit_flags(BIT_NAMED, 1);

	}

	// Now get the lines of text to read ..
	SDL_RWread(infile, &numlines, 1, 1);
	master->scentextlines = numlines;
	//master->(*scentext) = new char(numlines);

	for (i=0; i < numlines; i++)
	{
		SDL_RWread(infile, &tempwidth, 1, 1);
		if(tempwidth > 0)
        {
            SDL_RWread(infile, oneline, tempwidth, 1);
            oneline[(int)tempwidth] = 0;
        }
        else
            oneline[0] = 0;
        strcpy(master->scentext[i], oneline);
	}

	//fclose(infile);

	// Now read the grid file to our master screen ..
	strcat(newgrid, ".pix");
	master->grid = read_pixie_file(newgrid);
	master->maxx = master->grid[1];
	master->maxy = master->grid[2];
	master->pixmaxx = master->maxx * GRID_SIZE;
	master->pixmaxy = master->maxy * GRID_SIZE;
	master->grid += 3;
	if (!mysmoother)
		mysmoother = new smoother();
	mysmoother->set_target(master);

	// Fix up doors, etc.
	here = master->weaplist;
	while (here)
	{
		if (here->ob && here->ob->query_family()==FAMILY_DOOR)
		{
			if (mysmoother && mysmoother->query_genre_x_y(here->ob->xpos/GRID_SIZE,
			        (here->ob->ypos/GRID_SIZE)-1)==TYPE_WALL)
			{
				here->ob->set_frame(1);  // turn sideways ..
			}
		}
		here = here->next;
	}
	return 1;
} // end load_version_5


char  * query_my_map_name()
{
	return my_map_name;
}

// Look for the first non-dead instance of a given walker ..
walker  * screen::first_of(unsigned char whatorder, unsigned char whatfamily,
                           int team_num)
{
	oblink  * here = oblist;

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

	here = oblist;
	while(here)
	{
		if (here->ob &&
		        here->ob->query_order() == ORDER_LIVING &&
		        here->ob->query_act_type() != ACT_CONTROL &&
		        here->ob->team_num == my_team)
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

	here = fxlist;

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

	here = oblist;
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
	if (xover >= maxx || yover >= maxy)
		return 0;

	gridloc = (short) (yover*maxx+xover);

	switch ((unsigned char)grid[gridloc])
	{
		case PIX_GRASS1: // grass
		case PIX_GRASS2:
		case PIX_GRASS3:
		case PIX_GRASS4:
			grid[gridloc] = PIX_GRASS1_DAMAGED;
			break;
		default:
			break;
	}

	return grid[gridloc];
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
