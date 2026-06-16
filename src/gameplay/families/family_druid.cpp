/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/summon.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/sim_emit.h>

#include <format>
#include <string>
#include <list>

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

#define BASE_GUY_HP 30

static void druid_set_difficulty(living* self, std::uint32_t level)
{
    apply_difficulty_scaling(self, level, {9.0f, 12.0f, 4.0f, 0.5f});
}

static void druid_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {8, 3, 8, 12, 1});
}

static bool druid_do_special(walker* self)
{
    walker* newob;
    walker* alive;
    walker* tempwalk;
    std::int32_t didheal;
    std::string message;

    switch (self->current_special())
    {
        case 1: // plant tree
            if (self->busy() > 0)
                return false;
            self->stats()->set_magicpoints(self->stats()->magicpoints() + self->stats()->weapon_cost());
            newob = self->fire();
            if (!newob)
                return false;
            self->set_busy(self->busy() + (self->fire_frequency() * 2.0f));
            alive = summon_entity(self, Order::Weapon, FAMILY_TREE);
            if (!alive) return false;
            alive->setxy(newob->xpos(), newob->ypos());
            alive->set_ani_type(ANI_GROW);
            newob->set_dead(1);
            break;
        case 2: // summon faerie
            if (self->busy() > 0)
                return false;
            self->stats()->set_magicpoints(self->stats()->magicpoints() + self->stats()->weapon_cost());
            newob = self->fire();
            if (!newob)
                return false;
            alive = current_game->world->add_ob(Order::Living, FAMILY_FAERIE);
            if (!alive)
                return false;
            alive->set_owner(self);
            alive->set_team_num(self->team_num());
            alive->setxy(newob->xpos(), newob->ypos());
            alive->set_lifetime(50 + self->stats()->level() * 40);
            newob->set_dead(1);
            if (!current_game->world->query_passable(alive->xpos(), alive->ypos(), alive))
            {
                alive->set_dead(1);
                return false;
            }
            self->set_busy(self->busy() + (self->fire_frequency() * 3.0f));
            break;
        case 3: // reveal items
            if (self->busy() > 0)
                return false;
            self->set_view_all(static_cast<short>(self->view_all() + self->stats()->level() * 10));
            self->set_busy(self->busy() + (self->fire_frequency() * 4.0f));
            break;
        case 4: // circle of protection
        default:
            if (self->busy() > 0)
                return false;
            {
                std::int32_t howmany;
                std::list<walker*> newlist = current_game->world->find_friends_in_range(current_game->world->oblist,
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
                            for (auto& uptr : current_game->world->oblist)
                            {
                                walker* ob = uptr.get();
                                if (ob && ob->owner() == newob
                                        && ob->query_order() == Order::Weapon
                                        && ob->family() == FAMILY_CIRCLE_PROTECTION)
                                {
                                    tempwalk = ob;
                                    break;
                                }
                            }
                            if (!tempwalk)
                            {
                                alive = summon_entity(newob, Order::Weapon, FAMILY_CIRCLE_PROTECTION);
                                if (!alive)
                                    return false;
                                didheal++;
                            }
                            else
                            {
                                alive = current_game->world->add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
                                if (!alive)
                                    return false;
                                tempwalk->stats()->set_hitpoints(tempwalk->stats()->hitpoints() + alive->stats()->hitpoints());
                                alive->set_dead(1);
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
                        if (self->team_num() == 0 || self->myguy)
                            og::sim::emit_notification(current_game->sim_events, message);
                        og::sim::emit_sound(current_game->sim_events, SOUND_HEAL);
                    }
                }
                else
                    return false;
            }
            break;
    }
    return true;
}

static const char* const druid_names[] = {"Roland", "Merlin", "Hippy", "Green Thumb", "Treefall", "Rain"};

const FamilyDescriptor& describe_family_druid()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_DRUID,
        .name = "DRUID",
        .short_name = nullptr,
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
        .on_melee_hit = nullptr,
        .pix_filename = "druid.png",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 10,
        .description = "Druids are the magicians  \n"
                       "of nature, and have power \n"
                       "over natural events. They \n"
                       "throw lightning bolts at  \n"
                       "their foes; the fast bolts\n"
                       "have long range.          \n"
                       "\n"
                       "Special: Plant Tree",
        .name_pool = druid_names,
        .name_pool_size = sizeof(druid_names) / sizeof(druid_names[0]),
        .is_playable = true,
        .playable_order = 8,
    };
    return desc;
}
