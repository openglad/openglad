/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

const FamilyDescriptor& describe_family_orc()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ORC,
        .name = "ORC",
        .base_stats = {18, 8, 16, 5, 11, 1},
        .hiring_cost = 300,
        .derived_bonuses = {BASE_GUY_HP+110, 0, 23, 0, 0, 0, 3, 7},
        .stat_costs = {6, 15, 5, 40, 50, 200},
        .special_cost = {5000, 25, 20, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_ROCK,
        .init_bit_flags = BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HOWL", "EAT CORPSE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = nullptr,
        .on_death = nullptr,
    };
    return desc;
}
