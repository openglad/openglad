/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#pragma once

//
// H file for palette.cpp
//

//#include <stdio.h>
//the above is included in palette.cpp now

#include <openglad/interface/base.h>
#include <span>

short load_and_set_palette(const char *filename, std::span<unsigned char> newpalette); // load/set palette from disk
short load_palette(const char *filename, std::span<unsigned char> newpalette); // load palette from disk
short set_palette(std::span<const unsigned char> newpalette); // set palette
void adjust_palette(std::span<const unsigned char> whichpal, Sint32 amount); // gamma correction
void cycle_palette(std::span<unsigned char> newpalette, short start, short end, short shift); //color cycling

// BRIGHTNESS (cfg graphics/brightness). The applied gamma lives in session
// state and set_palette() re-applies it every time, so a palette swap
// (keyframe sync, level start, the cheat palette, the intro fades) can no
// longer wipe it. The value is seeded from cfg on first use; the DISPLAY
// -/+ pair calls set_display_brightness_steps, and RESTORE SETTINGS calls
// reload_display_brightness_from_cfg after re-reading the defaults.
Sint32 display_brightness_steps();
void set_display_brightness_steps(Sint32 steps);
void reload_display_brightness_from_cfg();

void query_palette_reg(unsigned char index, int *red, int *green, int *blue);
void set_palette_reg(unsigned char index,int red,int green,int blue);
short save_palette(std::span<const unsigned char> whatpalette);
