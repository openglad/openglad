/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* mixtest.c */

#include <graph.h>
#include <stdio.h>
#include <stdlib.h>

#include "detect.h"
#include "smix.h"

#define ON  1
#define OFF 0

#define TRUE  1
#define FALSE 0

#define NUMSOUNDS 6

#define random(num) (int)(((long)rand()*(num))/(RAND_MAX+1))
#define randomize() srand((unsigned)time(NULL))

char *soundfile[NUMSOUNDS] =
  {
    "JET.RAW",
    "GUN.RAW",
    "CRASH.RAW",
    "CANNON.RAW",
    "LASER.RAW",
    "GLASS.RAW"
  };

int baseio, irq, dma, dma16;

SOUND *sound[NUMSOUNDS];

unsigned char volume = 255;

void ourexitproc(void)
  {
    int i;

    for (i=0; i < NUMSOUNDS; ++i)
      if (sound[i] != NULL) free_sound(sound+i);
  }

void init(void)
  {
    int i;

    randomize();

    printf("-------------------------------------------\n");
    printf("Sound Mixing Library v1.21 by Ethan Brodsky\n");
    if (!detect_settings(&baseio, &irq, &dma, &dma16))
      {
        printf("ERROR:  Invalid or non-existant BLASTER environment variable!\n");
        exit(EXIT_FAILURE);
      }
    else
      {
        if (!init_sb(baseio, irq, dma, dma16))
          {
            printf("ERROR:  Error initializing sound card!\n");
            exit(EXIT_FAILURE);
          }
      }

    printf("BaseIO=%Xh     IRQ%u     DMA8=%u     DMA16=%u\n", baseio, irq, dma, dma16);

    printf("DSP version %.2f:  ", dspversion);
    if (sixteenbit)
      printf("16-bit, ");
    else
      printf("8-bit, ");
    if (autoinit)
      printf("Auto-initialized\n");
    else
      printf("Single-cycle\n");

    printf("Loading sounds\n");
    for (i=0; i < NUMSOUNDS; i++)
      load_sound((sound+i), soundfile[i]);

    atexit(ourexitproc); // Install exit procedure for emergency deallocation

    init_mixing();

    set_sound_volume(volume);

    printf("\n");
  }

void shutdown(void)
  {
    int i;

    shutdown_mixing();
    shutdown_sb();

    for (i=0; i < NUMSOUNDS; i++)
      free_sound(sound+i);
  }

int main(void)
  {
    struct rccoord coords;

    int  stop;

    long counter;

    char inkey;
    int  num;
    int  temp;


    init();

    start_sound(sound[0], 0, 255, ON);      /* Start up the jet engine */

    printf("Press:                \n");
    printf("  -   Decrease volume \n");
    printf("  +   Increase volume \n");
    printf("  1   Machine Gun     \n");
    printf("  2   Crash           \n");
    printf("  3   Cannon          \n");
    printf("  4   Laser           \n");
    printf("  5   Breaking glass  \n");
    printf("  Q   Quit            \n");

    stop = FALSE;

    counter = 0;
    
    coords = _gettextposition();

    do
      {
       /* Increment and display counters */
        counter++;
        printf("%8lu %8lu %4u \n", counter, intcount, voicecount);
        _settextposition(coords.row-1, 1);

       /* Maybe start a random sound */
        if (!random(10000))
          {
           num = (random(NUMSOUNDS-1))+1;
           start_sound(sound[num], num, 255, OFF);
          }

       /* Start a sound if a key is pressed */
        if (kbhit())
          {
            inkey = getch();
            if ((inkey >= '0') && (inkey <= '9'))
              {
                num = inkey - '0'; /* Convert to integer */
                if (num < NUMSOUNDS)
                  start_sound(sound[num], num, 255, OFF);
              }
            else
              {
                switch(inkey)
                  {
                    case '-':
                    case '_':
                      if (volume > 0)
                        set_sound_volume(volume -= 4);
                      break;

                    case '+':
                    case '=':
                      if (volume <255)
                        set_sound_volume(volume += 4);
                      break;

                    default:
                      stop = TRUE;
                      break;
                  }
              }
          }
      }
    while (!stop);

    stop_sound(1); /* Stop the jet engine */

    shutdown();

    printf("\n");

    return(0);
  }
