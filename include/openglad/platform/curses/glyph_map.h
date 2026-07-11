/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/platform/curses/terminal.h>

// Order is the entity-kind enum (Living/Weapon/Treasure/Generator/FX/...).
enum class Order : unsigned char;

namespace og::curses {

// A drawable glyph: a Unicode codepoint plus an ASCII fallback, a color, and a
// bold flag. `skip == true` means "draw nothing here" (transparent), used for
// floor decals and transient effects.
struct Glyph {
    char32_t unicode = U' ';
    char ascii = ' ';
    Color fg = Color::Default;
    bool bold = false;
    bool skip = false;

    // Pick the codepoint to render given whether the terminal supports Unicode.
    char32_t pick(bool allow_unicode) const
    {
        return allow_unicode ? unicode : static_cast<char32_t>(static_cast<unsigned char>(ascii));
    }
};

// --- Tiles ---------------------------------------------------------------

// Glyph for a terrain genre (a TYPE_* value from terrain_types.h).
Glyph tile_glyph(int genre);

// Glyph for a tile outside the level's grid — the impassable area beyond the
// arena edge. Drawn distinctly from grass so the boundary is visible (the engine
// reports out-of-range tiles as grass, which would otherwise hide the edge).
Glyph border_glyph();

// Direction-aware Z-stair glyph (roguelike convention: '<' ascends, '>'
// descends). Renderers that can read the raw tile id (PIX_ZSTAIR_UP vs
// PIX_ZSTAIR_DOWN) use this instead of the direction-less
// tile_glyph(TYPE_ZSTAIRS) fallback.
Glyph zstair_glyph(bool up);

// --- Entities ------------------------------------------------------------

// Glyph for a living creature by family id (FAMILY_SOLDIER..FAMILY_TOWER1).
// Color is not set here (the caller applies the team color); this returns the
// shape only, with fg == Color::Default.
Glyph living_glyph(int family);

// Glyph for an effect family (Order::FX — boomerang, magic shield, explosions,
// ...). Every effect family is visible (never skip), so cast specials show.
Glyph effect_glyph(int family);

// Full glyph for any entity. Applies team color, bolds the followed avatar
// (rendered as '@'), and returns skip==true for entities that should not be
// drawn (stains, most FX). `my_team` is the local player's team.
Glyph entity_glyph(Order order, int family, unsigned char team_num,
                   bool is_followed_avatar, unsigned char my_team);

// --- Teams ---------------------------------------------------------------

// Terminal color for a team number (0..7). Out-of-range clamps into the ramp.
Color team_color(unsigned char team_num);

} // namespace og::curses
