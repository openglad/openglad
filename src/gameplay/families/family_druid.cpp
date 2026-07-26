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

static const char* const druid_names[] = {"Roland", "Merlin", "Hippy", "Green Thumb", "Treefall", "Rain"};

const FamilyDescriptor& describe_family_druid()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_DRUID,
        .name = "DRUID",
        .short_name = nullptr,
        .base_stats = {7, 8, 14, 12, 7, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+80, 0, 10, 0, 0, 0, 3, 9},
        .stat_costs = {15, 15, 7, 6, 50, 200},
        .special_cost = {5000, 15, 80, 150, 200, 5000},
        .weapon_cost = 4,
        .default_weapon = FAMILY_LIGHTNING,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "GROW TREE", "SUMMON FAERIE", "REVEAL", "PROTECTION", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "DRUID VANQUISHED",
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
        .pix_filename = "druid.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 10,
        .description = "Druids are the magicians  \n"
                       "of nature, and have power \n"
                       "over natural events. They \n"
                       "throw lightning bolts at  \n"
                       "their foes; the fast bolts\n"
                       "have long range.          \n"
                       "\n"
                       "Special: Plant Tree",
        .name_pool = druid_names,
        .name_pool_size = std::size(druid_names),
        .is_playable = true,
        .playable_order = 8,
    };
    return desc;
}
