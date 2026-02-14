/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static bool fire_elemental_check_special_ai(living* self)
{
    if (self->foe)
    {
        Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
        return (distance < 130);
    }
    self->foe = myscreen->find_near_foe(self);
    if (!self->foe)
        return false;
    Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
    return (distance < 130);
}

static bool fire_elemental_on_death(walker* self)
{
    self->dead = 0;
    self->stats()->magicpoints += self->stats()->special_cost[1];
    self->special();
    self->dead = 1;
    return true;
}

static void fire_elemental_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    s = (s * 3) / 2;
    c /= 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

const FamilyDescriptor& describe_family_fire_elemental()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_FIREELEMENTAL,
        .name = "ELEMENTAL",
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
        .do_special = nullptr,
        .check_special_ai = fire_elemental_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = fire_elemental_level_up,
        .on_death = fire_elemental_on_death,
    };
    return desc;
}
