/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <cstdint>

// Create a new entity via add_ob and initialize common ownership properties:
// owner, team_num, floor, center_on(summoner), and level copy from summoner.
// Returns nullptr if add_ob fails.
inline walker* summon_entity(walker* summoner, Order order, std::int32_t family)
{
    walker* ob = current_game->world->add_ob(order, family);
    if (!ob) return nullptr;
    ob->set_owner(summoner);
    ob->set_team_num(summoner->team_num());
    // Children spawn on the summoner's floor. set_floor must come
    // BEFORE center_on/setxy: the obmap re-buckets at the entity's current
    // floor on every position change.
    ob->set_floor(summoner->floor());
    ob->center_on(summoner);
    ob->stats()->set_level(summoner->stats()->level());
    return ob;
}
