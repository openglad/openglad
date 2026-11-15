/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

// The New Specials per-walker kit marks (walker::kit_state(), a byte that
// rides every snapshot). They are kept apart from the classic bit_flags on
// purpose: transfer_stats copies bit_flags onto a blood stain and into a
// raised body, and transform_to wipes them, so a kit mark kept there would
// leak onto a corpse or vanish on a summon. Neither function touches
// kit_state. No kit transforms a hidden walker, so a transformed body never
// carries a HIDDEN it did not earn.
//
//   KIT_HIDDEN   the walker is inert and unseen by foes: a dug-in skeleton,
//                a ghost riding a possessed body.
//   KIT_FEARLESS the walker never flees (warband grunts).
//   KIT_WARD     the next death is cancelled once (REASSEMBLE).
//   KIT_CHANNEL  a channelled special is running on this walker; the next
//                press ends it and costs nothing (IMMOLATE).
//   KIT_QUARTER_FREEZE  the freeze this weapon lays is a quarter of the
//                rolled one (a faerie's GLIMMER sprinkle).
//   KIT_NOVA     a Supernova blast: its caster is gone, so the blast spares
//                its own team half the damage by team colour.
//   KIT_RIDER    Gameplay Fixes AZ: this weapon or effect rides the walker
//                who owns it (a circle of protection, a mace, a boomerang, a
//                hook blade, a scare, IMMOLATE's fire, a returning knife's
//                probe, the cheat's magic shield) and goes with him: while
//                he lives it fights for whatever side he is on now
//                (walker::alliance_head answers him for it). Set where the
//                thing is made, with the setting on only.
inline constexpr std::uint8_t KIT_HIDDEN = 1;
inline constexpr std::uint8_t KIT_FEARLESS = 2;
inline constexpr std::uint8_t KIT_WARD = 4;
inline constexpr std::uint8_t KIT_CHANNEL = 8;
inline constexpr std::uint8_t KIT_QUARTER_FREEZE = 16;
inline constexpr std::uint8_t KIT_NOVA = 32;
inline constexpr std::uint8_t KIT_RIDER = 64;
