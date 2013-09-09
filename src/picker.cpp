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
#include "version.h"
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
void popup_dialog(const char* title, const char* message);
void timed_dialog(const char* message, float delay_seconds = 3.0f);

bool prompt_for_string(text* mytext, const std::string& message, std::string& result);

#define BUTTON_HEIGHT 15

//int matherr (struct exception *);

void show_guy(Sint32 frames, Sint32 who, short centerx = 80, short centery = 45); // shows the current guy ..
Sint32 name_guy(Sint32 arg); // rename (or name) the current_guy

void glad_main(screen *myscreen, Sint32 playermode);
const char* get_saved_name(const char * filename);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);

Sint32 leftmouse(button* buttons);
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

// mainmenu
button mainmenu_buttons[] =
    {
        button("", KEYSTATE_UNKNOWN, 80, 50, 140, 20, BEGINMENU, 1 , MenuNav::Down(1), false), // BEGIN NEW GAME
        button("CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20, CREATE_TEAM_MENU, -1 , MenuNav::UpDown(0, 5)),

        button("4 PLAYER", KEYSTATE_4, 152,125,68,20, SET_PLAYER_MODE, 4 , MenuNav::UpDownLeft(4, 6, 3)),
        button("3 PLAYER", KEYSTATE_3, 80,125,68,20, SET_PLAYER_MODE,3 , MenuNav::UpDownRight(5, 6, 2)),
        button("2 PLAYER", KEYSTATE_2, 152,100,68,20, SET_PLAYER_MODE,2 , MenuNav::UpDownLeft(1, 2, 5)),
        button("1 PLAYER", KEYSTATE_1, 80,100,68,20, SET_PLAYER_MODE,1 , MenuNav::UpDownRight(1, 3, 4)),

        button("DIFFICULTY", KEYSTATE_UNKNOWN, 80, 148, 140, 10, SET_DIFFICULTY, -1, MenuNav::UpDown(3, 7)),

        button("PVP: Allied", KEYSTATE_UNKNOWN, 80, 160, 68, 10, ALLIED_MODE, -1, MenuNav::UpDownRight(6, 9, 8)),
        button("Level Edit", KEYSTATE_UNKNOWN, 152, 160, 68, 10, DO_LEVEL_EDIT, -1, MenuNav::UpDownLeft(6, 9, 7)),

        button("QUIT", KEYSTATE_ESCAPE, 120, 175, 60, 20, QUIT_MENU, -1 , MenuNav::Up(7))
    };

// beginmenu (first menu of new game), create_team_menu
button createmenu_buttons[] =
    {
        button("VIEW TEAM", KEYSTATE_UNKNOWN, 30, 70, 80, 15, CREATE_VIEW_MENU, -1, MenuNav::DownRight(3, 1)),
        button("TRAIN TEAM", KEYSTATE_UNKNOWN, 120, 70, 80, 15, CREATE_TRAIN_MENU, -1, MenuNav::DownLeftRight(4, 0, 2)),
        button("HIRE TROOPS",  KEYSTATE_UNKNOWN, 210, 70, 80, 15, CREATE_HIRE_MENU, -1, MenuNav::DownLeft(5, 1)),
        button("LOAD TEAM", KEYSTATE_UNKNOWN, 30, 100, 80, 15, CREATE_LOAD_MENU, -1, MenuNav::UpDownRight(0, 6, 4)),
        button("SAVE TEAM", KEYSTATE_UNKNOWN, 120, 100, 80, 15, CREATE_SAVE_MENU, -1, MenuNav::UpLeftRight(1, 3, 5)),
        button("GO", KEYSTATE_UNKNOWN,        210, 100, 80, 15, GO_MENU, -1, MenuNav::UpDownLeft(2, 7, 4)),

        button("BACK", KEYSTATE_ESCAPE, 30, 140, 60, 30, RETURN_MENU, EXIT, MenuNav::UpRight(3, 7)),
        button("SET LEVEL", KEYSTATE_UNKNOWN, 210, 140, 80, 20, DO_SET_SCEN_LEVEL, EXIT, MenuNav::UpDownLeft(5, 8, 6)),
        button("SET CAMPAIGN", KEYSTATE_UNKNOWN, 210, 170, 80, 20, DO_PICK_CAMPAIGN, EXIT, MenuNav::UpLeft(7, 6)),

    };

button viewteam_buttons[] =
    {
        //  button("TRAIN", KEYSTATE_e, 85, 170, 60, 20, CREATE_TRAIN_MENU, -1},
        //  button("HIRE",  KEYSTATE_b, 190, 170, 60, 20, CREATE_HIRE_MENU, -1},
        button("GO", KEYSTATE_UNKNOWN,        270, 170, 40, 20, GO_MENU, -1, MenuNav::Left(1)),
        button("BACK", KEYSTATE_ESCAPE,    10, 170, 44, 20, RETURN_MENU , EXIT, MenuNav::Right(0)),

    };

button details_buttons[] =
    {
        button("BACK", KEYSTATE_ESCAPE, 10, 170, 40, 20, RETURN_MENU , EXIT, MenuNav::UpRight(1, 1)),
        button(160, 4, 315 - 160, 66 - 4, 0 , -1, MenuNav::DownLeft(0, 0), false, true) // PROMOTE
    };

button trainmenu_buttons[] =
    {
        button("PREV", KEYSTATE_UNKNOWN,  10, 40, 40, 20, CYCLE_TEAM_GUY, -1, MenuNav::DownRight(2, 1)),
        button("NEXT", KEYSTATE_UNKNOWN,  110, 40, 40, 20, CYCLE_TEAM_GUY, 1, MenuNav::DownLeftRight(3, 0, 16)),
        button("", KEYSTATE_UNKNOWN,  16, 70, 16, 10, DECREASE_STAT, BUT_STR, MenuNav::UpDownRight(0, 4, 3)),
        button("", KEYSTATE_UNKNOWN,  126, 70, 16, 12, INCREASE_STAT, BUT_STR, MenuNav::UpDownLeft(1, 5, 2)),
        button("", KEYSTATE_UNKNOWN,  16, 85, 16, 10, DECREASE_STAT, BUT_DEX, MenuNav::UpDownRight(2, 6, 5)),
        button("", KEYSTATE_UNKNOWN,  126, 85, 16, 12, INCREASE_STAT, BUT_DEX, MenuNav::UpDownLeft(3, 7, 4)),
        button("", KEYSTATE_UNKNOWN,  16, 100, 16, 10, DECREASE_STAT, BUT_CON, MenuNav::UpDownRight(4, 8, 7)),
        button("", KEYSTATE_UNKNOWN,  126,100, 16, 12, INCREASE_STAT, BUT_CON, MenuNav::UpDownLeft(5, 9, 6)),
        button("", KEYSTATE_UNKNOWN,  16, 115, 16, 10, DECREASE_STAT, BUT_INT, MenuNav::UpDownRight(6, 10, 9)),
        button("", KEYSTATE_UNKNOWN,  126, 115, 16, 12, INCREASE_STAT, BUT_INT, MenuNav::UpDownLeft(7, 11, 8)),
        button("", KEYSTATE_UNKNOWN,  16, 130, 16, 10, DECREASE_STAT, BUT_ARMOR, MenuNav::UpDownRight(8, 12, 11)),
        button("", KEYSTATE_UNKNOWN,  126, 130, 16, 12, INCREASE_STAT, BUT_ARMOR, MenuNav::UpDownLeft(9, 13, 10)),
        button("", KEYSTATE_UNKNOWN,  16, 145, 16, 10, DECREASE_STAT, BUT_LEVEL, MenuNav::UpDownRight(10, 19, 13)),
        button("", KEYSTATE_UNKNOWN,  126, 145, 16, 12, INCREASE_STAT, BUT_LEVEL, MenuNav::UpDownLeftRight(11, 15, 12, 18)),
        button("VIEW TEAM", KEYSTATE_UNKNOWN,  190, 170, 90, 20, CREATE_VIEW_MENU, -1, MenuNav::UpLeft(18, 15)),
        button("ACCEPT", KEYSTATE_UNKNOWN,  80, 170, 80, 20, EDIT_GUY, -1, MenuNav::UpLeftRight(13, 19, 14)),
        button("RENAME", KEYSTATE_UNKNOWN, 174,  8, 64, 22, NAME_GUY, 1, MenuNav::DownLeftRight(18, 1, 17)),
        button("DETAILS..", KEYSTATE_UNKNOWN, 240, 8, 64, 22, CREATE_DETAIL_MENU, 0, MenuNav::DownLeft(18, 16)),
        button("Playing on Team X", KEYSTATE_UNKNOWN, 174, 138, 133, 22, CHANGE_TEAM, 1, MenuNav::UpDownLeft(17, 14, 13)),
        button("BACK", KEYSTATE_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT, MenuNav::UpRight(12, 15)),

    };

button hiremenu_buttons[] =
    {
        button("PREV", KEYSTATE_UNKNOWN,  10, 40, 40, 20, CYCLE_GUY, -1, MenuNav::DownRight(4, 1)),
        button("NEXT", KEYSTATE_UNKNOWN,  110, 40, 40, 20, CYCLE_GUY, 1, MenuNav::DownLeftRight(3, 0, 3)),
        button("hiring for team X", KEYSTATE_UNKNOWN, 190, 170, 110, 20, CHANGE_HIRE_TEAM, 1, MenuNav::UpLeft(1, 3)),
        button("HIRE ME", KEYSTATE_UNKNOWN,  82, 166, 88, 28, ADD_GUY, -1, MenuNav::UpLeftRight(1, 4, 2)),
        button("BACK", KEYSTATE_ESCAPE,10, 170, 40, 20, RETURN_MENU , EXIT, MenuNav::UpRight(0, 3)),

    };


button saveteam_buttons[] =
    {
        button("SLOT ONE", KEYSTATE_UNKNOWN,  25, 25, 220, 10, DO_SAVE, 1, MenuNav::UpDown(10, 1)),
        button("SLOT TWO", KEYSTATE_UNKNOWN,  25, 40, 220, 10, DO_SAVE, 2, MenuNav::UpDown(0, 2)),
        button("SLOT THREE", KEYSTATE_UNKNOWN,25, 55, 220, 10, DO_SAVE, 3, MenuNav::UpDown(1, 3)),
        button("SLOT FOUR", KEYSTATE_UNKNOWN, 25, 70, 220, 10, DO_SAVE, 4, MenuNav::UpDown(2, 4)),
        button("SLOT FIVE", KEYSTATE_UNKNOWN, 25, 85, 220, 10, DO_SAVE, 5, MenuNav::UpDown(3, 5)),
        button("SLOT Six", KEYSTATE_UNKNOWN, 25, 100, 220, 10, DO_SAVE,  6, MenuNav::UpDown(4, 6)),
        button("SLOT Seven", KEYSTATE_UNKNOWN, 25, 115, 220, 10, DO_SAVE, 7, MenuNav::UpDown(5, 7)),
        button("SLOT Eight", KEYSTATE_UNKNOWN, 25, 130, 220, 10, DO_SAVE, 8, MenuNav::UpDown(6, 8)),
        button("SLOT Nine", KEYSTATE_UNKNOWN, 25, 145, 220, 10, DO_SAVE, 9, MenuNav::UpDown(7, 9)),
        button("SLOT Ten", KEYSTATE_UNKNOWN, 25, 160, 220, 10, DO_SAVE, 10, MenuNav::UpDown(8, 10)),
        button("BACK", KEYSTATE_ESCAPE,25, 175, 40, 20, RETURN_MENU , EXIT, MenuNav::UpDown(9, 0)),

    };

button loadteam_buttons[] =
    {
        button("SLOT ONE", KEYSTATE_UNKNOWN,  25, 25, 220, 10, DO_LOAD, 1, MenuNav::UpDown(10, 1)),
        button("SLOT TWO", KEYSTATE_UNKNOWN,  25, 40, 220, 10, DO_LOAD, 2, MenuNav::UpDown(0, 2)),
        button("SLOT THREE", KEYSTATE_UNKNOWN,25, 55, 220, 10, DO_LOAD, 3, MenuNav::UpDown(1, 3)),
        button("SLOT FOUR", KEYSTATE_UNKNOWN, 25, 70, 220, 10, DO_LOAD, 4, MenuNav::UpDown(2, 4)),
        button("SLOT FIVE", KEYSTATE_UNKNOWN, 25, 85, 220, 10, DO_LOAD, 5, MenuNav::UpDown(3, 5)),
        button("SLOT Six", KEYSTATE_UNKNOWN, 25, 100, 220, 10, DO_LOAD,  6, MenuNav::UpDown(4, 6)),
        button("SLOT Seven", KEYSTATE_UNKNOWN, 25, 115, 220, 10, DO_LOAD, 7, MenuNav::UpDown(5, 7)),
        button("SLOT Eight", KEYSTATE_UNKNOWN, 25, 130, 220, 10, DO_LOAD, 8, MenuNav::UpDown(6, 8)),
        button("SLOT Nine", KEYSTATE_UNKNOWN, 25, 145, 220, 10, DO_LOAD, 9, MenuNav::UpDown(7, 9)),
        button("SLOT Ten", KEYSTATE_UNKNOWN, 25, 160, 220, 10, DO_LOAD, 10, MenuNav::UpDown(8, 10)),
        button("BACK", KEYSTATE_ESCAPE,25, 175, 40, 20, RETURN_MENU , EXIT, MenuNav::UpDown(9, 0)),

    };


button yes_or_no_buttons[] =
    {
        button("YES", KEYSTATE_UNKNOWN,  70, 130, 50, 20, YES_OR_NO, YES, MenuNav::Right(1)),
        button("NO", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, YES_OR_NO, NO, MenuNav::Left(0))
    };

button no_or_yes_buttons[] =
    {
        button("NO", KEYSTATE_UNKNOWN,  70, 130, 50, 20, YES_OR_NO, NO, MenuNav::Right(1)),
        button("YES", KEYSTATE_UNKNOWN,  320-50-70, 130, 50, 20, YES_OR_NO, YES, MenuNav::Left(0))
    };

button popup_dialog_buttons[] =
    {
        button("OK", KEYSTATE_ESCAPE,  160 - 25, 130, 50, 20, YES_OR_NO, YES, MenuNav::None())
    };

Sint32 leftmouse(button* buttons)
{
	Sint32 i = 0;
	Sint32 somebutton = -1;

	grab_mouse();
	mymouse = query_mouse();

	while (allbuttons[i])
	{
	    if(buttons != NULL && !buttons[i].hidden)
        {
            allbuttons[i]->mouse_on();
            if (keystates[allbuttons[i]->hotkey])
                somebutton = i;
        }
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



void draw_version_number()
{
	text mytext(myscreen);

	myscreen->redrawme = 1;
	int w = strlen(OPENGLAD_VERSION_STRING)*6;
	int h = 8;
	int x = 320 - w - 80;
	int y = 200 - 12;
	myscreen->fastbox(x, y, w, h, PURE_BLACK);
	mytext.write_xy(x, y, OPENGLAD_VERSION_STRING, (unsigned char) DARK_BLUE, 1);
}

#ifdef USE_CONTROLLER_INPUT
#define MENU_NAV_DEFAULT true
#else
#define MENU_NAV_DEFAULT false
#endif
bool menu_nav_enabled = MENU_NAV_DEFAULT;
Uint32 menu_nav_enabled_time = 0;

void draw_highlight_interior(const button& b)
{
    if(!menu_nav_enabled)
        return;
    
    float t = (1.0f + sinf(SDL_GetTicks()/300.0f))/2.0f;
    float size = 3;
    myscreen->draw_box(b.x + t*size, b.y + t*size, b.x + b.sizex - t*size, b.y + b.sizey - t*size, YELLOW, 0);
}

void draw_highlight(const button& b)
{
    if(!menu_nav_enabled)
        return;
    
    float t = (1.0f + sinf(SDL_GetTicks()/300.0f))/2.0f;
    float size = 3;
    myscreen->draw_box(b.x - t*size, b.y - t*size, b.x + b.sizex + t*size, b.y + b.sizey + t*size, YELLOW, 0);
}

bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);

bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons)
{
    int next_button = -1;
    bool pressed = false;
    bool activated = false;
    if(isPlayerHoldingKey(0, KEY_UP))
    {
        while(isPlayerHoldingKey(0, KEY_UP))
            get_input_events(POLL);
        next_button = buttons[highlighted_button].nav.up;
        
        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_DOWN))
    {
        while(isPlayerHoldingKey(0, KEY_DOWN))
            get_input_events(POLL);
        next_button = buttons[highlighted_button].nav.down;
        
        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_LEFT))
    {
        while(isPlayerHoldingKey(0, KEY_LEFT))
            get_input_events(POLL);
        next_button = buttons[highlighted_button].nav.left;
        
        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_RIGHT))
    {
        while(isPlayerHoldingKey(0, KEY_RIGHT))
            get_input_events(POLL);
        next_button = buttons[highlighted_button].nav.right;
        
        pressed = true;
    }
    if(isPlayerHoldingKey(0, KEY_FIRE))
    {
        while(isPlayerHoldingKey(0, KEY_FIRE))
            get_input_events(POLL);
        
        if(!menu_nav_enabled)
            pressed = true;
        else
        {
            myscreen->soundp->play_sound(SOUND_BOW);
            if(use_global_vbuttons)
            {
                allbuttons[highlighted_button]->vdisplay(1);
                allbuttons[highlighted_button]->vdisplay();
                if(allbuttons[highlighted_button]->myfunc)
                {
                    retvalue = allbuttons[highlighted_button]->do_call(allbuttons[highlighted_button]->myfunc, allbuttons[highlighted_button]->arg);
                }
            }
            else
                retvalue = OK;
            
            pressed = true;
            activated = true;
        }
    }
    
    if(next_button >= 0 && !buttons[next_button].hidden)
        highlighted_button = next_button;
    
    // Turn menu_nav on if something was pressed.
    if(pressed)
    {
        menu_nav_enabled = true;
        menu_nav_enabled_time = SDL_GetTicks();
    }
    // Turn it off if it's been a while since something was pressed.
    else if(menu_nav_enabled)
    {
        if(SDL_GetTicks() - menu_nav_enabled_time > 5000)
            menu_nav_enabled = MENU_NAV_DEFAULT;
    }
    
    return activated;
}

