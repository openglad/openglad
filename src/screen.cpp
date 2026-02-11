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
#include "game_context.h"
#include "smooth.h"
#include "util.h"
#include "input.h"
#include "view_sizes.h"
#include "results_screen.h"
#include <algorithm>
#include <string>
#include <cstring>
#include <format>

namespace
{
const char* scenario_title_error_string(screen::ScenarioTitleError err)
{
    switch(err)
    {
        case screen::ScenarioTitleError::None:
            return "none";
        case screen::ScenarioTitleError::OpenReadFailed:
            return "open_read_failed";
        case screen::ScenarioTitleError::InvalidHeader:
            return "invalid_header";
        case screen::ScenarioTitleError::UnsupportedVersion:
            return "unsupported_version";
        case screen::ScenarioTitleError::ReadFailed:
            return "read_failed";
    }
    return "unknown";
}

bool rw_read_exact(SDL_RWops* infile, void* dst, size_t size, size_t count)
{
    return infile != nullptr && SDL_RWread(infile, dst, size, count) == count;
}
} // namespace

static inline cfg_store& active_config()
{
    if(ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}


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

short load_version_2(SDL_RWops  *infile, screen * master);
short load_version_3(SDL_RWops  *infile, screen * master); // v.3 scen
short load_version_4(SDL_RWops  *infile, screen * master); // v.4 scen: + names
short load_version_5(SDL_RWops  *infile, screen * master); // v.5 scen: + type
short load_version_6(SDL_RWops  *infile, screen * master, short version=6); // v.6 scen: + title



Uint32 random(Uint32 x)
{
	if (x < 1)
		return 0;
	return static_cast<Uint32>( (static_cast<Uint32>(rand())) % x);
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
    TRACE("init", "screen constructor: numviews=%d", howmany);
    // Set the global here so objects we construct here can use it
    myscreen = this;
    
	Sint32 i, j;
	text& first_text = text_normal;
	Sint32 left = 66;

	grab_timer();

	timerstart = query_timer_control();
	framecount = 0;

	//  control = nullptr;
	//myradar[0] = myradar[1] = nullptr; // very important! :)
	control_hp = 0;

	// Load the palette ..
	load_and_set_palette("our.pal", newpalette);

	// load the pixie graphics data into memory

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
	
	update_overscan_setting();
	
	palmode = 0;

	end = 0;
	timer_wait = 6;       // 'moderate' speed setting

	redrawme = 1;
	cyclemode = 1; //color cycling on by default
	
	enemy_freeze = 0;

	level_done = 0;
	
	retry = false;

	// Load map data from a pixie format
	// FIXME: This was moved into level_data
	//load_map_data(pixdata);
	first_text.write_xy(left, 78, "Loading Gameplay Info...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 86, "Initializing Display...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);


	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens
	for (i=0; i < MAX_VIEWS; i++)
		viewob[i].reset();
    
    initialize_views();

	first_text.write_xy(left, 86, "Initializing Display...Done", DARK_BLUE, 1);
	first_text.write_xy(left, 94, "Initializing Sound...", DARK_BLUE, 1);
	buffer_to_screen(0, 0, 320, 200);
	

	// Init the sound data
    soundp = std::make_unique<soundob>();
    if(!active_config().is_on("sound", "sound"))
        soundp->set_sound(1);
    first_text.write_xy(left, 94, "Initializing Sound...Done", DARK_BLUE, 1);
    
	buffer_to_screen(0, 0, 320, 200);

	// Let's set the special names for all walkers ..
	for (i=0; i < NUM_FAMILIES; i++)
		for (j=0; j < NUM_SPECIALS; j++)
		{
			special_name[i][j] = "NONE";
			alternate_name[i][j] = "NONE";
		}

	special_name[FAMILY_SOLDIER][1] = "CHARGE";
	special_name[FAMILY_SOLDIER][2] = "BOOMERANG";
	special_name[FAMILY_SOLDIER][3] = "WHIRLWIND";
	special_name[FAMILY_SOLDIER][4] = "DISARM";

	special_name[FAMILY_BARBARIAN][1] = "HURL BOULDER";
	special_name[FAMILY_BARBARIAN][2] = "EXPLODING BOULDER";

	special_name[FAMILY_ELF][1] = "ROCKS";
	special_name[FAMILY_ELF][2] = "BOUNCING ROCKS";
	special_name[FAMILY_ELF][3] = "LOTS OF ROCKS";
	special_name[FAMILY_ELF][4] = "MEGA ROCKS";

	special_name[FAMILY_ARCHER][1] = "FIRE ARROWS";
	special_name[FAMILY_ARCHER][2] = "BARRAGE";
	special_name[FAMILY_ARCHER][3] = "EXPLODING BOLT";

	special_name[FAMILY_MAGE][1] = "TELEPORT";
	alternate_name[FAMILY_MAGE][1] = "TELEPORT MARKER";
	special_name[FAMILY_MAGE][2] = "WARP SPACE";
	special_name[FAMILY_MAGE][3] = "FREEZE TIME";
	special_name[FAMILY_MAGE][4] = "ENERGY WAVE";
	special_name[FAMILY_MAGE][5] = "HEARTBURST";

	special_name[FAMILY_ARCHMAGE][1] = "TELEPORT";
	alternate_name[FAMILY_ARCHMAGE][1] = "TELEPORT MARKER";
	special_name[FAMILY_ARCHMAGE][2] = "HEARTBURST";
	alternate_name[FAMILY_ARCHMAGE][2] = "CHAIN LIGHTNING";
	special_name[FAMILY_ARCHMAGE][3] = "SUMMON IMAGE";
	alternate_name[FAMILY_ARCHMAGE][3] = "SUMMON ELEMENTAL";
	special_name[FAMILY_ARCHMAGE][4] = "MIND CONTROL";
	//alternate_name[FAMILY_ARCHMAGE][4] = "SUMMON ELEMENTAL";


	special_name[FAMILY_CLERIC][1] = "HEAL";
	alternate_name[FAMILY_CLERIC][1] = "MYSTIC MACE";
	special_name[FAMILY_CLERIC][2] = "RAISE UNDEAD";
	alternate_name[FAMILY_CLERIC][2] = "TURN UNDEAD";
	special_name[FAMILY_CLERIC][3] = "RAISE GHOST";
	alternate_name[FAMILY_CLERIC][3] = "TURN UNDEAD";
	special_name[FAMILY_CLERIC][4] = "RESURRECT";

	special_name[FAMILY_DRUID][1] = "GROW TREE";
	special_name[FAMILY_DRUID][2] = "SUMMON FAERIE";
	special_name[FAMILY_DRUID][3] = "REVEAL";
	special_name[FAMILY_DRUID][4] = "PROTECTION";

	special_name[FAMILY_THIEF][1] = "DROP BOMB";
	special_name[FAMILY_THIEF][2] = "CLOAK";
	special_name[FAMILY_THIEF][3] = "TAUNT ENEMY";
	alternate_name[FAMILY_THIEF][3] = "CHARM OPPONENT";
	special_name[FAMILY_THIEF][4] = "POISON CLOUD";

	special_name[FAMILY_GHOST][1] = "SCARE";

	special_name[FAMILY_FIREELEMENTAL][1] = "STARBURST";

	special_name[FAMILY_ORC][1] = "HOWL";
	special_name[FAMILY_ORC][2] = "EAT CORPSE";

	special_name[FAMILY_SMALL_SLIME][1] = "GROW";

	special_name[FAMILY_MEDIUM_SLIME][1] = "GROW";

	special_name[FAMILY_SLIME][1] = "SPLIT";

	special_name[FAMILY_SKELETON][1] = "TUNNEL";

}

screen::~screen()
{
	release_timer();
	soundp.reset();
	cleanup(1); //make sure we've cleaned up
}

void screen::initialize_views()
{
    // Even though it looks okay here, these positions and sizes are overridden by viewscreen::resize() later.
	if (numviews == 1)
	{
		viewob[0] = std::make_unique<viewscreen>( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0);
	}
	else if (numviews == 2)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT, 1);
	}
	else if (numviews == 3)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
		viewob[2] = std::make_unique<viewscreen>( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
	}
	else if (numviews == 4)
	{
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT, 0);
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT, 1);
		viewob[2] = std::make_unique<viewscreen>( T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT, 2);
		viewob[3] = std::make_unique<viewscreen>( T_LEFT_FOUR, T_UP_FOUR, T_HALF_WIDTH, T_HALF_HEIGHT, 3);
	}
	else
    {
        LogError("screen_init_views_failed numviews={}\n", numviews);
    }
}

