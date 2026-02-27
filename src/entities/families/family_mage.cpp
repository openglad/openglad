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
#include <openglad/entities/summon.h>
#include <openglad/data/level_data.h>
#include <openglad/core/stats.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/entities/foe_query.h>

#include <format>
#include <string>
#include <list>
#include <cmath>

static bool mage_handle_teleport(walker* self)
{
    self->ani_type = ANI_TELE_IN;
    self->cycle = 0;
    self->teleport();
    return true;
}

#define BASE_GUY_HP 30

static bool mage_check_special_ai(living* self)
{
    std::int32_t howmany = count_foes_in_range(self, 110);
    return howmany < 1 || howmany > 3;
}

static void mage_hit_response(statistics* stats, walker* foe)
{
    walker* controller = stats->controller;
    float threshold;
    if (controller->myguy)
        threshold = (3.0f * stats->max_hitpoints) / 5.0f;
    else
        threshold = (3.0f * stats->max_hitpoints) / 8.0f;

    std::int32_t possible_specials[NUM_SPECIALS];
    for (int i = 0; i < NUM_SPECIALS; i++)
        possible_specials[i] = 0;
    for (int i = 0; i <= (stats->level + 2) / 3; i++)
        if (i < NUM_SPECIALS && stats->magicpoints >= stats->special_cost[i])
            possible_specials[i] = 1;

    if (stats->hitpoints < threshold && possible_specials[1])
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
    }
}

static void mage_set_difficulty(living* self, std::uint32_t level)
{
    apply_difficulty_scaling(self, level, {7.0f, 14.0f, 3.0f, 0.5f});
}

static void mage_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {4, 6, 4, 16, 1});
}

static bool mage_do_special(walker* self)
{
    walker* newob;
    walker* alive;
    std::int32_t tempx, tempy;
    std::int32_t generic;
    std::int32_t howmany;
    std::string message;

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
                    if (self->user != -1)
                        og::sim::emit_notification(current_game->sim_events, "Need 75 Int for Marker!");
                    return false;
                }
                // Remove a marker, if present
                generic = 0;
                for (auto& uptr : current_game->world->oblist)
                {
                    walker* ob = uptr.get();
                    if (ob &&
                            ob->query_order() == Order::FX &&
                            ob->family == FAMILY_MARKER &&
                            ob->owner == self &&
                            !ob->dead)
                    {
                        ob->dead = 1;
                        ob->death();
                        if ((self->team_num == 0 || self->myguy) && self->user != -1)
                            og::sim::emit_notification(current_game->sim_events, "(Old Marker Removed)");
                        self->busy += 8;
                        break;
                    }
                }
                generic = 0; // force new placement, for now
                if (!generic) // didn't remove a marker, so place one
                {
                    newob = summon_entity(self, Order::FX, FAMILY_MARKER);
                    if (!newob)
                        return false;
                    if (self->myguy)
                        newob->lifetime = self->myguy->intelligence / 33;
                    else
                        newob->lifetime = (self->stats()->level / 4) + 1;
                    newob->ani_type = ANI_SPIN;
                    if ((self->team_num == 0 || self->myguy) && self->user != -1)
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
            }
            else
            {
                og::sim::emit_sound(current_game->sim_events, SOUND_TELEPORT);
                self->ani_type = ANI_TELE_OUT;
                self->cycle = 0;
            }
            break;
        case 2: // starburst
        {
            tempx = static_cast<std::int32_t>(self->lastx);
            tempy = static_cast<std::int32_t>(self->lasty);
            generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[static_cast<int>(self->current_special)]));
            if (generic > 0)
            {
                generic = generic / 15;
                self->stats()->magicpoints -= static_cast<float>(generic);
            }
            else
                generic = 0;
            self->stats()->magicpoints += 8.0f * static_cast<float>(self->stats()->weapon_cost);
            for (std::int32_t i = -1; i < 2; i++)
                for (std::int32_t j = -1; j < 2; j++)
                {
                    if (i || j)
                    {
                        self->lastx = static_cast<float>(i);
                        self->lasty = static_cast<float>(j);
                        newob = self->fire();
                        if (newob)
                        {
                            newob->damage += static_cast<float>(generic);
                            newob->lineofsight += (generic / 3);
                            if (newob->lastx != 0.0f)
                                newob->lastx /= std::fabs(newob->lastx);
                            if (newob->lasty != 0.0f)
                                newob->lasty /= std::fabs(newob->lasty);
                        }
                    }
                }
            self->lastx = static_cast<float>(tempx);
            self->lasty = static_cast<float>(tempy);
            break;
        }
        case 3: // freeze time
            if (self->team_num == 0 || self->myguy)
            {
                current_game->world->enemy_freeze += 20 + 11 * self->stats()->level;
                og::sim::emit_event(current_game->sim_events, og::sim::EventKind::SetPalette, 1);
            }
            else
            {
                generic = 5 + 2 * self->stats()->level;
                if (generic > 50)
                    generic = 50;
                message = std::format("TIME IS FROZEN! ({} rounds)", generic);
                og::sim::emit_notification(current_game->sim_events, message, 2);
                og::sim::emit_event(current_game->sim_events, og::sim::EventKind::RequestRedraw);
                std::list<walker*> newlist = current_game->world->find_friends_in_range(
                              current_game->world->oblist, 30000, &howmany, self);
                for (auto* w : newlist)
                {
                    if (w)
                        w->bonus_rounds = static_cast<short>(w->bonus_rounds + generic);
                }
            }
            break;
        case 4: // energy wave
            newob = self->fire();
            if (!newob)
                return false;
            alive = current_game->world->add_ob(Order::Weapon, FAMILY_WAVE);
            alive->center_on(newob);
            alive->owner = self;
            alive->stats()->level = self->stats()->level;
            alive->lastx = newob->lastx;
            alive->lasty = newob->lasty;
            newob->dead = 1;
            break;
        case 5:
        default: // heartburst
        {
            std::list<walker*> newlist = current_game->world->find_foes_in_range(current_game->world->oblist,
                                                  80 + 2 * self->stats()->level, &howmany, self);
            if (!howmany)
                return false;
            generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[5]));
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
                newob->damage = static_cast<float>(generic);
                newob->center_on(ob);
                og::sim::emit_sound(current_game->sim_events, SOUND_EXPLODE);
                newob->ani_type = ANI_EXPLODE;
                newob->stats()->set_bit_flags(BIT_MAGICAL, 1);
                newob->skip_exit = 100;
                self->stats()->magicpoints -= static_cast<float>(generic);
            }
            break;
        }
    }
    return true;
}

