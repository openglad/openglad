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
//
// input.h
//
// input code
//

#include <SDL.h>
#include <ctype.h>
#include <string>


// Zardus: defines for event getting method
#define POLL 0
#define WAIT 1

//Keyboard defines
#define MAXKEYS 320

//Mouse Defines
#define MOUSE_RESET 0
#define MOUSE_STATE 3
#define MSTATE 4
#define MOUSE_X 0
#define MOUSE_Y 1
#define MOUSE_LEFT 2
#define MOUSE_RIGHT 3

//buffers: added prototype
void get_input_events(bool type);
void handle_events(SDL_Event *event);

void grab_keyboard();                                               // mask the keyboard short.
void release_keyboard();                                    // restore normal short.
int query_key();                                                            // return last keypress
void clear_keyboard();                                              // set keyboard to none pressed
char * query_keyboard();                                    // keyboard status
void wait_for_key(int somekey); // wait for key SOMEKEY
short query_key_press_event();                       //query_ & clear_key_press_event
void clear_key_press_event();                       // detect a key press :)
short query_key_code(int code);                       // OBSOLETE, use query_keyboard
void clear_key_code(int code);
void enable_keyrepeat();
void disable_keyrepeat();
void init_input();
void stop_input();

void grab_mouse();
void release_mouse();
long * query_mouse();
