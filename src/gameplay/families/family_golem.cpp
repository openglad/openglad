/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#define BASE_GUY_HP 30

static void golem_set_difficulty(living* self, std::uint32_t level)
{
    apply_difficulty_scaling(self, level, {18.0f, 5.0f, 7.0f, 4.0f});
}

const FamilyDescriptor& describe_family_golem()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_GOLEM,
        .name = "BEAST",
        .short_name = nullptr,
        .base_stats = {12, 6, 12, 8, 6, 1},
        .hiring_cost = 0,
        .derived_bonuses = {BASE_GUY_HP+270, 0, 60, 0, 0, 0, 8, 9},
        .stat_costs = {0, 0, 0, 0, 0, 0},
        .special_cost = {5000, 5000, 5000, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_BOULDER,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SOMEONE DIED",
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = golem_set_difficulty,
        .level_up = nullptr,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "golem1.pix",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 20,
        .description = nullptr,
        .name_pool = nullptr,
        .name_pool_size = 0,
        .is_playable = false,
        .playable_order = 999,
    };
    return desc;
}
