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
#include <openglad/entities/summon.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/core/stats.h>
#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/data/gparser.h>

#include <openglad/data/gparser.h>

#include <format>
#include <string>
#include <list>

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

// rng/config wrappers removed: use SimEntity fields sim_rng/sim_config directly

static void cleric_customize_weapon(walker* self, walker* weapon)
{
    weapon->ani_type = ANI_GLOWGROW;
    weapon->lifetime += (self->stats()->level * 110);
}

static void cleric_on_shoved(walker* self)
{
    self->current_special = 1; // healing
    self->special();
}

#define BASE_GUY_HP 30

static bool cleric_check_special_ai(living* self)
{
    if (self->current_special == 1) // healing
    {
        std::int32_t howmany = 0;
        og::gameplay::current_game->world->find_friends_in_range(og::gameplay::current_game->world->oblist,
                                        60, &howmany, self);
        if (howmany > 1)
        {
            self->shifter_down = 0; // we're HEALING
            return true;
        }
        else if (self->stats()->magicpoints >= (self->stats()->max_magicpoints / 2))
        {
            self->shifter_down = 1; // do mace
            return true;
        }
        return false;
    }
    return true;
}

static void cleric_set_difficulty(living* self, std::uint32_t level)
{
    apply_difficulty_scaling(self, level, {9.0f, 12.0f, 4.0f, 0.5f});
}

