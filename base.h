#ifndef __BASE_H
#define __BASE_H

// BASE definitions (perhaps this should be broken up some more

#include <stdio.h>
#include <stdlib.h>
// Z's script: #include <conio.h>
// Z's script: #include <mem.h>
#include <string.h>
#include <fstream.h>
// Z's script: #include <dos.h>
// Z's script: #include <process.h>
#include "sounds.h"
//buffers: we want input.h instead of int32.h now #include "int32.h"
#include "input.h"
#include "parse32.h"
#include "scankeys.h"
#include "pal32.h"
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

class oblink;
class text;
class loader;
class statistics;
class command;
class guy;
class radar;

class soundob;
class smoother;
class packfile;

class oblink
{
  public:
         oblink();
         walker  *ob;
         oblink  *next;
};

#define GLAD_VER "3.8K"

#define DIFFICULTY_SETTINGS 3

unsigned long random(unsigned long x);

#define VIDEO_ADDRESS 0xA000
#define VIDEO_LINEAR ( (VIDEO_ADDRESS) << 4)

#define DPMI_INT        0x31
struct meminfo {
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

extern screen * myscreen; // global, availible to anyone
extern smoother * mysmoother; // global, availible to anyone
extern packfile * pixpack;

#define MAX_LEVELS 500 // Maximum number of scenarios allowed ..

#define GRID_SIZE 16

#define PROT_MODE 1  // comment this out when not in protected mode
#ifdef PROT_MODE
  #define init_sound(x,y,z)  while (0)
  //#define play_sound(x)      while (0)
#endif

// Used for the help-text system:
#define MAX_LINES 100   // maximum number of lines in helpfile
#define HELP_WIDTH 100   // maximum length of display line
short   fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], FILE *infile);
short   read_help(char *somefile,screen *myscreen);
short   read_scenario(screen  *myscreen);
char* read_one_line(FILE *infile, short length);

//color defines:
#define DEFAULT_TEXT_COLOR 88

#define WHITE        24
#define BLACK        160
#define GREY         23
#define YELLOW       88
#define RED          40
#define DARK_BLUE    72
#define LIGHT_BLUE   120
#define DARK_GREEN   63
#define LIGHT_GREEN  56
        
// Color cycling:
#define WATER_START  208
#define WATER_END    223
#define ORANGE_START 224
#define ORANGE_END   231

// Random defines:
//#define PROFILING
//#include "profiler.h"
#define REGISTERED  // synchronize with soundob.h and cheats !!
#define CHEAT_MODE 1  // set to 0 for no cheats..
// Picture Object class defs

// HP BAR COLOR DEFINES
#define BAR_BACK_COLOR 11
#define BOX_COLOR 0
#define LOW_HP_COLOR 42
#define MID_HP_COLOR 237
#define HIGH_HP_COLOR 61
#define MAX_HP_COLOR 56 // When hp's are over max :)

// MP BAR COLOR DEFINES
#define LOW_MP_COLOR 42
#define MID_MP_COLOR 108
#define HIGH_MP_COLOR 72
#define MAX_MP_COLOR 64 // When mp's are over max :)

// Generators are limited by this number
#define MAXOBS 150

// Define scan codes
#define SCAN_UP_LEFT 71
#define SCAN_UP 72
#define SCAN_UP_RIGHT 73
#define SCAN_LEFT 75
#define SCAN_RIGHT 77
#define SCAN_DOWN_LEFT 79
#define SCAN_DOWN 80
#define SCAN_DOWN_RIGHT 81


//Screen window boundaries, two player
#define T_LEFT_ONE 0
#define T_UP_ONE 0
#define T_LEFT_TWO 164
#define T_UP_TWO 0
#define T_WIDTH 156
#define T_HEIGHT 200

// Act types
#define ACT_RANDOM 0
#define ACT_FIRE 1
#define ACT_CONTROL 2
#define ACT_GUARD 3
#define ACT_GENERATE 4
#define ACT_DIE 5
#define ACT_SIT 6

// Team types

//              //#define MY_TEAM 0
//              #define ELF_TEAM 1
//              #define KNIGHT_TEAM 2
//              #define MAX_TEAM 2

#define MAX_TEAM 7

// Other screen-type things
#define NUM_SPECIALS 6

