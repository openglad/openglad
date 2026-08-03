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
// resolve_opposing_directions() gives the fresh press priority. It has two
// modes:
//
// Legacy mode (cancel_stale=false, the native default — PR #147 semantics,
// unchanged): while both sides of an axis are held, the side whose key rose
// most recently wins and the stale side is suppressed from the reported
// mask. Both sides rising in the same sample keep the legacy cancellation;
// releasing either side ends the conflict (the stale hold RESUMES — on
// native that hold is a real, physically-held key). Suppression is
// group-level: a suppressed stale diagonal (e.g. UP_LEFT vs. a fresh DOWN)
// stops contributing to BOTH axes — it is presumed phantom.
//
// Cancel mode (cancel_stale=true, engaged on platforms with unreliable
// keyups — see InputHardwareState::unreliable_keyups): the suppression is
// PERSISTENT. A fresh press on one side of an axis cancels the held
// opposite side outright; releasing the fresh key leaves the phantom
// cancelled (dead) instead of letting it resume walking, and a cancelled
// diagonal stops feeding the other axis too (no up-left drift against a
// lone LEFT). A cancelled key comes back only when the record is corrected:
//  - its keyup is finally delivered (the bit leaves the raw mask), or
//  - a REAL key event for it arrives (note_direction_key_event) — browsers
//    deliver a keydown (repeat=true in SDL) for a re-press of a key whose
//    keyup was swallowed, which the sampled keystate alone can never show.
// Both sides rising in the same sample still keep the legacy
// cancellation-to-zero (no persistent cancel).
//
// This runs in the client input sampling (input_state_from_sdl) BEFORE
// coalesce_direction_release, so the wire format and the sim are untouched
// and the result is deterministic per input timeline.

// Per-player conflict state. Masks are bitmasks over the eight direction
// key slots KEY_UP(0)..KEY_UP_LEFT(7) (bit d == key slot d).
struct DirectionConflictState
{
    std::uint8_t prev_raw = 0;  // raw direction mask from the previous sample
    std::int8_t x_winner = 0;   // -1: left side wins, +1: right side, 0: none
    std::int8_t y_winner = 0;   // -1: up side wins, +1: down side, 0: none
    // Cancel mode only: persistently suppressed (presumed-phantom) key bits.
    std::uint8_t cancelled = 0;
    // Pending direction-slot bits reported by the key-EVENT layer since the
    // last sample (see note_direction_key_event); consumed by the resolver.
    std::uint8_t event_edges = 0;
};

// Feed one raw direction-key sample; returns the mask to report (the stale
// opposite side suppressed while a fresher press holds priority; in cancel
// mode the stale side stays cancelled until corrected). Pure and
// deterministic: same timeline of raw masks + event edges -> same outputs.
std::uint8_t resolve_opposing_directions(std::uint8_t raw_mask,
                                         DirectionConflictState& state,
                                         bool cancel_stale = false);

// Key-EVENT layer feed: called from the platform event pump for every
// direction-key keydown (including SDL repeat=true events — the only
// visible signal of a physical re-press of a key whose keyup was
// swallowed). Maps the keycode through every player's ACTIVE bindings
// (per-seat: P2's arrows must not touch P3's cluster) and marks the
// matching event-edge bits. SDL-free (int keycode) so the headless clients
// compile it.
void note_direction_key_event(int keycode);
