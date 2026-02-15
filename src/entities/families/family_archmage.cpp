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
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/core/stats.h>
#include <openglad/core/combat_math.h>

#include <format>
#include <string>
#include <list>
#include <cmath>

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

#define BASE_GUY_HP 30

static bool archmage_handle_teleport(walker* self)
{
    self->ani_type = ANI_TELE_IN;
    self->cycle = 0;
    self->teleport();
    return true;
}

static bool archmage_on_fire_weapon(walker* self, walker* weapon)
{
    // ArchMage gets 1/20th of 'extra' magic for more damage
    float extra = self->stats()->magicpoints / 20;
    self->stats()->magicpoints -= extra;
    weapon->damage += extra;
    return true;
}

static void archmage_on_act_living(living* self)
{
    // Archmage gets bonus viewing periodically based on level
    Sint32 temp;
    if (self->stats()->level >= 40)
        temp = 1;
    else
        temp = 40 - self->stats()->level;
    if (!(self->drawcycle % temp))
        self->view_all += 1;
}

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

static bool archmage_do_special(walker* self)
{
    walker* newob;
    Sint32 i, j;
    Sint32 generic, generic2;
    Sint32 howmany;
    Sint32 didheal;
    char person;
    std::string message, tempstr;

    switch (self->current_special)
    {
        case 1: // teleport
            if (self->ani_type == ANI_TELE_OUT || self->ani_type == ANI_TELE_IN)
                return false;
            if (self->shifter_down) // leave/remove a marker
            {
                if (self->busy > 0)
                    return false;
                if (self->myguy && (self->myguy->intelligence < 75))
                {
                    og::sim::emit_notification("Need 75 Int for Marker!");
                    return false;
                }
                generic = 0;
                for (auto& uptr : myscreen->level_data.oblist)
                {
                    walker* ob = uptr.get();
                    if (ob &&
                            ob->query_order() == Order::FX &&
                            ob->query_family() == FAMILY_MARKER &&
                            ob->owner == self &&
                            !ob->dead)
                    {
                        ob->dead = 1;
                        ob->death();
                        if (self->team_num == 0 || self->myguy)
                            og::sim::emit_notification("(Old Marker Removed)");
                        self->busy += 8;
                        generic = 1;
                        break;
                    }
                }
                newob = myscreen->level_data.add_ob(Order::FX, FAMILY_MARKER);
                if (!newob)
                    return false;
                newob->owner = self;
                newob->center_on(self);
                if (self->myguy)
                    newob->lifetime = self->myguy->intelligence / 33;
                else
                    newob->lifetime = (self->stats()->level / 4) + 1;
                newob->ani_type = 2;
                if (self->team_num == 0 || self->myguy)
                {
                    og::sim::emit_notification("Teleport Marker Placed");
                    message = std::format("({} Uses)", newob->lifetime);
                    og::sim::emit_notification(message);
                }
                self->busy += 8;
                generic = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[static_cast<int>(self->current_special)]));
                generic /= 2;
                self->stats()->magicpoints -= static_cast<float>(generic);
            }
            else
            {
                og::sim::emit_sound(SOUND_TELEPORT);
                self->ani_type = ANI_TELE_OUT;
                self->cycle = 0;
            }
            break;
        case 2: // heartburst / chain lightning
            if (self->busy > 0)
                return false;
            if (self->shifter_down)
            {
                if (self->myguy)
                    generic = 200 + self->myguy->intelligence / 2;
                else
                    generic = 200 + self->stats()->level * 5;
            }
            else
                generic = 80;
            {
                std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                      generic + 2 * self->stats()->level, &howmany, self);
                if (!howmany)
                    return false;
                if (!self->shifter_down) // normal heartburst
                {
                    generic = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[2]));
                    generic /= 2;
                    generic /= howmany;
                    if (self->myguy)
                    {
                        self->myguy->total_shots += howmany;
                        self->myguy->scen_shots = static_cast<short>(self->myguy->scen_shots + howmany);
                    }
                    self->busy += 5;
                    for (auto* ob : newlist)
                    {
                        newob = myscreen->level_data.add_ob(Order::FX, FAMILY_EXPLOSION);
                        if (!newob)
                            return false;
                        newob->owner = self;
                        newob->team_num = self->team_num;
                        newob->stats()->level = self->stats()->level;
                        newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                        newob->damage = static_cast<float>(generic);
                        newob->center_on(ob);
                        og::sim::emit_sound(SOUND_EXPLODE);
                        newob->ani_type = ANI_EXPLODE;
                        newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                        newob->skip_exit = 100;
                        self->stats()->magicpoints -= static_cast<float>(generic);
                    }
                }
                else // chain lightning
                {
                    self->busy += 5;
                    if (self->myguy)
                    {
                        self->myguy->total_shots++;
                        self->myguy->scen_shots++;
                    }
                    newob = myscreen->level_data.add_ob(Order::FX, FAMILY_CHAIN);
                    newob->center_on(self);
                    newob->owner = self;
                    newob->stats()->level = self->stats()->level;
                    newob->team_num = self->team_num;
                    generic = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[2]));
                    generic /= 2;
                    self->stats()->magicpoints -= static_cast<float>(generic);
                    newob->damage = static_cast<float>(generic);
                    generic = 30000;
                    for (auto* w : newlist)
                    {
                        Sint32 dist = self->distance_to_ob_center(w);
                        if (generic > dist)
                        {
                            generic = dist;
                            newob->leader = w;
                        }
                    }
                }
            }
            break;
        case 3: // summon image / elemental
            if (self->busy > 0)
                return false;
            if (self->shifter_down) // true summoning
            {
                if (self->myguy && self->myguy->intelligence < 150)
                {
                    if (self->user != -1)
                        og::sim::emit_notification("150 Int required to Summon!");
                    return false;
                }
                generic = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[3]));
                generic /= 2;
                self->stats()->magicpoints -= static_cast<float>(generic);
                newob = myscreen->level_data.add_ob(Order::Living, FAMILY_FIREELEMENTAL);
                if (!newob)
                    return false;
                generic = 0;
                for (i = -1; i <= 1; i++)
                    for (j = -1; j <= 1; j++)
                    {
                        if ((i == 0 && j == 0) || (generic))
                            continue;
                        float testx = static_cast<float>(self->xpos + ((newob->sizex + 1) * i));
                        float testy = static_cast<float>(self->ypos + ((newob->sizey + 1) * j));
                        if (myscreen->query_passable(testx, testy, newob))
                        {
                            generic = 1;
                            newob->setxy(testx, testy);
                            newob->stats()->level = (self->stats()->level + 1) / 2;
                            newob->set_difficulty(static_cast<Uint32>(newob->stats()->level));
                            newob->team_num = self->team_num;
                            newob->owner = self;
                            newob->lifetime = 200 + 60 * self->stats()->level;
                        }
                    }
                if (!generic)
                {
                    newob->dead = 1;
                    return false;
                }
                self->busy += 15;
            }
            else // illusion summoning
            {
                generic = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[3]));
                if (generic < 100)
                    person = FAMILY_ELF;
                else if (generic < 250)
                {
                    switch (rng(3))
                    {
                        case 0: person = FAMILY_ELF; break;
                        case 1: person = FAMILY_SOLDIER; break;
                        case 2: person = FAMILY_ARCHER; break;
                        default: person = FAMILY_SOLDIER; break;
                    }
                }
                else if (generic < 500)
                {
                    switch (rng(5))
                    {
                        case 0: person = FAMILY_ELF; break;
                        case 1: person = FAMILY_SOLDIER; break;
                        case 2: person = FAMILY_ARCHER; break;
                        case 3: person = FAMILY_ORC; break;
                        case 4: person = FAMILY_SKELETON; break;
                        default: person = FAMILY_ARCHER; break;
                    }
                }
                else if (generic < 1000)
                {
                    switch (rng(7))
                    {
                        case 0: person = FAMILY_ELF; break;
                        case 1: person = FAMILY_SOLDIER; break;
                        case 2: person = FAMILY_ARCHER; break;
                        case 3: person = FAMILY_ORC; break;
                        case 4: person = FAMILY_SKELETON; break;
                        case 5: person = FAMILY_DRUID; break;
                        case 6: person = FAMILY_CLERIC; break;
                        default: person = FAMILY_ARCHER; break;
                    }
                }
                else
                {
                    switch (rng(9))
                    {
                        case 0: person = FAMILY_ELF; break;
                        case 1: person = FAMILY_SOLDIER; break;
                        case 2: person = FAMILY_ARCHER; break;
                        case 3: person = FAMILY_ORC; break;
                        case 4: person = FAMILY_SKELETON; break;
                        case 5: person = FAMILY_DRUID; break;
                        case 6: person = FAMILY_CLERIC; break;
                        case 7: person = FAMILY_FIREELEMENTAL; break;
                        case 8: person = FAMILY_BIG_ORC; break;
                        default: person = FAMILY_ARCHER; break;
                    }
                }
                newob = myscreen->level_data.add_ob(Order::Living, person);
                if (!newob)
                    return false;
                generic = 0;
                for (i = -1; i <= 1; i++)
                    for (j = -1; j <= 1; j++)
                    {
                        if ((i == 0 && j == 0) || (generic))
                            continue;
                        float testx = static_cast<float>(self->xpos + ((newob->sizex + 1) * i));
                        float testy = static_cast<float>(self->ypos + ((newob->sizey + 1) * j));
                        if (myscreen->query_passable(testx, testy, newob))
                        {
                            generic = 1;
                            newob->setxy(testx, testy);
                            newob->stats()->level = (self->stats()->level + 2) / 3;
                            newob->set_difficulty(static_cast<Uint32>(newob->stats()->level));
                            newob->team_num = self->team_num;
                            newob->owner = self;
                            newob->lifetime = 100 + 20 * self->stats()->level;
                            newob->stats()->max_hitpoints = 1;
                            newob->stats()->hitpoints = 0;
                            newob->stats()->armor = 0;
                            newob->foe = self->foe;
                            newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                            newob->stats()->name = "Phantom";
                        }
                    }
                if (!generic)
                {
                    newob->dead = 1;
                    return false;
                }
                self->busy += 15;
            }
            break;
        case 4: // mind control
            if (self->busy > 0)
                return false;
            {
                std::list<walker*> newlist = myscreen->find_foes_in_range(myscreen->level_data.oblist,
                                                      80 + 4 * self->stats()->level, &howmany, self);
                if (howmany < 1)
                    return false;
                didheal = 0;
                generic2 = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[static_cast<int>(self->current_special)])) + 10;
                for (auto* ob : newlist)
                {
                    if (generic2 < 10) break;
                    if ((ob->real_team_num == 255) &&
                        (ob->query_order() == Order::Living) &&
                        (ob->charm_left() <= 10))
                    {
                        generic2 -= 10;
                        generic = self->stats()->level - ob->stats()->level;
                        if (generic < 0 || (!rng(20)))
                        {
                            ob->real_team_num = ob->team_num;
                            ob->team_num = static_cast<unsigned char>(rng(8));
                            ob->set_charm_left(static_cast<short>(compute_charm_duration(generic, *ctx().rng)));
                        }
                        else
                        {
                            ob->real_team_num = ob->team_num;
                            ob->team_num = self->team_num;
                            ob->foe = nullptr;
                            ob->set_charm_left(static_cast<short>(compute_charm_duration(generic, *ctx().rng)));
                        }
                        didheal++;
                    }
                }
            }
            if (!didheal)
                return false;
            if (self->stats()->name.size())
                message = self->stats()->name;
            else if (self->myguy && self->myguy->name.size())
                message = self->myguy->name;
            else
                message = "ArchMage";
            tempstr = std::format("{} has controlled {} men", message, didheal);
            og::sim::emit_notification(tempstr);
            generic2 = static_cast<Sint32>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[static_cast<int>(self->current_special)]));
            if (generic2 > 0)
            {
                while ((didheal > 0) && (generic2 >= 10))
                {
                    if (generic2 > 10)
                        generic2 -= 10;
                    didheal--;
                }
            }
            self->busy += 10;
            break;
        default:
            break;
    }
    return true;
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
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "SOMEONE DIED",
        .do_special = archmage_do_special,
        .check_special_ai = nullptr,
        .hit_response = archmage_hit_response,
        .set_difficulty = nullptr,
        .level_up = archmage_level_up,
        .on_death = nullptr,
        .on_act_living = archmage_on_act_living,
        .on_shoved = nullptr,
        .on_fire_weapon = archmage_on_fire_weapon,
        .handle_teleport = archmage_handle_teleport,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
    };
    return desc;
}
