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
#include <openglad/entities/walker.h>
#include <openglad/runtime/screen.h>
#include <openglad/runtime/game_context.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/soundob.h>
#include <openglad/core/stats.h>
#include <openglad/render/view.h>

#include <format>
#include <string>
#include <list>

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

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

static bool thief_do_special(walker* self)
{
    walker* newob;
    Sint32 i;
    std::string message, tempstr;

    switch (self->current_special)
    {
        case 1: // drop bomb
            newob = myscreen->level_data.add_ob(Order::FX, FAMILY_BOMB, 1);
            newob->ani_type = ANI_BOMB;
            if (self->myguy)
            {
                self->myguy->total_shots++;
                self->myguy->scen_shots++;
            }
            newob->damage = static_cast<float>(self->stats()->level + 1) * 15.0f;
            newob->setxy(self->xpos + self->sizex/2 - newob->sizex/2,
                         self->ypos + self->sizey/2 - newob->sizey/2);
            newob->owner = self;
            // Run away if we're AI
            {
                char person = 0;
                for (i = 0; i < myscreen->numviews; i++)
                    if (myscreen->viewob[i]->control == self)
                        person = 1;
                if (!person)
                {
                    Sint32 tempx = static_cast<Sint32>(rng(3)) - 1;
                    Sint32 tempy = static_cast<Sint32>(rng(3)) - 1;
                    if ((tempx == 0) && (tempy == 0))
                        tempx = 1;
                    self->stats()->force_command(COMMAND_WALK, 20, tempx, tempy);
                }
            }
            break;
        case 2: // cloak
            self->invisibility_left = static_cast<short>(self->invisibility_left + 20 + static_cast<Sint32>(rng(20)) * self->stats()->level);
            break;
        case 3: // taunt / charm
            if (!self->shifter_down) // normal taunt
            {
                if (self->busy > 0)
                    return false;
                {
                    Sint32 howmany;
                    std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                          80 + 4 * self->stats()->level, &howmany, self);
                    for (auto* ob : newlist)
                    {
                        if (ob && (rng(self->stats()->level) >= rng(ob->stats()->level)))
                        {
                            ob->foe = self;
                            ob->leader = self;
                            if (ob->query_act_type() != ACT_CONTROL)
                                ob->stats()->force_command(COMMAND_FOLLOW, 10 + rng(self->stats()->level), 0, 0);
                        }
                    }
                }
                if (self->myguy)
                    message = std::format("{}: 'Nyah Nyah!'", self->myguy->name);
                else if (self->stats()->name.size())
                    message = std::format("{}: 'Nyah Nyah!'", self->stats()->name);
                else
                    message = "THIEF: 'Nyah Nyah!'";
                myscreen->do_notify(message.c_str(), self);
                self->busy += 2;
                break;
            }
            else // charm opponent
            {
                if (self->busy > 0)
                    return false;
                {
                    Sint32 howmany;
                    Sint32 didheal = 0;
                    Sint32 generic2 = 0;
                    std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                          16 + 4 * self->stats()->level, &howmany, self);
                    if (howmany < 1)
                        return false;
                    for (auto* ob : newlist)
                    {
                        if (didheal) break;
                        if ((ob->real_team_num == 255) &&
                            (ob->query_order() == Order::Living) &&
                            1)
                        {
                            Sint32 generic = self->stats()->level - ob->stats()->level;
                            if (generic < 0 || (!rng(20)))
                            {
                                ob->foe = self;
                                ob->attack(self);
                                generic2 = 1;
                            }
                            else
                            {
                                ob->real_team_num = ob->team_num;
                                ob->team_num = self->team_num;
                                if (self->foe == ob)
                                    ob->foe = nullptr;
                                else
                                    ob->foe = self->foe;
                                ob->set_charm_left(static_cast<short>(75 + generic * 25));
                                generic2 = 0;
                            }
                            didheal++;
                        }
                    }
                    if (!didheal)
                        return false;
                    if (self->stats()->name.size())
                        message = self->stats()->name;
                    else if (self->myguy && self->myguy->name.size())
                        message = self->myguy->name;
                    else
                        message = "Thief";
                    if (generic2)
                        tempstr = std::format("{} failed to charm!", message);
                    else
                        tempstr = std::format("{} charmed an opponent!", message);
                    myscreen->do_notify(tempstr.c_str(), self);
                    self->busy += 10;
                }
                break;
            }
        case 4: // poison cloud
        default:
            if (self->busy > 0)
                return false;
            newob = myscreen->level_data.add_ob(Order::FX, FAMILY_CLOUD);
            if (!newob)
                return false;
            self->busy += 5;
            newob->ignore = 1;
            newob->lifetime = 40 + 3 * self->stats()->level;
            newob->center_on(self);
            newob->invisibility_left = 10;
            newob->ani_type = ANI_SPIN;
            newob->team_num = self->team_num;
            newob->stats()->level = self->stats()->level;
            newob->damage = static_cast<float>(self->stats()->level);
            newob->owner = self;
            break;
    }
    return true;
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
        .do_special = thief_do_special,
        .check_special_ai = thief_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = thief_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
    };
    return desc;
}
