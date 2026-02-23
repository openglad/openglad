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
#include <openglad/entities/guy.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/summon.h>
#include <openglad/data/level_data.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/legacy/soundob.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_emit.h>
#include <openglad/entities/foe_query.h>

#include <format>
#include <string>
#include <list>

#define BASE_GUY_HP 30

static bool thief_check_special_ai(living* self)
{
    if (self->current_special == 1) // drop bomb
    {
        if (self->foe)
        {
            std::uint32_t distance = static_cast<std::uint32_t>(self->distance_to_ob(self->foe));
            if (distance < 130 && distance > 35)
                return false;
        }
        else
        {
            return count_foes_in_range(self, 110) >= 3;
        }
        return true; // fallthrough for foe case when distance is acceptable
    }
    else if (self->current_special == 3)
    {
        std::uint32_t myrange;
        if (!self->shifter_down)
            myrange = 80 + 4 * self->stats()->level;
        else
            myrange = 16 + 4 * self->stats()->level;

        return count_foes_in_range(self, myrange) >= 1;
    }
    return true; // default: go for it
}

static void thief_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {4, 12, 4, 8, 1});
}

static bool thief_do_special(walker* self)
{
    walker* newob;
    std::string message, tempstr;

    switch (self->current_special)
    {
        case 1: // drop bomb
            newob = self->sim_level->add_ob(Order::FX, FAMILY_BOMB, 1);
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
                if (self->user == -1)
                {
                    std::int32_t tempx = static_cast<std::int32_t>(self->sim_rng->next(3)) - 1;
                    std::int32_t tempy = static_cast<std::int32_t>(self->sim_rng->next(3)) - 1;
                    if ((tempx == 0) && (tempy == 0))
                        tempx = 1;
                    self->stats()->force_command(COMMAND_WALK, 20, tempx, tempy);
                }
            }
            break;
        case 2: // cloak
            self->invisibility_left = static_cast<short>(self->invisibility_left + 20 + static_cast<std::int32_t>(self->sim_rng->next(20)) * self->stats()->level);
            break;
        case 3: // taunt / charm
            if (!self->shifter_down) // normal taunt
            {
                if (self->busy > 0)
                    return false;
                for_each_foe_in_range(self, 80 + 4 * self->stats()->level, [self](walker* ob) {
                    if (self->sim_rng->next(self->stats()->level) >= self->sim_rng->next(ob->stats()->level))
                    {
                        ob->foe = self;
                        ob->leader = self;
                        if (ob->act_type != ACT_CONTROL)
                            ob->stats()->force_command(COMMAND_FOLLOW, 10 + self->sim_rng->next(self->stats()->level), 0, 0);
                    }
                });
                message = std::format("{}: 'Nyah Nyah!'", entity_display_name(self, "THIEF"));
                og::sim::emit_notification(self->sim_events, message);
                self->busy += 2;
                break;
            }
            else // charm opponent
            {
                if (self->busy > 0)
                    return false;
                {
                    std::int32_t howmany;
                    std::int32_t didheal = 0;
                    std::int32_t generic2 = 0;
                    std::list<walker*> newlist = self->sim_level->find_foes_in_range(self->sim_level->oblist,
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
                            std::int32_t generic = self->stats()->level - ob->stats()->level;
                            if (generic < 0 || (!self->sim_rng->next(20)))
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
                                ob->charm_left = (static_cast<short>(75 + generic * 25));
                                generic2 = 0;
                            }
                            didheal++;
                        }
                    }
                    if (!didheal)
                        return false;
                    if (generic2)
                        tempstr = std::format("{} failed to charm!", entity_display_name(self, "Thief"));
                    else
                        tempstr = std::format("{} charmed an opponent!", entity_display_name(self, "Thief"));
                    og::sim::emit_notification(self->sim_events, tempstr);
                    self->busy += 10;
                }
                break;
            }
        case 4: // poison cloud
        default:
            if (self->busy > 0)
                return false;
            newob = summon_entity(self, Order::FX, FAMILY_CLOUD);
            if (!newob)
                return false;
            self->busy += 5;
            newob->ignore = 1;
            newob->lifetime = 40 + 3 * self->stats()->level;
            newob->invisibility_left = 10;
            newob->ani_type = ANI_SPIN;
            newob->damage = static_cast<float>(self->stats()->level);
            break;
    }
    return true;
}

static const char* const thief_names[] = {"Shinobi", "Dismas", "Shadow", "Stabby", "Swiftstrike", "Scourge", "Rogue"};

const FamilyDescriptor& describe_family_thief()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_THIEF,
        .name = "THIEF",
        .short_name = nullptr,
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
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "THIEF KILLED",
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
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "thief.pix",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 10,
        .description = "Thieves are fast, though  \n"
                       "not so potent as the      \n"
                       "soldier. Thieves can throw\n"
                       "small blades rapidly and  \n"
                       "damage whole groups of    \n"
                       "enemies with their bombs. \n"
                       "\n"
                       "Special: Drop Bomb",
        .name_pool = thief_names,
        .name_pool_size = sizeof(thief_names) / sizeof(thief_names[0]),
        .is_playable = true,
        .playable_order = 7,
    };
    return desc;
}
