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

// BASE definitions (perhaps this should be broken up some more

/* ChangeLog
	buffers: 7/31/02: *C++ style includes for string and fstream
			: *deleted some redundant headers
			: *added math.h,ctype.h
*/

#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <cmath>
#include <cctype>
#include <openglad/legacy/sounds.h>
#include "SDL_stdinc.h"
#include "SDL_rwops.h"
#include <openglad/input/input.h>
#include <openglad/core/util.h>
#include <openglad/data/gparser.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/legacy/soundob.h> // sound defines

class video;
class screen;
class viewscreen;
class pixie;
class pixieN;

class walker;
class living;
class weap;
class treasure;
class effect;

class text;
class loader;
class statistics;
class command;
class guy;
class radar;

class soundob;
class smoother;

// DIFFICULTY_SETTINGS moved to core/constants.h

Uint32 random(Uint32 x);

inline constexpr int VIDEO_ADDRESS = 0xA000;
inline constexpr int VIDEO_LINEAR = VIDEO_ADDRESS << 4;

inline constexpr int DPMI_INT = 0x31;
struct meminfo
{
	unsigned LargestBlockAvail;
	unsigned MaxUnlockedPage;
	unsigned LargestLockablePage;
	unsigned LinAddrSpace;
	unsigned NumFreePagesAvail;
	unsigned NumPhysicalPagesFree;
	unsigned TotalPhysicalPages;
	unsigned FreeLinAddrSpace;
	unsigned SizeOfPageFile;
	unsigned Reserved[3];
};

// Observer pointer (non-owning). Owned by og::runtime::GameSession.
// Transitional: prefer ctx().active_screen() for new code.
extern screen * myscreen;

inline constexpr int MAX_LEVELS = 500; // Maximum number of scenarios allowed ..

#define PROT_MODE 1  // comment this out when not in protected mode
#ifdef PROT_MODE
  #define init_sound(x,y,z)  while (0)
//#define play_sound(x)      while (0)
#endif

// Used for the help-text system:
inline constexpr int MAX_LINES = 100;   // maximum number of lines in helpfile
inline constexpr int HELP_WIDTH = 100;   // maximum length of display line
short   fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], SDL_RWops *infile);
short   read_campaign_intro(screen *myscreen);
short   read_scenario(screen  *myscreen);
std::string read_one_line(SDL_RWops *infile, short length);

//color defines:
inline constexpr unsigned char DEFAULT_TEXT_COLOR = 88;

inline constexpr unsigned char PURE_WHITE   = 15;
inline constexpr unsigned char PURE_BLACK   = 0;
inline constexpr unsigned char WHITE        = 24;
inline constexpr unsigned char BLACK        = 160;
inline constexpr unsigned char GREY         = 23;
inline constexpr unsigned char YELLOW       = 88;
inline constexpr unsigned char RED          = 40;
inline constexpr unsigned char DARK_BLUE    = 72;
inline constexpr unsigned char LIGHT_BLUE   = 120;
inline constexpr unsigned char DARK_GREEN   = 63;
inline constexpr unsigned char LIGHT_GREEN  = 56;

// Color cycling:
inline constexpr unsigned char WATER_START  = 208;
inline constexpr unsigned char WATER_END    = 223;
inline constexpr unsigned char ORANGE_START = 224;
inline constexpr unsigned char ORANGE_END   = 231;

// Random defines:
//#define PROFILING
//#include "profiler.h"
#define CHEAT_MODE 1  // set to 0 for no cheats..
// Picture Object class defs

// HP BAR COLOR DEFINES
inline constexpr unsigned char BAR_BACK_COLOR = 11;
inline constexpr unsigned char BOX_COLOR = 0;
inline constexpr unsigned char LOW_HP_COLOR = 42;
inline constexpr unsigned char MID_HP_COLOR = 237;
inline constexpr unsigned char HIGH_HP_COLOR = 61;
inline constexpr unsigned char MAX_HP_COLOR = 56; // When hp's are over max :)

// MP BAR COLOR DEFINES
inline constexpr unsigned char LOW_MP_COLOR = 42;
inline constexpr unsigned char MID_MP_COLOR = 108;
inline constexpr unsigned char HIGH_MP_COLOR = 72;
inline constexpr unsigned char MAX_MP_COLOR = 64; // When mp's are over max :)

// Game constants (families, facings, commands, etc.) now live in core/constants.h
#include <openglad/core/constants.h>
#include <openglad/core/stats.h>

inline constexpr int STANDARD_TEXT_TIME = 75;   // how many cycles to display text?
inline constexpr const char* TEXT_1 = "text.pix";       // standard text pixie
inline constexpr const char* TEXT_BIG = "textbig.pix";       // standard text pixie

inline constexpr int DONT_DELETE = 1;

#ifndef PROT_MODE
// sound
extern "C" short init_sound(char *filename, short speed, short which);
extern "C" void play_sound(short which);
#endif

class PixieData;

//most of these are graphlib and are being ported to video
void load_map_data(PixieData* whereto);
char* get_cfg_item(char *section, char *item);

// Functions in game.cpp
enum class LoadSavedGameError
{
    None = 0,
    MissingScreen,
    UsedFallbackLevel,
    FallbackLevelLoadFailed
};

short load_saved_game(const char *filename, screen  *myscreen);
LoadSavedGameError load_saved_game_with_error(const char *filename, screen *myscreen);

#define OUTLINE_INVISIBLE query_team_color() //

#include <openglad/data/pixie_data.h>
PixieData read_pixie_file(const char  * filename);

// Some stuff for palette
struct rgb
{
	char r, g, b;
};

using palette = rgb[256];

void set_vga_palette(palette p);
rgb set_rgb(char r, char g, char b);
short read_palette(FILE  *f, palette p);
