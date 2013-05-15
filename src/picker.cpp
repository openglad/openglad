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

// For yes/no prompts
#define YES 5
#define NO 6
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);

#define MAXTEAM 24 //max # of guys on a team

#define BUTTON_HEIGHT 15

//int matherr (struct exception *);

void show_guy(Sint32 frames, Sint32 who); // shows the current guy ..
Sint32 name_guy(Sint32 arg); // rename (or name) the current_guy

void glad_main(screen *myscreen, Sint32 playermode);
const char* get_saved_name(const char * filename);
Sint32 do_set_scen_level(Sint32 arg1);

Sint32 leftmouse();
void family_name_copy(char *name, short family);

// Zardus: PORT: put in a backpics var here so we can free the pixie files themselves
unsigned char *backpics[5];
pixieN *backdrops[5];

// Zardus: FIX: this is from view.cpp, so that we can delete it here
extern options *theprefs;

//screen  *myscreen;
text  *mytext;
Sint32 *mymouse;     // hold mouse information
//char main_dir[80];
guy  *current_guy;// = new guy();
Uint32 money[4] = {5000, 5000, 5000, 5000};
Uint32 score[4] = {0, 0, 0, 0};
Sint32 scen_level = 1;
char  message[80];
Sint32 editguy = 0;        // Global for editing guys ..
unsigned char playermode=1;
unsigned char  *gladpic,*magepic;
pixieN  *gladpix,*magepix;
char levels[MAX_LEVELS];        // our level-completion status
SDL_RWops *loadgame; //for loading the default game
vbutton * localbuttons; //global so we can delete the buttons anywhere
guy *ourteam[MAXTEAM];
Sint32 teamsize = 0;
char save_file[40] = "SAVED GAME";
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

Sint32 numbought[NUM_FAMILIES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,0,0};

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
	loadgame = open_user_file("save0.gtl", "save/");
	if (loadgame)
	{
	    SDL_RWclose(loadgame);
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
        { "", SDLK_b, 80, 70, 140, 20, BEGINMENU, -1 }, // BEGIN NEW GAME
        { "CONTINUE GAME", SDLK_c, 80, 95, 140, 20, CREATE_TEAM_MENU, -1 },

        { "DIFFICULTY", SDLK_d, 80, 120, 140, 10, SET_DIFFICULTY, -1},

        { "QUIT", SDLK_ESCAPE, 80, 135, 140, 20, QUIT_MENU, -1 },
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

Sint32 leftmouse()
{
	Sint32 i = 0;
	Sint32 somebutton = -1;
	Uint8* mousekeys = query_keyboard();

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

	localbuttons = buttonmenu(buttons1, 4);
	myscreen->clearbuffer();
	allbuttons[0]->set_graphic(FAMILY_NORMAL1);

	tempbuttons = localbuttons;
	count = 0;
	sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
	strcpy(allbuttons[2]->label, message);

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
				localbuttons = buttonmenu(buttons1, 4);
				
				myscreen->clearfontbuffer();
				
				count = 0;
				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[2]->label, message);

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


				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[2]->label, message);

				//myscreen->refresh();
				grab_mouse();

			}
			if (localbuttons && retvalue == OK)
			{
				tempbuttons = localbuttons;
				count = 0;

				sprintf(message, "Difficulty: %s", difficulty_names[current_difficulty]);
				strcpy(allbuttons[2]->label, message);

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
	localbuttons = buttonmenu(bteam,8);
	
	myscreen->fadeblack(1);

	myscreen->buffer_to_screen(0,0,320,200);
	
	retvalue = REDRAW;

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

        myscreen->buffer_to_screen(0,0,320,200);
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

	retvalue = REDRAW;
	
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
        myscreen->buffer_to_screen(0,0,320,200);
	}
	myscreen->clearbuffer();

	return REDRAW;
}

