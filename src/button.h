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
 * aSint32 with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __BUTTON_H
#define __BUTTON_H

#include "base.h"
#include "obmap.h"
#include "loader.h"
#include "pixien.h"
#include "text.h"
#include "screen.h"
#include <math.h>
//buffers: we are putting the int32 stuff into a new file (input.cpp/h)
//#include "int32.h"
#include "input.h"

#define MEM_TIME (Sint32) 2000
class vbutton;

// Definition of a button

#define BUT_STR 0
#define BUT_DEX 1
#define BUT_CON 2
#define BUT_INT 3
#define BUT_ARMOR 4
#define BUT_LEVEL 5

// Button edge-colors
#define BUTTON_FACING (char) 13 //12
#define BUTTON_TOP    (char) 15 //14
#define BUTTON_BOTTOM (char) 11 //10
#define BUTTON_LEFT   (char) 14 //13
#define BUTTON_RIGHT  (char) 12 //11

//extern screen *myscreen;
extern screen *myscreen;
extern text *mytext;
extern Sint32 *mymouse;
extern Uint32 money[4];

typedef struct
{
	char label[30];
	unsigned char hotkey;
	Sint32 x, y;
	Sint32 sizex, sizey;
	//Sint32 (*fun)(Sint32 arg1);
	Sint32 myfun;
	Sint32 arg1;     // argurment to function fun
}
button;

class vbutton
{
	public:
		vbutton();//this should only be used for pointers!!
		vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high, Sint32 func(Sint32),
		        Sint32 pass, char *msg, unsigned char hot );
		vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high, Sint32 func_code,
		        Sint32 pass, char *msg, unsigned char hot );
		vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high, Sint32 func_code,
		        Sint32 pass, char *msg, char family, unsigned char hot );
		~vbutton();
		void set_graphic(char family);
		Sint32 leftclick(); //is called when the button is left clicked
		Sint32 leftclick(Sint32 whichone);
		Sint32 rightclick(); //is called when the button is right clicked
		Sint32 rightclick(Sint32 whichone);
		Sint32 mouse_on(); //determins if mouse is on this button, returns 1 if true
		void vdisplay();
		void vdisplay(Sint32 status); // display depressed
		Sint32 do_call(Sint32 whatfunc, Sint32 arg);
		Sint32 do_call_right(Sint32 whatfunc, Sint32 arg);  // for right-button

		Sint32 xloc; //the xposition in screen-coords
		Sint32 yloc; //the yposition in screen-coords
		char label[80]; //the label on the button
		Sint32 width; // the buttons width in pixels
		Sint32 height; // the buttons height in pixels
		Sint32 xend; //xloc+width
		Sint32 yend; //yloc+height
		Sint32 (*fun)(Sint32 arg1); //the function this button calls when clicked, with one arg
		Sint32 myfunc;
		Sint32 arg; //the arg to be passed to the function when called
		vbutton * next; //a pointer to the next button
		vbutton * prev; //a pointer to the previous button
		char had_focus; // did we recently have focus?
		char do_outline; // force an outline
		char depressed;
		pixieN *mypixie;
		unsigned char hotkey;
};

#define MAX_BUTTONS 50  // max buttons per screen
extern vbutton *allbuttons[MAX_BUTTONS];

short has_mouse_focus(button thisbutton);
void clearmenu(button *buttons, short numbuttons);
Sint32 add_money(Sint32 howmuch);

Sint32 ventermenu(vbutton *vbuttons);
Sint32 vexitmenu(vbutton *vbuttons);

vbutton * buttonmenu(button * buttons, Sint32 numbuttons);
vbutton * buttonmenu(button * buttons, Sint32 numbuttons, Sint32 redraw);

// These are for picker ..
Sint32 score_panel(screen *myscreen);
Sint32 mainmenu(Sint32 arg1);
Sint32 beginmenu(Sint32 arg1);
Sint32 loadmenu(Sint32 arg1);
Sint32 newmenu(Sint32 arg1);
void quit(Sint32 arg1);
Sint32 nullmenu(Sint32 arg1);
Sint32 load1(Sint32 arg1);  // Begin a preset scenario ..
Sint32 load2(Sint32 arg1);
Sint32 load3(Sint32 arg1);
Sint32 create_team_menu(Sint32 arg1); // Create / modify team members
Sint32 create_detail_menu(guy *arg1); // detailed character information
Sint32 create_view_menu(Sint32 arg1); // View team members
Sint32 create_buy_menu(Sint32 arg1);  // Purchase new team members
Sint32 create_edit_menu(Sint32 arg1); // Edit or sell team members
Sint32 create_load_menu(Sint32 arg1); // Load a team
Sint32 create_save_menu(Sint32 arg1); // Save a team
Sint32 go_menu(Sint32 arg1); // run glad..
Sint32 increase_stat(Sint32 arg1, Sint32 howmuch=1); // increase a guy's stats
Sint32 decrease_stat(Sint32 arg1, Sint32 howmuch=1); // decrease a guy's stats
Uint32 calculate_cost();
Uint32 calculate_cost(guy * oldguy);
Sint32 cycle_guy(Sint32 whichway);
Sint32 cycle_team_guy(Sint32 whichway);
Sint32 add_guy(Sint32 ignoreme);
Sint32 edit_guy(Sint32 arg1); // transfer stats .. hardcoded
Sint32 do_save(Sint32 arg1);  // dummy function for save_team_list
Sint32 save_team_list(const char * filename); // save the team list
Sint32 do_load(Sint32 arg1); // dummy function for load_team_list_one
Sint32 load_team_list_one(const char * filename); // load a team list
Sint32 delete_all(); // delete entire team
Sint32 delete_first(); // delete first guy on team list
Sint32 how_many(Sint32 whatfamily);   // how many guys of family X on the team?
void statscopy(guy *dest, guy *source); //copy stats from source => dest
Sint32 set_player_mode(Sint32 howmany);
Sint32 calculate_level(Uint32 temp_exp);
Uint32 calculate_exp(Sint32 level);
void clear_levels();
Sint32 return_menu(Sint32 arg);
Sint32 name_guy(Sint32 arg); // name the current guy
Sint32 do_set_scen_level(Sint32 arg1);
Sint32 set_difficulty();
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
Sint32 change_allied();

// Function definitions ..
#define BEGINMENU               1
#define CREATE_TEAM_MENU        2
#define SET_PLAYER_MODE         3
#define QUIT_MENU               4
#define CREATE_VIEW_MENU        5
#define CREATE_EDIT_MENU        6
#define CREATE_BUY_MENU         7
#define CREATE_LOAD_MENU        8
#define CREATE_SAVE_MENU        9
#define GO_MENU                 10
#define RETURN_MENU             11
#define CYCLE_TEAM_GUY          12
#define DECREASE_STAT           13
#define INCREASE_STAT           14
#define EDIT_GUY                15
#define CYCLE_GUY               16
#define ADD_GUY                 17
#define DO_SAVE                 18
#define DO_LOAD                 19
#define NAME_GUY                20
#define CREATE_DETAIL_MENU      21
#define NULLMENU                22
#define DO_SET_SCEN_LEVEL       23
#define SET_DIFFICULTY          24
#define CHANGE_TEAM             25
#define ALLIED_MODE             26
#define CHANGE_HIRE_TEAM        27
#endif
