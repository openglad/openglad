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

const FamilyDescriptor& describe_family_mage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MAGE,
        .name = "MAGE",
        .base_stats = {4, 6, 4, 16, 5, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+60, 0, 4, 0, 0, 0, 2, 4},
        .stat_costs = {20, 15, 16, 6, 50, 200},
        .special_cost = {5000, 15, 60, 500, 70, 100},
        .weapon_cost = 5,
        .default_weapon = FAMILY_FIREBALL,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TELEPORT", "WARP SPACE", "FREEZE TIME", "ENERGY WAVE", "HEARTBURST"},
        .alternate_names = {"NONE", "TELEPORT MARKER", "NONE", "NONE", "NONE", "NONE"},
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
