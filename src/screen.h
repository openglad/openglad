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
#ifndef __SCREEN_H
#define __SCREEN_H

// Definition of SCREEN class

#include "base.h"
#include "video.h"
#include "gloader.h"
#include "obmap.h"
#include "smooth.h"
#include <map>
#include <string>
#include <set>
#include <vector>
#include "level_data.h"
#include "save_data.h"

#include "text.h"

class screen : public video
{
	public:
		screen();  // called with '1' for numviews
		screen(short howmany);

		void reset(short howmany);
		void ready_for_battle(short howmany);
		virtual ~screen();
		void initialize_views();
		void cleanup(short);
		void clear();
		video * get_video_ob();
		// quiet = true is a question that changes nothing: the same answer with
		// no draw and no side effect (obmap::query_list's quiet; the grid half
		// skips the arrow slit's shooter-distance roll and treats the slit as
		// passable, the loud answer at the one spot a quiet weapon probe is
		// ever made, a weapon's pad one pixel outside its shooter). Loud by
		// default. (port of the current game's 15053f4a)
		bool query_passable(float x, float y, walker  *ob, bool quiet = false);
		bool query_object_passable(float x, float y, walker  *ob, bool quiet = false);
		bool query_grid_passable(float x, float y, walker  *ob, bool quiet = false);
		short redraw();
		void refresh();
		walker  * first_of(unsigned char whatorder, unsigned char whatfamily,
		                   int team_num = -1);
		short input(const SDL_Event& event);
		short continuous_input();
		short act();

		short endgame(short ending);
		short endgame(short ending, short nextlevel); // what level next?
		walker *find_near_foe(walker *ob);
		walker *find_far_foe(walker *ob);
		void draw_panels(short howmany);
		walker* find_nearest_blood(walker *who);
		// Every stain find_nearest_blood would consider (live FAMILY_STAIN
		// treasure, squared centre distance under 800), nearest first; ties
		// keep fxlist order. Draw-free; empty for a NULL who. (port of the
		// current game's 15053f4a)
		std::vector<walker*> find_blood_in_reach(walker *who);
		// Does a body of this order and family fit at (x, y)? Probed with a
		// detached walker (never in a list or the obmap, no draw) through the
		// current game's eat-free spawn placement rule: the grid with the
		// body's own flags, then livings, generators, doors, trees and
		// boulders on exact boxes. (port of the current game's 15053f4a)
		bool body_spot_clear(char order, char family, short x, short y);
		// The first spot within `radius` px of (x, y) where the body fits, in
		// a fixed order: the corner itself, then for r = 1 .. radius the eight
		// offsets (-r,0) (r,0) (0,-r) (0,r) (-r,-r) (r,-r) (-r,r) (r,r).
		// Deterministic, draw-free.
		bool body_spot_near(char order, char family, short x, short y,
		                    int radius, short *out_x, short *out_y);
		walker* find_nearest_player(walker *ob);
		std::list<walker*> find_in_range(std::list<walker*>& somelist, Sint32 range, short *howmany, walker  *ob);
		std::list<walker*> find_foes_in_range(std::list<walker*>& somelist, Sint32 range, short *howmany, walker  *ob);
		std::list<walker*> find_friends_in_range(std::list<walker*>& somelist, Sint32 range, short *howmany, walker  *ob);
		std::list<walker*> find_foe_weapons_in_range(std::list<walker*>& somelist, Sint32 range, short *howmany, walker  *ob);
		char damage_tile(short xloc, short yloc); // damage the specified tile
		void do_notify(const char *message, walker  *who);  // printing text
		void report_mem();
		walker *set_walker(walker *ob, char order, char family);
		const char* get_scen_title(const char *filename, screen *master);
		bool is_level_completed(int level_index) const;
		int get_num_levels_completed(const std::string& campaign) const;
		void add_level_completed(const std::string& campaign, int level_index);

        // General drawing data
		unsigned char newpalette[768];
		short palmode;
		
		// Level data
		LevelData level_data;
		
		// Save data
		SaveData save_data;
		
		
		// Game state
		float control_hp; // last turn's hitpoints
		char end;
		signed char timer_wait;
		short level_done; // set true when all our foes are dead
		bool retry;  // we should reset the level and go again
		

		char special_name[NUM_FAMILIES][NUM_SPECIALS][20];
		char alternate_name[NUM_FAMILIES][NUM_SPECIALS][20];
		unsigned short enemy_freeze; // stops enemies from acting
		short gameplay_fixes; // port of the current game's 0721756c: the Gameplay Fixes setting, 0 = classic; the golden capture tool sets it per row
		soundob *soundp;
		short redrawme;
		viewscreen  * viewob[5];
		short numviews;
		Uint32 timerstart;
		Uint32 framecount;
};


#endif

