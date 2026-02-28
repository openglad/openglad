/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/entities/treasure_family_descriptor.h>
#include <openglad/entities/treasure.h>
#include <openglad/core/constants.h>
#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/data/level_data.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>
#include <format>
#include <string>
#include <cmath>
#include <algorithm>

static bool is_valid_score_team(unsigned char team_num)
{
    return team_num < SCORE_TEAM_COUNT;
}

static bool gold_bar_on_eat(treasure* self, walker* eater)
{
    if (eater->team_num == 0 || eater->myguy)
    {
        if (current_game && current_game->world && is_valid_score_team(eater->team_num))
            current_game->world->m_score[eater->team_num] += (200 * self->stats()->level);
        self->dead = 1;
        og::sim::emit_sound(current_game->sim_events, SOUND_MONEY);
    }
    return true;
}

static bool silver_bar_on_eat(treasure* self, walker* eater)
{
    if (eater->team_num == 0 || eater->myguy)
    {
        if (current_game && current_game->world && is_valid_score_team(eater->team_num))
            current_game->world->m_score[eater->team_num] += (50 * self->stats()->level);
        self->dead = 1;
        og::sim::emit_sound(current_game->sim_events, SOUND_MONEY);
    }
    return true;
}

static bool life_gem_on_eat(treasure* self, walker* eater)
{
    if (eater->team_num != self->team_num) // only our team can get these
        return true;
    if (current_game && current_game->world && is_valid_score_team(eater->team_num))
        current_game->world->m_score[eater->team_num] += static_cast<std::uint32_t>(std::max(0.0f, self->stats()->hitpoints));
    walker* flash = current_game->world->add_ob(Order::FX, FAMILY_FLASH);
    flash->ani_type = ANI_EXPAND_8;
    flash->center_on(self);
    self->dead = 1;
    self->death();
    return true;
}

static bool key_on_eat(treasure* self, walker* eater)
{
    if (!(eater->keys & static_cast<std::int32_t>(pow(static_cast<double>(2), self->stats()->level)))) // just got it?
    {
        eater->keys = eater->keys | static_cast<std::int32_t>(pow(static_cast<double>(2), self->stats()->level)); // ie, 2, 4, 8, 16...
        std::string message;
        if (eater->myguy)
            message = std::format("{} picks up key {}", eater->myguy->name,
                    self->stats()->level);
        else
            message = std::format("{} picks up key {}", eater->stats()->name, self->stats()->level);
        if (eater->team_num == 0) // only show players picking up keys
        {
            og::sim::emit_notification(current_game->sim_events, message);
            og::sim::emit_sound(current_game->sim_events, SOUND_MONEY);
        }
    }
    return true;
}

const TreasureFamilyDescriptor& describe_treasure_gold_bar()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_GOLD_BAR,
        .name = "GOLD_BAR",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = gold_bar_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_silver_bar()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_SILVER_BAR,
        .name = "SILVER_BAR",
        .init_ignore = false,
        .init_frame = 1,
        .on_eat = silver_bar_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_life_gem()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_LIFE_GEM,
        .name = "LIFE_GEM",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = life_gem_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_key()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_KEY,
        .name = "KEY",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = key_on_eat,
    };
    return desc;
}
