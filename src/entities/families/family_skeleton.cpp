/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static bool skeleton_check_special_ai(living* self)
{
    Sint32 howmany = 0;
    myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                 5 * GRID_SIZE, &howmany, self);
    if (howmany < 1)
        return true;
    return false;
}

static void skeleton_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    d *= 2;
    c /= 2;
    it /= 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

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
        .check_special_ai = skeleton_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = skeleton_level_up,
        .on_death = nullptr,
    };
    return desc;
}
