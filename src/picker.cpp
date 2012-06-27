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
// Z's script: #include <process.h>
// Z's script: #include <i86.h> //_enable, _disable

#define DOWN(x) (72+x*15)
#define VIEW_DOWN(x) (10+x*20)
#define RAISE 1.85  // please also change in guy.cpp

#define EXIT 1 //these are leftclick return values, exit means leave picker
#define REDRAW 2 //we just exited a menu, so redraw your buttons
#define OK 4 //this function was successful, continue normal operation

#define MAXTEAM 24 //max # of guys on a team

#define BUTTON_HEIGHT 15

//int matherr (struct exception *);

FILE * open_misc_file(const char *, const char *);
FILE * open_misc_file(const char *, const char *, const char *);

void show_guy(long frames, long who); // shows the current guy ..
long name_guy(long arg); // rename (or name) the current_guy

void glad_main(screen *myscreen, long playermode);
const char* get_saved_name(const char * filename);
long do_set_scen_level(long arg1);

long leftmouse();
void family_name_copy(char *name, short family);

// Zardus: PORT: put in a backpics var here so we can free the pixie files themselves
unsigned char *backpics[5];
pixieN *backdrops[5];

// Zardus: FIX: this is from view.cpp, so that we can delete it here
extern options *theprefs;

//screen  *myscreen;
text  *mytext;
long *mymouse;     // hold mouse information
//char main_dir[80];
guy  *current_guy;// = new guy();
unsigned long money[4] = {5000, 5000, 5000, 5000};
unsigned long score[4] = {0, 0, 0, 0};
long scen_level = 1;
char  message[80];
long editguy = 0;        // Global for editing guys ..
unsigned char playermode=1;
unsigned char  *gladpic,*magepic;
pixieN  *gladpix,*magepix;
char levels[MAX_LEVELS];        // our level-completion status
FILE *loadgame; //for loading the default game
vbutton * localbuttons; //global so we can delete the buttons anywhere
guy *ourteam[MAXTEAM];
long teamsize = 0;
char save_file[40] = "SAVED GAME";
short current_team_num = 0;

long allowable_guys[] =
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

long current_type = 0; // guy type we're looking at

long numbought[NUM_FAMILIES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,0,0};

long costlist[NUM_FAMILIES] =
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
long statlist[NUM_FAMILIES][6] =
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

long statcosts[NUM_FAMILIES][6] =
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
long current_difficulty = 1; // setting 'normal'
long difficulty_level[DIFFICULTY_SETTINGS] =
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

void picker_main(long argc, char  **argv)
{
	long i;

	for (i=0; i < MAX_BUTTONS; i++)
		allbuttons[i] = NULL;

	for (i=0; i < MAXTEAM; i++)
		ourteam[i] = NULL;

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

	//gladpic = read_pixie_file("glad.pix");
	gladpic = read_pixie_file("title.pix"); // marbled gladiator title
	gladpix = new pixieN(gladpic, myscreen);


	//magepic = read_pixie_file("mage.pix");
	magepic = read_pixie_file("columns.pix");
	magepix = new pixieN(magepic, myscreen);

	// Get the mouse, timer, & keyboard ..
	grab_mouse();
	grab_timer();
	grab_keyboard();
	clear_keyboard();

	// Load the current saved game, if it exists .. (save0.gtl)
	loadgame = open_misc_file("save0.gtl", "save/");
	if (loadgame)
	{
		fclose(loadgame);
		load_team_list_one("save0");
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
		if (backpics[i])
		{
			free(backpics[i]);
			backpics[i] = NULL;
		}
	}

	for (i = 0; i < MAX_BUTTONS; i++)
	{
		if (allbuttons[i])
			delete allbuttons[i];
	}

	delete_all();
	delete mytext;
	delete myscreen;
	delete magepix;
	free(magepic);
	delete gladpix;
	free(gladpic);

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

        { "Player-v-Player: Allied", SDLK_p, 80, 160, 140, 10, ALLIED_MODE, -1},

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

        { "ESC", SDLK_ESCAPE, 100, 130, 120, 20, RETURN_MENU, EXIT},
        { "SET LEVEL", SDLK_s, 100, 170, 120, 20, DO_SET_SCEN_LEVEL, EXIT},

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
        { "NEXT", SDLK_n,  10, 40, 40, 20, CYCLE_TEAM_GUY, 1},
        { "PREV", SDLK_p,  110, 40, 40, 20, CYCLE_TEAM_GUY, -1},
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
        { "NEXT", SDLK_n,  10, 40, 40, 20, CYCLE_GUY, 1},
        { "PREV", SDLK_p,  110, 40, 40, 20, CYCLE_GUY, -1},
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

long leftmouse()
{
	long i = 0;
	long somebutton = -1;
	char * mousekeys = query_keyboard();

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

	for(i=0; i<MAXTEAM; i++)
	{
		if (ourteam[i])
		{
			if (numguys++ > 24)
				break;

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



long mainmenu(long arg1)
{
	vbutton *tempbuttons;
	long retvalue=0;
	long count;
	char message[80];

	if (arg1)
		arg1 = 1;

	// Set screen to black, to non-display
	//buffers: PORT:  for(i=0;i<256;i++)
	//buffers: PORT: set_palette_reg((unsigned char)i, 0, 0, 0);
	//buffers: PORT:  load_palette("our.pal", (char *)mypalette);

	if (localbuttons != NULL)
		delete localbuttons; //we'll make a new set

	localbuttons = buttonmenu(buttons1, 9);
	myscreen->clearbuffer();
	allbuttons[0]->set_graphic(FAMILY_NORMAL1);

	tempbuttons = localbuttons;
	count = 0;
	if (playermode==4)
	{
		allbuttons[2]->do_outline = 1;
		allbuttons[3]->do_outline = 0;
		allbuttons[4]->do_outline = 0;
		allbuttons[5]->do_outline = 0;
	}
	else if (playermode==3)
	{
		allbuttons[2]->do_outline = 0;
		allbuttons[3]->do_outline = 1;
		allbuttons[4]->do_outline = 0;
		allbuttons[5]->do_outline = 0;
	}
	else if (playermode==2)
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
	if (myscreen->allied_mode)
		strcpy(allbuttons[7]->label, "Player-v-Player: Allied");
	else
		strcpy(allbuttons[7]->label, "Player-v-Player: Enemy");

	while (allbuttons[count])
	{
		allbuttons[count]->vdisplay();
		count++;
	}

	if (localbuttons == NULL)
		return 1;

	gladpix->set_frame(0);
	gladpix->drawMix(15,  8, myscreen->viewob[0]);
	gladpix->set_frame(1);
	gladpix->drawMix(151,  8, myscreen->viewob[0]);
	magepix->set_frame(0);
	magepix->drawMix(12,40, myscreen->viewob[0]);
	magepix->set_frame(1);
	magepix->drawMix(242,40, myscreen->viewob[0]);
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
				localbuttons = buttonmenu(buttons1, 9);
				
				myscreen->clearfontbuffer();
				
				count = 0;
				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[6]->label, message);

				// Show the allied mode
				if (myscreen->allied_mode)
					strcpy(allbuttons[7]->label, "Player-v-Player: Allied");
				else
					strcpy(allbuttons[7]->label, "Player-v-Player: Enemy");

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
				gladpix->set_frame(0);
				gladpix->drawMix(15,  8, myscreen->viewob[0]);
				gladpix->set_frame(1);
				gladpix->drawMix(151,  8, myscreen->viewob[0]);
				magepix->set_frame(0);
				magepix->drawMix(12,40, myscreen->viewob[0]);
				magepix->set_frame(1);
				magepix->drawMix(242,40, myscreen->viewob[0]);
				//magepix->next_frame();


				if (playermode==4)
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
				else if (playermode==3)
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
				else if (playermode==2)
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
				if (myscreen->allied_mode)
					strcpy(allbuttons[7]->label, "Player-v-Player: Allied");
				else
					strcpy(allbuttons[7]->label, "Player-v-Player: Enemy");

				//myscreen->refresh();
				grab_mouse();

			}
			if (localbuttons && retvalue == OK)
			{
				tempbuttons = localbuttons;
				count = 0;
				if (playermode==4)
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
				else if (playermode==3)
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
				else if (playermode==2)
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
				if (myscreen->allied_mode)
					sprintf(message, "Player-v-Player: Allied");
				else
					sprintf(message, "Player-v-Player: Enemy");
				strcpy(allbuttons[7]->label, message);

				tempbuttons = localbuttons;
				count = 0;
				myscreen->clearfontbuffer();
				while (allbuttons[count])
				{
					allbuttons[count]->vdisplay();
					count++;
				}
			} // end of "OK" buttons
		}
	}
	delete tempbuttons;
	return retvalue;
}

