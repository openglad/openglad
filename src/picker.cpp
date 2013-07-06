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
#include "graph.h"
#include "button.h"
#include "pal32.h"

//buffers:  using input.h instead #include "int32.h"
#include "input.h"
#include "util.h"

#include "SDL.h"
#include "parser.h"
#include "campaign_picker.h"
#include "level_picker.h"
// Z's script: #include <process.h>
// Z's script: #include <i86.h> //_enable, _disable

#define DOWN(x) (72+x*15)
#define VIEW_DOWN(x) (10+x*20)
#define RAISE 1.85  // please also change in guy.cpp

#define EXIT 1 //these are leftclick return values, exit means leave picker
#define REDRAW 2 //we just exited a menu, so redraw your buttons
#define OK 4 //this function was successful, continue normal operation

// For yes/no prompts
#define YES 5
#define NO 6
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);



#define BUTTON_HEIGHT 15

//int matherr (struct exception *);

void show_guy(Sint32 frames, Sint32 who); // shows the current guy ..
Sint32 name_guy(Sint32 arg); // rename (or name) the current_guy

void glad_main(screen *myscreen, Sint32 playermode);
const char* get_saved_name(const char * filename);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);

Sint32 leftmouse();
void family_name_copy(char *name, short family);

// Zardus: PORT: put in a backpics var here so we can free the pixie files themselves
PixieData backpics[5];
pixieN *backdrops[5];

// Zardus: FIX: this is from view.cpp, so that we can delete it here
extern options *theprefs;

//screen  *myscreen;
text  *mytext;
Sint32 *mymouse;     // hold mouse information
//char main_dir[80];
guy  *current_guy;// = new guy();

char  message[80];
Sint32 editguy = 0;        // Global for editing guys ..
PixieData main_title_logo_data, main_columns_data;
pixieN  *main_title_logo_pix,*main_columns_pix;

vbutton * localbuttons; //global so we can delete the buttons anywhere
short current_team_num = 0;

Sint32 allowable_guys[] =
    { FAMILY_SOLDIER,
      FAMILY_BARBARIAN,
      FAMILY_ELF,
      FAMILY_ARCHER,
      FAMILY_MAGE,
      FAMILY_CLERIC,
      FAMILY_THIEF,
      FAMILY_DRUID,
      FAMILY_ORC,
      FAMILY_SKELETON,
      FAMILY_FIREELEMENTAL,
      FAMILY_SMALL_SLIME,
      FAMILY_FAERIE,
      FAMILY_GHOST
    };

Sint32 current_type = 0; // guy type we're looking at

