//
// __interrupt routines library
// for Gladiator and associated files
//
// Created:  02-04-95
//
#include "int32.h"
#include "scankeys.h"
#include <dos.h>
#include <conio.h>
#include <i86.h>
#include <stdio.h>
#include <time.h>

unsigned long timer_count=0;
unsigned long timer_control=0;
unsigned long start_time=0;
time_t starttime, endtime;
tm tmbuffer;
dostime_t newtime;
dosdate_t newdate;
long keyboard_grabbed=0;
long timer_grabbed=0;

unsigned short raw_key;
short key_press_event = 0;    // used to signed key-press
char key_list[MAXKEYS+JOYKEYS];

long mouse_state[MSTATE];
long mouse_buttons;

long joy_state[JSTATE];

void (__far __interrupt *old_timer_isr)();
void (__far __interrupt *old_keyboard_isr)();


void change_time(unsigned long new_count)
{
  // Send the control word, mode 2, binary, least/most
  outp(CONTROL_8253, CONTROL_WORD);

  // Now write the least sig. byte to the counter register
  outp(COUNTER_0, LOW_BYTE(new_count));

  // and now the high byte
  outp(COUNTER_0, HI_BYTE(new_count));

  if (new_count == FREQ_HIGH) // beginning
  {
    starttime = time(NULL); // seconds since 1970
  }
  if (new_count == FREQ_NORMAL) // ending
  {
    endtime = starttime + (timer_control/72);
    _localtime(&endtime, &tmbuffer); // convert to structure tm
    newdate.year  = (1900 + tmbuffer.tm_year);
    newdate.month = (1 + tmbuffer.tm_mon);
    newdate.day   = (tmbuffer.tm_mday);
    _dos_setdate(&newdate);
    newtime.hour  = (tmbuffer.tm_hour);
    newtime.minute= (tmbuffer.tm_min);
    newtime.second= (tmbuffer.tm_sec);
    _dos_settime(&newtime);
  }

}

void __interrupt increment_timer()
{
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
  old_timer_isr = _dos_getvect(TIME_KEEPER_INT);
  _dos_setvect(TIME_KEEPER_INT,increment_timer);

}

void release_timer()
{
  if (!timer_grabbed)
    return;
  timer_grabbed = 0;
  _dos_setvect(TIME_KEEPER_INT,old_timer_isr);
//  printf("BC: %ld TC: %ld",basecall,timer_control);
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
  if (keyboard_grabbed)
    return;
  keyboard_grabbed = 1;

  old_keyboard_isr = _dos_getvect(KEYBOARD_INT);
  _dos_setvect(KEYBOARD_INT,key_int);
  // Make sure keyboard state is cleared
  clear_keyboard();
}

void release_keyboard()
{
  if (!keyboard_grabbed)
    return;
  keyboard_grabbed = 0;
  _dos_setvect(KEYBOARD_INT,old_keyboard_isr);
  // Make sure keyboard state is cleared
  clear_keyboard();
}

void __interrupt key_int()
{
  // above fetches a keypress from keyboard, and then
  // reenables both __interrupts and the keyboard control
  //remember cli/sti
extern void *keyasm(void);
  #pragma aux keyasm = \
   "cli"\
   "in al,0x60"\
   "xor ah,ah"\
   "mov raw_key, ax"\
   "in al, 0x61"\
   "or al, 0x82"\
   "out 0x61,al"\
   "and al, 0x7f"\
   "out 0x61, al"\
   "mov al, 0x20"\
   "out 0x20, al"\
   "sti"\
   modify [ax];

  // Set or unset the key we detected ..
  keyasm();
  _disable();


    
  if (raw_key < MAXKEYS && raw_key > 0)    // PRESSing key
  {
    key_list[raw_key] = 1;
    key_press_event = 1;
/*    
if ( (key_list[SCAN_CTRL]) && (raw_key == 69) ) //69 dude! (its the break key??)
    {
      _enable();
      change_time(FREQ_NORMAL);
      release_timer();
      old_keyboard_isr();
      grab_timer();
      change_time(FREQ_HIGH);
      _disable();
    }
*/
  }
  else
  {
    raw_key -= MAXKEYS;
    if (raw_key > 0 && raw_key < MAXKEYS)
      key_list[raw_key] = 0;  // Releasing key
  }
  // Call the lower-level keyboard routines and clear the keyboard buffer ..
//  if ((key_list[SCAN_CTRL] && key_list[SCAN_ALT] && key_list[SCAN_DELETE]) )
//    old_keyboard_isr();
//  while (kbhit())
//    getch();

  _enable();
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
  short dumbcount=0;

  // First wait for key press ..
  while (!key_list[somekey])
   dumbcount++; // do nothing

  // And now for the key to be released ..
  while (key_list[somekey])
    dumbcount--; // do nothing
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
  union REGS inregs,outregs;
  inregs.w.ax = MOUSE_SHOW;
  int386( MOUSE_INT, &inregs, &outregs );
//  asm
//  {
//   mov ax, MOUSE_SHOW
//   short MOUSE_INT
//  }
}

