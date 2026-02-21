/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>

#include <cmath>
#include <list>

#define BASE_GUY_HP 30

// rng wrapper removed: use SimEntity field sim_rng directly

static bool soldier_do_special(walker* self)
{
    walker* newob;
    std::int32_t tempx, tempy;
    std::int32_t howmany;
    std::int32_t generic;

    switch (self->current_special)
    {
        case 1: // charge enemy
            if (!self->stats()->forward_blocked())
            {
                self->stats()->add_command(COMMAND_RUSH, 3,
                    static_cast<std::int32_t>(self->lastx / self->stepsize),
                    static_cast<std::int32_t>(self->lasty / self->stepsize));
                og::sim::emit_sound(self->sim_events, SOUND_CHARGE);
            }
            else
                return false;
            break;
        case 2: // boomerang
            newob = self->sim_level->add_ob(Order::FX, FAMILY_BOOMERANG);
            newob->owner = self;
            newob->team_num = self->team_num;
            newob->ani_type = 1;
            newob->lifetime = 30 + self->stats()->level * 12;
            newob->stats()->hitpoints += static_cast<float>(self->stats()->level) * 12.0f;
            newob->stats()->max_hitpoints = newob->stats()->hitpoints;
            newob->damage += static_cast<float>(self->stats()->level) * 4.0f;
            break;
        case 3: // whirlwind attack
            if (self->busy)
                return false;
            self->busy += 8;
            tempx = static_cast<std::int32_t>(self->lastx);
            tempy = static_cast<std::int32_t>(self->lasty);
            self->curdir = -1;
            self->lastx = 0;
            self->lasty = 0;
            self->stats()->add_command(COMMAND_WALK, 1, 0, -1);
            self->stats()->add_command(COMMAND_WALK, 1, 1, -1);
            self->stats()->add_command(COMMAND_WALK, 1, 1, 0);
            self->stats()->add_command(COMMAND_WALK, 1, 1, 1);
            self->stats()->add_command(COMMAND_WALK, 1, 0, 1);
            self->stats()->add_command(COMMAND_WALK, 1, -1, 1);
            self->stats()->add_command(COMMAND_WALK, 1, -1, 0);
            self->stats()->add_command(COMMAND_WALK, 1, -1, -1);

            {
                std::list<walker*> newlist = self->sim_level->find_foes_in_range(
                    self->sim_level->oblist,
                    32 + self->stats()->level * 2, &howmany, self);

                for (auto* w : newlist)
                {
                    if (w)
                    {
                        tempx = w->xpos - self->xpos;
                        if (tempx)
                            tempx = tempx / (abs(tempx));
                        tempy = w->ypos - self->ypos;
                        if (tempy)
                            tempy = tempy / (abs(tempy));
                        self->attack(w);
                        w->stats()->force_command(COMMAND_WALK, 8, tempx, tempy);
                    }
                }
            }
            break;
        case 4: // Disarm opponent
            if (self->busy)
                return false;
            if (!self->stats()->forward_blocked())
                return false;

            {
                std::list<walker*> newlist = self->sim_level->find_foes_in_range(
                    self->sim_level->oblist, 28, &howmany, self);

                generic = 0;

                for (auto* w : newlist)
                {
                    if (w)
                    {
                        if (self->sim_rng->next(self->stats()->level) >= self->sim_rng->next(w->stats()->level))
                            w->busy += 6.0f * static_cast<float>(self->stats()->level - w->stats()->level + 1);
                        generic = 1;
                    }
                }

                if (generic)
                {
                    og::sim::emit_sound(self->sim_events, SOUND_CHARGE);
                    if (self->team_num == 0 || self->myguy)
                        og::sim::emit_notification(self->sim_events, "Fighter Disarmed Enemy!");
                    self->busy += 5;
                }
                else
                    return false;
            }
            break;
        default:
            break;
    }
    return true;
}

static bool soldier_check_special_ai(living* self)
{
    if (self->foe)
    {
        std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe));
        if (distance < 75 && distance > 20)
            return true;
        return false;
    }
    self->foe = self->sim_level->find_near_foe(self);
    if (!self->foe)
        return false;
    std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe));
    if (distance < 75 && distance > 20)
        return true;
    return false;
}

static bool soldier_on_fire_weapon(walker* self, walker* weapon)
{
    living* lv = dynamic_cast<living*>(self);
    if (lv == nullptr)
        return true;
    if (lv->weapons_left <= 0)
    {
        self->stats()->magicpoints += self->stats()->weapon_cost;
        weapon->dead = 1;
        return false;
    }
    lv->weapons_left--;
    return true;
}

static void soldier_on_create(walker* self)
{
    if (living* lv = dynamic_cast<living*>(self))
    {
        lv->weapons_left = static_cast<short>((self->stats()->level + 1) / 2);
    }
}

static void soldier_set_difficulty(living* self, std::uint32_t level)
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
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = true,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SOLDIER SLAIN",
        .do_special = soldier_do_special,
        .check_special_ai = soldier_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = soldier_set_difficulty,
        .level_up = nullptr,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = soldier_on_fire_weapon,
        .handle_teleport = nullptr,
        .on_create = soldier_on_create,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
    };
    return desc;
}
