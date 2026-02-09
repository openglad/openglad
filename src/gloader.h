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

// Definition of LOADER class

#include "base.h"
#include <array>
#include <vector>

class loader
{
	public:
		loader();
		virtual ~loader(void);
		loader(const loader&) = delete;
		loader& operator=(const loader&) = delete;
		loader(loader&&) = delete;
		loader& operator=(loader&&) = delete;
		walker  *create_walker(char order, char family, screen  *screenp, bool cache_weapons = true);
		void set_derived_stats(walker* w, char order, char family);
		pixieN *create_pixieN(char order, char family);
		walker *set_walker(walker *ob, char order, char family);
		std::vector<PixieData> graphics;
		std::vector<signed char**> animations;
		std::vector<float> stepsizes;
		std::vector<Sint32> lineofsight;

		std::array<float, 200> hitpoints{}; // hack for now
		std::vector<char> act_types;
		std::vector<float> damage;
		std::vector<float> fire_frequency;
};

