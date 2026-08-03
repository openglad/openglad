/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Portable presentation types carried by the family descriptors.
//
// The UI layers used to switch on family id to decide how a family looks:
// the curses client's glyph table, the radar's blip colour, the level
// editor's generator labels. That data now rides on the descriptors, which
// live in `gameplay` and may therefore only name types declared in `core`
// or `gameplay` — hence this header instead of reusing og::curses::Color.
//
// og::GlyphColor deliberately mirrors og::curses::Color entry-for-entry
// (Default..White) and adds one extra member, Team, meaning "paint this
// with the entity's team colour" (flags, control points).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace og {

// Terminal colour vocabulary. Values 0..8 match og::curses::Color exactly,
// so the curses client can static_cast across once it stops switching on
// family id. `Team` has no curses counterpart: it is a marker the renderer
// resolves to the entity's team colour.
enum class GlyphColor : std::uint8_t {
    Default = 0,
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    Team,
};

// How one family draws in a character-cell client. `transparent` is the
// curses Glyph::skip flag ("draw nothing here" — floor stains).
struct FamilyGlyph {
    char32_t codepoint = U'*';
    char ascii = '*';
    GlyphColor color = GlyphColor::Default;
    bool bold = false;
    bool transparent = false;

    friend constexpr bool operator==(const FamilyGlyph&,
                                     const FamilyGlyph&) = default;
};

// Radar (minimap) blip colour sentinels.
inline constexpr int kRadarColorNone = -1;  // family draws no blip
inline constexpr int kRadarColorTeam = -2;  // use the entity's team colour

// One family's radar blip: a palette index (or a sentinel) plus the span of
// the per-frame flicker. `jitter == 0` means "no rng call at all" — the
// legacy code only rolled rng() for the families that flickered, and the
// draw path must keep that call count identical.
struct RadarBlip {
    int color = kRadarColorNone;
    int jitter = 0;  // adds rng(jitter) to color when > 0
    // Landmark families (exits, teleporters, mode flags/waypoints) blip
    // without treasure sight (radar's ignores_view_all rule). Declared by
    // treasure/fx packs via `radar_landmark = true`; the radar consumption
    // of this flag lands with the generic mode renderers.
    bool landmark = false;

    friend constexpr bool operator==(const RadarBlip&,
                                     const RadarBlip&) = default;
};

// --- the declaration's presentation vocabulary ----------------------------

inline constexpr std::array<const char*, 10> kGlyphColorNames = {
    "default", "black",   "red",  "green", "yellow",
    "blue",    "magenta", "cyan", "white", "team",
};

constexpr const char* glyph_color_name(GlyphColor c)
{
    const auto i = static_cast<std::size_t>(c);
    return i < kGlyphColorNames.size() ? kGlyphColorNames[i] : "default";
}

inline bool glyph_color_from_name(std::string_view name, GlyphColor& out)
{
    for (std::size_t i = 0; i < kGlyphColorNames.size(); i++) {
        if (name == kGlyphColorNames[i]) {
            out = static_cast<GlyphColor>(i);
            return true;
        }
    }
    return false;
}

// The `radar_color:` sentinel spellings (any other value is a palette int).
inline constexpr const char* kRadarColorNoneName = "none";
inline constexpr const char* kRadarColorTeamName = "team";

// --- UTF-8 for the single-codepoint `glyph:` scalar -----------------------

// Encodes one codepoint as UTF-8. Returns an empty string for a surrogate
// or an out-of-range value (the exporter treats that as an error).
inline std::string glyph_to_utf8(char32_t codepoint)
{
    std::string out;
    const auto c = static_cast<std::uint32_t>(codepoint);
    if (c < 0x80u) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (c >> 6)));
        out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
    } else if (c < 0x10000u) {
        if (c >= 0xD800u && c <= 0xDFFFu)
            return out;  // lone surrogate: not a scalar value
        out.push_back(static_cast<char>(0xE0u | (c >> 12)));
        out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
    } else if (c <= 0x10FFFFu) {
        out.push_back(static_cast<char>(0xF0u | (c >> 18)));
        out.push_back(static_cast<char>(0x80u | ((c >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (c & 0x3Fu)));
    }
    return out;
}

// Decodes `text` as EXACTLY one well-formed UTF-8 scalar value. Rejects
// empty/multi-character text, overlong encodings, surrogates and values
// above U+10FFFF, so a malformed `glyph:` fails the pack instead of
// planting a garbage codepoint in a descriptor.
inline bool glyph_from_utf8(std::string_view text, char32_t& out)
{
    if (text.empty())
        return false;
    const auto b0 = static_cast<std::uint8_t>(text[0]);
    std::size_t len = 0;
    std::uint32_t value = 0;
    if (b0 < 0x80u) {
        len = 1;
        value = b0;
    } else if ((b0 & 0xE0u) == 0xC0u) {
        len = 2;
        value = b0 & 0x1Fu;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        len = 3;
        value = b0 & 0x0Fu;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        len = 4;
        value = b0 & 0x07u;
    } else {
        return false;  // continuation byte or 5/6-byte lead
    }
    if (text.size() != len)
        return false;
    for (std::size_t i = 1; i < len; i++) {
        const auto b = static_cast<std::uint8_t>(text[i]);
        if ((b & 0xC0u) != 0x80u)
            return false;
        value = (value << 6) | (b & 0x3Fu);
    }
    static constexpr std::uint32_t kMin[5] = {0, 0, 0x80u, 0x800u, 0x10000u};
    if (value < kMin[len])
        return false;  // overlong
    if (value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu))
        return false;
    out = static_cast<char32_t>(value);
    return true;
}

}  // namespace og
