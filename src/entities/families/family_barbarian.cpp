/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static void barbarian_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    s = (s * 3) / 2;
    d /= 2;
    c = (c * 3) / 2;
    it /= 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

static bool barbarian_do_special(walker* self)
{
    if (self->busy > 0)
        return false;
    walker* newob = self->fire();
    if (!newob)
        return false;
    walker* alive = self->sim_level->add_ob(Order::Weapon, FAMILY_BOULDER);
    alive->center_on(newob);
    alive->owner = self;
    alive->stats()->level = self->stats()->level;
    alive->lastx = newob->lastx;
    alive->lasty = newob->lasty;
    if (self->myguy)
    {
        alive->stepsize = 1.0f + self->myguy->strength / 7;
        alive->damage += self->myguy->strength / 5.0f;
    }
    else
    {
        alive->stepsize = static_cast<float>(self->stats()->level) * 2.0f;
        alive->damage += static_cast<float>(self->stats()->level);
    }
    if (alive->stepsize < 1)
        alive->stepsize = 1;
    if (alive->stepsize > 15)
        alive->stepsize = 15;
    if (alive->lasty > 0)
        alive->lasty = alive->stepsize;
    else if (alive->lasty < 0)
        alive->lasty = -(alive->stepsize);
    if (alive->lastx > 0)
        alive->lastx = alive->stepsize;
    else if (alive->lastx < 0)
        alive->lastx = -(alive->stepsize);
    if (self->current_special == 2)
        alive->skip_exit = 5000;
    else
        alive->skip_exit = 0;
    newob->dead = 1;
    self->busy += 1.0f + static_cast<float>(self->current_special) * 5.0f;
    return true;
}

const FamilyDescriptor& describe_family_barbarian()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_BARBARIAN,
        .name = "BARBARIAN",
        .base_stats = {14, 5, 14, 8, 8, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 25, 0, 0, 0, 3, 5.5f},
        .stat_costs = {5, 35, 5, 35, 50, 200},
        .special_cost = {5000, 20, 30, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_HAMMER,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HURL BOULDER", "EXPLODING BOULDER", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 0.5f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SOMEONE DIED",
        .do_special = barbarian_do_special,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = barbarian_level_up,
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
