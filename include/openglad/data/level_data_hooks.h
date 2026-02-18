/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/data/level_render.h>
#include <memory>

class LevelData;
class walker;
class screen;
class PixieData;

// Capability hooks for LevelData.
//
// These functions are implemented by SDL builds (sdl_context_services.cpp) and
// headless builds (platform_headless.cpp).  Both implementations include this
// header so the compiler enforces signature parity and prevents silent drift.

// Clear any stale view-control pointers that reference entities in |level|.
// SDL: nulls viewscreen::control for matching views. Headless: no-op.
void clear_stale_view_controls(LevelData* level);

// Wire sim context pointers from the global screen onto an entity.
// SDL: wires save_data, enemy_freeze, sim_events, rng, config from globals.
// Headless: no-op (LevelData::wire_entity handles sim context).
void level_data_wire_entity_from_screen(walker* w);

// Draw the level through all active viewscreens.
// SDL: iterates viewob[] and calls redraw(). Headless: one-time warning.
void level_data_draw_impl(LevelData* level, screen* screenp);

// Create a tile renderer for the level.
// SDL: returns LevelRender. Headless: returns nullptr with one-time warning.
std::unique_ptr<LevelRender> create_level_render(PixieData pixdata[]);
