/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>

#include "family_registry_base.h"

static constexpr int NUM_TREASURE_FAMILIES = 15;

// Forward declarations of per-family descriptor providers
const TreasureFamilyDescriptor& describe_treasure_drumstick();
const TreasureFamilyDescriptor& describe_treasure_gold_bar();
const TreasureFamilyDescriptor& describe_treasure_silver_bar();
const TreasureFamilyDescriptor& describe_treasure_magic_potion();
const TreasureFamilyDescriptor& describe_treasure_invis_potion();
const TreasureFamilyDescriptor& describe_treasure_invulnerable_potion();
const TreasureFamilyDescriptor& describe_treasure_flight_potion();
const TreasureFamilyDescriptor& describe_treasure_speed_potion();
const TreasureFamilyDescriptor& describe_treasure_exit();
const TreasureFamilyDescriptor& describe_treasure_teleporter();
const TreasureFamilyDescriptor& describe_treasure_life_gem();
const TreasureFamilyDescriptor& describe_treasure_key();
const TreasureFamilyDescriptor& describe_treasure_flag();
const TreasureFamilyDescriptor& describe_treasure_ctf_point();

static FamilyRegistryBase<TreasureFamilyDescriptor, NUM_TREASURE_FAMILIES> s_registry;

static void apply_defaults(TreasureFamilyDescriptor& d)
{
    d.name = "TREASURE";
    d.init_frame = -1;
}

static void populate(TreasureFamilyDescriptor* e)
{
    // Data-only families
    e[FAMILY_STAIN].name = "STAIN";
    e[FAMILY_STAIN].init_ignore = true;

    // Families with callbacks
    e[FAMILY_DRUMSTICK] = describe_treasure_drumstick();
    e[FAMILY_GOLD_BAR] = describe_treasure_gold_bar();
    e[FAMILY_SILVER_BAR] = describe_treasure_silver_bar();
    e[FAMILY_MAGIC_POTION] = describe_treasure_magic_potion();
    e[FAMILY_INVIS_POTION] = describe_treasure_invis_potion();
    e[FAMILY_INVULNERABLE_POTION] = describe_treasure_invulnerable_potion();
    e[FAMILY_FLIGHT_POTION] = describe_treasure_flight_potion();
    e[FAMILY_SPEED_POTION] = describe_treasure_speed_potion();
    e[FAMILY_EXIT] = describe_treasure_exit();
    e[FAMILY_TELEPORTER] = describe_treasure_teleporter();
    e[FAMILY_LIFE_GEM] = describe_treasure_life_gem();
    e[FAMILY_KEY] = describe_treasure_key();
    e[og::FAMILY_FLAG] = describe_treasure_flag();
    e[og::FAMILY_CTF_POINT] = describe_treasure_ctf_point();
}

void init_treasure_family_registry()
{
    s_registry.init(apply_defaults, populate);
}

const TreasureFamilyDescriptor* get_treasure_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_treasure_family_registry();
    return s_registry.get(family_id);
}
