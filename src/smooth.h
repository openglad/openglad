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

#include "SDL.h"
#include "base.h"

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

class screen;

class smoother
{
	public:
		smoother();
        
        void reset();
		void set_target(const PixieData& data);     // set our target grid to smooth ..
		Sint32 smooth();                        // smooths entire target grid
		Sint32 smooth(Sint32 x, Sint32 y);          // smooth at x, y; returns changed or not
		Sint32 query_x_y(Sint32 x, Sint32 y);       // return target type, ie PIX_GRASS1
		Sint32 query_genre_x_y(Sint32 x, Sint32 y); // returns target genre, ie TYPE_GRASS
    
    protected:
		Sint32 surrounds(Sint32 x, Sint32 y, Sint32 whatgenre); // returns 0-15 of 4 surroundings
		void set_x_y(Sint32 x, Sint32 y, Sint32 whatvalue);  // sets grid location to whatvalue
		
		unsigned char  *mygrid; // our grid to change
		Sint32 maxx, maxy;   // dimensions of our grid ..
};

#include "graph.h"

#endif
