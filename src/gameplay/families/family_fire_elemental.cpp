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
#include <iterator>

#define BASE_GUY_HP 30

static const char* const elemental_names[] = {"Furnace", "Molten", "Burns", "Fire Eli", "Fireball", "Sunny", "Lava", "Heatwave", "Torch", "Scorch"};

const FamilyDescriptor& describe_family_fire_elemental()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_FIREELEMENTAL,
        .name = "ELEMENTAL",
        .short_name = "ELEMENT.",
        .base_stats = {14, 10, 14, 14, 9, 1},
        .hiring_cost = 600,
        .derived_bonuses = {BASE_GUY_HP+70, 0, 28, 0, 0, 0, 4, 5},
        .stat_costs = {7, 10, 14, 12, 50, 200},
        .special_cost = {5000, 50, 5000, 5000, 5000, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_METEOR,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 150,
        .special_names = {"NONE", "STARBURST", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "FIRE ELEMENTAL EXTINGUISHED",
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
        .pix_filename = "firelem.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 10,
        .description = "Strong and quick, fire    \n"
                       "elementals can expel      \n"
                       "flaming meteors in all    \n"
                       "directions to decimate    \n"
                       "enemies.                  \n"
                       "\n"
                       "Special: Starburst",
        .name_pool = elemental_names,
        .name_pool_size = std::size(elemental_names),
        .is_playable = true,
        .playable_order = 11,
    };
    return desc;
}