bool reset_buttons(vbutton*& localbuttons, button* buttons, int num_buttons, Sint32& retvalue)
{
    if(localbuttons && (retvalue == OK || retvalue == REDRAW))
    {
        delete(localbuttons);
        localbuttons = init_buttons(buttons, num_buttons);
        
        retvalue = 0;
        return true;
    }
    return false;
}

void redraw_mainmenu()
{
    int count = 0;
	char message[80];
    
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
    allbuttons[6]->label = message;

    // Show the allied mode
    if (myscreen->save_data.allied_mode)
        sprintf(message, "PVP: Ally");
    else
        sprintf(message, "PVP: Enemy");
    allbuttons[7]->label = message;

    count = 0;
    while (allbuttons[count])
    {
        allbuttons[count]->vdisplay();
        count++;
    }
    allbuttons[0]->set_graphic(FAMILY_NORMAL1);
    
    draw_version_number();
}

Sint32 mainmenu(Sint32 arg1)
{
	Sint32 retvalue=0;

	if(arg1)
		arg1 = 1;

	if(localbuttons != NULL)
		delete localbuttons; //we'll make a new set

	button* buttons = mainmenu_buttons;
	int num_buttons = 10;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	allbuttons[0]->set_graphic(FAMILY_NORMAL1);
	
	redraw_mainmenu();

	clear_keyboard();
	reset_timer();
	while (query_timer() < 1);
	
	myscreen->fadeblack(1);

	grab_mouse();

	while(!(retvalue & EXIT))
	{
	    // Input
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        if(reset_buttons(localbuttons, buttons, num_buttons, retvalue))
            allbuttons[0]->set_graphic(FAMILY_NORMAL1);
		
		// Draw
		myscreen->clearbuffer();
        draw_buttons(buttons, num_buttons);
        redraw_mainmenu();
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
	return retvalue;
}

// Reset game data and go to create_team_menu()
Sint32 beginmenu(Sint32 arg1)
{
	Sint32 i;

	myscreen->clear();

	// Starting new game ..
	release_mouse();
	//grab_keyboard();
	myscreen->clearbuffer();
	myscreen->swap();
	read_campaign_intro(myscreen);
	//release_keyboard();
	myscreen->refresh();
	grab_mouse();
	myscreen->clear();

    // Reset the save data so we have a fresh, new team
	myscreen->save_data.reset();
	current_guy = NULL;
	
	// Clear the labeling counter
	for (i=0; i < NUM_FAMILIES; i++)
		numbought[i] = 0;

	return create_team_menu(1);
}


const char* get_family_string(short family)
{
	switch(family)
	{
		case FAMILY_ARCHER:
			return "ARCHER";
		case FAMILY_CLERIC:
			return "CLERIC";
		case FAMILY_DRUID:
			return "DRUID";
		case FAMILY_ELF:
			return "ELF";
		case FAMILY_MAGE:
			return "MAGE";
		case FAMILY_SOLDIER:
			return "SOLDIER";
		case FAMILY_THIEF:
			return "THIEF";
		case FAMILY_ARCHMAGE:
			return "ARCHMAGE";
		case FAMILY_ORC:
			return "ORC";
		case FAMILY_BIG_ORC:
			return "ORC CAPTAIN";
		case FAMILY_BARBARIAN:
			return "BARBARIAN";
		case FAMILY_FIREELEMENTAL:
			return "ELEMENTAL";
		case FAMILY_SKELETON:
			return "SKELETON";
		case FAMILY_SLIME:
		case FAMILY_MEDIUM_SLIME:
		case FAMILY_SMALL_SLIME:
			return "SLIME";
		case FAMILY_FAERIE:
			return "FAERIE";
		case FAMILY_GHOST:
			return "GHOST";
		default:
			return "BEAST";
	}
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

	if (arg1 == 1)
    {
        // Go straight to the hiring screen if we just started a new game.
        retvalue = create_hire_menu(arg1);
    }

	if (localbuttons)
		delete localbuttons;

	myscreen->fadeblack(0);
	
	text mytext(myscreen);
	
	button* buttons = createmenu_buttons;
	int num_buttons = 9;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	draw_backdrop();
	draw_buttons(buttons, num_buttons);
	
	int last_level_id = -1;
	
	myscreen->fadeblack(1);
	
	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        
        // Reset buttons
        bool buttons_were_reset = reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
        if(last_level_id != myscreen->save_data.scen_num || buttons_were_reset)
        {
            retvalue = 0;
            last_level_id = myscreen->save_data.scen_num;
            myscreen->level_data.id = last_level_id;
            myscreen->level_data.load();
        }
        
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        // Level name
        int len = strlen(myscreen->level_data.title.c_str());
        myscreen->draw_rect_filled(buttons[7].x + buttons[7].sizex - 6*len - 2, buttons[7].y - 8 - 1, 6*len + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(buttons[7].x + buttons[7].sizex - 6*len, buttons[7].y - 8, WHITE, "%s", myscreen->level_data.title.c_str());
        // Campaign name
        len = strlen(myscreen->save_data.current_campaign.c_str());
        myscreen->draw_rect_filled(buttons[8].x + buttons[8].sizex - 6*len - 2, buttons[8].y - 8 - 1, 6*len + 4, 8, PURE_BLACK, 150);
        mytext.write_xy(buttons[8].x + buttons[8].sizex - 6*strlen(myscreen->save_data.current_campaign.c_str()), buttons[8].y - 8, WHITE, "%s", myscreen->save_data.current_campaign.c_str());
        
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
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
    
	button* buttons = viewteam_buttons;
	int num_buttons = 2;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);

	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        view_team(5,5,314, 160);
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	myscreen->clearbuffer();

	return REDRAW;
}

std::string get_class_description(unsigned char family)
{
    std::string result;
    
    switch(family)
    {
    case FAMILY_SOLDIER:
        result = "Your basic grunt, can     \n"
                 "absorb and deal damage and\n"
                 "move moderately fast. A   \n"
                 "good all-around fighter. A\n"
                 "soldier's normal weapon is\n"
                 "a magical returning blade.\n"
                 "\n"
                 "Special: Charge";
        break;
    case FAMILY_ELF:
        result = "Elves are small and weak, \n"
                 "but are harder to hit than\n"
                 "most classes. Alone of all\n"
                 "the classes, elves posses \n"
                 "the 'ForestWalk' ability. \n"
                 "\n"
                 "Special: Rocks";
        break;
    case FAMILY_ARCHER:
        result = "Archers are fleet of foot,\n"
                 "and their arrows have a   \n"
                 "long range. Although      \n"
                 "they're not as strong as  \n"
                 "other fighters, they can  \n"
                 "be a good squad backbone. \n"
                 "\n"
                 "Special: Fire Arrows";
        break;
    case FAMILY_MAGE:
        result = "Mages are slow, can't     \n"
                 "stand much damage, and are\n"
                 "horrible at hand-to-hand  \n"
                 "combat, but their magical \n"
                 "energy balls pack a big   \n"
                 "punch.                    \n"
                 "\n"
                 "Special: Teleport";
        break;
    case FAMILY_SKELETON:
        result = "Skeletons are the pathetic\n"
                 "remains of those who once \n"
                 "were among the living.    \n"
                 "They are not particularly \n"
                 "dangerous, but they move  \n"
                 "with blinding speed.      \n"
                 "\n"
                 "Special: Tunnel";
        break;
    case FAMILY_CLERIC:
        result = "Clerics, like mages, are  \n"
                 "slow, but have a stronger \n"
                 "hand-to-hand attack.      \n"
                 "Clerics posses abilities  \n"
                 "related to healing and    \n"
                 "interaction with the dead.\n"
                 "\n"
                 "Special: Heal";
        break;
    case FAMILY_FIREELEMENTAL:
        result = "Strong and quick, fire    \n"
                 "elementals can expel      \n"
                 "flaming meteors in all    \n"
                 "directions to decimate    \n"
                 "enemies.                  \n"
                 "\n"
                 "Special: Starburst";
        break;
    case FAMILY_FAERIE:
        result = "The faerie are small,     \n"
                 "flying above friends and  \n"
                 "enemies alike unnoticed.  \n"
                 "Although they are delicate\n"
                 "and easily destroyed,     \n"
                 "faeries can sprinkle a    \n"
                 "magic powder which freezes\n"
                 "their enemies.";
        break;
    case FAMILY_SLIME:
    case FAMILY_SMALL_SLIME:
    case FAMILY_MEDIUM_SLIME:
        result = "Slimes are patches of ooze\n"
                 "which grow until they     \n"
                 "split into two small      \n"
                 "slimes, thus increasing   \n"
                 "the population and over-  \n"
                 "whelming the enemy.       \n"
                 "\n"
                 "Special: Grow";
        break;
    case FAMILY_THIEF:
        result = "Thieves are fast, though  \n"
                 "not so potent as the      \n"
                 "soldier. Thieves can throw\n"
                 "small blades rapidly and  \n"
                 "damage whole groups of    \n"
                 "enemies with their bombs. \n"
                 "\n"
                 "Special: Drop Bomb";
        break;
    case FAMILY_GHOST:
        result = "Ghosts can pass through   \n"
                 "walls, trees, and anything\n"
                 "else that gets in the way.\n"
                 "Their chilling touch can  \n"
                 "bring death quickly at    \n"
                 "close range.              \n"
                 "\n"
                 "Special: Scare";
        break;
    case FAMILY_DRUID:
        result = "Druids are the magicians  \n"
                 "of nature, and have power \n"
                 "over natural events. They \n"
                 "throw lightning bolts at  \n"
                 "their foes; the fast bolts\n"
                 "have long range.          \n"
                 "\n"
                 "Special: Plant Tree";
        break;
    case FAMILY_ORC:
        result = "Orcs are a basic 'grunt'; \n"
                 "strong and hard to hurt,  \n"
                 "they don't do much more   \n"
                 "than inflict pain. Orcs   \n"
                 "can't attack at range.    \n"
                 "\n"
                 "Special: Howl";
        break;
    case FAMILY_BIG_ORC:
        result = "Orcs captains are stronger\n"
                 "and smarter than the basic\n"
                 "orc.  They throw blades   \n"
                 "across the battlefield to \n"
                 "deal damage from afar.";
        break;
    case FAMILY_BARBARIAN:
        result = "Barbarians are powerful,  \n"
                 "but possess more will than\n"
                 "skill. They are tough and \n"
                 "strong, tending to bash   \n"
                 "their way through trouble \n"
                 "with heavy iron hammers.  \n"
                 "\n"
                 "Special: Hurl Boulder";
        break;
    case FAMILY_ARCHMAGE:
        result = "An Archmage takes the     \n"
                 "learnings of the Magi one \n"
                 "step further, possessing  \n"
                 "extraordinary firepower at\n"
                 "the expense of physical   \n"
                 "weakness.                 \n"
                 "\n"
                 "Special: Teleport";
        break;
    default:
        break;
    }
    
    return result;
}

// stat: str 0, dex 1, con 2, int 3, armor 4
const char* get_training_cost_rating(unsigned char family, int stat)
{
    int value = 55/(statcosts[family][stat]);
    int rating = float(value)/11 * 5;
    switch(rating)
    {
    case 0:
        return "";
    case 1:
        return "*";
    case 2:
        return "**";
    case 3:
        return "***";
    case 4:
        return "****";
    case 5:
        return "*****";
    default:
        return "";
    }
}

Sint32 create_hire_menu(Sint32 arg1)
{
	Sint32 linesdown, retvalue = 0;
	Sint32 start_time = query_timer();
	unsigned char showcolor; // normally STAT_COLOR or STAT_CHANGED
	Uint32 current_cost;
	Sint32 clickvalue;

#define STAT_NUM_OFFSET 42
#define STAT_COLOR   DARK_BLUE // color for normal stat text
#define STAT_CHANGED RED       // color for changed stat text
#define STAT_DERIVED DARK_BLUE + 3
    
    SDL_Rect stat_box = {196, 50 - 6 - 32, 104, 82 + 32};
    SDL_Rect stat_box_inner = {stat_box.x + 4, stat_box.y + 4 + 6, stat_box.w - 8, stat_box.h - 8 - 6};
    SDL_Rect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};
    
    SDL_Rect cost_box = {196, 130, 104, 30};
    SDL_Rect cost_box_inner = {cost_box.x + 4, cost_box.y + 4, cost_box.w - 8, cost_box.h - 8};
    SDL_Rect cost_box_content = {cost_box_inner.x + 4, cost_box_inner.y + 4, cost_box_inner.w - 8, cost_box_inner.h - 8};
    
    SDL_Rect description_box = {11, 71, 180, 90};
    SDL_Rect description_box_inner = {description_box.x + 4, description_box.y + 4, description_box.w - 8, description_box.h - 8};
    SDL_Rect description_box_content = {description_box_inner.x + 4, description_box_inner.y + 4, description_box_inner.w - 8, description_box_inner.h - 8};
    
    SDL_Rect name_box = {description_box.x + description_box.w/2 - (126-34)/2, description_box.y - 71 + 8, 126 - 34, 24 - 8};
    SDL_Rect name_box_inner = {name_box.x + 2, name_box.y + 2, name_box.w - 4, name_box.h - 4};
    
    hiremenu_buttons[0].x = description_box.x + description_box.w/2 - hiremenu_buttons[0].sizex - 4 - 30;
    hiremenu_buttons[0].y = name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2 - hiremenu_buttons[0].sizey/2;
    
    hiremenu_buttons[1].x = description_box.x + description_box.w/2 + 4 + 30;
    hiremenu_buttons[1].y = name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2 - hiremenu_buttons[1].sizey/2;
    
    hiremenu_buttons[2].hidden = (myscreen->save_data.numplayers == 1);
    
	myscreen->clearbuffer();

	if (localbuttons)
		delete (localbuttons);
    
	button* buttons = hiremenu_buttons;
	int num_buttons = 5;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
    
    cycle_guy(0);
    change_hire_teamnum(0);
    
    
    unsigned char last_family = current_guy->family;
    std::string description = get_class_description(last_family);
    std::list<std::string> desc = explode(description);
    const char* family_name = get_family_string(last_family);
	
	grab_mouse();

	while ( !(retvalue & EXIT) )
	{
	    // Input
		clickvalue = leftmouse(buttons);
		if (clickvalue == 1)
			retvalue = localbuttons->leftclick();
		else if (clickvalue == 2)
			retvalue = localbuttons->rightclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        if(retvalue == OK || retvalue == REDRAW)
        {
            if (localbuttons)
                delete (localbuttons);
            
            localbuttons = init_buttons(buttons, num_buttons);

            // Update our team-number display ..
            change_hire_teamnum(0);
            
            retvalue = 0;
        }
		
		// Draw
		myscreen->clearbuffer();
		
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        if (!current_guy)
            cycle_guy(0);
        
        // Name box
        myscreen->draw_button(name_box, 1);
        myscreen->draw_button_inverted(name_box_inner);
        
        mytext->write_xy(name_box.x + name_box.w/2 - 3*strlen(family_name), name_box.y + 6, family_name, (unsigned char) DARK_BLUE, 1);
        
		show_guy(query_timer()-start_time, 0, description_box.x + description_box.w/2, name_box.y + name_box.h + (description_box.y - (name_box.y + name_box.h))/2); // 0 means current_guy
        change_hire_teamnum(0);
        
        
        // Description box
        myscreen->draw_button(description_box, 1);
        myscreen->draw_button_inverted(description_box_inner);
        
        if(current_guy->family != last_family)
        {
            // Update description
            last_family = current_guy->family;
            description = get_class_description(last_family);
            desc = explode(description);
            
            family_name = get_family_string(last_family);
        }
        
        int i = 0;
        for(std::list<std::string>::iterator e = desc.begin(); e != desc.end(); e++)
        {
            mytext->write_xy(description_box_content.x, description_box_content.y + i*10, DARK_BLUE, "%s", e->c_str());
            i++;
        }
        
        // Cost box
        myscreen->draw_button(cost_box, 1);
        myscreen->draw_button_inverted(cost_box_inner);
        
        sprintf(message, "CASH: %u", myscreen->save_data.m_totalcash[current_team_num]);
        mytext->write_xy(cost_box_content.x, cost_box_content.y, message,(unsigned char) DARK_BLUE, 1);
        current_cost = calculate_cost();
        mytext->write_xy(cost_box_content.x, cost_box_content.y + 10, "COST: ", DARK_BLUE, 1);
        sprintf(message, "      %u", current_cost );
        if (current_cost > myscreen->save_data.m_totalcash[current_team_num])
            mytext->write_xy(cost_box_content.x + 10, cost_box_content.y + 10, message, STAT_CHANGED, 1);
        else
            mytext->write_xy(cost_box_content.x + 10, cost_box_content.y + 10, message, STAT_COLOR, 1);

        // Stat box
        myscreen->draw_button(stat_box, 1);
        mytext->write_xy(stat_box.x + 65, stat_box.y + 2, DARK_BLUE, "Train");
        myscreen->draw_button_inverted(stat_box_inner);

        // Stat box content
        linesdown = 0;
        
        // Strength
        sprintf(message, "%d", current_guy->strength);
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12, "STR:",
                         (unsigned char) STAT_COLOR, 1);
        if (statlist[(int)current_guy->family][BUT_STR] < current_guy->strength)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12, message, showcolor, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*12, get_training_cost_rating(last_family, 0), showcolor, 1);
        
        linesdown++;
        // Dexterity
        sprintf(message, "%d", current_guy->dexterity);
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12, "DEX:",
                         (unsigned char) STAT_COLOR, 1);
        if (statlist[(int)current_guy->family][BUT_DEX] < current_guy->dexterity)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12, message, showcolor, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*12, get_training_cost_rating(last_family, 1), showcolor, 1);

        linesdown++;
        // Constitution
        sprintf(message, "%d", current_guy->constitution);
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12, "CON:",
                         (unsigned char) STAT_COLOR, 1);
        if (statlist[(int)current_guy->family][BUT_CON] < current_guy->constitution)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12, message, showcolor, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*12, get_training_cost_rating(last_family, 2), showcolor, 1);

        linesdown++;
        // Intelligence
        sprintf(message, "%d", current_guy->intelligence);
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12, "INT:",
                         (unsigned char) STAT_COLOR, 1);
        if (statlist[(int)current_guy->family][BUT_INT] < current_guy->intelligence)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12, message, showcolor, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET + 18, stat_box_content.y + linesdown*12, get_training_cost_rating(last_family, 3), showcolor, 1);

        linesdown++;
        // Armor
        sprintf(message, "%d", current_guy->armor);
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12, "ARMOR:",
                         (unsigned char) STAT_COLOR, 1);
        if (statlist[(int)current_guy->family][BUT_ARMOR] < current_guy->armor)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12, message, showcolor, 1);
		
		SDL_Rect r = {stat_box_content.x + 10, stat_box_content.y + (linesdown+1)*12 - 3, stat_box_content.w - 20, 2};
		myscreen->draw_button_inverted(r);
		
        linesdown++;
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12 + 4, "HP:", STAT_DERIVED, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12 + 4, showcolor, "%d", myscreen->level_data.myloader->hitpoints[PIX(ORDER_LIVING, last_family)]);
		
		linesdown++;
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12 + 4, "MELEE:", STAT_DERIVED, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12 + 4, showcolor, "%d", myscreen->level_data.myloader->damage[PIX(ORDER_LIVING, last_family)]);
		
		linesdown++;
        mytext->write_xy(stat_box_content.x, stat_box_content.y + linesdown*12 + 4, "SPEED:", STAT_DERIVED, 1);
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, stat_box_content.y + linesdown*12 + 4, showcolor, "%d", myscreen->level_data.myloader->stepsizes[PIX(ORDER_LIVING, last_family)]);
        
        
		
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
        
        if(arg1 == 1)
        {
            // Show popup on new game
            arg1 = -1;
            popup_dialog("HIRE TROOPS", "Get your team started here\nby hiring some fresh recruits.");
            
            if(localbuttons)
                delete (localbuttons);
            localbuttons = init_buttons(buttons, num_buttons);
        }
	}
	
	myscreen->clearbuffer();
	//myscreen->clearscreen();
	return REDRAW;
}

