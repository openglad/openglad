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
#include <openglad/entities/walker.h>
#include <openglad/data/level_data.h>
#include <openglad/core/stats.h>
#include <openglad/core/combat_math.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>

#include <format>
#include <string>
#include <list>

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

#define BASE_GUY_HP 30

static void druid_set_difficulty(living* self, Uint32 level)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += 9.0f * levmult;
    self->stats()->max_magicpoints += 12.0f * levmult;
    self->damage += 4.0f * level_f;
    self->stats()->armor += levmult / 2.0f;
}

static void druid_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    d /= 2;
    it = (it * 3) / 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

static bool druid_do_special(walker* self)
{
    walker* newob;
    walker* alive;
    walker* tempwalk;
    Sint32 didheal;
    std::string message;

    switch (self->current_special)
    {
        case 1: // plant tree
            if (self->busy > 0)
                return false;
            self->stats()->magicpoints += self->stats()->weapon_cost;
            newob = self->fire();
            if (!newob)
                return false;
            self->busy += (self->fire_frequency * 2);
            alive = self->sim_level->add_ob(Order::Weapon, FAMILY_TREE);
            alive->setxy(newob->xpos, newob->ypos);
            alive->team_num = self->team_num;
            alive->ani_type = ANI_GROW;
            alive->owner = self;
            newob->dead = 1;
            break;
        case 2: // summon faerie
            if (self->busy > 0)
                return false;
            self->stats()->magicpoints += self->stats()->weapon_cost;
            newob = self->fire();
            if (!newob)
                return false;
            alive = self->sim_level->add_ob(Order::Living, FAMILY_FAERIE);
            alive->setxy(newob->xpos, newob->ypos);
            alive->team_num = self->team_num;
            alive->owner = self;
            alive->lifetime = 50 + self->stats()->level * 40;
            newob->dead = 1;
            if (!self->sim_level->query_passable(alive->xpos, alive->ypos, alive))
            {
                alive->dead = 1;
                return false;
            }
            self->busy += (self->fire_frequency * 3);
            break;
        case 3: // reveal items
            if (self->busy > 0)
                return false;
            self->view_all = static_cast<short>(self->view_all + self->stats()->level * 10);
            self->busy += (self->fire_frequency * 4);
            break;
        case 4: // circle of protection
        default:
            if (self->busy > 0)
                return false;
            {
                Sint32 howmany;
                std::list<walker*> newlist = self->sim_level->find_friends_in_range(self->sim_level->oblist,
                          60, &howmany, self);
                didheal = 0;
                if (howmany > 1)
                {
                    for (auto* w : newlist)
                    {
                        newob = w;
                        if (newob != self)
                        {
                            tempwalk = nullptr;
                            for (auto& uptr : self->sim_level->oblist)
                            {
                                walker* ob = uptr.get();
                                if (ob && ob->owner == newob
                                        && ob->query_order() == Order::Weapon
                                        && ob->query_family() == FAMILY_CIRCLE_PROTECTION)
                                {
                                    tempwalk = ob;
                                    break;
                                }
                            }
                            if (!tempwalk)
                            {
                                alive = self->sim_level->add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
                                if (!alive)
                                    return false;
                                alive->owner = newob;
                                alive->center_on(newob);
                                alive->team_num = newob->team_num;
                                alive->stats()->level = newob->stats()->level;
                                didheal++;
                            }
                            else
                            {
                                alive = self->sim_level->add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
                                if (!alive)
                                    return false;
                                tempwalk->stats()->hitpoints += alive->stats()->hitpoints;
                                alive->dead = 1;
                                didheal++;
                            }
                            if (self->myguy)
                                self->myguy->exp += exp_from_action(ExpAction::Protection, self, newob, 0);
                        }
                    }
                    if (!didheal)
                        return false;
                    else
                    {
                        if (didheal == 1)
                            message = "Druid protected 1 man!";
                        else
                            message = std::format("Druid protected {} men!", didheal);
                        if (self->team_num == 0 || self->myguy)
                            og::sim::emit_notification(self->sim_events, message);
                        og::sim::emit_sound(self->sim_events, SOUND_HEAL);
                    }
                }
                else
                    return false;
            }
            break;
    }
    return true;
}

const FamilyDescriptor& describe_family_druid()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_DRUID,
        .name = "DRUID",
        .base_stats = {7, 8, 14, 12, 7, 1},
        .hiring_cost = 350,
        .derived_bonuses = {BASE_GUY_HP+80, 0, 10, 0, 0, 0, 3, 9},
        .stat_costs = {15, 15, 7, 6, 50, 200},
        .special_cost = {5000, 15, 80, 150, 200, 5000},
        .weapon_cost = 4,
        .default_weapon = FAMILY_LIGHTNING,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "GROW TREE", "SUMMON FAERIE", "REVEAL", "PROTECTION", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "DRUID VANQUISHED",
        .do_special = druid_do_special,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = druid_set_difficulty,
        .level_up = druid_level_up,
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
