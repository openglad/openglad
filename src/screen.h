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

// Goddamned function doesn't belong in screen, much less as a C style extern!
//   I'll fix this eventually, if I ever get hold of that part of the code.
short load_scenario(const char * filename, screen * master);

class screen : public video
{
	public:
		screen();  // called with '1' for numviews
		screen(short howmany);

		void reset(short howmany);
		virtual ~screen();
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
		walker  *get_new_control();
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
		inline walker *set_walker(walker *ob, char order, char family)
		{
			return myloader->set_walker(ob, order, family);
		}
		const char* get_scen_title(const char *filename, screen *master);

		char scenario_type; // 0 is default
		char special_name[NUM_FAMILIES][NUM_SPECIALS][20];
		char alternate_name[NUM_FAMILIES][NUM_SPECIALS][20];
		unsigned short enemy_freeze; // stops enemies from acting
		soundob *soundp;
		short redrawme;
		bool is_level_completed(int level_index) const;
		int get_num_levels_completed(const std::string& campaign) const;
		void add_level_completed(const std::string& campaign, int level_index);
		std::map<std::string, std::set<int> > completed_levels;
		std::map<std::string, int> current_levels;
		char scentext[80][80];                         // Array to hold scenario information
		char scentextlines;                    // How many lines of text in scenario info

		unsigned char newpalette[768];


		short palmode;
		short my_team;
		guy  *first_guy;
		PixieData grid;
		Sint32 pixmaxx,pixmaxy;
		Sint32 topx, topy;
		short control_hp; // last turn's hitpoints
		oblink  *oblist;
		oblink  *fxlist;  // fx--explosions, etc.
		oblink  *weaplist;  // weapons
		loader * myloader;
		char end;
		pixieN  *back[PIX_MAX];
		Sint32 numobs;
		obmap  *myobmap;
		signed char timer_wait;
        char current_campaign[41];
		short scen_num;
		Uint32 score;
		Uint32 m_score[4];
		Uint32 totalcash;
		Uint32 m_totalcash[4];
		Uint32 totalscore;
		Uint32 m_totalscore[4];
		viewscreen  * viewob[5];
		short numviews;
		Uint32 timerstart;
		Uint32 framecount;
		walker * weapfree; //free weapons for re-allocation
		char scenario_title[30]; // used in scenarios v. 6+
		short allied_mode;
		short par_value;
		short level_done; // set true when all our foes are dead
		
		PixieData pixdata[PIX_MAX];
		
		smoother mysmoother;
};

#endif

