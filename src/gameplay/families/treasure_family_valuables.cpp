/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/core/constants.h>

const TreasureFamilyDescriptor& describe_treasure_gold_bar()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_GOLD_BAR,
        .name = "GOLD_BAR",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_silver_bar()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_SILVER_BAR,
        .name = "SILVER_BAR",
        .init_ignore = false,
        .init_frame = 1,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_life_gem()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_LIFE_GEM,
        .name = "LIFE_GEM",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = nullptr,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_key()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_KEY,
        .name = "KEY",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = nullptr,
    };
    return desc;
}
