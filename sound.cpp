// Sound object

#include <string.h>
#include <stdio.h>
#include "soundob.h"

//#define SOUND_DB   // define for debugging messages


soundob::soundob()
{
    // Do stuff
    silence = 0;        // default is sound ON
    init();
}

// This version of the constructor will set "silence" to
// the value of toggle before init-ing, so that if we
// don't want sound, we won't load them into memory.
soundob::soundob(unsigned char toggle)
{
    silence = toggle;
    init();             // init will do nothing if toggle (silence) is set
}

soundob::~soundob()
{
    shutdown();
}

int soundob::init()
{
    int i;

    // Guarantee null pointers, regardless of sound status
    for (i=0; i < NUMSOUNDS; i++)
      sound[i] = NULL;
      
    // Do we have sounds on?
    if (silence)
      return 0;
      
    // First get the sound card settings
    if (!detect_settings(&baseio, &irq, &dma, &dma16))
    {
        printf("Sound Error: Invalid or non-existant BLASTER environment variable.\n");
        silence = 1;
        return 0;
    }
    else if (!init_sb(baseio, irq, dma, dma16))
    {
          printf("Sound Error: Error initializing sound card.\n");
          silence = 1;
          return 0;
    }

    #ifdef SOUND_DB
      printf("BaseIO=%Xh,   IRQ%u,   DMA8=%u,   DMA16=%u\n",
        baseio, irq, dma, dma16);
      printf("DSP version %.2f:  ", dspversion);
      if (sixteenbit)
        printf("16-bit, ");
      else
        printf("8-bit, ");
      if (autoinit)
        printf("Auto-initialized\n");
      else
        printf("Single-cycle\n");

      printf("Loading sounds...\n");
    #endif
        
    // Init the sounds ..
    strcpy(soundlist[SOUND_BOW],      "TWANG.SOU");
    strcpy(soundlist[SOUND_CLANG],    "CLANG.SOU");
    strcpy(soundlist[SOUND_DIE1],     "DIE1.SOU");
    strcpy(soundlist[SOUND_BLAST],    "BLAST1.SOU");
    strcpy(soundlist[SOUND_SPARKLE],  "FAERIE1.SOU"); 
    strcpy(soundlist[SOUND_TELEPORT], "TELEPORT.SOU");
    strcpy(soundlist[SOUND_YO],       "YO.SOU");
    strcpy(soundlist[SOUND_BOLT],     "BOLT1.SOU");
    strcpy(soundlist[SOUND_HEAL],     "HEAL1.SOU");
    strcpy(soundlist[SOUND_CHARGE],   "CHARGE.SOU");
    strcpy(soundlist[SOUND_FWIP],     "FWIP.SOU");
    strcpy(soundlist[SOUND_EXPLODE],  "EXPLODE1.SOU");
    //#ifdef REGISTERED
    #if 1
      strcpy(soundlist[SOUND_DIE2],     "DIE2.SOU"); // registered only
      strcpy(soundlist[SOUND_ROAR],     "ROAR.SOU"); // reg
      strcpy(soundlist[SOUND_MONEY],    "MONEY.SOU"); // reg
      strcpy(soundlist[SOUND_EAT],      "EAT.SOU"); // reg
    #endif
    for (i=0; i < NUMSOUNDS; i++)
    {
      #ifdef SOUND_DB
        printf("Loading sound %d: %s\n", i, soundlist[i]);
      #endif
      load_sound( (sound+i), soundlist[i] );
    }

    // Initialize the mixing stuff..
    #ifdef SOUND_DB
      printf("Initializing mixing\n");
    #endif
    init_mixing();

    // Set volume (default is loudest)
    volume = 255;
    set_sound_volume(volume);
    
    #ifdef SOUND_DB
      printf("Done with sound initialization\n");
    #endif
    
    return 1;
}


void soundob::shutdown()
{
    int i;

    if (silence)
      return;
      
    shutdown_mixing();
    shutdown_sb();

    for (i=0; i < NUMSOUNDS; i++)
      if (sound[i] != NULL)
        free_sound(sound+i);

}

void soundob::play_sound(short whichnum)
{
    if (silence)         // If silent mode set, do nothing here
      return;
    start_sound(sound[whichnum], whichnum, volume, 0); // 0 means play once?
}

unsigned char soundob::query_volume()
{
    return volume;
}

unsigned char soundob::set_volume(unsigned char volumelevel)
{
    volume = volumelevel;
    return volume;
}

// Used to turn sound on or off
unsigned char soundob::set_sound(unsigned char toggle)
{
    if (silence == toggle)      // Are we already set this way?
      return silence;

    silence = toggle;
    init();

    return silence;
}

