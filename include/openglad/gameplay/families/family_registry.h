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

struct FamilyDescriptor;

// Returns the descriptor for the given living family ID. EVERY slot starts
// free: the core ids (0..NUM_FAMILIES-1) are declared by packs/core, the
// rest by whatever mod packs are mounted, and both arrive through
// install_classpacks(). Returns nullptr for out-of-range IDs and for slots
// no pack has claimed — "nullptr means this family does not exist".
const FamilyDescriptor* get_family_descriptor(int family_id);

// Lays down the registry's per-slot defaults. Call once at startup; it no
// longer installs any family (design doc §9a stage B), so a lookup before
// the class packs are mounted answers nullptr.
void init_family_registry();

// Overwrites one living-family slot (classpack install: copy the current
// descriptor, patch data fields, preserve callbacks, set it back) and marks
// the slot populated, so a pack family above the core pins becomes visible
// to get_family_descriptor. Entry addresses are stable across set. Returns
// false when family_id is outside the registry's capacity. Initializes the
// registry if needed.
bool set_family_descriptor(int family_id, const FamilyDescriptor& d);

// The slot a classpack install starts from: the live descriptor when the
// slot is populated, the defaults-initialised blank when the slot is a free
// mod slot, nullptr only when family_id is outside the registry capacity.
// Only the installer wants this — everything else asks
// get_family_descriptor, which hides free slots.
const FamilyDescriptor* get_family_descriptor_install_slot(int family_id);

// Drops every pack-installed living family (ids >= NUM_FAMILIES) back to
// "free"; the core span is untouched (it is re-installed from the core pack
// moments later). Runs before a fresh install pass so an unmounted pack
// leaves no family behind.
void reset_family_registry_mod_slots();

// The lowest core living id (< NUM_FAMILIES) no mounted pack has declared,
// or -1 when the core span is complete. Feeds the startup check below.
int first_unpopulated_core_family_slot();

// Throws std::runtime_error unless every core slot of all five registries
// has been installed by a mounted class pack. `context` names the caller
// ("io_init") and appears in the message.
//
// The core pack is a hard runtime dependency, exactly like the user data
// path and the default campaign that io_init already refuses to start
// without: without packs/core the game has no families at all, and the
// failure modes downstream (a null descriptor per lookup) are far harder to
// read than one message at startup.
void require_core_families_installed(const char* context);
