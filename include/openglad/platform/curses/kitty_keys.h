/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * A pure decoder for the Kitty keyboard protocol (the "CSI u" progressive
 * keyboard enhancement). It turns the raw byte stream a supporting terminal emits
 * into rich Key events — press / repeat / release, real modifier state, and
 * standalone modifier keys — none of which legacy ncurses getch() can deliver.
 *
 * This file has NO terminal/ncurses dependency: CursesTerminal does the tty I/O
 * (reading bytes, writing the enable/disable sequences) and hands the bytes here.
 * That keeps the fiddly parsing fully unit-testable in CI (no TTY) — see
 * tests/curses/test_kitty_keys.cpp.
 */
#pragma once

#include <openglad/platform/curses/terminal.h>

#include <string>
#include <string_view>

namespace og::curses::kitty {

// --- Control sequences CursesTerminal writes to the terminal ---------------

// Capability probe: ask whether the protocol is supported, then immediately ask
// for Primary Device Attributes. A supporting terminal answers the first with
// "CSI ? <flags> u"; EVERY terminal answers the DA with "CSI ? ... c", which
// bounds the wait — if the DA reply arrives with no preceding "u" reply, the
// terminal does not support the protocol.
inline constexpr std::string_view kQuery = "\x1b[?u\x1b[c";

// Push our enhancement flags onto the terminal's keyboard-mode stack:
//   1  disambiguate escape codes (Esc/Tab/Ctrl+key unambiguous)
//   2  report event types        (press / repeat / release — i.e. key-up!)
//   8  report all keys as escape codes (even plain letters, so they get releases)
// 1 | 2 | 8 == 11.
inline constexpr std::string_view kEnable = "\x1b[>11u";
// Pop one level back off that stack (undoes kEnable) on shutdown.
inline constexpr std::string_view kDisable = "\x1b[<u";
// Focus-change reporting (DEC mode 1004): lets us drop held keys when the window
// loses focus, so a key held during an alt-tab cannot get stuck "down".
inline constexpr std::string_view kEnableFocus = "\x1b[?1004h";
inline constexpr std::string_view kDisableFocus = "\x1b[?1004l";

// Inspect a capability-handshake reply. Returns true if it contains the kitty
// response (a private "CSI ? ... u"). Sets `done` true once the DA reply
// ("CSI ? ... c") is seen, signalling the handshake is complete.
bool response_indicates_support(std::string_view reply, bool& done);

// Incremental decoder. Feed it raw terminal bytes (in whatever chunks read()
// returns) and pull complete Key events out one at a time. Partial sequences are
// retained until the rest of their bytes arrive.
class Decoder
{
public:
    void feed(std::string_view bytes) { buf_.append(bytes); }

    // Pop the next fully-buffered key event into `out`. Returns false when more
    // bytes are needed (the incomplete tail stays buffered). Non-key sequences
    // (capability/DA responses) are silently consumed.
    bool next(Key& out);

    bool empty() const { return buf_.empty(); }
    void clear() { buf_.clear(); }

private:
    std::string buf_;
};

} // namespace og::curses::kitty
