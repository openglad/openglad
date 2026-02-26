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
#pragma once

#include <cstdint>
#include <openglad/resources/pixie_data.h>
#include <openglad/core/terrain_types.h>


class smoother
{
	public:
		smoother();

        void reset();
		void set_target(const PixieData& data);     // set our target grid to smooth ..
		std::int32_t smooth();                        // smooths entire target grid
		std::int32_t smooth(std::int32_t x, std::int32_t y);          // smooth at x, y; returns changed or not
		std::int32_t query_x_y(std::int32_t x, std::int32_t y);       // return target type, ie PIX_GRASS1
		std::int32_t query_genre_x_y(std::int32_t x, std::int32_t y); // returns target genre, ie TYPE_GRASS

    protected:
		std::int32_t surrounds(std::int32_t x, std::int32_t y, std::int32_t whatgenre); // returns 0-15 of 4 surroundings
		void set_x_y(std::int32_t x, std::int32_t y, std::int32_t whatvalue);  // sets grid location to whatvalue

		unsigned char  *mygrid; // our grid to change
		std::int32_t maxx, maxy;   // dimensions of our grid ..
};
