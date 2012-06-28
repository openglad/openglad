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

#define MEM_TIME (int) 2000
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
extern int *mymouse;
extern unsigned int money[4];

typedef struct
{
	char label[30];
	unsigned char hotkey;
	int x, y;
	int sizex, sizey;
	//int (*fun)(int arg1);
	int myfun;
	int arg1;     // argurment to function fun
}
button;

class vbutton
{
	public:
		vbutton();//this should only be used for pointers!!
		vbutton(int xpos, int ypos, int wide, int high, int func(int),
		        int pass, char *msg, unsigned char hot );
		vbutton(int xpos, int ypos, int wide, int high, int func_code,
		        int pass, char *msg, unsigned char hot );
		vbutton(int xpos, int ypos, int wide, int high, int func_code,
		        int pass, char *msg, char family, unsigned char hot );
		~vbutton();
		void set_graphic(char family);
		int leftclick(); //is called when the button is left clicked
		int leftclick(int whichone);
		int rightclick(); //is called when the button is right clicked
		int rightclick(int whichone);
		int mouse_on(); //determins if mouse is on this button, returns 1 if true
		void vdisplay();
		void vdisplay(int status); // display depressed
		int do_call(int whatfunc, int arg);
		int do_call_right(int whatfunc, int arg);  // for right-button

		int xloc; //the xposition in screen-coords
		int yloc; //the yposition in screen-coords
		char label[80]; //the label on the button
		int width; // the buttons width in pixels
		int height; // the buttons height in pixels
		int xend; //xloc+width
		int yend; //yloc+height
		int (*fun)(int arg1); //the function this button calls when clicked, with one arg
		int myfunc;
		int arg; //the arg to be passed to the function when called
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
int add_money(int howmuch);

int ventermenu(vbutton *vbuttons);
int vexitmenu(vbutton *vbuttons);

vbutton * buttonmenu(button * buttons, int numbuttons);
vbutton * buttonmenu(button * buttons, int numbuttons, int redraw);

// These are for picker ..
int score_panel(screen *myscreen);
int mainmenu(int arg1);
int beginmenu(int arg1);
int loadmenu(int arg1);
int newmenu(int arg1);
void quit(int arg1);
int nullmenu(int arg1);
int load1(int arg1);  // Begin a preset scenario ..
int load2(int arg1);
int load3(int arg1);
int create_team_menu(int arg1); // Create / modify team members
int create_detail_menu(guy *arg1); // detailed character information
int create_view_menu(int arg1); // View team members
int create_buy_menu(int arg1);  // Purchase new team members
int create_edit_menu(int arg1); // Edit or sell team members
int create_load_menu(int arg1); // Load a team
int create_save_menu(int arg1); // Save a team
int go_menu(int arg1); // run glad..
int increase_stat(int arg1, int howmuch=1); // increase a guy's stats
int decrease_stat(int arg1, int howmuch=1); // decrease a guy's stats
unsigned int calculate_cost();
unsigned int calculate_cost(guy * oldguy);
int cycle_guy(int whichway);
int cycle_team_guy(int whichway);
int add_guy(int ignoreme);
int edit_guy(int arg1); // transfer stats .. hardcoded
int do_save(int arg1);  // dummy function for save_team_list
int save_team_list(const char * filename); // save the team list
int do_load(int arg1); // dummy function for load_team_list_one
int load_team_list_one(const char * filename); // load a team list
int delete_all(); // delete entire team
int delete_first(); // delete first guy on team list
int how_many(int whatfamily);   // how many guys of family X on the team?
void statscopy(guy *dest, guy *source); //copy stats from source => dest
int set_player_mode(int howmany);
int calculate_level(unsigned int temp_exp);
unsigned int calculate_exp(int level);
void clear_levels();
int return_menu(int arg);
int name_guy(int arg); // name the current guy
int do_set_scen_level(int arg1);
int set_difficulty();
int change_teamnum(int arg);
int change_hire_teamnum(int arg);
int change_allied();

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
