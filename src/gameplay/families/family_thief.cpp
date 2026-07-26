/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/core/constants.h>

inline constexpr int BASE_GUY_HP = 30;

static const char* const thief_names[] = {"Shinobi", "Dismas", "Shadow", "Stabby", "Swiftstrike", "Scourge", "Rogue"};

const FamilyDescriptor& describe_family_thief()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_THIEF,
        .name = "THIEF",
        .short_name = nullptr,
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
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "THIEF KILLED",
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
        .pix_filename = "thief.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 10,
        .description = "Thieves are fast, though  \n"
                       "not so potent as the      \n"
                       "soldier. Thieves can throw\n"
                       "small blades rapidly and  \n"
                       "damage whole groups of    \n"
                       "enemies with their bombs. \n"
                       "\n"
                       "Special: Drop Bomb",
        .name_pool = thief_names,
        .name_pool_size = sizeof(thief_names) / sizeof(thief_names[0]),
        .is_playable = true,
        .playable_order = 7,
    };
    return desc;
}
