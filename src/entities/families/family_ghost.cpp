/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static bool ghost_check_special_ai(living* self)
{
    if (self->foe)
    {
        Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
        return (distance < 130);
    }
    self->foe = self->sim_level->find_near_foe(self);
    if (!self->foe)
        return false;
    Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
    return (distance < 130);
}

static bool ghost_do_special(walker* self)
{
    walker* newob = self->sim_level->add_ob(Order::FX, FAMILY_GHOST_SCARE);
    newob->ani_type = ANI_SCARE;
    newob->setxy(self->xpos + self->sizex/2 - newob->sizex/2,
                 self->ypos + self->sizey/2 - newob->sizey/2);
    newob->owner = self;
    newob->stats()->level = self->stats()->level;
    newob->team_num = self->team_num;
    return true;
}

const FamilyDescriptor& describe_family_ghost()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_GHOST,
        .name = "GHOST",
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
    };
    return desc;
}
