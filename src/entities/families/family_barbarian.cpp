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

const FamilyDescriptor& describe_family_barbarian()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_BARBARIAN,
        .name = "BARBARIAN",
        .base_stats = {14, 5, 14, 8, 8, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 25, 0, 0, 0, 3, 5.5f},
        .stat_costs = {5, 35, 5, 35, 50, 200},
        .special_cost = {5000, 20, 30, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_HAMMER,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HURL BOULDER", "EXPLODING BOULDER", "NONE", "NONE", "NONE"},
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
