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
// Specials kit): the SDL HUD, the curses status line and the text client's
// state all read this one answer.
struct SeatTimer {
    const char* label;  // nullptr: nothing running
    int ticks;          // sim ticks left (12 a second)
};

// The running timed effect of `control` (the walker a seat drives: the
// host while a ghost possesses it). It reads snapshot fields only
// (possess_link, possess_ticks, kit_state, speed_bonus_left and the kit
// markers' family, ani_type, lifetime and owner), so a display mirror with
// no sim of its own answers exactly what the server would. With the New
// Specials setting off it answers nothing, so the classic HUD never
// changes. It does not look at who drives the walker: a bot's effect is
// answered too, and the HUD decides whom to show it to.
//
// Candidates: POSSESS (a possessed body's own clock), then the walker's
// kit markers (PHASE, DIG IN, REASSEMBLE, IMMOLATE, LEGION; the meteor
// rain is not a countdown), then HASTE (the speed bonus, the potion's
// included). When several run, the one ending soonest is shown; a tie goes
// to the earlier one in that order.
[[nodiscard]] SeatTimer seat_timer(const GameWorld& world,
                                   const walker& control) noexcept;

// Ticks to whole seconds, rounded up, at the 12-tick-a-second sim rate:
// the same rule the SCARED line uses, so a clock never reads 0s while
// something is still running.
[[nodiscard]] int timer_seconds(int ticks) noexcept;

}  // namespace og::sim