void screen::cleanup(short howmany)
{
	Sint32 i;

    numviews = howmany; // # of viewscreens
    for (i=0; i < MAX_VIEWS; i++)
    {
        viewob[i].reset();
    }
}

void screen::ready_for_battle(short howmany)
{
	// Set up the viewscreen poshorters
	numviews = howmany; // # of viewscreens

	// Clean stuff up
	cleanup(howmany);
    
    initialize_views();

	end = 0;
	
	retry = false;

	redrawme = 1;

	timerstart = query_timer_control();
	framecount = 0;
	enemy_freeze = 0;

	control_hp = 0;

	palmode = 0;

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
		viewob[0] = std::make_unique<viewscreen>( S_LEFT, S_UP, S_WIDTH, S_HEIGHT, 0);
	}
	else if (numviews == 2)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
	}
	else if (numviews == 3)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
		viewob[2] = std::make_unique<viewscreen>( 112, 16, 100, 168, 2);
	}
	else if (numviews == 4)
	{
		viewob[1] = std::make_unique<viewscreen>( T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT, 1);
		viewob[0] = std::make_unique<viewscreen>( T_LEFT_TWO, T_UP_TWO, T_WIDTH, T_HEIGHT, 0);
		viewob[2] = std::make_unique<viewscreen>( 112, 16, 100, 168, 2);
		viewob[3] = std::make_unique<viewscreen>( 112, 16, 100, 168, 3);
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

bool screen::query_grid_passable(float x, float y, walker  *ob)
{
	Sint32 i,j;
	//  short xsize=ob->sizex, ysize=ob->sizey;
	Sint32 xtrax = 1;
	Sint32 xtray = 1;
	Sint32 xtarg; //the for loop target
	Sint32 ytarg; //the for loop target
	Sint32 dist;
	// NOTE: we're going to shrink dimensions by one in each..
	//Sint32 xover = static_cast<Sint32>(x+ob->sizex-1), yover = static_cast<Sint32>(y+ob->sizey-1);
	Sint32 xover = x+ob->sizex, yover = y+ob->sizey;

	// Again, this is for shrinking ...
	//x+=1;
	//y+=1;

	if (x < 0 || y < 0 || xover >= level_data.pixmaxx || yover >= level_data.pixmaxy)
		return 0;

	// Are we ethereal?
	if (ob->stats()->query_bit_flags(BIT_ETHEREAL) )
		return 1; //moved up to avoid unneeded calculation

	// Zardus: PORT: Does the grid exist?
	if (!level_data.grid.valid())
		return 0;

	// Check if our butt hangs over into next grid square
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
			switch (static_cast<unsigned char>(level_data.grid.data[i+level_data.grid.w*j]))
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
					if (ob->stats()->query_bit_flags(BIT_FORESTWALK) )
						break;
					else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
						break;
					else
						return 0;
				case PIX_TREE_B1:  // Tree bottoms
					{
						if (ob->query_order() == Order::Weapon
						        || ob->stats()->query_bit_flags(BIT_FORESTWALK) )
							break;
						else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
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
						if (ob->query_order()==Order::Living)
							return 0;

						if (abs(ob->xpos - ob->owner->xpos)>
						        abs(ob->ypos - ob->owner->ypos))
							dist = abs(ob->xpos - ob->owner->xpos);
						else
							dist = abs(ob->ypos - ob->owner->ypos);
						dist -= (GRID_SIZE/2);
						if (dist < GRID_SIZE)
							dist += GRID_SIZE;
						if (ctx().rng->next(dist/GRID_SIZE))
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
						if (ob->query_order() == Order::Weapon)
							break;
						else if (ob->stats()->query_bit_flags(BIT_FLYING) || ob->flight_left)
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

bool screen::query_object_passable(float x, float y, walker  *ob)
{
	if (ob->dead)
		return 1;
	return level_data.myobmap->query_list(ob, x, y);
}

bool screen::query_passable(float x, float y, walker  *ob)
{
	return query_grid_passable(x, y, ob) && query_object_passable(x, y, ob);
}

void screen::clear()
{
	unsigned short i;

	//buffers: PORT:  for (i=0;i<64000;i++)
	//buffers: PORT:  {
	//buffers: PORT:         videobuffer[i] = 0;
	//buffers: PORT:  }
	clearbuffer();
	//SDL_FillRect(screen,nullptr,SDL_MapRGB(screen->format,0,0,0));

	for (i=0; i < numviews; i ++)
		viewob[i]->clear();
}

// REDRAW -- This function moves through the data on the grid (map)
//           finding which grid squares are on screen.  For each on
//           screen, it pashorts the appropriate graphics pixie onto
//           the screen by calling the function DRAW in PIXIE.
bool screen::redraw()
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

bool screen::act()
{
	Sint32 printed_time = 0; // have we printed message yet?
	//  static short debug = 0;

	level_done = 2; // unless we find valid foes while looping

	if (enemy_freeze)
		enemy_freeze--;
	if (enemy_freeze == 1)
		set_palette(ourpalette);

    for(auto& uptr : level_data.oblist)
    {
        walker* ob = uptr.get();
		if (!enemy_freeze) // normal functionality
		{
			if (ob && !ob->dead)
			{
				ob->in_act = true; // Zardus: while acting, in_act is set
				ob->act();
				ob->in_act = false;
				if (ob && !ob->dead)
				{
					if (!ob->is_friendly_to_team(save_data.my_team) &&
					        ob->query_order() == Order::Living)
						level_done = 0;
					// Testing .. trying to FORCE foes :)
					if (ob->foe == nullptr && ob->leader == nullptr)
						ob->foe = find_far_foe(ob);
				}
			}
		}
		else // enemy livings are frozen
		{
			if (!(enemy_freeze%10) && !printed_time)
			{
				std::string obmessage = std::format("TIME LEFT: {}", enemy_freeze);
				viewob[0]->set_display_text(obmessage.c_str(), 10);
				printed_time = 1;
			}
			if (ob && !ob->dead &&
			        ( (    (ob->query_order() != Order::Living)
			               && (ob->query_order() != Order::Generator)
			          ) || (ob->team_num == 0) )
			   )
			{
				ob->act();
				if (ob && !ob->dead)
				{
					if (!ob->is_friendly_to_team(save_data.my_team) &&
					        ob->query_order() == Order::Living)
						level_done = 0;
				}
			}
		}

	}

	// Let the weapons act ...
	for(auto& uptr : level_data.weaplist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead)
		{
			ob->act();
			if (ob && !ob->dead)
			{
				if (!ob->is_friendly_to_team(save_data.my_team) &&
				        ob->query_order() == Order::Living)
					level_done = 0;
			}
		}
	}  // end of weapons acting

	// Quickly check the background for exits, etc.
	for(auto& uptr : level_data.fxlist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead)
		{
			if (ob->query_order() == Order::Treasure &&
			        ob->query_family() == FAMILY_EXIT &&
			        level_done != 0)
			{
				level_done = 1; // 0 => foes, 1 => no foes but exit, 2 => no foes or exit
			}
		}
	}

	if (level_done == 2)
		return endgame(0, level_data.id + 1);  // No exits and no enemies: Go to next sequential level.
    
    if(end)
        return 1;
    
	// Make sure we're all pointing to legal targets
	for(auto& uptr : level_data.oblist)
	{
	    walker* ob = uptr.get();
        if (ob->foe && ob->foe->dead)
            ob->foe = nullptr;
        if (ob->leader && ob->leader->dead)
            ob->leader = nullptr;
        if (ob->owner && ob->owner->dead)
            ob->owner = nullptr;
        if (ob->collide_ob && ob->collide_ob->dead)
            ob->collide_ob = nullptr;
	}

	for(auto& uptr : level_data.weaplist)
	{
	    walker* ob = uptr.get();
        if (ob->foe && ob->foe->dead)
            ob->foe = nullptr;
        if (ob->leader && ob->leader->dead)
            ob->leader = nullptr;
        if (ob->owner && ob->owner->dead)
            ob->owner = nullptr;
        if (ob->collide_ob && ob->collide_ob->dead)
            ob->collide_ob = nullptr;
	}


	// Remove dead objects
	for(auto e = level_data.oblist.begin(); e != level_data.oblist.end();)
	{
	    walker* ob = e->get();
		if (ob && ob->dead && ob->myguy == nullptr)
		{
		    // Delete the dead thing safely

			// Is it a player?
			if(ob->user != -1)
			{
			    // Remove it from its viewscreen
			    for(int i = 0; i < numviews; i++)
			    {
			        if(ob == viewob[i]->control)
                        viewob[i]->control = nullptr;
			    }
			}

			// Save dead guys to be deleted later.  Delete everything else right now.  This is so the "owner" of weapons remains valid.
            level_data.dead_list.push_back(std::move(*e));

            //level_data.remove_ob(ob);
            // Remove from the list directly here so we can preserve our iterator
			if(ob->query_order() == Order::Living)
                level_data.numobs--;

            e = level_data.oblist.erase(e);
            continue;
		}

		e++;
	}

	std::erase_if(level_data.fxlist, [](const auto& uptr) {
		walker* ob = uptr.get();
		return ob && ob->dead;
	});

	std::erase_if(level_data.weaplist, [](const auto& uptr) {
		walker* ob = uptr.get();
		return ob && ob->dead;
	});

	return 1;
}

Uint32 get_time_bonus(int playernum);

short screen::endgame(short ending)
{
	return endgame(ending, -1);
}

short screen::endgame(short ending, short nextlevel)
{
    if(end)
        return 1;
	
	
	std::map<int, guy*> before;
	std::map<int, walker*> after;
	
	// Get guys from before battle
	for(int i = 0; i < save_data.team_size; i++)
    {
        if(save_data.team_list[i] != nullptr)
            before.insert(std::make_pair(save_data.team_list[i]->id, save_data.team_list[i].get()));
    }
	
    // Get guys from the battle
    for(auto& uptr : level_data.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && ob->myguy)
			after.insert(std::make_pair(ob->myguy->id, ob));
	}
	
	// Let's show the results!
    retry = results_screen(ending, nextlevel, before, after);
    
    if(retry)
    {
        // Retry without updating the roster and saving the game
        end = 1;
        return 1;
    }
    
	if (ending == 1)  // 1 = lose, for some reason
	{
		if (nextlevel == -1) // generic defeat
		{
			end = 1;
		}
		else // we're withdrawing to another level
		{
			end = 1;
		}
	}
	else if (ending == SCEN_TYPE_SAVE_ALL) // failed to save a guy
	{
		end = 1;
	}
	else if (ending == 0) // we won
	{
        Uint32 bonuscash[4] = {0, 0, 0, 0};
        Uint32 allbonuscash = 0;
        
		// Update all the money!
		for (int i=0; i < 4; i++)
		{
			save_data.m_totalscore[i] += save_data.m_score[i];
			save_data.m_totalcash[i] += (save_data.m_score[i]*2);
		}
		for (int i=0; i < 4; i++)
		{
            bonuscash[i] = get_time_bonus(i);
			save_data.m_totalcash[i] += bonuscash[i];
			allbonuscash += bonuscash[i];
		}
		if (save_data.is_level_completed(save_data.scen_num)) // already won, no bonus
		{
			for (int i=0; i < 4; i++)
				bonuscash[i] = 0;
			allbonuscash = 0;
		}
	    
		// Beat that level
		save_data.add_level_completed(save_data.current_campaign, save_data.scen_num); // this scenario is completed ..
		if (nextlevel != -1)
			save_data.scen_num = nextlevel;    // Fake jumping to next level ..
        
        // Grab our team out of the level
        save_data.update_guys(level_data.oblist);
        
        // Autosave because we won
		save_data.save("save0");

		end = 1;
	}

    
	return 1;
}

