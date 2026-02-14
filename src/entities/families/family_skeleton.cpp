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

const FamilyDescriptor& describe_family_skeleton()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SKELETON,
        .name = "SKELETON",
        .base_stats = {9, 14, 9, 6, 6, 1},
        .hiring_cost = 300,
        .derived_bonuses = {BASE_GUY_HP+30, 0, 4, 0, 0, 0, 6, 4.5f},
        .stat_costs = {15, 6, 16, 25, 50, 200},
        .special_cost = {5000, 10, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BONE,
        .init_bit_flags = 0,
        .init_ani_type = ANI_SKEL_GROW,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TUNNEL", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = false,
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = nullptr,
        .on_death = nullptr,
    };
    return desc;
}
