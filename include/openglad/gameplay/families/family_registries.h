/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/family_registry.h>

// Forward declarations
struct WeaponFamilyDescriptor;
struct EffectFamilyDescriptor;
struct TreasureFamilyDescriptor;
struct GeneratorFamilyDescriptor;

// Weapon family registry
const WeaponFamilyDescriptor* get_weapon_family_descriptor(int family_id);
void init_weapon_family_registry();

// Effect family registry
const EffectFamilyDescriptor* get_effect_family_descriptor(int family_id);
void init_effect_family_registry();

// Treasure family registry
const TreasureFamilyDescriptor* get_treasure_family_descriptor(int family_id);
void init_treasure_family_registry();

// Generator family registry
const GeneratorFamilyDescriptor* get_generator_family_descriptor(int family_id);
void init_generator_family_registry();

// Slot overwrite for classpack install (same contract as
// set_family_descriptor in family_registry.h: bounds-checked, stable
// entry addresses, initializes the registry if needed).
bool set_weapon_family_descriptor(int family_id,
                                  const WeaponFamilyDescriptor& d);
bool set_effect_family_descriptor(int family_id,
                                  const EffectFamilyDescriptor& d);
bool set_treasure_family_descriptor(int family_id,
                                    const TreasureFamilyDescriptor& d);
bool set_generator_family_descriptor(int family_id,
                                     const GeneratorFamilyDescriptor& d);

// Initialize all five family registries. Call once at startup before any lookups.
inline void init_all_registries()
{
    init_family_registry();
    init_weapon_family_registry();
    init_effect_family_registry();
    init_treasure_family_registry();
    init_generator_family_registry();
}
