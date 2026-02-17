/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/entities/treasure_family_descriptor.h>
#include <openglad/entities/treasure_family_registry.h>

static constexpr int NUM_TREASURE_FAMILIES = 13;

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

static bool s_registry_initialized = false;
static TreasureFamilyDescriptor s_registry[NUM_TREASURE_FAMILIES];

void init_treasure_family_registry()
{
    if (s_registry_initialized)
        return;

    // Default all fields to safe values
    for (int i = 0; i < NUM_TREASURE_FAMILIES; i++)
    {
        auto& d = s_registry[i];
        d.family_id = i;
        d.name = "TREASURE";
        d.init_ignore = false;
        d.init_frame = -1;
        d.on_eat = nullptr;
    }

    // === Data-only families (registered inline) ===

    // FAMILY_STAIN (0) — permanent bloodstains
    s_registry[FAMILY_STAIN].name = "STAIN";
    s_registry[FAMILY_STAIN].init_ignore = true;

    // === Families with callbacks ===

    s_registry[FAMILY_DRUMSTICK] = describe_treasure_drumstick();
    s_registry[FAMILY_GOLD_BAR] = describe_treasure_gold_bar();
    s_registry[FAMILY_SILVER_BAR] = describe_treasure_silver_bar();
    s_registry[FAMILY_MAGIC_POTION] = describe_treasure_magic_potion();
    s_registry[FAMILY_INVIS_POTION] = describe_treasure_invis_potion();
    s_registry[FAMILY_INVULNERABLE_POTION] = describe_treasure_invulnerable_potion();
    s_registry[FAMILY_FLIGHT_POTION] = describe_treasure_flight_potion();
    s_registry[FAMILY_SPEED_POTION] = describe_treasure_speed_potion();
    s_registry[FAMILY_EXIT] = describe_treasure_exit();
    s_registry[FAMILY_TELEPORTER] = describe_treasure_teleporter();
    s_registry[FAMILY_LIFE_GEM] = describe_treasure_life_gem();
    s_registry[FAMILY_KEY] = describe_treasure_key();

    s_registry_initialized = true;
}

const TreasureFamilyDescriptor* get_treasure_family_descriptor(int family_id)
{
    if (family_id < 0 || family_id >= NUM_TREASURE_FAMILIES)
        return nullptr;

    if (!s_registry_initialized)
        init_treasure_family_registry();

    return &s_registry[family_id];
}
