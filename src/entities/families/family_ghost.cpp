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

const FamilyDescriptor& describe_family_ghost()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_GHOST,
        .name = "GHOST",
        .base_stats = {6, 12, 18, 10, 15, 1},
        .hiring_cost = 600,
        .derived_bonuses = {BASE_GUY_HP+20, 0, 12, 0, 0, 0, 4, 7},
        .stat_costs = {16, 16, 16, 16, 45, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = BIT_ANIMATE | BIT_FLYING | BIT_ETHEREAL | BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "SCARE", "NONE", "NONE", "NONE", "NONE"},
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
