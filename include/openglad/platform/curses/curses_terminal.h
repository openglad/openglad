/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/platform/curses/terminal.h>

#include <memory>

namespace og::curses {

// Real ncurses-backed terminal. Owns the ncurses session (initscr..endwin) for
// its lifetime. Not exercised in CI (needs a TTY); all logic is tested through
// HeadlessTerminal. Construction failure (no usable terminal) throws.
class CursesTerminal final : public ITerminal
{
public:
    struct Options {
        bool use_unicode = true; // attempt UTF-8 glyphs (falls back if unavailable)
        bool use_color = true;   // attempt color (falls back to monochrome)
    };

    CursesTerminal();
    explicit CursesTerminal(Options opt);
    ~CursesTerminal() override;

    CursesTerminal(const CursesTerminal&) = delete;
    CursesTerminal& operator=(const CursesTerminal&) = delete;

    int rows() const override;
    int cols() const override;
    bool supports_unicode() const override;
    bool supports_color() const override;
    void clear() override;
    void put(int row, int col, char32_t ch, Color fg, Color bg, bool bold) override;
    void put_str(int row, int col, std::string_view utf8,
                 Color fg, Color bg, bool bold) override;
    void present() override;
    Key poll_key(bool block) override;
    void set_cursor_visible(bool visible) override;
    void beep() override;

private:
#ifdef TESTING
    friend int curses_terminal_testing_exercise_internal_helpers();
    struct TestingTag {};
    explicit CursesTerminal(TestingTag);
#endif
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::curses
