/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

struct EffectFamilyDescriptor;

// Returns the descriptor for the given effect family ID (0..NUM_EFFECT_FAMILIES-1).
// Returns nullptr for out-of-range IDs.
const EffectFamilyDescriptor* get_effect_family_descriptor(int family_id);

// Initialize the effect family registry. Call once at startup before any lookups.
void init_effect_family_registry();
