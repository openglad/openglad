/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// A truecolour picture as the game keeps it in memory: w x h pixels, four
// bytes each (R, G, B, A), row-major with no padding. The keyboard picture
// (og::ui::keyboard_picture, on KEYMAP and the CHANGELOG! page) is the one
// user: it is rendered into one at run time from the glyph atlas
// pix/keyboard/glyphs.png (read by og::io::read_rgba_png). Every sprite stays
// an indexed pixie; this is for pictures that are drawn at the display's own
// resolution instead.

#include <atomic>
#include <cstdint>
#include <vector>

namespace og::io {

// A fresh picture identity (never 0). Whoever fills an RgbaImage's bytes
// gives it a new serial, so a drawing cache keyed on the serial can never
// mistake a later picture that reuses the same heap block for the old one.
inline std::uint64_t next_rgba_image_serial()
{
    static std::atomic<std::uint64_t> last{0};
    return ++last;
}

struct RgbaImage {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> bytes;  // w * h * 4
    // 0: no identity, never cached by a drawer (see next_rgba_image_serial).
    std::uint64_t serial = 0;
};

}  // namespace og::io
