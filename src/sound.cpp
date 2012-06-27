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
// Sound object

/* ChangeLog
	buffers: 8/7/02: *moved SDL_OpenAudio to after the silence check in
			  init()
	buffers: 8/16/02: *sound works now.
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "soundob.h"
#include "SDL_mixer.h"
#include <string>
#include "util.h"
using namespace std;

//#define SOUND_DB   0 // define for debugging messages

char * get_file_path(char * file);


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

	if(Mix_OpenAudio(22050,AUDIO_S16,1,1024)==-1)
	{
		printf("ERROR: Mix_OpenAudio: %s\n",Mix_GetError());
		exit(0);
	}

	Mix_AllocateChannels(8);

	// Init the sounds ..
	strcpy(soundlist[SOUND_BOW],      "twang.wav");
	strcpy(soundlist[SOUND_CLANG],    "clang.wav");
	strcpy(soundlist[SOUND_DIE1],     "die1.wav");
	strcpy(soundlist[SOUND_BLAST],    "blast1.wav");
	strcpy(soundlist[SOUND_SPARKLE],  "faerie1.wav");
	strcpy(soundlist[SOUND_TELEPORT], "teleport.wav");
	strcpy(soundlist[SOUND_YO],       "yo.wav");
	strcpy(soundlist[SOUND_BOLT],     "bolt1.wav");
	strcpy(soundlist[SOUND_HEAL],     "heal1.wav");
	strcpy(soundlist[SOUND_CHARGE],   "charge.wav");
	strcpy(soundlist[SOUND_FWIP],     "fwip.wav");
	strcpy(soundlist[SOUND_EXPLODE],  "explode1.wav");
	strcpy(soundlist[SOUND_DIE2],     "die2.wav"); // registered only
	strcpy(soundlist[SOUND_ROAR],     "roar.wav"); // reg
	strcpy(soundlist[SOUND_MONEY],    "money.wav"); // reg
	strcpy(soundlist[SOUND_EAT],      "eat.wav"); // reg

	for (i=0; i < NUMSOUNDS; i++)
	{
#ifdef SOUND_DB
		printf("Loading sound %d: %s\n", i, soundlist[i]);
#endif

		load_sound( &sound[i], soundlist[i] );
	}

	// Set volume (default is loudest)
	volume = MIX_MAX_VOLUME;

#ifdef SOUND_DB

	printf("Done with sound initialization\n");
#endif

	return 1;
}

void soundob::load_sound(Mix_Chunk **audio, char * file)
{
	char * filepath;
	filepath = get_file_path(file, "sound/");
	*audio = Mix_LoadWAV(filepath);
	if(!*audio)
	{
		printf("ERROR: Mix_LoadWAV: %s\n",Mix_GetError());
		exit(0);
	}
	free(filepath);

	Mix_VolumeChunk(*audio,MIX_MAX_VOLUME/2);
}

void soundob::free_sound(Mix_Chunk **sound)
{
	Mix_FreeChunk(*sound);
}


void soundob::shutdown()
{
	int i;

	if (silence)
		return;

	for (i=0; i < NUMSOUNDS; i++)
		if (sound[i] != NULL)
			free_sound(&sound[i]);

	Mix_CloseAudio();
}

void soundob::play_sound(short whichnum)
{
	if (silence)         // If silent mode set, do nothing here
		return;

	Mix_PlayChannel(-1,sound[whichnum],0);
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
