/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// SDL-specific GameContext wiring and link-time dispatch implementations.
// This file is compiled only in the SDL build (not in openglad_text).

#include <openglad/platform/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/soundob_sdl.h>
#include <openglad/platform/sdl/video.h>
#include <openglad/resources/gparser.h>
#include <openglad/interface/input/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/platform/io.h>
#include "SDL_mixer.h"

// myscreen and theprefs are now macros defined in base.h / view.h

void input_state_from_sdl(InputState& out)
{
    for (int p = 0; p < MAX_PLAYERS; p++) {
        // Save previous held state to detect press edges
        bool was_held[NUM_INPUT_KEYS];
        for (int k = 0; k < NUM_INPUT_KEYS; k++)
            was_held[k] = out.players[p].held[k];

        // Sample current held state from SDL
        for (int k = 0; k < NUM_INPUT_KEYS; k++) {
            out.players[p].held[k] = isPlayerHoldingKey(p, k);
            // Pressed = held now but wasn't held last frame
            out.players[p].pressed[k] = out.players[p].held[k] && !was_held[k];
        }

        if (!player_allows_diagonal_movement(p))
        {
            out.players[p].held[KEY_UP_RIGHT] = false;
            out.players[p].held[KEY_DOWN_RIGHT] = false;
            out.players[p].held[KEY_DOWN_LEFT] = false;
            out.players[p].held[KEY_UP_LEFT] = false;
            out.players[p].pressed[KEY_UP_RIGHT] = false;
            out.players[p].pressed[KEY_DOWN_RIGHT] = false;
            out.players[p].pressed[KEY_DOWN_LEFT] = false;
            out.players[p].pressed[KEY_UP_LEFT] = false;
        }
    }
}

// Called from the SDL client initialization to populate GameContext fields.
// The production main() or GameSession should call this after io_init.
namespace og::runtime {
void install_sdl_context_services()
{
    // Fields game_screen, prefs, config removed from GameContext.
    // Callers use myscreen, theprefs, cfg globals directly.
}
} // namespace og::runtime

namespace
{
void sdl_clear_stale_view_controls(og::gameplay::GameWorld* world)
{
    if (og::runtime::current_session &&
        og::runtime::current_session->myscreen_ != nullptr &&
        &og::runtime::current_session->myscreen_->world() == world)
    {
        for (auto& view : og::runtime::current_session->myscreen_->viewob)
        {
            if (view)
                view->control = nullptr;
        }
    }
}

void sdl_present_frame()
{
    if (og::runtime::current_session && og::runtime::current_session->myscreen_)
        og::runtime::current_session->myscreen_->swap();
}

void sdl_play_sound(int sound_id)
{
    if (og::runtime::current_session &&
        og::runtime::current_session->myscreen_ &&
        og::runtime::current_session->myscreen_->soundp)
    {
        og::runtime::current_session->myscreen_->soundp->play_sound(static_cast<short>(sound_id));
    }
}

Mix_Music* g_bridge_music = nullptr;

void sdl_stop_music()
{
    Mix_HaltMusic();
    if (g_bridge_music) {
        Mix_FreeMusic(g_bridge_music);
        g_bridge_music = nullptr;
    }
}

void sdl_play_music(const char* music_file)
{
    sdl_stop_music();
    if (music_file == nullptr || music_file[0] == '\0')
        return;

    SDL_RWops* rw = open_read_file(music_file);
    if (!rw)
        rw = open_read_file("sound/", music_file);
    if (!rw)
        return;

    g_bridge_music = Mix_LoadMUS_RW(rw, 1);
    if (!g_bridge_music)
        return;

    Mix_PlayMusic(g_bridge_music, -1);
}

og::render::VideoBase* sdl_create_surface(int w, int h)
{
    (void)w;
    (void)h;
    return new video(false);
}

const og::interface::PlatformBridge kSdlPlatformBridge{
    .present_frame = sdl_present_frame,
    .play_sound = sdl_play_sound,
    .play_music = sdl_play_music,
    .stop_music = sdl_stop_music,
    .create_surface = sdl_create_surface,
    .clear_stale_view_controls = sdl_clear_stale_view_controls,
};
} // namespace

struct SdlPlatformBridgeInstaller {
    SdlPlatformBridgeInstaller()
    {
        og::interface::install_platform_bridge(kSdlPlatformBridge);
    }
};

static SdlPlatformBridgeInstaller g_sdl_platform_bridge_installer;
