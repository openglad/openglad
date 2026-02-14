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

const FamilyDescriptor& describe_family_cleric()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_CLERIC,
        .name = "CLERIC",
        .base_stats = {6, 7, 6, 14, 7, 1},
        .hiring_cost = 400,
        .derived_bonuses = {BASE_GUY_HP+90, 0, 12, 0, 0, 0, 2, 7.5f},
        .stat_costs = {15, 15, 9, 6, 50, 200},
        .special_cost = {5000, 2, 20, 50, 150, 5000},
        .weapon_cost = 8,
        .default_weapon = FAMILY_GLOW,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HEAL", "RAISE UNDEAD", "RAISE GHOST", "RESURRECT", "NONE"},
        .alternate_names = {"NONE", "MYSTIC MACE", "TURN UNDEAD", "TURN UNDEAD", "NONE", "NONE"},
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
