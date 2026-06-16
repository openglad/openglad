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

#include <openglad/resources/pixie_data.h>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward-declare Order enum class (defined in base.h)
enum class Order : unsigned char;

class walker;
class pixieN;
class screen;

struct EntityFactory
{
    std::function<void(walker&, const PixieData&)> attach_render;
    std::function<void(const std::string&)> report_error;
};

class loader
{
	public:
		explicit loader(EntityFactory entity_factory = {});
		virtual ~loader();
		loader(const loader&) = delete;
		loader& operator=(const loader&) = delete;
		loader(loader&&) = delete;
		loader& operator=(loader&&) = delete;
		[[nodiscard]] std::unique_ptr<walker> create_walker_owned(Order order, std::int32_t family);
		[[nodiscard]] std::unique_ptr<pixieN> create_pixieN_owned(Order order, std::int32_t family);
		[[nodiscard]] const PixieData* graphics_for(Order order, std::int32_t family) const;
		void set_derived_stats(walker* w, Order order, std::int32_t family);
		walker *set_walker(walker *ob, Order order, std::int32_t family);
		std::vector<PixieData> graphics;
		std::vector<const signed char * const *> animations;
		// Parallel to `animations`: number of (facing x ani_type) pointer entries
		// in each table, used to bound animation index math against short tables.
		std::vector<int> animation_counts;
		std::vector<float> stepsizes;
		std::vector<std::int32_t> lineofsight;

		std::vector<float> hitpoints; // sized to SIZE_ORDERS*SIZE_FAMILIES like its sibling tables
		std::vector<char> act_types;
		std::vector<float> damage;
		std::vector<float> fire_frequency;

		// Replaces loader-owned sprite buffers. Call only when no live render
		// component still points at graphics data from this loader.
		void reload_graphics();
	private:
		EntityFactory entity_factory_;
};

// Link-time dispatch: SDL build provides the real instance; headless build provides nothing.
loader* sdl_entity_loader();
