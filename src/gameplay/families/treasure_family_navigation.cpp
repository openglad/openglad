/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <cstdint>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_emit.h>
#include <format>
#include <string>

std::string get_scenario_title(const char* filename);

namespace
{

bool is_level_completed(const GameWorld& world, int level)
{
    return world.completed_levels.count(level) > 0;
}

std::string format_exit_prompt(const std::string& exitname, bool is_withdraw)
{
    if (is_withdraw)
        return std::format("Withdraw to {}?", exitname);
    return std::format("Exit to {}?", exitname);
}

} // namespace

static bool exit_on_eat(treasure* self, walker* eater)
{
    if (current_game == nullptr || current_game->world == nullptr)
        return true;

    if (eater->in_act) return true;
    if (eater->act_type != ACT_CONTROL || (eater->skip_exit > 1))
        return true;
    eater->skip_exit = 10;
    // See if there are any enemies left ...
    short guys_here;
    if (current_game->world->level_done == 0)
        guys_here = 1;
    else
        guys_here = 0;
    // Get the name of our exit..
    std::string message = std::format("scen{}", self->stats()->level);
    std::string exitname = get_scenario_title(message.c_str());

    if (exitname == "none")
    {
        exitname = std::format("Level {}", self->stats()->level);
    }

    GameWorld& world = *current_game->world;
    const short destination_level = static_cast<short>(self->stats()->level);
    const bool can_exit_now = (!guys_here || (world.type & GameWorld::TYPE_CAN_EXIT_WHENEVER));
    const bool can_withdraw = is_level_completed(world, destination_level) &&
                              !is_level_completed(world, world.current_scenario);

    // Exit path: all enemies dead, or scenario allows free exit.
    // Check this BEFORE the withdraw path so that CAN_EXIT_WHENEVER levels
    // show the normal "Exit to X?" dialog instead of "Withdraw to X?".
    if (can_exit_now)
    {
        og::sim::emit_event_text(current_game->sim_events,
                                 og::sim::EventKind::RequestExitConfirmation,
                                 format_exit_prompt(exitname, false),
                                 static_cast<std::uint32_t>(destination_level),
                                 0);
        return true;
    }

    // Withdraw path: enemies still present, destination level already completed,
    // current scenario not yet completed. Abort the current level and revert
    // to the autosave, then jump to the exit's destination level.
    // This is mutually exclusive with the exit path above — if the exit path
    // didn't fire, we know guys_here != 0 and CAN_EXIT_WHENEVER is not set.
    if (can_withdraw)
    {
        world.withdraw_requested = true;
        world.withdraw_level = destination_level;
        og::sim::emit_event(current_game->sim_events,
                            og::sim::EventKind::WithdrawToLevel,
                            static_cast<std::uint32_t>(destination_level), 0);
        og::sim::emit_event_text(current_game->sim_events,
                                 og::sim::EventKind::RequestExitConfirmation,
                                 format_exit_prompt(exitname, true),
                                 static_cast<std::uint32_t>(destination_level),
                                 1);
    } // end of checking for withdrawal to completed level
    return true;
}

static bool teleporter_on_eat(treasure* self, walker* eater)
{
    if (eater->skip_exit > 1)
        return true;
    std::int32_t distance = self->distance_to_ob_center(eater); // how far away?
    if (distance > 21)
        return true;
    if (distance < 4 && eater->skip_exit)
    {
        eater->skip_exit = 8;
        return true;
    }
    // If we're close enough, teleport ..
    eater->skip_exit = eater->skip_exit + 20;
    walker* target;
    if (!self->leader())
        target = self->find_teleport_target();
    else
        target = self->leader();
    if (!target)
        return true;
    self->set_leader(target);
    eater->center_on(target);
    if (!current_game->world->query_passable(eater->xpos, eater->ypos, eater))
    {
        eater->center_on(self);
        return true;
    }
    // Now do special effects
    walker* flash = current_game->world->add_ob(Order::FX, FAMILY_FLASH);
    flash->ani_type = ANI_EXPAND_8;
    flash->center_on(self);
    return true;
}

const TreasureFamilyDescriptor& describe_treasure_exit()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_EXIT,
        .name = "EXIT",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = exit_on_eat,
    };
    return desc;
}

const TreasureFamilyDescriptor& describe_treasure_teleporter()
{
    static const TreasureFamilyDescriptor desc = {
        .family_id = FAMILY_TELEPORTER,
        .name = "TELEPORTER",
        .init_ignore = false,
        .init_frame = -1,
        .on_eat = teleporter_on_eat,
    };
    return desc;
}
