//
// Interupt.h
//
// Header file for Interupt.cpp, the DOS __interrupt routines
//
// Version 1.0   02-04-95:
//  * Timer routines, for Glad main code
//  * Allowed hi-speed timer
//
// Version 2.0
//  * Keyboard __interrupts, for shared code
//
// Version 3.0
//  * Mouse code
//  * added non-resettable timer count,
//  * added several keyboard 'utilities'
//
//
#define VERSION_INT 3

// Z's script: //#include <dos.h>
#include <SDL/SDL.h>
#include <ctype.h>


#define TIME_KEEPER_INT 0x1C
#define KEYBOARD_INT 0x09
#define CPPARGS ...

#define DIVISOR (unsigned long) 4
#define CONTROL_8253 0x43               // timer control register
#define CONTROL_WORD 0x3C               // set the counter's mode
#define COUNTER_0    0x40            // counter zero
#define FREQ_NORMAL  (unsigned long) 65536  // normal timer rate
#define FREQ_HIGH    (unsigned long) (FREQ_NORMAL/DIVISOR)  // hi-speed timer

#define LOW_BYTE(n)  (n & 0x00ff)
#define HI_BYTE(n)   ((n>>8) & 0x00ff)

//Keyboard defines
#define MAXKEYS 320
#define KEY_BUFFER 0x60
#define KEY_CONTROL 0x61
#define INT_CONTROL 0x20
#define JOYKEYS 8 //these keys are reserved to hold the joystick state
#define JOY_KEYBOARD_B1 (MAXKEYS + 0)
#define JOY_KEYBOARD_B2 (MAXKEYS + 1)
#define JOY_KEYBOARD_B3 (MAXKEYS + 2)
#define JOY_KEYBOARD_B4 (MAXKEYS + 3)
#define JOY_KEYBOARD_LEFT (MAXKEYS + 4)
#define JOY_KEYBOARD_RIGHT (MAXKEYS + 5)
#define JOY_KEYBOARD_UP (MAXKEYS + 6)
#define JOY_KEYBOARD_DOWN (MAXKEYS + 7)  //above map joystick state to keyboard

//Mouse Defines
#define MOUSE_INT 0x33
#define MOUSE_RESET 0
#define MOUSE_STATE 3
#define MSTATE 4
#define MOUSE_SHOW 01
#define MOUSE_HIDE 02
#define MOUSE_X 0
#define MOUSE_Y 1
#define MOUSE_LEFT 2
#define MOUSE_RIGHT 3
#define LEFT_MASK 1
#define RIGHT_MASK 2

//joystick defines
#define JOY_PORT 0x201
#define JOY_INTERRUPT 0x15
#define JOY_BIOS 0x84
#define JOY_READ_BUTTONS 0x00
#define JOY_READ_POSITION 0x01
#define JOY_BUTTON_1 0x10
#define JOY_BUTTON_2 0x20
#define JOY_BUTTON_3 0x40
#define JOY_BUTTON_4 0x80
#define JOY_X 0 //indexes in array
#define JOY_Y 1
#define JOY_B1 2
#define JOY_B2 3
#define JOY_B3 4
#define JOY_B4 5 //indexes in array
#define JSTATE 6 //there are 6 longs in this structure
// they define the x and y deflections, and four button states, so we
// can support chad's gamepad winner thingy




void change_time(unsigned long new_count);

//buffers: PORT: doesnt compile void __interrupt increment_timer();
void grab_timer();
void release_timer();
void reset_timer();
long query_timer();
unsigned long query_timer_control();

void grab_keyboard();                                               // mask the keyboard short.
void release_keyboard();                                    // restore normal short.
int query_key();                                                            // return last keypress
void clear_keyboard();                                              // set keyboard to none pressed
char * query_keyboard();                                    // keyboard status
void wait_for_key(unsigned char somekey); // wait for key SOMEKEY
short query_key_press_event();                       //query_ & clear_key_press_event
void clear_key_press_event();                       // detect a key press :)
short query_key_code(short code);                       // OBSOLETE, use query_keyboard

void grab_mouse();
void release_mouse();
void reset_mouse();
long * query_mouse();

long * query_joy();

// Zardus: add: lowercase func
void lowercase(char *);
