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
// Sound object — SDL3 audio streams (SDL2_mixer removed).
//
// Architecture: one playback device opened at S16/stereo/22050 (the old
// SDL2 mixer format), with NUM_SOUND_CHANNELS persistent audio streams
// bound to it. SDL mixes bound streams and converts each stream's input
// format to the device format. Playing a clip picks an idle stream (or
// steals the round-robin victim) and queues the whole PCM buffer.

/* ChangeLog
	buffers: 8/7/02: *moved SDL_OpenAudio to after the silence check in
			  init()
	buffers: 8/16/02: *sound works now.
*/

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <openglad/platform/soundob_sdl.h>
#include <SDL3/SDL.h>
#include <string>
#include <openglad/core/util.h>
#include <openglad/resources/io.h>

//#define SOUND_DB   0 // define for debugging messages

char * get_file_path(const char * file);


sdl_soundob::sdl_soundob()
{
	// Do stuff
	silence = 0;        // default is sound ON
	// sound[] chunks default-initialize to empty (buf == nullptr).
	init();
}

// This version of the constructor will set "silence" to
// the value of toggle before init-ing, so that if we
// don't want sound, we won't load them into memory.
sdl_soundob::sdl_soundob(bool silent)
{
	silence = silent;
	init();             // init will do nothing if silent is set
}

sdl_soundob::~sdl_soundob()
{
	shutdown();
}

int sdl_soundob::init()
{
	int i;

    // Free any existing sounds
	for (i=0; i < NUMSOUNDS; i++)
		free_sound(&sound[i]);

	// Do we have sounds on?
	if (silence)
		return 0;

	// First enable: open the device and bind the mixing streams once;
	// they persist across set_sound() toggles until shutdown().
	if (device_ == 0)
	{
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
		{
			LogError("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}\n", SDL_GetError());
			exit(0);
		}

		// Mirrors the old mixer's open-audio(22050, S16, stereo, 1024);
		// SDL3 has no chunksize knob. Devices start unpaused.
		const SDL_AudioSpec want{SDL_AUDIO_S16, 2, 22050};
		device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
		if (device_ == 0)
		{
			LogError("SDL_OpenAudioDevice failed: {}\n", SDL_GetError());
			exit(0);
		}

		for (int c = 0; c < NUM_SOUND_CHANNELS; c++)
		{
			channels_[c] = SDL_CreateAudioStream(nullptr, nullptr);
			if (channels_[c] == nullptr || !SDL_BindAudioStream(device_, channels_[c]))
			{
				LogError("SDL audio stream setup failed: {}\n", SDL_GetError());
				exit(0);
			}
		}
	}

	// Init the sounds ..
	soundlist[SOUND_BOW]      = "twang.wav";
	soundlist[SOUND_CLANG]    = "clang.wav";
	soundlist[SOUND_DIE1]     = "die1.wav";
	soundlist[SOUND_BLAST]    = "blast1.wav";
	soundlist[SOUND_SPARKLE]  = "faerie1.wav";
	soundlist[SOUND_TELEPORT] = "teleport.wav";
	soundlist[SOUND_YO]       = "yo.wav";
	soundlist[SOUND_BOLT]     = "bolt1.wav";
	soundlist[SOUND_HEAL]     = "heal1.wav";
	soundlist[SOUND_CHARGE]   = "charge.wav";
	soundlist[SOUND_FWIP]     = "fwip.wav";
	soundlist[SOUND_EXPLODE]  = "explode1.wav";
	soundlist[SOUND_DIE2]     = "die2.wav"; // registered only
	soundlist[SOUND_ROAR]     = "roar.wav"; // reg
	soundlist[SOUND_MONEY]    = "money.wav"; // reg
	soundlist[SOUND_EAT]      = "eat.wav"; // reg

	for (i=0; i < NUMSOUNDS; i++)
	{
#ifdef SOUND_DB
		Log("Loading sound {}: {}\n", i, soundlist[i]);
#endif

		load_sound( &sound[i], soundlist[i].c_str() );
	}

	// Set volume (default is loudest)
	volume = 128;

#ifdef SOUND_DB

	Log("Done with sound initialization\n");
#endif

	return 1;
}

void sdl_soundob::load_sound(sound_chunk *audio, const char * file)
{
    SDL_IOStream* rw = open_read_file("sound/", file);

	// closeio=true: SDL_LoadWAV_IO closes rw itself, success or failure.
	if (rw == nullptr || !SDL_LoadWAV_IO(rw, true, &audio->spec, &audio->buf, &audio->len))
	{
		LogError("SDL_LoadWAV_IO failed: {}\n", SDL_GetError());
		exit(0);
	}

	// Half volume, matching the old mixer's per-chunk volume of max/2.
	audio->gain = 0.5f;
}

void sdl_soundob::free_sound(sound_chunk *soundp)
{
	SDL_free(soundp->buf);
	*soundp = sound_chunk{};
}


void sdl_soundob::shutdown()
{
	int i;

	for (i=0; i < NUMSOUNDS; i++)
		free_sound(&sound[i]);

	for (int c = 0; c < NUM_SOUND_CHANNELS; c++)
	{
		if (channels_[c] != nullptr)
		{
			SDL_DestroyAudioStream(channels_[c]); // unbinds from the device
			channels_[c] = nullptr;
		}
	}

	if (device_ != 0)
	{
		SDL_CloseAudioDevice(device_);
		device_ = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
}

void sdl_soundob::play_sound(short whichnum)
{
	if (silence)         // If silent mode set, do nothing here
		return;

	if (whichnum < 0 || whichnum >= NUMSOUNDS || sound[whichnum].buf == nullptr)
		return;

	const sound_chunk& chunk = sound[whichnum];

	// Find an idle channel (nothing queued on the input side, nothing
	// converted and waiting on the output side).
	SDL_AudioStream* channel = nullptr;
	for (int c = 0; c < NUM_SOUND_CHANNELS; c++)
	{
		SDL_AudioStream* s = channels_[c];
		if (s != nullptr && SDL_GetAudioStreamQueued(s) == 0 &&
		    SDL_GetAudioStreamAvailable(s) == 0)
		{
			channel = s;
			break;
		}
	}

	// All busy: steal one round-robin (the old mixer's play-on-any-channel
	// dropped the new sound instead; stealing keeps combat cues audible).
	if (channel == nullptr)
	{
		channel = channels_[next_steal_];
		next_steal_ = (next_steal_ + 1) % NUM_SOUND_CHANNELS;
		if (channel == nullptr)
			return;
		SDL_ClearAudioStream(channel);
	}

	SDL_SetAudioStreamFormat(channel, &chunk.spec, nullptr);
	SDL_SetAudioStreamGain(channel, chunk.gain);
	SDL_PutAudioStreamData(channel, chunk.buf, static_cast<int>(chunk.len));
}

// Used to turn sound on or off
unsigned char sdl_soundob::set_sound(bool silent)
{
	if (silence == silent)      // Are we already set this way?
		return silence;

	silence = silent;
	init();

	return silence;
}

std::unique_ptr<soundob> create_soundob(bool silent)
{
    return std::make_unique<sdl_soundob>(silent);
}
