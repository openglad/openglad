/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* лл SMIX.H лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

#define TRUE  1
#define FALSE 0

#define ON  1
#define OFF 0

typedef struct
  {
    signed   char *soundptr;
    unsigned long soundsize;
  } SOUND;

extern "C" int  init_sb(int baseio, int irq, int dma, int dma16);
extern "C" void shutdown_sb(void);

extern "C" void init_mixing(void);
extern "C" void shutdown_mixing(void);

extern "C" void load_sound(SOUND **sound, char *filename);
extern "C" void free_sound(SOUND **sound);

extern "C" void start_sound(SOUND *sound, int index, unsigned char volume, int loop);
extern "C" void stop_sound(int index);

extern "C" void set_sound_volume(unsigned char new_volume);

extern "C" volatile long intcount;         /* Current count of sound interrupts */
extern "C" volatile int  voicecount;       /* Number of voices currently in use */

extern "C" float dspversion;
extern "C" int   autoinit;
extern "C" int   sixteenbit;

/* лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

