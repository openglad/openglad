/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/generator_family_descriptor.h>
#include <openglad/entities/generator_family_registry.h>

static constexpr int NUM_GENERATOR_FAMILIES = 4;

static bool s_registry_initialized = false;
static GeneratorFamilyDescriptor s_registry[NUM_GENERATOR_FAMILIES];

void init_generator_family_registry()
{
    if (s_registry_initialized)
        return;

    // FAMILY_TENT (0) — skeleton tent
    s_registry[FAMILY_TENT] = {
        .family_id = FAMILY_TENT,
        .name = "TENT",
        .default_weapon = FAMILY_SKELETON,
        .has_lifetime = true,
        .spawn_ani_type = 0,
        .clear_owner = false,
    };

    // FAMILY_TOWER (1) — mage tower
    s_registry[FAMILY_TOWER] = {
        .family_id = FAMILY_TOWER,
        .name = "TOWER",
        .default_weapon = FAMILY_MAGE,
        .has_lifetime = false,
        .spawn_ani_type = ANI_TELE_IN,
        .clear_owner = true,
    };

    // FAMILY_BONES (2) — ghost bone pile
    s_registry[FAMILY_BONES] = {
        .family_id = FAMILY_BONES,
        .name = "BONES",
        .default_weapon = FAMILY_GHOST,
        .has_lifetime = true,
        .spawn_ani_type = 0,
        .clear_owner = false,
    };

    // FAMILY_TREEHOUSE (3) — elf tree-house
    s_registry[FAMILY_TREEHOUSE] = {
        .family_id = FAMILY_TREEHOUSE,
        .name = "TREEHOUSE",
        .default_weapon = FAMILY_ELF,
        .has_lifetime = false,
        .spawn_ani_type = 0,
        .clear_owner = true,
    };

    s_registry_initialized = true;
}

const GeneratorFamilyDescriptor* get_generator_family_descriptor(int family_id)
{
    if (family_id < 0 || family_id >= NUM_GENERATOR_FAMILIES)
        return nullptr;

    if (!s_registry_initialized)
        init_generator_family_registry();

    return &s_registry[family_id];
}
