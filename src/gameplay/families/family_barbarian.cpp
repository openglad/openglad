/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>

#define BASE_GUY_HP 30

static void barbarian_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {12, 3, 12, 4, 1});
}

static bool barbarian_do_special(walker* self)
{
    if (self->busy > 0)
        return false;
    walker* newob = self->fire();
    if (!newob)
        return false;
    walker* alive = current_game->world->add_ob(Order::Weapon, FAMILY_BOULDER);
    alive->center_on(newob);
    alive->set_owner(self);
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

static const char* const barbarian_names[] = {"Thor", "Conan", "Beowulf", "Cronus", "Pallas", "Atlas", "Prometheus", "Titan"};

const FamilyDescriptor& describe_family_barbarian()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_BARBARIAN,
        .name = "BARBARIAN",
        .short_name = "BARBAR.",
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
        .on_melee_hit = nullptr,
        .pix_filename = "barby.png",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 12,
        .description = "Barbarians are powerful   \n"
                       "and resist some magic     \n"
                       "damage, but have more will\n"
                       "than skill. They are tough,\n"
                       "tending to bash their way \n"
                       "through trouble with heavy\n"
                       "iron hammers.             \n"
                       "Special: Hurl Boulder",
        .name_pool = barbarian_names,
        .name_pool_size = sizeof(barbarian_names) / sizeof(barbarian_names[0]),
        .is_playable = true,
        .playable_order = 1,
    };
    return desc;
}
