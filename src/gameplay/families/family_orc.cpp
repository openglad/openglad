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
#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_emit.h>

#include <format>
#include <list>
#include <string>

inline constexpr int BASE_GUY_HP = 30;

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

static bool orc_do_special(walker* self)
{
    walker* newob;
    std::int32_t tempx, tempy;
    std::int32_t howmany;
    std::uint32_t distance;
    std::string message;

    switch (self->current_special())
    {
        case 1: // yell and 'freeze' foes
            if (self->busy() > 0)
                return false;
            self->set_busy(self->busy() + 2.0f);

            {
                std::list<walker*> newlist = current_game->world->find_foes_in_range(
                    current_game->world->oblist,
                    160 + (20 * self->stats()->level()), &howmany, self);

                for (auto* ob : newlist)
                {
                    if (ob)
                    {
                        if (ob->myguy)
                            tempx = ob->myguy->constitution;
                        else
                            tempx = static_cast<std::int32_t>(ob->stats()->hitpoints() / 30.0f);
                        std::int32_t tempx_clamped = (tempx > 0) ? tempx : 0;
                        tempy = 10
                            + static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(self->stats()->level() * 10)))
                            - static_cast<std::int32_t>(current_game->world->rng_.next(static_cast<std::uint32_t>(tempx_clamped * 10)));
                        if (tempy < 0)
                            tempy = 0;
                        ob->stats()->set_frozen_delay(static_cast<short>(ob->stats()->frozen_delay() + tempy));
                    }
                }

                og::sim::emit_sound(current_game->sim_events, SOUND_ROAR);
            }
            break;
        case 2: // eat corpse for health
        case 3:
        case 4:
        default:
            if (self->stats()->hitpoints() >= self->stats()->max_hitpoints())
                return false;
            newob = current_game->world->find_nearest_blood(self);
            if (!newob)
                return false;
            distance = static_cast<std::uint32_t>(self->distance_to_ob_center(newob));
            if (distance > 24)
                return false;
            self->stats()->set_hitpoints(self->stats()->hitpoints() + static_cast<float>(newob->stats()->level()) * 5.0f);
            self->do_heal_effects(nullptr, self, static_cast<short>(newob->stats()->level() * 5));
            // Print the eating notice
            if (self->myguy)
            {
                self->myguy->exp += exp_from_action(ExpAction::EatCorpse, self, newob, 0);
            }
            message = std::format("{} ate a corpse.", entity_display_name(self, "Orc"));

            og::sim::emit_notification(current_game->sim_events, message);
            if (self->stats()->hitpoints() > self->stats()->max_hitpoints())
                self->stats()->set_hitpoints(self->stats()->max_hitpoints());
            newob->set_dead(1);
            newob->death();
            break;
    }
    return true;
}

static bool orc_check_special_ai(living* self)
{
    return check_special_ai_distance(self, 130);
}

static void orc_set_difficulty(living* self, std::uint32_t level)
{
    apply_difficulty_scaling(self, level, {14.0f, 7.0f, 6.0f, 3.0f});
}

static void orc_level_up(guy* self, std::int32_t level_diff)
{
    apply_level_up(self, level_diff, {12, 3, 12, 4, 1});
}

static short orc_promotion_level([[maybe_unused]] int old_level)
{
    return 1;
}

static const char* const orc_names[] = {"Grom", "Thrull", "Vernix", "Lanugo", "Grok", "Horde", "Grog", "Krosh"};

const FamilyDescriptor& describe_family_orc()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ORC,
        .name = "ORC",
        .short_name = nullptr,
        .base_stats = {18, 8, 16, 5, 11, 1},
        .hiring_cost = 300,
        .derived_bonuses = {BASE_GUY_HP+110, 0, 23, 0, 0, 0, 3, 7},
        .stat_costs = {6, 15, 5, 40, 50, 200},
        .special_cost = {5000, 25, 20, 5000, 5000, 5000},
        .weapon_cost = 2,
        .default_weapon = FAMILY_ROCK,
        .init_bit_flags = BIT_NO_RANGED,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "HOWL", "EAT CORPSE", "NONE", "NONE", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = FAMILY_BIG_ORC,
        .promotion_level_req = 5,
        .promotion_new_level = orc_promotion_level,
        .death_message = "ORC DIED",
        .do_special = orc_do_special,
        .check_special_ai = orc_check_special_ai,
        .hit_response = nullptr,
        .set_difficulty = orc_set_difficulty,
        .level_up = orc_level_up,
        .on_death = nullptr,
        .on_act_living = nullptr,
        .on_shoved = nullptr,
        .on_fire_weapon = nullptr,
        .handle_teleport = nullptr,
        .on_create = nullptr,
        .customize_weapon = nullptr,
        .on_ani_complete = nullptr,
        .on_melee_hit = nullptr,
        .pix_filename = "orc.png",
        .animation_type = FAMILY_ANIM_STANDARD,
        .ai_line_of_sight = 20,
        .description = "Orcs are a basic 'grunt'; \n"
                       "strong and hard to hurt,  \n"
                       "they don't do much more   \n"
                       "than inflict pain. Orcs   \n"
                       "can't attack at range.    \n"
                       "\n"
                       "Special: Howl",
        .name_pool = orc_names,
        .name_pool_size = sizeof(orc_names) / sizeof(orc_names[0]),
        .is_playable = true,
        .playable_order = 9,
    };
    return desc;
}
