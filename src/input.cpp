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
#include <malloc.h>
#include <string>

long quit(long arg1);

int raw_key;
short key_press_event = 0;    // used to signed key-press
bool *key_list;

long mouse_state[MSTATE];
long mouse_buttons;

int mult = 1;

// Zardus: add: arrays to keep track of joystick data
int joy_numaxes[4];
int joy_startval[4];
int joy_numbuttons[4];


//
// Input routines (for handling all events and then setting the appropriate vars)
//

void init_input()
{
	int numjoy, i;
	int listlength = MAXKEYS;
	SDL_Joystick *js;

	numjoy = SDL_NumJoysticks();
	for (i = 0; i < numjoy; i++)
	{
		js = SDL_JoystickOpen(i);
		joy_numaxes[i] = SDL_JoystickNumAxes(js);
		joy_numbuttons[i] = SDL_JoystickNumButtons(js);
		joy_startval[i] = listlength;
		listlength += SDL_JoystickNumAxes(js) * 2 + SDL_JoystickNumButtons(js);
	}

	key_list = (bool *)malloc(sizeof(bool) * listlength);

	SDL_JoystickEventState(SDL_ENABLE);
}

void stop_input()
{
	free(key_list);
	key_list = NULL;
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
			key_list[event->key.keysym.sym] = 1;
			raw_key = event->key.keysym.sym;
			key_press_event = 1;
			break;
		case SDL_KEYUP:
			key_list[event->key.keysym.sym] = 0;
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
			if (event->jaxis.value > 0)
			{
				key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2] = 1;
				key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1] = 0;
				key_press_event = 1;
				raw_key = joy_startval[event->jaxis.which] + event->jaxis.axis * 2;
			}
			else if (event->jaxis.value < 0)
			{
				key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2] = 0;
				key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1] = 1;
				key_press_event = 1;
				raw_key = joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1;
			}
			else
			{
				key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2] = 0;
				key_list[joy_startval[event->jaxis.which] + event->jaxis.axis * 2 + 1] = 0;
			}
			break;
		case SDL_JOYBUTTONDOWN:
			key_list[joy_startval[event->jbutton.which] + joy_numaxes[event->jbutton.which] * 2 + event->jbutton.button] = 1;
			raw_key = joy_startval[event->jbutton.which] + joy_numaxes[event->jbutton.which] * 2 + event->jbutton.button;
			key_press_event = 1;
			break;
		case SDL_JOYBUTTONUP:
			key_list[joy_startval[event->jbutton.which] + joy_numaxes[event->jbutton.which] * 2 + event->jbutton.button] = 0;
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

//
// Set the keyboard array to all zeros, the
// virgin state, nothing depressed
//
void clear_keyboard()
{
	int i = 0;
	for (i = 0; i < MAXKEYS; i++)
		key_list[i] = 0;
}

char * query_keyboard()
{
	return (char *) key_list;
}

void wait_for_key(int somekey)
{
	// First wait for key press ..
	while (!key_list[somekey])
		get_input_events(WAIT);

	// And now for the key to be released ..
	while (key_list[somekey])
		get_input_events(WAIT);
}

short query_key_press_event()
{

	return key_press_event;
}

void clear_key_press_event()
{
	key_press_event = 0;
}

short query_key_code(int code)
{
	return key_list[code];
}

void clear_key_code(int code)
{
	key_list[code] = 0;
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

void reset_mouse()
{}

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