void release_mouse()
{
  union REGS inregs,outregs;

  inregs.w.ax = MOUSE_HIDE;
  int386(MOUSE_INT, &inregs, &outregs);

  //asm
  //{
  // mov ax, MOUSE_HIDE
  // short MOUSE_INT
  //}
}

void reset_mouse()
{
  union REGS inregs,outregs;

  inregs.w.ax = MOUSE_RESET;
  int386(MOUSE_INT,&inregs,&outregs);

  //asm
  //{
  // mov ax, MOUSE_RESET
  // short MOUSE_INT
  //}
}

long * query_mouse()
{
  union REGS inregs,outregs;
//short mouse_x,mouse_y;

  /*
  asm
  {
   mov ax, MOUSE_STATE
   short MOUSE_INT
   mov mouse_buttons, bx
   //mov mouse_state[MOUSE_X], cx
   //mov mouse_state[MOUSE_Y], dx
   shr cx, 1       // divide the X value by 2
   mov mouse_x, cx
   mov mouse_y, dx
  } //Fetches buttons, and sets mouse x,y coords
  mouse_state[MOUSE_LEFT] = (mouse_buttons & LEFT_MASK);
  mouse_state[MOUSE_RIGHT] = (mouse_buttons & RIGHT_MASK);
  mouse_state[MOUSE_X] = mouse_x;
  mouse_state[MOUSE_Y] = mouse_y;
  //extracts the button states
  return mouse_state; //return pointer to the 4-int mouse state
  */

  inregs.w.ax = MOUSE_STATE;
  int386(MOUSE_INT,&inregs,&outregs);
  mouse_buttons = (long) outregs.w.bx;
  //mouse_x = outregs.w.cx >> 1;
  //mouse_y = outregs.w.dx;
  mouse_state[MOUSE_LEFT] = (long) (mouse_buttons & (long) LEFT_MASK);
  mouse_state[MOUSE_RIGHT] = (long) (mouse_buttons & (long) RIGHT_MASK);
  mouse_state[MOUSE_X] = (long) (outregs.w.cx >> 1);
  mouse_state[MOUSE_Y] = (long) outregs.w.dx;
  return mouse_state;
}

long * query_joy()
{
static long joytemp;  
//union REGS joyinregs,joyoutregs;
long joyx=0,joyy=0;

  outp(JOY_PORT,0);
  joytemp = (long) (~inp(JOY_PORT));
  joy_state[JOY_B1] = (long) (joytemp & (long) JOY_BUTTON_1);
  joy_state[JOY_B2] = (long) (joytemp & (long) JOY_BUTTON_2);
  joy_state[JOY_B3] = (long) (joytemp & (long) JOY_BUTTON_3);
  joy_state[JOY_B4] = (long) (joytemp & (long) JOY_BUTTON_4);
  //ok, should have the buttons

extern void *joyasm(void);
  #pragma aux joyasm = \
   "cli"\
   "xor ax,ax"\
   "xor ebx,ebx"\
   "xor ecx,ecx"\
   "mov ah,0x01"\
   "mov dx,0x201"\
   "out dx,al"\
   "joydis:"\
   "cmp ecx,0xFFFF"\
   "je exit"\
   "inc ecx"\
   "in al,dx"\
   "test al,0x01"\
   "je joyxdone"\
   "joycon:"\
   "test al,0x02"\
   "je joyydone"\
   "jmp joydis"\
   "joyxdone:"\
   "cmp joyx,0"\
   "jne joycon"\
   "mov joyx,ecx"\
   "cmp joyy,0"\
   "jne exit"\
   "jmp joydis"\
   "joyydone:"\
   "cmp joyy,0"\
   "jne joydis"\
   "mov joyy,ecx"\
   "cmp joyx,0"\
   "jne exit"\
   "jmp joydis"\
   "exit:"\
   "sti"\
   modify [ax ecx dx];

  joyasm();
  joy_state[JOY_X] = (long) joyx;
  joy_state[JOY_Y] = (long) joyy;
  
  return joy_state;
}

