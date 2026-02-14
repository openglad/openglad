/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static bool thief_check_special_ai(living* self)
{
    if (self->current_special == 1) // drop bomb
    {
        if (self->foe)
        {
            Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
            if (distance < 130 && distance > 35)
                return false;
        }
        else
        {
            Sint32 howmany = 0;
            myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                         110, &howmany, self);
            if (howmany < 3)
                return false;
            return true;
        }
        return true; // fallthrough for foe case when distance is acceptable
    }
    else if (self->current_special == 3)
    {
        Uint32 myrange;
        if (!self->shifter_down)
            myrange = 80 + 4 * self->stats()->level;
        else
            myrange = 16 + 4 * self->stats()->level;

        Sint32 howmany = 0;
        myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                     myrange, &howmany, self);
        if (howmany < 1)
            return false;
        return true;
    }
    return true; // default: go for it
}

static void thief_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    s /= 2;
    d *= 2;
    c /= 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

const FamilyDescriptor& describe_family_thief()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_THIEF,
        .name = "THIEF",
        .base_stats = {9, 12, 12, 10, 5, 1},
        .hiring_cost = 400,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 12, 0, 0, 0, 5, 5},
        .stat_costs = {15, 6, 9, 10, 50, 200},
        .special_cost = {5000, 35, 125, 100, 150, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_KNIFE,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "DROP BOMB", "CLOAK", "TAUNT ENEMY", "POISON CLOUD", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "CHARM OPPONENT", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = thief_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = thief_level_up,
        .on_death = nullptr,
    };
    return desc;
}