// Animation Types : Livings
#define ANI_WALK 0
#define ANI_ATTACK 1
#define ANI_TELE_OUT 2
#define ANI_SKEL_GROW 3
#define ANI_TELE_IN 3
#define ANI_SLIME_SPLIT 4

// Animations types : weapons
#define ANI_GROW 1 // Trees have no attack animation
#define ANI_GLOWGROW 1 // Neither do sparkles
#define ANI_GLOWPULSE 2 // sparkles cycling

// These are for effect objects ..
#define ANI_EXPAND_8 1 //1
#define ANI_DOOR_OPEN 1 // Door openning
#define ANI_SCARE    1 // 2 ghost scare
#define ANI_BOMB     1 // 3 thief's bomb
#define ANI_EXPLODE  1 // 4
#define ANI_SPIN     1 // for the marker

// Orders
#define ORDER_LIVING 0
#define ORDER_WEAPON 1
#define ORDER_TREASURE 2
#define ORDER_GENERATOR 3
#define ORDER_FX 4
#define ORDER_SPECIAL 5
#define ORDER_BUTTON1 6

// Living familes
#define FAMILY_SOLDIER 0
#define FAMILY_ELF 1
#define FAMILY_ARCHER 2
#define FAMILY_MAGE 3
#define FAMILY_SKELETON 4
#define FAMILY_CLERIC 5
#define FAMILY_FIREELEMENTAL 6
#define FAMILY_FAERIE 7
#define FAMILY_SLIME 8
#define FAMILY_SMALL_SLIME 9
#define FAMILY_MEDIUM_SLIME 10
#define FAMILY_THIEF 11
#define FAMILY_GHOST 12
#define FAMILY_DRUID 13
#define FAMILY_ORC   14
#define FAMILY_BIG_ORC 15
#define FAMILY_BARBARIAN 16
#define FAMILY_ARCHMAGE 17
#define FAMILY_GOLEM 18
#define FAMILY_GIANT_SKELETON 19
#define FAMILY_TOWER1 20
#define NUM_FAMILIES 21  // # of families; make sure to change the
                         // SIZE_FAMILIES in loader.cpp as well
                         // (or your code will act weird)

#define PIX(a,b) (NUM_FAMILIES*a+b)

//Weapon familes
#define FAMILY_KNIFE 0
#define FAMILY_ROCK 1
#define FAMILY_ARROW 2
#define FAMILY_FIREBALL 3
#define FAMILY_TREE 4
#define FAMILY_METEOR 5
#define FAMILY_SPRINKLE 6
#define FAMILY_BONE 7
#define FAMILY_BLOOD 8
#define FAMILY_BLOB 9
#define FAMILY_FIRE_ARROW 10
#define FAMILY_LIGHTNING 11
#define FAMILY_GLOW 12
#define FAMILY_WAVE 13
#define FAMILY_WAVE2 14
#define FAMILY_WAVE3 15
#define FAMILY_CIRCLE_PROTECTION 16
#define FAMILY_HAMMER 17
#define FAMILY_DOOR 18
#define FAMILY_BOULDER 19

// Treasure families
#define FAMILY_STAIN 0
#define FAMILY_DRUMSTICK 1
#define FAMILY_GOLD_BAR 2
#define FAMILY_SILVER_BAR 3
#define FAMILY_MAGIC_POTION 4
#define FAMILY_INVIS_POTION 5
#define FAMILY_INVULNERABLE_POTION 6
#define FAMILY_FLIGHT_POTION 7
#define FAMILY_EXIT 8
#define FAMILY_TELEPORTER 9
#define FAMILY_LIFE_GEM 10 // generated upon death
#define FAMILY_KEY 11
#define FAMILY_SPEED_POTION 12
#define MAX_TREASURE 12          // # of biggest treasure..

// Generator families
#define FAMILY_TENT 0  // skeletons
#define FAMILY_TOWER 1 // mages
#define FAMILY_BONES 2 // ghosts
#define FAMILY_TREEHOUSE 3 // elves :)

