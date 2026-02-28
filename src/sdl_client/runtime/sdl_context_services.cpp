/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// SDL-specific GameContext wiring and link-time dispatch implementations.
// This file is compiled only in the SDL build (not in openglad_text).

#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>
#include <openglad/input/input.h>
#include <openglad/render/view.h>
#include <openglad/data/level_data.h>
#include <openglad/data/level_render.h>
#include <openglad/data/level_data_hooks.h>
#include <openglad/data/gloader.h>

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

void popup_dialog(const char* title, const char* message);

namespace
{
void sdl_clear_stale_view_controls(LevelData* level)
{
    if (og::runtime::current_session &&
        og::runtime::current_session->myscreen_ != nullptr &&
        &og::runtime::current_session->myscreen_->level_data == level)
    {
        for (auto& view : og::runtime::current_session->myscreen_->viewob)
        {
            if (view)
                view->control = nullptr;
        }
    }
}

void sdl_level_data_draw(LevelData* level, screen* screenp)
{
    for (short i = 0; i < screenp->numviews; i++)
        screenp->viewob[i]->redraw(level, false);
}

std::unique_ptr<LevelRender> sdl_create_level_render(PixieData pixdata[])
{
    return create_sdl_level_render(pixdata);
}

EntityFactory sdl_create_entity_factory()
{
    EntityFactory factory;
    factory.attach_render = [](walker& w, const PixieData& data) {
        w.attach_render(data);
    };
    factory.report_error = [](const std::string& message) {
        popup_dialog("ERROR", message.c_str());
    };
    return factory;
}

const LevelDataHooks kSdlLevelDataHooks{
    .clear_stale_view_controls = sdl_clear_stale_view_controls,
    .draw = sdl_level_data_draw,
    .create_level_render = sdl_create_level_render,
    .create_entity_factory = sdl_create_entity_factory,
};
} // namespace

const LevelDataHooks& sdl_level_data_hooks()
{
    return kSdlLevelDataHooks;
}
