/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/core/constants.h>
#include <iterator>

#define BASE_GUY_HP 30

static const char* const cleric_names[] = {"Tuck", "Brother", "Pater", "Drake", "Friar", "Francis", "John Paul", "Medic"};

const FamilyDescriptor& describe_family_cleric()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_CLERIC,
        .name = "CLERIC",
        .short_name = nullptr,
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
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "CLERIC DIED",
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
        .pix_filename = "cleric.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 4,
        .description = "Clerics, like mages, are  \n"
                       "slow, but have a stronger \n"
                       "hand-to-hand attack.      \n"
                       "Clerics possess abilities \n"
                       "related to healing and    \n"
                       "interaction with the dead.\n"
                       "\n"
                       "Special: Heal",
        .name_pool = cleric_names,
        .name_pool_size = std::size(cleric_names),
        .is_playable = true,
        .playable_order = 5,
    };
    return desc;
}
