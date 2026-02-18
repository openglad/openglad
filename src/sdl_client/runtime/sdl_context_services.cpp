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
#include <openglad/entities/walker.h>
#include <openglad/data/level_render.h>
#include <openglad/data/level_data_hooks.h>

extern screen* myscreen;
extern options* theprefs;

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
    }
}

// Called from the SDL client initialization to populate GameContext fields.
// The production main() or GameSession should call this after io_init.
namespace og::runtime {
void install_sdl_context_services()
{
    auto& c = ctx();
    c.game_screen = myscreen;
    c.prefs = theprefs;
}
} // namespace og::runtime

// Link-time dispatch functions for level_data.cpp
// These provide SDL-specific behavior that level_data.cpp can't access directly.

void clear_stale_view_controls(LevelData* level)
{
    if (myscreen != nullptr && &myscreen->level_data == level)
    {
        for (auto& view : myscreen->viewob)
        {
            if (view)
                view->control = nullptr;
        }
    }
}

void level_data_wire_entity_from_screen(walker* w)
{
    if (myscreen) {
        w->sim_save = &myscreen->save_data;
        w->sim_enemy_freeze = &myscreen->enemy_freeze;
    }
    if (ctx().sim_events)
        w->sim_events = ctx().sim_events.get();
    w->sim_rng = ctx().rng;
    w->sim_config = ctx().config;
}

void level_data_draw_impl(LevelData* level, screen* screenp)
{
    for (short i = 0; i < screenp->numviews; i++)
        screenp->viewob[i]->redraw(level, false);
}

std::unique_ptr<LevelRender> create_level_render(PixieData pixdata[])
{
    return create_sdl_level_render(pixdata);
}