// Used to label new hires, like "SOLDIER5"
Sint32 numbought[NUM_FAMILIES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

Sint32 costlist[NUM_FAMILIES] =
    {
        250,  // soldier
        150,  // elf
        350,  // archer
        450,  // mage
        300,  // skeleton
        400,  // cleric
        1500, // fire elem
        350,  // faerie
        1500, // slime          // can't buy
        1500, // small slime
        1500, // medium slime   // can't buy
        400,  // thief
        1000, // ghost
        350,  // druid
        700,  // orc
        1500, // 'big' orc
        350,  // barbarian
        450,  // archmage, not used
    };

// Also change guy::guy
Sint32 statlist[NUM_FAMILIES][6] =
    {
        // STR, DEX, CON, INT, ARMOR, LEVEL
        {12, 6, 12,  8, 9,  1},  // soldier
        {5,  9,  5,  12, 8,  1},  // elf
        {6, 12,  6,  10, 5,  1},  // archer
        {4,  6,  4, 16, 5,  1},  // mage
        {9,  6,  9,  6, 9,  1},  // skeleton
        {6,  7,  6, 14, 7,  1},  // cleric
        {14, 5, 30,  6, 16, 1},  // fire elem
        {3,  8,  3,  8, 4,  1},  // faerie
        //  {30, 2, 30,  4, 20, 1},  // slime
        {18, 2, 18,  4, 8, 1},  // slime (big)
        {18, 2, 18,  4, 8, 1},  // small slime
        {18, 2, 18,  4, 8, 1},  // slime (medium)
        //  {24, 2, 24,  4, 14, 1},  // medium slime
        {9, 12, 12,  10,  5, 1},  // thief
        {6, 12, 18,  10, 15, 1},  // ghost
        {7,  8,  6,  12,  7, 1},  // druid
        {16, 4, 14,   2, 11, 1},  // orc
        {16, 4, 14,   2, 11, 1},  // 'big' orc
        {14, 5, 14,   8, 8, 1},  // barbarian
        {4,  6,  4, 16, 5,  1},  // archmage
        //  {8, 12,  8, 32,10,  1},  // archmage
    };

Sint32 statcosts[NUM_FAMILIES][6] =
    {
        // STR, DEX, CON, INT, ARMOR, LEVEL
        { 6,10, 6,25,50, 200},  // soldier
        {25, 6,12,10,50, 200},  // elf
        {15, 6, 9,10,50, 200},  // archer
        {20,15,16, 6,50, 200},  // mage
        { 6, 6,16,25,50, 200},  // skeleton
        {15,15, 9, 6,50, 200},  // cleric
        {15,15,12,15,50, 200},  // fire elem
        {25, 6,12,15,50, 200},  // faerie
        {20,20,16,20,50, 200},  // slime
        {20,20,16,20,50, 200},  // small slime
        {20,20,16,20,50, 200},  // medium slime
        {15, 6, 9,10,50, 200},  // thief
        {20,20,16,20,45, 200},  // ghost
        {15,15, 9, 6,50, 200},  // druid
        { 8,15, 9,40,50, 200},  // orc
        { 8,15, 9,40,50, 200},  // 'big' orc
        { 8,10, 5,35,50, 200},  // barbarian
        //  {25,15,20, 5,50, 200},  // archmage
        {30,20,25, 7,55, 200},  // archmage
    };

// Difficulty settings .. in percent, so 100 == normal
Sint32 current_difficulty = 1; // setting 'normal'
Sint32 difficulty_level[DIFFICULTY_SETTINGS] =
    {
        50,
        100,
        200,
    };  // end of difficulty settings
char difficulty_names[DIFFICULTY_SETTINGS][80] =
    {
        "Skirmish",
        "Battle",
        "Slaughter",
    };  // end of difficulty names

void picker_main(Sint32 argc, char  **argv)
{
	Sint32 i;

	for (i=0; i < MAX_BUTTONS; i++)
		allbuttons[i] = NULL;

	// Get main dir ..
	//strcpy(main_dir, "");

	// Set backdrops to NULL
	for (i=0; i < 5; i++)
		backdrops[i] = NULL;

	backpics[0] = read_pixie_file("mainul.pix");
	backpics[1] = read_pixie_file("mainur.pix");
	backpics[2] = read_pixie_file("mainll.pix");
	backpics[3] = read_pixie_file("mainlr.pix");

	backdrops[0] = new pixieN(backpics[0], myscreen);
	backdrops[0]->setxy(0, 0);
	backdrops[1] = new pixieN(backpics[1], myscreen);
	backdrops[1]->setxy(160, 0);
	backdrops[2] = new pixieN(backpics[2], myscreen);
	backdrops[2]->setxy(0, 100);
	backdrops[3] = new pixieN(backpics[3], myscreen);
	backdrops[3]->setxy(160, 100);


	if (!myscreen)
		myscreen = new screen(1);
	myscreen->viewob[0]->resize(PREF_VIEW_FULL);
	mytext = new text(myscreen);

	myscreen->clearbuffer();
	myscreen->clearscreen();

	//main_title_logo_data = read_pixie_file("glad.pix");
	main_title_logo_data = read_pixie_file("title.pix"); // marbled gladiator title
	main_title_logo_pix = new pixieN(main_title_logo_data, myscreen);


	//main_columns_data = read_pixie_file("mage.pix");
	main_columns_data = read_pixie_file("columns.pix");
	main_columns_pix = new pixieN(main_columns_data, myscreen);

	// Get the mouse, timer, & keyboard ..
	grab_mouse();
	grab_timer();
	grab_keyboard();
	clear_keyboard();

	// Load the current saved game, if it exists .. (save0.gtl)
	SDL_RWops* loadgame = open_read_file("save/", "save0.gtl");
	if (loadgame)
	{
	    SDL_RWclose(loadgame);
		myscreen->save_data.load("save0");
	}

	mainmenu(1);
}

void picker_quit()
{
	int i;

	for (i = 0; i < 5; i ++)
	{
		if (backdrops[i])
		{
			delete backdrops[i];
			backdrops[i] = NULL;
		}
		
        backpics[i].free();
	}

	for (i = 0; i < MAX_BUTTONS; i++)
	{
		if (allbuttons[i])
			delete allbuttons[i];
	}

	delete mytext;
	delete myscreen;
	delete main_columns_pix;
	main_columns_data.free();
	delete main_title_logo_pix;
	main_title_logo_data.free();

#if 0
	if (cfgfile)
		cfgfile = NULL;
#endif
}

button buttons1[] =
    {
        { "", SDLK_b, 80, 50, 140, 20, BEGINMENU, -1 }, // BEGIN NEW GAME
        { "CONTINUE GAME", SDLK_c, 80, 75, 140, 20, CREATE_TEAM_MENU, -1 },

        { "4 PLAYER", SDLK_4, 152,125,68,20, SET_PLAYER_MODE, 4 },
        { "3 PLAYER", SDLK_3, 80,125,68,20, SET_PLAYER_MODE,3 },
        { "2 PLAYER", SDLK_2, 152,100,68,20, SET_PLAYER_MODE,2 },
        { "1 PLAYER", SDLK_1, 80,100,68,20, SET_PLAYER_MODE,1 },

        { "DIFFICULTY", SDLK_d, 80, 148, 140, 10, SET_DIFFICULTY, -1},

        { "PVP: Allied", SDLK_p, 80, 160, 68, 10, ALLIED_MODE, -1},
        { "Level Edit", SDLK_l, 152, 160, 68, 10, DO_LEVEL_EDIT, -1},

        { "QUIT", SDLK_ESCAPE, 80, 175, 140, 20, QUIT_MENU, -1 },
    };

button bteam[] =
    {
        { "VIEW TEAM", SDLK_v, 30, 70, 80, 15, CREATE_VIEW_MENU, -1},
        { "TRAIN TEAM", SDLK_t, 120, 70, 80, 15, CREATE_EDIT_MENU, -1},
        { "Hire Troops",  SDLK_h, 210, 70, 80, 15, CREATE_BUY_MENU, -1},
        { "LOAD TEAM", SDLK_l, 30, 100, 80, 15, CREATE_LOAD_MENU, -1},
        { "SAVE TEAM", SDLK_s, 120, 100, 80, 15, CREATE_SAVE_MENU, -1},
        { "GO", SDLK_g,        210, 100, 80, 15, GO_MENU, -1},

        { "QUIT", SDLK_ESCAPE, 30, 140, 60, 30, RETURN_MENU, EXIT},
        { "SET LEVEL", SDLK_e, 210, 140, 80, 20, DO_SET_SCEN_LEVEL, EXIT},
        { "SET CAMPAIGN", SDLK_c, 210, 170, 80, 20, DO_PICK_CAMPAIGN, EXIT},

    };

button viewteam[] =
    {
        //  { "TRAIN", SDLK_e, 85, 170, 60, 20, CREATE_EDIT_MENU, -1},
        //  { "HIRE",  SDLK_b, 190, 170, 60, 20, CREATE_BUY_MENU, -1},
        { "GO", SDLK_g,        270, 170, 40, 20, GO_MENU, -1},
        { "ESC", SDLK_ESCAPE,    10, 170, 44, 20, RETURN_MENU , EXIT},

    };

button detailed[] =
    {
        { "ESC", SDLK_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT},
    };

button editteam[] =
    {
        { "PREV", SDLK_p,  10, 40, 40, 20, CYCLE_TEAM_GUY, -1},
        { "NEXT", SDLK_n,  110, 40, 40, 20, CYCLE_TEAM_GUY, 1},
        { "", SDLK_s,  16, 70, 16, 10, DECREASE_STAT, BUT_STR},
        { "", SDLK_s,  126, 70, 16, 12, INCREASE_STAT, BUT_STR},
        { "", SDLK_d,  16, 85, 16, 10, DECREASE_STAT, BUT_DEX},
        { "", SDLK_d,  126, 85, 16, 12, INCREASE_STAT, BUT_DEX},
        { "", SDLK_c,  16, 100, 16, 10, DECREASE_STAT, BUT_CON},
        { "", SDLK_c,  126,100, 16, 12, INCREASE_STAT, BUT_CON},
        { "", SDLK_i,  16, 115, 16, 10, DECREASE_STAT, BUT_INT},
        { "", SDLK_i,  126, 115, 16, 12, INCREASE_STAT, BUT_INT},
        { "", SDLK_a,  16, 130, 16, 10, DECREASE_STAT, BUT_ARMOR},
        { "", SDLK_a,  126, 130, 16, 12, INCREASE_STAT, BUT_ARMOR},
        { "", SDLK_l,  16, 145, 16, 10, DECREASE_STAT, BUT_LEVEL},
        { "", SDLK_l,  126, 145, 16, 12, INCREASE_STAT, BUT_LEVEL},
        { "VIEW TEAM", SDLK_v,  190, 170, 90, 20, CREATE_VIEW_MENU, -1},
        { "ACCEPT", SDLK_a,  80, 170, 80, 20, EDIT_GUY, -1},
        { "RENAME", SDLK_r, 174,  8, 64, 22, NAME_GUY, 1},
        { "DETAILS..", SDLK_d, 240, 8, 64, 22, CREATE_DETAIL_MENU, 0},
        { "Playing on Team X", SDLK_t, 174, 138, 133, 22, CHANGE_TEAM, 1},
        { "ESC", SDLK_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT},

    };

button buyteam[] =
    {
        { "PREV", SDLK_p,  10, 40, 40, 20, CYCLE_GUY, -1},
        { "NEXT", SDLK_n,  110, 40, 40, 20, CYCLE_GUY, 1},
        { "", SDLK_s,  16, 70, 16, 10, DECREASE_STAT, BUT_STR},
        { "", SDLK_s,  126, 70, 16, 12, INCREASE_STAT, BUT_STR},
        { "", SDLK_d,  16, 85, 16, 10, DECREASE_STAT, BUT_DEX},
        { "", SDLK_d,  126, 85, 16, 12, INCREASE_STAT, BUT_DEX},
        { "", SDLK_c,  16, 100, 16, 10, DECREASE_STAT, BUT_CON},
        { "", SDLK_c,  126,100, 16, 12, INCREASE_STAT, BUT_CON},
        { "", SDLK_i,  16, 115, 16, 10, DECREASE_STAT, BUT_INT},
        { "", SDLK_i,  126, 115, 16, 12, INCREASE_STAT, BUT_INT},
        { "", SDLK_a,  16, 130, 16, 10, DECREASE_STAT, BUT_ARMOR},
        { "", SDLK_a,  126, 130, 16, 12, INCREASE_STAT, BUT_ARMOR},
        { "", SDLK_l,  16, 145, 16, 10, DECREASE_STAT, BUT_LEVEL},
        { "", SDLK_l,  126, 145, 16, 12, INCREASE_STAT, BUT_LEVEL},
        { "VIEW TEAM", SDLK_v,  190, 170, 90, 20, CREATE_VIEW_MENU, -1},
        { "HIRE ME", SDLK_h,  80, 170, 80, 20, ADD_GUY, -1},
        { "Select Team", SDLK_t, 170, 130, 130, 20, CHANGE_HIRE_TEAM, 1},
        { "ESC", SDLK_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT},

    };


button saveteam[] =
    {
        { "SLOT ONE", SDLK_1,  25, 25, 220, 10, DO_SAVE, 1},
        { "SLOT TWO", SDLK_2,  25, 40, 220, 10, DO_SAVE, 2},
        { "SLOT THREE", SDLK_3,25, 55, 220, 10, DO_SAVE, 3},
        { "SLOT FOUR", SDLK_4, 25, 70, 220, 10, DO_SAVE, 4},
        { "SLOT FIVE", SDLK_5, 25, 85, 220, 10, DO_SAVE, 5},
        { "SLOT Six", SDLK_6, 25, 100, 220, 10, DO_SAVE,  6},
        { "SLOT Seven", SDLK_7, 25, 115, 220, 10, DO_SAVE, 7},
        { "SLOT Eight", SDLK_8, 25, 130, 220, 10, DO_SAVE, 8},
        { "SLOT Nine", SDLK_9, 25, 145, 220, 10, DO_SAVE, 9},
        { "SLOT Ten", SDLK_0, 25, 160, 220, 10, DO_SAVE, 10},
        { "ESC", SDLK_ESCAPE,25, 175, 40, 20, RETURN_MENU , EXIT},

    };

button loadteam[] =
    {
        { "SLOT ONE", SDLK_1,  25, 25, 220, 10, DO_LOAD, 1},
        { "SLOT TWO", SDLK_2,  25, 40, 220, 10, DO_LOAD, 2},
        { "SLOT THREE", SDLK_3,25, 55, 220, 10, DO_LOAD, 3},
        { "SLOT FOUR", SDLK_4, 25, 70, 220, 10, DO_LOAD, 4},
        { "SLOT FIVE", SDLK_5, 25, 85, 220, 10, DO_LOAD, 5},
        { "SLOT Six", SDLK_6, 25, 100, 220, 10, DO_LOAD,  6},
        { "SLOT Seven", SDLK_7, 25, 115, 220, 10, DO_LOAD, 7},
        { "SLOT Eight", SDLK_8, 25, 130, 220, 10, DO_LOAD, 8},
        { "SLOT Nine", SDLK_9, 25, 145, 220, 10, DO_LOAD, 9},
        { "SLOT Ten", SDLK_0, 25, 160, 220, 10, DO_LOAD, 10},
        { "ESC", SDLK_ESCAPE,25, 175, 40, 20, RETURN_MENU , EXIT},

    };


button yes_or_no_buttons[] =
    {
        { "YES", SDLK_1,  70, 130, 50, 20, YES_OR_NO, YES},
        { "NO", SDLK_2,  320-50-70, 130, 50, 20, YES_OR_NO, NO}
    };

button popup_dialog_buttons[] =
    {
        { "OK", SDLK_1,  160 - 25, 130, 50, 20, YES_OR_NO, YES}
    };

Sint32 leftmouse()
{
	Sint32 i = 0;
	Sint32 somebutton = -1;
	const Uint8* mousekeys = query_keyboard();

	grab_mouse();
	mymouse = query_mouse();

	while (allbuttons[i])
	{
		allbuttons[i]->mouse_on();
		if (mousekeys[allbuttons[i]->hotkey])
			somebutton = i;
		i++;
	}

	if (somebutton != -1)
	{
		return 1;  // simulate left-click
	}

	if (mymouse[MOUSE_LEFT])
	{
		while (mymouse[MOUSE_LEFT]) // wait for release
			mymouse = query_mouse();
		return 1;
	}
	else if (mymouse[MOUSE_RIGHT])
	{
		while (mymouse[MOUSE_RIGHT]) // wait for release
			mymouse = query_mouse();
		return 2; // for right-mouse
	}
	else
		return 0;
}

void view_team(short left, short top, short right, short bottom)
{
	char text_down = top+3;
	int i;
	char message[30], namecolor, numguys = 0;
	text mytext(myscreen);

	myscreen->redrawme = 1;
	myscreen->draw_button(left, top, right, bottom, 2, 1);

	strcpy(message, "  Name  ");
	mytext.write_xy(left+5, text_down, message, (unsigned char) BLACK, 1);

	strcpy (message, "STR  DEX  CON  INT  ARM");
	mytext.write_xy(left+80, text_down, message, (unsigned char) BLACK, 1);

	sprintf (message, "Level");
	mytext.write_xy(left+230, text_down, message, (unsigned char) BLACK, 1);

	text_down+=6;

	for(i=0; i < myscreen->save_data.team_size; i++)
	{
	    guy** ourteam = myscreen->save_data.team_list;
		if (ourteam[i])
		{
			numguys++;

			strcpy(message, ourteam[i]->name);
			// Pick a nice dark color based on family type
			namecolor = ((ourteam[i]->family +1) << 4) & 255;
			mytext.write_xy(left+5, text_down, message, (unsigned char)namecolor, 1);

			sprintf (message, "%4d %4d %4d %4d %4d",
			         ourteam[i]->strength, ourteam[i]->dexterity,
			         ourteam[i]->constitution, ourteam[i]->intelligence,
			         ourteam[i]->armor);
			mytext.write_xy(left+70, text_down, message, (unsigned char) BLACK, 1);

			sprintf (message, "%2d", ourteam[i]->level);
			mytext.write_xy(left+235, text_down, message, (unsigned char) BLACK, 1);

			family_name_copy(message, ourteam[i]->family);
			mytext.write_xy(left+260, text_down, message, (unsigned char)namecolor, 1);

			text_down+=6;
		}
	}
	if (numguys == 0)
	{
		strcpy(message, "*** YOU HAVE NO TEAM! ***");
		mytext.write_xy(left+80, 60, message, (unsigned char)ORANGE_START, 1);
	}

	return;
}



Sint32 mainmenu(Sint32 arg1)
{
	vbutton *tempbuttons;
	Sint32 retvalue=0;
	Sint32 count;
	char message[80];

	if (arg1)
		arg1 = 1;

	// Set screen to black, to non-display
	//buffers: PORT:  for(i=0;i<256;i++)
	//buffers: PORT: set_palette_reg((unsigned char)i, 0, 0, 0);
	//buffers: PORT:  load_palette("our.pal", (char *)mypalette);

	if (localbuttons != NULL)
		delete localbuttons; //we'll make a new set

	localbuttons = buttonmenu_no_backdrop(buttons1, 10, 0);
	myscreen->clearbuffer();
	allbuttons[0]->set_graphic(FAMILY_NORMAL1);

	tempbuttons = localbuttons;
	count = 0;
	if (myscreen->save_data.numplayers==4)
	{
		allbuttons[2]->do_outline = 1;
		allbuttons[3]->do_outline = 0;
		allbuttons[4]->do_outline = 0;
		allbuttons[5]->do_outline = 0;
	}
	else if (myscreen->save_data.numplayers==3)
	{
		allbuttons[2]->do_outline = 0;
		allbuttons[3]->do_outline = 1;
		allbuttons[4]->do_outline = 0;
		allbuttons[5]->do_outline = 0;
	}
	else if (myscreen->save_data.numplayers==2)
	{
		allbuttons[2]->do_outline = 0;
		allbuttons[3]->do_outline = 0;
		allbuttons[4]->do_outline = 1;
		allbuttons[5]->do_outline = 0;
	}
	else
	{
		allbuttons[2]->do_outline = 0;
		allbuttons[3]->do_outline = 0;
		allbuttons[4]->do_outline = 0;
		allbuttons[5]->do_outline = 1;
	}
	sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
	strcpy(allbuttons[6]->label, message);

	// Show the allied mode
	if (myscreen->save_data.allied_mode)
		strcpy(allbuttons[7]->label, "PVP: Ally");
	else
		strcpy(allbuttons[7]->label, "PVP: Enemy");

	while (allbuttons[count])
	{
		allbuttons[count]->vdisplay();
		count++;
	}

	if (localbuttons == NULL)
		return 1;

	main_title_logo_pix->set_frame(0);
	main_title_logo_pix->drawMix(15,  8, myscreen->viewob[0]);
	main_title_logo_pix->set_frame(1);
	main_title_logo_pix->drawMix(151,  8, myscreen->viewob[0]);
	main_columns_pix->set_frame(0);
	main_columns_pix->drawMix(12,40, myscreen->viewob[0]);
	main_columns_pix->set_frame(1);
	main_columns_pix->drawMix(242,40, myscreen->viewob[0]);
	//myscreen->refresh();

	clear_keyboard();
	reset_timer();
	while (query_timer() < 1);
	// Zardus: PORT: fade from black
	myscreen->fadeblack(1);

	grab_mouse();

	while ( !(retvalue & EXIT) )
	{
		myscreen->buffer_to_screen(0,0,320,200);
		if (leftmouse())
		{
			//myscreen->soundp->play_sound(SOUND_BOW);
			retvalue = localbuttons->leftclick();
			if (localbuttons && (retvalue == REDRAW))
			{
				myscreen->clearbuffer();
				delete(localbuttons);
				localbuttons = buttonmenu_no_backdrop(buttons1, 10, 0);
				
				myscreen->clearfontbuffer();
				
				count = 0;
				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[6]->label, message);

				// Show the allied mode
				if (myscreen->save_data.allied_mode)
					strcpy(allbuttons[7]->label, "PVP: Ally");
				else
					strcpy(allbuttons[7]->label, "PVP: Enemy");

				while (allbuttons[count])
				{
					allbuttons[count]->vdisplay();
					count++;
				}
				allbuttons[0]->set_graphic(FAMILY_NORMAL1);
				retvalue = 0;
				tempbuttons = localbuttons;
				count = 0;

				release_mouse();
				main_title_logo_pix->set_frame(0);
				main_title_logo_pix->drawMix(15,  8, myscreen->viewob[0]);
				main_title_logo_pix->set_frame(1);
				main_title_logo_pix->drawMix(151,  8, myscreen->viewob[0]);
				main_columns_pix->set_frame(0);
				main_columns_pix->drawMix(12,40, myscreen->viewob[0]);
				main_columns_pix->set_frame(1);
				main_columns_pix->drawMix(242,40, myscreen->viewob[0]);
				//main_columns_pix->next_frame();


				if (myscreen->save_data.numplayers==4)
				{
					allbuttons[2]->do_outline = 1;
					allbuttons[3]->do_outline = 0;
					allbuttons[4]->do_outline = 0;
					allbuttons[5]->do_outline = 0;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}
				else if (myscreen->save_data.numplayers==3)
				{
					allbuttons[2]->do_outline = 0;
					allbuttons[3]->do_outline = 1;
					allbuttons[4]->do_outline = 0;
					allbuttons[5]->do_outline = 0;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}
				else if (myscreen->save_data.numplayers==2)
				{
					allbuttons[2]->do_outline = 0;
					allbuttons[3]->do_outline = 0;
					allbuttons[4]->do_outline = 1;
					allbuttons[5]->do_outline = 0;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}
				else
				{
					allbuttons[2]->do_outline = 0;
					allbuttons[3]->do_outline = 0;
					allbuttons[4]->do_outline = 0;
					allbuttons[5]->do_outline = 1;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}

				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[6]->label, message);

				// Show the allied mode
				if (myscreen->save_data.allied_mode)
					strcpy(allbuttons[7]->label, "PVP: Ally");
				else
					strcpy(allbuttons[7]->label, "PVP: Enemy");

				//myscreen->refresh();
				grab_mouse();

			}
			if (localbuttons && retvalue == OK)
			{
				delete(localbuttons);
				localbuttons = buttonmenu_no_backdrop(buttons1, 10, 0);
				
				tempbuttons = localbuttons;
				count = 0;
				
				main_title_logo_pix->set_frame(0);
				main_title_logo_pix->drawMix(15,  8, myscreen->viewob[0]);
				main_title_logo_pix->set_frame(1);
				main_title_logo_pix->drawMix(151,  8, myscreen->viewob[0]);
				main_columns_pix->set_frame(0);
				main_columns_pix->drawMix(12,40, myscreen->viewob[0]);
				main_columns_pix->set_frame(1);
				main_columns_pix->drawMix(242,40, myscreen->viewob[0]);
				//main_columns_pix->next_frame();
				
				if (myscreen->save_data.numplayers==4)
				{
					allbuttons[2]->do_outline = 1;
					allbuttons[3]->do_outline = 0;
					allbuttons[4]->do_outline = 0;
					allbuttons[5]->do_outline = 0;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}
				else if (myscreen->save_data.numplayers==3)
				{
					allbuttons[2]->do_outline = 0;
					allbuttons[3]->do_outline = 1;
					allbuttons[4]->do_outline = 0;
					allbuttons[5]->do_outline = 0;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}
				else if (myscreen->save_data.numplayers==2)
				{
					allbuttons[2]->do_outline = 0;
					allbuttons[3]->do_outline = 0;
					allbuttons[4]->do_outline = 1;
					allbuttons[5]->do_outline = 0;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}
				else
				{
					allbuttons[2]->do_outline = 0;
					allbuttons[3]->do_outline = 0;
					allbuttons[4]->do_outline = 0;
					allbuttons[5]->do_outline = 1;
					allbuttons[2]->vdisplay();
					allbuttons[3]->vdisplay();
					allbuttons[4]->vdisplay();
					allbuttons[5]->vdisplay();
				}

				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[6]->label, message);

				// Show the allied mode
				if (myscreen->save_data.allied_mode)
					sprintf(message, "PVP: Ally");
				else
					sprintf(message, "PVP: Enemy");
				strcpy(allbuttons[7]->label, message);

				tempbuttons = localbuttons;
				count = 0;
				myscreen->clearfontbuffer();
				while (allbuttons[count])
				{
					allbuttons[count]->vdisplay();
					count++;
				}
				allbuttons[0]->set_graphic(FAMILY_NORMAL1);
			} // end of "OK" buttons
		}
	}
	delete tempbuttons;
	return retvalue;
}

