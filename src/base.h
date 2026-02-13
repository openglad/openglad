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
#include "sounds.h"
#include "SDL.h"
#include "input/input.h"
#include "core/util.h"
#include "data/gparser.h"
#include "render/pal32.h"
#include "pixdefs.h"
#include "soundob.h" // sound defines

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

inline constexpr int DIFFICULTY_SETTINGS = 3;

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

// Observer pointer. Owned by `runtime/screen_lifecycle` (`global_screen_owner()`).
extern screen * myscreen; // global, available to anyone

inline constexpr int MAX_LEVELS = 500; // Maximum number of scenarios allowed ..

inline constexpr int GRID_SIZE = 16;

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

// Generators are limited by this number
inline constexpr int MAXOBS = 150;



// Act types
inline constexpr int ACT_RANDOM = 0;
inline constexpr int ACT_FIRE = 1;
inline constexpr int ACT_CONTROL = 2;
inline constexpr int ACT_GUARD = 3;
inline constexpr int ACT_GENERATE = 4;
inline constexpr int ACT_DIE = 5;
inline constexpr int ACT_SIT = 6;

// Team types

//              //#define MY_TEAM 0
//              #define ELF_TEAM 1
//              #define KNIGHT_TEAM 2
//              #define MAX_TEAM 2

inline constexpr int MAX_TEAM = 7;

// Other screen-type things
inline constexpr int NUM_SPECIALS = 6;

// Animation Types : Livings
inline constexpr int ANI_WALK = 0;
inline constexpr int ANI_ATTACK = 1;
inline constexpr int ANI_TELE_OUT = 2;
inline constexpr int ANI_SKEL_GROW = 3;
inline constexpr int ANI_TELE_IN = 3;
inline constexpr int ANI_SLIME_SPLIT = 4;

// Animations types : weapons
inline constexpr int ANI_GROW = 1; // Trees have no attack animation
inline constexpr int ANI_GLOWGROW = 1; // Neither do sparkles
inline constexpr int ANI_GLOWPULSE = 2; // sparkles cycling

// These are for effect objects ..
inline constexpr int ANI_EXPAND_8 = 1; //1
inline constexpr int ANI_DOOR_OPEN = 1; // Door opening
inline constexpr int ANI_SCARE    = 1; // 2 ghost scare
inline constexpr int ANI_BOMB     = 1; // 3 thief's bomb
inline constexpr int ANI_EXPLODE  = 1; // 4
inline constexpr int ANI_SPIN     = 1; // for the marker

// Orders
enum class Order : unsigned char {
    Living = 0,
    Weapon = 1,
    Treasure = 2,
    Generator = 3,
    FX = 4,
    Special = 5,
    Button1 = 6
};

// Living families
inline constexpr int FAMILY_SOLDIER = 0;
inline constexpr int FAMILY_ELF = 1;
inline constexpr int FAMILY_ARCHER = 2;
inline constexpr int FAMILY_MAGE = 3;
inline constexpr int FAMILY_SKELETON = 4;
inline constexpr int FAMILY_CLERIC = 5;
inline constexpr int FAMILY_FIREELEMENTAL = 6;
inline constexpr int FAMILY_FAERIE = 7;
inline constexpr int FAMILY_SLIME = 8;
inline constexpr int FAMILY_SMALL_SLIME = 9;
inline constexpr int FAMILY_MEDIUM_SLIME = 10;
inline constexpr int FAMILY_THIEF = 11;
inline constexpr int FAMILY_GHOST = 12;
inline constexpr int FAMILY_DRUID = 13;
inline constexpr int FAMILY_ORC   = 14;
inline constexpr int FAMILY_BIG_ORC = 15;
inline constexpr int FAMILY_BARBARIAN = 16;
inline constexpr int FAMILY_ARCHMAGE = 17;
inline constexpr int FAMILY_GOLEM = 18;
inline constexpr int FAMILY_GIANT_SKELETON = 19;
inline constexpr int FAMILY_TOWER1 = 20;
inline constexpr int NUM_FAMILIES = 21;  // # of families; make sure to change the
// SIZE_FAMILIES in loader.cpp as well
// (or your code will act weird)

constexpr int PIX(int a, int b) { return NUM_FAMILIES * a + b; }
constexpr int PIX(Order a, int b) { return NUM_FAMILIES * static_cast<int>(a) + b; }

//Weapon families
inline constexpr int FAMILY_KNIFE = 0;
inline constexpr int FAMILY_ROCK = 1;
inline constexpr int FAMILY_ARROW = 2;
inline constexpr int FAMILY_FIREBALL = 3;
inline constexpr int FAMILY_TREE = 4;
inline constexpr int FAMILY_METEOR = 5;
inline constexpr int FAMILY_SPRINKLE = 6;
inline constexpr int FAMILY_BONE = 7;
inline constexpr int FAMILY_BLOOD = 8;
inline constexpr int FAMILY_BLOB = 9;
inline constexpr int FAMILY_FIRE_ARROW = 10;
inline constexpr int FAMILY_LIGHTNING = 11;
inline constexpr int FAMILY_GLOW = 12;
inline constexpr int FAMILY_WAVE = 13;
inline constexpr int FAMILY_WAVE2 = 14;
inline constexpr int FAMILY_WAVE3 = 15;
inline constexpr int FAMILY_CIRCLE_PROTECTION = 16;
inline constexpr int FAMILY_HAMMER = 17;
inline constexpr int FAMILY_DOOR = 18;
inline constexpr int FAMILY_BOULDER = 19;

