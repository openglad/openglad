#ifndef __BUTTON_H
#define __BUTTON_H

#include "base.h"
#include "obmap.h"
#include "loader.h"
#include "pixien.h"
#include "text.h"
#include "screen.h"
#include <math.h>
#include "int32.h"

#define MEM_TIME (long) 2000
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
extern long *mymouse;
extern unsigned long money[4];

typedef struct
{
  char label[30];
  unsigned char hotkey;
  long x, y;
  long sizex, sizey;
//long (*fun)(long arg1);
  long myfun;
  long arg1;     // argurment to function fun
} button;

class vbutton
{
  public:
         vbutton();//this should only be used for pointers!!
         vbutton(long xpos, long ypos, long wide, long high, long func(long),
                 long pass, char *msg, unsigned char hot );
         vbutton(long xpos, long ypos, long wide, long high, long func_code,
                 long pass, char *msg, unsigned char hot );
         vbutton(long xpos, long ypos, long wide, long high, long func_code,
                 long pass, char *msg, char family, unsigned char hot );
         ~vbutton();
         void set_graphic(char family);
         long leftclick(); //is called when the button is left clicked
         long leftclick(long whichone);
         long rightclick(); //is called when the button is right clicked
         long rightclick(long whichone);
         long mouse_on(); //determins if mouse is on this button, returns 1 if true
         void vdisplay();
         void vdisplay(long status); // display depressed
         long do_call(long whatfunc, long arg);
         long do_call_right(long whatfunc, long arg);  // for right-button
         
         long xloc; //the xposition in screen-coords
         long yloc; //the yposition in screen-coords
         char label[80]; //the label on the button
         long width; // the buttons width in pixels
         long height; // the buttons height in pixels
         long xend; //xloc+width
         long yend; //yloc+height
         long (*fun)(long arg1); //the function this button calls when clicked, with one arg
         long myfunc;
         long arg; //the arg to be passed to the function when called
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
long add_money(long howmuch);

long ventermenu(vbutton *vbuttons);
long vexitmenu(vbutton *vbuttons);

vbutton * buttonmenu(button * buttons, long numbuttons);
vbutton * buttonmenu(button * buttons, long numbuttons, long redraw);

// These are for picker ..
long score_panel(screen *myscreen);
long mainmenu(long arg1);
long beginmenu(long arg1);
long loadmenu(long arg1);
long newmenu(long arg1);
long quit(long arg1);
long nullmenu(long arg1);
long load1(long arg1);  // Begin a preset scenario ..
long load2(long arg1);
long load3(long arg1);
long create_team_menu(long arg1); // Create / modify team members
long create_detail_menu(guy *arg1); // detailed character information
long create_view_menu(long arg1); // View team members
long create_buy_menu(long arg1);  // Purchase new team members
long create_edit_menu(long arg1); // Edit or sell team members
long create_load_menu(long arg1); // Load a team
long create_save_menu(long arg1); // Save a team
long go_menu(long arg1); // run glad..
long increase_stat(long arg1, long howmuch=1); // increase a guy's stats
long decrease_stat(long arg1, long howmuch=1); // decrease a guy's stats
long calculate_cost();
long calculate_cost(guy * oldguy);
long cycle_guy(long whichway);
long cycle_team_guy(long whichway);
long add_guy(long ignoreme);
long edit_guy(long arg1); // transfer stats .. hardcoded
long do_save(long arg1);  // dummy function for save_team_list
long save_team_list(char * filename); // save the team list
long do_load(long arg1); // dummy function for load_team_list_one
long load_team_list_one(char * filename); // load a team list
long delete_all(); // delete entire team
long delete_first(); // delete first guy on team list
long how_many(long whatfamily);   // how many guys of family X on the team?
void statscopy(guy *dest, guy *source); //copy stats from source => dest
long set_player_mode(long howmany);
long calculate_level(unsigned long temp_exp);
unsigned long calculate_exp(long level);
void clear_levels();
long return_menu(long arg);
long name_guy(long arg); // name the current guy
long do_set_scen_level(long arg1);
long set_difficulty();
long change_teamnum(long arg);
long change_hire_teamnum(long arg);
long change_allied();

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
