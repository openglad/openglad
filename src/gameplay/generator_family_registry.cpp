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

// Only free (pack-claimable) slots ever keep these: populate() assigns the
// four core generators whole-struct. A name is what makes a slot resolvable
// by string id, so a pack generator that declares no name still gets one.
static void apply_defaults(GeneratorFamilyDescriptor& d)
{
    d.name = "GENERATOR";
}

void init_generator_family_registry()
{
    s_registry.init(apply_defaults, populate);
}

const GeneratorFamilyDescriptor* get_generator_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_generator_family_registry();
    return s_registry.get(family_id);
}

bool set_generator_family_descriptor(int family_id,
                                     const GeneratorFamilyDescriptor& d)
{
    if (!s_registry.is_initialized())
        init_generator_family_registry();
    return s_registry.set(family_id, d);
}

const GeneratorFamilyDescriptor* get_generator_family_descriptor_install_slot(
    int family_id)
{
    if (!s_registry.is_initialized())
        init_generator_family_registry();
    return s_registry.install_slot(family_id);
}

void reset_generator_family_registry_mod_slots()
{
    if (!s_registry.is_initialized())
        init_generator_family_registry();
    s_registry.reset_mod_slots();
}