long beginmenu(long arg1)
{
	long i,retvalue=0;

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

	localbuttons = buttonmenu(bteam, 8);

	myscreen->swap();

	if (localbuttons == NULL)
		return 1;


	for (i=0; i < 4; i++)
	{
		money[i] = 5000;
		score[i] = 0;
	}

	scen_level = 1;
	delete_all();
	current_guy = NULL;
	clear_levels();
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
			localbuttons = buttonmenu(bteam, 8);
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

long loadmenu(long arg1)
{
	long retvalue = 0;

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

long newmenu(long arg1)
{
	long retvalue = 0;

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

long nullmenu(long arg1)
{
	long retvalue = 0;
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

long create_team_menu(long arg1)
{
	long retvalue=0;

	if (arg1)
		arg1 = 1;

	if (localbuttons)
		delete localbuttons;

	myscreen->clearfontbuffer();

	//  myscreen->clear();
	localbuttons = buttonmenu(bteam,8);

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
			localbuttons = buttonmenu(bteam, 8);
			myscreen->buffer_to_screen(0,0,320,200);
			retvalue = 0;
		}

	}

	myscreen->clearfontbuffer();
	
	return REDRAW;
}

long create_view_menu(long arg1)
{
	long retvalue = 0;

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

long create_buy_menu(long arg1)
{
	long linesdown, retvalue = 0;
	char * inputkeyboard = query_keyboard();
	long i;
	long start_time = query_timer();
	unsigned char showcolor; // normally STAT_COLOR or STAT_CHANGED
	unsigned long current_cost;
	long clickvalue;

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
	sprintf(message, "CASH: %ld", money[current_team_num]);
	mytext->write_xy(180, 62, message,(unsigned char) DARK_BLUE, 1);
	current_cost = calculate_cost();
	mytext->write_xy(180, 72, "COST: ", DARK_BLUE, 1);
	sprintf(message, "      %lu", current_cost );
	if (current_cost > money[current_team_num])
		mytext->write_xy(180, 72, message, STAT_CHANGED, 1);
	else
		mytext->write_xy(180, 72, message, STAT_COLOR, 1);

	sprintf(message, "TOTAL SCORE: %ld", score[current_team_num]);
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

		if (inputkeyboard[SDLK_LCTRL])
		{
			if (inputkeyboard[SDLK_KP_PLUS])
			{
				money[current_team_num] += 1000;
				retvalue = OK;
			}
			if (inputkeyboard[SDLK_KP_MINUS])
			{
				money[current_team_num] -= 1000;
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
			sprintf(message, "CASH: %ld", money[current_team_num]);
			mytext->write_xy(180, 62, message,(unsigned char) DARK_BLUE, 1);
			current_cost = calculate_cost();
			mytext->write_xy(180, 72, "COST: ", DARK_BLUE, 1);
			sprintf(message, "      %lu", current_cost );
			if (current_cost > money[current_team_num])
				mytext->write_xy(180, 72, message, STAT_CHANGED, 1);
			else
				mytext->write_xy(180, 72, message, STAT_COLOR, 1);

			sprintf(message, "TOTAL SCORE: %ld", score[current_team_num]);
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

long create_edit_menu(long arg1)
{
	guy * here;
	long linesdown, i, retvalue=0;
	unsigned char showcolor;
	long start_time = query_timer();
	unsigned long current_cost;
	long clickvalue;

	if (arg1)
		arg1 = 1;

	if (teamsize < 1)
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
	here = ourteam[editguy];

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
		sprintf(message, " Exp / Kill: %ld ",
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
		sprintf(message, "   Accuracy: %ld%% ",
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
	sprintf(message, " EXPERIENCE: %ld", current_guy->exp);
	mytext->write_xy(180, 62+22, message,(unsigned char) DARK_BLUE, 1);
	sprintf(message, "CASH: %ld", money[current_guy->teamnum]);
	mytext->write_xy(180, 76+22, message,(unsigned char) DARK_BLUE, 1);
	current_cost = calculate_cost(here);
	mytext->write_xy(180, 86+22, "COST: ", DARK_BLUE, 1);
	sprintf(message, "      %lu", current_cost );
	if (current_cost > money[current_guy->teamnum])
		mytext->write_xy(180, 86+22, message, STAT_CHANGED, 1);
	else
		mytext->write_xy(180, 86+22, message, STAT_COLOR, 1);
	sprintf(message, "TOTAL SCORE: %ld", score[current_guy->teamnum]);
	mytext->write_xy(180, 102+22, message,(unsigned char) DARK_BLUE, 1);

	// Display our team setting ..
	sprintf(message, "Playing on Team %d", current_guy->teamnum+1);
	strcpy(allbuttons[18]->label, message);
	allbuttons[18]->vdisplay();

	myscreen->buffer_to_screen(0, 0, 320, 200);

	grab_mouse();

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
				sprintf(message, " Exp / Kill: %ld ",
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
				sprintf(message, "   Accuracy: %ld%% ",
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
			sprintf(message, " EXPERIENCE: %ld", current_guy->exp);
			mytext->write_xy(180, 62+22, message,(unsigned char) DARK_BLUE, 1);
			sprintf(message, "CASH: %ld", money[current_guy->teamnum]);
			mytext->write_xy(180, 76+22, message,(unsigned char) DARK_BLUE, 1);
			current_cost = calculate_cost(here);
			mytext->write_xy(180, 86+22, "COST: ", DARK_BLUE, 1);
			sprintf(message, "      %lu", current_cost );
			if (current_cost > money[current_guy->teamnum])
				mytext->write_xy(180, 86+22, message, STAT_CHANGED, 1);
			else
				mytext->write_xy(180, 86+22, message, STAT_COLOR, 1);
			sprintf(message, "TOTAL SCORE: %ld", score[current_guy->teamnum]);
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

long create_load_menu(long arg1)
{
	long retvalue=0;
	long i;
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
		sprintf(temp_filename, "save%ld", i+1);
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
			return REDRAW;
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
				sprintf(temp_filename, "save%ld", i+1);
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

long create_save_menu(long arg1)
{
	long retvalue=0;
	long i;
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
		sprintf(temp_filename, "save%ld", i+1);
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
				sprintf(temp_filename, "save%ld", i+1);
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

long increase_stat(long whatstat, long howmuch)
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

long decrease_stat(long whatstat, long howmuch)
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

unsigned long calculate_cost()
{
	guy  *ob = current_guy;
	long temp;
	long myfamily;

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
	temp += (long)((pow( (long)(ob->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * (long)statcosts[myfamily][BUT_STR]) ;
	temp += (long)((pow( (long)(ob->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * (long)statcosts[myfamily][BUT_DEX]);
	temp += (long)((pow( (long)(ob->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * (long)statcosts[myfamily][BUT_CON]);
	temp += (long)((pow( (long)(ob->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * (long)statcosts[myfamily][BUT_INT]);
	temp += (long)((pow( (long)(ob->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * (long)statcosts[myfamily][BUT_ARMOR]);
	temp += (long)((pow( (long)(ob->level - statlist[myfamily][BUT_LEVEL]), RAISE))
	               * (long)statcosts[myfamily][BUT_LEVEL]);
	if ((signed long) calculate_exp(ob->level) < 0) // overflow
		ob->level = 1;
	temp += (long) (calculate_exp(ob->level));
	if (temp < 0)
	{
		//guytemp = new guy(current_guy->family);
		//delete current_guy;
		//current_guy = guytemp;
		cycle_guy(0);
		//temp = -1;  // This used to be an error code checked by picker.cpp line 2213
		temp = 0;
	}
	return (unsigned long)temp;
}

// This version compares current_guy versus the old version ..
unsigned long calculate_cost(guy  *oldguy)
{
	guy  *ob = current_guy;
	long temp;
	long myfamily;

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
	temp += (long)((pow( (long)(ob->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * (long)statcosts[myfamily][BUT_STR]) ;
	temp += (long)((pow( (long)(ob->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * (long)statcosts[myfamily][BUT_DEX]);
	temp += (long)((pow( (long)(ob->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * (long)statcosts[myfamily][BUT_CON]);
	temp += (long)((pow( (long)(ob->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * (long)statcosts[myfamily][BUT_INT]);
	temp += (long)((pow( (long)(ob->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * (long)statcosts[myfamily][BUT_ARMOR]);
	temp += (long)((pow( (long)(ob->level - statlist[myfamily][BUT_LEVEL]), RAISE))
	               * (long)statcosts[myfamily][BUT_LEVEL]);

	// Now subtract what we've already paid for ..
	temp -= (long)((pow( (long)(oldguy->strength - statlist[myfamily][BUT_STR]), RAISE))
	               * (long)statcosts[myfamily][BUT_STR]);
	temp -= (long)((pow( (long)(oldguy->dexterity - statlist[myfamily][BUT_DEX]), RAISE))
	               * (long)statcosts[myfamily][BUT_DEX]);
	temp -= (long)((pow( (long)(oldguy->constitution - statlist[myfamily][BUT_CON]), RAISE))
	               * (long)statcosts[myfamily][BUT_CON]);
	temp -= (long)((pow( (long)(oldguy->intelligence - statlist[myfamily][BUT_INT]), RAISE))
	               * (long)statcosts[myfamily][BUT_INT]);
	temp -= (long)((pow( (long)(oldguy->armor - statlist[myfamily][BUT_ARMOR]), RAISE))
	               * (long)statcosts[myfamily][BUT_ARMOR]);
	temp -= (long)((pow( (long)(oldguy->level - statlist[myfamily][BUT_LEVEL]), RAISE))
	               * (long)statcosts[myfamily][BUT_LEVEL]);

	// Add on extra level cost ..
	if (calculate_exp(ob->level) > oldguy->exp)
		temp += (long) ( ( (calculate_exp(ob->level) ) - oldguy->exp)*(ob->level-1) );

	if (temp < 0)
	{
		temp = 0;
		statscopy(current_guy, oldguy);
		cycle_team_guy(0);

	}

	return (unsigned long)temp;
}

long cycle_guy(long whichway)
{
	long newfamily;
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
	sprintf(tempnum, "%ld", numbought[newfamily]+1);
	strcat(current_guy->name, tempnum);

	show_guy(0, 0);

	//myscreen->refresh();
	myscreen->buffer_to_screen(52, 24, 108, 64);

	grab_mouse();
	//myscreen->refresh();
	return OK;
}

void show_guy(long frames, long who) // shows the current guy ..
{
	walker *mywalker;
	short centerx = 80, centery = 45; // center for walker
	long i;
	long newfamily;


	if (!current_guy)
		return;

	frames = abs(frames);

	if (who == 0) // use current_type of guy
		newfamily = allowable_guys[current_type];
	else
		newfamily = ourteam[editguy]->family;
	newfamily = current_guy->family;

	mywalker = myscreen->myloader->create_walker(ORDER_LIVING,
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
long cycle_team_guy(long whichway)
{
	if (teamsize < 1)
		return -1;

	editguy += whichway;
	if (editguy <0)
	{
		editguy += MAXTEAM;
		while (!ourteam[editguy])
			editguy--;
	}

	if (editguy < 0 || editguy >= MAXTEAM)
		editguy = 0;

	if (!whichway && !ourteam[editguy])
		whichway = 1;

	while (!ourteam[editguy])
	{
		editguy += whichway;
		if (editguy < 0 || editguy >= MAXTEAM)
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

long add_guy(guy *newguy)
{
	long i;

	for (i=0; i < MAXTEAM; i++)
		if (!ourteam[i])
		{
			ourteam[i] = newguy;
			teamsize++;
			return i;
		}

	// failed the case; too many guys
	return -1;
}

long name_guy(long arg)  // 0 == current_guy, 1 == ourteam[editguy]
{
	text nametext(myscreen);
	char tempnum[30];  // don't ask
	guy *someguy;

	if (arg)
		someguy = ourteam[editguy];
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
	strcpy(tempnum, nametext.input_string(176, 20, 11, someguy->name) );
	strcpy(someguy->name, tempnum);
	//release_keyboard();
	myscreen->draw_button(174,  8, 306, 30, 1, 1); // text box

	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();

	return REDRAW;
}

long add_guy(long ignoreme)
{
	long newfamily = current_guy->family;
	char tempnum[12];
	//buffers: changed typename to type_name due to some compile error
	char type_name[30];
	static text addtext(myscreen);
	long i;

	if (teamsize >= MAXTEAM) // abort abort!
		return -1;

	if (!current_guy) // we should be adding current_guy
		return -1;

	if (calculate_cost() > money[current_team_num] || calculate_cost() == 0)
		return OK;

	money[current_team_num] -= calculate_cost();

	for (i=0; i < MAXTEAM; i++)
		if (!ourteam[i]) // found an empty slot
		{
			current_guy->teamnum = current_team_num;
			ourteam[i] = current_guy;
			teamsize++;
			current_guy = NULL;
			release_mouse();
			myscreen->draw_button(174, 20, 306, 42, 1, 1); // text box
			addtext.write_xy(176, 24, "NAME THIS CHARACTER:", DARK_BLUE, 1);
			myscreen->buffer_to_screen(0, 0, 320, 200);
			clear_keyboard();
			strcpy(tempnum, addtext.input_string(176, 32, 11, ourteam[i]->name) );
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
			sprintf(tempnum, "%ld", numbought[newfamily]+1);
			strcat(current_guy->name, tempnum);

			// Return okay status
			return OK;
		}

	return OK;
}

// Accept changes ..
long edit_guy(long arg1)
{
	guy *here;
	long *cheatmouse = query_mouse();

	if (arg1)
		arg1 = 1;

	if (!current_guy)
		return -1;

	here = ourteam[editguy];
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

	if ( (calculate_cost(here) > money[current_guy->teamnum]) ||  // compare cost of here to current_guy
	        (calculate_cost(here) < 0) )
		return OK;

	money[current_guy->teamnum] -= calculate_cost(here);  // cost of new - old (current_guy - here)

	if (here->level != current_guy->level)
		current_guy->exp = calculate_exp(current_guy->level);
	statscopy(here, current_guy);

	// Color our team button normally
	allbuttons[18]->do_outline = 0;

	return OK;
}

long how_many(long whatfamily)    // how many guys of family X on the team?
{
	long counter = 0;
	long i;

	for (i=0; i < MAXTEAM; i++)
		if (ourteam[i] && ourteam[i]->family == whatfamily)
			counter++;

	return counter;
}

long do_save(long arg1)
{
	char newname[8];
	char newnum[8];
	static text savetext(myscreen);
	long xloc, yloc, x2loc, y2loc;

	strcpy(newname, "save");

	//buffers: PORT: changed itoa to sprintf, might be BUGGY
	//itoa(arg1, newnum, 10);
	sprintf(newnum,"%li",arg1);

	strcat(newname, newnum);
	release_mouse();
	clear_keyboard();
	xloc = allbuttons[arg1-1]->xloc;
	yloc = allbuttons[arg1-1]->yloc;
	x2loc = allbuttons[arg1-1]->width + xloc;
	y2loc = allbuttons[arg1-1]->height + yloc + 10;
	myscreen->draw_button(xloc, yloc, x2loc, y2loc, 2, 1);
	
	myscreen->clearfontbuffer(xloc,yloc,x2loc-xloc,y2loc-yloc);
	
	savetext.write_xy(xloc+5, yloc+4, "NAME YOUR SAVED GAME:", DARK_BLUE, 1);
	strcpy(save_file, savetext.input_string(xloc+5, yloc+11, 35, allbuttons[arg1-1]->label) );
	myscreen->draw_box(xloc, yloc, x2loc, y2loc, 0, 1, 1);
	myscreen->buffer_to_screen(0, 0, 320, 200);
	save_team_list(newname);
	grab_mouse();

	return REDRAW;
}

long do_load(long arg1)
{
	char newname[8];
	char newnum[8];

	// First delete the old team ..
	delete_all();
	//printf("Guys deleted: %d\n", delete_all());
	//commented out debugging done
	//myscreen->soundp->play_sound(SOUND_YO);
	strcpy(newname, "save");

	//buffers: PORT: changed itoa to sprintf
	//itoa(arg1, newnum, 10);
	sprintf(newnum,"%li",arg1);

	strcat(newname, newnum);
	load_team_list_one(newname);
	return REDRAW;
}

long save_team_list(const char * filename)
{
	char filler[50];// = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	FILE  *outfile;
	char temp_filename[80];
	char savedgame[40]; // for 38-byte saved game name
	guy *here;
	long i, j;

	char temptext[10];
	char temp_version = 7;
	unsigned char temp_playermode = playermode;
	long next_scenario = scen_level;
	unsigned long newcash = money[0];
	unsigned long newscore = score[0];
	//  long numguys;
	long listsize;

	char tempname[12] = "FRED";
	char guyname[12] = "JOE";
	char temp_order, temp_family;
	long temp_str, temp_dex, temp_con;
	long temp_int, temp_arm, temp_lev;
	unsigned  long temp_exp;
	long temp_kills, temp_level_kills;
	long temp_td, temp_th, temp_ts;
	short temp_teamnum;
	short temp_allied;
	short temp_registered;

	outfile = NULL;
	strcpy(temptext, "GTL");
	strcpy(filler, "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL");

	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number (from graph.h)
	// 2-bytes Registered or not          // Version 7+
	// 40-byte saved game name (version 2 and up only!)
	// 2-bytes (int) = scenario number
	// 4-bytes (long)= cash (unsigned)
	// 4-bytes (long)= score (unsigned)
	// 4-bytes (long)= cash-B (unsigned)   // All alternate scores
	// 4-bytes (long)= score-B (unsigned)  // version 6+
	// 4-bytes (long)= cash-C (unsigned)
	// 4-bytes (long)= score-C (unsigned)
	// 4-bytes (long)= cash-D (unsigned)
	// 4-bytes (long)= score-D (unsigned)
	// 2-bytes allied setting              // Version 7+
	// 2-bytes (int) = # of team members in list
	// 1-byte number of players
	// 31-bytes RESERVED
	// List of n objects, each of 58-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 12-byte name
	// 2-bytes strength
	// 2-bytes dexterity
	// 2-bytes constitution
	// 2-bytes intelligence
	// 2-bytes armor
	// 2-bytes level
	// 4-bytes unsigned long exp
	// 2-bytes # kills, v.3+
	// 4-bytes # level kills, v.3+
	// 4-bytes total damage done, v.4+
	// 4-bytes total hits, v.4+
	// 4-bytes total shots, v.4+
	// 2-bytes team number, v.5+
	// 2*4 = 8 bytes RESERVED
	// List of 500 (max levels) 1-byte scenario status flags

	//strcpy(temp_filename, scen_directory);
	strcpy(temp_filename, filename);
	strcat(temp_filename, ".gtl"); // gladiator team list

	release_mouse();

	//buffers: PORT: dont need this? _disable(); //disable interrupts

	outfile = open_misc_file(temp_filename, "save/", "wb");
	//myscreen->restore_ints();

	//buffers: PORT: dont need this? _enable(); //enable interrupts
	grab_mouse();

	if (!outfile)
		return 0;

	// Write id header
	fwrite(temptext, 3, 1, outfile);

	// Write version number
	fwrite(&temp_version, 1, 1, outfile);

	// Versions 7+ include a mark for registered or not
	temp_registered = 0;
	temp_registered = 1;
	fwrite(&temp_registered, 2, 1, outfile);

	// Write the saved game name
	strcpy(savedgame, save_file);  // save_file is global, 20-char name
	for (i=strlen(savedgame); i < 40; i++)
		savedgame[i] = 0;
	fwrite(savedgame, 40, 1, outfile);

	// Write scenario number
	fwrite(&next_scenario, 2, 1, outfile);

	// Write cash
	fwrite(&newcash, 4, 1, outfile);
	// Write score
	fwrite(&newscore, 4, 1, outfile);

	// Versions 6+ have a score for each possible team
	for (i=0; i < 4; i++)
	{
		newcash = money[i];
		fwrite(&newcash, 4, 1, outfile);
		newscore = score[i];
		fwrite(&newscore, 4, 1, outfile);
	}

	// Versions 7+ include the allied mode information
	temp_allied = myscreen->allied_mode;
	fwrite(&temp_allied, 2, 1, outfile);

	// Determine size of team list ...
	listsize = teamsize;

	fwrite(&listsize, 2, 1, outfile);

	//write number of players
	fwrite(&temp_playermode,1,1,outfile);

	// Write the reserved area, 31 bytes
	fwrite(filler, 31, 1, outfile);

	// Okay, we've written header .. now dump the data ..
	for (i=0; i < MAXTEAM; i++)
	{
		if (ourteam[i])
		{
			here = ourteam[i];
			// Get temp values to be saved
			temp_order = ORDER_LIVING; // may be changed later
			temp_family= here->family;
			// Write name of current guy...
			if (strlen(here->name))
				strcpy(guyname, here->name);
			else
				strcpy(guyname, tempname);
			// Set any chars under 12 not used to 0 ..
			for (j=strlen(guyname); j < 12; j++)
				guyname[j] = 0;
			temp_str = here->strength;
			temp_dex = here->dexterity;
			temp_con = here->constitution;
			temp_int = here->intelligence;
			temp_arm = here->armor;
			temp_lev = here->level;
			temp_exp = here->exp;
			temp_kills = here->kills; // v.3+
			temp_level_kills = here->level_kills; // v.3+
			temp_td = here->total_damage; // v.4+
			temp_th = here->total_hits;   // v.4+
			temp_ts = here->total_shots;  // v.4+
			temp_teamnum = here->teamnum; // v.5+

			// Now write all those values
			fwrite(&temp_order, 1, 1, outfile);
			fwrite(&temp_family,1, 1, outfile);
			fwrite(guyname, 12, 1, outfile);
			fwrite(&temp_str, 2, 1, outfile);
			fwrite(&temp_dex, 2, 1, outfile);
			fwrite(&temp_con, 2, 1, outfile);
			fwrite(&temp_int, 2, 1, outfile);
			fwrite(&temp_arm, 2, 1, outfile);
			fwrite(&temp_lev, 2, 1, outfile);
			fwrite(&temp_exp, 4, 1, outfile);
			fwrite(&temp_kills, 2, 1, outfile);
			fwrite(&temp_level_kills, 4, 1, outfile);
			fwrite(&temp_td, 4, 1, outfile); // v.4+
			fwrite(&temp_th, 4, 1, outfile); // v.4+
			fwrite(&temp_ts, 4, 1, outfile); // v.4+
			fwrite(&temp_teamnum, 2, 1, outfile); // v.5+

			// And the filler
			fwrite(filler, 8, 1, outfile);

		} // end of found valid guy in slot
	}

	// Write our level status ..
	fwrite(levels, 500, 1, outfile);

	fclose(outfile);
	return 1;
}

long load_team_list_one(const char * filename)
{
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	FILE  *infile;
	char temp_filename[80];

	char temptext[10] = "GTL";
	char savedgame[40];
	char temp_version = 7;
	long next_scenario = 1;
	unsigned long newcash = money[0];
	unsigned long newscore = 0;
	//  long numguys;
	long listsize = 0;
	long i;
	unsigned char temp_playermode;

	char tempname[12] = "FRED";
	char guyname[12] = "JOE";
	char temp_order, temp_family;

	long temp_str, temp_dex, temp_con;
	long temp_int, temp_arm, temp_lev;
	unsigned long temp_exp;
	guy  *tempguy;
	long temp_kills, temp_level_kills;
	long temp_td, temp_th, temp_ts;
	short temp_teamnum;           // v.5+
	short temp_allied;            // v.7+
	short temp_registered;        // v.7+

	teamsize = 0;  // reset our current team size; this is added in add_guy

	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number (from graph.h)
	// 2-bytes registered mark            // Versions 7+
	// 40-byte saved-game name (version 2 and up only!)
	// 2-bytes (int) = scenario number
	// 4-bytes (long)= cash (unsigned)
	// 4-bytes (long)= score (unsigned)
	// 4-bytes (long)= cash-B (unsigned)   // All alternate scores
	// 4-bytes (long)= score-B (unsigned)  // version 6+
	// 4-bytes (long)= cash-C (unsigned)
	// 4-bytes (long)= score-C (unsigned)
	// 4-bytes (long)= cash-D (unsigned)
	// 4-bytes (long)= score-D (unsigned)
	// 2-bytes Allied mode                // Versions 7+
	// 2-bytes (int) = # of team members in list
	// 1-byte number of players
	// 31-bytes RESERVED
	// List of n objects, each of 58-bytes of form:
	// 1-byte ORDER
	// 1-byte FAMILY
	// 12-byte name
	// 2-bytes strength
	// 2-bytes dexterity
	// 2-bytes constitution
	// 2-bytes intelligence
	// 2-bytes armor
	// 2-bytes level
	// 4-bytes unsigned long exp
	// 2-bytes # kills, v. 3+
	// 4-bytes total level kills, v. 3+
	// 4-bytes total damage done, v.4+
	// 4-bytes total hits inflicted, v.4+
	// 4-bytes total shots made, v.4+
	// 2-bytes team number, v.5+
	// 2*4 = 8 bytes RESERVED
	// List of 200 or 500 (max levels) 1-byte scenario-level status

	strcpy(temp_filename, filename);
	//buffers: PORT: changed .GTL to .gtl
	strcat(temp_filename, ".gtl"); // gladiator team list

	// Reset the number of guys bought ..
	for (i=0; i < NUM_FAMILIES; i++)
		numbought[i] = 0;

	if ( (infile = open_misc_file(temp_filename, "save/")) == NULL ) // open for write
	{
		//gotoxy(1, 22);
		//buffers: DEBUG: uncommented following line
		printf("Error in opening team file: %s\n", filename);
		return 0;
	}

	// Read id header
	fread(temptext, 3, 1, infile);
	if ( strcmp(temptext,"GTL"))
	{
		fclose(infile);
		//buffers: DEBUG: uncommented following line
		printf("Error, selected file is not a GTL file: %s\n",filename);
		return 0; //not a gtl file
	}

	// Read version number
	fread(&temp_version, 1, 1, infile);

	// Versions 7+ have a registered mark ..
	if (temp_version >= 7)
	{
		fread(&temp_registered, 2, 1, infile);
	}

	// Do stuff based on the version number
	if (temp_version != 1)
	{
		if (temp_version >= 2)
		{
			fread(savedgame, 40, 1, infile);
		}
		else
		{
			fclose(infile);
			//buffers: DEBUG: uncommented following line
			printf("Error, selected file is not version one: %s\n",filename);
			return 0;
		}
	}
	else
		strcpy(savedgame, "SAVED GAME"); // fake the game name

	// Read scenario number
	fread(&next_scenario, 2, 1, infile);
	scen_level = next_scenario;

	// Read cash
	fread(&newcash, 4, 1, infile);
	money[0] = newcash;
	// Read score
	fread(&newscore, 4, 1, infile);
	score[0] = newscore;

	// Versions 6+ have a score for each possible team, 0-3
	if (temp_version >= 6)
	{
		for (i=0; i < 4; i++)
		{
			fread(&newcash, 4, 1, infile);
			money[i] = newcash;
			fread(&newscore, 4, 1, infile);
			score[i] = newscore;
		}
	}

	// Versions 7+ have the allied information ..
	if (temp_version >= 7)
	{
		fread(&temp_allied, 2, 1, infile);
		myscreen->allied_mode = temp_allied;
	}

	// Determine size of team list ...
	fread(&listsize, 2, 1, infile);

	//read number of players
	fread(&temp_playermode, 1, 1, infile);
	playermode = temp_playermode;

	// Read the reserved area, 31 bytes
	fread(filler, 31, 1, infile);

	// Delete any team in memory ..
	for (i=0; i < MAXTEAM; i++)
		if (ourteam[i])
		{
			delete ourteam[i];
			ourteam[i] = NULL;
		}

	// Okay, we've read header .. now read the team list data ..

	if (current_guy)
	{
		delete current_guy;
		current_guy = NULL;
	}

	//current_guy = new guy();

	while (listsize--)
	{
		tempguy = new guy();
		// Get temp values to be read
		temp_order = ORDER_LIVING; // may be changed later
		// Read name of current guy...
		strcpy(guyname, tempname);
		// Set any chars under 12 not used to 0 ..
		for (i=strlen(guyname); i < 12; i++)
			guyname[i] = 0;
		// Now write all those values
		fread(&temp_order, 1, 1, infile);
		fread(&temp_family,1, 1, infile);
		fread(guyname, 12, 1, infile);
		fread(&temp_str, 2, 1, infile);
		fread(&temp_dex, 2, 1, infile);
		fread(&temp_con, 2, 1, infile);
		fread(&temp_int, 2, 1, infile);
		fread(&temp_arm, 2, 1, infile);
		fread(&temp_lev, 2, 1, infile);
		fread(&temp_exp, 4, 1, infile);
		fread(&temp_kills, 2, 1, infile);
		fread(&temp_level_kills, 4, 1, infile);
		fread(&temp_td, 4, 1, infile); // v.4+
		fread(&temp_th, 4, 1, infile); // v.4+
		fread(&temp_ts, 4, 1, infile); // v.4+
		fread(&temp_teamnum, 2, 1, infile); // v.5+

		// And the filler
		fread(filler, 8, 1, infile);
		// Now set the values ..
		tempguy->family       = temp_family;
		strcpy(tempguy->name,guyname);
		tempguy->strength     = temp_str;
		tempguy->dexterity    = temp_dex;
		tempguy->constitution = temp_con;
		tempguy->intelligence = temp_int;
		tempguy->armor        = temp_arm;
		tempguy->level        = temp_lev;
		tempguy->exp          = temp_exp;
		if (temp_version >= 3)
		{
			tempguy->kills = temp_kills;
			tempguy->level_kills = temp_level_kills;
		}
		else
		{
			tempguy->kills = 0;
			tempguy->level_kills = 0;
		}
		// Version 4+ stuff:
		if (temp_version >= 4)
		{
			tempguy->total_damage = temp_td;
			tempguy->total_hits   = temp_th;
			tempguy->total_shots  = temp_ts;
		}
		else
		{
			tempguy->total_damage = 0;
			tempguy->total_hits   = 0;
			tempguy->total_shots  = 0;
		}
		// Version 5.+ stuff:
		if (temp_version >= 5)
		{
			tempguy->teamnum = temp_teamnum;
		}
		else
		{
			tempguy->teamnum = 0;
		}

		// Recalculate guy's level ..
		tempguy->level = calculate_level(temp_exp);

		// Count him as 'bought'
		numbought[(int)temp_family]++;

		// Advance to the next guy ..
		add_guy(tempguy);
	}

	// Read the level status
	// First, clear the status ..
	memset( levels, 0, 500 );
	if (temp_version >= 5)
		fread(levels, 500, 1, infile);
	else
		fread(levels, 200, 1, infile);

	fclose(infile);

	return 1;
}

const char* get_saved_name(const char * filename)
{
	FILE  *infile;
	char temp_filename[80];

	char temptext[10] = "GTL";
	static char savedgame[40];
	char temp_version = 1;
	short temp_registered;

	// Format of a team list file is:
	// 3-byte header: 'GTL'
	// 1-byte version number (from graph.h)
	// 2-bytes registered mark, version 7+ only
	// 40-byte saved-game name (version 2 and up only!)
	//   .
	//   .
	//   .

	strcpy(temp_filename, filename);
	//buffers: PORT: changed .GTL to .gtl
	strcat(temp_filename, ".gtl"); // gladiator team list

	if ( (infile = open_misc_file(temp_filename, "save/")) == NULL ) // open for read
	{
		return "EMPTY SLOT";
	}

	// Read id header
	fread(temptext, 3, 1, infile);
	if ( strcmp(temptext,"GTL"))
	{
		fclose(infile);
		strcpy(savedgame, "EMPTY SLOT");
		return savedgame;
	}

	// Read version number
	fread(&temp_version, 1, 1, infile);
	if (temp_version != 1)
	{
		if (temp_version >= 2)
		{
			if (temp_version >= 7)
				fread(&temp_registered, 2, 1, infile);
			fread(savedgame, 40, 1, infile);
		}
		else
		{
			fclose(infile);
			strcpy(savedgame, "SAVED GAME");
		}
	}
	else
		strcpy(savedgame, "SAVED GAME"); // fake the game name

	fclose(infile);
	return (savedgame);
}

long delete_all()
{
	long i;
	long counter = teamsize;

	for (i=0; i < MAXTEAM; i++)
		if (ourteam[i])
		{
			delete ourteam[i];
			ourteam[i] = NULL;
		}

	teamsize = 0;

	return counter;
}

long add_money(long howmuch)
{
	money[current_guy->teamnum] += (long) howmuch;
	return money[current_guy->teamnum];
}

long go_menu(long arg1)
{
	// Save the current team in memory to save0.gtl, and
	// run gladiator.
	static text gotext(myscreen);
	long temptime;

	if (arg1)
		arg1 = 1;

	// Make sure we have a valid team
	if (!ourteam[0])
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
	save_team_list("save0");
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
	myscreen->reset(playermode);

	glad_main(myscreen, playermode);
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

	loadgame = open_misc_file("save0.gtl", "save/");
	if (loadgame)
	{
		fclose(loadgame);
		load_team_list_one("save0");
	}
	// Zardus: PORT: this obviously causes it to segfault
	//else
	//	fclose(loadgame);

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

long quit(long arg1)
{
	if (arg1)
		arg1 = 1;
	release_mouse();

	myscreen->refresh();

	delete theprefs;
	picker_quit();
	release_keyboard();
	stop_input();
	exit(1);
	return 1;
}

long set_player_mode(long howmany)
{
	long count = 0;
	playermode = howmany;

	while (allbuttons[count])
	{
		allbuttons[count]->vdisplay();
		count++;
	}
	//buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

	return OK;
}

long calculate_level(unsigned long experience)
{
	long result=1;

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

unsigned long calculate_exp(long level)
{


	/*
	  if (level > 13)
	    return (long) (2*calculate_exp(level-1) );
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
		return (long) ( (8000*(level+10)) / 10) + calculate_exp(level-1);
	else if (level > 1)
		return (long) 8000;
	else
		return (long) 0;
}

void clear_levels()
{
	long i;

	// Set all of our level-completion status to off
	for (i=0; i < MAX_LEVELS; i++)
		levels[i] = 0;

}


//new functions
long return_menu(long arg)
           {
	           return arg;
           }

           long create_detail_menu(guy *arg1)
           {
#define DETAIL_LM 11             // left edge margin ..
#define DETAIL_MM 164            // center margin
#define DETAIL_LD(x) (90+(x*6))  // vertical line for text
#define WL(p,m) if (m[1] != ' ') mytext->write_xy(DETAIL_LM, DETAIL_LD(p), m, RED, 1); else mytext->write_xy(DETAIL_LM, DETAIL_LD(p), m, DARK_BLUE, 1)
#define WR(p,m) if (m[1] != ' ') mytext->write_xy(DETAIL_MM, DETAIL_LD(p), m, RED, 1); else mytext->write_xy(DETAIL_MM, DETAIL_LD(p), m, DARK_BLUE, 1)

	           long retvalue = 0;
	           guy *thisguy;
	           long start_time = query_timer();
	           long *detailmouse;

	           release_mouse();

			myscreen->clearfontbuffer();

	           if (localbuttons)
		           delete localbuttons;
	           localbuttons = buttonmenu(detailed, 1, 0);

	           if (arg1)
		           thisguy = arg1;
	           else
		           thisguy = ourteam[editguy];

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
				           WL(8, "  The boomerang files  ");
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
				           WR(4, " Energy Burst");
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
				           WR(5, " Energy Burst");
				           WR(6, "  Send a growing ripple ");
				           WR(7, "  of energy through     ");
				           WR(8 ,"  walls and foes.       ");
			           }
			           // Level 13 things
			           if (thisguy->level >= 13)
			           {
				           WR(9, " Mind Control");
				           WR(10,"  Convert nearby foes to");
				           WR(11,"  your team, for a time.");
			           }
			           break;

		           case FAMILY_CLERIC:
			           sprintf(message, "Level %d Cleric has:", thisguy->level);
			           mytext->write_xy(DETAIL_LM+1, DETAIL_LD(0)+1, message, 10, 1);
			           mytext->write_xy(DETAIL_LM, DETAIL_LD(0), message, DARK_BLUE, 1);
			           // Level 1 things
			           WL(2, " Heal            ");
			           WL(3, "  Heal all teammages who ");
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
				           WR(5, " Ressurection");
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


           long do_set_scen_level(long arg1)
           {
	           char newname[8];
	           char newnum[8];
	           static text savetext(myscreen);
	           long xloc, yloc, x2loc, y2loc;
	           long templevel;
	           long temptime;


	           //myscreen->soundp->play_sound(SOUND_YO);

	           release_mouse();
	           //grab_keyboard();
	           clear_keyboard();

	           xloc = 100;
	           yloc = 170;
	           x2loc = 220;
	           y2loc = 190;

		myscreen->clearfontbuffer(xloc,yloc,x2loc,y2loc);

	           myscreen->draw_button(xloc, yloc, x2loc, y2loc, 2, 1);
	           savetext.write_xy(xloc+5, yloc+4, "ENTER LEVEL NUMBER:", DARK_BLUE, 1);

	           //buffers: changed itoa to sprintf
	           //itoa(scen_level, newnum, 10);
	           sprintf(newnum,"%li",scen_level);

	           templevel = atoi(savetext.input_string(xloc+50, yloc+11, 8, newnum) );

	           //buffers: changed itoa to sprintf
	           //itoa(templevel, newnum, 10);
	           sprintf(newnum,"%li",templevel);

	           strcpy(newname, "scen");
	           strcat(newname, newnum);
	           if (!load_scenario(newname, myscreen))
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
		           myscreen->buffer_to_screen(0, 0, 320, 200);
		           scen_level = templevel;
	           }

	           //release_keyboard();
	           grab_mouse();
	           //myscreen->soundp->play_sound(SOUND_CHARGE);

	           return REDRAW;
           }

           /*
           int matherr(struct exception *problem)
           {
             // Do nothing
             return 0;
           }
           */

           long set_difficulty()
           {
	           char message[80];

	           current_difficulty = (current_difficulty + 1) % DIFFICULTY_SETTINGS;
	           sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
	           strcpy(allbuttons[6]->label, message);

	           //allbuttons[6]->vdisplay();
	           //myscreen->buffer_to_screen(0, 0, 320, 200);

	           return OK;
           }

           long change_teamnum(long arg)
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

           long change_hire_teamnum(long arg)
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

           long change_allied()
           {
	           // Change our allied mode (on or off)
	           char message[80];

	           myscreen->allied_mode += 1;
	           myscreen->allied_mode %= 2;

	           if (myscreen->allied_mode)
		           sprintf(message, "Player-v-Player: Allied");
	           else
		           sprintf(message, "Player-v-Player: Enemy");

	           // Update our button display
	           strcpy(allbuttons[7]->label, message);
	           //buffers: allbuttons[7]->vdisplay();
	           //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

	           return OK;
           }
