/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

struct WeaponFamilyDescriptor;

// Returns the descriptor for the given weapon family ID (0..NUM_WEAPON_FAMILIES-1).
// Returns nullptr for out-of-range IDs.
const WeaponFamilyDescriptor* get_weapon_family_descriptor(int family_id);

// Initialize the weapon family registry. Call once at startup before any lookups.
void init_weapon_family_registry();
