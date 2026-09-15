/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

// The horizontal fit rule for the classic HUD's upper-right counter box (the
// TEAM / FOES / WAVE / FLR stack score_panel.cpp draws at rm-57 .. rm-2).
//
// THE RULE, in one sentence: the box's WIDTH is derived from its widest row
// exactly as its HEIGHT is already derived from its row count. Every row is a
// "LABEL: value" string, split at its first ": " by one helper, fitted by one
// helper, and left-aligned at one shared text column left + 2 -- the classic
// rm-55 column. There is no per-row x arithmetic and no right-alignment.
//
//     floor    = min(lm + 66, rm - 57)
//     capacity = (rm - 4 - floor) / 6          glyphs, at the leftmost box
//     left     = min(rm - 57, rm - 4 - 6 * widest_fitted_row)
//
// 6 is the text.png advance (a 5-px glyph plus a 1-px gap; the same literal
// draw_mode_panel budgets with, pinned by
// TextRender.small_font_query_width_is_one_advance_per_character). The 2-px
// chrome on the left and the 4-px margin on the right keep the last glyph
// column at or left of rm-4, the same air the classic box left at rm-55 + 8
// glyphs = rm-7.
//
// THE CLASSIC-MINIMUM GUARANTEE: 57 is a MINIMUM inset, never exceeded
// rightwards. When every present row fits in 8 glyphs the rule returns
// left = rm-57 and text_x = rm-55 -- the classic box and the classic text
// column, byte for byte. That is the envelope every pre-existing HUD pixel
// pin sits in (PREF_FOES-off frames never reach here at all; a no-wave
// single-floor frame is "TEAM: n" / "FOES: n"; "FLR: 2/3" and "F23: 2/3" are
// 8 glyphs). Longer rows grow the box LEFTWARD only.
//
// THE FLOOR: the box may not grow left of lm + 66, the caption column
// draw_mode_panel already uses as its own left edge (the caption's button box
// lm+1 .. lm+63 plus a gap). In a pane narrower than 123 px the classic box
// ALREADY crosses that column (the 1p VIEW_3 108-px pane and the 3p inset
// 100-px pane, a 2002 fact this rule neither fixes nor worsens), so the floor
// is clamped to rm-57 there: the box stays exactly as wide as classic and the
// rows shrink instead.
//
// THE TWO FALLBACK TIERS, applied by fit_row when a row exceeds the capacity
// at the floor: (1) drop the LABEL and draw the value alone ("FOES: 1+2" ->
// "1+2", "WAVE: 12s" -> "12s", "FLR: 10/12" -> "10/12"); (2) if even the bare
// value overflows, truncate it to the capacity. Tier 1 keeps a readout terse
// rather than misleading -- truncating "WAVE: 12s" from the right would print
// "WAVE: 12", which reads as a COUNT of 12.
//
// ACCEPTED 1-PX WIDENING: a 9-glyph classic row ("TEAM: 100" / "FOES: 100"
// with no wave) now makes the box rm-58 instead of rm-57. Classic drew those
// 9 glyphs onto the box's own interior margin; the rule has no exception for
// them, because an exception is a rule twin. Only >= 100 allies or foes with
// no pending wave reach it.
//
// Header-only and dependency-free (the view_layout.h precedent) so the pure
// arithmetic can be pinned headlessly at every pane width without a
// numviews/PREF_VIEW dance: tests/unit/test_hud_counter_box.cpp, og_unit_core.

namespace og::hud_counter_box
{

// text.png advance: a 5-px glyph plus its 1-px gap.
inline constexpr int kGlyphAdvance = 6;
// The classic box's left inset from the pane's right margin (rm-57 .. rm-2).
inline constexpr int kClassicLeftInset = 57;
// The box's right edge, inset from rm.
inline constexpr int kRightInset = 2;
// The shared text column, inset from the box's left edge.
inline constexpr int kTextInset = 2;
// The last glyph column may not pass rm - kRightMargin.
inline constexpr int kRightMargin = 4;
// The caption column draw_mode_panel uses as its own left edge.
inline constexpr int kCaptionFloor = 66;
// TEAM, FOES, WAVE, FLR.
inline constexpr int kMaxRows = 4;

struct Row
{
    std::string label;
    std::string value;

    friend bool operator==(const Row&, const Row&) = default;
};

// Split at the FIRST ": ". A string without one is all value (an unlabeled
// row still goes through the same fit).
inline Row split_row(std::string_view s)
{
    const std::size_t at = s.find(": ");
    if (at == std::string_view::npos)
        return Row{std::string(), std::string(s)};
    return Row{std::string(s.substr(0, at)), std::string(s.substr(at + 2))};
}

inline std::string join(const Row& r)
{
    if (r.label.empty())
        return r.value;
    return r.label + ": " + r.value;
}

// Tier 0: the whole row. Tier 1: the value alone. Tier 2: the value cut to
// the capacity.
inline std::string fit_row(const Row& r, int capacity)
{
    const std::size_t room =
        capacity > 0 ? static_cast<std::size_t>(capacity) : 0u;
    const std::string full = join(r);
    if (full.size() <= room)
        return full;
    if (r.value.size() <= room)
        return r.value;
    return r.value.substr(0, room);
}

struct Layout
{
    // Box rectangle: left .. rm - kRightInset (the caller owns the y axis).
    int left = 0;
    // The one column every row is drawn at.
    int text_x = 0;
    // Glyphs that fit between text_x and rm - kRightMargin.
    int capacity = 0;
    // Fitted text per row, empty for an absent row.
    std::array<std::string, kMaxRows> text{};
};

// lm/rm are this viewport's left and right margins (score_panel's own
// xloc + OVERSCAN_PADDING / endx - OVERSCAN_PADDING).
inline Layout fit(const std::array<std::optional<Row>, kMaxRows>& rows,
                  int lm, int rm)
{
    const int floor = std::min(lm + kCaptionFloor, rm - kClassicLeftInset);
    const int cap_max =
        std::max(0, (rm - kRightMargin - floor) / kGlyphAdvance);

    Layout out;
    int widest = 0;
    for (int i = 0; i < kMaxRows; ++i)
    {
        if (!rows[static_cast<std::size_t>(i)].has_value())
            continue;
        out.text[static_cast<std::size_t>(i)] =
            fit_row(*rows[static_cast<std::size_t>(i)], cap_max);
        widest = std::max(
            widest,
            static_cast<int>(out.text[static_cast<std::size_t>(i)].size()));
    }

    out.left = std::min(rm - kClassicLeftInset,
                        rm - kRightMargin - kGlyphAdvance * widest);
    out.text_x = out.left + kTextInset;
    out.capacity = (rm - kRightMargin - out.left) / kGlyphAdvance;
    return out;
}

} // namespace og::hud_counter_box
