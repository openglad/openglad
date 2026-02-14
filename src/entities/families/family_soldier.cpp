/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/base.h>

#include <openglad/runtime/screen.h>

#define BASE_GUY_HP 30

static bool soldier_check_special_ai(living* self)
{
    if (self->foe)
    {
        Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
        if (distance < 75 && distance > 20)
            return true;
        return false;
    }
    self->foe = myscreen->find_near_foe(self);
    if (!self->foe)
        return false;
    Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
    if (distance < 75 && distance > 20)
        return true;
    return false;
}

static void soldier_set_difficulty(living* self, Uint32 level)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += 13.0f * levmult;
    self->stats()->max_magicpoints += 8.0f * levmult;
    self->weapons_left = static_cast<short>((level + 1) / 2);
    self->damage += 5.0f * level_f;
    self->stats()->armor += 2.0f * levmult;
}

const FamilyDescriptor& describe_family_soldier()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SOLDIER,
        .name = "SOLDIER",
        .base_stats = {12, 6, 12, 8, 9, 1},
        .hiring_cost = 250,
        .derived_bonuses = {BASE_GUY_HP+90, 0, 20, 0, 0, 0, 4, 6},
        .stat_costs = {6, 10, 6, 25, 50, 200},
        .special_cost = {5000, 25, 100, 120, 150, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "CHARGE", "BOOMERANG", "WHIRLWIND", "DISARM", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = soldier_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = soldier_set_difficulty,
        .level_up = nullptr,
        .on_death = nullptr,
    };
    return desc;
}
