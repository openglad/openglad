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
#include <openglad/runtime/level_runtime_data.h>
#include <openglad/interface/level_visuals.h>
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
		void sync_world_from_save_data();
		void sync_save_data_from_world();
		bool is_level_completed(int level_index) const;
		int get_num_levels_completed(const std::string& campaign) const;
		void add_level_completed(const std::string& campaign, int level_index);
		bool load_level();
		bool save_level();
		LevelRuntimeData::IoError load_level_with_error();
		LevelRuntimeData::IoError save_level_with_error();
		LevelRuntimeData::IoError level_io_error() const { return level_runtime_data_.last_io_error(); }
		std::string& level_grid_file() { return level_runtime_data_.grid_file; }
		const std::string& level_grid_file() const { return level_runtime_data_.grid_file; }
		std::list<std::string>& level_description() { return level_runtime_data_.description; }
		const std::list<std::string>& level_description() const { return level_runtime_data_.description; }
		std::string get_level_description_line(int i) const { return level_runtime_data_.get_description_line(i); }
		void set_level_draw_pos(std::int32_t new_topx, std::int32_t new_topy) { level_runtime_data_.set_draw_pos(new_topx, new_topy); }
		void add_level_draw_pos(std::int32_t dx, std::int32_t dy) { level_runtime_data_.add_draw_pos(dx, dy); }
		void draw_level(screen* scr = nullptr) { level_runtime_data_.draw(scr ? scr : this); }
		LevelRuntimeData& level_runtime_data() { return level_runtime_data_; }
		const LevelRuntimeData& level_runtime_data() const { return level_runtime_data_; }
		GameWorld& world() { return world_; }
		const GameWorld& world() const { return world_; }
		LevelVisuals& level_visuals() { return level_visuals_; }
		const LevelVisuals& level_visuals() const { return level_visuals_; }
		auto& oblist() { return world_.oblist; }
		const auto& oblist() const { return world_.oblist; }
		auto& fxlist() { return world_.fxlist; }
		const auto& fxlist() const { return world_.fxlist; }
		auto& weaplist() { return world_.weaplist; }
		const auto& weaplist() const { return world_.weaplist; }
		auto& dead_list() { return world_.dead_list; }
		const auto& dead_list() const { return world_.dead_list; }
		int& living_count() { return world_.living_count; }
		const int& living_count() const { return world_.living_count; }

		// General drawing data
		std::array<unsigned char, 768> newpalette{};
		short palmode;
		
			// Level data
			GameWorld world_;
			// Forwarders to gameplay-owned game state.
			float& control_hp;
			char& end;
			signed char& timer_wait;
			short& level_done;
			bool& retry;
			Sint32& enemy_freeze;
			// Platform-owned entity loader (wired at setup time).
			loader* myloader;
			LevelVisuals level_visuals_;
			LevelRuntimeData level_runtime_data_;
			
			// Save data
			SaveData save_data;
			
			std::string special_name[NUM_FAMILIES][NUM_SPECIALS];
			std::string alternate_name[NUM_FAMILIES][NUM_SPECIALS];
			std::unique_ptr<soundob> soundp;
		short redrawme;
		std::unique_ptr<viewscreen> viewob[5];
		short numviews;
		Uint32 timerstart;
		Uint32 framecount;

	private:
		void init_common(short howmany, bool has_display);
};
