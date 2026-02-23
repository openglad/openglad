/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/entities/family_registry.h>

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

// Initialize all five family registries. Call once at startup before any lookups.
inline void init_all_registries()
{
    init_family_registry();
    init_weapon_family_registry();
    init_effect_family_registry();
    init_treasure_family_registry();
    init_generator_family_registry();
}
