/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

// The roles a New Specials kit marker (the invisible core:kit_marker effect
// the kits hang their timers on) can play. The role is stored in the
// marker's ani_type byte, which rides every snapshot, so a mirror can tell
// a dig-in clock from a ward clock. This is the one list: the Lua kits read
// it as og.C.MARKER_* through packs/core/lib/kit_marker.lua, and the HUD's
// countdown (timed_effects.h) labels a running marker by it.
//
//   MARKER_BURROW      a dug-in skeleton (DIG IN)
//   MARKER_LEGION      the window in which a skeleton's kills rise (LEGION)
//   MARKER_IMMOLATION  a burning fire elemental (IMMOLATE)
//   MARKER_PHASE_VEIL  a phased ghost (PHASE)
//   MARKER_METEOR_RAIN a running meteor rain (not shown as a countdown)
//   MARKER_WARD        a skeleton's armed REASSEMBLE ward
inline constexpr std::uint8_t MARKER_BURROW = 1;
inline constexpr std::uint8_t MARKER_LEGION = 2;
inline constexpr std::uint8_t MARKER_IMMOLATION = 3;
inline constexpr std::uint8_t MARKER_PHASE_VEIL = 4;
inline constexpr std::uint8_t MARKER_METEOR_RAIN = 5;
inline constexpr std::uint8_t MARKER_WARD = 6;
