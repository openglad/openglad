/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/entities/weapon_family_registry.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include "SDL_stdinc.h"
#include <openglad/legacy/soundob.h>

static constexpr int NUM_WEAPON_FAMILIES = 20;

// Forward declarations of per-family descriptor providers
const WeaponFamilyDescriptor& describe_weapon_knife();
const WeaponFamilyDescriptor& describe_weapon_rock();
const WeaponFamilyDescriptor& describe_weapon_projectiles_fire_arrow();
const WeaponFamilyDescriptor& describe_weapon_projectiles_boulder();
const WeaponFamilyDescriptor& describe_weapon_wave();
const WeaponFamilyDescriptor& describe_weapon_wave2();
const WeaponFamilyDescriptor& describe_weapon_door();
const WeaponFamilyDescriptor& describe_weapon_animate_tree();
const WeaponFamilyDescriptor& describe_weapon_animate_blood();
const WeaponFamilyDescriptor& describe_weapon_animate_circle_protection();
const WeaponFamilyDescriptor& describe_weapon_animate_glow();
const WeaponFamilyDescriptor& describe_weapon_animate_sprinkle();

static bool s_registry_initialized = false;
static WeaponFamilyDescriptor s_registry[NUM_WEAPON_FAMILIES];

void init_weapon_family_registry()
{
    if (s_registry_initialized)
        return;

    // Default all fields to safe values
    for (int i = 0; i < NUM_WEAPON_FAMILIES; i++)
    {
        auto& d = s_registry[i];
        d.family_id = i;
        d.name = "WEAPON";
        d.fire_sound = SOUND_FWIP;
        d.skip_sit_notify = false;
        d.is_auto_attackable = false;
        d.init_bit_flags = 0;
        d.init_lifetime = 0;
        d.init_ani_type = 0;
        d.on_death = nullptr;
        d.on_animate = nullptr;
        d.on_hit_target = nullptr;
    }

    // === Data-only families (registered inline) ===

    // FAMILY_ARROW (2)
    s_registry[FAMILY_ARROW].name = "ARROW";
    s_registry[FAMILY_ARROW].fire_sound = SOUND_BOW;

    // FAMILY_FIREBALL (3)
    s_registry[FAMILY_FIREBALL].name = "FIREBALL";
    s_registry[FAMILY_FIREBALL].fire_sound = SOUND_BLAST;
    s_registry[FAMILY_FIREBALL].init_bit_flags = BIT_MAGICAL;

    // FAMILY_METEOR (5)
    s_registry[FAMILY_METEOR].name = "METEOR";
    s_registry[FAMILY_METEOR].fire_sound = SOUND_BLAST;
    s_registry[FAMILY_METEOR].init_bit_flags = BIT_MAGICAL;

    // FAMILY_BONE (7)
    s_registry[FAMILY_BONE].name = "BONE";

    // FAMILY_BLOB (9)
    s_registry[FAMILY_BLOB].name = "BLOB";

    // FAMILY_LIGHTNING (11)
    s_registry[FAMILY_LIGHTNING].name = "LIGHTNING";
    s_registry[FAMILY_LIGHTNING].fire_sound = SOUND_BOLT;

    // FAMILY_WAVE3 (15)
    s_registry[FAMILY_WAVE3].name = "WAVE3";
    s_registry[FAMILY_WAVE3].init_bit_flags = BIT_IMMORTAL | BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING | BIT_MAGICAL;

    // FAMILY_HAMMER (17)
    s_registry[FAMILY_HAMMER].name = "HAMMER";

    // === Families with callbacks ===

    s_registry[FAMILY_KNIFE] = describe_weapon_knife();
    s_registry[FAMILY_ROCK] = describe_weapon_rock();
    s_registry[FAMILY_FIRE_ARROW] = describe_weapon_projectiles_fire_arrow();
    s_registry[FAMILY_BOULDER] = describe_weapon_projectiles_boulder();
    s_registry[FAMILY_WAVE] = describe_weapon_wave();
    s_registry[FAMILY_WAVE2] = describe_weapon_wave2();
    s_registry[FAMILY_DOOR] = describe_weapon_door();
    s_registry[FAMILY_TREE] = describe_weapon_animate_tree();
    s_registry[FAMILY_BLOOD] = describe_weapon_animate_blood();
    s_registry[FAMILY_CIRCLE_PROTECTION] = describe_weapon_animate_circle_protection();
    s_registry[FAMILY_GLOW] = describe_weapon_animate_glow();
    s_registry[FAMILY_SPRINKLE] = describe_weapon_animate_sprinkle();

    s_registry_initialized = true;
}

const WeaponFamilyDescriptor* get_weapon_family_descriptor(int family_id)
{
    if (family_id < 0 || family_id >= NUM_WEAPON_FAMILIES)
        return nullptr;

    if (!s_registry_initialized)
        init_weapon_family_registry();

    return &s_registry[family_id];
}
