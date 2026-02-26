/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

class walker;
class viewscreen;

// Rendering functions for walker entities.
// These live in the render layer, not the entity layer, because they
// depend on the screen/video system for drawing.
bool draw_walker(walker& w, viewscreen* view_buf);
bool draw_walker_tile(walker& w, viewscreen* view_buf);
void draw_walker_path(walker& w, viewscreen* view_buf);
void draw_small_health_bar(walker* w, viewscreen* view_buf);