Sint32 beginmenu(Sint32 arg1)
{
	Sint32 i,retvalue=0;

	if (arg1)
		arg1 = 1;
	if (localbuttons != NULL)
		delete localbuttons; //we'll make a new set

	myscreen->clear();

	// Starting new game ..
	release_mouse();
	//grab_keyboard();
	myscreen->clearbuffer();
	myscreen->swap();
	read_help("start.tex", myscreen);
	//release_keyboard();
	myscreen->refresh();
	grab_mouse();
	myscreen->clear();

	localbuttons = buttonmenu(bteam, 9);

	myscreen->swap();

	if (localbuttons == NULL)
		return 1;


    // Reset the save data so we have a fresh, new team
	myscreen->save_data.reset();
	current_guy = NULL;
	
	// Clear the labeling counter
	for (i=0; i < NUM_FAMILIES; i++)
		numbought[i] = 0;

	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
		{
			retvalue=localbuttons->leftclick();
		}

		if (localbuttons && (retvalue == REDRAW))
		{
			myscreen->clearbuffer();
			delete(localbuttons);
			localbuttons = buttonmenu(bteam, 9);
			myscreen->swap();
			retvalue = 0;
		}

	}
	return REDRAW;
}

button bload[] =
    {
        { "START NEW TEAM", SDLK_s, 100, 70, 120, 15, NULLMENU, -1},
        { "LOAD A TEAM", SDLK_l, 100, 100, 120, 15, NULLMENU, -1},
        { "MAIN MENU", SDLK_ESCAPE, 100, 130, 120, 15, 0 , -1},
    };

Sint32 loadmenu(Sint32 arg1)
{
	Sint32 retvalue = 0;

	if (arg1)
		arg1 = 1;

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(bload, 3);

	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
			retvalue=localbuttons->leftclick();

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			localbuttons = buttonmenu(bload, 3);
			retvalue = 0;
		}

	}
	return REDRAW;
}

button bnew[] =
    {
        { "A", SDLK_a, 100, 70, 15, 15, NULLMENU, -1},
        { "B", SDLK_b, 120, 70, 15, 15, NULLMENU, -1 },
        { "C", SDLK_c, 140, 70, 15, 15, NULLMENU, -1 },
        { "D", SDLK_d, 160, 70, 15, 15, NULLMENU, -1 },
        { "E", SDLK_e, 180, 70, 15, 15, NULLMENU, -1 },
        { "F", SDLK_f, 200, 70, 15, 15, NULLMENU, -1 },
        { "G", SDLK_g, 100, 90, 15, 15, NULLMENU, -1},
        { "H", SDLK_h, 120, 90, 15, 15, NULLMENU, -1 },
        { "I", SDLK_i, 140, 90, 15, 15, NULLMENU, -1 },
        { "J", SDLK_j, 160, 90, 15, 15, NULLMENU, -1 },
        { "K", SDLK_k, 180, 90, 15, 15, NULLMENU, -1 },
        { "L", SDLK_l, 200, 90, 15, 15, NULLMENU, -1 },
        { "M", SDLK_m, 100, 110, 15, 15, NULLMENU, -1},
        { "N", SDLK_n, 120, 110, 15, 15, NULLMENU, -1 },
        { "O", SDLK_o, 140, 110, 15, 15, NULLMENU, -1 },
        { "P", SDLK_p, 160, 110, 15, 15, NULLMENU, -1 },
        { "Q", SDLK_q, 180, 110, 15, 15, NULLMENU, -1 },
        { "R", SDLK_r, 200, 110, 15, 15, NULLMENU, -1 },
        { "BACK", SDLK_ESCAPE, 100, 130, 115, 20, 0, -1 },
    };

Sint32 newmenu(Sint32 arg1)
{
	Sint32 retvalue = 0;

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(bnew, 19);

	if (arg1)
		arg1 = 1;
	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
			retvalue=localbuttons->leftclick();

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			localbuttons = buttonmenu(bnew, 19);
			retvalue = 0;
		}
	}
	return REDRAW;
}


button bnull[] =
    {
        { "BACK", SDLK_ESCAPE, 100, 80, 120, 30, 0, -1 },
    };

Sint32 nullmenu(Sint32 arg1)
{
	Sint32 retvalue = 0;
	if (arg1)
		arg1 = 1;

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(bnull, 1);

	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
			retvalue=localbuttons->leftclick();

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			localbuttons = buttonmenu(bnull, 1);
			retvalue = 0;
		}
	}
	return REDRAW;
}

void family_name_copy(char *name, short family)
{
	switch(family)
	{
		case FAMILY_ARCHER:
			strcpy(name, "ARCHER");
			break;
		case FAMILY_CLERIC:
			strcpy(name, "CLERIC");
			break;
		case FAMILY_DRUID:
			strcpy(name, "DRUID");
			break;
		case FAMILY_ELF:
			strcpy(name, "ELF");
			break;
		case FAMILY_MAGE:
			strcpy(name, "MAGE");
			break;
		case FAMILY_SOLDIER:
			strcpy(name, "SOLDIER");
			break;
		case FAMILY_THIEF:
			strcpy(name, "THIEF");
			break;
		case FAMILY_ARCHMAGE:
			strcpy(name, "ARCHMAGE");
			break;
		case FAMILY_ORC:
			strcpy(name, "ORC");
			break;
		case FAMILY_BIG_ORC:
			strcpy(name, "ORC CAP.");
			break;
		case FAMILY_BARBARIAN:
			strcpy(name, "BARBAR.");
			break;
		default:
			strcpy(name, "BEAST");
	}
}

Sint32 create_team_menu(Sint32 arg1)
{
	Sint32 retvalue=0;

	if (arg1)
		arg1 = 1;

	if (localbuttons)
		delete localbuttons;

	myscreen->fadeblack(0);
	myscreen->clearfontbuffer();

	//  myscreen->clear();
	localbuttons = buttonmenu(bteam,9);
	
	myscreen->fadeblack(1);

	myscreen->buffer_to_screen(0,0,320,200);
	
	

	//myscreen->soundp->play_sound(SOUND_CHARGE);
	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
		{
			//myscreen->soundp->play_sound(SOUND_BOW);
			retvalue=localbuttons->leftclick();
		}

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			//      myscreen->clear();
			myscreen->clearfontbuffer();
			localbuttons = buttonmenu(bteam, 9);
			myscreen->buffer_to_screen(0,0,320,200);
			retvalue = 0;
		}

	}

	myscreen->clearfontbuffer();
	
	return REDRAW;
}

Sint32 create_view_menu(Sint32 arg1)
{
	Sint32 retvalue = 0;

	if (arg1)
		arg1 = 1;

	myscreen->clearbuffer();

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(viewteam, 2); // used to be 4

	release_mouse();
	view_team(5,5,314, 160);
	myscreen->refresh();
	grab_mouse();

	while ( !(retvalue & EXIT) )
	{

		if (leftmouse())
			retvalue=localbuttons->leftclick();

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			localbuttons = buttonmenu(viewteam, 2); // was 4
			retvalue = 0;

			release_mouse();
			view_team(5,5,314, 160);
			grab_mouse();

		}
	}
	myscreen->clearbuffer();

	return REDRAW;
}

Sint32 create_buy_menu(Sint32 arg1)
{
	Sint32 linesdown, retvalue = 0;
	const Uint8* inputkeyboard = query_keyboard();
	Sint32 i;
	Sint32 start_time = query_timer();
	unsigned char showcolor; // normally STAT_COLOR or STAT_CHANGED
	Uint32 current_cost;
	Sint32 clickvalue;

#define STAT_LEFT    44        // where 'INT:' appears
#define STAT_NUM     86        // where '12' appears
#define STAT_COLOR   DARK_BLUE // color for normal stat text
#define STAT_CHANGED RED       // color for changed stat text

	if (arg1)
		arg1 = 1;

	myscreen->clearbuffer();
	//myscreen->clearscreen();

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(buyteam, 18);
	for (i=2; i < 14; i++)
	{
		if (!(i%2)) // 2, 4, ..., 12
			allbuttons[i]->set_graphic(FAMILY_MINUS);
		else
			allbuttons[i]->set_graphic(FAMILY_PLUS);
	}

	// Update our team-number display ..
	change_hire_teamnum(0);
	cycle_guy(0);
	myscreen->draw_button(174, 20, 306, 42, 1, 1); // text box

	linesdown = 0;
	release_mouse();
	myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
	myscreen->draw_text_bar(36, 10, 124, 22);
	myscreen->draw_button(38, 66, 120, 160, 1, 1); // stats box
	myscreen->draw_text_bar(42, 70, 116, 156);
	mytext->write_xy(38, 14, current_guy->name, (unsigned char) DARK_BLUE, 1);

	// Strength
	sprintf(message, "%d", current_guy->strength);
	mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  STR:",
	                 (unsigned char) STAT_COLOR, 1);
	if (statlist[(int)current_guy->family][BUT_STR] < current_guy->strength)
		showcolor = STAT_CHANGED;
	else
		showcolor = STAT_COLOR;
	mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

	// Dexterity
	sprintf(message, "%d", current_guy->dexterity);
	mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  DEX:",
	                 (unsigned char) STAT_COLOR, 1);
	if (statlist[(int)current_guy->family][BUT_DEX] < current_guy->dexterity)
		showcolor = STAT_CHANGED;
	else
		showcolor = STAT_COLOR;
	mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

	// Constitution
	sprintf(message, "%d", current_guy->constitution);
	mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  CON:",
	                 (unsigned char) STAT_COLOR, 1);
	if (statlist[(int)current_guy->family][BUT_CON] < current_guy->constitution)
		showcolor = STAT_CHANGED;
	else
		showcolor = STAT_COLOR;
	mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

	// Intelligence
	sprintf(message, "%d", current_guy->intelligence);
	mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  INT:",
	                 (unsigned char) STAT_COLOR, 1);
	if (statlist[(int)current_guy->family][BUT_INT] < current_guy->intelligence)
		showcolor = STAT_CHANGED;
	else
		showcolor = STAT_COLOR;
	mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

	// Armor
	sprintf(message, "%d", current_guy->armor);
	mytext->write_xy(STAT_LEFT, DOWN(linesdown), "ARMOR:",
	                 (unsigned char) STAT_COLOR, 1);
	if (statlist[(int)current_guy->family][BUT_ARMOR] < current_guy->armor)
		showcolor = STAT_CHANGED;
	else
		showcolor = STAT_COLOR;
	mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

	// Level
	sprintf(message, "%d", current_guy->level);
	mytext->write_xy(STAT_LEFT, DOWN(linesdown), "LEVEL:",
	                 (unsigned char) STAT_COLOR, 1);
	if (statlist[(int)current_guy->family][BUT_LEVEL] < current_guy->level)
		showcolor = STAT_CHANGED;
	else
		showcolor = STAT_COLOR;
	mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

	myscreen->draw_button(174, 46, 306, 114, 1, 1); // info box
	myscreen->draw_text_bar(178, 48, 302, 58); // title bar
	strcpy(message, "GAME INFORMATION");
	mytext->write_xy(240 - (strlen(message)*6/2), 51, message, (unsigned char) RED, 1);
	myscreen->draw_text_bar(178, 60, 302, 110); // main text box
	myscreen->draw_text_bar(188, 94, 292, 95); // dividing line
	sprintf(message, "CASH: %u", myscreen->save_data.m_totalcash[current_team_num]);
	mytext->write_xy(180, 62, message,(unsigned char) DARK_BLUE, 1);
	current_cost = calculate_cost();
	mytext->write_xy(180, 72, "COST: ", DARK_BLUE, 1);
	sprintf(message, "      %u", current_cost );
	if (current_cost > myscreen->save_data.m_totalcash[current_team_num])
		mytext->write_xy(180, 72, message, STAT_CHANGED, 1);
	else
		mytext->write_xy(180, 72, message, STAT_COLOR, 1);

	sprintf(message, "TOTAL SCORE: %u", myscreen->save_data.m_totalscore[current_team_num]);
	mytext->write_xy(180, 102, message,(unsigned char) DARK_BLUE, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	while ( !(retvalue & EXIT) )
	{
		show_guy(query_timer()-start_time, 0); // 0 means current_guy
		clickvalue = leftmouse();
		if (clickvalue == 1)
			retvalue=localbuttons->leftclick();
		else if (clickvalue == 2)
			retvalue=localbuttons->rightclick();

		if (inputkeyboard[KEYSTATE_LCTRL])
		{
			if (inputkeyboard[KEYSTATE_KP_PLUS])
			{
				myscreen->save_data.m_totalcash[current_team_num] += 1000;
				retvalue = OK;
			}
			if (inputkeyboard[KEYSTATE_KP_MINUS])
			{
				myscreen->save_data.m_totalcash[current_team_num] -= 1000;
				retvalue = OK;
			}
		}

		if (localbuttons && ( (retvalue == REDRAW)||(retvalue == OK) ) )
		{
			if (!current_guy)
				cycle_guy(0);
			if (retvalue == REDRAW)
			{
				delete(localbuttons);
				//myscreen->clear();
				myscreen->clearfontbuffer();
				localbuttons = buttonmenu(buyteam, 18);
				for (i=2; i < 14; i++)
				{
					if (!(i%2)) // 2, 4, ..., 12
						allbuttons[i]->set_graphic(FAMILY_MINUS);
					else
						allbuttons[i]->set_graphic(FAMILY_PLUS);
				}
				cycle_guy(0);

				change_hire_teamnum(0);
				myscreen->draw_button(174, 20, 306, 42, 1, 1); // text box
				myscreen->buffer_to_screen(0,0,320,200);
			}
			retvalue = 0;
			linesdown = 0;
			release_mouse();

			myscreen->clearfontbuffer(174, 20, 306-174, 42-20);
			myscreen->clearfontbuffer(174,46,306-174,114-26);

			myscreen->draw_button(174, 46, 306, 114, 1, 1); // info box
			myscreen->draw_text_bar(178, 48, 302, 58); // title bar
			strcpy(message, "GAME INFORMATION");
			mytext->write_xy(240 - (strlen(message)*6/2), 51, message, (unsigned char) RED, 1);
			myscreen->draw_text_bar(178, 60, 302, 110); // main text box
			myscreen->draw_text_bar(188, 94, 292, 95); // dividing line
			sprintf(message, "CASH: %u", myscreen->save_data.m_totalcash[current_team_num]);
			mytext->write_xy(180, 62, message,(unsigned char) DARK_BLUE, 1);
			current_cost = calculate_cost();
			mytext->write_xy(180, 72, "COST: ", DARK_BLUE, 1);
			sprintf(message, "      %u", current_cost );
			if (current_cost > myscreen->save_data.m_totalcash[current_team_num])
				mytext->write_xy(180, 72, message, STAT_CHANGED, 1);
			else
				mytext->write_xy(180, 72, message, STAT_COLOR, 1);

			sprintf(message, "TOTAL SCORE: %u", myscreen->save_data.m_totalscore[current_team_num]);
			mytext->write_xy(180, 102, message,(unsigned char) DARK_BLUE, 1);
			myscreen->clearfontbuffer(34,8,126-34,24-8);
			myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
			myscreen->draw_text_bar(36, 10, 124, 22);

			myscreen->clearfontbuffer(38,66,120-38,160-66);
			myscreen->draw_button(38, 66, 120, 160, 1, 1); // stats box
			myscreen->draw_text_bar(42, 70, 116, 156);
			mytext->write_xy(38, 14, current_guy->name, (unsigned char) DARK_BLUE, 1);

			// Strength
			sprintf(message, "%d", current_guy->strength);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  STR:",
			                 (unsigned char) STAT_COLOR, 1);
			if (statlist[(int)current_guy->family][BUT_STR] < current_guy->strength)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Dexterity
			sprintf(message, "%d", current_guy->dexterity);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  DEX:",
			                 (unsigned char) STAT_COLOR, 1);
			if (statlist[(int)current_guy->family][BUT_DEX] < current_guy->dexterity)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Constitution
			sprintf(message, "%d", current_guy->constitution);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  CON:",
			                 (unsigned char) STAT_COLOR, 1);
			if (statlist[(int)current_guy->family][BUT_CON] < current_guy->constitution)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Intelligence
			sprintf(message, "%d", current_guy->intelligence);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  INT:",
			                 (unsigned char) STAT_COLOR, 1);
			if (statlist[(int)current_guy->family][BUT_INT] < current_guy->intelligence)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Armor
			sprintf(message, "%d", current_guy->armor);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "ARMOR:",
			                 (unsigned char) STAT_COLOR, 1);
			if (statlist[(int)current_guy->family][BUT_ARMOR] < current_guy->armor)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Level
			sprintf(message, "%d", current_guy->level);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "LEVEL:",
			                 (unsigned char) STAT_COLOR, 1);
			if (statlist[(int)current_guy->family][BUT_LEVEL] < current_guy->level)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			myscreen->buffer_to_screen(0, 0, 320, 200);
			grab_mouse();
		}
	}
	myscreen->clearbuffer();
	//myscreen->clearscreen();
	return REDRAW;
}

