/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/effect_family_descriptor.h>
#include <openglad/entities/effect_family_registry.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/base.h>

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

static bool s_registry_initialized = false;
static EffectFamilyDescriptor s_registry[NUM_EFFECT_FAMILIES];

void init_effect_family_registry()
{
    if (s_registry_initialized)
        return;

    // Default all fields to safe values
    for (int i = 0; i < NUM_EFFECT_FAMILIES; i++)
    {
        auto& d = s_registry[i];
        d.family_id = i;
        d.name = "EFFECT";
        d.loops_animation = false;
        d.init_bit_flags = 0;
        d.on_act = nullptr;
        d.on_death = nullptr;
    }

    // === Data-only families (registered inline) ===

    // FAMILY_EXPAND (0)
    s_registry[FAMILY_EXPAND].name = "EXPAND";

    // FAMILY_FLASH (4)
    s_registry[FAMILY_FLASH].name = "FLASH";

    // FAMILY_MARKER (9)
    s_registry[FAMILY_MARKER].name = "MARKER";
    s_registry[FAMILY_MARKER].loops_animation = true;

    // FAMILY_HIT (12)
    s_registry[FAMILY_HIT].name = "HIT";

    // === Families with callbacks ===

    s_registry[FAMILY_GHOST_SCARE] = describe_effect_ghost_scare();
    s_registry[FAMILY_MAGIC_SHIELD] = describe_effect_magic_shield();
    s_registry[FAMILY_BOOMERANG] = describe_effect_boomerang();
    s_registry[FAMILY_KNIFE_BACK] = describe_effect_knife_back();
    s_registry[FAMILY_CLOUD] = describe_effect_cloud();
    s_registry[FAMILY_CHAIN] = describe_effect_chain();
    s_registry[FAMILY_DOOR_OPEN] = describe_effect_door_open();
    s_registry[FAMILY_BOMB] = describe_effect_bomb();
    s_registry[FAMILY_EXPLOSION] = describe_effect_explosion();

    s_registry_initialized = true;
}

const EffectFamilyDescriptor* get_effect_family_descriptor(int family_id)
{
    if (family_id < 0 || family_id >= NUM_EFFECT_FAMILIES)
        return nullptr;

    if (!s_registry_initialized)
        init_effect_family_registry();

    return &s_registry[family_id];
}
