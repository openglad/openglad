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

namespace og::resources {
// Monotonic counter naming the current set of sprite byte sources: mounted
// campaign packages plus the extra_pix sprite-sheet mount. Bumped by every
// campaign mount/unmount and by sprite-sheet (re)mounts — but NOT by an
// editor-save remount (remount_campaign_package_with_error), which restores
// the identical source set and must not mark live loaders stale. A loader
// records the value it last rebuilt at; reload_graphics_if_stale() compares.
unsigned sprite_source_generation();
void note_sprite_source_changed();
} // namespace og::resources

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

		// Reloads iff the sprite sources changed (campaign mount/unmount or
		// sprite-sheet remount) since this loader last rebuilt its graphics.
		// Carries reload_graphics()'s live-render contract when it fires;
		// answers false as a cheap no-op when the loader is current, so
		// same-campaign level advances cost one integer compare.
		bool reload_graphics_if_stale();

		// Brings the BLOOD/STAIN sprite pair in line with the current
		// effects/gore setting by swapping the active and held-back variants.
		// Unlike reload_graphics() this frees nothing: both variants stay
		// allocated for the loader's lifetime, so a pixieN that already cached
		// its facings pointer keeps pointing at valid pixels. Safe to call from
		// a menu handler with live render components and buttons on screen.
		void sync_gore_graphics();
	private:
		// TESTING tripwire: TRACEs when a walker is created while this
		// loader is stale (a campaign-switch flow missed its
		// reload_graphics_if_stale() call). No-op in production builds.
		void trace_if_sprites_stale() const;

		EntityFactory entity_factory_;
		// The BLOOD/STAIN variants that effects/gore is NOT currently using,
		// parked here so the toggle is a swap rather than a reload.
		PixieData gore_alt_blood_;
		PixieData gore_alt_stain_;
		bool gore_graphics_gory_ = true;
		// The og::resources::sprite_source_generation() this loader's
		// graphics were last rebuilt at (stamped by reload_graphics()).
		unsigned built_generation_ = 0;
};

// Link-time dispatch: SDL build provides the real instance; headless build provides nothing.
loader* sdl_entity_loader();
// The headless twin (platform_headless.cpp): openglad_text, openglad_server,
// openglad_curses and the mapgen tools resolve this one instead.
loader* headless_entity_loader();
