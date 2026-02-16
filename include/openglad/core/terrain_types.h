/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Terrain genre constants used by simulation and rendering layers.
// Extracted from render/smooth.h so entity/sim code can query terrain
// types without depending on the render module.

// Directional bits for tile adjacency queries
inline constexpr int TO_UP = 1;
inline constexpr int TO_RIGHT = 2;
inline constexpr int TO_DOWN = 4;
inline constexpr int TO_LEFT = 8;
inline constexpr int TO_AROUND = 15;

// Terrain genre IDs returned by smoother::query_genre_x_y()
inline constexpr int TYPE_GRASS = 1;
inline constexpr int TYPE_WATER = 2;
inline constexpr int TYPE_TREES = 3;
inline constexpr int TYPE_DIRT = 4;
inline constexpr int TYPE_COBBLE = 5;
inline constexpr int TYPE_GRASS_DARK = 6;
inline constexpr int TYPE_DIRT_DARK = 7;
inline constexpr int TYPE_WALL = 8;
inline constexpr int TYPE_CARPET = 9;
inline constexpr int TYPE_GRASS_LIGHT = 10;
inline constexpr int TYPE_UNKNOWN = 50;