Sint32 create_train_menu(Sint32 arg1)
{
	guy * here;
	Sint32 linesdown, i, retvalue=0;
	unsigned char showcolor;
	Sint32 start_time = query_timer();
	Uint32 current_cost;
	Sint32 clickvalue;
	
    SDL_Rect stat_box = {38, 66, 82, 94};
    SDL_Rect stat_box_inner = {stat_box.x + 4, stat_box.y + 4, stat_box.w - 8, stat_box.h - 8};
    SDL_Rect stat_box_content = {stat_box_inner.x + 4, stat_box_inner.y + 4, stat_box_inner.w - 8, stat_box_inner.h - 8};

	if (arg1)
		arg1 = 1;

	// Make sure we have a valid team
	if (myscreen->save_data.team_size < 1)
	{
		popup_dialog("NEED A TEAM!", "You need to\nhire a team\nto train");
		
		return OK;
	}

	myscreen->clearbuffer();

	if (localbuttons)
		delete localbuttons;
    
	button* buttons = trainmenu_buttons;
	int num_buttons = 20;
	int highlighted_button = 1;
	localbuttons = init_buttons(buttons, num_buttons);
	
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


	grab_mouse();
	
    clear_keyboard();
    
    clear_key_press_event();
	
	guy** ourteam = myscreen->save_data.team_list;

	while ( !(retvalue & EXIT) )
	{
	    // Input
		clickvalue = leftmouse(buttons);
		if (clickvalue == 1)
			retvalue = localbuttons->leftclick();
		else if (clickvalue == 2)
			retvalue = localbuttons->rightclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        if(localbuttons && (retvalue == OK || retvalue == REDRAW))
        {
            if(retvalue == REDRAW)
            {
				delete(localbuttons);
				localbuttons = init_buttons(buttons, num_buttons);
				
				for (i=2; i < 14; i++)
				{
					if (!(i%2)) // 2, 4, ..., 12
						allbuttons[i]->set_graphic(FAMILY_MINUS);
					else
						allbuttons[i]->set_graphic(FAMILY_PLUS);
				}
				cycle_team_guy(0);
            }

            retvalue = 0;
        }
		
        if (!current_guy)
            cycle_team_guy(0);
        if (here != ourteam[editguy])
            here = ourteam[editguy];
        current_cost = calculate_cost(here);
        
		// Draw
		myscreen->clearbuffer();
		
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
		show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
		

        linesdown = 0;

        myscreen->draw_button(34,  8, 126, 24, 1, 1);  // name box
        myscreen->draw_text_bar(36, 10, 124, 22);
        mytext->write_xy(80 - mytext->query_width(current_guy->name)/2, 14,
                         current_guy->name,(unsigned char) DARK_BLUE, 1);
        myscreen->draw_button(38, 66, 120, 160, 1, 1); // stats box
        myscreen->draw_text_bar(42, 70, 116, 156);

        // Strength
        sprintf(message, "%d", current_guy->strength);
        mytext->write_xy(stat_box_content.x, DOWN(linesdown), "  STR:",
                         (unsigned char) STAT_COLOR, 1);
        if (here->strength < current_guy->strength)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message, showcolor, 1);

        // Dexterity
        sprintf(message, "%d", current_guy->dexterity);
        mytext->write_xy(stat_box_content.x, DOWN(linesdown), "  DEX:",
                         (unsigned char) STAT_COLOR, 1);
        if (here->dexterity < current_guy->dexterity)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message, showcolor, 1);

        // Constitution
        sprintf(message, "%d", current_guy->constitution);
        mytext->write_xy(stat_box_content.x, DOWN(linesdown), "  CON:",
                         (unsigned char) STAT_COLOR, 1);
        if (here->constitution < current_guy->constitution)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message, showcolor, 1);

        // Intelligence
        sprintf(message, "%d", current_guy->intelligence);
        mytext->write_xy(stat_box_content.x, DOWN(linesdown), "  INT:",
                         (unsigned char) STAT_COLOR, 1);
        if (here->intelligence < current_guy->intelligence)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message, showcolor, 1);

        // Armor
        sprintf(message, "%d", current_guy->armor);
        mytext->write_xy(stat_box_content.x, DOWN(linesdown), "ARMOR:",
                         (unsigned char) STAT_COLOR, 1);
        if (here->armor < current_guy->armor)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message, showcolor, 1);

        // Level
        sprintf(message, "%d", current_guy->level);
        mytext->write_xy(stat_box_content.x, DOWN(linesdown), "LEVEL:",
                         (unsigned char) STAT_COLOR, 1);
        if (here->level < current_guy->level)
            showcolor = STAT_CHANGED;
        else
            showcolor = STAT_COLOR;
        mytext->write_xy(stat_box_content.x + STAT_NUM_OFFSET, DOWN(linesdown++), message, showcolor, 1);

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
        allbuttons[18]->label = message;
        allbuttons[18]->vdisplay();

        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
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

	if (localbuttons)
		delete (localbuttons);
    
	button* buttons = loadteam_buttons;
	int num_buttons = 11;
	int highlighted_button = 10;
	localbuttons = init_buttons(buttons, num_buttons);

	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
        {
			retvalue = localbuttons->leftclick();
			if(retvalue == REDRAW)
            {
                return REDRAW;
            }
        }
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        myscreen->draw_button(15,  9, 255, 199, 1, 1);
        myscreen->draw_text_bar(19, 13, 251, 21);
        strcpy(message, "Gladiator: Load Game");
        loadtext.write_xy(135-(strlen(message)*3), 15, message, RED, 1);
        for (i=0; i < 10; i++)
        {
            sprintf(temp_filename, "save%d", i+1);
            allbuttons[i]->label = get_saved_name(temp_filename);
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
                           
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
	return REDRAW;
}