walker *screen::find_near_foe(walker  *ob)
{
	short targx, targy;
	short spread=1,xchange=0;
	short loop=0;
	short resolution = level_data.myobmap->obmapres;

	if (!ob)
	{
		Log("no ob in find near foe.\n");
		return nullptr;
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

			std::list<walker*>& ls = level_data.myobmap->obmap_get_list(targx,targy);
			for(auto* w : ls) //go through the list we received
			{
				if (!(w->dead) && (ob->is_friendly(w)==0)  &&
				        (ctx().rng->next(w->invisibility_left/20)==0)
				   )
				{
					if (w->query_order() == Order::Living ||
					        w->query_order() == Order::Generator)
						//done separately since they are logically more significant
						return w; // this should be a valid foe
				}
			}//end inner while

		}//end for
		xchange++; //change whether we do x or y in each for loop
		if (!(xchange%2))
		{
			resolution = static_cast<short>(-resolution); // reverse direction around the search every other for
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
	walker  *endfoe;

	if (!ob)
	{
		Log("no ob in find far foe.\n");
		return nullptr;
	}

	// Get our current coordinates
	//targx = ob->xpos;
	//targy = ob->ypos;

	// Set our 'default' foe to nullptr
	endfoe = nullptr;
	distance = 10000;
	ob->stats()->last_distance = 10000;

    for(auto& uptr : level_data.oblist)
	{
	    walker* foe = uptr.get();
		if (foe == nullptr || foe->dead)
			continue;
        
		// Check for valid objects ..
		if (ob->is_friendly(foe) == 0)
		{
			if (
			    (foe->query_order() == Order::Living ||
			     foe->query_order() == Order::Generator)  &&
			    (!(ctx().rng->next(foe->invisibility_left/20)))
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
	}
	return endfoe;
}

walker* screen::set_walker(walker *ob, Order order, char family)
{
    return level_data.myloader->set_walker(ob, order, family);
}

screen::ScenarioTitleError screen::get_scen_title_with_error(const char *filename, std::string& out_title)
{
    out_title = "none";
    if(filename == nullptr || filename[0] == '\0')
        return ScenarioTitleError::OpenReadFailed;

    SDL_RWops  *infile = nullptr;
    char temptext[4] = {};
    char versionnumber = 0;
    char gridname[8] = {};
    char buffer[31] = {};

    std::string tempfile = std::string(filename) + ".fss";

    // Zardus: first get the file from scen/
    infile = open_read_file("scen/", tempfile.c_str());
    if(infile == nullptr)
        return ScenarioTitleError::OpenReadFailed;

    ScenarioTitleError err = ScenarioTitleError::None;
    if(!rw_read_exact(infile, temptext, 1, 3))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }
    if (std::string(temptext, 3) != "FSS")
    {
        err = ScenarioTitleError::InvalidHeader;
        goto close_and_return;
    }

    if(!rw_read_exact(infile, &versionnumber, 1, 1))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }
    if (versionnumber < 6)
    {
        err = ScenarioTitleError::UnsupportedVersion;
        goto close_and_return;
    }

    // Discard the grid name ...
    if(!rw_read_exact(infile, gridname, 1, 8))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }

    // Return the title, 30 bytes
    if(!rw_read_exact(infile, buffer, 1, 30))
    {
        err = ScenarioTitleError::ReadFailed;
        goto close_and_return;
    }

    out_title = std::string(buffer);

close_and_return:
    if(infile != nullptr)
        SDL_RWclose(infile);

    if(err != ScenarioTitleError::None)
    {
        Log("scenario_title_load failed: filename={} error={}\n",
            filename ? filename : "(null)", scenario_title_error_string(err));
    }
    return err;
}

