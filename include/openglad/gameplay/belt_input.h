/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// New Ability Select: the key fold. SDL-free, shared by every client that
// samples keys (the SDL sampler, curses, the web touch overlay through the
// touch key state).
//
// A client reads four keys per seat: SPEC 1 / SPEC 2 / SPEC 3 (key slots 9,
// 17, 18) and SHIFTER (slot 13). This file turns them into what the input
// wire carries: the Special and SwitchSpecial bits and the one belt byte
// (PlayerInput::belt_cast, og::sim::belt::encode / kCastNothing; 0 = the
// classic cast). It never writes the wire struct: the caller copies the
// Fold into its PlayerInput.
//
// With the setting off (classic): SPEC 1 is the old SPECIAL key, SPEC 2 is
// the old change-special key, SPEC 3 does nothing, the byte stays 0. With
// the setting on and a walker under the seat: one press = one belt entry,
// latched at the press edge.

#include <openglad/gameplay/families/belt.h>

#include <cstdint>

namespace og::sim::belt {

// One sample of a seat's belt keys. Index 0..2 = SPEC 1..3. `pressed` is the
// sample's down edge (held now, not held the sample before).
struct Keys {
    bool held[3] = {false, false, false};
    bool pressed[3] = {false, false, false};
    bool shift_held = false;
};

// Per seat, kept between samples: the running SPEC key (1..3, 0 = none) and
// the belt byte it latched when it went down.
struct Latch {
    int active_key = 0;
    std::uint8_t byte = 0;
    friend bool operator==(const Latch&, const Latch&) = default;
};

// What one sample sends: the Special and SwitchSpecial bits and the byte.
struct Fold {
    bool special_held = false;
    bool special_pressed = false;
    bool switch_held = false;
    bool switch_pressed = false;
    std::uint8_t belt_cast = 0;
    friend bool operator==(const Fold&, const Fold&) = default;
};

// The belt fold (setting on, a walker under the seat), decision 11:
//   1. A SPEC key with a press edge becomes the running key (the highest one
//      when two go down in one sample). Its entry is read ONCE, from the
//      SHIFTER row when SHIFTER is held at that moment, and latched: letting
//      go of SHIFTER or pressing it mid-hold does not change the running
//      entry.
//   2. special_pressed is set on that sample, so a second SPEC key pressed
//      during a hold is a fresh press, never a hold.
//   3. special_held follows the running key. When it is let go while another
//      SPEC key is still held, that key takes over in the same sample as a
//      fresh press (its entry read then).
//   4. An Empty slot latches kCastNothing (the server says NO ABILITY THERE);
//      a Locked slot latches its entry (the server says which level opens
//      it).
//   5. belt_cast is the latched byte while a key runs, else 0.
// SwitchSpecial is never set: the server must not cycle under the belt.
[[nodiscard]] Fold fold_belt_keys(const Keys& keys, const Layout& belt,
                                  Latch& latch);

// The classic fold (setting off): SPEC 1 is SPECIAL, SPEC 2 is the old
// change-special key, SPEC 3 sends nothing, the byte is 0. Clears the latch.
[[nodiscard]] Fold fold_classic_keys(const Keys& keys, Latch& latch);

// The one call a sampler makes per seat. `belt` is the seat's resolved belt
// when it drives a walker, nullptr when it does not (a menu, a dead seat).
//   off               -> fold_classic_keys
//   on, no walker     -> SPEC 1 is SPECIAL, nothing else, byte 0 (menus read
//                        the Special bit as "select"); clears the latch
//   on, a walker      -> fold_belt_keys
[[nodiscard]] Fold fold_keys(const Keys& keys, bool ability_select,
                             const Layout* belt, Latch& latch);

}  // namespace og::sim::belt