void timed_dialog(const char* message, float delay_seconds)
{
    Log("%s\n", message);
    
    myscreen->darken_screen();
    
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
}


bool yes_or_no_prompt(const char* title, const char* message, bool default_value)
{
    Log("%s, %s: \n", title, message);
    
    myscreen->darken_screen();
    
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
    int j = 0;
	
	int dumbcount;

	if (localbuttons)
		delete (localbuttons);
    
	button* buttons = yes_or_no_buttons;
	int num_buttons = 2;
	int highlighted_button = (default_value? 0 : 1);
	localbuttons = init_buttons(buttons, num_buttons);

	grab_mouse();
    clear_keyboard();
    
    clear_key_press_event();
	
    int retvalue = 0;
	while (retvalue == 0)
	{
		get_input_events(POLL);
        
	    // Input
        if(query_key_press_event())
        {
            if(keystates[KEYSTATE_y])
                retvalue = YES;
            else if(keystates[KEYSTATE_n])
                retvalue = NO;
            else if(keystates[KEYSTATE_ESCAPE])
                break;
        }
        
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
        
		// Draw
		dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
		j = 0;
        for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
        {
            gladtext.write_xy(dumbcount + 3*pix_per_char/2, 104 - h/2 + 10*j, e->c_str(), (unsigned char) DARK_BLUE, 1);
            j++;
        }
        
        draw_buttons(buttons, num_buttons);
        
        draw_highlight_interior(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
    if(retvalue == YES)
    {
        Log("YES\n");
        return true;
    }
    if(retvalue == NO)
    {
        Log("NO\n");
        return false;
    }
	return default_value;
}


bool no_or_yes_prompt(const char* title, const char* message, bool default_value)
{
    Log("%s, %s: \n", title, message);
    
    myscreen->darken_screen();
    
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
    int j = 0;
    
	int dumbcount;

	if (localbuttons)
		delete (localbuttons);
    
	button* buttons = no_or_yes_buttons;
	int num_buttons = 2;
	int highlighted_button = (default_value? 1 : 0);
	localbuttons = init_buttons(buttons, num_buttons);

	grab_mouse();
    clear_keyboard();
    
    clear_key_press_event();
	
    int retvalue = 0;
	while (retvalue == 0)
	{
		get_input_events(POLL);
        
	    // Input
        if(query_key_press_event())
        {
            if(keystates[KEYSTATE_y])
                retvalue = YES;
            else if(keystates[KEYSTATE_n])
                retvalue = NO;
            else if(keystates[KEYSTATE_ESCAPE])
                break;
        }
        
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
		j = 0;
        for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
        {
            gladtext.write_xy(dumbcount + 3*pix_per_char/2, 104 - h/2 + 10*j, e->c_str(), (unsigned char) DARK_BLUE, 1);
            j++;
        }
        
        draw_buttons(buttons, num_buttons);
        
        draw_highlight_interior(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
    if(retvalue == YES)
    {
        Log("YES\n");
        return true;
    }
    if(retvalue == NO)
    {
        Log("NO\n");
        return false;
    }
	return default_value;
}

void popup_dialog(const char* title, const char* message)
{
    Log("%s, %s\n", title, message);
    
    myscreen->darken_screen();
    
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
    int dumbcount;
    
    // Draw message
    int j = 0;

	if (localbuttons)
		delete (localbuttons);
    
	button* buttons = popup_dialog_buttons;
	int num_buttons = 1;
	int highlighted_button = 0;
	localbuttons = init_buttons(buttons, num_buttons);

	grab_mouse();
    clear_keyboard();
    
    clear_key_press_event();
	
    int retvalue = 0;
	while (retvalue == 0)
	{
	    // Input
		get_input_events(POLL);
        if(query_key_press_event())
        {
            if(keystates[KEYSTATE_RETURN] || keystates[KEYSTATE_SPACE] || keystates[KEYSTATE_ESCAPE])
                break;
        }
        
		if(leftmouse(buttons))
			retvalue = localbuttons->leftclick();
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		dumbcount = myscreen->draw_dialog(leftside, 80 - h/2, rightside, 80 + h/2, title);
		j = 0;
        for(std::list<std::string>::iterator e = ls.begin(); e != ls.end(); e++)
        {
            gladtext.write_xy(dumbcount + 3*pix_per_char/2 + w/2 - e->size()*pix_per_char/2, 104 - h/2 + 10*j, e->c_str(), (unsigned char) DARK_BLUE, 1);
            j++;
        }
		
        draw_buttons(buttons, num_buttons);
        
        draw_highlight_interior(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
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
    
	button* buttons = saveteam_buttons;
	int num_buttons = 11;
	int highlighted_button = 10;
	localbuttons = init_buttons(buttons, num_buttons);
	

	while ( !(retvalue & EXIT) )
	{
	    // Input
		if(leftmouse(buttons))
        {
			retvalue = localbuttons->leftclick();
			if(retvalue == REDRAW)
            {
                return REDRAW;
            }
        }
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        
        // Reset buttons
        reset_buttons(localbuttons, buttons, num_buttons, retvalue);
		
		// Draw
		myscreen->clearbuffer();
        draw_backdrop();
        draw_buttons(buttons, num_buttons);
        
        myscreen->draw_button(15,  9, 255, 199, 1, 1);
        myscreen->draw_text_bar(19, 13, 251, 21);
        strcpy(message, "Gladiator: Save Game");
        savetext.write_xy(135-(strlen(message)*3), 15, message, RED, 1);
        for (i=0; i < 10; i++)
        {
            sprintf(temp_filename, "save%d", i+1);
            allbuttons[i]->label = get_saved_name(temp_filename);
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
                           
        draw_highlight(buttons[highlighted_button]);
        myscreen->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
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
		temp += (Sint32)(calculate_exp(ob->level) - oldguy->exp);

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

	//myscreen->buffer_to_screen(52, 24, 108, 64);

	grab_mouse();
	
	return OK;
}

void show_guy(Sint32 frames, Sint32 who, short centerx, short centery) // shows the current guy ..
{
	walker *mywalker;
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
	myscreen->draw_button(centerx - 80 + 54, centery - 45 + 26, centerx - 80 + 106, centery - 45 + 64, 1, 1);
	myscreen->draw_text_bar(centerx - 80 + 56, centery - 45 + 28, centerx - 80 + 104, centery - 45 + 62);
	mywalker->draw(myscreen->viewob[0]);
	delete mywalker;
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
			
			std::string name = ourteam[i]->name;
			if(prompt_for_string(&addtext, "NAME THIS CHARACTER", name))
                strncpy(ourteam[i]->name, name.c_str(), 12);
            
			grab_mouse();

			// Increment the next guy's number
			numbought[newfamily]++;

			// Ensure we have the right exp for our level
			ourteam[i]->exp = calculate_exp(ourteam[i]->level);

			// Grab a new, generic guy to be edited/bought
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
	static text savetext(myscreen);

	release_mouse();
	clear_keyboard();
	
	std::string name = allbuttons[arg1-1]->label;
	if(prompt_for_string(&savetext, "NAME YOUR SAVED GAME", name))
    {
        myscreen->save_data.save_name = name;
        
        char newname[8];
        snprintf(newname, 8, "save%d", arg1);
        if(myscreen->save_data.save(newname))
            timed_dialog("GAME SAVED");
        else
            timed_dialog("SAVE FAILED");
    }
    else
        timed_dialog("SAVE CANCELED");
    
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

Sint32 go_menu(Sint32 arg1)
{
	// Save the current team in memory to save0.gtl, and
	// run gladiator.
	static text gotext(myscreen);

	if (arg1)
		arg1 = 1;

	// Make sure we have a valid team
	if (myscreen->save_data.team_size < 1)
	{
		popup_dialog("NEED A TEAM!", "Please hire a\nteam before\nstarting the level");
		
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

   if (arg1)
       thisguy = arg1;
   else
       thisguy = myscreen->save_data.team_list[editguy];

   release_mouse();

   if (localbuttons)
       delete localbuttons;
    
	button* buttons = details_buttons;
	int num_buttons = 2;
	int highlighted_button = 0;
	
	buttons[1].hidden = !(thisguy->family == FAMILY_MAGE && thisguy->level >= 6) && !(thisguy->family == FAMILY_ORC && thisguy->level >= 5);
	localbuttons = init_buttons(buttons, num_buttons);

   //leftmouse(buttons);
   //localbuttons->leftclick(buttons);

   while ( !(retvalue & EXIT) )
   {
       show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
    
       bool pressed = handle_menu_nav(buttons, highlighted_button, retvalue);
       
       bool do_click = false;
       if(leftmouse(buttons))
       {
           detailmouse = query_mouse();
           do_click = true;
       }
       
       bool do_promote = !buttons[1].hidden && ((do_click && detailmouse[MOUSE_X] >= 160 &&
                   detailmouse[MOUSE_X] <= 315 &&
                   detailmouse[MOUSE_Y] >= 4   &&
                   detailmouse[MOUSE_Y] <= 66) || (pressed && highlighted_button == 1));
       if(do_promote)
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
        
        if(do_click)
            retvalue=localbuttons->leftclick(buttons);
       
       
    
        draw_backdrop();

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
       
       
       draw_buttons(buttons, num_buttons);
       draw_highlight_interior(buttons[highlighted_button]);
       myscreen->buffer_to_screen(0, 0, 320, 200);
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
   CampaignResult result = pick_campaign(myscreen, &myscreen->save_data);
   if(result.id.size() > 0)
   {
        // Load new campaign
        myscreen->save_data.current_campaign = result.id;
        myscreen->save_data.scen_num = load_campaign(result.id, myscreen->save_data.current_levels, result.first_level);
   }
   return REDRAW;
}

Sint32 do_set_scen_level(Sint32 arg1)
{
   static text savetext(myscreen);
   Sint32 templevel = myscreen->save_data.scen_num;
   
   templevel = pick_level(myscreen, myscreen->level_data.id);
   
   // Have some feedback if the level changed
   if(templevel != myscreen->level_data.id)
   {
       int old_id = myscreen->level_data.id;
       myscreen->level_data.id = templevel;
       if (templevel < 0 || !myscreen->level_data.load())
       {
            myscreen->clearbuffer();
            popup_dialog("Load Failed", "Invalid level file.");
            
           myscreen->level_data.id = old_id;
           if(!myscreen->level_data.load())
           {
                myscreen->clearbuffer();
                popup_dialog("Big problem", "Also failed to reload current level...");
           }
       }
       else  // We're good
       {
           myscreen->save_data.scen_num = templevel;
           Log("Set level to %d\n", templevel);
       }
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
   allbuttons[6]->label = message;

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

   allbuttons[18]->label = message;
   //allbuttons[18]->do_outline = 1;
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
   sprintf(message, "Hiring for Team %d", current_team_num + 1);

   allbuttons[2]->label = message;

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
   allbuttons[7]->label = message;
   //buffers: allbuttons[7]->vdisplay();
   //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

   return OK;
}
