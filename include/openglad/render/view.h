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

#include "SDL.h"
#include <openglad/data/level_data.h>
#include <memory>
#include <string>
#include <string_view>

// Zoom constants
inline constexpr float ZOOM_DEFAULT = 1.0f;
inline constexpr float ZOOM_MIN = 0.5f;
inline constexpr float ZOOM_MAX = 3.0f;
inline constexpr float ZOOM_STEP = 0.25f;

// Pure zoom math helpers (testable without SDL)
float zoom_clamp(float level);
float zoom_apply_in(float level);
float zoom_apply_out(float level);
int zoom_world_dim(int screen_dim, float zoom_level);

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
		bool redraw(LevelData* data, bool draw_radar = true);
		bool refresh();
		short input(const SDL_Event& event);
		short continuous_input();
		void process_input(const InputState& input_state);
		void set_display_text(std::string_view newtext, short numcycles);
		void display_text(); // put the text to the buffer, if there
		void shift_text(Sint32 row); // cycle text upward
		void clear_text(void); // clear all text in buffer
		bool draw_obs(); //moved here to fix radar
		bool draw_obs(LevelData* data);
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
		short textcycles[MAX_MESSAGES];  // cycles to display screen-text
		
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

			// Camera zoom (1.0 = default, <1.0 = zoom out, >1.0 = zoom in)
			float zoom_level = ZOOM_DEFAULT;
			void zoom_in();
			void zoom_out();
			float get_zoom_level() const { return zoom_level; }
			// World-space viewport dimensions at current zoom
			Sint32 world_width() const;
			Sint32 world_height() const;

		// Cached offscreen surface for zoom rendering (reused across frames)
		SDL_Surface* zoom_surface_ = nullptr;
		Sint32 zoom_surface_w_ = 0;
		Sint32 zoom_surface_h_ = 0;
		void free_zoom_surface();

	protected:
		options *prefsob;

		short size;
		unsigned char  *bmp,  *oldbmp;
};

// Legacy global (transitional): installed by og::runtime::GameSession.
// Treat as borrowed and nullable.
extern options* theprefs;