Sint32 create_edit_menu(Sint32 arg1)
{
	guy * here;
	Sint32 linesdown, i, retvalue=0;
	unsigned char showcolor;
	Sint32 start_time = query_timer();
	Uint32 current_cost;
	Sint32 clickvalue;

	if (arg1)
		arg1 = 1;

	if (myscreen->save_data.team_size < 1)
		return OK;

	myscreen->clearbuffer();

	if (localbuttons)
		delete localbuttons;
	localbuttons = buttonmenu(editteam, 20, 0);  // don't refresh yet
	for (i=2; i < 14; i++)
	{
		if (!(i%2)) // 2, 4, ..., 12
			allbuttons[i]->set_graphic(FAMILY_MINUS);
		else
			allbuttons[i]->set_graphic(FAMILY_PLUS);
	}

	// Set to first guy on list using global variable ..
	cycle_team_guy(0);
	here = myscreen->save_data.team_list[editguy];

	linesdown = 0;
	release_mouse();
	myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
	myscreen->draw_text_bar(36, 10, 124, 22);
	mytext->write_xy(80 - mytext->query_width(current_guy->name)/2, 14,
	                 current_guy->name,(unsigned char) DARK_BLUE, 1);

	myscreen->draw_button(38, 66, 120, 160, 1, 1); // stats box
	myscreen->draw_text_bar(42, 70, 116, 156);

	sprintf(message, "  STR: %d", current_guy->strength);
	mytext->write_xy(STAT_LEFT,DOWN(linesdown++),message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "  DEX: %d", current_guy->dexterity);
	mytext->write_xy(STAT_LEFT,DOWN(linesdown++),message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "  CON: %d", current_guy->constitution);
	mytext->write_xy(STAT_LEFT,DOWN(linesdown++),message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "  INT: %d", current_guy->intelligence);
	mytext->write_xy(STAT_LEFT,DOWN(linesdown++),message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "ARMOR: %d", current_guy->armor);
	mytext->write_xy(STAT_LEFT,DOWN(linesdown++),message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "LEVEL: %d", current_guy->level);
	mytext->write_xy(STAT_LEFT,DOWN(linesdown++),message,(unsigned char) DARK_BLUE, 1);

	myscreen->draw_button(174, 32, 306, 114+22, 1, 1); // info box
	myscreen->draw_text_bar(178, 34, 302, 44); // title bar
	strcpy(message, "GAME INFORMATION");
	mytext->write_xy(240 - (strlen(message)*6/2), 37, message, (unsigned char) RED, 1);
	myscreen->draw_text_bar(178, 46, 302, 110+22); // main text box
	myscreen->draw_text_bar(188, 70+22, 292, 71+22); // dividing line #1
	myscreen->draw_text_bar(188, 94+22, 292, 95+22); // dividing line #2
	sprintf(message, "Total Kills: %d", current_guy->kills);
	mytext->write_xy(180, 48, message, DARK_BLUE, 1);
	if (current_guy->kills) // are we a veteran?
	{
		sprintf(message, "Avg. Victim: %.2lf ",
		        (float) ((float)current_guy->level_kills / (float)current_guy->kills) );
		mytext->write_xy(180, 55, message, DARK_BLUE, 1);
		sprintf(message, " Exp / Kill: %u ",
		        (current_guy->exp / current_guy->kills) );
		mytext->write_xy(180, 62, message, DARK_BLUE, 1);
	}
	else
	{
		sprintf(message, "Avg. Victim: N/A ");
		mytext->write_xy(180, 55, message, DARK_BLUE, 1);
		sprintf(message, " Exp / Kill: N/A ");
		mytext->write_xy(180, 62, message, DARK_BLUE, 1);
	}
	if (current_guy->total_hits && current_guy->total_shots) // have we at least hit something? :)
	{
		sprintf(message, " Damage/Hit: %.2lf ",
		        (float) ( (float)current_guy->total_damage / (float)current_guy->total_hits) );
		mytext->write_xy(180, 69, message, DARK_BLUE, 1);
		sprintf(message, "   Accuracy: %d%% ",
		        (current_guy->total_hits*100)/current_guy->total_shots);
		mytext->write_xy(180, 76, message, DARK_BLUE, 1);
	}
	else // haven't ever hit anyone
	{
		sprintf(message, " Damage/Hit: N/A ");
		mytext->write_xy(180, 69, message, DARK_BLUE, 1);
		sprintf(message, "   Accuracy: N/A ");
		mytext->write_xy(180, 76, message, DARK_BLUE, 1);
	}
	sprintf(message, " EXPERIENCE: %u", current_guy->exp);
	mytext->write_xy(180, 62+22, message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "CASH: %u", myscreen->save_data.m_totalcash[current_guy->teamnum]);
	mytext->write_xy(180, 76+22, message,(unsigned char) DARK_BLUE, 1);
	current_cost = calculate_cost(here);
	mytext->write_xy(180, 86+22, "COST: ", DARK_BLUE, 1);
	sprintf(message, "      %u", current_cost );
	if (current_cost > myscreen->save_data.m_totalcash[current_guy->teamnum])
		mytext->write_xy(180, 86+22, message, STAT_CHANGED, 1);
	else
		mytext->write_xy(180, 86+22, message, STAT_COLOR, 1);
	sprintf(message, "TOTAL SCORE: %u", myscreen->save_data.m_totalscore[current_guy->teamnum]);
	mytext->write_xy(180, 102+22, message,(unsigned char) DARK_BLUE, 1);

	// Display our team setting ..
	sprintf(message, "Playing on Team %d", current_guy->teamnum+1);
	strcpy(allbuttons[18]->label, message);
	allbuttons[18]->vdisplay();

	myscreen->buffer_to_screen(0, 0, 320, 200);

	grab_mouse();
	
	guy** ourteam = myscreen->save_data.team_list;

	while ( !(retvalue & EXIT) )
	{
		show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
		clickvalue = leftmouse();
		if (clickvalue)
		{
			if (clickvalue == 1)
				retvalue=localbuttons->leftclick();
			else if (clickvalue == 2)
				retvalue = localbuttons->rightclick();

			if (here != ourteam[editguy])
				here = ourteam[editguy];
			current_cost = calculate_cost(here);
		}

		// Set to current guy ..
		here = ourteam[editguy];

		if (localbuttons && ( (retvalue == REDRAW)||(retvalue == OK) ) )
		{
			if (!current_guy)
				cycle_team_guy(0);
			if (retvalue == REDRAW)
			{
				myscreen->clearfontbuffer();
				
				delete(localbuttons);
				localbuttons = buttonmenu(editteam, 20, 0); // don't redraw yet
				for (i=2; i < 14; i++)
				{
					if (!(i%2)) // 2, 4, ..., 12
						allbuttons[i]->set_graphic(FAMILY_MINUS);
					else
						allbuttons[i]->set_graphic(FAMILY_PLUS);
				}
				cycle_team_guy(0);
			}
			linesdown = 0;
			release_mouse();

			myscreen->clearfontbuffer(34,8,126-34,24-8);
			myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
			myscreen->draw_text_bar(36, 10, 124, 22);
			mytext->write_xy(80 - mytext->query_width(current_guy->name)/2, 14,
			                 current_guy->name,(unsigned char) DARK_BLUE, 1);
			myscreen->clearfontbuffer(38,66,120-38,160-66);
			myscreen->draw_button(38, 66, 120, 160, 1, 1); // stats box
			myscreen->draw_text_bar(42, 70, 116, 156);

			// Strength
			sprintf(message, "%d", current_guy->strength);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  STR:",
			                 (unsigned char) STAT_COLOR, 1);
			if (here->strength < current_guy->strength)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Dexterity
			sprintf(message, "%d", current_guy->dexterity);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  DEX:",
			                 (unsigned char) STAT_COLOR, 1);
			if (here->dexterity < current_guy->dexterity)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Constitution
			sprintf(message, "%d", current_guy->constitution);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  CON:",
			                 (unsigned char) STAT_COLOR, 1);
			if (here->constitution < current_guy->constitution)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Intelligence
			sprintf(message, "%d", current_guy->intelligence);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "  INT:",
			                 (unsigned char) STAT_COLOR, 1);
			if (here->intelligence < current_guy->intelligence)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Armor
			sprintf(message, "%d", current_guy->armor);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "ARMOR:",
			                 (unsigned char) STAT_COLOR, 1);
			if (here->armor < current_guy->armor)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			// Level
			sprintf(message, "%d", current_guy->level);
			mytext->write_xy(STAT_LEFT, DOWN(linesdown), "LEVEL:",
			                 (unsigned char) STAT_COLOR, 1);
			if (here->level < current_guy->level)
				showcolor = STAT_CHANGED;
			else
				showcolor = STAT_COLOR;
			mytext->write_xy(STAT_NUM, DOWN(linesdown++), message, showcolor, 1);

			myscreen->clearfontbuffer(174,32,306-174,(114+22)-32);	
			myscreen->draw_button(174, 32, 306, 114+22, 1, 1); // info box
			myscreen->draw_text_bar(178, 34, 302, 44); // title bar
			strcpy(message, "GAME INFORMATION");
			mytext->write_xy(240 - (strlen(message)*6/2), 37, message, (unsigned char) RED, 1);
			myscreen->draw_text_bar(178, 46, 302, 110+22); // main text box
			myscreen->draw_text_bar(188, 70+22, 292, 71+22); // dividing line #1
			myscreen->draw_text_bar(188, 94+22, 292, 95+22); // dividing line #2
			sprintf(message, "Total Kills: %d", current_guy->kills);
			mytext->write_xy(180, 48, message, DARK_BLUE, 1);
			if (current_guy->kills) // are we a veteran?
			{
				sprintf(message, "Avg. Victim: %.2lf ",
				        (float) ((float)current_guy->level_kills / (float)current_guy->kills) );
				mytext->write_xy(180, 55, message, DARK_BLUE, 1);
				sprintf(message, " Exp / Kill: %u ",
				        (current_guy->exp / current_guy->kills) );
				mytext->write_xy(180, 62, message, DARK_BLUE, 1);
			}
			else
			{
				sprintf(message, "Avg. Victim: N/A ");
				mytext->write_xy(180, 55, message, DARK_BLUE, 1);
				sprintf(message, " Exp / Kill: N/A ");
				mytext->write_xy(180, 62, message, DARK_BLUE, 1);
			}
			if (current_guy->total_hits && current_guy->total_shots) // have we at least hit something? :)
			{
				sprintf(message, " Damage/Hit: %.2lf ",
				        (float) ( (float)current_guy->total_damage / (float)current_guy->total_hits) );
				mytext->write_xy(180, 69, message, DARK_BLUE, 1);
				sprintf(message, "   Accuracy: %d%% ",
				        (current_guy->total_hits*100)/current_guy->total_shots);
				mytext->write_xy(180, 76, message, DARK_BLUE, 1);
			}
			else // haven't ever hit anyone
			{
				sprintf(message, " Damage/Hit: N/A ");
				mytext->write_xy(180, 69, message, DARK_BLUE, 1);
				sprintf(message, "   Accuracy: N/A ");
				mytext->write_xy(180, 76, message, DARK_BLUE, 1);
			}
			sprintf(message, " EXPERIENCE: %u", current_guy->exp);
			mytext->write_xy(180, 62+22, message,(unsigned char) DARK_BLUE, 1);
			sprintf(message, "CASH: %u", myscreen->save_data.m_totalcash[current_guy->teamnum]);
			mytext->write_xy(180, 76+22, message,(unsigned char) DARK_BLUE, 1);
			current_cost = calculate_cost(here);
			mytext->write_xy(180, 86+22, "COST: ", DARK_BLUE, 1);
			sprintf(message, "      %u", current_cost );
			if (current_cost > myscreen->save_data.m_totalcash[current_guy->teamnum])
				mytext->write_xy(180, 86+22, message, STAT_CHANGED, 1);
			else
				mytext->write_xy(180, 86+22, message, STAT_COLOR, 1);
			sprintf(message, "TOTAL SCORE: %u", myscreen->save_data.m_totalscore[current_guy->teamnum]);
			mytext->write_xy(180, 102+22, message,(unsigned char) DARK_BLUE, 1);

			// Display our team setting ..
			myscreen->clearfontbuffer(174, 138, 133, 22); 
			sprintf(message, "Playing on Team %d", current_guy->teamnum+1);
			strcpy(allbuttons[18]->label, message);
			allbuttons[18]->vdisplay();

			myscreen->buffer_to_screen(0, 0, 320, 200);
			grab_mouse();
			retvalue = 0;
		}

	}
	myscreen->clearbuffer();
	//myscreen->clearscreen();
	return REDRAW;
}

