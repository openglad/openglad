/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// SDL3 audio-stream-backed sound playback. Only included by SDL builds.
#pragma once

#include <SDL3/SDL_audio.h>

#include <openglad/interface/sound.h>
#include <openglad/legacy/soundob.h>  // SOUND_* constants, NUMSOUNDS

#include <string>

// A loaded WAV clip: raw PCM buffer plus its source format.
struct sound_chunk
{
    SDL_AudioSpec spec{};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    float gain = 1.0f;
};

// Number of simultaneously playing clips (mirrors the old SDL2
// mixer's 8 allocated channels).
inline constexpr int NUM_SOUND_CHANNELS = 8;

class sdl_soundob final : public soundob
{
public:
    sdl_soundob();
    explicit sdl_soundob(bool silent);
    ~sdl_soundob() override;

    sdl_soundob(const sdl_soundob&) = delete;
    sdl_soundob& operator=(const sdl_soundob&) = delete;

    int init();
    void shutdown();
    void play_sound(short whichsound) override;
    void load_sound(sound_chunk* audio, const char* file);
    void free_sound(sound_chunk* sound);

    unsigned char set_sound(bool silent) override; // Toggle sound on/off
    std::string soundlist[NUMSOUNDS]; // Our list of sounds
    sound_chunk sound[NUMSOUNDS];     // Loaded clips
    int baseio = 0, irq = 0, dma = 0, dma16 = 0; // Card-specific information
    int volume = 0;                   // Volume: 0 - 255
    unsigned char silence;            // 0 = on, 1 = silent

private:
    SDL_AudioDeviceID device_ = 0;    // 0 = not opened
    SDL_AudioStream* channels_[NUM_SOUND_CHANNELS] = {};
    int next_steal_ = 0;              // round-robin victim when all channels busy
};
