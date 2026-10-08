/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/families/family_descriptor.h>

class walker;

// The specials table AS THIS SESSION SEES IT. A special or an alternate a
// family declares with `new_kit = true` exists only while the session's New
// Specials setting (GameWorld::new_specials) is on. With the setting off
// every answer here equals the registry as it was before the setting
// existed: such a slot reads "NONE" and costs kSpecialCostDisabled, so no
// player, HUD or bot can tell it is there. Every reader of a family's
// special names or costs goes through these functions; the stamped
// per-walker costs are never gated at load time, so the order in which a
// world learns its setting does not matter.
//
// No RNG, no side effects. A null descriptor reads as a classic family.
namespace og::sim {

// The slot is not hidden by the setting. A classic slot is always in play
// (even a "NONE" one: its stamped cost already says it cannot be cast).
[[nodiscard]] bool special_in_play(const FamilyDescriptor* fd, int slot,
                                   short new_specials) noexcept;
// The slot is in play AND its alternate is not hidden by the setting. The
// alternate is subordinate to its slot: a hidden slot hides its alternate.
[[nodiscard]] bool alternate_in_play(const FamilyDescriptor* fd, int slot,
                                     short new_specials) noexcept;
// The HUD name; kSpecialNameNone when the slot is hidden.
[[nodiscard]] const char* special_name(const FamilyDescriptor* fd, int slot,
                                       short new_specials) noexcept;
// The alternate's HUD name; kSpecialNameNone when it is not in play.
[[nodiscard]] const char* alternate_name(const FamilyDescriptor* fd, int slot,
                                         short new_specials) noexcept;
// The alternate's own declared price when it is in play and declares one;
// otherwise 0, which means "the primary's price".
[[nodiscard]] unsigned short alternate_cost_in_play(const FamilyDescriptor* fd,
                                                    int slot,
                                                    short new_specials) noexcept;
// The session's setting as the sim reads it: the current world's flag, or 0
// when there is no world.
[[nodiscard]] short session_new_specials() noexcept;
// The price the engine gates a cast on and charges, in this order:
//   1. the slot is hidden by the setting   -> kSpecialCostDisabled
//                                             (the shift does not matter)
//   2. the walker is HIDDEN or CHANNEL     -> 0 (a second press surfaces or
//                                             ends the running special, free)
//   3. shifted, the alternate is in play and priced -> that price
//   4. otherwise                           -> the walker's stamped cost
[[nodiscard]] unsigned short cast_cost(const walker& w, int slot,
                                       bool shifted) noexcept;
// A bot that rolled the shift but cannot pay for the priced alternate of
// `slot` drops the shift (living::check_special). Always false while the
// setting is off: no classic alternate declares a price.
[[nodiscard]] bool cannot_afford_priced_alternate(const walker& w,
                                                  int slot) noexcept;

}  // namespace og::sim
