// Soundob.h file ..
#ifndef __SOUNDOB_H
#define __SOUNDOB_H

#define REGISTERED      // synchronize with graph.h!

#define SOUND_BOW       0
#define SOUND_CLANG     1
#define SOUND_DIE1      2
#define SOUND_BLAST     3
#define SOUND_SPARKLE   4
#define SOUND_TELEPORT  5
#define SOUND_YO        6
#define SOUND_BOLT      7
#define SOUND_HEAL      8
#define SOUND_CHARGE    9
#define SOUND_FWIP      10
#define SOUND_EXPLODE   11
#define SOUND_DIE2      12  // registered only
#define SOUND_ROAR      13  // orc, reg
#define SOUND_MONEY     14  // reg
#define SOUND_EAT       15  // reg

//#ifdef REGISTERED
  #define NUMSOUNDS 16   // For now, let's use ALL sounds, regardless
//#else
//  #define NUMSOUNDS 12
//#endif


#include "detect.h"
#include "smix.h"

class soundob
{
  public:
    soundob();
    soundob(unsigned char toggle);
    ~soundob();
    int            init();
    void           shutdown();
    
    void           play_sound(short whichsound);
    unsigned char query_volume();
    unsigned char set_sound(unsigned char toggle);      // Toggle sound on/off
    unsigned char set_volume(unsigned char volumelevel);
    char soundlist[NUMSOUNDS][14];              // Our list of sounds
    
    int baseio, irq, dma, dma16;                // Card-specific information
    SOUND *sound[NUMSOUNDS];
    unsigned char volume;                       // Volume: 0 - 255
    unsigned char silence;                      // 0 = on, 1 = silent
    
  
};

#endif
