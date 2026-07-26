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

static const char* const elf_names[] = {"Legolas", "Took", "Elrond", "Tanis", "Acorn", "Lightfoot", "Treewee"};

const FamilyDescriptor& describe_family_elf()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ELF,
        .name = "ELF",
        .short_name = nullptr,
        .base_stats = {5, 14, 5, 12, 8, 1},
        .hiring_cost = 150,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 12, 0, 0, 0, 4, 5},
        .stat_costs = {25, 6, 12, 8, 50, 200},
        .special_cost = {5000, 10, 20, 30, 40, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_ROCK,
        .init_bit_flags = BIT_FORESTWALK,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "ROCKS", "BOUNCING ROCKS", "LOTS OF ROCKS", "MEGA ROCKS", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "ELF KILLED",
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
        .pix_filename = "elf.png",
        .animation_type = FamilyAnimationType::FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 8,
        .description = "Elves are small and weak, \n"
                       "but are harder to hit than\n"
                       "most classes. Alone of all\n"
                       "the classes, elves possess\n"
                       "the 'ForestWalk' ability. \n"
                       "\n"
                       "Special: Rocks",
        .name_pool = elf_names,
        .name_pool_size = std::size(elf_names),
        .is_playable = true,
        .playable_order = 2,
    };
    return desc;
}
