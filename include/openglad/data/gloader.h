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

#include <openglad/data/level_data.h> // Order forward-decl
#include <openglad/data/pixie_data.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class walker;
class screen;

class loader
{
	public:
		loader();
		virtual ~loader(void);
		loader(const loader&) = delete;
		loader& operator=(const loader&) = delete;
		loader(loader&&) = delete;
		loader& operator=(loader&&) = delete;
		[[nodiscard]] std::unique_ptr<walker> create_walker_owned(Order order, std::int32_t family, screen* screenp = nullptr, bool cache_weapons = true);
		[[nodiscard]] std::unique_ptr<walker> create_walker_headless(Order order, std::int32_t family);
		void set_derived_stats(walker* w, Order order, std::int32_t family);
		walker *set_walker(walker *ob, Order order, std::int32_t family);
		std::vector<PixieData> graphics;
		std::vector<signed char**> animations;
		std::vector<float> stepsizes;
		std::vector<std::int32_t> lineofsight;

		std::array<float, 200> hitpoints{}; // hack for now
		std::vector<char> act_types;
		std::vector<float> damage;
		std::vector<float> fire_frequency;
};
