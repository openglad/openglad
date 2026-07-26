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

static short mage_promotion_level(int old_level)
{
    return static_cast<short>((old_level - 6) / 2 + 1);
}

static const char* const mage_names[] = {"Gandalf", "Saruman", "Radagast", "Alatar", "Pallando", "Raistlin", "Fizban", "Mordenkainen", "Merlin", "Harry", "Manannan", "Mordack", "Jace"};

const FamilyDescriptor& describe_family_mage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MAGE,
        .name = "MAGE",
        .short_name = nullptr,
        .base_stats = {4, 6, 4, 16, 5, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+60, 0, 4, 0, 0, 0, 2, 4},
        .stat_costs = {20, 15, 16, 6, 50, 200},
        .special_cost = {5000, 15, 60, 500, 70, 100},
        .weapon_cost = 5,
        .default_weapon = FAMILY_FIREBALL,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TELEPORT", "WARP SPACE", "FREEZE TIME", "ENERGY WAVE", "HEARTBURST"},
        .alternate_names = {"NONE", "TELEPORT MARKER", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = FAMILY_ARCHMAGE,
        .promotion_level_req = 6,
        .promotion_new_level = mage_promotion_level,
        .death_message = "MAGE DIED",
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
        .pix_filename = "mage.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_MAGE,
        .ai_line_of_sight = 7,
        .description = "Mages are slow, can't     \n"
                       "stand much damage, and are\n"
                       "horrible at hand-to-hand  \n"
                       "combat, but their magical \n"
                       "fireballs pack a big      \n"
                       "punch.                    \n"
                       "\n"
                       "Special: Teleport",
        .name_pool = mage_names,
        .name_pool_size = sizeof(mage_names) / sizeof(mage_names[0]),
        .is_playable = true,
        .playable_order = 4,
    };
    return desc;
}
