/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#define BASE_GUY_HP 30

static void big_orc_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {12, 3, 12, 4, 1});
}

static const char* const orc_names[] = {"Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog", "Krosh"};

const FamilyDescriptor& describe_family_big_orc()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_BIG_ORC,
        .name = "ORC CAPTAIN",
        .short_name = "ORC CAP.",
        .base_stats = {18, 8, 16, 5, 11, 1},
        .hiring_cost = 1000,
        .derived_bonuses = {BASE_GUY_HP+150, 0, 28, 0, 0, 0, 3, 6},
        .stat_costs = {6, 15, 5, 40, 50, 200},
        .special_cost = {5000, 5000, 5000, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_KNIFE,
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
        .set_difficulty = nullptr,
        .level_up = big_orc_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "orc2.pix",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 25,
        .description = "Orcs captains are stronger\n"
                       "and smarter than the basic\n"
                       "orc.  They throw blades   \n"
                       "across the battlefield to \n"
                       "deal damage from afar.",
        .name_pool = orc_names,
        .name_pool_size = sizeof(orc_names) / sizeof(orc_names[0]),
        .is_playable = false,
        .playable_order = 999,
    };
    return desc;
}