Sint32 create_load_menu(Sint32 arg1)
{
	Sint32 retvalue=0;
	Sint32 i;
	char temp_filename[20];
	text loadtext(myscreen);
	char message[80];

	if (arg1)
		arg1 = 1;
	grab_mouse();

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(loadteam, 11, 0);  // don't redraw!

	myscreen->clearfontbuffer();	

	myscreen->draw_button(15,  9, 255, 199, 1, 1);
	myscreen->draw_text_bar(19, 13, 251, 21);
	strcpy(message, "Gladiator: Load Game");
	loadtext.write_xy(135-(strlen(message)*3), 15, message, RED, 1);

	for (i=0; i < 10; i++)
	{
		sprintf(temp_filename, "save%d", i+1);
		strcpy(allbuttons[i]->label, get_saved_name(temp_filename) );
		myscreen->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
		allbuttons[i]->vdisplay();
		myscreen->draw_box(allbuttons[i]->xloc-1,
		                   allbuttons[i]->yloc-1,
		                   allbuttons[i]->xend,
		                   allbuttons[i]->yend, 0, 0, 1);
	}
	myscreen->draw_text_bar(23, allbuttons[10]->yloc-2, 66, allbuttons[10]->yend+1);
	allbuttons[10]->vdisplay();
	myscreen->draw_box(allbuttons[10]->xloc-1,
	                   allbuttons[10]->yloc-1,
	                   allbuttons[10]->xend,
	                   allbuttons[10]->yend, 0, 0, 1);

	myscreen->buffer_to_screen(0, 0, 320, 200);

	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
		{
			retvalue=localbuttons->leftclick();
			if(retvalue == REDRAW)
            {
                return REDRAW;
            }
		}

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			//myscreen->clear();

			myscreen->clearfontbuffer();
			
			localbuttons = buttonmenu(loadteam, 11);
			myscreen->draw_button(15,  9, 255, 175, 1, 1);
			myscreen->draw_text_bar(19, 13, 251, 21);
			strcpy(message, "Gladiator: Load Game");
			loadtext.write_xy(135-(strlen(message)*3), 15, message, RED, 1);
			for (i=0; i < 10; i++)
			{
				sprintf(temp_filename, "save%d", i+1);
				strcpy(allbuttons[i]->label, get_saved_name(temp_filename) );
				myscreen->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
				allbuttons[i]->vdisplay();
				myscreen->draw_box(allbuttons[i]->xloc-1,
				                   allbuttons[i]->yloc-1,
				                   allbuttons[i]->xend,
				                   allbuttons[i]->yend, 0, 0, 1);
			}
			myscreen->draw_text_bar(23, allbuttons[10]->yloc-2, 66, allbuttons[10]->yend+1);
			allbuttons[10]->vdisplay();
			myscreen->draw_box(allbuttons[10]->xloc-1,
			                   allbuttons[10]->yloc-1,
			                   allbuttons[10]->xend,
			                   allbuttons[10]->yend, 0, 0, 1);
			retvalue = 0;
		}
	}
	
	return REDRAW;
}


void timed_dialog(const char* message, float delay_seconds)
{
    Log("%s\n", message);
    
	text gladtext(myscreen);
	
	int pix_per_char = 6;
	int len = strlen(message);
	int width = len * pix_per_char;
    int leftside  = 160 - width/2 - 12;
    int rightside = 160 + width/2 + 12;
    
    myscreen->draw_button(leftside, 80, rightside, 110, 1);
    gladtext.write_xy(160 - width/2, 94, message, (unsigned char) DARK_BLUE, 1);

    myscreen->buffer_to_screen(0, 0, 320, 200); // refresh screen

	grab_mouse();
    clear_keyboard();
    
    clear_key_press_event();
	
	Uint32 start_time = SDL_GetTicks();
	while ((SDL_GetTicks() - start_time)/1000.0f < delay_seconds)
	{
		get_input_events(POLL);
        
        if(query_mouse()[MOUSE_LEFT] || query_key_press_event())
            break;
        
        SDL_Delay(10);
	}
	
	myscreen->clearfontbuffer();
}

// TODO: Multi-line messages would be nice...
bool yes_or_no_prompt(const char* title, const char* message, bool default_value)
{
	text gladtext(myscreen);
	
	int pix_per_char = 3;
    int leftside  = 160 - ( (strlen(message)) * pix_per_char) - 12;
    int rightside = 160 + ( (strlen(message)) * pix_per_char) + 12;
    //buffers: PORT: we will redo this: set_palette(myscreen->redpalette);
    //myscreen->clearfontbuffer(leftside, 80, rightside, 40);
    int dumbcount = myscreen->draw_dialog(leftside, 80, rightside, 120, title);
    gladtext.write_xy(dumbcount + 3*pix_per_char, 104, message, (unsigned char) DARK_BLUE, 1);

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu_no_backdrop(yes_or_no_buttons, 2, 0);  // don't redraw!

    int i;
	for (i=0; i < 2; i++)
	{
		allbuttons[i]->vdisplay();
	}

    myscreen->buffer_to_screen(0, 0, 320, 200); // refresh screen

	grab_mouse();
    clear_keyboard();
    const Uint8* keyboard = query_keyboard();
    
    clear_key_press_event();
	
    int retvalue = 0;
	while (retvalue == 0)
	{
		get_input_events(POLL);
		
		if(leftmouse())
			retvalue = localbuttons->leftclick();
        
        if(query_key_press_event())
        {
            if(keyboard[KEYSTATE_y])
                retvalue = YES;
            else if(keyboard[KEYSTATE_n])
                retvalue = NO;
            else if(keyboard[KEYSTATE_ESCAPE])
                break;
        }
	}
	
	myscreen->clearfontbuffer();
	
    if(retvalue == YES)
        return true;
    if(retvalue == NO)
        return false;
	return default_value;
}

void popup_dialog(const char* title, const char* message)
{
	text gladtext(myscreen);
	
	int pix_per_char = 6;
	
	// Break message into lines
    std::list<std::string> ls = explode(message, '\n');
    
    // Get the max dimensions needed to display it
    int w = strlen(title)*9;
    int h = 30 + 10*ls.size();
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
    {
        if(int(e->size()*pix_per_char) > w)
            w = e->size()*pix_per_char;
    }
    
    // Centered bounds
    int leftside  = 160 - w/2 - 12;
    int rightside = 160 + w/2 + 12;
    
    // Draw background
    int dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
    
    // Draw message
    int j = 0;
    for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
    {
        gladtext.write_xy(dumbcount + 3*pix_per_char/2, 104 - h/2 + 10*j, e->c_str(), (unsigned char) DARK_BLUE, 1);
        j++;
    }

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu_no_backdrop(popup_dialog_buttons, 1, 0);  // don't redraw!

    allbuttons[0]->vdisplay();

    myscreen->buffer_to_screen(0, 0, 320, 200); // refresh screen

	grab_mouse();
    clear_keyboard();
    const Uint8* keyboard = query_keyboard();
    
    clear_key_press_event();
	
    int retvalue = 0;
	while (retvalue == 0)
	{
		get_input_events(POLL);
		
		if(leftmouse())
			retvalue = localbuttons->leftclick();
        
        if(query_key_press_event())
        {
            if(keyboard[KEYSTATE_RETURN] || keyboard[KEYSTATE_SPACE] || keyboard[KEYSTATE_ESCAPE])
                break;
        }
	}
	
	myscreen->clearfontbuffer();
}

Sint32 create_save_menu(Sint32 arg1)
{
	Sint32 retvalue=0;
	Sint32 i;
	char temp_filename[20];
	text savetext(myscreen);
	char message[80];

	if (arg1)
		arg1 = 1;

	if (localbuttons)
		delete (localbuttons);
	localbuttons = buttonmenu(saveteam, 11, 0);  // don't redraw screen yet

	myscreen->clearfontbuffer();

	myscreen->draw_button(15,  9, 255, 199, 1, 1);
	myscreen->draw_text_bar(19, 13, 251, 21);
	strcpy(message, "Gladiator: Save Game");
	savetext.write_xy(135-(strlen(message)*3), 15, message, RED, 1);

	for (i=0; i < 10; i++)
	{
		sprintf(temp_filename, "save%d", i+1);
		strcpy(allbuttons[i]->label, get_saved_name(temp_filename) );
		myscreen->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
		allbuttons[i]->vdisplay();
		myscreen->draw_box(allbuttons[i]->xloc-1,
		                   allbuttons[i]->yloc-1,
		                   allbuttons[i]->xend,
		                   allbuttons[i]->yend, 0, 0, 1);
	}
	myscreen->draw_text_bar(23, allbuttons[10]->yloc-2, 66, allbuttons[10]->yend+1);
	allbuttons[10]->vdisplay();
	myscreen->draw_box(allbuttons[10]->xloc-1,
	                   allbuttons[10]->yloc-1,
	                   allbuttons[10]->xend,
	                   allbuttons[10]->yend, 0, 0, 1);

	myscreen->buffer_to_screen(0, 0, 320, 200);

	while ( !(retvalue & EXIT) )
	{
		if (leftmouse())
		{
			retvalue=localbuttons->leftclick();
			if(retvalue == REDRAW)
                return REDRAW;
		}

		if (localbuttons && (retvalue == REDRAW))
		{
			delete(localbuttons);
			localbuttons = buttonmenu(saveteam, 11);
			myscreen->draw_button(15,  9, 255, 199, 1, 1);
			myscreen->draw_text_bar(19, 13, 251, 21);
			strcpy(message, "Gladiator: Save Game");
			savetext.write_xy(135-(strlen(message)*3), 15, message, RED, 1);
			for (i=0; i < 10; i++)
			{
				sprintf(temp_filename, "save%d", i+1);
				strcpy(allbuttons[i]->label, get_saved_name(temp_filename) );
				myscreen->draw_text_bar(23, 23+i*BUTTON_HEIGHT, 246, 36+BUTTON_HEIGHT*i);
				allbuttons[i]->vdisplay();
				myscreen->draw_box(allbuttons[i]->xloc-1,
				                   allbuttons[i]->yloc-1,
				                   allbuttons[i]->xend,
				                   allbuttons[i]->yend, 0, 0, 1);
			}
			myscreen->draw_text_bar(23, allbuttons[10]->yloc-2, 66, allbuttons[10]->yend+1);
			allbuttons[10]->vdisplay();
			myscreen->draw_box(allbuttons[10]->xloc-1,
			                   allbuttons[10]->yloc-1,
			                   allbuttons[10]->xend,
			                   allbuttons[10]->yend, 0, 0, 1);
			retvalue = 0;
		}
	}

	myscreen->clearfontbuffer();
	return REDRAW;

}