const char* screen::get_scen_title(const char *filename, screen *master)
{
    (void)master;
    static char buffer[31] = {};
    std::string out_title;
    const ScenarioTitleError err = get_scen_title_with_error(filename, out_title);
    if(err != ScenarioTitleError::None)
        out_title = "none";
    std::snprintf(buffer, sizeof(buffer), "%s", out_title.c_str());
    return buffer;

}


// Look for the first non-dead instance of a given walker ..
walker  * screen::first_of(Order whatorder, unsigned char whatfamily,
                           int team_num)
{
	for(auto& uptr : level_data.oblist)
	{
	    walker* ob = uptr.get();
		if (ob && !ob->dead)
		{
			if (ob->query_order() == whatorder &&
			        ob->query_family()== whatfamily)
			{
				if (team_num == -1 || team_num == ob->team_num)
					return ob;
			}
		}
	}
	return nullptr;
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
	Sint32 distance, newdistance;
	walker  *returnob = nullptr;

	if (!who)
		return nullptr;

	distance = 800;

	for(auto& uptr : level_data.fxlist)
	{
	    walker* w = uptr.get();
		if (w && w->query_order() == Order::Treasure &&
		        w->query_family() == FAMILY_STAIN && !w->dead)
		{
			newdistance = static_cast<Uint32>(who->distance_to_ob_center(w));
			if (newdistance < distance)
			{
				distance = newdistance;
				returnob = w;
			}
		}
	}
	return returnob;

}

