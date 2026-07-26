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

static const char* const soldier_names[] = {"Lothar", "Arthur", "Uther", "Achilles", "Lu Bu", "Wallace", "Leonidas", "Attila", "Alexander", "Ajax", "Nestor", "Priam", "Hector", "Tom", "Bigfoot"};

const FamilyDescriptor& describe_family_soldier()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SOLDIER,
        .name = "SOLDIER",
        .short_name = nullptr,
        .base_stats = {12, 6, 12, 8, 9, 1},
        .hiring_cost = 250,
        .derived_bonuses = {BASE_GUY_HP+90, 0, 20, 0, 0, 0, 4, 6},
        .stat_costs = {6, 10, 6, 25, 50, 200},
        .special_cost = {5000, 25, 100, 120, 150, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = true,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SOLDIER SLAIN",
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
        .pix_filename = "footman.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 7,
        .description = "Your basic grunt, can     \n"
                       "absorb and deal damage and\n"
                       "move moderately fast. A   \n"
                       "good all-around fighter. A\n"
                       "soldier's normal weapon is\n"
                       "a magical returning blade.\n"
                       "\n"
                       "Special: Charge",
        .name_pool = soldier_names,
        .name_pool_size = sizeof(soldier_names) / sizeof(soldier_names[0]),
        .is_playable = true,
        .playable_order = 0,
    };
    return desc;
}
