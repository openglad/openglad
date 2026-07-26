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

// Returns the descriptor for the given living family ID: the core pins
// (0..NUM_FAMILIES-1) plus any slot a class pack has installed (ids up to
// NUM_FAMILY_SLOTS-1). Returns nullptr for out-of-range IDs and for slots no
// pack has claimed — "nullptr means this family does not exist".
const FamilyDescriptor* get_family_descriptor(int family_id);

// Initialize the family registry. Call once at startup before any lookups.
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
// "free"; the core pins are untouched. Runs before a fresh install pass so
// an unmounted pack leaves no family behind.
void reset_family_registry_mod_slots();