// Treasure families
inline constexpr int FAMILY_STAIN = 0;
inline constexpr int FAMILY_DRUMSTICK = 1;
inline constexpr int FAMILY_GOLD_BAR = 2;
inline constexpr int FAMILY_SILVER_BAR = 3;
inline constexpr int FAMILY_MAGIC_POTION = 4;
inline constexpr int FAMILY_INVIS_POTION = 5;
inline constexpr int FAMILY_INVULNERABLE_POTION = 6;
inline constexpr int FAMILY_FLIGHT_POTION = 7;
inline constexpr int FAMILY_EXIT = 8;
inline constexpr int FAMILY_TELEPORTER = 9;
inline constexpr int FAMILY_LIFE_GEM = 10; // generated upon death
inline constexpr int FAMILY_KEY = 11;
inline constexpr int FAMILY_SPEED_POTION = 12;
inline constexpr int MAX_TREASURE = 12;          // # of biggest treasure..

// Generator families
inline constexpr int FAMILY_TENT = 0;  // skeletons
inline constexpr int FAMILY_TOWER = 1; // mages
inline constexpr int FAMILY_BONES = 2; // ghosts
inline constexpr int FAMILY_TREEHOUSE = 3; // elves :)

// FX families
//inline constexpr int FAMILY_STAIN = 0;  // same as treasure FAMILY_STAIN
inline constexpr int FAMILY_EXPAND = 0;
inline constexpr int FAMILY_GHOST_SCARE = 1;
inline constexpr int FAMILY_BOMB = 2;
inline constexpr int FAMILY_EXPLOSION = 3;      // Bombs, etc.
inline constexpr int FAMILY_FLASH = 4;          // Used for teleporter effects
inline constexpr int FAMILY_MAGIC_SHIELD = 5;   // revolving protective shield
inline constexpr int FAMILY_KNIFE_BACK  = 6;    // Returning blade
inline constexpr int FAMILY_BOOMERANG  = 7;     // Circling boomerang
inline constexpr int FAMILY_CLOUD = 8;          // purple poison cloud
inline constexpr int FAMILY_MARKER = 9;         // Marker for Mages Teleport
inline constexpr int FAMILY_CHAIN = 10;         // 'Chain lightning' effect
inline constexpr int FAMILY_DOOR_OPEN = 11;     // The open door
inline constexpr int FAMILY_HIT = 12;           // Show when hit

// Special families
inline constexpr int FAMILY_RESERVED_TEAM = 0;

// Button graphic families
inline constexpr int FAMILY_NORMAL1 = 0;
inline constexpr int FAMILY_PLUS = 1;
inline constexpr int FAMILY_MINUS = 2;
inline constexpr int FAMILY_WRENCH = 3;

// Facings
inline constexpr int FACE_UP = 0;
inline constexpr int FACE_UP_RIGHT = 1;
inline constexpr int FACE_RIGHT = 2;
inline constexpr int FACE_DOWN_RIGHT = 3;
inline constexpr int FACE_DOWN = 4;
inline constexpr int FACE_DOWN_LEFT = 5;
inline constexpr int FACE_LEFT = 6;
inline constexpr int FACE_UP_LEFT = 7;
inline constexpr int NUM_FACINGS = 8;

// Stats defines
inline constexpr int COMMAND_WALK = 1;
inline constexpr int COMMAND_FIRE = 2;
inline constexpr int COMMAND_RANDOM_WALK = 3;   // walk random dir ..
inline constexpr int COMMAND_DIE = 4;   // bug fixing ..
inline constexpr int COMMAND_FOLLOW = 5;
inline constexpr int COMMAND_RUSH = 6;  // Rush your enemy!
inline constexpr int COMMAND_MULTIDO = 7; // Do <com1> commands in one round
inline constexpr int COMMAND_QUICK_FIRE = 8; // Fires with no busy or animation
inline constexpr int COMMAND_SET_WEAPON = 9; // set weapon type
inline constexpr int COMMAND_RESET_WEAPON = 10; // restores weapon to default
inline constexpr int COMMAND_SEARCH = 11;       // use right-hand rule to find foe
inline constexpr int COMMAND_ATTACK = 12;       // attack / move to a close, current foe
inline constexpr int COMMAND_RIGHT_WALK = 13;   // use right-hand rule ONLY; no direct walk
inline constexpr int COMMAND_UNCHARM = 14;      // recover from being 'charmed'
inline constexpr Sint32 REGEN = 4000;       // used to calculate time between heals

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

inline constexpr int NORMAL_MODE    = 0;     // for walkputbuffer mode type
inline constexpr int INVISIBLE_MODE = 1;
inline constexpr int PHANTOM_MODE   = 2;
inline constexpr int OUTLINE_MODE   = 3;

inline constexpr int SHIFT_LIGHTER      = 0;  //  for phantomputbuffer
inline constexpr int SHIFT_DARKER       = 1;
inline constexpr int SHIFT_LEFT         = 2;
inline constexpr int SHIFT_RIGHT        = 3;
inline constexpr int SHIFT_RIGHT_RANDOM = 4;  //  shifts right 1 or 2 spaces (whole image)
inline constexpr int SHIFT_RANDOM       = 5;  //  shifts 1 or 2 right (on pixel x pixel basis)
inline constexpr int SHIFT_BLOCKY       = 6;  //  courtroom style


inline constexpr char SCEN_TYPE_CAN_EXIT = 1; // make these go by power of 2, 1,2,4,8
inline constexpr char SCEN_TYPE_GEN_EXIT = 2;
inline constexpr char SCEN_TYPE_SAVE_ALL = 4; // save named npc's

inline constexpr unsigned char OUTLINE_NAMED         = 7;              // for outline colors
inline constexpr unsigned char OUTLINE_INVULNERABLE  = 224;
inline constexpr unsigned char OUTLINE_FLYING        = 208;
#define OUTLINE_INVISIBLE query_team_color() //

inline constexpr char ACTION_FOLLOW = 1;

#include "data/pixie_data.h"

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
