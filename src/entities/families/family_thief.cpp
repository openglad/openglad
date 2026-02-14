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

const FamilyDescriptor& describe_family_thief()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_THIEF,
        .name = "THIEF",
        .base_stats = {9, 12, 12, 10, 5, 1},
        .hiring_cost = 400,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 12, 0, 0, 0, 5, 5},
        .stat_costs = {15, 6, 9, 10, 50, 200},
        .special_cost = {5000, 35, 125, 100, 150, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "DROP BOMB", "CLOAK", "TAUNT ENEMY", "POISON CLOUD", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "CHARM OPPONENT", "NONE", "NONE"},
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
