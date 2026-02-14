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

const FamilyDescriptor& describe_family_fire_elemental()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_FIREELEMENTAL,
        .name = "ELEMENTAL",
        .base_stats = {14, 10, 14, 14, 9, 1},
        .hiring_cost = 600,
        .derived_bonuses = {BASE_GUY_HP+70, 0, 28, 0, 0, 0, 4, 5},
        .stat_costs = {7, 10, 14, 12, 50, 200},
        .special_cost = {5000, 50, 5000, 5000, 5000, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_METEOR,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 150,
        .special_names = {"NONE", "STARBURST", "NONE", "NONE", "NONE", "NONE"},
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