std::list<walker*> screen::find_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, short *howmany, walker  *ob)
{
	//short obx, oby;
    std::list<walker*> result;

	*howmany = 0;

	if(!ob)
		return result;

	//obx = static_cast<short>(ob->xpos + (ob->sizex/2) );  // center of object
	//oby = static_cast<short>(ob->ypos + (ob->sizey/2) );

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead)
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}

walker* screen::find_nearest_player(walker *ob)
{
	walker *returnob = nullptr;
	Uint32 distance = 32000;
	Uint32 tempdistance;

	if (!ob)
		return nullptr;

	for(auto& uptr : level_data.oblist)
	{
	    walker* w = uptr.get();
		if (w && (w->user != -1) )
		{
			tempdistance = ob->distance_to_ob(w);
			if (tempdistance < distance)
			{
				distance = tempdistance;
				returnob = w;
			}
		}
	}

	return returnob;
}

std::list<walker*> screen::find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, short *howmany, walker  *ob)
{
    std::list<walker*> result;
    *howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead &&
		        (w->query_order() == Order::Living ||
		         w->query_order() == Order::Generator)
		        && (ob->is_friendly(w) == 0)
		   )
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}

std::list<walker*> screen::find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range,
                                      short *howmany, walker  *ob)
{
    std::list<walker*> result;
    *howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead && w->query_order() == Order::Living
		        && ( ob->is_friendly(w) )
		   )
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}

