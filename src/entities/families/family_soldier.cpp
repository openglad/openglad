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

const FamilyDescriptor& describe_family_soldier()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SOLDIER,
        .name = "SOLDIER",
        .base_stats = {12, 6, 12, 8, 9, 1},
        .hiring_cost = 250,
        .derived_bonuses = {BASE_GUY_HP+90, 0, 20, 0, 0, 0, 4, 6},
        .stat_costs = {6, 10, 6, 25, 50, 200},
        .special_cost = {5000, 25, 100, 120, 150, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM", "NONE"},
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
