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
#include <openglad/core/stats.h>
#include <openglad/entities/guy.h>
#include <openglad/legacy/soundob.h>
#include <openglad/sim/sim_emit.h>
#include <format>
#include <string>

static bool drumstick_on_eat(treasure* self, walker* eater)
{
    if (eater->stats()->hitpoints >= eater->stats()->max_hitpoints)
        return true;
    const std::int32_t heal_amount = 10 * self->stats()->level + static_cast<std::int32_t>(self->sim_rng->next(static_cast<std::uint32_t>(10 * self->stats()->level)));
    const short amount = static_cast<short>(heal_amount);
    eater->stats()->hitpoints += amount;
    if (eater->stats()->hitpoints > eater->stats()->max_hitpoints)
        eater->stats()->hitpoints = eater->stats()->max_hitpoints;
    self->do_heal_effects(nullptr, eater, amount);
    self->dead = 1;
    og::sim::emit_sound(self->sim_events, SOUND_EAT);
    return true;
}

static bool magic_potion_on_eat(treasure* self, walker* eater)
{
    if (eater->stats()->magicpoints < eater->stats()->max_magicpoints)
        eater->stats()->magicpoints = eater->stats()->max_magicpoints;
    eater->stats()->magicpoints += static_cast<float>(50 * self->stats()->level);
    self->dead = 1;
    if (eater->user != -1)
    {
        std::string message = std::format("Potion of Mana({})!", self->stats()->level);
        og::sim::emit_notification(self->sim_events, message);
    }
    return true;
}

static bool flight_potion_on_eat(treasure* self, walker* eater)
{
    if (!eater->stats()->query_bit_flags(BIT_FLYING))
    {
        eater->flight_left = static_cast<short>(eater->flight_left + (150 * self->stats()->level));
        if (eater->user != -1)
        {
            std::string message = std::format("Potion of Flight({})!", self->stats()->level);
            og::sim::emit_notification(self->sim_events, message);
        }
        self->dead = 1;
    }
    return true;
}

static bool invulnerable_potion_on_eat(treasure* self, walker* eater)
{
    if (!eater->stats()->query_bit_flags(BIT_INVINCIBLE))
    {
        eater->invulnerable_left = static_cast<short>(eater->invulnerable_left + (150 * self->stats()->level));
        self->dead = 1;
        if (eater->user != -1)
        {
            std::string message = std::format("Potion of Invulnerability({})!", self->stats()->level);
            og::sim::emit_notification(self->sim_events, message);
        }
    }
    return true;
}

static bool invis_potion_on_eat(treasure* self, walker* eater)
{
    eater->invisibility_left = static_cast<short>(eater->invisibility_left + (150 * self->stats()->level));
    if (eater->user != -1)
    {
        std::string message = std::format("Potion of Invisibility({})!", self->stats()->level);
        og::sim::emit_notification(self->sim_events, message);
    }
    self->dead = 1;
    return true;
}

static bool speed_potion_on_eat(treasure* self, walker* eater)
{
    eater->speed_bonus_left = eater->speed_bonus_left + 50 * self->stats()->level;
    eater->speed_bonus = static_cast<float>(self->stats()->level);
    if (eater->user != -1)
    {
        std::string message = std::format("Potion of Speed({})!", self->stats()->level);
        og::sim::emit_notification(self->sim_events, message);
    }
    self->dead = 1;
    return true;
}

const TreasureFamilyDescriptor& describe_treasure_drumstick()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_DRUMSTICK,
        .name = "DRUMSTICK",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = drumstick_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_magic_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_MAGIC_POTION,
        .name = "MAGIC_POTION",
        .init_ignore = false,
        .init_frame = 0,
        .on_eat = magic_potion_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_flight_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_FLIGHT_POTION,
        .name = "FLIGHT_POTION",
        .init_ignore = false,
        .init_frame = 11,
        .on_eat = flight_potion_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_invulnerable_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_INVULNERABLE_POTION,
        .name = "INVULNERABLE_POTION",
        .init_ignore = false,
        .init_frame = 2,
        .on_eat = invulnerable_potion_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_invis_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_INVIS_POTION,
        .name = "INVIS_POTION",
        .init_ignore = false,
        .init_frame = 1,
        .on_eat = invis_potion_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_speed_potion()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_SPEED_POTION,
        .name = "SPEED_POTION",
        .init_ignore = false,
        .init_frame = 3,
        .on_eat = speed_potion_on_eat,
    };
    return desc;
}
