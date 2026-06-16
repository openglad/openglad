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
#include <openglad/gameplay/summon.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>

#include <iterator>

#define BASE_GUY_HP 30

static bool ghost_check_special_ai(living* self)
{
    return check_special_ai_distance(self, 130);
}

static bool ghost_do_special(walker* self)
{
    walker* newob = summon_entity(self, Order::FX, FAMILY_GHOST_SCARE);
    if (!newob) return false;
    newob->set_ani_type(ANI_SCARE);
    newob->setxy(self->xpos() + self->sizex()/2 - newob->sizex()/2,
                 self->ypos() + self->sizey()/2 - newob->sizey()/2);
    return true;
}

static const char* const ghost_names[] = {"Casper", "Slimer", "Reaper", "Ecto", "Pepper", "Boo", "Banshee", "Nyx"};

const FamilyDescriptor& describe_family_ghost()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_GHOST,
        .name = "GHOST",
        .short_name = nullptr,
        .base_stats = {6, 12, 18, 10, 15, 1},
        .hiring_cost = 600,
        .derived_bonuses = {BASE_GUY_HP+20, 0, 12, 0, 0, 0, 4, 7},
        .stat_costs = {16, 16, 16, 16, 45, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = BIT_ANIMATE | BIT_FLYING | BIT_ETHEREAL | BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "SCARE", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = false,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = true,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "GHOST VANISHED",
        .do_special = ghost_do_special,
        .check_special_ai = ghost_check_special_ai,
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
        .pix_filename = "ghost.png",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 12,
        .description = "Ghosts can pass through   \n"
                       "walls, trees, and anything\n"
                       "else that gets in the way.\n"
                       "Their chilling touch can  \n"
                       "bring death quickly at    \n"
                       "close range.              \n"
                       "\n"
                       "Special: Scare",
        .name_pool = ghost_names,
        .name_pool_size = std::size(ghost_names),
        .is_playable = true,
        .playable_order = 14,
    };
    return desc;
}
