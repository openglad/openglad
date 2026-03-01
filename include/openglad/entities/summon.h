/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <cstdint>

// Create a new entity via add_ob and initialize common ownership properties:
// owner, team_num, center_on(summoner), and level copy from summoner.
// Returns nullptr if add_ob fails.
inline walker* summon_entity(walker* summoner, Order order, std::int32_t family)
{
    walker* ob = current_game->world->add_ob(order, family);
    if (!ob) return nullptr;
    ob->owner = summoner;
    ob->team_num = summoner->team_num;
    ob->center_on(summoner);
    ob->stats()->level = summoner->stats()->level;
    return ob;
}
