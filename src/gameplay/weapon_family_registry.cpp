/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>

#include "family_registry_base.h"

static constexpr int NUM_WEAPON_FAMILIES = 20;

static FamilyRegistryBase<WeaponFamilyDescriptor, NUM_WEAPON_FAMILIES> s_registry;

static void apply_defaults(WeaponFamilyDescriptor& d)
{
    d.name = "WEAPON";
    d.fire_sound = SOUND_FWIP;
}

void init_weapon_family_registry()
{
    s_registry.init(apply_defaults);
}

const WeaponFamilyDescriptor* get_weapon_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_weapon_family_registry();
    return s_registry.get(family_id);
}

bool set_weapon_family_descriptor(int family_id,
                                  const WeaponFamilyDescriptor& d)
{
    if (!s_registry.is_initialized())
        init_weapon_family_registry();
    return s_registry.set(family_id, d);
}

const WeaponFamilyDescriptor* get_weapon_family_descriptor_install_slot(
    int family_id)
{
    if (!s_registry.is_initialized())
        init_weapon_family_registry();
    return s_registry.install_slot(family_id);
}

void reset_weapon_family_registry_mod_slots()
{
    if (!s_registry.is_initialized())
        init_weapon_family_registry();
    s_registry.reset_mod_slots();
}

int first_unpopulated_core_weapon_family_slot()
{
    if (!s_registry.is_initialized())
        init_weapon_family_registry();
    return s_registry.first_unpopulated_core_slot();
}
