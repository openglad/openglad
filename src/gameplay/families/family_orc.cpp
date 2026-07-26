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

static short orc_promotion_level([[maybe_unused]] int old_level)
{
    return 1;
}

static const char* const orc_names[] = {"Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog", "Krosh"};

const FamilyDescriptor& describe_family_orc()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ORC,
        .name = "ORC",
        .short_name = nullptr,
        .base_stats = {18, 8, 16, 5, 11, 1},
        .hiring_cost = 300,
        .derived_bonuses = {BASE_GUY_HP+110, 0, 23, 0, 0, 0, 3, 7},
        .stat_costs = {6, 15, 5, 40, 50, 200},
        .special_cost = {5000, 25, 20, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_ROCK,
        .init_bit_flags = BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HOWL", "EAT CORPSE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = FAMILY_BIG_ORC,
        .promotion_level_req = 5,
        .promotion_new_level = orc_promotion_level,
        .death_message = "ORC DIED",
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
        .pix_filename = "orc.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 20,
        .description = "Orcs are a basic 'grunt'; \n"
                       "strong and hard to hurt,  \n"
                       "they don't do much more   \n"
                       "than inflict pain. Orcs   \n"
                       "can't attack at range.    \n"
                       "\n"
                       "Special: Howl",
        .name_pool = orc_names,
        .name_pool_size = sizeof(orc_names) / sizeof(orc_names[0]),
        .is_playable = true,
        .playable_order = 9,
    };
    return desc;
}
