//
// __interrupt routines library
// for Gladiator and associated files
//
// Created:  02-04-95
//
#include "input.h"
// Z's script: #include <dos.h>
// Z's script: #include <conio.h>
// Z's script: #include <i86.h>
#include <stdio.h>
#include <time.h>

unsigned long start_time=0;
unsigned long reset_value=0;
time_t starttime, endtime;
tm tmbuffer;
// Z: might need these two later
// dostime_t newtime;
// dosdate_t newdate;
long quit(long arg1);

int raw_key;
short key_press_event = 0;    // used to signed key-press
char key_list[MAXKEYS+JOYKEYS];

long mouse_state[MSTATE];
long mouse_buttons;

long joy_state[JSTATE];

// Zardus: PORT: the __stuff seems to freak it out: void (__far __interrupt *old_timer_isr)();
// same here: void (__far __interrupt *old_keyboard_isr)();


void change_time(unsigned long new_count)
{
}

void grab_timer()
{
}

void release_timer()
{
}

void reset_timer()
{
	reset_value = SDL_GetTicks();
}

long query_timer()
{
	// Zardus: why 13.6? With DOS timing, you had to divide 1,193,180 by the desired frequency and
	// that would return ticks / second. Gladiator used to use a frequency of 65536/4 ticks per hour,
	// or 1193180/16383 = 72.3 ticks per second. This translates into 13.6 milliseconds / tick
	return (long) ((SDL_GetTicks() - reset_value) / 13.6);
}

long query_timer_control()
{
	return (long) (SDL_GetTicks() / 13.6);
}


//
// Input routines (for handling all events and then setting the appropriate vars)
//

void get_input_events(bool type)
{
	SDL_Event event;

	if (type == POLL)
		while (SDL_PollEvent(&event)) handle_events(event);
	if (type == WAIT)
	{
		SDL_WaitEvent(&event);
		handle_events(event);
	}
}

void handle_events(SDL_Event event)
{
	switch (event.type)
	{
		// Key pressed or released:
		case SDL_KEYDOWN:
			key_list[event.key.keysym.sym] = 1;
			raw_key = event.key.keysym.sym;
			key_press_event = 1;
			break;
		case SDL_KEYUP:
			key_list[event.key.keysym.sym] = 0;
			break;

		// Mouse event
		case SDL_MOUSEMOTION:
			mouse_state[MOUSE_X] = event.motion.x;
			mouse_state[MOUSE_Y] = event.motion.y;
			break;
		case SDL_MOUSEBUTTONUP:
			if (event.button.button == SDL_BUTTON_LEFT)
				mouse_state[MOUSE_LEFT] = 0;
			if (event.button.button == SDL_BUTTON_RIGHT)
				mouse_state[MOUSE_RIGHT] = 0;
			//mouse_state[MOUSE_LEFT] = SDL_BUTTON(SDL_BUTTON_LEFT);
			//printf ("LMB: %d",  SDL_BUTTON(SDL_BUTTON_LEFT));
			//mouse_state[MOUSE_RIGHT] = SDL_BUTTON(SDL_BUTTON_RIGHT);
			//printf ("RMB: %d",  SDL_BUTTON(SDL_BUTTON_RIGHT));
			break;
		case SDL_MOUSEBUTTONDOWN:
			if (event.button.button == SDL_BUTTON_LEFT)
				mouse_state[MOUSE_LEFT] = 1;
			if (event.button.button == SDL_BUTTON_RIGHT)
				mouse_state[MOUSE_RIGHT] = 1;
			break;
		case SDL_QUIT:
			//buffers: PORT: the quit function is not avialiable to the scen app so we don't try to call it if we compile scen
			#ifndef SCEN
				quit(1);
			#endif
			break;
		default:
			break;
	}
}


//
//Keyboard routines
//

void grab_keyboard()
{
}

void release_keyboard()
{
}

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
	return key_list;
}

void wait_for_key(unsigned char somekey)
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

short query_key_code(short code)
{
  return key_list[code];
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
{
}

long * query_mouse()
{
	// The mouse_state thing is set using get_input_events, though
	// it should probably get its own function
	get_input_events(POLL);
	return mouse_state;
}

long * query_joy()
{
  //joyasm();
  //joy_state[JOY_X] = (long) joyx;
  //joy_state[JOY_Y] = (long) joyy;
  
  //return joy_state;
}

// Zardus: add: some extra routines (one right now) that really shouldn't be here, but we'll put them here anyways
void lowercase(char * string)
{
	int i;
	for (i = 0; i < strlen(string);i++)
		string[i] = tolower(string[i]);
}

//buffers: add: another extra routine.
void uppercase(char *string)
{
	int i;
	for(i=0;i<strlen(string);i++)
		string[i] = toupper(string[i]);
}
