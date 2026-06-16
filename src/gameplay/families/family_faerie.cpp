/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>

#include <iterator>

#define BASE_GUY_HP 30

static void faerie_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {4, 12, 4, 8, 1});
}

static const char* const faerie_names[] = {"Tink", "Gem", "Glitter", "Jewel", "Blossom", "Ruby", "Muffin", "Flutter", "Sparkle", "Sprint", "Sprite", "Eve", "Twinkle", "Violet", "Daisy", "Lily"};

const FamilyDescriptor& describe_family_faerie()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_FAERIE,
        .name = "FAERIE",
        .short_name = nullptr,
        .base_stats = {3, 8, 3, 14, 2, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 5, 0, 0, 0, 4, 9},
        .stat_costs = {25, 6, 12, 8, 50, 200},
        .special_cost = {5000, 5000, 5000, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_SPRINKLE,
        .init_bit_flags = BIT_ANIMATE | BIT_FLYING,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "FAERIE POPPED",
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = faerie_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "faerie.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 8,
        .description = "The faerie are small,     \n"
                       "flying above friends and  \n"
                       "enemies alike unnoticed.  \n"
                       "Although they are delicate\n"
                       "and easily destroyed,     \n"
                       "faeries can sprinkle a    \n"
                       "magic powder which freezes\n"
                       "their enemies.",
        .name_pool = faerie_names,
        .name_pool_size = std::size(faerie_names),
        .is_playable = true,
        .playable_order = 13,
    };
    return desc;
}
