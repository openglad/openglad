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

inline constexpr int BASE_GUY_HP = 30;

static const char* const archer_names[] = {"Robin", "Green Arrow", "Legolas", "Yeoman", "Strider", "Longshot", "Bowyer", "Hunter", "Archy"};

const FamilyDescriptor& describe_family_archer()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ARCHER,
        .name = "ARCHER",
        .short_name = nullptr,
        .base_stats = {6, 12, 6, 10, 5, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+60, 0, 8, 0, 0, 0, 4, 5},
        .stat_costs = {15, 6, 9, 10, 50, 200},
        .special_cost = {5000, 20, 60, 70, 5000, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_ARROW,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "FIRE ARROWS", "BARRAGE", "EXPLODING BOLT", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "ARCHER DIED",
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
        .pix_filename = "archer.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 12,
        .description = "Archers are fleet of foot,\n"
                       "and their arrows have a   \n"
                       "long range. Although      \n"
                       "they're not as strong as  \n"
                       "other fighters, they can  \n"
                       "be a good squad backbone. \n"
                       "\n"
                       "Special: Fire Arrows",
        .name_pool = archer_names,
        .name_pool_size = std::size(archer_names),
        .is_playable = true,
        .playable_order = 3,
    };
    return desc;
}
