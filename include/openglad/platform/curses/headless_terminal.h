/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/platform/curses/terminal.h>

#include <deque>
#include <string>
#include <vector>

namespace og::curses {

// In-memory ITerminal for tests. Stores a rows*cols grid of Cells that the most
// recent present() snapshot, accepts a scripted queue of keys, and exposes
// accessors so tests can assert what was drawn. Needs no TTY, so it runs in CI.
class HeadlessTerminal final : public ITerminal
{
public:
    HeadlessTerminal(int rows, int cols);

    // --- ITerminal ---
    int rows() const override { return rows_; }
    int cols() const override { return cols_; }
    bool supports_unicode() const override { return unicode_; }
    bool supports_color() const override { return color_; }
    void clear() override;
    void put(int row, int col, char32_t ch, Color fg, Color bg, bool bold) override;
    void put_str(int row, int col, std::string_view utf8,
                 Color fg, Color bg, bool bold) override;
    void present() override;
    Key poll_key(bool block) override;
    void set_cursor_visible(bool visible) override;
    void beep() override;

    // --- test configuration ---
    void set_unicode(bool v) { unicode_ = v; }
    void set_color(bool v) { color_ = v; }
    void resize(int rows, int cols);

    // Queue scripted input. poll_key() returns these in order; when exhausted,
    // a blocking poll returns Key::none() (tests must script enough keys).
    void push_key(const Key& k) { scripted_keys_.push_back(k); }
    void push_char(char32_t c) { scripted_keys_.push_back(Key::character(c)); }
    void push_special(KeyCode code) { scripted_keys_.push_back(Key::special(code)); }
    // Release events (the Kitty protocol delivers key-up; tests script it to
    // exercise held/release behavior and to prove releases don't double-fire).
    void push_char_release(char32_t c) { scripted_keys_.push_back(Key::character(c, KeyEvent::Release)); }
    void push_special_release(KeyCode code) { scripted_keys_.push_back(Key::special(code, KeyEvent::Release)); }
    // A full keystroke: press then release of the same key.
    void push_char_tap(char32_t c) { push_char(c); push_char_release(c); }
    void push_string(std::string_view ascii); // one Char key per byte
    bool input_exhausted() const { return scripted_keys_.empty(); }

    // --- assertions over the back buffer (current frame being drawn) ---
    const Cell& cell_at(int row, int col) const;
    char32_t char_at(int row, int col) const { return cell_at(row, col).ch; }
    // The row as a UTF-8/ASCII string (Unicode codepoints downgraded to '?').
    std::string text_row(int row) const;
    // Whole buffer joined with newlines (handy for failure messages).
    std::string dump() const;
    // First (row,col) holding `ch`, or {-1,-1} if absent.
    std::pair<int, int> find_char(char32_t ch) const;
    int count_char(char32_t ch) const;

    // --- assertions over the last present()ed frame ---
    const std::vector<Cell>& presented() const { return presented_; }
    char32_t presented_char_at(int row, int col) const;
    int present_count() const { return present_count_; }
    int beep_count() const { return beep_count_; }

private:
    int index(int row, int col) const { return row * cols_ + col; }
    bool in_bounds(int row, int col) const
    {
        return row >= 0 && row < rows_ && col >= 0 && col < cols_;
    }

    int rows_;
    int cols_;
    bool unicode_ = true;
    bool color_ = true;
    std::vector<Cell> buffer_;     // current back buffer
    std::vector<Cell> presented_;  // snapshot of last present()
    std::deque<Key> scripted_keys_;
    int present_count_ = 0;
    int beep_count_ = 0;
    bool cursor_visible_ = true;
};

} // namespace og::curses
