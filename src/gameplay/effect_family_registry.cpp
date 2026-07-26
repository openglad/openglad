/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#include "family_registry_base.h"

static constexpr int NUM_EFFECT_FAMILIES = 13;

// Forward declarations of per-family descriptor providers
const EffectFamilyDescriptor& describe_effect_ghost_scare();
const EffectFamilyDescriptor& describe_effect_magic_shield();
const EffectFamilyDescriptor& describe_effect_boomerang();
const EffectFamilyDescriptor& describe_effect_knife_back();
const EffectFamilyDescriptor& describe_effect_cloud();
const EffectFamilyDescriptor& describe_effect_chain();
const EffectFamilyDescriptor& describe_effect_door_open();
const EffectFamilyDescriptor& describe_effect_bomb();
const EffectFamilyDescriptor& describe_effect_explosion();

static FamilyRegistryBase<EffectFamilyDescriptor, NUM_EFFECT_FAMILIES> s_registry;

static void apply_defaults(EffectFamilyDescriptor& d)
{
    d.name = "EFFECT";
}

static void populate(EffectFamilyDescriptor* e)
{
    // Data-only families
    e[FAMILY_EXPAND].name = "EXPAND";

    e[FAMILY_FLASH].name = "FLASH";

    e[FAMILY_MARKER].name = "MARKER";
    e[FAMILY_MARKER].loops_animation = true;

    e[FAMILY_HIT].name = "HIT";

    // Families with callbacks
    e[FAMILY_GHOST_SCARE] = describe_effect_ghost_scare();
    e[FAMILY_MAGIC_SHIELD] = describe_effect_magic_shield();
    e[FAMILY_BOOMERANG] = describe_effect_boomerang();
    e[FAMILY_KNIFE_BACK] = describe_effect_knife_back();
    e[FAMILY_CLOUD] = describe_effect_cloud();
    e[FAMILY_CHAIN] = describe_effect_chain();
    e[FAMILY_DOOR_OPEN] = describe_effect_door_open();
    e[FAMILY_BOMB] = describe_effect_bomb();
    e[FAMILY_EXPLOSION] = describe_effect_explosion();
}

void init_effect_family_registry()
{
    s_registry.init(apply_defaults, populate);
}

const EffectFamilyDescriptor* get_effect_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_effect_family_registry();
    return s_registry.get(family_id);
}

bool set_effect_family_descriptor(int family_id,
                                  const EffectFamilyDescriptor& d)
{
    if (!s_registry.is_initialized())
        init_effect_family_registry();
    return s_registry.set(family_id, d);
}

const EffectFamilyDescriptor* get_effect_family_descriptor_install_slot(
    int family_id)
{
    if (!s_registry.is_initialized())
        init_effect_family_registry();
    return s_registry.install_slot(family_id);
}

void reset_effect_family_registry_mod_slots()
{
    if (!s_registry.is_initialized())
        init_effect_family_registry();
    s_registry.reset_mod_slots();
}
