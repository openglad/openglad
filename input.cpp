//
// __interrupt routines library
// for Gladiator and associated files
//
// Created:  02-04-95
//
#include "input.h"
#include "scankeys.h"
// Z's script: #include <dos.h>
// Z's script: #include <conio.h>
// Z's script: #include <i86.h>
#include <stdio.h>
#include <time.h>

unsigned long timer_count=0;
unsigned long timer_control=0;
unsigned long start_time=0;
time_t starttime, endtime;
tm tmbuffer;
// Z: might need these two later
// dostime_t newtime;
// dosdate_t newdate;
long keyboard_grabbed=0;
long timer_grabbed=0;

unsigned short raw_key;
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

// Zardus: PORT: g++ isn't a bit __stuff fan: void __interrupt increment_timer()
// Put this in for now:
void increment_timer()
{
// Theirs:
//  if (!(timer_control%DIVISOR))
//  {
//    old_timer_isr();
//    basecall++;
//  }
  timer_count++;
  timer_control++;
}

void grab_timer()
{
  if (timer_grabbed)
    return;
  timer_grabbed = 1;
// Zardus: PORT: seem to be dos things:  old_timer_isr = _dos_getvect(TIME_KEEPER_INT);
// Same here:  _dos_setvect(TIME_KEEPER_INT,increment_timer);

}

void release_timer()
{
  if (!timer_grabbed)
    return;
  timer_grabbed = 0;
// Zardus: dos stuff:  _dos_setvect(TIME_KEEPER_INT,old_timer_isr);
}

void reset_timer()
{
  timer_count = 0;
}

long query_timer()
{
  return timer_count;
}

unsigned long query_timer_control()
{
  return timer_control;
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

// Zardus: __stuff again. will replace temporarily: void __interrupt key_int()
void key_int()
{
  // above fetches a keypress from keyboard, and then
  // reenables both __interrupts and the keyboard control
  //remember cli/sti
}

short query_key()
{
  return raw_key;
}

//
// Set the keyboard array to all zeros, the
// virgin state, nothing depressed
//
void clear_keyboard()
{
  unsigned char i;

  for (i=0; i < MAXKEYS+JOYKEYS; i++)
   key_list[i] = 0;  // 0 is off, of course

}

char * query_keyboard()
{
  return key_list;
}

void wait_for_key(unsigned char somekey)
{
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
}

void release_mouse()
{
}

void reset_mouse()
{
}

long * query_mouse()
{
  //Fetches buttons, and sets mouse x,y coords
  //mouse_state[MOUSE_LEFT] = (long) (mouse_buttons & (long) LEFT_MASK);
  //mouse_state[MOUSE_RIGHT] = (long) (mouse_buttons & (long) RIGHT_MASK);
  //mouse_state[MOUSE_X] = (long) (outregs.w.cx >> 1);
  //mouse_state[MOUSE_Y] = (long) outregs.w.dx;
  //return mouse_state;
}

long * query_joy()
{
  //joyasm();
  //joy_state[JOY_X] = (long) joyx;
  //joy_state[JOY_Y] = (long) joyy;
  
  //return joy_state;
}