static bool cleric_do_special(walker* self)
{
    walker* newob;
    walker* alive;
    std::int32_t generic;
    float targetx, targety;
    std::uint32_t distance;
    std::int32_t didheal;
    std::string message;

    switch (self->current_special)
    {
        case 1: // heal / mystic mace
            if (!self->shifter_down) // normal heal
            {
                std::int32_t howmany;
                std::list<walker*> newlist = og::gameplay::current_game->world->find_friends_in_range(og::gameplay::current_game->world->oblist,
                          60, &howmany, self);
                didheal = 0;
                if (howmany > 1)
                {
                    for (auto* w : newlist)
                    {
                        newob = w;
                        if (newob->stats()->hitpoints < newob->stats()->max_hitpoints &&
                                newob != self)
                        {
                            HealResult heal = compute_heal_amount(static_cast<std::int32_t>(self->stats()->magicpoints), self->stats()->level, og::gameplay::current_game->world->rng_);
                            generic = heal.amount;
                            std::int32_t cost = heal.cost;
                            if (self->stats()->magicpoints < static_cast<float>(cost))
                            {
                                generic -= static_cast<std::int32_t>(self->stats()->magicpoints);
                                cost -= static_cast<std::int32_t>(self->stats()->magicpoints);
                            }
                            if (generic <= 0 || cost <= 0)
                                break;
                            newob->stats()->hitpoints += static_cast<float>(generic);
                            self->stats()->magicpoints -= static_cast<float>(cost);
                            if (self->myguy)
                                self->myguy->exp += exp_from_action(ExpAction::Heal, self, newob, static_cast<short>(generic));
                            didheal++;
                            self->do_heal_effects(self, newob, static_cast<short>(generic));
                        }
                    }
                    if (!didheal)
                        return false;
                    else
                    {
                        if (self->sim_config && self->sim_config->is_on("effects", "heal_numbers"))
                        {
                            if (didheal == 1)
                                message = "Cleric healed 1 man!";
                            else
                                message = std::format("Cleric healed {} men!", didheal);
                            if (self->team_num == 0 || self->myguy)
                                og::sim::emit_notification(og::gameplay::current_game->sim_events, message);
                        }
                        og::sim::emit_sound(og::gameplay::current_game->sim_events, SOUND_HEAL);
                    }
                }
                else
                    return false;
                break;
            }
            else // mystic mace
            {
                if (self->busy > 0)
                    return false;
                if (self->myguy && self->myguy->intelligence < 50)
                {
                    if (self->user != -1)
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, "50 Int required for Mystic Mace!");
                    return false;
                }
                if (self->myguy)
                {
                    self->myguy->total_shots++;
                    self->myguy->scen_shots++;
                }
                newob = summon_entity(self, Order::FX, FAMILY_MAGIC_SHIELD);
                if (!newob)
                    return false;
                newob->ani_type = 1;
                generic = static_cast<std::int32_t>(self->stats()->magicpoints - static_cast<float>(self->stats()->special_cost[static_cast<int>(self->current_special)]));
                generic /= 2;
                newob->lifetime = 100 + generic;
                newob->stats()->hitpoints += static_cast<float>(generic) / 2.0f;
                newob->damage += static_cast<float>(generic) / 4.0f;
                self->stats()->magicpoints -= static_cast<float>(generic);
                self->busy += 5;
                break;
            }
        case 2: // raise skeletons / turn undead
            if (self->shifter_down)
            {
                if (self->busy > 0)
                    return false;
                if (self->myguy && self->myguy->intelligence < 60)
                {
                    if (self->team_num == 0 || self->myguy)
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, "You need 60 Int to Turn Undead");
                    self->busy += 5;
                    return false;
                }
                if ((generic = self->turn_undead(4 * self->stats()->level, self->stats()->level)) == -1)
                    return false;
                if (self->myguy && generic)
                {
                    self->myguy->exp += exp_from_action(ExpAction::TurnUndead, self, nullptr, static_cast<short>(generic));
                    if (self->team_num == 0 || self->myguy)
                    {
                        message = std::format("{} turned {} undead.", self->myguy->name, generic);
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, message);
                    }
                }
                og::sim::emit_sound(og::gameplay::current_game->sim_events, SOUND_HEAL);
            }
            else
            {
                newob = og::gameplay::current_game->world->find_nearest_blood(self);
                if (newob)
                {
                    targetx = newob->xpos;
                    targety = newob->ypos;
                    distance = static_cast<std::uint32_t>(self->distance_to_ob(newob));
                    if (og::gameplay::current_game->world->query_passable(targetx, targety, newob) && distance < 60)
                    {
                        alive = self->do_summon(FAMILY_SKELETON, 125 + (self->stats()->level * 40));
                        if (!alive)
                            return false;
                        alive->team_num = self->team_num;
                        alive->stats()->level = og::gameplay::current_game->world->rng_.next(self->stats()->level) + 1;
                        alive->set_difficulty(static_cast<std::uint32_t>(alive->stats()->level));
                        alive->setxy(newob->xpos, newob->ypos);
                        alive->owner = self;
                        newob->dead = 1;
                        if (self->myguy)
                            self->myguy->exp += exp_from_action(ExpAction::RaiseSkeleton, self, alive, 0);
                    }
                    else
                        return false;
                }
                else
                    return false;
            }
            break;
        case 3: // raise ghosts / turn undead high
            if (self->shifter_down)
            {
                if (self->busy > 0)
                    return false;
                if (self->myguy && self->myguy->intelligence < 60)
                {
                    if (self->team_num == 0 || self->myguy)
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, "You need 60 Int to Turn Undead");
                    self->busy += 5;
                    return false;
                }
                if ((generic = self->turn_undead(4 * self->stats()->level, self->stats()->level)) == -1)
                    return false;
                if (self->myguy && generic)
                {
                    self->myguy->exp += exp_from_action(ExpAction::TurnUndead, self, nullptr, static_cast<short>(generic));
                    if (self->team_num == 0 || self->myguy)
                    {
                        message = std::format("{} turned {} undead.", self->myguy->name, generic);
                        og::sim::emit_notification(og::gameplay::current_game->sim_events, message);
                    }
                }
                og::sim::emit_sound(og::gameplay::current_game->sim_events, SOUND_HEAL);
            }
            else
            {
                newob = og::gameplay::current_game->world->find_nearest_blood(self);
                if (newob)
                {
                    targetx = newob->xpos;
                    targety = newob->ypos;
                    distance = static_cast<std::uint32_t>(self->distance_to_ob(newob));
                    if (og::gameplay::current_game->world->query_passable(targetx, targety, newob) && distance < 30)
                    {
                        alive = self->do_summon(FAMILY_GHOST, 150 + (self->stats()->level * 40));
                        if (!alive)
                            return false;
                        alive->stats()->level = og::gameplay::current_game->world->rng_.next(self->stats()->level) + 1;
                        alive->set_difficulty(static_cast<std::uint32_t>(alive->stats()->level));
                        alive->team_num = self->team_num;
                        alive->setxy(newob->xpos, newob->ypos);
                        alive->owner = self;
                        newob->dead = 1;
                        if (self->myguy)
                            self->myguy->exp += exp_from_action(ExpAction::RaiseGhost, self, alive, 0);
                    }
                    else
                        return false;
                }
                else
                    return false;
            }
            break;
        case 4: // resurrect
        default:
            newob = og::gameplay::current_game->world->find_nearest_blood(self);
            if (newob)
            {
                targetx = newob->xpos;
                targety = newob->ypos;
                distance = self->distance_to_ob(newob);
                if (og::gameplay::current_game->world->query_passable(targetx, targety, newob) && distance < 30)
                {
                    if (self->is_friendly(newob))
                    {
                        alive = og::gameplay::current_game->world->add_ob(Order::Living, newob->stats()->old_family);
                        if (!alive)
                            return false;
                        newob->transfer_stats(alive);
                        alive->stats()->hitpoints = (alive->stats()->max_hitpoints) / 2;
                        self->do_heal_effects(self, alive, static_cast<short>(alive->stats()->max_hitpoints / 2.0f));
                        alive->team_num = newob->team_num;
                        if (self->myguy)
                        {
                            unsigned short exp_loss = exp_from_action(ExpAction::ResurrectPenalty, self, newob, 0);
                            if (self->myguy->exp >= exp_loss)
                                self->myguy->exp -= exp_loss;
                            else
                                self->myguy->exp = 0;
                        }
                    }
                    else
                    {
                        alive = self->do_summon(FAMILY_GHOST, 200);
                        if (!alive)
                            return false;
                        alive->team_num = self->team_num;
                        alive->stats()->level = og::gameplay::current_game->world->rng_.next(self->stats()->level) + 1;
                        alive->set_difficulty(static_cast<std::uint32_t>(alive->stats()->level));
                        alive->owner = self;
                    }
                    alive->setxy(newob->xpos, newob->ypos);
                    newob->dead = 1;
                    if (self->myguy)
                        self->myguy->exp += exp_from_action(ExpAction::Resurrect, self, alive, 0);
                }
                else
                    return false;
            }
            else
                return false;
            break;
    }
    return true;
}

