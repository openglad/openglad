/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>

#include "family_registry_base.h"

// Core span 0..12 (BLOOD..SPEED_POTION). Wire ids 13/14 were the retired
// core CTF flag/waypoint slots: they sit ABOVE the core span on purpose, so
// a campaign pack (the modes campaign) can claim them with its own
// declarations and they unmount with it (reset_mod_slots frees ids above
// this span). Registry capacity stays NUM_FAMILY_SLOTS.
static constexpr int NUM_TREASURE_FAMILIES = 13;

static FamilyRegistryBase<TreasureFamilyDescriptor, NUM_TREASURE_FAMILIES> s_registry;

static void apply_defaults(TreasureFamilyDescriptor& d)
{
    d.name = "TREASURE";
    d.init_frame = -1;
}

void init_treasure_family_registry()
{
    s_registry.init(apply_defaults);
}

const TreasureFamilyDescriptor* get_treasure_family_descriptor(int family_id)
{
    if (!s_registry.is_initialized())
        init_treasure_family_registry();
    return s_registry.get(family_id);
}

bool set_treasure_family_descriptor(int family_id,
                                    const TreasureFamilyDescriptor& d)
{
    if (!s_registry.is_initialized())
        init_treasure_family_registry();
    return s_registry.set(family_id, d);
}

const TreasureFamilyDescriptor* get_treasure_family_descriptor_install_slot(
    int family_id)
{
    if (!s_registry.is_initialized())
        init_treasure_family_registry();
    return s_registry.install_slot(family_id);
}

void reset_treasure_family_registry_mod_slots()
{
    if (!s_registry.is_initialized())
        init_treasure_family_registry();
    s_registry.reset_mod_slots();
}

int first_unpopulated_core_treasure_family_slot()
{
    if (!s_registry.is_initialized())
        init_treasure_family_registry();
    return s_registry.first_unpopulated_core_slot();
}
