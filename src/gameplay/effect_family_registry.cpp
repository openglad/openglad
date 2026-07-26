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

static FamilyRegistryBase<EffectFamilyDescriptor, NUM_EFFECT_FAMILIES> s_registry;

static void apply_defaults(EffectFamilyDescriptor& d)
{
    d.name = "EFFECT";
}

void init_effect_family_registry()
{
    s_registry.init(apply_defaults);
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

int first_unpopulated_core_effect_family_slot()
{
    if (!s_registry.is_initialized())
        init_effect_family_registry();
    return s_registry.first_unpopulated_core_slot();
}
