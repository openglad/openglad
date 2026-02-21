/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/guy.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>

#define BASE_GUY_HP 30

static bool archer_do_special(walker* self)
{
    walker* newob;

    switch (self->current_special)
    {
        case 1: // fire arrows
        {
            self->curdir = -1;
            self->lastx = 0;
            self->lasty = 0;
            self->stats()->magicpoints += 8.0f * static_cast<float>(self->stats()->weapon_cost);
            self->stats()->add_command(COMMAND_SET_WEAPON, 1, FAMILY_FIRE_ARROW, 0);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, 0, -1);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, 1, -1);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, 1, 0);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, 1, 1);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, 0, 1);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, -1, 1);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, -1, 0);
            self->stats()->add_command(COMMAND_QUICK_FIRE, 1, -1, -1);
            self->stats()->add_command(COMMAND_RESET_WEAPON, 1, 0, 0);
            break;
        }
        case 2: // flurry of arrows
            if (self->busy)
                return false;
            self->stats()->magicpoints += 3.0f * static_cast<float>(self->stats()->weapon_cost);
            self->fire();
            self->fire();
            self->fire();
            self->busy += (self->fire_frequency * 2.0f);
            break;
        case 3: // exploding arrows
        case 4:
        default:
        {
            if (self->busy)
                return false;
            unsigned short old_weapon = self->current_weapon;
            self->current_weapon = FAMILY_FIRE_ARROW;
            newob = self->fire();
            self->current_weapon = old_weapon;
            if (!newob)
                return false;
            newob->skip_exit = 5000;
            newob->stats()->hitpoints = 500;
            newob->damage *= 2;
            break;
        }
    }
    return true;
}

static bool archer_check_special_ai(living* self)
{
    if (self->foe)
    {
        std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe));
        return (distance < 130);
    }
    self->foe = self->sim_level->find_near_foe(self);
    if (!self->foe)
        return false;
    std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe));
    return (distance < 130);
}

static void archer_hit_response(statistics* stats, walker* foe)
{
    walker* controller = stats->controller;
    if (!controller->foe || controller->foe != foe)
    {
        controller->foe = foe;
        stats->clear_command();
        stats->last_distance = stats->current_distance = 15000;
    }
    std::int32_t distance = controller->distance_to_ob(foe);
    if (distance < 64)
    {
        std::int32_t deltax = static_cast<short>(controller->xpos - foe->xpos);
        if (deltax)
            deltax = static_cast<short>(deltax / abs(deltax));
        std::int32_t deltay = static_cast<short>(controller->ypos - foe->ypos);
        if (deltay)
            deltay = static_cast<short>(deltay / abs(deltay));
        stats->force_command(COMMAND_WALK, 8, deltax, deltay);
    }
}

static void archer_set_difficulty(living* self, std::uint32_t level)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += 11.0f * levmult;
    self->stats()->max_magicpoints += 12.0f * levmult;
    self->damage += 4.0f * level_f;
    self->stats()->armor += levmult;
}

static void archer_level_up(guy* self, std::int32_t level_diff)
{
    std::int32_t s = 8 * level_diff;
    std::int32_t d = 6 * level_diff;
    std::int32_t c = 8 * level_diff;
    std::int32_t it = 8 * level_diff;
    std::int32_t a = 1 * level_diff;
    s /= 2;
    d = (d * 3) / 2;
    self->strength = static_cast<short>(static_cast<std::int32_t>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<std::int32_t>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<std::int32_t>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<std::int32_t>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<std::int32_t>(self->armor) + a);
}

const FamilyDescriptor& describe_family_archer()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ARCHER,
        .name = "ARCHER",
        .base_stats = {6, 12, 6, 10, 5, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+60, 0, 8, 0, 0, 0, 4, 5},
        .stat_costs = {15, 6, 9, 10, 50, 200},
        .special_cost = {5000, 20, 60, 70, 5000, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_ARROW,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "FIRE ARROWS", "BARRAGE", "EXPLODING BOLT", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "ARCHER DIED",
        .do_special = archer_do_special,
        .check_special_ai = archer_check_special_ai,
        .hit_response = archer_hit_response,
        .set_difficulty = archer_set_difficulty,
        .level_up = archer_level_up,
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
