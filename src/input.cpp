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
// input.cpp
//
// input code
//

#include "input.h"
#include <stdio.h>
#include <time.h>
#include <string.h> //buffers: for strlen
#include <string>

long quit(long arg1);

int raw_key;
short key_press_event = 0;    // used to signed key-press
Uint8* keystates = NULL;

long mouse_state[MSTATE];
long mouse_buttons;

int mult = 1;

#define KEYBOARD 0
#define JOYSTICK 1

int player_input_type[4] = {KEYBOARD, KEYBOARD, KEYBOARD, KEYBOARD};

JoyData player_joy[4];

#define JOY_DEAD_ZONE 8000
#define MAX_NUM_JOYSTICKS 10  // Just in case there are joysticks attached that are not useable (e.g. accelerometer)
SDL_Joystick* joysticks[MAX_NUM_JOYSTICKS];

int player_keys[4][NUM_KEYS] = {
    {
         SDLK_w, SDLK_e, SDLK_d, SDLK_c,  // movements
         SDLK_x, SDLK_z, SDLK_a, SDLK_q,
         SDLK_LCTRL, SDLK_LALT,                    // fire & special
         SDLK_TAB,                               // switch guys
         SDLK_1,                                 // change special
         SDLK_s,                                 // Yell
         SDLK_LSHIFT,                        // Shifter
         SDLK_2,                                 // Options menu
         SDLK_F5,                                 // Cheat key
    },
    {
         SDLK_KP8, SDLK_KP9, SDLK_KP6, SDLK_KP3,  // movements
         SDLK_KP2, SDLK_KP1, SDLK_KP4, SDLK_KP7,
         SDLK_KP0, SDLK_KP_ENTER,                    // fire & special
         SDLK_KP_PLUS,                          // switch guys
         SDLK_KP_MINUS,                         // change special
         SDLK_KP5,                                // Yell
         SDLK_KP_PERIOD,                                // Shifter
         SDLK_KP_MULTIPLY,                         // Options menu
         SDLK_F8,                                    // Cheat key
     },
     {
         SDLK_i, SDLK_o, SDLK_l, SDLK_PERIOD,  // movements
         SDLK_COMMA, SDLK_m, SDLK_j, SDLK_u,
         SDLK_SPACE, SDLK_SEMICOLON,                    // fire & special
         SDLK_BACKSPACE,                               // switch guys
         SDLK_7,                                 // change special
         SDLK_k,                                 // Yell
         SDLK_RSHIFT,                        // Shifter
         SDLK_8,                                 // Options menu
         SDLK_F7,                                 // Cheat key
     },
     {
         SDLK_t, SDLK_y, SDLK_h, SDLK_n,  // movements
         SDLK_b, SDLK_v, SDLK_f, SDLK_r,
         SDLK_5, SDLK_6,                    // fire & special
         SDLK_EQUALS,                               // switch guys
         SDLK_3,                                 // change special
         SDLK_g,                                 // Yell
         SDLK_MINUS,                        // Shifter
         SDLK_4,                                 // Options menu
         SDLK_F6,                                 // Cheat key
     }
};



//
// Input routines (for handling all events and then setting the appropriate vars)
//

void init_input()
{
    keystates = SDL_GetKeyState(NULL);
    
    // Set up joysticks
    for(int i = 0; i < MAX_NUM_JOYSTICKS; i++)
    {
        joysticks[i] = NULL;
    }
    
	int numjoy;

	numjoy = SDL_NumJoysticks();

	for(int i = 0; i < numjoy; i++)
	{
        joysticks[i] = SDL_JoystickOpen(i);
        if(joysticks[i] == NULL)
            continue;
        player_joy[i] = JoyData(i);
	}

	SDL_JoystickEventState(SDL_ENABLE);
}

void stop_input()
{
    
}

void get_input_events(bool type)
{
	SDL_Event event;

	if (type == POLL)
		while (SDL_PollEvent(&event))
			handle_events(&event);
	if (type == WAIT)
	{
		SDL_WaitEvent(&event);
		handle_events(&event);
	}
}

