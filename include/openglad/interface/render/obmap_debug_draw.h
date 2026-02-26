/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

class obmap;
class screen;

// Debug visualization for the object collision map.
// Draws grid cells and walker bounding boxes. Extracted from obmap::draw()
// so the entities module has no rendering dependency.
void obmap_debug_draw(obmap& map, screen* scr);
