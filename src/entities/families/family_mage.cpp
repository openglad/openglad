/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/living.h>
#include <openglad/core/stats.h>
#include <openglad/legacy/base.h>

#include <openglad/runtime/screen.h>

#define BASE_GUY_HP 30

static bool mage_check_special_ai(living* self)
{
    Sint32 howmany = 0;
    myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                 110, &howmany, self);
    if (howmany < 1)
        return true;
    if (howmany > 3)
        return true;
    return false;
}

static void mage_hit_response(statistics* stats, walker* foe)
{
    walker* controller = stats->controller;
    float threshold;
    if (controller->myguy)
        threshold = (3.0f * stats->max_hitpoints) / 5.0f;
    else
        threshold = (3.0f * stats->max_hitpoints) / 8.0f;

    Sint32 possible_specials[NUM_SPECIALS];
    for (int i = 0; i < NUM_SPECIALS; i++)
        possible_specials[i] = 0;
    for (int i = 0; i <= (stats->level + 2) / 3; i++)
        if (i < NUM_SPECIALS && stats->magicpoints >= stats->special_cost[i])
            possible_specials[i] = 1;

    if (stats->hitpoints < threshold && possible_specials[1])
    {
        controller->current_special = 1;
        controller->shifter_down = 0;
        controller->busy = 0;
        controller->special();
    }
    else
    {
        if (controller->foe != foe)
        {
            controller->foe = foe;
            foe->foe = controller;
            stats->last_distance = stats->current_distance = 15000;
        }
    }
}

static void mage_set_difficulty(living* self, Uint32 level)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += 7.0f * levmult;
    self->stats()->max_magicpoints += 14.0f * levmult;
    self->damage += 3.0f * level_f;
    self->stats()->armor += levmult / 2.0f;
}

static void mage_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    s /= 2;
    c /= 2;
    it *= 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

const FamilyDescriptor& describe_family_mage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MAGE,
        .name = "MAGE",
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
        .do_special = nullptr,
        .check_special_ai = mage_check_special_ai,
        .hit_response = mage_hit_response,
        .set_difficulty = mage_set_difficulty,
        .level_up = mage_level_up,
        .on_death = nullptr,
    };
    return desc;
}
