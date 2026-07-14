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
