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
// Soundob.h file ..
#pragma once

#include "SDL_mixer.h"
#include <string>

inline constexpr int SOUND_BOW = 0;
inline constexpr int SOUND_CLANG = 1;
inline constexpr int SOUND_DIE1 = 2;
inline constexpr int SOUND_BLAST = 3;
inline constexpr int SOUND_SPARKLE = 4;
inline constexpr int SOUND_TELEPORT = 5;
inline constexpr int SOUND_YO = 6;
inline constexpr int SOUND_BOLT = 7;
inline constexpr int SOUND_HEAL = 8;
inline constexpr int SOUND_CHARGE = 9;
inline constexpr int SOUND_FWIP = 10;
inline constexpr int SOUND_EXPLODE = 11;
inline constexpr int SOUND_DIE2 = 12;  // registered only
inline constexpr int SOUND_ROAR = 13;  // orc, reg
inline constexpr int SOUND_MONEY = 14; // reg
inline constexpr int SOUND_EAT = 15;   // reg

inline constexpr int NUMSOUNDS = 16;   // For now, let's use ALL sounds, regardless

//buffers: PORT: don't need this anymore: #include "detect.h"
//buffers: PORT: don't need this anymore: #include "smix.h"

class soundob
{
	public:
		soundob();
		soundob(bool silent);
		~soundob();
		int init();
		void shutdown();
		void play_sound(short whichsound);
		void set_sound_volume(int);
		void load_sound(Mix_Chunk **audio, const char * file);
		void free_sound(Mix_Chunk **sound);

		unsigned char set_sound(bool silent);      // Toggle sound on/off
		void load_sound(SDL_AudioSpec, char *);
		std::string soundlist[NUMSOUNDS];            // Our list of sounds
		Mix_Chunk *sound[NUMSOUNDS];		// AudioSpec for loading sounds
		int baseio, irq, dma, dma16;                // Card-specific information
		int volume;                       // Volume: 0 - 255
		unsigned char silence;                      // 0 = on, 1 = silent
};
