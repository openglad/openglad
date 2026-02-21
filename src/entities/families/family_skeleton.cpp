/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/stats.h>

static bool skeleton_handle_teleport(walker* self)
{
    self->ani_type = ANI_TELE_IN;
    self->cycle = 0;
    self->teleport_ranged(self->stats()->level * 18);
    return true;
}

#define BASE_GUY_HP 30

static bool skeleton_check_special_ai(living* self)
{
    std::int32_t howmany = 0;
    self->sim_level->find_foes_in_range(self->sim_level->oblist,
                                 5 * GRID_SIZE, &howmany, self);
    if (howmany < 1)
        return true;
    return false;
}

static void skeleton_level_up(guy* self, std::int32_t level_diff)
{
    std::int32_t s = 8 * level_diff;
    std::int32_t d = 6 * level_diff;
    std::int32_t c = 8 * level_diff;
    std::int32_t it = 8 * level_diff;
    std::int32_t a = 1 * level_diff;
    d *= 2;
    c /= 2;
    it /= 2;
    self->strength = static_cast<short>(static_cast<std::int32_t>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<std::int32_t>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<std::int32_t>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<std::int32_t>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<std::int32_t>(self->armor) + a);
}

static bool skeleton_do_special(walker* self)
{
    if (self->ani_type == ANI_TELE_OUT || self->ani_type == ANI_TELE_IN)
        return false;
    self->ani_type = ANI_TELE_OUT;
    self->cycle = 0;
    return true;
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
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = true,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SKELETON CRUMBLED",
        .do_special = skeleton_do_special,
        .check_special_ai = skeleton_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = skeleton_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = skeleton_handle_teleport,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
    };
    return desc;
}
