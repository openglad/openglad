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

// Definition of VIEWSCREEN class

#include <openglad/interface/base.h>
#include <openglad/interface/level_runtime_data.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Viewscreen-related defines
inline constexpr signed char PREF_LIFE = 0;
  inline constexpr signed char PREF_LIFE_TEXT  = 0;
  inline constexpr signed char PREF_LIFE_BARS  = 1;
  inline constexpr signed char PREF_LIFE_BOTH  = 2;
  inline constexpr signed char PREF_LIFE_SMALL = 3;
  inline constexpr signed char PREF_LIFE_OFF   = 4;
inline constexpr signed char PREF_SCORE = 1;
  inline constexpr signed char PREF_SCORE_OFF = 0;
  inline constexpr signed char PREF_SCORE_ON  = 1;
inline constexpr signed char PREF_VIEW = 2;
  inline constexpr signed char PREF_VIEW_FULL   = 0;
  inline constexpr signed char PREF_VIEW_PANELS = 1;
  inline constexpr signed char PREF_VIEW_1      = 2;
  inline constexpr signed char PREF_VIEW_2      = 3;
  inline constexpr signed char PREF_VIEW_3      = 4;
inline constexpr signed char PREF_JOY = 3;
  inline constexpr signed char PREF_NO_JOY = 0;
  inline constexpr signed char PREF_USE_JOY = 1;
inline constexpr signed char PREF_RADAR = 4;
  inline constexpr signed char PREF_RADAR_OFF   = 0;
  inline constexpr signed char PREF_RADAR_ON    = 1;
inline constexpr signed char PREF_FOES = 5;
  inline constexpr signed char PREF_FOES_OFF    = 0;
  inline constexpr signed char PREF_FOES_ON     = 1;
inline constexpr signed char PREF_GAMMA = 6;
inline constexpr signed char PREF_OVERLAY = 7;
  inline constexpr signed char PREF_OVERLAY_OFF = 0;
  inline constexpr signed char PREF_OVERLAY_ON  = 1;

inline constexpr int PREF_MAX = 8;  // == 1 + highest pref ..

inline constexpr int MAX_MESSAGES = 5;  // max of 5 lines, currently

struct InputState;
class viewscreen;
class walker;
class radar;

// Pure helper functions for HP/MP color thresholds
unsigned char compute_hp_color(float hp, float maxhp);
unsigned char compute_mp_color(float mp, float maxmp);
void reset_viewscreen_input_debounce();

// This is a child object of all viewscreens
//  It is used to save and load all prefs
//  because each player has their own
//  prefs.  WE ASSUME 4 PLAYERS ALWAYS
class options
{
	public:
		options();
		~options();
		short load(viewscreen *viewp);
		short save(viewscreen *viewp);
	protected:
		signed char prefs[4][10];
		char keys[4][16];
};

class viewscreen
{
	public:
		viewscreen(short x, short y, short length, short height, short whatnum);
		~viewscreen();
		void clear();
		bool draw ();
		bool redraw();
		bool redraw(LevelRuntimeData* data, bool draw_radar = true);
		bool refresh();
		short input(const void* native_event);
		template <typename EventT>
		short input(const EventT& event)
		{
			return input(static_cast<const void*>(&event));
		}
		short continuous_input();
		void process_input(const InputState& input_state);
		void set_display_text(std::string_view newtext, short numcycles);
		void refresh_display_text(std::string_view newtext, short numcycles);
		void display_text(); // put the text to the buffer, if there
		void shift_text(Sint32 row); // cycle text upward
		void clear_text(void); // clear all text in buffer
		bool draw_obs(); //moved here to fix radar
		bool draw_obs(LevelRuntimeData* data);
		void resize(short x, short y, short length, short height);
		void resize(char whatmode); // set according to preferences ..
		void view_team();
		void view_team(short left, short top, short right, short bottom);
		void options_menu();   // display the options menu
		Sint32 set_key_prefs(); // get player keyboard info
		void view_key_bindings(); // display current key bindings
		Sint32 change_speed(Sint32 whichway);
		Sint32 change_gamma(Sint32 whichway);
		walker* find_next_control();

		Sint32 gamma; // for gamma correction

		std::string textlist[MAX_MESSAGES];
		short textcycles[MAX_MESSAGES];  // duration in sim ticks
		std::uint32_t text_expire_ticks[MAX_MESSAGES];
		
		char infotext[80]; // text to display
		short mynum;     // # to id the viewscreen, 0, 1, 2 ...
		short my_team;         // used for Player-v-Player mode
			int* mykeys;     // holds the keyboard mapping
			walker  *control;  // the user
			Sint32 xpos,ypos;
			Sint32 topx, topy;
			Sint32 xloc, yloc; // physical screen coords
			Sint32 endx, endy; // screen coords of lower right corner
			signed char prefs[10]; // User preferences ..
			std::unique_ptr<radar> myradar;
			short radarstart; //has the radar been started yet?
			Sint32 xview;
			Sint32 yview;
			float interpolation_alpha = 1.0f;

	protected:
		options *prefsob;

		short size = 0;
		unsigned char  *bmp = nullptr,  *oldbmp = nullptr;
};