static short mage_promotion_level(int old_level)
{
    return static_cast<short>((old_level - 6) / 2 + 1);
}

static const char* const mage_names[] = {"Gandalf", "Saruman", "Radagast", "Alatar", "Pallando", "Raistlin", "Fizban", "Mordenkainen", "Merlin", "Harry", "Manannan", "Mordack", "Jace"};

const FamilyDescriptor& describe_family_mage()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_MAGE,
        .name = "MAGE",
        .short_name = nullptr,
        .base_stats = {4, 6, 4, 16, 5, 1},
        .hiring_cost = 450,
        .derived_bonuses = {BASE_GUY_HP+60, 0, 4, 0, 0, 0, 2, 4},
        .stat_costs = {20, 15, 16, 6, 50, 200},
        .special_cost = {5000, 15, 60, 500, 70, 100},
        .weapon_cost = 5,
        .default_weapon = FAMILY_FIREBALL,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "TELEPORT", "WARP SPACE", "FREEZE TIME", "ENERGY WAVE", "HEARTBURST"},
        .alternate_names = {"NONE", "TELEPORT MARKER", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = FAMILY_ARCHMAGE,
        .promotion_level_req = 6,
        .promotion_new_level = mage_promotion_level,
        .death_message = "MAGE DIED",
        .do_special = mage_do_special,
        .check_special_ai = mage_check_special_ai,
        .hit_response = mage_hit_response,
        .set_difficulty = mage_set_difficulty,
        .level_up = mage_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = mage_handle_teleport,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "mage.pix",
        .animation_type = FAMILY_ANIM_MAGE,
        .ai_line_of_sight = 7,
        .description = "Mages are slow, can't     \n"
                       "stand much damage, and are\n"
                       "horrible at hand-to-hand  \n"
                       "combat, but their magical \n"
                       "fireballs pack a big      \n"
                       "punch.                    \n"
                       "\n"
                       "Special: Teleport",
        .name_pool = mage_names,
        .name_pool_size = sizeof(mage_names) / sizeof(mage_names[0]),
        .is_playable = true,
        .playable_order = 4,
    };
    return desc;
}
