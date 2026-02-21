/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Entity classification enum, extracted from base.h for use without SDL.

enum class Order : unsigned char {
    Living = 0,
    Weapon = 1,
    Treasure = 2,
    Generator = 3,
    FX = 4,
    Special = 5,
    Button1 = 6
};
