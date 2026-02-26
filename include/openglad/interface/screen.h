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
#include <openglad/interface/render/video_base.h>
#include <openglad/interface/render/text.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/level_io.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/save_io.h>
#include <openglad/interface/level_visuals.h>
#include <array>
#include <list>
#include <map>
#include <memory>
#include <span>
#include <set>
#include <string>
#include <string_view>

struct InputState;
class video;
struct SDL_Rect;
struct SDL_Surface;
union SDL_Event;

class screen : public og::render::VideoBase
{
	private:
		std::unique_ptr<video> video_;

	public:
		enum class ScenarioTitleError
		{
			None = 0,
			OpenReadFailed,
			InvalidHeader,
			UnsupportedVersion,
			ReadFailed
		};

		screen(short howmany, og::gameplay::GameWorld* world, bool create_display = true);

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
		void set_draw_pos(std::int32_t new_topx, std::int32_t new_topy);
		void add_draw_pos(std::int32_t dx, std::int32_t dy);
		void draw_level_data(og::gameplay::GameWorld* world);
		void reload_level_visuals();
		bool load_level();
		bool save_level();
		std::string get_description_line(int i) const;
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
		void rewire_entity_factory_for_tests();

		// Render interface split: screen depends on abstract interface and
		// forwards to platform SDL implementation.
		void set_fullscreen(bool fullscreen);
		void clearbuffer() override;
		void clearbuffer(int x, int y, int w, int h) override;
		void clear_window();
		std::span<unsigned char> getbuffer() override;
		void putblack(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize);
		void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color);
		void fastbox(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color, unsigned char flag);
		void fastbox_outline(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize, unsigned char color);
		void point(Sint32 x, Sint32 y, unsigned char color) override;
		void pointb(Sint32 x, Sint32 y, unsigned char color) override;
		void pointb(Sint32 x, Sint32 y, unsigned char color, unsigned char alpha);
		void pointb(int offset, unsigned char color);
		void pointb(Sint32 x, Sint32 y, int r, int g, int b);
		void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color);
		void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color);
		void hor_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer);
		void hor_line_alpha(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Uint8 alpha);
		void ver_line(Sint32 x, Sint32 y, Sint32 length, unsigned char color, Sint32 tobuffer);
		void draw_line(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color);
		void do_cycle(Sint32 curmode, Sint32 maxmode);
		void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		             std::span<const unsigned char> sourcedata);
		void putdata_alpha(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		             std::span<const unsigned char> sourcedata, unsigned char alpha);
		void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		                 std::span<const unsigned char> sourcedata);
		void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		             std::span<const unsigned char> sourcedata, unsigned char color);
		void putdatatext(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
		                 std::span<const unsigned char> sourcedata, unsigned char color);
		void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
		               Sint32 tilewidth, Sint32 tileheight,
		               Sint32 portstartx, Sint32 portstarty,
		               Sint32 portendx, Sint32 portendy,
		               std::span<const unsigned char> sourceptr);
		void putbuffer_alpha(Sint32 tilestartx, Sint32 tilestarty,
		               Sint32 tilewidth, Sint32 tileheight,
		               Sint32 portstartx, Sint32 portstarty,
		               Sint32 portendx, Sint32 portendy,
		               std::span<const unsigned char> sourceptr, unsigned char alpha);
		void putbuffer(Sint32 tilestartx, Sint32 tilestarty,
		               Sint32 tilewidth, Sint32 tileheight,
		               Sint32 portstartx, Sint32 portstarty,
		               Sint32 portendx, Sint32 portendy,
		               SDL_Surface *sourceptr);
		void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   std::span<const unsigned char> sourceptr, unsigned char teamcolor);
		void walkputbuffer_flash(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   std::span<const unsigned char> sourceptr, unsigned char teamcolor);
		void walkputbuffertext(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   std::span<const unsigned char> sourceptr, unsigned char teamcolor);
		void walkputbuffertext_alpha(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   std::span<const unsigned char> sourceptr, unsigned char teamcolor, Uint8 alpha);
		void walkputbuffer(Sint32 walkerstartx, Sint32 walkerstarty,
		                   Sint32 walkerwidth, Sint32 walkerheight,
		                   Sint32 portstartx, Sint32 portstarty,
		                   Sint32 portendx, Sint32 portendy,
		                   std::span<const unsigned char> sourceptr, unsigned char teamcolor,
		                   unsigned char mode, Sint32 invisibility,
		                   unsigned char outline, unsigned char shifttype);
		void buffer_to_screen(Sint32 viewstartx,Sint32 viewstarty,
		                      Sint32 viewwidth, Sint32 viewheight) override;
		void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled) override;
		void draw_box(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, unsigned char color, Sint32 filled, Sint32 tobuffer);
		void draw_rect_filled(Sint32 x, Sint32 y, Uint32 w, Uint32 h, unsigned char color, Uint8 alpha);
		void draw_button(const SDL_Rect& rect, Sint32 border);
		void draw_button_inverted(const SDL_Rect& rect);
		void draw_button_inverted(Sint32 x, Sint32 y, Uint32 w, Uint32 h);
		void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border) override;
		void draw_button(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, Sint32 border, Sint32 tobuffer);
		void draw_button_colored(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, bool use_border, int base_color, int high_color = 15, int shadow_color = 11);
		Sint32 draw_dialog(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2, const char *header);
		void draw_text_bar(Sint32 x1, Sint32 y1, Sint32 x2, Sint32 y2);
		void darken_screen();
		void swap() override;
		void get_pixel(int x, int y, Uint8 *r, Uint8 *g, Uint8 *b);
		int get_pixel(int x, int y, int *index);
		int get_pixel(int offset);
		bool save_screenshot();
		void FadeBetween24(SDL_Surface *, const Uint8 *, const Uint8 *, const int);
		int FadeBetween(SDL_Surface *, SDL_Surface *, SDL_Surface *);
		int fadeblack(bool fade_in);

		// GameWorld accessor
		og::gameplay::GameWorld& world() { return *world_; }
		const og::gameplay::GameWorld& world() const { return *world_; }

		std::array<unsigned char, 768>& ourpalette;
		std::array<unsigned char, 768>& redpalette;
		std::array<unsigned char, 768>& bluepalette;
		std::array<unsigned char, 768>& dospalette;
		std::array<unsigned char, 64000>& videobuffer;
		short& cyclemode;
		int& fadeDuration;
		int& screen_width;
		int& screen_height;
		int& fullscreen;
		int& pdouble;
		text& text_normal;
		text& text_big;

        // General drawing data
		std::array<unsigned char, 768> newpalette{};
		short palmode;

		// Level file data and loader wiring
		std::unique_ptr<loader> myloader;
		og::data::LevelFileMetadata level_file_metadata_;
		og::data::LevelFileIoError last_level_io_error_ = og::data::LevelFileIoError::None;
		LevelVisuals level_visuals_;

		// Save data
		SaveData save_data;

		std::string special_name[NUM_FAMILIES][NUM_SPECIALS];
		std::string alternate_name[NUM_FAMILIES][NUM_SPECIALS];
		std::unique_ptr<soundob> soundp;
		short redrawme;
		std::unique_ptr<viewscreen> viewob[5];
		short numviews;
		std::uint32_t timerstart;
		std::uint32_t framecount;

	private:
		void init_common(short howmany, bool has_display);
		void wire_entity_factory_callbacks();
		void clear_stale_view_controls();
		void sync_world_from_save();
		void sync_save_from_world();
		og::gameplay::GameWorld* world_ = nullptr;
};
