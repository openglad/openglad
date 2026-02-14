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

const FamilyDescriptor& describe_family_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SLIME,
        .name = "SLIME",
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 28, 0, 0, 0, 3, 11},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "SPLIT", "NONE", "NONE", "NONE", "NONE"},
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

const FamilyDescriptor& describe_family_small_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SMALL_SLIME,
        .name = "SLIME",
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+50, 0, 12, 0, 0, 0, 2, 12},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE | BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "GROW", "NONE", "NONE", "NONE", "NONE"},
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

const FamilyDescriptor& describe_family_medium_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MEDIUM_SLIME,
        .name = "SLIME",
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+80, 0, 20, 0, 0, 0, 2, 10},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "GROW", "NONE", "NONE", "NONE", "NONE"},
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