void handle_events(SDL_Event *event)
{
	switch (event->type)
	{
			// Key pressed or released:
		case SDL_KEYDOWN:
			raw_key = event->key.keysym.sym;
			key_press_event = 1;
			break;
		case SDL_KEYUP:
			break;

			// Mouse event
		case SDL_MOUSEMOTION:
			//printf("%i %i  -  %i %i\n", event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
			//if (!(event.motion.x < 10 && mouse_state[MOUSE_X] * mult > 620)
			//	&& !(event.motion.y == 0 && mouse_state[MOUSE_Y] > 20))
			mouse_state[MOUSE_X] = event->motion.x / mult;
			//if (!(event.motion.y < 10 && mouse_state[MOUSE_Y] * mult > 460))
			mouse_state[MOUSE_Y] = event->motion.y / mult;
			break;
		case SDL_MOUSEBUTTONUP:
			if (event->button.button == SDL_BUTTON_LEFT)
				mouse_state[MOUSE_LEFT] = 0;
			if (event->button.button == SDL_BUTTON_RIGHT)
				mouse_state[MOUSE_RIGHT] = 0;
			//mouse_state[MOUSE_LEFT] = SDL_BUTTON(SDL_BUTTON_LEFT);
			//printf ("LMB: %d",  SDL_BUTTON(SDL_BUTTON_LEFT));
			//mouse_state[MOUSE_RIGHT] = SDL_BUTTON(SDL_BUTTON_RIGHT);
			//printf ("RMB: %d",  SDL_BUTTON(SDL_BUTTON_RIGHT));
			break;
		case SDL_MOUSEBUTTONDOWN:
			if (event->button.button == SDL_BUTTON_LEFT)
				mouse_state[MOUSE_LEFT] = 1;
			if (event->button.button == SDL_BUTTON_RIGHT)
				mouse_state[MOUSE_RIGHT] = 1;
			break;
		case SDL_JOYAXISMOTION:
			if (event->jaxis.value > 8000)
			{
				//key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2] = 1;
				//key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1] = 0;
				key_press_event = 1;
				//raw_key = joy_startval[event->jaxis.which] + event->jaxis.axis * 2;
			}
			else if (event->jaxis.value < -8000)
			{
				//key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2] = 0;
				//key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1] = 1;
				key_press_event = 1;
				//raw_key = joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1;
			}
			else
			{
				//key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2] = 0;
				//key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1] = 0;
			}
			break;
		case SDL_JOYBUTTONDOWN:
			//key_list[joy_startval[event->jbutton.which] + joy_numaxes[event->jbutton.which] * 2 + event->jbutton.button] = 1;
			//raw_key = joy_startval[event->jbutton.which] + joy_numaxes[event->jbutton.which] * 2 + event->jbutton.button;
			key_press_event = 1;
			break;
		case SDL_JOYBUTTONUP:
			//key_list[joy_startval[event->jbutton.which] + joy_numaxes[event->jbutton.which] * 2 + event->jbutton.button] = 0;
			break;
		case SDL_QUIT:
			quit(1);
			break;
		default:
			break;
	}
}


//
//Keyboard routines
//

void grab_keyboard()
{}

void release_keyboard()
{}

int query_key()
{
	return raw_key;
}

bool query_key_event(int key, const SDL_Event& event)
{
    if(event.type == SDL_KEYDOWN)
        return (event.key.keysym.sym == key);
    return false;
}

//
// Set the keyboard array to all zeros, the
// virgin state, nothing depressed
//
void clear_keyboard()
{
	key_press_event = 0;
	raw_key = 0;
}

Uint8* query_keyboard()
{
	return keystates;
}

void wait_for_key(int somekey)
{
	// First wait for key press ..
	while (!keystates[somekey])
		get_input_events(WAIT);

	// And now for the key to be released ..
	while (keystates[somekey])
		get_input_events(WAIT);
}

JoyData::JoyData()
    : index(-1), numAxes(0), numButtons(0)
{}

