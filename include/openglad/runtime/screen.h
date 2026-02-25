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
#pragma once

// Definition of SCREEN class

#include <openglad/legacy/base.h> // NUM_FAMILIES/NUM_SPECIALS + legacy globals (transitional)
#include <openglad/platform/soundob_sdl.h> // soundob class for soundp member
#include <openglad/render/video.h>
#include <openglad/data/gloader.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <array>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

struct InputState;

class screen : public video
{
	public:
		enum class ScenarioTitleError
		{
			None = 0,
			OpenReadFailed,
			InvalidHeader,
			UnsupportedVersion,
			ReadFailed
		};

		screen();  // called with '1' for numviews
		screen(short howmany);
		screen(short howmany, bool create_display);

		void reset(short howmany);
		void ready_for_battle(short howmany);
		~screen() override;
		screen(const screen&) = delete;
		screen& operator=(const screen&) = delete;
		screen(screen&&) = delete;
		screen& operator=(screen&&) = delete;
		void initialize_views();
		void cleanup(short);
		void clear();
		bool query_passable(float x, float y, walker  *ob);
		bool query_object_passable(float x, float y, walker  *ob);
		bool query_grid_passable(float x, float y, walker  *ob);
		// Overloads to avoid implicit int->float conversions at call sites.
		bool query_passable(Sint32 x, Sint32 y, walker* ob) { return query_passable(static_cast<float>(x), static_cast<float>(y), ob); }
		bool query_object_passable(Sint32 x, Sint32 y, walker* ob) { return query_object_passable(static_cast<float>(x), static_cast<float>(y), ob); }
		bool query_grid_passable(Sint32 x, Sint32 y, walker* ob) { return query_grid_passable(static_cast<float>(x), static_cast<float>(y), ob); }
		bool redraw();
		void refresh();
		walker  * first_of(Order whatorder, unsigned char whatfamily,
		                   int team_num = -1);
		short input(const SDL_Event& event);
		short continuous_input();
		void process_input(const InputState& input_state);
		bool act();

		short endgame(short ending);
		short endgame(short ending, short nextlevel); // what level next?
		walker *find_near_foe(walker *ob);
		walker *find_far_foe(walker *ob);
		void draw_panels(short howmany);
		walker* find_nearest_blood(walker *who);
		walker* find_nearest_player(walker *ob);
			std::list<walker*> find_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
			std::list<walker*> find_foes_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
			std::list<walker*> find_friends_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
			std::list<walker*> find_foe_weapons_in_range(std::list<std::unique_ptr<walker>>& somelist, Sint32 range, Sint32* howmany, walker* ob);
		char damage_tile(short xloc, short yloc); // damage the specified tile
		void do_notify(std::string_view message, walker  *who);  // printing text
		void report_mem();
		walker *set_walker(walker *ob, Order order, Sint32 family);
		ScenarioTitleError get_scen_title_with_error(const char *filename, std::string& out_title);
		const char* get_scen_title(const char *filename, screen *master);
		bool is_level_completed(int level_index) const;
		int get_num_levels_completed(const std::string& campaign) const;
		void add_level_completed(const std::string& campaign, int level_index);

		// GameWorld accessor
		og::gameplay::GameWorld& world() { return world_; }
		const og::gameplay::GameWorld& world() const { return world_; }

        // General drawing data
		std::array<unsigned char, 768> newpalette{};
		short palmode;

		// Game world — owns entity lists. Must be declared before level_data
		// so it is constructed first and destroyed last.
		og::gameplay::GameWorld world_;

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


		std::string special_name[NUM_FAMILIES][NUM_SPECIALS];
		std::string alternate_name[NUM_FAMILIES][NUM_SPECIALS];
		Sint32 enemy_freeze; // stops enemies from acting
		std::unique_ptr<soundob> soundp;
		short redrawme;
		std::unique_ptr<viewscreen> viewob[5];
		short numviews;
		Uint32 timerstart;
		Uint32 framecount;

	private:
		void init_common(short howmany, bool has_display);
};
