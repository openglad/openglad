/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef OPENGLAD_ENTITIES_PATHFINDING_GRID_H
#define OPENGLAD_ENTITIES_PATHFINDING_GRID_H

#include <cstdint>
#include <openglad/core/constants.h>

inline constexpr int MAP_WIDTH = 400;
// Multi-floor path-state encoding: state = floor*FLOOR_STRIDE + (y/16)*MAP_WIDTH
// + (x/16). FLOOR_STRIDE exceeds the maximum single-floor linear index
// (254*400+254 = 101854), so floor 0 produces the identical numeric value and
// the %FLOOR_STRIDE mask is a no-op — A* node expansion stays byte-identical.
// See docs/z-axis-design.md.
inline constexpr int MAP_HEIGHT = 256;
inline constexpr intptr_t FLOOR_STRIDE = static_cast<intptr_t>(MAP_WIDTH) * MAP_HEIGHT;
#define GET_STATE_FLOOR(state) (static_cast<std::int32_t>(reinterpret_cast<intptr_t>(state) / FLOOR_STRIDE))
#define GET_STATE_X(state) (static_cast<std::int32_t>((reinterpret_cast<intptr_t>(state) % FLOOR_STRIDE) % MAP_WIDTH) * GRID_SIZE)
#define GET_STATE_Y(state) (static_cast<std::int32_t>((reinterpret_cast<intptr_t>(state) % FLOOR_STRIDE) / MAP_WIDTH) * GRID_SIZE)

#endif // OPENGLAD_ENTITIES_PATHFINDING_GRID_H
