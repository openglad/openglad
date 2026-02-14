/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/legacy/base.h>

#define BASE_GUY_HP 30

const FamilyDescriptor& describe_family_druid()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_DRUID,
        .name = "DRUID",
        .base_stats = {7, 8, 14, 12, 7, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+80, 0, 10, 0, 0, 0, 3, 9},
        .stat_costs = {15, 15, 7, 6, 50, 200},
        .special_cost = {5000, 15, 80, 150, 200, 5000},
        .weapon_cost = 4,
        .default_weapon = FAMILY_LIGHTNING,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "GROW TREE", "SUMMON FAERIE", "REVEAL", "PROTECTION", "NONE"},
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
