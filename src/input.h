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

#ifndef _INPUT_H__
#define _INPUT_H__

#include <SDL.h>
#include <ctype.h>
#include <string>
#include "video.h"


// SDL 2 compat
#if SDL_VERSION_ATLEAST(2,0,0)
    #define USE_SDL2
#endif

#ifdef USE_SDL2
    #define SDLKey SDL_Keycode
    
    #define KEYSTATE_q SDL_SCANCODE_Q
    #define KEYSTATE_y SDL_SCANCODE_Y
    #define KEYSTATE_n SDL_SCANCODE_N
    #define KEYSTATE_z SDL_SCANCODE_Z
    #define KEYSTATE_DOWN SDL_SCANCODE_DOWN
    #define KEYSTATE_UP SDL_SCANCODE_UP
    #define KEYSTATE_PAGEDOWN SDL_SCANCODE_PAGEDOWN
    #define KEYSTATE_PAGEUP SDL_SCANCODE_PAGEUP
    #define KEYSTATE_ESCAPE SDL_SCANCODE_ESCAPE
    #define KEYSTATE_SPACE SDL_SCANCODE_SPACE
    #define KEYSTATE_LCTRL SDL_SCANCODE_LCTRL
    #define KEYSTATE_KP_PLUS SDL_SCANCODE_KP_PLUS
    #define KEYSTATE_KP_MINUS SDL_SCANCODE_KP_MINUS
#else
    #define KEYSTATE_q SDLK_q
    #define KEYSTATE_y SDLK_y
    #define KEYSTATE_n SDLK_n
    #define KEYSTATE_z SDLK_z
    #define KEYSTATE_DOWN SDLK_DOWN
    #define KEYSTATE_UP SDLK_UP
    #define KEYSTATE_PAGEDOWN SDLK_PAGEDOWN
    #define KEYSTATE_PAGEUP SDLK_PAGEUP
    #define KEYSTATE_ESCAPE SDLK_ESCAPE
    #define KEYSTATE_SPACE SDLK_SPACE
    #define KEYSTATE_LCTRL SDLK_LCTRL
    #define KEYSTATE_KP_PLUS SDLK_KP_PLUS
    #define KEYSTATE_KP_MINUS SDLK_KP_MINUS
#endif


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

// These are keyboard defines .. high-level
#define KEY_UP                  0
#define KEY_UP_RIGHT            1
#define KEY_RIGHT               2
#define KEY_DOWN_RIGHT          3
#define KEY_DOWN                4
#define KEY_DOWN_LEFT           5
#define KEY_LEFT                6
#define KEY_UP_LEFT             7
#define KEY_FIRE                8
#define KEY_SPECIAL             9
#define KEY_SWITCH              10
#define KEY_SPECIAL_SWITCH      11
#define KEY_YELL                12
#define KEY_SHIFTER             13
#define KEY_PREFS               14
#define KEY_CHEAT               15
#define NUM_KEYS               16


class JoyData
{
    public:
    int index;
    int numAxes;
    int numButtons;
    int numHats;
    
    static const int NONE = 0;
    static const int BUTTON = 1;
    static const int POS_AXIS = 2;
    static const int NEG_AXIS = 3;
    static const int HAT_UP = 4;
    static const int HAT_UP_RIGHT = 5;
    static const int HAT_RIGHT = 6;
    static const int HAT_DOWN_RIGHT = 7;
    static const int HAT_DOWN = 8;
    static const int HAT_DOWN_LEFT = 9;
    static const int HAT_LEFT = 10;
    static const int HAT_UP_LEFT = 11;
    
    int key_type[NUM_KEYS];
    int key_index[NUM_KEYS];
    
    JoyData();
    JoyData(int index);
    
    void setKeyFromEvent(int key_enum, const SDL_Event& event);
    
    bool getState(int key_enum) const;
    bool getPress(int key_enum, const SDL_Event& event) const;
    bool hasButtonSet(int key_enum) const;
};


bool playerHasJoystick(int player_num);
void disablePlayerJoystick(int player_num);
void resetJoystick(int player_num);
bool isPlayerHoldingKey(int player_index, int key_enum);
bool didPlayerPressKey(int player_index, int key_enum, const SDL_Event& event);

//buffers: added prototype
void get_input_events(bool type);
void handle_events(SDL_Event *event);

void grab_keyboard();                                               // mask the keyboard short.
void release_keyboard();                                    // restore normal short.
int query_key();                                                            // return last keypress
char* query_text_input();                                                            // return last text input

bool query_input_continue();
short get_and_reset_scroll_amount();

#ifdef ANDROID
void draw_touch_controls(video* vob);
#define CONTINUE_ACTION_STRING "TAP"
#else
#define CONTINUE_ACTION_STRING "PRESS 'ESC'"
#endif

bool query_key_event(int key, const SDL_Event& event);
bool isAnyPlayerKey(SDLKey key);
bool isPlayerKey(int player_num, SDLKey key);

SDL_Event wait_for_key_event();
void quit_if_quit_event(const SDL_Event& event);

bool isKeyboardEvent(const SDL_Event& event);
bool isJoystickEvent(const SDL_Event& event);

void clear_events();  // Clears the SDL event queue

void assignKeyFromWaitEvent(int player_num, int key_enum);

void clear_keyboard();                                              // set keyboard to none pressed
Uint8* query_keyboard();                                    // keyboard status
void wait_for_key(int somekey); // wait for key SOMEKEY
short query_key_press_event();                       //query_ & clear_key_press_event
void clear_key_press_event();                       // detect a key press :)
short query_text_input_event();                       //query_ & clear_key_press_event
void clear_text_input_event();                       // detect a key press :)
bool query_key_code(int code);                       // OBSOLETE, use query_keyboard
void clear_key_code(int code);
void enable_keyrepeat();
void disable_keyrepeat();
void init_input();

void grab_mouse();
void release_mouse();
Sint32 * query_mouse();

unsigned char convert_to_ascii(int scancode);

#endif