std::list<walker*> screen::find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, short *howmany, walker  *ob)
{
    std::list<walker*> result;
    *howmany = 0;

	if(!ob)
		return result;

	for(auto& uptr : somelist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead &&
		        (w->query_order() == Order::Weapon)
		        && ( ob->is_friendly(w) )
		   )
		{
			if (ob->distance_to_ob(w) <= range)
			{
			    result.push_back(w);
				(*howmany)++;
			}
		}
	}

	return result;
}


// Uses pixel coordinates
char screen::damage_tile(short xloc, short yloc) // damage the specified tile
{
	short xover, yover;
	short gridloc;

	xover = static_cast<short>(xloc / GRID_SIZE);
	yover = static_cast<short>(yloc / GRID_SIZE);

	if (xover < 0 || yover < 0)
		return 0;
	if (xover >= level_data.grid.w || yover >= level_data.grid.h)
		return 0;

	gridloc = static_cast<short>(yover*level_data.grid.w+xover);

	switch (static_cast<unsigned char>(level_data.grid.data[gridloc]))
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

void screen::do_notify(std::string_view message, walker  *who)
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
	// Zardus: PORT: Undeclared because of problems above:  regs.x.eax = 0x00000500;
	// Same here:  memset( &sregs, 0, sizeof(sregs) );

	// See two lines up:  sregs.es = FP_SEG( &Memory );
	// See three lines up:  regs.x.edi = FP_OFF( &Memory );

	// See two lines up: (plus sounds like a dos thing):  int386x( DPMI_INT, &regs, &regs, &sregs );

	// Them:
	//sprintf(memreport, "Largest Block: %lu bytes",
	//  Memory.LargestBlockAvail);
	//viewob[0]->set_display_text(memreport, STANDARD_TEXT_TIME);
	std::string memreport = std::format("Free Linear address: {} pages",
	        Memory.FreeLinAddrSpace);
	//  Log(memreport);
	//  Log("\n");
	viewob[0]->set_display_text(memreport.c_str(), 25);
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
