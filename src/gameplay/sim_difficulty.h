/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

// The world's difficulty as a percent for the sim's scaling math: 100 when
// there is no running game or world, or when the world's difficulty is not
// positive. One definition (walker.cpp) for walker's and living's callers.
std::uint32_t query_difficulty_percent();
