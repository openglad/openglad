/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/guy.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static void elf_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    s = (s * 3) / 4;
    d = (d * 3) / 2;
    c = (c * 3) / 4;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

const FamilyDescriptor& describe_family_elf()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ELF,
        .name = "ELF",
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
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = elf_level_up,
        .on_death = nullptr,
    };
    return desc;
}
