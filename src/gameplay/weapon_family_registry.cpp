/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>

#include "family_registry_base.h"

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

static FamilyRegistryBase<WeaponFamilyDescriptor, NUM_WEAPON_FAMILIES> s_registry;

static void apply_defaults(WeaponFamilyDescriptor& d)
{
    d.name = "WEAPON";
    d.fire_sound = SOUND_FWIP;
}

static void populate(WeaponFamilyDescriptor* e)
{
    // Data-only families
    e[FAMILY_ARROW].name = "ARROW";
    e[FAMILY_ARROW].fire_sound = SOUND_BOW;

    e[FAMILY_FIREBALL].name = "FIREBALL";
    e[FAMILY_FIREBALL].fire_sound = SOUND_BLAST;
    e[FAMILY_FIREBALL].init_bit_flags = BIT_MAGICAL;

    e[FAMILY_METEOR].name = "METEOR";
    e[FAMILY_METEOR].fire_sound = SOUND_BLAST;
    e[FAMILY_METEOR].init_bit_flags = BIT_MAGICAL;

    e[FAMILY_BONE].name = "BONE";

    e[FAMILY_BLOB].name = "BLOB";

    e[FAMILY_LIGHTNING].name = "LIGHTNING";
    e[FAMILY_LIGHTNING].fire_sound = SOUND_BOLT;

    e[FAMILY_WAVE3].name = "WAVE3";
    e[FAMILY_WAVE3].init_bit_flags = BIT_IMMORTAL | BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING | BIT_MAGICAL;

    e[FAMILY_HAMMER].name = "HAMMER";

    // Families with callbacks
    e[FAMILY_KNIFE] = describe_weapon_knife();
    e[FAMILY_ROCK] = describe_weapon_rock();
    e[FAMILY_FIRE_ARROW] = describe_weapon_projectiles_fire_arrow();
    e[FAMILY_BOULDER] = describe_weapon_projectiles_boulder();
    e[FAMILY_WAVE] = describe_weapon_wave();
    e[FAMILY_WAVE2] = describe_weapon_wave2();
    e[FAMILY_DOOR] = describe_weapon_door();
    e[FAMILY_TREE] = describe_weapon_animate_tree();
    e[FAMILY_BLOOD] = describe_weapon_animate_blood();
    e[FAMILY_CIRCLE_PROTECTION] = describe_weapon_animate_circle_protection();
    e[FAMILY_GLOW] = describe_weapon_animate_glow();
    e[FAMILY_SPRINKLE] = describe_weapon_animate_sprinkle();
}

void init_weapon_family_registry()
{
    s_registry.init(apply_defaults, populate);
}

const WeaponFamilyDescriptor* get_weapon_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_weapon_family_registry();
    return s_registry.get(family_id);
}
