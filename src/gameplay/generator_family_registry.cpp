/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>

#include "family_registry_base.h"

static constexpr int NUM_GENERATOR_FAMILIES = 4;

// Process-global registry by design: populated once at startup, then read-only.
static FamilyRegistryBase<GeneratorFamilyDescriptor, NUM_GENERATOR_FAMILIES> s_registry;

static void populate(GeneratorFamilyDescriptor* e)
{
    e[FAMILY_TENT] = {
        .family_id = FAMILY_TENT,
        .name = "TENT",
        .default_weapon = FAMILY_SKELETON,
        .has_lifetime = true,
        .spawn_ani_type = 0,
        .clear_owner = false,
    };

    e[FAMILY_TOWER] = {
        .family_id = FAMILY_TOWER,
        .name = "TOWER",
        .default_weapon = FAMILY_MAGE,
        .has_lifetime = false,
        .spawn_ani_type = ANI_TELE_IN,
        .clear_owner = true,
    };

    e[FAMILY_BONES] = {
        .family_id = FAMILY_BONES,
        .name = "BONES",
        .default_weapon = FAMILY_GHOST,
        .has_lifetime = true,
        .spawn_ani_type = 0,
        .clear_owner = false,
    };

    e[FAMILY_TREEHOUSE] = {
        .family_id = FAMILY_TREEHOUSE,
        .name = "TREEHOUSE",
        .default_weapon = FAMILY_ELF,
        .has_lifetime = false,
        .spawn_ani_type = 0,
        .clear_owner = true,
    };
}

void init_generator_family_registry()
{
    s_registry.init(nullptr, populate);
}

const GeneratorFamilyDescriptor* get_generator_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_generator_family_registry();
    return s_registry.get(family_id);
}
