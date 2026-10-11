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

// The countdown a seat sees for its own running timed effect (a New
// Specials kit's or a classic one's): the SDL HUD, the curses status line
// and the text client's state all read this one answer.
struct SeatTimer {
    const char* label;  // nullptr: nothing running
    int ticks;          // sim ticks left (12 a second)
};

// The running timed effect of `control` (the walker a seat drives: the
// host while a ghost possesses it). It reads snapshot fields only, so a
// display mirror with no sim of its own answers exactly what the server
// would. It does not look at who drives the walker: a bot's effect is
// answered too, and the HUD decides whom to show it to.
//
// Candidates, in tie order, with the field each one reads:
//   POSSESS     possess_link and possess_ticks (the host's clock)
//   PHASE       a PHASE_VEIL kit marker the walker owns (lifetime)
//   DIG IN      a BURROW kit marker
//   REASSEMBLE  a WARD kit marker
//   IMMOLATE    an IMMOLATION kit marker
//   LEGION      a LEGION kit marker (the meteor rain is not a countdown)
//   SUMMONED    the walker's own lifetime (a summoned body the seat drives)
//   CHARMED     charm_left
//   STUNNED     stats()->frozen_delay()
//   INVISIBLE   invisibility_left (CLOAK, the potion)
//   INVINCIBLE  invulnerable_left (the potion)
//   FLYING      flight_left, above 1 (1 holds a walker over ground it
//               cannot stand on)
//   HASTE       speed_bonus_left, above 1, showing one less (the field
//               sticks at 1 once a bonus has run out)
// While the walker's PHASE veil runs, INVINCIBLE and HASTE are not offered:
// the phase writes those two fields itself.
//
// The New Specials setting gates the kit's six (POSSESS to LEGION); with it
// off they are skipped and the classic seven still answer. When several
// run, the one ending soonest is shown; a tie goes to the earlier one in
// the order above.
[[nodiscard]] SeatTimer seat_timer(const GameWorld& world,
                                   const walker& control) noexcept;

// Ticks to whole seconds, rounded up, at the 12-tick-a-second sim rate:
// the same rule the SCARED line uses, so a clock never reads 0s while
// something is still running.
[[nodiscard]] int timer_seconds(int ticks) noexcept;

}  // namespace og::sim
