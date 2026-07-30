#pragma once

// Diagonal release retention (client-side input shaping).
//
// When a player runs diagonally by holding two direction keys and then lets
// go of both, human release timing is sloppy: one key often clears 1-3 input
// samples before the other. Raw sampling then reports the surviving single
// key for those ticks, so the control walker's final facing snaps from the
// diagonal to that cardinal direction. (The effect is direction-dependent in
// practice only because of release ergonomics — e.g. S+D lift together more
// often than W+A — the sampling pipeline itself is symmetric.)
//
// coalesce_direction_release() absorbs that skew: while a diagonal was held,
// a pure-release transition down to a subset of its keys keeps reporting the
// full diagonal for a short grace window. If the remaining key persists past
// the window it was an intentional turn and the raw state applies; pressing
// any key OUTSIDE the retained diagonal applies immediately.
//
// This runs in the client input sampling (input_state_from_sdl) BEFORE the
// InputState snapshot is built, so the wire format and the sim are untouched
// and the result is deterministic per input timeline.

#include <cstdint>

// Number of consecutive input samples (including the trigger sample) for
// which the diagonal is still reported after a partial release.
inline constexpr int kDiagonalReleaseGraceTicks = 4;

// Per-player retention state. Masks are bitmasks over the eight direction
// key slots KEY_UP(0)..KEY_UP_LEFT(7) (bit d == key slot d).
struct DirectionGraceState
{
    std::uint8_t prev_raw = 0;   // raw direction mask from the previous sample
    std::uint8_t hold_mask = 0;  // diagonal being retained (0 == inactive)
    std::uint8_t ticks_left = 0; // retention samples remaining after this one
};

// Feed one raw direction-key sample; returns the mask to report. Pure and
// deterministic: same timeline of raw masks -> same outputs.
std::uint8_t coalesce_direction_release(std::uint8_t raw_mask,
                                        DirectionGraceState& state);

// ---------------------------------------------------------------------------
// Opposite-direction re-assert (missed-keyup resilience).
//
// Browsers on some devices swallow keyup events outright — iPad Safari
// around system gestures and the Globe/Cmd HUD, shared-keyboard mashing —
// and no later event corrects the record. The sampled keyboard state then
// keeps the key down forever ("latched"). Because held[] is re-derived from
// that state every sample, a latched UP alone walks the character up
// constantly, and a REAL press of the opposite direction nets to zero in
// PlayerInput::move_x()/move_y(): the whole axis looks dead while the other
// axis keeps working (the 3-player iPad arrow-seat bug).
//
// resolve_opposing_directions() gives the fresh press priority: while both
// sides of an axis are held, the side whose key rose most recently wins and
// the stale side is suppressed from the reported mask. Both sides rising in
// the same sample keep the legacy cancellation; releasing either side ends
// the conflict; a correctly-delivered keyup behaves exactly as before.
// Suppression is group-level: a suppressed stale diagonal (e.g. UP_LEFT
// vs. a fresh DOWN) stops contributing to BOTH axes — it is presumed
// phantom.
//
// This runs in the client input sampling (input_state_from_sdl) BEFORE
// coalesce_direction_release, so the wire format and the sim are untouched
// and the result is deterministic per input timeline.

// Per-player conflict state. prev_raw is a bitmask over the eight direction
// key slots KEY_UP(0)..KEY_UP_LEFT(7) (bit d == key slot d).
struct DirectionConflictState
{
    std::uint8_t prev_raw = 0;  // raw direction mask from the previous sample
    std::int8_t x_winner = 0;   // -1: left side wins, +1: right side, 0: none
    std::int8_t y_winner = 0;   // -1: up side wins, +1: down side, 0: none
};

// Feed one raw direction-key sample; returns the mask to report (the stale
// opposite side suppressed while a fresher press holds priority). Pure and
// deterministic: same timeline of raw masks -> same outputs.
std::uint8_t resolve_opposing_directions(std::uint8_t raw_mask,
                                         DirectionConflictState& state);
