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
#ifndef __VIEW_H
#define __VIEW_H

// Definition of VIEWSCREEN class

#include "base.h"

// Viewscreen-related defines
#define PREF_LIFE (signed char) 0
  #define PREF_LIFE_TEXT  (signed char) 0
  #define PREF_LIFE_BARS  (signed char) 1
  #define PREF_LIFE_BOTH  (signed char) 2
  #define PREF_LIFE_SMALL (signed char) 3
  #define PREF_LIFE_OFF   (signed char) 4
#define PREF_SCORE (signed char) 1
  #define PREF_SCORE_OFF (signed char) 0
  #define PREF_SCORE_ON  (signed char) 1
#define PREF_VIEW (signed char) 2
  #define PREF_VIEW_FULL   (signed char) 0
  #define PREF_VIEW_PANELS (signed char) 1
  #define PREF_VIEW_1      (signed char) 2
  #define PREF_VIEW_2      (signed char) 3
  #define PREF_VIEW_3      (signed char) 4
#define PREF_JOY (signed char) 3
  #define PREF_NO_JOY (signed char) 0
  #define PREF_USE_JOY (signed char) 1
#define PREF_RADAR (signed char) 4
  #define PREF_RADAR_OFF   (signed char) 0
  #define PREF_RADAR_ON    (signed char) 1
#define PREF_FOES (signed char) 5
  #define PREF_FOES_OFF    (signed char) 0
  #define PREF_FOES_ON     (signed char) 1
#define PREF_GAMMA (signed char) 6
#define PREF_OVERLAY (signed char) 7
  #define PREF_OVERLAY_OFF (signed char) 0
  #define PREF_OVERLAY_ON  (signed char) 1

#define PREF_MAX 8  // == 1 + highest pref ..

class viewscreen;

class joyvalues
{
	public:
		long joyaligned; //has the joystick, if used been centered??
		long joyleft; //left,right,up,down max values
		long joyright;
		long joyup;
		long joydown;//left,right,up,down max values
		long minoffleft;
		long minoffright;
		long minoffup;
		long minoffdown; //minimum deflections to get a joystick hit
		long joycenterx;
		long joycentery;
		long deltax, deltay;
};

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
		joyvalues joys[4];
};

class viewscreen
{
	public:
		friend class walker;
		friend class pixieN;
		friend class pixie;
		friend class text;
		viewscreen(short x, short y, short length, short height, short whatnum, screen  *myscreen);
		~viewscreen();
		void clear();
		short draw ();
		short redraw();
		short refresh();
		short input(char input);
		void set_display_text(char *newtext, short numcycles);
		void display_text(); // put the text to the buffer, if there
		void shift_text(long row); // cycle text upward
		void clear_text(void); // clear all text in buffer
		short draw_obs(); //moved here to fix radar
		void align_joy(); //center the joystick
		void input_joy(); //query the joystick, and move appropriate data to the keyboard!! buffer
		void remove_joy(); //let the player revert to keyboard control
		void resize(short x, short y, short length, short height);
		void resize(char whatmode); // set according to preferences ..
		void view_team();
		void view_team(short left, short top, short right, short bottom);
		void options_menu();   // display the options menu
		long set_key_prefs(); // get player keyboard info
		long change_speed(long whichway);
		long change_gamma(long whichway);

		long gamma; // for gamma correction
#define MAX_MESSAGES 5  // max of 5 lines, currently

		char textlist[MAX_MESSAGES][80]; // max of 80-wide
		short textcycles[MAX_MESSAGES];  // cycles to display screen-text
		text *screentext;       // display screen text;
		char infotext[80]; // text to display
		short mynum;     // # to id the viewscreen, 0, 1, 2 ...
		short my_team;         // used for Player-v-Player mode
		int * mykeys;     // holds the keyboard mapping
		walker  *control;  // the user
		short xpos,ypos;
		short topx, topy;
		short xloc, yloc; // physical screen coords
		short endx, endy; // screen coords of lower right corner
		signed char prefs[10]; // User preferences ..
		radar * myradar;
		short radarstart; //has the radar been started yet?

		long joyaligned; //has the joystick, if used been centered??
		long joyleft; //left,right,up,down max values
		long joyright;
		long joyup;
		long joydown;//left,right,up,down max values
		long minoffleft;
		long minoffright;
		long minoffup;
		long minoffdown; //minimum deflections to get a joystick hit
		long joycenterx;
		long joycentery;
		long deltax, deltay;


	protected:
		options *prefsob;
		//char  *buffer;
		screen  *screenp;
		short size;
		short xview;
		short yview;
		char  *bmp,  *oldbmp;
};

#endif
