/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace og::curses {

// Abstract terminal colors. Mapped to concrete terminal colors by the backend.
// "Default" means the terminal's default fg/bg.
enum class Color : std::uint8_t {
    Default = 0,
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
};

// A single rendered cell (used by HeadlessTerminal for assertions).
struct Cell {
    char32_t ch = U' ';
    Color fg = Color::Default;
    Color bg = Color::Default;
    bool bold = false;
};

// A key event. When code == Char, `ch` holds the Unicode codepoint; otherwise
// `code` names a special key. None means "no key available" (non-blocking poll).
//
// With the Kitty keyboard protocol (the only input path the curses client
// supports), keys carry a transition type (press/repeat/release), real modifier
// state, and standalone modifier-key events — so the named set below is far richer
// than the legacy ncurses getch() vocabulary.
enum class KeyCode : std::int16_t {
    None = 0,
    Char, // printable: `ch` holds the codepoint
    // editing / navigation
    Up, Down, Left, Right,
    Enter, Escape, Tab, Backspace,
    Home, End, PageUp, PageDown, Insert, Delete,
    // modifier keys, reported standalone by the protocol (e.g. Fire = LeftCtrl)
    LeftShift, LeftCtrl, LeftAlt, LeftSuper,
    RightShift, RightCtrl, RightAlt, RightSuper,
    // function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // terminal lifecycle signals (not physical keys)
    Resize, FocusIn, FocusOut,
};

// A key event's transition type. Values match the Kitty protocol's event-type
// codes so the decoder can use them directly.
enum class KeyEvent : std::uint8_t {
    Press = 1,
    Repeat = 2,
    Release = 3,
};

// Modifier keys held at the time of a key event.
struct KeyMods {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;

    bool any() const { return shift || ctrl || alt || super; }
    bool operator==(const KeyMods&) const = default;
};

struct Key {
    KeyCode code = KeyCode::None;
    char32_t ch = 0;                    // valid when code == Char
    KeyEvent event = KeyEvent::Press;   // press / repeat / release
    KeyMods mods{};                     // modifiers active for this event

    static Key none() { return Key{}; }
    static Key character(char32_t c) { return Key{KeyCode::Char, c}; }
    static Key special(KeyCode k) { return Key{k, 0}; }
    // Event-typed builders (the decoder and tests construct repeat/release/mods).
    static Key character(char32_t c, KeyEvent e, KeyMods m = {}) { return Key{KeyCode::Char, c, e, m}; }
    static Key special(KeyCode k, KeyEvent e, KeyMods m = {}) { return Key{k, 0, e, m}; }

    bool is_none() const { return code == KeyCode::None; }
    bool is_char() const { return code == KeyCode::Char; }
    // True when this is the given printable character (case-sensitive).
    bool is_char(char32_t c) const { return code == KeyCode::Char && ch == c; }
    // True for the platform "confirm" keys (Enter, or a literal newline/space).
    bool is_enter() const { return code == KeyCode::Enter || ch == U'\n' || ch == U'\r'; }

    // Event-type predicates.
    bool is_press() const { return event == KeyEvent::Press; }
    bool is_repeat() const { return event == KeyEvent::Repeat; }
    bool is_release() const { return event == KeyEvent::Release; }
    // "Down" == physically down this event: a fresh press or an auto-repeat.
    bool is_down() const { return event == KeyEvent::Press || event == KeyEvent::Repeat; }
};

// Minimal terminal surface. All rendering/menu/input logic depends only on this
// interface, so it can be exercised headlessly with HeadlessTerminal (no TTY).
class ITerminal
{
public:
    virtual ~ITerminal() = default;

    virtual int rows() const = 0;
    virtual int cols() const = 0;

    // Whether the backend can render non-ASCII glyphs / use color. Renderers use
    // these to pick Unicode-vs-ASCII and colored-vs-monochrome output.
    virtual bool supports_unicode() const = 0;
    virtual bool supports_color() const = 0;

    virtual void clear() = 0;
    virtual void put(int row, int col, char32_t ch, Color fg, Color bg, bool bold) = 0;
    virtual void put_str(int row, int col, std::string_view utf8,
                         Color fg, Color bg, bool bold) = 0;
    virtual void present() = 0; // flush the back buffer to the screen

    // Read one key. When block is false, returns Key::none() if nothing is ready.
    virtual Key poll_key(bool block) = 0;

    virtual void set_cursor_visible(bool visible) = 0;
    virtual void beep() = 0;
};

} // namespace og::curses