static const char* const cleric_names[] = {"Tuck", "Brother", "Pater", "Drake", "Friar", "Francis", "John Paul", "Medic"};

const FamilyDescriptor& describe_family_cleric()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_CLERIC,
        .name = "CLERIC",
        .short_name = nullptr,
        .base_stats = {6, 7, 6, 14, 7, 1},
        .hiring_cost = 400,
        .derived_bonuses = {BASE_GUY_HP+90, 0, 12, 0, 0, 0, 2, 7.5f},
        .stat_costs = {15, 15, 9, 6, 50, 200},
        .special_cost = {5000, 2, 20, 50, 150, 5000},
        .weapon_cost = 8,
        .default_weapon = FAMILY_GLOW,
        .init_bit_flags = 0,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HEAL", "RAISE UNDEAD", "RAISE GHOST", "RESURRECT", "NONE"},
        .alternate_names = {"NONE", "MYSTIC MACE", "TURN UNDEAD", "TURN UNDEAD", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "CLERIC DIED",
        .do_special = cleric_do_special,
        .check_special_ai = cleric_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = cleric_set_difficulty,
        .level_up = nullptr,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = cleric_on_shoved,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = cleric_customize_weapon,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "cleric.pix",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 4,
        .description = "Clerics, like mages, are  \n"
                       "slow, but have a stronger \n"
                       "hand-to-hand attack.      \n"
                       "Clerics possess abilities \n"
                       "related to healing and    \n"
                       "interaction with the dead.\n"
                       "\n"
                       "Special: Heal",
        .name_pool = cleric_names,
        .name_pool_size = sizeof(cleric_names) / sizeof(cleric_names[0]),
        .is_playable = true,
        .playable_order = 5,
    };
    return desc;
}
