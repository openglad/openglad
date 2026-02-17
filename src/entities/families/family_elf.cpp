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
#include <openglad/entities/walker.h>
#include <openglad/entities/weap.h>
#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/core/stats.h>

#include <cstdlib>

#define BASE_GUY_HP 30

static bool elf_do_special(walker* self)
{
    weap* fireob;
    std::int32_t i;

    switch (self->current_special)
    {
        case 1: // some rocks (normal)
            self->stats()->magicpoints += 2.0f * static_cast<float>(self->stats()->weapon_cost);
            fireob = static_cast<weap*>(self->fire());
            if (!fireob)
                return false;
            fireob->lastx *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            fireob->lasty *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            fireob = static_cast<weap*>(self->fire());
            if (!fireob)
                return false;
            fireob->lastx *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            fireob->lasty *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            break;
        case 2: // more rocks, and bouncing
            self->stats()->magicpoints += 3.0f * static_cast<float>(self->stats()->weapon_cost);
            for (i = 0; i < 2; i++)
            {
                fireob = static_cast<weap*>(self->fire());
                if (!fireob)
                    return false;
                fireob->lineofsight *= 3;
                fireob->lineofsight /= 2;
                fireob->do_bounce = 1;
                fireob->lastx *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
                fireob->lasty *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            }
            break;
        case 3:
            self->stats()->magicpoints += 4.0f * static_cast<float>(self->stats()->weapon_cost);
            for (i = 0; i < 3; i++)
            {
                fireob = static_cast<weap*>(self->fire());
                if (!fireob)
                    return false;
                fireob->lineofsight *= 2;
                fireob->do_bounce = 1;
                fireob->lastx *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
                fireob->lasty *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            }
            break;
        case 4:
        default:
            self->stats()->magicpoints += 5.0f * static_cast<float>(self->stats()->weapon_cost);
            for (i = 0; i < 4; i++)
            {
                fireob = static_cast<weap*>(self->fire());
                if (!fireob)
                    return false;
                fireob->lineofsight *= 5;
                fireob->lineofsight /= 2;
                fireob->do_bounce = 1;
                fireob->lastx *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
                fireob->lasty *= 0.8f + 0.4f * static_cast<float>(rand() % 101) / 100.0f;
            }
            break;
    }
    return true;
}

static void elf_level_up(guy* self, std::int32_t level_diff)
{
    std::int32_t s = 8 * level_diff;
    std::int32_t d = 6 * level_diff;
    std::int32_t c = 8 * level_diff;
    std::int32_t it = 8 * level_diff;
    std::int32_t a = 1 * level_diff;
    s = (s * 3) / 4;
    d = (d * 3) / 2;
    c = (c * 3) / 4;
    self->strength = static_cast<short>(static_cast<std::int32_t>(self->strength) + s);
    self->dexterity = static_cast<short>(static_cast<std::int32_t>(self->dexterity) + d);
    self->constitution = static_cast<short>(static_cast<std::int32_t>(self->constitution) + c);
    self->intelligence = static_cast<short>(static_cast<std::int32_t>(self->intelligence) + it);
    self->armor = static_cast<short>(static_cast<std::int32_t>(self->armor) + a);
}

const FamilyDescriptor& describe_family_elf()
{
    static const FamilyDescriptor desc = {
        .family_id = FAMILY_ELF,
        .name = "ELF",
        .base_stats = {5, 14, 5, 12, 8, 1},
        .hiring_cost = 150,
        .derived_bonuses = {BASE_GUY_HP+45, 0, 12, 0, 0, 0, 4, 5},
        .stat_costs = {25, 6, 12, 8, 50, 200},
        .special_cost = {5000, 10, 20, 30, 40, 5000},
        .weapon_cost = 1,
        .default_weapon = FAMILY_ROCK,
        .init_bit_flags = BIT_FORESTWALK,
        .init_ani_type = 0,
        .init_max_magicpoints = 0,
        .special_names = {"NONE", "ROCKS", "BOUNCING ROCKS", "LOTS OF ROCKS", "MEGA ROCKS", "NONE"},
        .alternate_names = {"NONE", "NONE", "NONE", "NONE", "NONE", "NONE"},
        .leaves_bloodspot = true,
        .magic_damage_modifier = 1.0f,
        .is_stationary = false,
        .has_returning_weapon = false,
        .is_undead = false,
        .promotes_to = -1,
        .promotion_level_req = 0,
        .promotion_new_level = nullptr,
        .death_message = "ELF KILLED",
        .do_special = elf_do_special,
        .check_special_ai = nullptr,
        .hit_response = nullptr,
        .set_difficulty = nullptr,
        .level_up = elf_level_up,
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
