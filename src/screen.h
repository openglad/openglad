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
#include "loader.h"
#include "obmap.h"
#include "smooth.h"
#include <map>
#include <string>
#include <set>
#include "level_data.h"
#include "save_data.h"


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
		short query_passable(short x,short y,walker  *ob);
		short query_object_passable(short x, short y, walker  *ob);
		short query_grid_passable(short x, short y, walker  *ob);
		short redraw();
		void refresh();
		walker  * first_of(unsigned char whatorder, unsigned char whatfamily,
		                   int team_num = -1);
		short input(const SDL_Event& event);
		short continuous_input();
		short act();
		walker  *add_ob(char order, char family);
		walker  *add_ob(char order, char family, short atstart); // to insert
		walker  *add_ob(walker  *newob); // add an existing walker
		walker  *add_fx_ob(char order, char family);
		walker  *add_fx_ob(walker  *newob); // add an existing walker
		walker  *add_weap_ob(char order, char family);
		walker  *add_weap_ob(walker  *newob); // add an existing weapon
		short remove_ob(walker  *ob, short no_delete); // don't delete us?

		short endgame(short ending);
		short endgame(short ending, short nextlevel); // what level next?
		walker *find_near_foe(walker *ob);
		walker *find_far_foe(walker *ob);
		void draw_panels(short howmany);
		walker  * find_nearest_blood(walker  *who);
		walker* find_nearest_player(walker *ob);
		oblink* find_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob);
		oblink* find_foes_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob);
		oblink* find_friends_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob);
		oblink* find_foe_weapons_in_range(oblink *somelist, Sint32 range, short *howmany, walker  *ob);
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
		short control_hp; // last turn's hitpoints
		char end;
		signed char timer_wait;
		short level_done; // set true when all our foes are dead
		walker * weapfree; //free weapons for re-allocation
		bool retry;  // we should reset the level and go again
		

		char special_name[NUM_FAMILIES][NUM_SPECIALS][20];
		char alternate_name[NUM_FAMILIES][NUM_SPECIALS][20];
		unsigned short enemy_freeze; // stops enemies from acting
		soundob *soundp;
		short redrawme;
		viewscreen  * viewob[5];
		short numviews;
		Uint32 timerstart;
		Uint32 framecount;
};

#endif

