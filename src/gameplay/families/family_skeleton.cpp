/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/stats.h>

static bool skeleton_handle_teleport(walker* self)
{
    self->ani_type = ANI_TELE_IN;
    self->cycle = 0;
    self->teleport_ranged(self->stats()->level * 18);
    return true;
}

#define BASE_GUY_HP 30

static bool skeleton_check_special_ai(living* self)
{
    std::int32_t howmany = 0;
    og::gameplay::current_game->world->find_foes_in_range(og::gameplay::current_game->world->oblist,
                                 5 * GRID_SIZE, &howmany, self);
    if (howmany < 1)
        return true;
    return false;
}

static void skeleton_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {8, 12, 4, 4, 1});
}

static bool skeleton_do_special(walker* self)
{
    if (self->ani_type == ANI_TELE_OUT || self->ani_type == ANI_TELE_IN)
        return false;
    self->ani_type = ANI_TELE_OUT;
    self->cycle = 0;
    return true;
}

static const char* const skeleton_names[] = {"Drybones", "Blackbeard", "Boney", "Femur", "Patella", "Humerus", "Scapula"};

const FamilyDescriptor& describe_family_skeleton()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SKELETON,
        .name = "SKELETON",
        .short_name = "SKELTON",
        .base_stats = {9, 14, 9, 6, 6, 1},
        .hiring_cost = 300,
        .derived_bonuses = {BASE_GUY_HP+30, 0, 4, 0, 0, 0, 6, 4.5f},
        .stat_costs = {15, 6, 16, 25, 50, 200},
        .special_cost = {5000, 10, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BONE,
        .init_bit_flags = 0,
        .init_ani_type = ANI_SKEL_GROW,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TUNNEL", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = false,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = true,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SKELETON CRUMBLED",
        .do_special = skeleton_do_special,
        .check_special_ai = skeleton_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = skeleton_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = skeleton_handle_teleport,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "skeleton.pix",
        .animation_type = FAMILY_ANIM_SKELETON,
        .ai_line_of_sight = 7,
        .description = "Skeletons are the pathetic\n"
                       "remains of those who once \n"
                       "were among the living.    \n"
                       "They are not particularly \n"
                       "dangerous, but they move  \n"
                       "with blinding speed.      \n"
                       "\n"
                       "Special: Tunnel",
        .name_pool = skeleton_names,
        .name_pool_size = sizeof(skeleton_names) / sizeof(skeleton_names[0]),
        .is_playable = true,
        .playable_order = 10,
    };
    return desc;
}
