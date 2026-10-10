/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

class GameWorld;
class walker;

namespace og::sim {

// A weapon-order walker whose family declares `blocks_placement` (bone
// walls, banners): solid for the teleport, spawn and landing probes the way
// doors, trees and boulders are. False for every classic family.
[[nodiscard]] bool declares_blocks_placement(const walker& w);

// Exchange two walkers' positions and floors, keeping the collision table
// in step. Refuses (false, nothing moved) when either is dead, dormant or
// hidden, or when they are the same walker.
bool swap_places(GameWorld& world, walker& a, walker& b);

}  // namespace og::sim
