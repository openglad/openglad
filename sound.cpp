// Sound object

/* ChangeLog
	buffers: 8/7/02: *moved SDL_OpenAudio to after the silence check in
			  init()
*/

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

    
	if (!SDL_OpenAudio (&des_audio, &real_audio))
		printf("Sound init failed\n");


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
    strcpy(soundlist[SOUND_DIE2],     "DIE2.SOU"); // registered only
    strcpy(soundlist[SOUND_ROAR],     "ROAR.SOU"); // reg
    strcpy(soundlist[SOUND_MONEY],    "MONEY.SOU"); // reg
    strcpy(soundlist[SOUND_EAT],      "EAT.SOU"); // reg

    for (i=0; i < NUMSOUNDS; i++)
    {
      #ifdef SOUND_DB
        printf("Loading sound %d: %s\n", i, soundlist[i]);
      #endif
      load_sound( sound[i], soundlist[i] );
    }

    // Set volume (default is loudest)
    volume = 255;
    set_sound_volume(volume);
    
    #ifdef SOUND_DB
      printf("Done with sound initialization\n");
    #endif
    
    return 1;
	if (!SDL_OpenAudio (&des_audio, &real_audio))
		printf("Sound init failed\n");
}

void soundob::load_sound(SDL_AudioSpec *audio, char * file)
{
/*	audio = (SDL_AudioSpec *)malloc (sizeof (SDL_AudioSpec));
	audio->freq = 16384;
	audio->format = AUDIO_U8;
	audio->samples = 1024;
	audio->callback = audio_cb;
	audio->user_data = NULL;

	if (!SDL_OpenAudio(audio, audio))
		printf ("Can't open audio device for %s\n", file);

	if (!SDL_LoadWAV(file, audio, */
}

void soundob::free_sound(SDL_AudioSpec *sound)
{
	
}


void soundob::shutdown()
{
	int i;

	if (silence)
		return;

	for (i=0; i < NUMSOUNDS; i++)
		if (sound[i] != NULL)
			free_sound(sound[i]);

	SDL_CloseAudio();
}

void soundob::play_sound(short whichnum)
{
//buffers: PORT: commented func:    if (silence)         // If silent mode set, do nothing here
//buffers: PORT: commented func:       return;
//buffers: PORT: commented func:     start_sound(sound[whichnum], whichnum, volume, 0); // 0 means play once?
	
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

void soundob::set_sound_volume(int vol)
{

}