Sint32 increase_stat(Sint32 whatstat, Sint32 howmuch)
{
	switch(whatstat)
	{
		case BUT_STR:
			current_guy->strength+=howmuch;
			break;
		case BUT_DEX:
			current_guy->dexterity+=howmuch;
			break;
		case BUT_CON:
			current_guy->constitution+=howmuch;
			break;
		case BUT_INT:
			current_guy->intelligence+=howmuch;
			break;
		case BUT_ARMOR:
			current_guy->armor+=howmuch;
			break;
		case BUT_LEVEL:
			current_guy->level+=howmuch;
			break;
		default:
			break;
	}
	//calculate_cost();
	return OK;
}

Sint32 decrease_stat(Sint32 whatstat, Sint32 howmuch)
{
	switch(whatstat)
	{
		case BUT_STR:
			current_guy->strength-=howmuch;
			break;
		case BUT_DEX:
			current_guy->dexterity-=howmuch;
			break;
		case BUT_CON:
			current_guy->constitution-=howmuch;
			break;
		case BUT_INT:
			current_guy->intelligence-=howmuch;
			break;
		case BUT_ARMOR:
			current_guy->armor-=howmuch;
			break;
		case BUT_LEVEL:
			current_guy->level-=howmuch;
			break;
		default:
			break;
	}
	//calculate_cost();
	return OK;
}

