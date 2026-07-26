/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/core/constants.h>
#include <openglad/gameplay/statistics.h>

inline constexpr int BASE_GUY_HP = 30;

static const char* const slime_names[] = {"Grimer", "Goop", "Slurp", "Glopp", "Sludge", "Blob"};

const FamilyDescriptor& describe_family_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SLIME,
        .name = "SLIME",
        .short_name = nullptr,
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 28, 0, 0, 0, 3, 11},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "SPLIT", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 2.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SLIME DESTROYED",
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
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
        .pix_filename = "amoeba3.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_SLIME,
        .ai_line_of_sight = 4,
        .description = "Slimes are patches of ooze\n"
                       "which grow and split into \n"
                       "two smaller slimes, over- \n"
                       "whelming the enemy. Their \n"
                       "nebulous nature makes them\n"
                       "more susceptible to magic.\n"
                       "\n"
                       "Special: Grow",
        .name_pool = slime_names,
        .name_pool_size = sizeof(slime_names) / sizeof(slime_names[0]),
        .is_playable = false,
        .playable_order = 999,
    };
    return desc;
}

const FamilyDescriptor& describe_family_small_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SMALL_SLIME,
        .name = "SLIME",
        .short_name = nullptr,
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+50, 0, 12, 0, 0, 0, 2, 12},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE | BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "GROW", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 2.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SLIME DESTROYED",
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
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
        .pix_filename = "s_slime.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME,
        .ai_line_of_sight = 2,
        .description = "Slimes are patches of ooze\n"
                       "which grow and split into \n"
                       "two smaller slimes, over- \n"
                       "whelming the enemy. Their \n"
                       "nebulous nature makes them\n"
                       "more susceptible to magic.\n"
                       "\n"
                       "Special: Grow",
        .name_pool = slime_names,
        .name_pool_size = sizeof(slime_names) / sizeof(slime_names[0]),
        .is_playable = true,
        .playable_order = 12,
    };
    return desc;
}

const FamilyDescriptor& describe_family_medium_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MEDIUM_SLIME,
        .name = "SLIME",
        .short_name = nullptr,
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+80, 0, 20, 0, 0, 0, 2, 10},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "GROW", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 2.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SLIME DESTROYED",
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
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
        .pix_filename = "m_slime.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME,
        .ai_line_of_sight = 3,
        .description = "Slimes are patches of ooze\n"
                       "which grow and split into \n"
                       "two smaller slimes, over- \n"
                       "whelming the enemy. Their \n"
                       "nebulous nature makes them\n"
                       "more susceptible to magic.\n"
                       "\n"
                       "Special: Grow",
        .name_pool = slime_names,
        .name_pool_size = sizeof(slime_names) / sizeof(slime_names[0]),
        .is_playable = false,
        .playable_order = 999,
    };
    return desc;
}
