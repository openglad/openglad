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

const FamilyDescriptor& describe_family_archer()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ARCHER,
        .name = "ARCHER",
        .base_stats = {6, 12, 6, 10, 5, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+60, 0, 8, 0, 0, 0, 4, 5},
        .stat_costs = {15, 6, 9, 10, 50, 200},
        .special_cost = {5000, 20, 60, 70, 5000, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_ARROW,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "FIRE ARROWS", "BARRAGE", "EXPLODING BOLT", "NONE", "NONE"},
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
