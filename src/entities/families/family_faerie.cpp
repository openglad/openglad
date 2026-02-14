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

const FamilyDescriptor& describe_family_faerie()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_FAERIE,
        .name = "FAERIE",
        .base_stats = {3, 8, 3, 14, 2, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 5, 0, 0, 0, 4, 9},
        .stat_costs = {25, 6, 12, 8, 50, 200},
        .special_cost = {5000, 5000, 5000, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_SPRINKLE,
        .init_bit_flags = BIT_ANIMATE | BIT_FLYING,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
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
