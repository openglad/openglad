/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/gameplay/sim_entity.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/reach.h>

namespace og::sim {

SimEntity::SimEntity() = default;
SimEntity::~SimEntity() = default;

void SimEntity::door_state_written() noexcept
{
    // rule: og::reach: a door's dead, team and level setters tell its world's maps (the door
    //   count goes up), so a map built before the change is not read after it; any other entity's,
    //   or a door in no world's lists, tells nothing.
    // tests: SemReach.every_writer_of_the_doors_and_the_terrain_bumps_its_count
    if (owning_world_ != nullptr && order_ == Order::Weapon && family_ == FAMILY_DOOR)
        owning_world_->reach_maps.note_doors();
}

} // namespace og::sim