JoyData::JoyData(int index)
    : index(index), numAxes(0), numButtons(0)
{
    SDL_Joystick *js = joysticks[index];
    if(js == NULL)
        return;
    numAxes = SDL_JoystickNumAxes(js);
    numButtons = SDL_JoystickNumButtons(js);
    
    // Clear all keys for this joystick
    for(int i = 0; i < NUM_KEYS; i++)
    {
        key_type[i] = NONE;
        key_index[i] = 0;
    }
    
    // Default movement
    if(numAxes > 0)
    {
        key_type[KEY_UP] = POS_AXIS;
        key_index[KEY_UP] = 0;
        key_type[KEY_DOWN] = NEG_AXIS;
        key_index[KEY_DOWN] = 0;
    }
    if(numAxes > 1)
    {
        key_type[KEY_RIGHT] = POS_AXIS;
        key_index[KEY_RIGHT] = 1;
        key_type[KEY_LEFT] = NEG_AXIS;
        key_index[KEY_LEFT] = 1;
    }
    
    // Default actions
    if(numButtons > 0)
    {
        key_type[KEY_FIRE] = BUTTON;
        key_index[KEY_FIRE] = 0;
    }
    if(numButtons > 1)
    {
        key_type[KEY_SPECIAL] = BUTTON;
        key_index[KEY_SPECIAL] = 1;
    }
    if(numButtons > 2)
    {
        key_type[KEY_SPECIAL_SWITCH] = BUTTON;
        key_index[KEY_SPECIAL_SWITCH] = 2;
    }
    if(numButtons > 3)
    {
        key_type[KEY_SHIFTER] = BUTTON;
        key_index[KEY_SHIFTER] = 3;
    }
    if(numButtons > 4)
    {
        key_type[KEY_SWITCH] = BUTTON;
        key_index[KEY_SWITCH] = 4;
    }
    if(numButtons > 5)
    {
        key_type[KEY_YELL] = BUTTON;
        key_index[KEY_YELL] = 5;
    }
}

bool JoyData::getState(int key_enum) const
{
    if(index < 0)
        return false;
    if(key_type[key_enum] == POS_AXIS)
        return SDL_JoystickGetAxis(joysticks[index], key_index[key_enum]) > JOY_DEAD_ZONE;
    else if(key_type[key_enum] == POS_AXIS)
        return SDL_JoystickGetAxis(joysticks[index], key_index[key_enum]) < -JOY_DEAD_ZONE;
    else if(key_type[key_enum] == BUTTON)
        return SDL_JoystickGetButton(joysticks[index], key_index[key_enum]);
    return false;
}

bool JoyData::getPress(int key_enum, const SDL_Event& event) const
{
    switch(key_type[key_enum])
    {
        case BUTTON:
            if(event.type == SDL_JOYBUTTONDOWN)
            {
                return (event.jbutton.which == index && event.jbutton.button == key_index[key_enum]);
            }
        return false;
        case POS_AXIS:
            if(event.type == SDL_JOYAXISMOTION)
            {
                return (event.jaxis.which == index && event.jaxis.axis == key_index[key_enum] && event.jaxis.value > JOY_DEAD_ZONE);
            }
        return false;
        case NEG_AXIS:
            if(event.type == SDL_JOYAXISMOTION)
            {
                return (event.jaxis.which == index && event.jaxis.axis == key_index[key_enum] && event.jaxis.value < -JOY_DEAD_ZONE);
            }
        return false;
        default:
        return false;
    }
}

bool JoyData::hasButtonSet(int key_enum) const
{
    return (key_type[key_enum] != NONE);
}

bool isPlayerHoldingKey(int player_index, int key_enum)
{
    if(player_input_type[player_index] == JOYSTICK)
    {
        return player_joy[player_index].getState(key_enum);
    }
    else
    {
        return keystates[player_keys[player_index][key_enum]];
    }
}

bool didPlayerPressKey(int player_index, int key_enum, const SDL_Event& event)
{
    if(player_input_type[player_index] == JOYSTICK && player_joy[player_index].hasButtonSet(key_enum))
    {
        // This key is on the joystick, so check it.
        return player_joy[player_index].getPress(key_enum, event);
    }
    else
    {
        // If the player is using KEYBOARD or doesn't have a joystick button set for this key, then check the keyboard.
        if(event.type == SDL_KEYDOWN)
        {
            return (event.key.keysym.sym == player_keys[player_index][key_enum]);
        }
        return false;
    }
}

short query_key_press_event()
{

	return key_press_event;
}

void clear_key_press_event()
{
	key_press_event = 0;
}

void enable_keyrepeat()
{
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,SDL_DEFAULT_REPEAT_INTERVAL);
}

void disable_keyrepeat()
{
	SDL_EnableKeyRepeat(0,0);
}



//
// Mouse routines
//

void grab_mouse()
{
	SDL_ShowCursor(SDL_ENABLE);
}

void release_mouse()
{
	SDL_ShowCursor(SDL_DISABLE);
}

long * query_mouse()
{
	// The mouse_state thing is set using get_input_events, though
	// it should probably get its own function
	get_input_events(POLL);
	return mouse_state;
}

// Zardus: add: this sets the multiplier mult
void set_mult(int m)
{
	mult = m;
}