Sint32 create_buy_menu(Sint32 arg1)
{
	Sint32 linesdown, retvalue = 0;
	Uint8* inputkeyboard = query_keyboard();
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
	localbuttons = buttonmenu(buyteam, 17);
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
	sprintf(message, "CASH: %u", money[current_team_num]);
	mytext->write_xy(180, 62, message,(unsigned char) DARK_BLUE, 1);
	current_cost = calculate_cost();
	mytext->write_xy(180, 72, "COST: ", DARK_BLUE, 1);
	sprintf(message, "      %u", current_cost );
	if (current_cost > money[current_team_num])
		mytext->write_xy(180, 72, message, STAT_CHANGED, 1);
	else
		mytext->write_xy(180, 72, message, STAT_COLOR, 1);

	sprintf(message, "TOTAL SCORE: %u", score[current_team_num]);
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
				money[current_team_num] += 1000;
				retvalue = OK;
			}
			if (inputkeyboard[KEYSTATE_KP_MINUS])
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
				localbuttons = buttonmenu(buyteam, 17);
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
			sprintf(message, "CASH: %u", money[current_team_num]);
			mytext->write_xy(180, 62, message,(unsigned char) DARK_BLUE, 1);
			current_cost = calculate_cost();
			mytext->write_xy(180, 72, "COST: ", DARK_BLUE, 1);
			sprintf(message, "      %u", current_cost );
			if (current_cost > money[current_team_num])
				mytext->write_xy(180, 72, message, STAT_CHANGED, 1);
			else
				mytext->write_xy(180, 72, message, STAT_COLOR, 1);

			sprintf(message, "TOTAL SCORE: %u", score[current_team_num]);
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

	if (teamsize < 1)
		return OK;

	myscreen->clearbuffer();

	if (localbuttons)
		delete localbuttons;
	localbuttons = buttonmenu(editteam, 19, 0);  // don't refresh yet
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
	sprintf(message, "    Tallies: %d", current_guy->kills);
	mytext->write_xy(180, 48, message, DARK_BLUE, 1);
	if (current_guy->kills) // are we a veteran?
	{
		sprintf(message, "Avg. Victim: %.2lf ",
		        (float) ((float)current_guy->level_kills / (float)current_guy->kills) );
		mytext->write_xy(180, 55, message, DARK_BLUE, 1);
		sprintf(message, "  Exp/Tally: %u ",
		        (current_guy->exp / current_guy->kills) );
		mytext->write_xy(180, 62, message, DARK_BLUE, 1);
	}
	else
	{
		sprintf(message, "Avg. Victim: N/A ");
		mytext->write_xy(180, 55, message, DARK_BLUE, 1);
		sprintf(message, "  Exp/Tally: N/A ");
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
	sprintf(message, "CASH: %u", money[current_guy->teamnum]);
	mytext->write_xy(180, 76+22, message,(unsigned char) DARK_BLUE, 1);
	current_cost = calculate_cost(here);
	mytext->write_xy(180, 86+22, "COST: ", DARK_BLUE, 1);
	sprintf(message, "      %u", current_cost );
	if (current_cost > money[current_guy->teamnum])
		mytext->write_xy(180, 86+22, message, STAT_CHANGED, 1);
	else
		mytext->write_xy(180, 86+22, message, STAT_COLOR, 1);
	sprintf(message, "TOTAL SCORE: %u", score[current_guy->teamnum]);
	mytext->write_xy(180, 102+22, message,(unsigned char) DARK_BLUE, 1);

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
				localbuttons = buttonmenu(editteam, 19, 0); // don't redraw yet
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
			sprintf(message, "    Tallies: %d", current_guy->kills);
			mytext->write_xy(180, 48, message, DARK_BLUE, 1);
			if (current_guy->kills) // are we a veteran?
			{
				sprintf(message, "Avg. Victim: %.2lf ",
				        (float) ((float)current_guy->level_kills / (float)current_guy->kills) );
				mytext->write_xy(180, 55, message, DARK_BLUE, 1);
				sprintf(message, "  Exp/Tally: %u ",
				        (current_guy->exp / current_guy->kills) );
				mytext->write_xy(180, 62, message, DARK_BLUE, 1);
			}
			else
			{
				sprintf(message, "Avg. Victim: N/A ");
				mytext->write_xy(180, 55, message, DARK_BLUE, 1);
				sprintf(message, "  Exp/Tally: N/A ");
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
			sprintf(message, "CASH: %u", money[current_guy->teamnum]);
			mytext->write_xy(180, 76+22, message,(unsigned char) DARK_BLUE, 1);
			current_cost = calculate_cost(here);
			mytext->write_xy(180, 86+22, "COST: ", DARK_BLUE, 1);
			sprintf(message, "      %u", current_cost );
			if (current_cost > money[current_guy->teamnum])
				mytext->write_xy(180, 86+22, message, STAT_CHANGED, 1);
			else
				mytext->write_xy(180, 86+22, message, STAT_COLOR, 1);
			sprintf(message, "TOTAL SCORE: %u", score[current_guy->teamnum]);
			mytext->write_xy(180, 102+22, message,(unsigned char) DARK_BLUE, 1);

			myscreen->clearfontbuffer(174, 138, 133, 22);

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

bool yes_or_no_prompt(const char* title, const char* message, bool default_value)
{
	text gladtext(myscreen);
	
	int pix_per_char = 3;
    int leftside  = 160 - ( (strlen(message)) * pix_per_char) - 12;
    int rightside = 160 + ( (strlen(message)) * pix_per_char) + 12;
    //buffers: PORT: we will redo this: set_palette(myscreen->redpalette);
    myscreen->clearfontbuffer(leftside, 80, rightside, 40);
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
    Uint8* keyboard = query_keyboard();
	
	// FIXME: Change this to use events instead of key states so the keyboard method works right again.
    int retvalue = 0;
	while (retvalue == 0)
	{
		if(leftmouse())
			retvalue = localbuttons->leftclick();
        else if(keyboard[KEYSTATE_y])
            retvalue = YES;
        else if(keyboard[KEYSTATE_n])
            retvalue = NO;
        else if(keyboard[KEYSTATE_ESCAPE])
            break;
	}
	
    if(retvalue == YES)
        return true;
    if(retvalue == NO)
        return false;
	return default_value;
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

	//myscreen->refresh();
	myscreen->buffer_to_screen(52, 24, 108, 64);

	grab_mouse();
	//myscreen->refresh();
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
Sint32 cycle_team_guy(Sint32 whichway)
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

Sint32 add_guy(guy *newguy)
{
	Sint32 i;

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

Sint32 name_guy(Sint32 arg)  // 0 == current_guy, 1 == ourteam[editguy]
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

Sint32 add_guy(Sint32 ignoreme)
{
	Sint32 newfamily = current_guy->family;
	char tempnum[12];
	//buffers: changed typename to type_name due to some compile error
	char type_name[30];
	static text addtext(myscreen);
	Sint32 i;

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
			sprintf(tempnum, "%d", numbought[newfamily]+1);
			strcat(current_guy->name, tempnum);

			// Return okay status
			return OK;
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

Sint32 how_many(Sint32 whatfamily)    // how many guys of family X on the team?
{
	Sint32 counter = 0;
	Sint32 i;

	for (i=0; i < MAXTEAM; i++)
		if (ourteam[i] && ourteam[i]->family == whatfamily)
			counter++;

	return counter;
}

Sint32 do_save(Sint32 arg1)
{
	char newname[8];
	char newnum[8];
	static text savetext(myscreen);
	Sint32 xloc, yloc, x2loc, y2loc;

	strcpy(newname, "save");

	//buffers: PORT: changed itoa to sprintf, might be BUGGY
	//itoa(arg1, newnum, 10);
	sprintf(newnum,"%d",arg1);

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

Sint32 do_load(Sint32 arg1)
{
	char newname[8];
	char newnum[8];

	// First delete the old team ..
	delete_all();
	//Log("Guys deleted: %d\n", delete_all());
	//commented out debugging done
	//myscreen->soundp->play_sound(SOUND_YO);
	strcpy(newname, "save");

	//buffers: PORT: changed itoa to sprintf
	//itoa(arg1, newnum, 10);
	sprintf(newnum,"%d",arg1);

	strcat(newname, newnum);
	load_team_list_one(newname);
	return REDRAW;
}

Sint32 save_team_list(const char * filename)
{
	char filler[50];// = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *outfile;
	char temp_filename[80];
	char savedgame[40]; // for 38-byte saved game name
	guy *here;
	Sint32 i, j;

	char temptext[10];
	char temp_version = 7;
	unsigned char temp_playermode = playermode;
	Sint32 next_scenario = scen_level;
	Uint32 newcash = money[0];
	Uint32 newscore = score[0];
	//  Sint32 numguys;
	Sint32 listsize;

	char tempname[12] = "FRED";
	char guyname[12] = "JOE";
	char temp_order, temp_family;
	Sint32 temp_str, temp_dex, temp_con;
	Sint32 temp_int, temp_arm, temp_lev;
	Uint32 temp_exp;
	Sint32 temp_kills, temp_level_kills;
	Sint32 temp_td, temp_th, temp_ts;
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
	// 4-bytes (Sint32)= cash (unsigned)
	// 4-bytes (Sint32)= score (unsigned)
	// 4-bytes (Sint32)= cash-B (unsigned)   // All alternate scores
	// 4-bytes (Sint32)= score-B (unsigned)  // version 6+
	// 4-bytes (Sint32)= cash-C (unsigned)
	// 4-bytes (Sint32)= score-C (unsigned)
	// 4-bytes (Sint32)= cash-D (unsigned)
	// 4-bytes (Sint32)= score-D (unsigned)
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
	// 4-bytes Uint32 exp
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

	outfile = open_user_file(temp_filename, "save/", "wb");
	//myscreen->restore_ints();

	//buffers: PORT: dont need this? _enable(); //enable interrupts
	grab_mouse();

	if (!outfile)
		return 0;

	// Write id header
	SDL_RWwrite(outfile, temptext, 3, 1);

	// Write version number
	SDL_RWwrite(outfile, &temp_version, 1, 1);

	// Versions 7+ include a mark for registered or not
	temp_registered = 0;
	temp_registered = 1;
	SDL_RWwrite(outfile, &temp_registered, 2, 1);

	// Write the saved game name
	strcpy(savedgame, save_file);  // save_file is global, 20-char name
	for (i=strlen(savedgame); i < 40; i++)
		savedgame[i] = 0;
	SDL_RWwrite(outfile, savedgame, 40, 1);

	// Write scenario number
	SDL_RWwrite(outfile, &next_scenario, 2, 1);

	// Write cash
	SDL_RWwrite(outfile, &newcash, 4, 1);
	// Write score
	SDL_RWwrite(outfile, &newscore, 4, 1);

	// Versions 6+ have a score for each possible team
	for (i=0; i < 4; i++)
	{
		newcash = money[i];
		SDL_RWwrite(outfile, &newcash, 4, 1);
		newscore = score[i];
		SDL_RWwrite(outfile, &newscore, 4, 1);
	}

	// Versions 7+ include the allied mode information
	temp_allied = myscreen->allied_mode;
	SDL_RWwrite(outfile, &temp_allied, 2, 1);

	// Determine size of team list ...
	listsize = teamsize;

	SDL_RWwrite(outfile, &listsize, 2, 1);

	//write number of players
	SDL_RWwrite(outfile, &temp_playermode,1,1);

	// Write the reserved area, 31 bytes
	SDL_RWwrite(outfile, filler, 31, 1);

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
			SDL_RWwrite(outfile, &temp_order, 1, 1);
			SDL_RWwrite(outfile, &temp_family,1, 1);
			SDL_RWwrite(outfile, guyname, 12, 1);
			SDL_RWwrite(outfile, &temp_str, 2, 1);
			SDL_RWwrite(outfile, &temp_dex, 2, 1);
			SDL_RWwrite(outfile, &temp_con, 2, 1);
			SDL_RWwrite(outfile, &temp_int, 2, 1);
			SDL_RWwrite(outfile, &temp_arm, 2, 1);
			SDL_RWwrite(outfile, &temp_lev, 2, 1);
			SDL_RWwrite(outfile, &temp_exp, 4, 1);
			SDL_RWwrite(outfile, &temp_kills, 2, 1);
			SDL_RWwrite(outfile, &temp_level_kills, 4, 1);
			SDL_RWwrite(outfile, &temp_td, 4, 1); // v.4+
			SDL_RWwrite(outfile, &temp_th, 4, 1); // v.4+
			SDL_RWwrite(outfile, &temp_ts, 4, 1); // v.4+
			SDL_RWwrite(outfile, &temp_teamnum, 2, 1); // v.5+

			// And the filler
			SDL_RWwrite(outfile, filler, 8, 1);

		} // end of found valid guy in slot
	}

	// Write our level status ..
	SDL_RWwrite(outfile, levels, 500, 1);

    SDL_RWclose(outfile);
	return 1;
}

Sint32 load_team_list_one(const char * filename)
{
	char filler[50] = "GTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTLGTL"; // for RESERVED
	SDL_RWops  *infile;
	char temp_filename[80];

	char temptext[10] = "GTL";
	char savedgame[40];
	char temp_version = 7;
	Sint32 next_scenario = 1;
	Uint32 newcash = money[0];
	Uint32 newscore = 0;
	//  Sint32 numguys;
	Sint32 listsize = 0;
	Sint32 i;
	unsigned char temp_playermode;

	char tempname[12] = "FRED";
	char guyname[12] = "JOE";
	char temp_order, temp_family;

	Sint32 temp_str, temp_dex, temp_con;
	Sint32 temp_int, temp_arm, temp_lev;
	Uint32 temp_exp;
	guy  *tempguy;
	Sint32 temp_kills, temp_level_kills;
	Sint32 temp_td, temp_th, temp_ts;
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
	// 4-bytes (Sint32)= cash (unsigned)
	// 4-bytes (Sint32)= score (unsigned)
	// 4-bytes (Sint32)= cash-B (unsigned)   // All alternate scores
	// 4-bytes (Sint32)= score-B (unsigned)  // version 6+
	// 4-bytes (Sint32)= cash-C (unsigned)
	// 4-bytes (Sint32)= score-C (unsigned)
	// 4-bytes (Sint32)= cash-D (unsigned)
	// 4-bytes (Sint32)= score-D (unsigned)
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
	// 4-bytes Uint32 exp
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

	if ( (infile = open_user_file(temp_filename, "save/")) == NULL ) // open for write
	{
		//gotoxy(1, 22);
		//buffers: DEBUG: uncommented following line
		Log("Error in opening team file: %s\n", filename);
		return 0;
	}

	// Read id header
	SDL_RWread(infile, temptext, 3, 1);
	if ( strcmp(temptext,"GTL"))
	{
	    SDL_RWclose(infile);
		//buffers: DEBUG: uncommented following line
		Log("Error, selected file is not a GTL file: %s\n",filename);
		return 0; //not a gtl file
	}

	// Read version number
	SDL_RWread(infile, &temp_version, 1, 1);

	// Versions 7+ have a registered mark ..
	if (temp_version >= 7)
	{
		SDL_RWread(infile, &temp_registered, 2, 1);
	}

	// Do stuff based on the version number
	if (temp_version != 1)
	{
		if (temp_version >= 2)
		{
			SDL_RWread(infile, savedgame, 40, 1);
		}
		else
		{
            SDL_RWclose(infile);
			//buffers: DEBUG: uncommented following line
			Log("Error, selected file is not version one: %s\n",filename);
			return 0;
		}
	}
	else
		strcpy(savedgame, "SAVED GAME"); // fake the game name

	// Read scenario number
	SDL_RWread(infile, &next_scenario, 2, 1);
	scen_level = next_scenario;

	// Read cash
	SDL_RWread(infile, &newcash, 4, 1);
	money[0] = newcash;
	// Read score
	SDL_RWread(infile, &newscore, 4, 1);
	score[0] = newscore;

	// Versions 6+ have a score for each possible team, 0-3
	if (temp_version >= 6)
	{
		for (i=0; i < 4; i++)
		{
			SDL_RWread(infile, &newcash, 4, 1);
			money[i] = newcash;
			SDL_RWread(infile, &newscore, 4, 1);
			score[i] = newscore;
		}
	}

	// Versions 7+ have the allied information ..
	if (temp_version >= 7)
	{
		SDL_RWread(infile, &temp_allied, 2, 1);
		myscreen->allied_mode = temp_allied;
	}

	// Determine size of team list ...
	SDL_RWread(infile, &listsize, 2, 1);

	//read number of players
	SDL_RWread(infile, &temp_playermode, 1, 1);
	playermode = temp_playermode;

	// Read the reserved area, 31 bytes
	SDL_RWread(infile, filler, 31, 1);

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
		SDL_RWread(infile, &temp_order, 1, 1);
		SDL_RWread(infile, &temp_family,1, 1);
		SDL_RWread(infile, guyname, 12, 1);
		SDL_RWread(infile, &temp_str, 2, 1);
		SDL_RWread(infile, &temp_dex, 2, 1);
		SDL_RWread(infile, &temp_con, 2, 1);
		SDL_RWread(infile, &temp_int, 2, 1);
		SDL_RWread(infile, &temp_arm, 2, 1);
		SDL_RWread(infile, &temp_lev, 2, 1);
		SDL_RWread(infile, &temp_exp, 4, 1);
		SDL_RWread(infile, &temp_kills, 2, 1);
		SDL_RWread(infile, &temp_level_kills, 4, 1);
		SDL_RWread(infile, &temp_td, 4, 1); // v.4+
		SDL_RWread(infile, &temp_th, 4, 1); // v.4+
		SDL_RWread(infile, &temp_ts, 4, 1); // v.4+
		SDL_RWread(infile, &temp_teamnum, 2, 1); // v.5+

		// And the filler
		SDL_RWread(infile, filler, 8, 1);
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
		SDL_RWread(infile, levels, 500, 1);
	else
		SDL_RWread(infile, levels, 200, 1);

    SDL_RWclose(infile);

	return 1;
}

const char* get_saved_name(const char * filename)
{
	SDL_RWops  *infile;
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

	if ( (infile = open_user_file(temp_filename, "save/")) == NULL ) // open for read
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
	Sint32 i;
	Sint32 counter = teamsize;

	for (i=0; i < MAXTEAM; i++)
		if (ourteam[i])
		{
			delete ourteam[i];
			ourteam[i] = NULL;
		}

	teamsize = 0;

	return counter;
}

Sint32 add_money(Sint32 howmuch)
{
	money[current_guy->teamnum] += (Sint32) howmuch;
	return money[current_guy->teamnum];
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

	loadgame = open_user_file("save0.gtl", "save/");
	if (loadgame)
	{
	    SDL_RWclose(loadgame);
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
	playermode = howmany;

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

void clear_levels()
{
	Sint32 i;

	// Set all of our level-completion status to off
	for (i=0; i < MAX_LEVELS; i++)
		levels[i] = 0;

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
				           WR(2, "  but your team and fell");
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
			           WL(3, "  Heal all teammates who ");
			           WL(4, "  are close to you, for  ");
			           WL(5, "  as much as you have SP.");
			           //WL(6, "  lets you move in trees.");
			           // Level 4 things
			           if (thisguy->level >= 4)
			           {
				           WL(7, " Raise/Turn Undead");
				           WL(8, "  Raise remains of any  ");
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
				           WL(7, " Devour Remains    ");
				           WL(8, "  Regain health by      ");
				           WL(9, "  devouring the remains ");
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


           /*Sint32 do_set_scen_level(Sint32 arg1)
           {
	           char newname[8];
	           char newnum[8];
	           static text savetext(myscreen);
	           Sint32 xloc, yloc, x2loc, y2loc;
	           Sint32 templevel;
	           Sint32 temptime;


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
	           sprintf(newnum,"%d",scen_level);

	           templevel = atoi(savetext.input_string(xloc+50, yloc+11, 8, newnum) );

	           //buffers: changed itoa to sprintf
	           //itoa(templevel, newnum, 10);
	           sprintf(newnum,"%d",templevel);

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
           }*/
           
           
           
           
           // scenbrowse stuff
           


#include <list>
#include <string>

#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>

using namespace std;







void getLevelStats(screen* screenp, int* max_enemy_level, float* average_enemy_level, int* num_enemies, float* difficulty, list<int>& exits)
{
    int num = 0;
    int level_sum = 0;
    int difficulty_sum = 0;
    int difficulty_sum_friends = 0;
    int diff_per_level = 3;
    
    int max_level = 0;
    exits.clear();
    
    // Go through objects
    oblink* fx = screenp->oblist;
	while(fx)
	{
		if(fx->ob)
		{
		    walker* ob = fx->ob;
		    switch(ob->query_order())
		    {
		        case ORDER_LIVING:
                    if(ob->team_num != 0)
                    {
                        num++;
                        level_sum += ob->stats->level;
                        difficulty_sum += diff_per_level*ob->stats->level;
                        if(ob->stats->level > max_level)
                            max_level = ob->stats->level;
                    }
                    else
                    {
                        difficulty_sum_friends += diff_per_level*ob->stats->level;
                    }
                break;
		    }
		}
		
		fx = fx->next;
	}
	
	// Go through effects
	fx = screenp->fxlist;
	while(fx)
	{
		if(fx->ob)
		{
		    walker* ob = fx->ob;
		    switch(ob->query_order())
		    {
                case ORDER_TREASURE:
                    if(ob->query_family() == FAMILY_EXIT)
                    {
                        exits.push_back(ob->stats->level);
                    }
                break;
		    }
		}
		
		fx = fx->next;
	}
	
	*num_enemies = num;
	*max_enemy_level = max_level;
	if(num == 0)
        *average_enemy_level = 0;
    else
        *average_enemy_level = level_sum/float(num);
    
    *difficulty = difficulty_sum - difficulty_sum_friends;
    
    exits.sort();
    exits.unique();
}


bool isDir(const string& filename)
{
    struct stat status;
    stat(filename.c_str(), &status);

    return (status.st_mode & S_IFDIR);
}

#ifdef ANDROID
list<string> list_scen_files_from_index()
{
    list<string> fileList;
    SDL_RWops* rwops = open_data_file("scen_index.txt", "scen/", "r");
    if(rwops == NULL)
    {
        return fileList;
    }
    
    std::string s;
    char c;
    unsigned int data_bytes = SDL_RWseek(rwops, 0, SEEK_END);
    SDL_RWseek(rwops, 0, SEEK_SET);
    
    for(unsigned int i = 0; i < data_bytes; i++)
    {
        SDL_RWread(rwops, &c, 1, 1);
        bool save = false;
        if(c == '\n')
            save = true;
        else if(c == '\r')
        {
            save = true;
            if(i+1 < data_bytes)
            {
                // Chomp the line feed
                SDL_RWread(rwops, &c, 1, 1);
                if(c != '\n')
                {
                    // Oops, put it back
                    SDL_RWseek(rwops, -1, SEEK_CUR);
                }
                else
                    i++;
            }
        }
        
        if(save)
        {
            if(s.size() > 0)
            {
                fileList.push_back(s);
                s.clear();
            }
            save = false;
        }
        else
            s += c;
    }
    
    if(s.size() > 0)
    {
        fileList.push_back(s);
    }
    
    SDL_FreeRW(rwops);
    return fileList;
}
#endif

list<string> list_files(const string& dirname)
{
    list<string> fileList;
    
    DIR* dir = opendir(dirname.c_str());
    dirent* entry;
    
    if(dir != NULL)
    {
        while ((entry = readdir(dir)) != NULL)
        {
            #ifdef WIN32
            if(!isDir(dirname + "/" + entry->d_name))
            #else
            if(entry->d_type != DT_DIR)
            #endif
                fileList.push_back(entry->d_name);
        }
     
        closedir(dir);
    }
    
    fileList.sort();
    
    return fileList;
}

bool sort_scen(const string& first, const string& second)
{
    string s1;
    string s1num;
    string s2;
    string s2num;
    
    bool gotNum = false;
    for(string::const_iterator e = first.begin(); e != first.end(); e++)
    {
        if(!gotNum && isalpha(*e))
            s1 += *e;
        else
            s1num += *e;
    }
    
    gotNum = false;
    for(string::const_iterator e = second.begin(); e != second.end(); e++)
    {
        if(!gotNum && isalpha(*e))
            s2 += *e;
        else
            s2num += *e;
    }
    
    if(s1 == s2)
        return (atoi(s1num.c_str()) < atoi(s2num.c_str()));
    return (first < second);
}

void load_level_list(char**& level_list, int* level_list_length)
{
    // Do some directory browsing
    const char* dir_path = get_file_path("", "scen/", "r");
    if(dir_path == NULL)
    {
        Log("Level browser: load_level_list() got NULL dir_path");
    }
    #ifndef ANDROID
    list<string> ls = list_files(dir_path != NULL? dir_path : "scen/");
    #else
    // Android needs a better way to list the scen files.  Using scenpack would work, presumably.  This is mostly just a hack.
    list<string> ls = list_scen_files_from_index();
    #endif
    for(list<string>::iterator e = ls.begin(); e != ls.end();)
    {
        if(e->size() > 4 && e->substr(e->size() - 4, 4) == ".fss")
        {
            *e = e->substr(0, e->size() - 4);
            e++;
        }
        else
            e = ls.erase(e);
    }
    
    ls.sort(sort_scen);
    
    *level_list_length = ls.size();
    level_list = new char*[ls.size()];
    
    list<string>::iterator e = ls.begin();
    for(unsigned int i = 0; i < ls.size(); i++)
    {
        level_list[i] = new char[40];
        strncpy(level_list[i], e->c_str(), 40);
        e++;
    }
}


class BrowserEntry
{
    public:
    
    SDL_Rect mapAreas;
    radar* radars;
    int max_enemy_level;
    float average_enemy_level;
    int num_enemies;
    float difficulty;
    oblink* oblist;
    oblink* fxlist;
    oblink* weaplist;
    char* level_name;
    list<int> exits;
    char scentext[80][80];                         // Array to hold scenario information
    char scentextlines;                    // How many lines of text in scenario info
    
    BrowserEntry(screen* screenp, int index, const char* filename);
    ~BrowserEntry();
    
    void draw(screen* screenp, text* loadtext, const char* filename);
};
void remove_all_objects(screen *master)
{
	oblink *fx = master->fxlist;

	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = master->oblist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	fx = master->weaplist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		else
			fx = fx->next;
	}
	if (fx && fx->ob)
		delete fx->ob;

	master->numobs = 0;
} // end remove_all_objects

BrowserEntry::BrowserEntry(screen* screenp, int index, const char* filename)
{
    // Clear the level so we can load the next one
    remove_all_objects(screenp);  // kill current obs
    for (int j=0; j < 60; j++)
        screenp->scentext[j][0] = 0;
        
    // bool loaded = load_scenario(filename, screenp);
    load_scenario(filename, screenp);
    
    radar* r = new radar(NULL, screenp, 0);
    r->start();
    radars = r;
    

    int w = radars->xview;
    int h = radars->yview;
    
    mapAreas.w = w;
    mapAreas.h = h;
    mapAreas.x = 10;
    mapAreas.y = 5 + (53 + 12)*index;
    
    r->xloc = mapAreas.x + mapAreas.w/2 - w/2;
    r->yloc = mapAreas.y + 10;
    
    
    getLevelStats(screenp, &max_enemy_level, &average_enemy_level, &num_enemies, &difficulty, exits);
    
    // Store this level's objects
    oblist = screenp->oblist;
    screenp->oblist = NULL;
    fxlist = screenp->fxlist;
    screenp->fxlist = NULL;
    weaplist = screenp->weaplist;
    screenp->weaplist = NULL;
    level_name = new char[24];
    strncpy(level_name, screenp->scenario_title, 23);
    if(level_name[20] != '\0')
    {
        level_name[20] = '.';
        level_name[21] = '.';
        level_name[22] = '.';
        level_name[23] = '\0';
    }
    
    scentextlines = screenp->scentextlines;
    for(int i = 0; i < scentextlines; i++)
    {
        strncpy(scentext[i], screenp->scentext[i], 80);
    }
}


BrowserEntry::~BrowserEntry()
{
    // Delete all objects
    oblink *fx = fxlist;

	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		fx = fx->next;
	}

	fx = oblist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		fx = fx->next;
	}

	fx = weaplist;
	while (fx)
	{
		if (fx->ob)
		{
			delete fx->ob;
			fx->ob = NULL;
		}
		fx = fx->next;
	}
    
    delete radars;
    delete[] level_name;
}

void BrowserEntry::draw(screen* screenp, text* loadtext, const char* filename)
{
    // Set the current objects
    screenp->oblist = oblist;
    screenp->fxlist = fxlist;
    screenp->weaplist = weaplist;
    
    int x = radars->xloc;
    int y = radars->yloc;
    int w = radars->xview;
    int h = radars->yview;
    screenp->draw_button(x - 2, y - 2, x + w + 2, y + h + 2, 1, 1);
    // Draw radar
    radars->draw();
    loadtext->write_xy(mapAreas.x, mapAreas.y, level_name, DARK_BLUE, 1);
    
    char buf[30];
    snprintf(buf, 30, "%s", filename);
    loadtext->write_xy(x + w + 5, y, buf, WHITE, 1);
    snprintf(buf, 30, "Enemies: %d", num_enemies);
    loadtext->write_xy(x + w + 5, y + 8, buf, WHITE, 1);
    snprintf(buf, 30, "Max level: %d", max_enemy_level);
    loadtext->write_xy(x + w + 5, y + 16, buf, WHITE, 1);
    snprintf(buf, 30, "Avg level: %.1f", average_enemy_level);
    loadtext->write_xy(x + w + 5, y + 24, buf, WHITE, 1);
    snprintf(buf, 30, "Difficulty: %.0f", difficulty);
    loadtext->write_xy(x + w + 5, y + 32, buf, RED, 1);
    
    if(exits.size() > 0)
    {
        snprintf(buf, 30, "Exits: ");
        bool first = true;
        for(list<int>::iterator e = exits.begin(); e != exits.end(); e++)
        {
            char buf2[10];
            snprintf(buf2, 10, (first? "%d" : ", %d"), *e);
            strncat(buf, buf2, 30);
            first = false;
        }
        if(strlen(buf) > 19)
        {
            buf[17] = '.';
            buf[18] = '.';
            buf[19] = '.';
            buf[20] = '\0';
        }
        loadtext->write_xy(x + w + 5, y + 40, buf, WHITE, 1);
    }
}


#define NUM_BROWSE_RADARS 3

// Load a grid or scenario ..
char* browse(screen *screenp)
{
    char* result = NULL;
    
    Uint8* mykeyboard = query_keyboard();
    
    // Clear all objects from the current level
    remove_all_objects(screenp);  // kill current obs
    for (int j=0; j < 60; j++)
        screenp->scentext[j][0] = 0;
    
	text* loadtext = new text(screenp);
    
    // Here are the browser variables
    BrowserEntry* entries[NUM_BROWSE_RADARS];
    
    int level_list_length = 0;
    char** level_list = NULL;
    load_level_list(level_list, &level_list_length);
    
    int current_level_index = 1;
    
    // Load the radars (minimaps)
    for(int i = 0; i < NUM_BROWSE_RADARS; i++)
    {
        if(i < level_list_length)
            entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
        else
            entries[i] = NULL;
    }
    
    int selected_entry = -1;
    
    // Figure out how good the player's army is
    int army_power = 0;
	for(int i=0; i<MAXTEAM; i++)
	{
		if (ourteam[i])
		{
		    army_power += 3*ourteam[i]->level;
		}
	}
    
    // Buttons
    Sint16 screenW = 320;
    Sint16 screenH = 200;
    SDL_Rect prev = {Sint16(screenW - 150), 20, 30, 10};
    SDL_Rect next = {Sint16(screenW - 150), Sint16(screenH - 50), 30, 10};
    SDL_Rect descbox = {Sint16(prev.x - 40), Sint16(prev.y + 15), 185, Uint16(next.y - 10 - (prev.y + prev.h))};
    
    SDL_Rect choose = {Sint16(screenW - 50), Sint16(screenH - 30), 30, 10};
    SDL_Rect cancel = {Sint16(screenW - 100), Sint16(screenH - 30), 38, 10};
    
    bool done = false;
	while (!done)
	{
		// Reset the timer count to zero ...
		reset_timer();

		if (screenp->end)
			break;

		// Get keys and stuff
		get_input_events(POLL);
		
		// Quit if 'q' is pressed
		if(mykeyboard[KEYSTATE_q])
            done = true;
            
		if(mykeyboard[KEYSTATE_UP])
		{
		    // Scroll up
		    if(current_level_index > 0)
		    {
                selected_entry = -1;
		    
                current_level_index--;
                
                for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                {
                    if(i < level_list_length)
                    {
                        delete entries[i];
                        entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                    }
                }
		    }
            while (mykeyboard[KEYSTATE_UP])
                get_input_events(WAIT);
		}
		if(mykeyboard[KEYSTATE_DOWN])
		{
		    // Scroll down
		    if(current_level_index < level_list_length - NUM_BROWSE_RADARS)
		    {
                selected_entry = -1;
		    
                current_level_index++;
                
                for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                {
                    if(i < level_list_length)
                    {
                        delete entries[i];
                        entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                    }
                }
		    }
            while (mykeyboard[KEYSTATE_DOWN])
                get_input_events(WAIT);
		}
		
		// Mouse stuff ..
		mymouse = query_mouse();
		if (mymouse[MOUSE_LEFT])       // put or remove the current guy
		{
		    while(mymouse[MOUSE_LEFT])
                get_input_events(WAIT);
		    
			int mx = mymouse[MOUSE_X];
			int my = mymouse[MOUSE_Y];
			
		    
            
            // Prev
            if(prev.x <= mx && mx <= prev.x + prev.w
               && prev.y <= my && my <= prev.y + prev.h)
               {
                    if(current_level_index > 0)
                    {
                        selected_entry = -1;
                        current_level_index--;
                        
                        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                        {
                            if(i < level_list_length)
                            {
                                delete entries[i];
                                entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                            }
                        }
                    }
               }
            // Next
            else if(next.x <= mx && mx <= next.x + next.w
               && next.y <= my && my <= next.y + next.h)
               {
                    if(current_level_index < level_list_length - NUM_BROWSE_RADARS)
                    {
                        selected_entry = -1;
                        current_level_index++;
                        
                        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                        {
                            if(i < level_list_length)
                            {
                                delete entries[i];
                                entries[i] = new BrowserEntry(screenp, i, level_list[current_level_index + i]);
                            }
                        }
                    }
               }
            // Choose
			else if(choose.x <= mx && mx <= choose.x + choose.w
               && choose.y <= my && my <= choose.y + choose.h)
               {
                   if(selected_entry != -1)
                   {
                       result = new char[strlen(level_list[current_level_index + selected_entry])+1];
                       strcpy(result, level_list[current_level_index + selected_entry]);
                       done = true;
                       break;
                   }
               }
            // Cancel
			else if(cancel.x <= mx && mx <= cancel.x + cancel.w
               && cancel.y <= my && my <= cancel.y + cancel.h)
               {
                   done = true;
                   break;
               }
			else
			{
                selected_entry = -1;
                // Select
                for(int i = 0; i < NUM_BROWSE_RADARS; i++)
                {
                    if(i < level_list_length && entries[i] != NULL)
                    {
                        int x = entries[i]->radars->xloc;
                        int y = entries[i]->radars->yloc;
                        int w = entries[i]->radars->xview;
                        int h = entries[i]->radars->yview;
                        SDL_Rect b = {Sint16(x - 2), Sint16(y - 2), Uint16(w + 2), Uint16(h + 2)};
                        if(b.x <= mx && mx <= b.x+b.w
                           && b.y <= my && my <= b.y+b.h)
                           {
                               selected_entry = i;
                               break;
                           }
                    }
                }
			}
		}
		
        
        // Draw
        screenp->clearscreen();
        
        char buf[20];
        snprintf(buf, 20, "Army power: %d", army_power);
        loadtext->write_xy(prev.x + 50, prev.y + 2, buf, RED, 1);
        
        screenp->draw_button(prev.x, prev.y, prev.x + prev.w, prev.y + prev.h, 1, 1);
        loadtext->write_xy(prev.x + 2, prev.y + 2, "Prev", DARK_BLUE, 1);
        screenp->draw_button(next.x, next.y, next.x + next.w, next.y + next.h, 1, 1);
        loadtext->write_xy(next.x + 2, next.y + 2, "Next", DARK_BLUE, 1);
        if(selected_entry != -1 && selected_entry < level_list_length && entries[selected_entry] != NULL)
        {
            screenp->draw_button(choose.x, choose.y, choose.x + choose.w, choose.y + choose.h, 1, 1);
            loadtext->write_xy(choose.x + 9, choose.y + 2, "OK", DARK_GREEN, 1);
            loadtext->write_xy(next.x, choose.y + 20, entries[selected_entry]->level_name, DARK_GREEN, 1);
        }
        screenp->draw_button(cancel.x, cancel.y, cancel.x + cancel.w, cancel.y + cancel.h, 1, 1);
        loadtext->write_xy(cancel.x + 2, cancel.y + 2, "Cancel", RED, 1);
        
        if(selected_entry != -1)
        {
            int i = selected_entry;
            if(i < level_list_length && entries[i] != NULL)
            {
                int x = entries[i]->radars->xloc - 4;
                int y = entries[i]->radars->yloc - 4;
                int w = entries[i]->radars->xview + 8;
                int h = entries[i]->radars->yview + 8;
                screenp->draw_box(x, y, x + w, y + h, DARK_BLUE, 1, 1);
            }
        }
        for(int i = 0; i < NUM_BROWSE_RADARS; i++)
        {
            if(i < level_list_length && entries[i] != NULL)
                entries[i]->draw(screenp, loadtext, level_list[current_level_index + i]);
        }
        
        // Description
        if(selected_entry != -1 && selected_entry < level_list_length && entries[selected_entry] != NULL)
        {
            screenp->draw_box(descbox.x, descbox.y, descbox.x + descbox.w, descbox.y + descbox.h, GREY, 1, 1);
            for(int i = 0; i < entries[selected_entry]->scentextlines; i++)
            {
                if(prev.y + 20 + 10*i+1 > descbox.y + descbox.h)
                    break;
                loadtext->write_xy(descbox.x, descbox.y + 10*i+1, entries[selected_entry]->scentext[i], BLACK, 1);
            }
        }
        
        
		screenp->buffer_to_screen(0, 0, 320, 200);
		SDL_Delay(10);
	}
	
    while (mykeyboard[KEYSTATE_q])
        get_input_events(WAIT);
	
    for(int i = 0; i < NUM_BROWSE_RADARS; i++)
    {
        delete entries[i];
    }
    
    delete loadtext;
    
    for(int i = 0; i < level_list_length; i++)
    {
        delete[] level_list[i];
    }
    delete[] level_list;
    
    
    // Clear all objects from the current level
    remove_all_objects(screenp);  // kill current obs
    for (int j=0; j < 60; j++)
        screenp->scentext[j][0] = 0;
    
	return result;
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
           
           
           // scenbrowse now!
           Sint32 do_set_scen_level(Sint32 arg1)
           {
	           static text savetext(myscreen);
	           Sint32 xloc, yloc, x2loc, y2loc;
	           Sint32 templevel = scen_level;
	           Sint32 temptime;

	           xloc = 100;
	           yloc = 170;
	           x2loc = 220;
	           y2loc = 190;

                myscreen->clearfontbuffer(xloc,yloc,x2loc,y2loc);
               
	           char* newname = browse(myscreen);
	           if(newname)
	           {
	               templevel = get_scen_num_from_filename(newname);
                   if (templevel < 0 || !load_scenario(newname, myscreen))
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
                       scen_level = templevel;
                   }
                   delete[] newname;
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
	           strcpy(allbuttons[2]->label, message);

	           //allbuttons[2]->vdisplay();
	           //myscreen->buffer_to_screen(0, 0, 320, 200);

	           return OK;
           }

           Sint32 change_teamnum(Sint32 arg)
           {
	           // Change the team number of the current guy
	           short current_team;

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
	           //myscreen->buffer_to_screen(0, 0, 320, 200);

	           return OK;
           }

           Sint32 change_hire_teamnum(Sint32 arg)
           {
	           // Change the team number of the hiring menu ..

	           current_team_num += arg;
	           current_team_num %= 4;

	           // Change our guy, if he exists ..
	           if (current_guy)
	           {
		           current_guy->teamnum = current_team_num;
	           }

		myscreen->clearfontbuffer(170, 130, 133, 22);

	           myscreen->buffer_to_screen(0, 0, 320, 200);

	           return OK;
           }

           Sint32 change_allied()
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
