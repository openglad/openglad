/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/core/constants.h>

const TreasureFamilyDescriptor& describe_treasure_drumstick()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_DRUMSTICK,
        .name = "DRUMSTICK",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_magic_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_MAGIC_POTION,
        .name = "MAGIC_POTION",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_flight_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_FLIGHT_POTION,
        .name = "FLIGHT_POTION",
        .init_ignore = false,
        .init_frame = 11,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_invulnerable_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_INVULNERABLE_POTION,
        .name = "INVULNERABLE_POTION",
        .init_ignore = false,
        .init_frame = 2,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_invis_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_INVIS_POTION,
        .name = "INVIS_POTION",
        .init_ignore = false,
        .init_frame = 1,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_speed_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_SPEED_POTION,
        .name = "SPEED_POTION",
        .init_ignore = false,
        .init_frame = 3,
        .on_eat = nullptr,
    };
    return desc;
}
