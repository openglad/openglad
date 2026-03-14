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
#include <openglad/gameplay/summon.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/combat_math.h>

#include <format>
#include <string>
#include <list>
#include <cmath>
#include <algorithm>

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
    std::int32_t temp;
    if (self->stats()->level >= 40)
        temp = 1;
    else
        temp = 40 - self->stats()->level;
    if (!(self->drawcycle % temp))
        self->view_all += 1;
}

static void archmage_hit_response(statistics* stats, walker* foe)
{
    walker* controller = stats->controller();
    controller->busy = 0; // yes, this is a cheat

    std::int32_t possible_specials[NUM_SPECIALS];
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

    if (stats->hitpoints < threshold && possible_specials[1] && current_game->world->rng_.next(3))
    {
        controller->current_special = 1;
        controller->shifter_down = 0;
        controller->busy = 0;
        controller->special();
    }
    else
    {
        if (controller->foe() != foe)
        {
            controller->set_foe(foe);
            foe->set_foe(controller);
            stats->last_distance = stats->current_distance = 15000;
        }
        std::int32_t howmany = 0;
        current_game->world->find_foes_in_range(current_game->world->oblist,
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
                if (current_game->world->rng_.next(2))
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

static void archmage_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {4, 6, 4, 16, 1});
}

static bool archmage_do_special(walker* self)
{
    walker* newob;
    std::int32_t i, j;
    std::int32_t generic, generic2;
    std::int32_t howmany;
    std::int32_t didheal;
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
                    og::sim::emit_notification(current_game->sim_events, "Need 75 Int for Marker!");
                    return false;
                }
                generic = 0;
                for (auto& uptr : current_game->world->oblist)
                {
                    walker* ob = uptr.get();
                    if (ob &&
                            ob->query_order() == Order::FX &&
                            ob->family == FAMILY_MARKER &&
                            ob->owner() == self &&
                            !ob->dead)
                    {
                        ob->dead = 1;
                        ob->death();
                        if (self->team_num == 0 || self->myguy)
                            og::sim::emit_notification(current_game->sim_events, "(Old Marker Removed)");
                        self->busy += 8;
                        generic = 1;
                        break;
                    }
                }
                newob = summon_entity(self, Order::FX, FAMILY_MARKER);
                if (!newob)
                    return false;
                if (self->myguy)
                    newob->lifetime = self->myguy->intelligence / 33;
                else
                    newob->lifetime = (self->stats()->level / 4) + 1;
                newob->ani_type = 2;
                if (self->team_num == 0 || self->myguy)
                {
                    og::sim::emit_notification(current_game->sim_events, "Teleport Marker Placed");
                    message = std::format("({} Uses)", newob->lifetime);
                    og::sim::emit_notification(current_game->sim_events, message);
                }
                self->busy += 8;
                generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[static_cast<int>(self->current_special)]));
                generic /= 2;
                self->stats()->magicpoints -= static_cast<float>(generic);
            }
            else
            {
                og::sim::emit_sound(current_game->sim_events, SOUND_TELEPORT);
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
                std::list<walker*> newlist = current_game->world->find_foes_in_range(current_game->world->oblist,
                                                      generic + 2 * self->stats()->level, &howmany, self);
                if (!howmany)
                    return false;
                if (!self->shifter_down) // normal heartburst
                {
                    generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[2]));
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
                        newob = summon_entity(self, Order::FX, FAMILY_EXPLOSION);
                        if (!newob)
                            return false;
                        newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                        newob->damage = static_cast<float>(generic);
                        newob->center_on(ob);
                        og::sim::emit_sound(current_game->sim_events, SOUND_EXPLODE);
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
                    newob = summon_entity(self, Order::FX, FAMILY_CHAIN);
                    generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[2]));
                    generic /= 2;
                    self->stats()->magicpoints -= static_cast<float>(generic);
                    newob->damage = static_cast<float>(generic);
                    generic = 30000;
                    for (auto* w : newlist)
                    {
                        std::int32_t dist = self->distance_to_ob_center(w);
                        if (generic > dist)
                        {
                            generic = dist;
                            newob->set_leader(w);
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
                        og::sim::emit_notification(current_game->sim_events, "150 Int required to Summon!");
                    return false;
                }
                generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[3]));
                generic /= 2;
                self->stats()->magicpoints -= static_cast<float>(generic);
                newob = current_game->world->add_ob(Order::Living, FAMILY_FIREELEMENTAL);
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
                        if (current_game->world->query_passable(testx, testy, newob))
                        {
                            generic = 1;
                            newob->setxy(testx, testy);
                            newob->stats()->level = (self->stats()->level + 1) / 2;
                            newob->set_difficulty(static_cast<std::uint32_t>(newob->stats()->level));
                            newob->team_num = self->team_num;
                            newob->set_owner(self);
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
                generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[3]));
                if (generic < 100)
                    person = FAMILY_ELF;
                else if (generic < 250)
                {
                    switch (current_game->world->rng_.next(3))
                    {
                        case 0: person = FAMILY_ELF; break;
                        case 1: person = FAMILY_SOLDIER; break;
                        case 2: person = FAMILY_ARCHER; break;
                        default: person = FAMILY_SOLDIER; break;
                    }
                }
                else if (generic < 500)
                {
                    switch (current_game->world->rng_.next(5))
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
                    switch (current_game->world->rng_.next(7))
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
                    switch (current_game->world->rng_.next(9))
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
                newob = current_game->world->add_ob(Order::Living, person);
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
                        if (current_game->world->query_passable(testx, testy, newob))
                        {
                            generic = 1;
                            newob->setxy(testx, testy);
                            newob->stats()->level = (self->stats()->level + 2) / 3;
                            newob->set_difficulty(static_cast<std::uint32_t>(newob->stats()->level));
                            newob->team_num = self->team_num;
                            newob->set_owner(self);
                            newob->lifetime = 100 + 20 * self->stats()->level;
                            newob->stats()->max_hitpoints = 1;
                            newob->stats()->hitpoints = 0;
                            newob->stats()->armor = 0;
                            newob->set_foe(self->foe());
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
        {
            if (self->busy > 0)
                return false;
            std::int32_t mp_after_base_cost = 0;
            std::int32_t controlled_targets = 0;
            {
                const std::int32_t special_cost = self->stats()->special_cost[static_cast<int>(self->current_special)];
                mp_after_base_cost =
                    static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(special_cost));
                std::list<walker*> newlist = current_game->world->find_foes_in_range(current_game->world->oblist,
                                                      80 + 4 * self->stats()->level, &howmany, self);
                if (howmany < 1)
                    return false;
                didheal = 0;
                generic2 = mp_after_base_cost + 10;
                for (auto* ob : newlist)
                {
                    if (generic2 < 10) break;
                    if ((ob->real_team_num == 255) &&
                        (ob->query_order() == Order::Living) &&
                        (ob->charm_left <= 10))
                    {
                        generic2 -= 10;
                        generic = self->stats()->level - ob->stats()->level;
                        if (generic < 0 || (!current_game->world->rng_.next(20)))
                        {
                            ob->real_team_num = ob->team_num;
                            ob->team_num = static_cast<unsigned char>(current_game->world->rng_.next(8));
                            ob->charm_left = (static_cast<short>(compute_charm_duration(generic, current_game->world->rng_)));
                        }
                        else
                        {
                            ob->real_team_num = ob->team_num;
                            ob->team_num = self->team_num;
                            ob->set_foe(nullptr);
                            ob->charm_left = (static_cast<short>(compute_charm_duration(generic, current_game->world->rng_)));
                        }
                        didheal++;
                        controlled_targets++;
                    }
                }
            }
            if (!didheal)
                return false;
            tempstr = std::format("{} has controlled {} men", entity_display_name(self, "ArchMage"), didheal);
            og::sim::emit_notification(current_game->sim_events, tempstr);
            // Target budget starts at mp_after_base_cost + 10, so the first
            // control is covered by base special cost and extras cost 10 MP each.
            if (mp_after_base_cost > 0 && controlled_targets > 1)
            {
                const std::int32_t additional_spend = (controlled_targets - 1) * 10;
                self->stats()->magicpoints -= static_cast<float>(std::min(additional_spend, mp_after_base_cost));
            }
            self->busy += 10;
            break;
        }
        default:
            break;
    }
    return true;
}

static const char* const mage_names[] = {"Gandalf", "Saruman", "Radagast", "Alatar", "Pallando", "Raistlin", "Fizban", "Mordenkainen", "Merlin", "Harry", "Manannan", "Mordack", "Jace"};

const FamilyDescriptor& describe_family_archmage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ARCHMAGE,
        .name = "ARCHMAGE",
        .short_name = nullptr,
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
        .on_melee_hit = nullptr,
        .pix_filename = "archmage.png",
        .animation_type = FAMILY_ANIM_MAGE,
        .ai_line_of_sight = 10,
        .description = "An Archmage takes the     \n"
                       "learnings of the Magi one \n"
                       "step further, possessing  \n"
                       "extraordinary firepower at\n"
                       "the expense of physical   \n"
                       "weakness.                 \n"
                       "\n"
                       "Special: Teleport",
        .name_pool = mage_names,
        .name_pool_size = sizeof(mage_names) / sizeof(mage_names[0]),
        .is_playable = false,
        .playable_order = 999,
    };
    return desc;
}
