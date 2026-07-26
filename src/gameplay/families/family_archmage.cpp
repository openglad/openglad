/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/core/constants.h>

#define BASE_GUY_HP 30

static const char* const mage_names[] = {"Gandalf", "Saruman", "Radagast", "Alatar", "Pallando", "Raistlin", "Fizban", "Mordenkainen", "Merlin", "Harry", "Manannan", "Mordack", "Jace"};

const FamilyDescriptor& describe_family_archmage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ARCHMAGE,
        .name = "ARCHMAGE",
        .short_name = nullptr,
        .base_stats = {4, 6, 4, 16, 5, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 8, 0, 0, 0, 3, 1},
        .stat_costs = {30, 20, 25, 7, 55, 200},
        .special_cost = {5000, 10, 80, 500, 150, 5000},
        .weapon_cost = 12,
        .default_weapon = FAMILY_FIREBALL,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TELEPORT", "HEARTBURST", "SUMMON IMAGE", "MIND CONTROL", "NONE"},
        .alternate_names = {"NONE", "TELEPORT MARKER", "CHAIN LIGHTNING", "SUMMON ELEMENTAL", "NONE", "NONE"},
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
        .pix_filename = "archmage.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_MAGE,
        .ai_line_of_sight = 10,
        .description = "An Archmage takes the     \n"
                       "learnings of the Magi one \n"
                       "step further, possessing  \n"
                       "extraordinary firepower at\n"
                       "the expense of physical   \n"
                       "weakness.                 \n"
                       "\n"
                       "Special: Teleport",
        .name_pool = mage_names,
        .name_pool_size = sizeof(mage_names) / sizeof(mage_names[0]),
        .is_playable = false,
        .playable_order = 999,
    };
    return desc;
}
