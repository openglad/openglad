/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/platform/curses/headless_terminal.h>

#include <openglad/platform/curses/text_util.h>

namespace og::curses {

HeadlessTerminal::HeadlessTerminal(int rows, int cols)
    : rows_(rows > 0 ? rows : 1), cols_(cols > 0 ? cols : 1)
{
    const auto total = static_cast<std::size_t>(rows_) * static_cast<std::size_t>(cols_);
    buffer_.assign(total, Cell{});
    presented_.assign(total, Cell{});
}

void HeadlessTerminal::resize(int rows, int cols)
{
    rows_ = rows > 0 ? rows : 1;
    cols_ = cols > 0 ? cols : 1;
    const auto total = static_cast<std::size_t>(rows_) * static_cast<std::size_t>(cols_);
    buffer_.assign(total, Cell{});
    presented_.assign(total, Cell{});
}

void HeadlessTerminal::clear()
{
    for (Cell& c : buffer_)
        c = Cell{};
}

void HeadlessTerminal::put(int row, int col, char32_t ch, Color fg, Color bg, bool bold)
{
    if (!in_bounds(row, col))
        return;
    buffer_[static_cast<std::size_t>(index(row, col))] = Cell{ch, fg, bg, bold};
}

void HeadlessTerminal::put_str(int row, int col, std::string_view utf8,
                               Color fg, Color bg, bool bold)
{
    int c = col;
    std::size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        i += utf8_decode(utf8.substr(i), cp);
        put(row, c, cp, fg, bg, bold);
        ++c;
    }
}

void HeadlessTerminal::present()
{
    presented_ = buffer_;
    ++present_count_;
}

Key HeadlessTerminal::poll_key(bool /*block*/)
{
    if (scripted_keys_.empty())
        return Key::none();
    Key k = scripted_keys_.front();
    scripted_keys_.pop_front();
    return k;
}

void HeadlessTerminal::set_cursor_visible(bool visible)
{
    cursor_visible_ = visible;
}

void HeadlessTerminal::beep()
{
    ++beep_count_;
}

void HeadlessTerminal::push_string(std::string_view ascii)
{
    for (char ch : ascii)
        scripted_keys_.push_back(Key::character(static_cast<char32_t>(static_cast<unsigned char>(ch))));
}

const Cell& HeadlessTerminal::cell_at(int row, int col) const
{
    static const Cell kEmpty{};
    if (!in_bounds(row, col))
        return kEmpty;
    return buffer_[static_cast<std::size_t>(index(row, col))];
}

std::string HeadlessTerminal::text_row(int row) const
{
    std::string out;
    if (row < 0 || row >= rows_)
        return out;
    for (int c = 0; c < cols_; ++c) {
        const char32_t ch = buffer_[static_cast<std::size_t>(index(row, c))].ch;
        if (ch == 0)
            out.push_back(' ');
        else if (ch < 0x80)
            out.push_back(static_cast<char>(ch));
        else
            out.push_back('?');
    }
    return out;
}

std::string HeadlessTerminal::dump() const
{
    std::string out;
    for (int r = 0; r < rows_; ++r) {
        out += text_row(r);
        out.push_back('\n');
    }
    return out;
}

std::pair<int, int> HeadlessTerminal::find_char(char32_t ch) const
{
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c)
            if (buffer_[static_cast<std::size_t>(index(r, c))].ch == ch)
                return {r, c};
    return {-1, -1};
}

int HeadlessTerminal::count_char(char32_t ch) const
{
    int n = 0;
    for (const Cell& cell : buffer_)
        if (cell.ch == ch)
            ++n;
    return n;
}

char32_t HeadlessTerminal::presented_char_at(int row, int col) const
{
    if (!in_bounds(row, col))
        return 0;
    return presented_[static_cast<std::size_t>(index(row, col))].ch;
}

} // namespace og::curses
