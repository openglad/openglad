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
#define GET_STATE_X(state) (static_cast<std::int32_t>(reinterpret_cast<intptr_t>(state) % MAP_WIDTH) * GRID_SIZE)
#define GET_STATE_Y(state) (static_cast<std::int32_t>(reinterpret_cast<intptr_t>(state) / MAP_WIDTH) * GRID_SIZE)

#endif // OPENGLAD_ENTITIES_PATHFINDING_GRID_H
