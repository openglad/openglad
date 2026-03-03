/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// SDL_mixer-backed sound playback. Only included by SDL builds.
#pragma once

#include "SDL_mixer.h"

#include <openglad/platform/sound.h>
#include <openglad/legacy/soundob.h>  // SOUND_* constants, NUMSOUNDS

#include <string>

class sdl_soundob final : public soundob
{
public:
    sdl_soundob();
    explicit sdl_soundob(bool silent);
    ~sdl_soundob() override;

    int init();
    void shutdown();
    void play_sound(short whichsound) override;
    void set_sound_volume(int);
    void load_sound(Mix_Chunk** audio, const char* file);
    void free_sound(Mix_Chunk** sound);

    unsigned char set_sound(bool silent) override; // Toggle sound on/off
    void load_sound(SDL_AudioSpec, char*);
    std::string soundlist[NUMSOUNDS]; // Our list of sounds
    Mix_Chunk* sound[NUMSOUNDS];      // AudioSpec for loading sounds
    int baseio, irq, dma, dma16;      // Card-specific information
    int volume;                       // Volume: 0 - 255
    unsigned char silence;            // 0 = on, 1 = silent
};