// FX families
//#define FAMILY_STAIN 0
#define FAMILY_EXPAND 0
#define FAMILY_GHOST_SCARE 1
#define FAMILY_BOMB 2
#define FAMILY_EXPLOSION 3      // Bombs, etc.
#define FAMILY_FLASH 4          // Used for teleporter effects
#define FAMILY_MAGIC_SHIELD 5   // revolving protective shield
#define FAMILY_KNIFE_BACK  6    // Returning blade
#define FAMILY_BOOMERANG  7     // Circling boomerang
#define FAMILY_CLOUD 8          // purple poison cloud
#define FAMILY_MARKER 9         // Marker for Mages Teleport
#define FAMILY_CHAIN 10         // 'Chain lightning' effect
#define FAMILY_DOOR_OPEN 11     // The open door

// Special families
#define FAMILY_RESERVED_TEAM 0

// Button graphic families
#define FAMILY_NORMAL1 0
#define FAMILY_PLUS 1
#define FAMILY_MINUS 2

// Facings
#define FACE_UP 0
#define FACE_UP_RIGHT 1
#define FACE_RIGHT 2
#define FACE_DOWN_RIGHT 3
#define FACE_DOWN 4
#define FACE_DOWN_LEFT 5
#define FACE_LEFT 6
#define FACE_UP_LEFT 7
#define NUM_FACINGS 8

// Stats defines
#define COMMAND_WALK 1
#define COMMAND_FIRE 2
#define COMMAND_RANDOM_WALK 3   // walk random dir ..
#define COMMAND_DIE 4   // bug fixing ..
#define COMMAND_FOLLOW 5
#define COMMAND_RUSH 6  // Rush your enemy!
#define COMMAND_MULTIDO 7 // Do <com1> commands in one round
#define COMMAND_QUICK_FIRE 8 // Fires with no busy or animation
#define COMMAND_SET_WEAPON 9 // set weapon type
#define COMMAND_RESET_WEAPON 10 // restores weapon to default
#define COMMAND_SEARCH 11       // use right-hand rule to find foe
#define COMMAND_ATTACK 12       // attack / move to a close, current foe
#define COMMAND_RIGHT_WALK 13   // use right-hand rule ONLY; no direct walk
#define COMMAND_UNCHARM 14      // recover from being 'charmed'
#define REGEN (long) 4000       // used to calculate time between heals

#define STANDARD_TEXT_TIME 75   // how many cycles to display text?
#define TEXT_1 "text.pix"       // standard text pixie

#define DONT_DELETE 1

#ifndef PROT_MODE
  // sound
  extern "C" short init_sound(char *filename, short speed, short which);
  extern "C" void play_sound(short which);
#endif

//most of these are graphlib and are being ported to video
void load_map_data(unsigned char **whereto);
char* get_cfg_item(char *section, char *item);
short get_pix_directory();
short get_pix_directory(char *whereto); // copies to whereto, returns OKAY..

// Functions in game.cpp
short load_team_list(char * filename, screen  *myscreen);
short load_saved_game(char *filename, screen  *myscreen);
short save_game(char *filename, screen  *myscreen);

#define NORMAL_MODE    0     // #defines for walkputbuffer mode type
#define INVISIBLE_MODE 1     //
#define PHANTOM_MODE   2     //
#define OUTLINE_MODE   3     //

#define SHIFT_LIGHTER      0  //  #defines for phantomputbuffer
#define SHIFT_DARKER       1  //
#define SHIFT_LEFT         2  //
#define SHIFT_RIGHT        3  //
#define SHIFT_RIGHT_RANDOM 4  //  shifts right 1 or 2 spaces (whole image)
#define SHIFT_RANDOM       5  //  shifts 1 or 2 right (on pixel x pixel basis)
#define SHIFT_BLOCKY       6  //  courtroom style


#define SCEN_TYPE_CAN_EXIT (char) 1 // make these go by power of 2, 1,2,4,8
#define SCEN_TYPE_GEN_EXIT (char) 2
#define SCEN_TYPE_SAVE_ALL (char) 4 // save named npc's 

#define OUTLINE_NAMED         7              // #defines for outline colors
#define OUTLINE_INVULNERABLE  224            //
#define OUTLINE_FLYING        208            //
#define OUTLINE_INVISIBLE query_team_color() //

#define ACTION_FOLLOW (char) 1

unsigned char * read_pixie_file(char  * filename);

// Some stuff for palette
typedef struct
{
  char r, g, b;
} rgb;

typedef rgb palette[256];

void set_vga_palette(palette p);
rgb set_rgb(char r, char g, char b);
short read_palette(FILE  *f, palette p);
unsigned char * read_pixie_file(char  * filename);


#endif

