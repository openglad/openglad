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
#include <openglad/runtime/game_context.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

#define BASE_GUY_HP 30

static void archmage_hit_response(statistics* stats, walker* foe)
{
    walker* controller = stats->controller;
    controller->busy = 0; // yes, this is a cheat

    Sint32 possible_specials[NUM_SPECIALS];
    for (int i = 0; i < NUM_SPECIALS; i++)
        possible_specials[i] = 0;
    for (int i = 0; i <= (stats->level + 2) / 3; i++)
        if (i < NUM_SPECIALS && stats->magicpoints >= stats->special_cost[i])
            possible_specials[i] = 1;

    float threshold;
    if (controller->myguy)
        threshold = (3.0f * stats->max_hitpoints) / 5.0f;
    else
        threshold = (3.0f * stats->max_hitpoints) / 8.0f;

    if (stats->hitpoints < threshold && possible_specials[1] && rng(3))
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
        Sint32 howmany = 0;
        myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                     200, &howmany, controller);
        if (howmany)
        {
            if (possible_specials[3])
            {
                controller->current_special = 3;
                if (controller->special())
                    return;
            }
            if (possible_specials[2])
            {
                if (rng(2))
                {
                    controller->shifter_down = 1;
                    controller->current_special = 2;
                    if (controller->special())
                    {
                        controller->shifter_down = 0;
                        if (stats->magicpoints >= stats->special_cost[1])
                        {
                            controller->busy = 0;
                            controller->special();
                        }
                        return;
                    }
                }
                controller->shifter_down = 0;
                controller->current_special = 2;
                if (controller->special())
                {
                    if (stats->magicpoints >= stats->special_cost[1])
                    {
                        controller->busy = 0;
                        controller->special();
                    }
                    return;
                }
            }
        }
    }
}

static void archmage_level_up(guy* self, Sint32 level_diff)
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

const FamilyDescriptor& describe_family_archmage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ARCHMAGE,
        .name = "ARCHMAGE",
        .base_stats = {4, 6, 4, 16, 5, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 8, 0, 0, 0, 3, 1},
        .stat_costs = {30, 20, 25, 7, 55, 200},
        .special_cost = {5000, 10, 80, 500, 150, 5000},
        .weapon_cost = 12,
        .default_weapon = FAMILY_FIREBALL,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TELEPORT", "HEARTBURST", "SUMMON IMAGE", "MIND CONTROL", "NONE"},
        .alternate_names = {"NONE", "TELEPORT MARKER", "CHAIN LIGHTNING", "SUMMON ELEMENTAL", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = archmage_hit_response,
        .set_difficulty = nullptr,
        .level_up = archmage_level_up,
        .on_death = nullptr,
    };
    return desc;
}
