/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>

#define BASE_GUY_HP 30

static bool slime_on_death(walker* self)
{
    self->dead = 1;
    walker* newob = myscreen->level_data.add_ob(Order::Living, FAMILY_MEDIUM_SLIME);
    newob->team_num = self->team_num;
    newob->stats()->level = self->stats()->level;
    newob->set_difficulty(self->stats()->level);
    newob->foe = self->foe;
    newob->leader = self->leader;
    if (self->stats()->name.size())
        self->stats()->name = newob->stats()->name;
    if (self->myguy)
    {
        self->move_myguy_to(newob);
    }
    newob->center_on(self);
    self->stats()->hitpoints = self->stats()->max_hitpoints;
    return true;
}

static bool medium_slime_on_death(walker* self)
{
    self->dead = 1;
    walker* newob = myscreen->level_data.add_ob(Order::Living, FAMILY_SMALL_SLIME);
    newob->team_num = self->team_num;
    newob->stats()->level = self->stats()->level;
    newob->set_difficulty(self->stats()->level);
    newob->foe = self->foe;
    newob->leader = self->leader;
    if (self->stats()->name.size())
        self->stats()->name = newob->stats()->name;
    if (self->myguy)
    {
        self->move_myguy_to(newob);
    }
    newob->center_on(self);
    self->stats()->hitpoints = self->stats()->max_hitpoints;
    return true;
}

const FamilyDescriptor& describe_family_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SLIME,
        .name = "SLIME",
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+120, 0, 28, 0, 0, 0, 3, 11},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "SPLIT", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = nullptr,
        .on_death = slime_on_death,
    };
    return desc;
}

const FamilyDescriptor& describe_family_small_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SMALL_SLIME,
        .name = "SLIME",
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+50, 0, 12, 0, 0, 0, 2, 12},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE | BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "GROW", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = nullptr,
        .on_death = nullptr,
    };
    return desc;
}

const FamilyDescriptor& describe_family_medium_slime()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MEDIUM_SLIME,
        .name = "SLIME",
        .base_stats = {18, 2, 18, 7, 6, 1},
        .hiring_cost = 700,
        .derived_bonuses = {BASE_GUY_HP+80, 0, 20, 0, 0, 0, 2, 10},
        .stat_costs = {20, 20, 8, 14, 50, 200},
        .special_cost = {5000, 30, 5000, 5000, 5000, 5000},
        .weapon_cost = 0,
        .default_weapon = FAMILY_BLOB,
        .init_bit_flags = BIT_ANIMATE,
        .init_ani_type = 0,
        .init_max_magicpoints = 50,
        .special_names = {"NONE", "GROW", "NONE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .do_special = nullptr,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = nullptr,
        .on_death = medium_slime_on_death,
    };
    return desc;
}
