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
//
// Smooth.h
//
#ifndef SMOOTH_H
#define SMOOTH_H

#include "graph.h"

// Used for deciding cases
#define TO_UP 1
#define TO_RIGHT 2
#define TO_DOWN 4
#define TO_LEFT 8
#define TO_AROUND 15

// These are the 'genre' defines ..
#define TYPE_GRASS 1
#define TYPE_WATER 2
#define TYPE_TREES 3
#define TYPE_DIRT  4
#define TYPE_COBBLE 5
#define TYPE_GRASS_DARK 6
#define TYPE_DIRT_DARK 7
#define TYPE_WALL 8
#define TYPE_CARPET 9
#define TYPE_GRASS_LIGHT 10
#define TYPE_UNKNOWN 50


class smoother
{
	public:
		smoother();
		~smoother();

		void set_target(screen  *target);     // set our target grid to smooth ..
		long query_x_y(long x, long y);       // return target type, ie PIX_GRASS1
		long query_genre_x_y(long x, long y); // returns target genre, ie TYPE_GRASS
		long surrounds(long x, long y, long whatgenre); // returns 0-15 of 4 surroundings
		long smooth(long x, long y);          // smooth at x, y; returns changed or not
		long smooth();                        // smooths entire target grid
		void set_x_y(long x, long y, long whatvalue);  // sets grid location to whatvalue

		unsigned char  *mygrid; // our grid to change
		long maxx, maxy;   // dimensions of our grid ..
		char  *buffer; // start of the data in the target grid
};

#endif
