/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/summon.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/foe_query.h>

#include <cmath>

#define BASE_GUY_HP 30

// RNG now comes from current_game->world->rng_.

static bool soldier_do_special(walker* self)
{
    walker* newob;
    std::int32_t tempx, tempy;
    std::int32_t generic;

    switch (self->current_special)
    {
        case 1: // charge enemy
            if (!self->stats()->forward_blocked())
            {
                self->stats()->add_command(COMMAND_RUSH, 3,
                    static_cast<std::int32_t>(self->lastx / self->stepsize),
                    static_cast<std::int32_t>(self->lasty / self->stepsize));
                og::sim::emit_sound(current_game->sim_events, SOUND_CHARGE);
            }
            else
                return false;
            break;
        case 2: // boomerang
            newob = summon_entity(self, Order::FX, FAMILY_BOOMERANG);
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

            for_each_foe_in_range(self, 32 + self->stats()->level * 2, [&](walker* w) {
                tempx = w->xpos - self->xpos;
                if (tempx)
                    tempx = tempx / (abs(tempx));
                tempy = w->ypos - self->ypos;
                if (tempy)
                    tempy = tempy / (abs(tempy));
                self->attack(w);
                w->stats()->force_command(COMMAND_WALK, 8, tempx, tempy);
            });
            break;
        case 4: // Disarm opponent
            if (self->busy)
                return false;
            if (!self->stats()->forward_blocked())
                return false;

            {
                generic = 0;
                for_each_foe_in_range(self, 28, [&](walker* w) {
                    if (current_game->world->rng_.next(self->stats()->level) >= current_game->world->rng_.next(w->stats()->level))
                        w->busy += 6.0f * static_cast<float>(self->stats()->level - w->stats()->level + 1);
                    generic = 1;
                });

                if (generic)
                {
                    og::sim::emit_sound(current_game->sim_events, SOUND_CHARGE);
                    if (self->team_num == 0 || self->myguy)
                        og::sim::emit_notification(current_game->sim_events, "Fighter Disarmed Enemy!");
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
    self->foe = current_game->world->find_near_foe(self);
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
    apply_difficulty_scaling(self, level, {13.0f, 8.0f, 5.0f, 2.0f});
    self->weapons_left = static_cast<short>((level + 1) / 2);
}

static const char* const soldier_names[] = {"Lothar", "Arthur", "Uther", "Achilles", "Lu Bu", "Wallace", "Leonidas", "Attila", "Alexander", "Ajax", "Nestor", "Priam", "Hector", "Tom", "Bigfoot"};

const FamilyDescriptor& describe_family_soldier()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_SOLDIER,
        .name = "SOLDIER",
        .short_name = nullptr,
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
        .on_melee_hit = nullptr,
        .pix_filename = "footman.pix",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 7,
        .description = "Your basic grunt, can     \n"
                       "absorb and deal damage and\n"
                       "move moderately fast. A   \n"
                       "good all-around fighter. A\n"
                       "soldier's normal weapon is\n"
                       "a magical returning blade.\n"
                       "\n"
                       "Special: Charge",
        .name_pool = soldier_names,
        .name_pool_size = sizeof(soldier_names) / sizeof(soldier_names[0]),
        .is_playable = true,
        .playable_order = 0,
    };
    return desc;
}
