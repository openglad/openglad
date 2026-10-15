/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

class GameWorld;
class living;
class walker;

// New Specials possession: a ghost (the rider) enters another living (the
// host) and the rider's seat drives the host. Pure C++, no RNG. The link is
// walker::possess_link() on both sides; the host carries the countdown in
// walker::possess_ticks(). The host's original team is remembered on the
// rider's real_team_num (charm_left is never used).
namespace og::sim {

struct PossessResult {
    bool ok;
    const char* reason;  // <= 24 bytes, a HUD refusal when !ok
};

// The rider enters the host. Refuses (nothing written) unless both are
// livings, the host is alive, not undead, not hidden or dormant, unlinked
// and not seat-driven, neither side is charmed, and the rider is unlinked
// and not hidden. ticks 0 = permanent. The caller's world must be the
// current gameplay context: the rider's hide and move act on the collision
// table of the world that is current.
PossessResult possess(GameWorld& world, walker& rider, walker& host,
                      std::int16_t ticks);
// Ends a possession from either side; does nothing on an unlinked walker.
// overkill > 0: the rider takes half of it as damage. Returns the rider.
walker* release_possession(GameWorld& world, walker& either_side,
                           int overkill = 0);
// Once per tick on the HOST (living::act): the rider follows, the
// countdown runs, and the possession ends at 0.
void possession_host_tick(GameWorld& world, living& host);
// Called at the top of walker::death(). A dying host releases its rider; a
// dying hidden rider releases its host; a warded walker (KIT_WARD) gets up
// instead. Returns true when the walker SURVIVES.
bool kit_on_death(GameWorld& world, walker& self);
// The seat redirect at the top of sim_process_player_input: rewrites
// `control` across a possession link in either direction.
void possession_seat_redirect(GameWorld& world, walker*& control,
                              short player_num);

}  // namespace og::sim
