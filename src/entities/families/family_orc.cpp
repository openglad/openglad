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
#include <openglad/core/combat_math.h>
#include <openglad/legacy/base.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen.h>

#include <format>
#include <list>
#include <string>

#define BASE_GUY_HP 30

short exp_from_action(ExpAction action, walker* w, walker* target, short value);

namespace {
static inline Uint32 rng(Uint32 max_exclusive)
{
    return ctx().rng->next(max_exclusive);
}

static inline cfg_store& active_config()
{
    if (ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}
} // namespace

static bool orc_do_special(walker* self)
{
    walker* newob;
    Sint32 tempx, tempy;
    Sint32 howmany;
    Uint32 distance;
    std::string message;

    switch (self->current_special)
    {
        case 1: // yell and 'freeze' foes
            if (self->busy > 0)
                return false;
            self->busy += 2;

            {
                std::list<walker*> newlist = myscreen->find_foes_in_range(
                    myscreen->level_data.oblist,
                    160 + (20 * self->stats()->level), &howmany, self);

                for (auto* ob : newlist)
                {
                    if (ob)
                    {
                        if (ob->myguy)
                            tempx = ob->myguy->constitution;
                        else
                            tempx = static_cast<Sint32>(ob->stats()->hitpoints / 30.0f);
                        Sint32 tempx_clamped = (tempx > 0) ? tempx : 0;
                        tempy = 10
                            + static_cast<Sint32>(rng(static_cast<Uint32>(self->stats()->level * 10)))
                            - static_cast<Sint32>(rng(static_cast<Uint32>(tempx_clamped * 10)));
                        if (tempy < 0)
                            tempy = 0;
                        ob->stats()->frozen_delay = static_cast<short>(ob->stats()->frozen_delay + tempy);
                    }
                }

                if (self->on_screen())
                    myscreen->soundp->play_sound(SOUND_ROAR);
            }
            break;
        case 2: // eat corpse for health
        case 3:
        case 4:
        default:
            if (self->stats()->hitpoints >= self->stats()->max_hitpoints)
                return false;
            newob = myscreen->find_nearest_blood(self);
            if (!newob)
                return false;
            distance = static_cast<Uint32>(self->distance_to_ob_center(newob));
            if (distance > 24)
                return false;
            self->stats()->hitpoints += static_cast<float>(newob->stats()->level) * 5.0f;
            self->do_heal_effects(nullptr, self, static_cast<short>(newob->stats()->level * 5));
            // Print the eating notice
            if (self->myguy)
            {
                self->myguy->exp += exp_from_action(ExpAction::EatCorpse, self, newob, 0);
                message = std::format("{} ate a corpse.", self->myguy->name);
            }
            else if (self->stats()->name.size())
                message = std::format("{} ate a corpse.", self->stats()->name);
            else
                message = "Orc ate a corpse.";

            if (!active_config().is_on("effects", "heal_numbers"))
                myscreen->do_notify(message.c_str(), self);
            if (self->stats()->hitpoints > self->stats()->max_hitpoints)
                self->stats()->hitpoints = self->stats()->max_hitpoints;
            newob->dead = 1;
            newob->death();
            break;
    }
    return true;
}

static bool orc_check_special_ai(living* self)
{
    if (self->foe)
    {
        Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
        return (distance < 130);
    }
    self->foe = myscreen->find_near_foe(self);
    if (!self->foe)
        return false;
    Uint32 distance = static_cast<Uint32>(self->distance_to_ob(self->foe));
    return (distance < 130);
}

static void orc_set_difficulty(living* self, Uint32 level)
{
    const float levmult = static_cast<float>(level) * static_cast<float>(level);
    const float level_f = static_cast<float>(level);
    self->stats()->max_hitpoints   += 14.0f * levmult;
    self->stats()->max_magicpoints += 7.0f * levmult;
    self->damage += 6.0f * level_f;
    self->stats()->armor += 3.0f * levmult;
}

static void orc_level_up(guy* self, Sint32 level_diff)
{
    Sint32 s = 8 * level_diff;
    Sint32 d = 6 * level_diff;
    Sint32 c = 8 * level_diff;
    Sint32 it = 8 * level_diff;
    Sint32 a = 1 * level_diff;
    s = (s * 3) / 2;
    d /= 2;
    c = (c * 3) / 2;
    it /= 2;
    self->strength = static_cast<short>(static_cast<Sint32>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<Sint32>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<Sint32>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<Sint32>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<Sint32>(self->armor) + a);
}

static short orc_promotion_level([[maybe_unused]] int old_level)
{
    return 1;
}

const FamilyDescriptor& describe_family_orc()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ORC,
        .name = "ORC",
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
    };
    return desc;
}