Uint32 calculate_cost()
{
	guy  *ob = current_guy;
	Sint32 temp;
	Sint32 myfamily;

	if (!ob)
		return 0;

	myfamily = ob->family;
	temp = costlist[myfamily];

	// Long check of various things ..
	if (ob->strength < statlist[myfamily][BUT_STR])
		ob->strength = statlist[myfamily][BUT_STR];
	if (ob->dexterity < statlist[myfamily][BUT_DEX])
		ob->dexterity = statlist[myfamily][BUT_DEX];
	if (ob->constitution < statlist[myfamily][BUT_CON])
		ob->constitution = statlist[myfamily][BUT_CON];
	if (ob->intelligence < statlist[myfamily][BUT_INT])
		ob->intelligence = statlist[myfamily][BUT_INT];
	if (ob->armor < statlist[myfamily][BUT_ARMOR])
		ob->armor = statlist[myfamily][BUT_ARMOR];
	if (ob->level < statlist[myfamily][BUT_LEVEL])
		ob->level = statlist[myfamily][BUT_LEVEL];

	// Now figure out costs ..
	temp += (Sint32)((pow( (Sint32)(ob->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_STR]) ;
	temp += (Sint32)((pow( (Sint32)(ob->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_DEX]);
	temp += (Sint32)((pow( (Sint32)(ob->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_CON]);
	temp += (Sint32)((pow( (Sint32)(ob->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_INT]);
	temp += (Sint32)((pow( (Sint32)(ob->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_ARMOR]);
	temp += (Sint32)((pow( (Sint32)(ob->level - statlist[myfamily][BUT_LEVEL]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_LEVEL]);
	if ((Sint32) calculate_exp(ob->level) < 0) // overflow
		ob->level = 1;
	temp += (Sint32) (calculate_exp(ob->level));
	if (temp < 0)
	{
		//guytemp = new guy(current_guy->family);
		//delete current_guy;
		//current_guy = guytemp;
		cycle_guy(0);
		//temp = -1;  // This used to be an error code checked by picker.cpp line 2213
		temp = 0;
	}
	return (Uint32)temp;
}

// This version compares current_guy versus the old version ..
Uint32 calculate_cost(guy  *oldguy)
{
	guy  *ob = current_guy;
	Sint32 temp;
	Sint32 myfamily;

	if (!ob || !oldguy)
		return 0;

	myfamily = ob->family;
	temp = 0;

	// Long check of various things ..
	if (ob->strength < oldguy->strength)
		ob->strength = oldguy->strength;
	if (ob->dexterity < oldguy->dexterity)
		ob->dexterity = oldguy->dexterity;
	if (ob->constitution < oldguy->constitution)
		ob->constitution = oldguy->constitution;
	if (ob->intelligence < oldguy->intelligence)
		ob->intelligence = oldguy->intelligence;
	if (ob->armor < oldguy->armor)
		ob->armor = oldguy->armor;
	if (ob->level < oldguy->level)
		ob->level = oldguy->level;

	// Now figure out costs ..

	// First we have our 'total increased value..'
	temp += (Sint32)((pow( (Sint32)(ob->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_STR]) ;
	temp += (Sint32)((pow( (Sint32)(ob->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_DEX]);
	temp += (Sint32)((pow( (Sint32)(ob->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_CON]);
	temp += (Sint32)((pow( (Sint32)(ob->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_INT]);
	temp += (Sint32)((pow( (Sint32)(ob->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_ARMOR]);
	temp += (Sint32)((pow( (Sint32)(ob->level - statlist[myfamily][BUT_LEVEL]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_LEVEL]);

	// Now subtract what we've already paid for ..
	temp -= (Sint32)((pow( (Sint32)(oldguy->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_STR]);
	temp -= (Sint32)((pow( (Sint32)(oldguy->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_DEX]);
	temp -= (Sint32)((pow( (Sint32)(oldguy->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_CON]);
	temp -= (Sint32)((pow( (Sint32)(oldguy->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_INT]);
	temp -= (Sint32)((pow( (Sint32)(oldguy->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_ARMOR]);
	temp -= (Sint32)((pow( (Sint32)(oldguy->level - statlist[myfamily][BUT_LEVEL]), RAISE))
	               * (Sint32)statcosts[myfamily][BUT_LEVEL]);

	// Add on extra level cost ..
	if (calculate_exp(ob->level) > oldguy->exp)
		temp += (Sint32) ( ( (calculate_exp(ob->level) ) - oldguy->exp)*(ob->level-1) );

	if (temp < 0)
	{
		temp = 0;
		statscopy(current_guy, oldguy);
		cycle_team_guy(0);

	}

	return (Uint32)temp;
}

Sint32 cycle_guy(Sint32 whichway)
{
	Sint32 newfamily;
	char tempnum[5];

	if (!current_guy)
		newfamily = allowable_guys[0];
	else
	{
		current_type = current_type+ whichway + (sizeof(allowable_guys)/4);
		current_type %= (sizeof(allowable_guys)/4);
		if (current_type < 0)
			current_type = (sizeof(allowable_guys)/4) - 1;
		newfamily = allowable_guys[current_type];
		//newfamily = current_guy->family + whichway;
	}

	//newfamily = (newfamily + NUM_FAMILIES) % NUM_FAMILIES;

	// Delete the old guy ..
	if (current_guy)
	{
		delete current_guy;
		current_guy = NULL;
	}

	// Make the new guy
	current_guy = new guy(newfamily);
	current_guy->teamnum = current_team_num;
	sprintf(tempnum, "%d", numbought[newfamily]+1);
	strcat(current_guy->name, tempnum);

	show_guy(0, 0);

	myscreen->buffer_to_screen(52, 24, 108, 64);

	grab_mouse();
	
	return OK;
}

void show_guy(Sint32 frames, Sint32 who) // shows the current guy ..
{
	walker *mywalker;
	short centerx = 80, centery = 45; // center for walker
	Sint32 i;
	Sint32 newfamily;


	if (!current_guy)
		return;

	frames = abs(frames);

	if (who == 0) // use current_type of guy
		newfamily = allowable_guys[current_type];
	else
		newfamily = myscreen->save_data.team_list[editguy]->family;
	newfamily = current_guy->family;

	mywalker = myscreen->level_data.myloader->create_walker(ORDER_LIVING,
	           newfamily,myscreen);
	mywalker->stats->bit_flags = 0;
	mywalker->curdir = ((frames/192) + FACE_DOWN)%8;
	mywalker->ani_type = ANI_WALK;
	for (i=0; i <= (frames/12)%4; i++)
		mywalker->animate();
	//mywalker->team_num = ourteam[editguy]->teamnum;
	mywalker->team_num = current_guy->teamnum;

	mywalker->xpos = centerx - (mywalker->sizex/2);
	mywalker->ypos = centery - (mywalker->sizey/2);
	myscreen->draw_button(54, 26, 106, 64, 1, 1);
	myscreen->draw_text_bar(56, 28, 104, 62);
	mywalker->draw(myscreen->viewob[0]);
	delete mywalker;
	if (mymouse[MOUSE_X] >= 46 && mymouse[MOUSE_X] <= 104
	        && mymouse[MOUSE_Y] >= 12 && mymouse[MOUSE_Y] <= 64)
		release_mouse();
	myscreen->buffer_to_screen(56, 28, 48, 36);
	grab_mouse();
}
// Sets current_guy to 'whichguy' in the teamlist, and
// returns a COPY of him as the function result
Sint32 cycle_team_guy(Sint32 whichway)
{
	if (myscreen->save_data.team_size < 1)
		return -1;
    
    guy** ourteam = myscreen->save_data.team_list;
    
	editguy += whichway;
	if (editguy < 0)
	{
		editguy += MAX_TEAM_SIZE;
		while (!ourteam[editguy])
			editguy--;
	}

	if (editguy < 0 || editguy >= MAX_TEAM_SIZE)
		editguy = 0;

	if (!whichway && !ourteam[editguy])
		whichway = 1;

	while (!ourteam[editguy])
	{
		editguy += whichway;
		if (editguy < 0 || editguy >= MAX_TEAM_SIZE)
			editguy = 0;
	}

	if (current_guy)
		delete current_guy;
	current_guy = new guy(ourteam[editguy]->family);
	statscopy(current_guy, ourteam[editguy]);

	show_guy(0, 0);

	current_team_num = current_guy->teamnum;

	// Set our team button back to normal color ..
	// Zardus: FIX: added a check for null pointers
	if (allbuttons[18])
		allbuttons[18]->do_outline = 0;

	return OK;
}

Sint32 add_guy(guy *newguy)
{
	Sint32 i;

	for (i=0; i < MAX_TEAM_SIZE; i++)
    {
		if (!myscreen->save_data.team_list[i])
		{
			myscreen->save_data.team_list[i] = newguy;
			myscreen->save_data.team_size++;
			return i;
		}
    }

	// failed the case; too many guys
	return -1;
}

Sint32 name_guy(Sint32 arg)  // 0 == current_guy, 1 == ourteam[editguy]
{
	text nametext(myscreen);
	guy *someguy;

	if (arg)
		someguy = myscreen->save_data.team_list[editguy];
	else
		someguy = current_guy;

	if (!someguy)
		return REDRAW;

	release_mouse();

	myscreen->clearfontbuffer(174,8,306,30);
	
	myscreen->draw_button(174,  8, 306, 30, 1, 1); // text box
	nametext.write_xy(176, 12, "NAME THIS CHARACTER:", DARK_BLUE, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	//grab_keyboard();
	clear_keyboard();
	char* new_text = nametext.input_string(176, 20, 11, someguy->name);
	if(new_text == NULL)
        new_text = someguy->name;
	memmove(someguy->name, new_text, strlen(new_text)+1);  // Could be overlapping strings
	//release_keyboard();
	myscreen->draw_button(174,  8, 306, 30, 1, 1); // text box

	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	return REDRAW;
}

Sint32 add_guy(Sint32 ignoreme)
{
	Sint32 newfamily = current_guy->family;
	char tempnum[12];
	//buffers: changed typename to type_name due to some compile error
	char type_name[30];
	static text addtext(myscreen);
	Sint32 i;

	if (myscreen->save_data.team_size >= MAX_TEAM_SIZE) // abort abort!
		return -1;

	if (!current_guy) // we should be adding current_guy
		return -1;

	if (calculate_cost() > myscreen->save_data.m_totalcash[current_team_num] || calculate_cost() == 0)
		return OK;

	myscreen->save_data.m_totalcash[current_team_num] -= calculate_cost();
    
    guy** ourteam = myscreen->save_data.team_list;
	for (i=0; i < MAX_TEAM_SIZE; i++)
    {
		if (!ourteam[i]) // found an empty slot
		{
			current_guy->teamnum = current_team_num;
			ourteam[i] = current_guy;
			myscreen->save_data.team_size++;
			current_guy = NULL;
			release_mouse();
			myscreen->draw_button(174, 20, 306, 42, 1, 1); // text box
			addtext.write_xy(176, 24, "NAME THIS CHARACTER:", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			clear_keyboard();
			char* new_text = addtext.input_string(176, 32, 11, ourteam[i]->name);
			if(new_text == NULL)
                new_text = ourteam[i]->name;
			strcpy(tempnum, new_text);
			strcpy(ourteam[i]->name, tempnum);
			myscreen->draw_button(174, 20, 306, 42, 1, 1); // text box

			myscreen->buffer_to_screen(0, 0, 320, 200);
			grab_mouse();

			// Increment the next guy's number
			numbought[newfamily]++;

			// Ensure we have the right exp for our level
			ourteam[i]->exp = calculate_exp(ourteam[i]->level);

			//buffers: changed typename to type_name in below code

			// Grab a new, generic guy to be editted/bought
			current_guy = new guy(newfamily);
			strcpy(type_name, current_guy->name);
			statscopy(current_guy, ourteam[i]); // set to same stats as just bought
			strcpy(current_guy->name, type_name);
			sprintf(tempnum, "%d", numbought[newfamily]+1);
			strcat(current_guy->name, tempnum);

			// Return okay status
			return OK;
		}
    }

	return OK;
}

// Accept changes ..
Sint32 edit_guy(Sint32 arg1)
{
	guy *here;
	Sint32 *cheatmouse = query_mouse();

	if (arg1)
		arg1 = 1;

	if (!current_guy)
		return -1;

	here = myscreen->save_data.team_list[editguy];
	if (!here)
		return -1;  // error case; should never happen

	// This is for cheating! Only CHEAT :)
	// When holding down the right mouse button, can always accept free changes
	if (CHEAT_MODE && cheatmouse[MOUSE_RIGHT])
	{
		if (here->level != current_guy->level)
			current_guy->exp = calculate_exp(current_guy->level);
		statscopy(here, current_guy);
		return OK;
	}

	if ( (calculate_cost(here) > myscreen->save_data.m_totalcash[current_guy->teamnum]) ||  // compare cost of here to current_guy
	        (calculate_cost(here) < 0) )
		return OK;

	myscreen->save_data.m_totalcash[current_guy->teamnum] -= calculate_cost(here);  // cost of new - old (current_guy - here)

	if (here->level != current_guy->level)
		current_guy->exp = calculate_exp(current_guy->level);
	statscopy(here, current_guy);

	// Color our team button normally
	allbuttons[18]->do_outline = 0;

	return OK;
}

Sint32 how_many(Sint32 whatfamily)    // how many guys of family X on the team?
{
	Sint32 counter = 0;
	Sint32 i;

	for (i=0; i < MAX_TEAM_SIZE; i++)
    {
		if (myscreen->save_data.team_list[i] && myscreen->save_data.team_list[i]->family == whatfamily)
			counter++;
    }

	return counter;
}

Sint32 do_save(Sint32 arg1)
{
	char newname[8];
	
	static text savetext(myscreen);
	Sint32 xloc, yloc, x2loc, y2loc;

	snprintf(newname, 8, "save%d",arg1);

	release_mouse();
	clear_keyboard();
	xloc = allbuttons[arg1-1]->xloc;
	yloc = allbuttons[arg1-1]->yloc;
	x2loc = allbuttons[arg1-1]->width + xloc;
	y2loc = allbuttons[arg1-1]->height + yloc + 10;
	myscreen->draw_button(xloc, yloc, x2loc, y2loc, 2, 1);
	
	myscreen->clearfontbuffer(xloc,yloc,x2loc-xloc,y2loc-yloc);
	
	savetext.write_xy(xloc+5, yloc+4, "NAME YOUR SAVED GAME:", DARK_BLUE, 1);
	char* new_text = savetext.input_string(xloc+5, yloc+11, 35, allbuttons[arg1-1]->label);
	if(new_text == NULL)
        new_text = allbuttons[arg1-1]->label;
	myscreen->draw_box(xloc, yloc, x2loc, y2loc, 0, 1, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	
	myscreen->save_data.save_name = new_text;
	myscreen->save_data.save(newname);
	grab_mouse();

	return REDRAW;
}

Sint32 do_load(Sint32 arg1)
{
	char newname[8];

	snprintf(newname, 8, "save%d", arg1);

	myscreen->save_data.load(newname);
	return REDRAW;
}

const char* get_saved_name(const char * filename)
{
	SDL_RWops  *infile;
	char temp_filename[80];

	char temptext[10] = "GTL";
	static char savedgame[40];
	char temp_version = 1;
	short temp_registered;

	// This only uses the first segment of the save format.
	// See load_team_list() for full format
	
	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number (from graph.h)
	// 2-bytes registered mark, version 7+ only
	// 40-byte saved-game name (version 2 and up only!)
	//   .
	//   .

	strcpy(temp_filename, filename);
	//buffers: PORT: changed .GTL to .gtl
	strcat(temp_filename, ".gtl"); // gladiator team list

	if ( (infile = open_read_file("save/", temp_filename)) == NULL ) // open for read
	{
		return "EMPTY SLOT";
	}

	// Read id header
	SDL_RWread(infile, temptext, 3, 1);
	if ( strcmp(temptext,"GTL"))
	{
	    SDL_RWclose(infile);
		strcpy(savedgame, "EMPTY SLOT");
		return savedgame;
	}

	// Read version number
	SDL_RWread(infile, &temp_version, 1, 1);
	if (temp_version != 1)
	{
		if (temp_version >= 2)
		{
			if (temp_version >= 7)
				SDL_RWread(infile, &temp_registered, 2, 1);
			SDL_RWread(infile, savedgame, 40, 1);
		}
		else
		{
            SDL_RWclose(infile);
			strcpy(savedgame, "SAVED GAME");
			return savedgame;
		}
	}
	else
		strcpy(savedgame, "SAVED GAME"); // fake the game name

    SDL_RWclose(infile);
	return (savedgame);
}

Sint32 delete_all()
{
	Sint32 counter = myscreen->save_data.team_size;

	for (int i = 0; i < myscreen->save_data.team_size; i++)
    {
        delete myscreen->save_data.team_list[i];
        myscreen->save_data.team_list[i] = NULL;
    }
    
    myscreen->save_data.team_size = 0;

	return counter;
}

Sint32 add_money(Sint32 howmuch)
{
	myscreen->save_data.m_totalcash[current_guy->teamnum] += (Sint32) howmuch;
	return myscreen->save_data.m_totalcash[current_guy->teamnum];
}

Sint32 go_menu(Sint32 arg1)
{
	// Save the current team in memory to save0.gtl, and
	// run gladiator.
	static text gotext(myscreen);
	Sint32 temptime;

	if (arg1)
		arg1 = 1;

	// Make sure we have a valid team
	if (myscreen->save_data.team_size < 1)
	{
		release_mouse();
		myscreen->clearfontbuffer(100,65,220-100,125-65);
		myscreen->draw_dialog(100, 65, 220, 125, "Need a team!");
		gotext.write_y(89, "Please hire a", DARK_BLUE, 1);
		gotext.write_y(97, "team before", DARK_BLUE, 1);
		gotext.write_y(105,"starting the level", DARK_BLUE, 1);
		myscreen->buffer_to_screen(0, 0, 320, 200);
		temptime = query_timer();
		while(query_timer() < temptime + 150)
			;
		grab_mouse();
		myscreen->clearfontbuffer(100,65,220-100,125-65);
		
		return REDRAW;
	}
	myscreen->save_data.save("save0");
	release_mouse();
	//grab_keyboard();
	//*******************************
	// Fade out from MENU loop
	//*******************************
	// Zardus: PORT: fade out from menu code now in glad.cpp
	//clear_keyboard();
	//myscreen->fadeblack(0);

	if (current_guy)
		delete current_guy;
	current_guy = NULL;

	// Reset viewscreen prefs
	myscreen->ready_for_battle(myscreen->save_data.numplayers);

	glad_main(myscreen, myscreen->save_data.numplayers);
	//release_keyboard();
	//*******************************
	// Fade out from ACTION loop
	//*******************************
	// Zardus: PORT: new fade code
	myscreen->fadeblack(0);

	// Zardus: PORT: doesn't seem to be neccessary
	myscreen->clearbuffer();
	myscreen->clearscreen();

	// Zardus: PORT: they had this in just so that the pallettes got reset to
	// normal. It actually faded in a black screen, since fading in the menu
	// would mean messing with a bunch of things. Maybe we'll do the fade in
	// menu later, but for now we'll keep it like they had
	//*******************************
	// Fade in to MENU loop
	//*******************************
	// Zardus: PORT: new fade code
	//myscreen->fadeblack(1);

	grab_mouse();

	myscreen->reset(1);
	myscreen->viewob[0]->resize(PREF_VIEW_FULL);

	SDL_RWops* loadgame = open_read_file("save/", "save0.gtl");
	if (loadgame)
	{
	    SDL_RWclose(loadgame);
		myscreen->save_data.load("save0");
	}

	return CREATE_TEAM_MENU;
}

void statscopy(guy *dest, guy *source)
{
	dest->family = source->family;
	dest->strength = source->strength;
	dest->dexterity = source->dexterity;
	dest->constitution = source->constitution;
	dest->intelligence = source->intelligence;
	dest->level = source->level;
	dest->armor = source->armor;
	dest->exp = source->exp;
	dest->kills = source->kills;
	dest->level_kills = source->level_kills;
	dest->total_damage = source->total_damage;
	dest->total_hits   = source->total_hits;
	dest->total_shots  = source->total_shots;
	dest->teamnum = source->teamnum;

	strcpy(dest->name, source->name);
}

void quit(Sint32 arg1)
{
	if (arg1)
		arg1 = 1;
	release_mouse();

	myscreen->refresh();

	delete theprefs;
	picker_quit();
	release_keyboard();
	exit(0);
}

Sint32 set_player_mode(Sint32 howmany)
{
	Sint32 count = 0;
	myscreen->save_data.numplayers = howmany;

	while (allbuttons[count])
	{
		allbuttons[count]->vdisplay();
		count++;
	}
	//buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

	return OK;
}

Sint32 calculate_level(Uint32 experience)
{
	Sint32 result=1;

	while (calculate_exp(result) <= experience)
		result++;
	return (result-1);


	/*
	  if (experience >=  44232000L)
	         return 13;
	  else if (experience >=  20963000L)
	         return 12;
	  else if (experience >= 9982000L)
	         return 11;
	  else if (experience >= 4776000L)
	         return 10;
	  else if (experience >= 2296000L)
	         return 9;
	  else if (experience >= 1109000L)
	         return 8;
	  else if (experience >= 538000L)
	         return 7;
	  else if (experience >= 262000L)
	         return 6;
	  else if (experience >= 128000L)
	         return 5;
	  else if (experience >= 63000L)
	         return 4;
	  else if (experience >= 31000L)
	         return 3;
	  else if (experience >= 11000L)
	         return 2;
	  else
	         return 1;
	*/
}

Uint32 calculate_exp(Sint32 level)
{


	/*
	  if (level > 13)
	    return (Sint32) (2*calculate_exp(level-1) );
	  if (level == 13)
	         return 44232000L;
	  else if (level == 12)
	         return 20963000L;
	  else if (level == 11)
	         return 9982000L;
	  else if (level == 10)
	         return 4776000L;
	  else if (level == 9)
	         return 2296000L;
	  else if (level == 8)
	         return 1109000L;
	  else if (level == 7)
	         return 538000L;
	  else if (level == 6)
	         return 262000L;
	  else if (level == 5)
	         return 128000L;
	  else if (level == 4)
	         return 63000L;
	  else if (level == 3)
	         return 31000L;
	  else if (level == 2)
	         return 11000L;
	  else
	         return 0;
	 
	*/

	if (level > 2)
		return (Sint32) ( (8000*(level+10)) / 10) + calculate_exp(level-1);
	else if (level > 1)
		return (Sint32) 8000;
	else
		return (Sint32) 0;
}


//new functions
Sint32 return_menu(Sint32 arg)
{
   return arg;
}

Sint32 create_detail_menu(guy *arg1)
{
#define DETAIL_LM 11             // left edge margin ..
#define DETAIL_MM 164            // center margin
#define DETAIL_LD(x) (90+(x*6))  // vertical line for text
#define WL(p,m) if (m[1] != ' ') mytext->write_xy(DETAIL_LM, DETAIL_LD(p), m, RED, 1); else mytext->write_xy(DETAIL_LM, DETAIL_LD(p), m, DARK_BLUE, 1)
#define WR(p,m) if (m[1] != ' ') mytext->write_xy(DETAIL_MM, DETAIL_LD(p), m, RED, 1); else mytext->write_xy(DETAIL_MM, DETAIL_LD(p), m, DARK_BLUE, 1)

   Sint32 retvalue = 0;
   guy *thisguy;
   Sint32 start_time = query_timer();
   Sint32 *detailmouse;

   release_mouse();

   myscreen->clearfontbuffer();

   if (localbuttons)
       delete localbuttons;
   localbuttons = buttonmenu(detailed, 1, 0);

   if (arg1)
       thisguy = arg1;
   else
       thisguy = myscreen->save_data.team_list[editguy];

   myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
   myscreen->draw_text_bar(36, 10, 124, 22);
   mytext->write_xy(80 - mytext->query_width(current_guy->name)/2, 14,
                    current_guy->name,(unsigned char) DARK_BLUE, 1);
   myscreen->draw_dialog(5, 68, 315, 167, "Character Special Abilities");
   myscreen->draw_text_bar(160, 90, 162, 160);

   // Text stuff, determined by character class & level
   switch (thisguy->family)
   {
       case FAMILY_SOLDIER:
           sprintf(message, "Level %d soldier has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things (charge)
           WL(2, " Charge");
           WL(3, "  Charge causes you to ");
           WL(4, "  run forward, damaging");
           WL(5, "  anything in your way.");
           // Level 4 things (boomerang)
           if (thisguy->level >= 4)
           {
               WL(7, " Boomerang");
               WL(8, "  The boomerang flies  ");
               WL(9, "  out in a spiral,     ");
               WL(10,"  hurting nearby foes. ");
           }
           // Level 7 things (whirl)
           if (thisguy->level >= 7)
           {
               WR(0, " Whirl    ");
               WR(1, "  The fighter whirls in");
               WR(2, "  a spiral, hurting or ");
               WR(3 ,"  stunning melee foes. ");
           }
           // Level 10 things (disarm)
           if (thisguy->level >= 10)
           {
               WR(5, " Disarm   ");
               WR(6, "  Cause a melee foe to ");
               WR(7, "  temporarily lose the ");
               WR(8 ,"  strength of attacks. ");
           }
           break;
       case FAMILY_BARBARIAN:
           sprintf(message, "Level %d barbarian has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things (hurl boulder)
           WL(2, " Hurl Boulder");
           WL(3, "  Throw a massive stone");
           WL(4, "  boulder at your      ");
           WL(5, "  enemies.             ");
           // Level 4 things (exploding boulder)
           if (thisguy->level >= 4)
           {
               WL(7, " Exploding Boulder");
               WL(8, "  Hurl a boulder so hard ");
               WL(9, "  that it explodes and   ");
               WL(10,"  hits foes all around.  ");
           }
           break;
       case FAMILY_ELF:
           sprintf(message, "Level %d elf has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things (rocks)
           WL(2, " Rocks/Forestwalk");
           WL(3, "  Rocks hurls a few rocks");
           WL(4, "  at the enemy.  Forest- ");
           WL(5, "  walk, dexterity-based, ");
           WL(6, "  lets you move in trees.");
           // Level 4 things (more rocks)
           if (thisguy->level >= 4)
           {
               WL(7, " More Rocks");
               WL(8, "  Like #1, but these    ");
               WL(9, "  rocks bounce off walls");
               WL(10,"  and other barricades. ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Lots of Rocks");
               WR(1, "  Like #2, but more     ");
               WR(2, "  rocks, with a longer  ");
               WR(3 ,"  thrown range.         ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, " MegaRocks");
               WR(6, "  This giant handful of ");
               WR(7, "  rocks bounces far away");
               WR(8 ,"  and packs a big punch.");
           }
           break;
       case FAMILY_ARCHER:
           sprintf(message, "Level %d archer has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Fire Arrows     ");
           WL(3, "  An archer can spin in a");
           WL(4, "  circle, firing off a   ");
           WL(5, "  ring of flaming bolts. ");
           //WL(6, "  lets you move in trees.");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " Barrage   ");
               WL(8, "  Rather than a single  ");
               WL(9, "  bolt, the archer sends");
               WL(10,"  3 deadly bolts ahead. ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Exploding Bolt");
               WR(1, "  This fatal bolt will  ");
               WR(2, "  explode on contact,   ");
               WR(3 ,"  dealing death to all. ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, "          ");
               WR(6, "                        ");
               WR(7, "                        ");
               WR(8 ,"                        ");
           }
           break;
       case FAMILY_MAGE:
           sprintf(message, "Level %d Mage has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Teleport/Marker ");
           WL(3, "  Any mage can teleport  ");
           WL(4, "  randomly away easily.  ");
           WL(5, "  Leaving a marker for   ");
           WL(6, "  anchor requires 75 int.");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " Warp Space");
               WL(8, "  Twist the fabric of   ");
               WL(9, "  space around you to   ");
               WL(10,"  deal death to enemies.");
           }
           // Can we change to archmage?
           if (thisguy->level >= 6)
           {
               sprintf(message,"Level %d Archmage. This",
                       (thisguy->level-6)/2+1);
               myscreen->draw_dialog(158, 4, 315, 66, "Become ArchMage");
               WR(-10,"Your Mage is now of high");
               WR( -9,"enough level to become a");
               //WR( -8,"Level 1 Archmage. This  ");
               WR(-8, message);
               WR( -7,"change CANNOT be undone!");
               WR( -6," Click here to change.  ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Freeze Time   ");
               WR(1, "  Freeze time for all   ");
               WR(2, "  but your team and kill");
               WR(3 ,"  enemies with ease.    ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(4, " Energy Wave");
               WR(5, "  Send a growing ripple ");
               WR(6, "  of energy through     ");
               WR(7 ,"  walls and foes.       ");
           }
           // Level 13 things
           if (thisguy->level >= 13)
           {
               WR(8, " HeartBurst  ");
               WR(9, "  Burst your enemies    ");
               WR(10,"  into flame. More magic");
               WR(11,"  means a bigger effect.");
           }
           break;
       case FAMILY_ARCHMAGE:
           sprintf(message, "Level %d ArchMage has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Teleport/Marker ");
           WL(3, "  Any mage can teleport  ");
           WL(4, "  randomly away easily.  ");
           WL(5, "  Leaving a marker for   ");
           WL(6, "  anchor requires 75 int.");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " HeartBurst/Lightning");
               WL(8, "  Burst your enemies    ");
               WL(9, "  into flame around you.");
               WL(10,"  ALT: Chain lightning  ");
               WL(11,"  bounces through foes. ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Summon Image/Sum. Elem.");
               WR(1, "  Summon an illusionary ");
               WR(2, "  ally to fight for you.");
               WR(3 ,"  ALT: Summon a daemon, ");
               WR(4 ,"  who uses your stamina.");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, " Mind Control");
               WR(6,"  Convert nearby foes to");
               WR(7,"  your team, for a time.");
           }
           break;

       case FAMILY_CLERIC:
           sprintf(message, "Level %d Cleric has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Heal            ");
           WL(3, "  Heal all teammates who ");
           WL(4, "  are close to you, for  ");
           WL(5, "  as much as you have SP.");
           //WL(6, "  lets you move in trees.");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " Raise/Turn Undead");
               WL(8, "  Raise the gore of any ");
               WL(9, "  victim to a skeleton. ");
               WL(10,"  Alternate (turning)   ");
               WL(11,"  requires 65 Int.      ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Raise/Turn Ghost");
               WR(1, "  A more powerful raise,");
               WR(2, "  you can now get ghosts");
               WR(3 ,"  to fly and wail.      ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, " Resurrection");
               WR(6, "  The ultimate Healing, ");
               WR(7, "  this restores dead    ");
               WR(8 ,"  friends to life, or   ");
               WR(9 ,"  enemies to undead.    ");
               WR(10,"  Beware: this will use ");
               WR(11,"  your own EXP to cast! ");
           }
           break;
       case FAMILY_DRUID:
           sprintf(message, "Level %d Druid has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Plant Tree      ");
           WL(3, "  These magical trees    ");
           WL(4, "  will resist the enemy, ");
           WL(5, "  while allowing friends ");
           WL(6, "  to pass.               ");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " Summon Faerie");
               WL(8, "  This spell brings to  ");
               WL(9, "  you a small flying    ");
               WL(10,"  faerie to stun foes.  ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Circle of Protection");
               WR(1, "  Calls the winds to aid");
               WR(2, "  your nearby friends by");
               WR(3 ,"  circling them with a  ");
               WR(4 ,"  shield of moving air. ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, " Reveal   ");
               WR(6, "  Gives you a magical   ");
               WR(7, "  view to see treasure, ");
               WR(8 ,"  potions, outposts, and");
               WR(9 ,"  invisible enemies.    ");
           }
           break;
       case FAMILY_THIEF:
           sprintf(message, "Level %d Thief has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Drop Bomb       ");
           WL(3, "  Leave a burning bomb to");
           WL(4, "  explode and hurt the   ");
           WL(5, "  unwary, friend or foe! ");
           //WL(6, "  to pass.               ");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " Cloak of Darkness");
               WL(8, "  Cloak yourself in the ");
               WL(9, "  shadows, slipping past");
               WL(10,"  your enemies.         ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, " Taunt Enemies       ");
               WR(1, "  Beckon your enemies   ");
               WR(2, "  to you with jeers, and");
               WR(3 ,"  confuse their attack. ");
               //WR(4 ,"  shield of moving air. ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, " Poison Cloud");
               WR(6, "  Release a cloud of    ");
               WR(7, "  poisonous gas to roam ");
               WR(8 ,"  at will and sicken    ");
               WR(9 ,"  your foes.            ");
           }
           break;
       case FAMILY_ORC:
           sprintf(message, "Level %d Orc has:", thisguy->level);
           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
           // Level 1 things
           WL(2, " Howl            ");
           WL(3, "  Howl in rage, stunning ");
           WL(4, "  nearby enemies in their");
           WL(5, "  tracks.                ");
           //WL(6, "  to pass.               ");
           // Level 4 things
           if (thisguy->level >= 4)
           {
               WL(7, " Devour Corpse    ");
               WL(8, "  Regain health by      ");
               WL(9, "  devouring the corpses ");
               WL(10,"  of your foes.         ");
           }
           // Can we change to orc captain?
           if (thisguy->level >= 6)
           {
               myscreen->draw_dialog(158, 4, 315, 66, "Become Orc Captain");
               WR(-10,"Your Orc is now of high ");
               WR( -9,"enough level to become a");
               WR( -8,"Level 1 Orc Captain. You");
               WR( -7,"CANNOT undo this action!");
               WR( -6," Click here to change.  ");
           }
           // Level 7 things
           if (thisguy->level >= 7)
           {
               WR(0, "                     ");
               //WR(1, "  Beckon your enemies   ");
               //WR(2, "  to you with jeers, and");
               //WR(3 ,"  confuse their attack. ");
               //WR(4 ,"  shield of moving air. ");
           }
           // Level 10 things
           if (thisguy->level >= 10)
           {
               WR(5, "             ");
               //WR(6, "  Release a cloud of    ");
               //WR(7, "  poisonous gas to roam ");
               //WR(8 ,"  at will and sicken    ");
               //WR(9 ,"  your foes.            ");
           }
           break;
       default:
           break;
   }

   show_guy(0, 1);
   release_mouse();
   myscreen->buffer_to_screen(0, 0, 320, 200);
   grab_mouse();

   leftmouse();
   localbuttons->leftclick();

   while ( !(retvalue & EXIT) )
   {
       show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]

       if (leftmouse())
       {
           detailmouse = query_mouse();
           if (detailmouse[MOUSE_X] >= 160 &&
                   detailmouse[MOUSE_X] <= 315 &&
                   detailmouse[MOUSE_Y] >= 4   &&
                   detailmouse[MOUSE_Y] <= 66)
           {
               if (thisguy->family == FAMILY_MAGE &&
                       thisguy->level >= 6)
               {
                   // Become an archmage!
                   thisguy->level = ( (thisguy->level-6) / 2) + 1;
                   thisguy->exp = calculate_exp(thisguy->level);
                   thisguy->family = FAMILY_ARCHMAGE;
                   myscreen->soundp->play_sound(SOUND_EXPLODE);
                   myscreen->soundp->play_sound(SOUND_EXPLODE);
                   myscreen->soundp->play_sound(SOUND_EXPLODE);
                   return REDRAW;
               }  // end of mage->archmage
               else if (thisguy->family == FAMILY_ORC &&
                        thisguy->level >= 5)
               {
                   // Become an Orcish Captain!
                   thisguy->exp = 0;
                   thisguy->level = 1;
                   thisguy->family = FAMILY_BIG_ORC; // fake for now
                   myscreen->soundp->play_sound(SOUND_DIE1);
                   myscreen->soundp->play_sound(SOUND_DIE2);
                   myscreen->soundp->play_sound(SOUND_DIE1);
                   return REDRAW;
               } // end of orc->orc-captain
           }

           retvalue=localbuttons->leftclick();
       }
   }
   return REDRAW;  // back to edit menu
}





int get_scen_num_from_filename(const char* name)
{
   if(!name)
    return -1;
    
   const char* n = name;
   while(isalpha(*n))
   {
       n++;
   }
   if(*n == '\0')
    return -1;
   else
    return atoi(n);
}


Sint32 do_pick_campaign(Sint32 arg1)
{
   pick_campaign(myscreen, myscreen->save_data);
   return REDRAW;
}

Sint32 do_set_scen_level(Sint32 arg1)
{
   static text savetext(myscreen);
   Sint32 xloc, yloc, x2loc, y2loc;
   Sint32 templevel = myscreen->save_data.scen_num;
   Sint32 temptime;

   xloc = 100;
   yloc = 170;
   x2loc = 220;
   y2loc = 190;

    myscreen->clearfontbuffer(xloc,yloc,x2loc,y2loc);
   
   templevel = pick_level(myscreen);
   myscreen->level_data.id = templevel;
   if (templevel < 0 || !myscreen->level_data.load())
   {
       myscreen->draw_box(xloc, yloc, x2loc, y2loc, 0, 1, 1);
       savetext.write_xy(xloc+15, yloc+4, "INVALID LEVEL", DARK_BLUE, 1);
       myscreen->buffer_to_screen(0, 0, 320, 200);
       temptime = query_timer();
       while(query_timer() < temptime + 100)
           ;
   }
   else
   {
       myscreen->draw_box(xloc, yloc, x2loc, y2loc, 0, 1, 1);
       savetext.write_xy(xloc+15, yloc+4, "LEVEL LOADED", DARK_BLUE, 1);
       myscreen->buffer_to_screen(0, 0, 320, 200);
       temptime = query_timer();
       while(query_timer() < temptime + 100)
           ;
       myscreen->save_data.scen_num = templevel;
   }

   return REDRAW;
}

/*
int matherr(struct exception *problem)
{
 // Do nothing
 return 0;
}
*/

Sint32 set_difficulty()
{
   char message[80];

   current_difficulty = (current_difficulty + 1) % DIFFICULTY_SETTINGS;
   sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
   strcpy(allbuttons[6]->label, message);

   //allbuttons[6]->vdisplay();
   //myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}

Sint32 change_teamnum(Sint32 arg)
{
   // Change the team number of the current guy
   short current_team;
   char  message[80];

   // What is our current team number?
   if (!current_guy)
       return 0;
   current_team = current_guy->teamnum;

   // We can be from team 0 (default) to team 3 .. make sure
   // we don't exceed this range.
   current_team += (short)arg;
   current_team %= 4;

   // Set our team number ..
   current_guy->teamnum = current_team;

   // Update our button display
   sprintf(message, "Playing on Team %d", current_team + 1);

   strcpy(allbuttons[18]->label, message);
   allbuttons[18]->do_outline = 1;
   //allbuttons[18]->vdisplay();
   //myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}

Sint32 change_hire_teamnum(Sint32 arg)
{
   // Change the team number of the hiring menu ..
   char  message[80];

   current_team_num += arg;
   current_team_num %= 4;

   // Change our guy, if he exists ..
   if (current_guy)
   {
       current_guy->teamnum = current_team_num;
   }

   // Update our button display
   sprintf(message, "Hiring For Team %d", current_team_num + 1);

myscreen->clearfontbuffer(170, 130, 133, 22);

   strcpy(allbuttons[16]->label, message);
   allbuttons[16]->vdisplay();
   myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}

Sint32 change_allied()
{
   // Change our allied mode (on or off)
   char message[80];

   myscreen->save_data.allied_mode += 1;
   myscreen->save_data.allied_mode %= 2;

   if (myscreen->save_data.allied_mode)
       sprintf(message, "PVP: Ally");
   else
       sprintf(message, "PVP: Enemy");

   // Update our button display
   strcpy(allbuttons[7]->label, message);
   //buffers: allbuttons[7]->vdisplay();
   //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}
