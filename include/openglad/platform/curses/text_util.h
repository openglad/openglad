/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace og::curses {

// Encode a single Unicode codepoint to UTF-8, appending to `out`.
inline void utf8_append(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline std::string utf8_encode(char32_t cp)
{
    std::string s;
    utf8_append(s, cp);
    return s;
}

// Decode the first codepoint from a UTF-8 string, writing it to `out` and
// returning the number of bytes consumed (>=1). Invalid sequences decode the
// single leading byte as a Latin-1 codepoint.
inline std::size_t utf8_decode(std::string_view s, char32_t& out)
{
    if (s.empty()) {
        out = 0;
        return 0;
    }
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) {
        out = b0;
        return 1;
    }
    auto cont = [&](std::size_t i) -> bool {
        return i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80;
    };
    if ((b0 & 0xE0) == 0xC0 && cont(1)) {
        out = static_cast<char32_t>(((b0 & 0x1F) << 6) |
                                    (static_cast<unsigned char>(s[1]) & 0x3F));
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        out = static_cast<char32_t>(((b0 & 0x0F) << 12) |
                                    ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                                    (static_cast<unsigned char>(s[2]) & 0x3F));
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        out = static_cast<char32_t>(((b0 & 0x07) << 18) |
                                    ((static_cast<unsigned char>(s[1]) & 0x3F) << 12) |
                                    ((static_cast<unsigned char>(s[2]) & 0x3F) << 6) |
                                    (static_cast<unsigned char>(s[3]) & 0x3F));
        return 4;
    }
    out = b0; // invalid lead byte: treat as Latin-1
    return 1;
}

} // namespace og::curses
