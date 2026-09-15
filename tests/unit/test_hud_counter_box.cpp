// Pins for og::hud_counter_box — the pure horizontal fit rule the classic
// HUD's upper-right counter box (TEAM / FOES / WAVE / FLR) follows.
//
// The rule, stated once in the header and implemented once there: the box's
// WIDTH is derived from its widest row exactly as its HEIGHT is derived from
// its row count. Every row is a "LABEL: value" string split at its first
// ": ", fitted to the capacity at the caption-column floor, and left-aligned
// at one shared column left + 2.
//
//     floor    = min(lm + 66, rm - 57)
//     capacity = (rm - 4 - floor) / 6
//     left     = min(rm - 57, rm - 4 - 6 * widest_fitted_row)
//
// The cases below are the geometry score_panel.cpp draws from, at the pane
// widths the game actually produces: the full 320-px classic pane, the 4p
// 159-px quadrant, the 1p VIEW_3 108-px inset and the 3p 100-px inset. They
// pin what a maintainer would notice broken — the box's left edge, the shared
// text column, the capacity, and which of the two fallback tiers each row
// lands in — never merely that the call returned.

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>

#include <openglad/interface/hud_counter_box.h>

using og::hud_counter_box::fit;
using og::hud_counter_box::fit_row;
using og::hud_counter_box::join;
using og::hud_counter_box::kMaxRows;
using og::hud_counter_box::Layout;
using og::hud_counter_box::Row;
using og::hud_counter_box::split_row;

namespace {

// The four row slots score_panel fills, in its order: TEAM, FOES, WAVE, FLR.
using Rows = std::array<std::optional<Row>, kMaxRows>;

Rows rows_of(std::initializer_list<const char*> present)
{
    Rows rows;
    std::size_t i = 0;
    for (const char* s : present)
    {
        if (s != nullptr)
            rows[i] = split_row(s);
        ++i;
    }
    return rows;
}

} // namespace

// --- the split -------------------------------------------------------------

TEST(HudCounterBox, split_row_cuts_at_the_first_label_separator)
{
    const Row r = split_row("F23: 2/3");
    EXPECT_EQ("F23", r.label) << "the tower floor row's label";
    EXPECT_EQ("2/3", r.value) << "the value keeps its own slash";
    EXPECT_EQ("F23: 2/3", join(r)) << "join is split_row's inverse";
}

TEST(HudCounterBox, split_row_with_no_separator_is_all_value)
{
    const Row r = split_row("12s");
    EXPECT_EQ("", r.label) << "an unlabeled row has nothing to drop";
    EXPECT_EQ("12s", r.value);
    EXPECT_EQ("12s", join(r)) << "join must not invent a separator";
}

TEST(HudCounterBox, split_row_keeps_later_separators_in_the_value)
{
    const Row r = split_row("WAVE: 1: 2");
    EXPECT_EQ("WAVE", r.label) << "the FIRST separator, not the last";
    EXPECT_EQ("1: 2", r.value);
}

// --- the three fit tiers ---------------------------------------------------

TEST(HudCounterBox, fit_row_keeps_the_whole_row_when_it_fits)
{
    EXPECT_EQ("FOES: 1+2", fit_row(split_row("FOES: 1+2"), 9))
        << "exactly the capacity is still a fit";
    EXPECT_EQ("FOES: 1+2", fit_row(split_row("FOES: 1+2"), 40));
}

TEST(HudCounterBox, fit_row_drops_the_label_before_it_truncates)
{
    EXPECT_EQ("1+2", fit_row(split_row("FOES: 1+2"), 8))
        << "one glyph short: the label goes, the count survives whole";
    EXPECT_EQ("12s", fit_row(split_row("WAVE: 12s"), 8))
        << "truncating to 'WAVE: 12' would read as a COUNT of 12";
    EXPECT_EQ("10/12", fit_row(split_row("FLR: 10/12"), 8))
        << "the floor row drops its label through the same helper";
}

TEST(HudCounterBox, fit_row_truncates_only_when_the_bare_value_overflows)
{
    EXPECT_EQ("32767+32", fit_row(split_row("FOES: 32767+32767"), 8))
        << "tier 2: even the bare value is 11 glyphs";
    EXPECT_EQ("", fit_row(split_row("FOES: 1+2"), 0))
        << "a zero capacity draws nothing at all";
}

// --- the classic-minimum envelope ------------------------------------------

TEST(HudCounterBox, short_rows_reproduce_the_classic_55px_box)
{
    const Layout l = fit(rows_of({"TEAM: 1", "FOES: 1"}), 0, 320);
    EXPECT_EQ(263, l.left) << "rm - 57: the classic box, byte for byte";
    EXPECT_EQ(265, l.text_x) << "rm - 55: the classic text column";
    EXPECT_EQ(8, l.capacity) << "(320 - 4 - 263) / 6";
    EXPECT_EQ("TEAM: 1", l.text[0]) << "no row is refitted at this width";
    EXPECT_EQ("FOES: 1", l.text[1]);
    EXPECT_EQ("", l.text[2]) << "absent rows stay empty";
    EXPECT_EQ("", l.text[3]);
}

TEST(HudCounterBox, a_nine_glyph_classic_row_widens_the_box_by_one_pixel)
{
    const Layout l = fit(rows_of({"TEAM: 100"}), 0, 320);
    EXPECT_EQ(262, l.left)
        << "rm - 4 - 6*9: the accepted 1-px widening at >= 100 allies "
           "(classic drew the 9th glyph onto the box's own margin)";
    EXPECT_EQ(264, l.text_x);
    EXPECT_EQ(9, l.capacity);
}

// --- the pane widths the game produces -------------------------------------

TEST(HudCounterBox, a_pending_wave_widens_the_full_pane_box_to_its_foes_row)
{
    const Layout l =
        fit(rows_of({"TEAM: 1", "FOES: 12+34", "WAVE: 120s"}), 0, 320);
    EXPECT_EQ(250, l.left) << "widest row 11 glyphs -> rm - 4 - 66";
    EXPECT_EQ(252, l.text_x) << "one shared column for all three rows";
    EXPECT_EQ(11, l.capacity);
    EXPECT_EQ("TEAM: 1", l.text[0]) << "nothing is dropped at 320 px";
    EXPECT_EQ("FOES: 12+34", l.text[1]);
    EXPECT_EQ("WAVE: 120s", l.text[2]);
}

TEST(HudCounterBox, a_two_digit_floor_row_sets_the_width_by_itself)
{
    const Layout l =
        fit(rows_of({"TEAM: 1", "FOES: 1", nullptr, "FLR: 10/12"}), 0, 320);
    EXPECT_EQ(256, l.left) << "widest row 10 glyphs -> rm - 4 - 60";
    EXPECT_EQ(258, l.text_x);
    EXPECT_EQ(10, l.capacity);
    EXPECT_EQ("FLR: 10/12", l.text[3])
        << "the FLR row goes through the identical fit path";
}

TEST(HudCounterBox, the_4p_quadrant_pane_still_holds_the_worst_real_rows)
{
    // 159-px quadrant: "FOES: 100+250" (13) and "WAVE: 5462s" (11, the
    // uint16 spawn delay's ceiling) both fit un-dropped.
    const Layout l =
        fit(rows_of({nullptr, "FOES: 100+250", "WAVE: 5462s"}), 0, 159);
    EXPECT_EQ(77, l.left) << "rm - 4 - 6*13, still right of the floor 66";
    EXPECT_EQ(79, l.text_x);
    EXPECT_EQ(13, l.capacity);
    EXPECT_EQ("FOES: 100+250", l.text[1]) << "no tier is reached at 159 px";
    EXPECT_EQ("WAVE: 5462s", l.text[2]);
}

TEST(HudCounterBox, a_148px_pane_drops_the_label_of_the_row_that_overflows)
{
    // floor = min(66, 91) = 66, capacity there = (148 - 4 - 66) / 6 = 13, so
    // a 14-glyph row is exactly one glyph too wide: "FOES: 100+2500" loses its
    // label, and "WAVE: 5462s" (11) keeps its own and sets the width. The
    // boundary is deliberate -- a caption floor one row-height further left
    // would hold 14 and leave the label on.
    const Layout l =
        fit(rows_of({nullptr, "FOES: 100+2500", "WAVE: 5462s"}), 0, 148);
    EXPECT_EQ("100+2500", l.text[1]) << "tier 1, not a truncation";
    EXPECT_EQ("WAVE: 5462s", l.text[2]) << "a fitting row keeps its label";
    EXPECT_EQ(78, l.left) << "rm - 4 - 6*11, the widest FITTED row";
    EXPECT_EQ(80, l.text_x);
    EXPECT_EQ(11, l.capacity);
}

TEST(HudCounterBox, the_108px_inset_pane_may_not_grow_past_the_classic_box)
{
    // The 1p VIEW_3 pane (106..214): classic ALREADY crosses the caption
    // column here, so floor = min(106 + 66, 214 - 57) = 157 = rm - 57.
    const Layout l =
        fit(rows_of({"TEAM: 1", "FOES: 1+2", "WAVE: 12s"}), 106, 214);
    EXPECT_EQ(157, l.left) << "rm - 57: exactly as wide as classic, no wider";
    EXPECT_EQ(159, l.text_x) << "the classic rm - 55 column";
    EXPECT_EQ(8, l.capacity) << "(214 - 4 - 157) / 6";
    EXPECT_EQ("TEAM: 1", l.text[0]) << "7 glyphs: still whole";
    EXPECT_EQ("1+2", l.text[1]) << "9 glyphs at capacity 8: the label goes";
    EXPECT_EQ("12s", l.text[2]);
}

TEST(HudCounterBox, the_100px_inset_pane_fits_the_same_rows_the_same_way)
{
    // The 3p inset pane is 100 px wide: floor = min(4 + 66, 104 - 57) = 47.
    const Layout l =
        fit(rows_of({"TEAM: 1", "FOES: 1+2", "WAVE: 12s"}), 4, 104);
    EXPECT_EQ(47, l.left) << "rm - 57 again: the narrow-pane clamp";
    EXPECT_EQ(49, l.text_x);
    EXPECT_EQ(8, l.capacity);
    EXPECT_EQ("TEAM: 1", l.text[0]);
    EXPECT_EQ("1+2", l.text[1]);
    EXPECT_EQ("12s", l.text[2]);
}

TEST(HudCounterBox, an_absurd_value_is_cut_to_the_capacity_not_hung_off_the_box)
{
    const Layout l = fit(rows_of({nullptr, "FOES: 32767+32767"}), 4, 104);
    EXPECT_EQ("32767+32", l.text[1])
        << "tier 2 in the narrowest pane: 8 glyphs, inside the box";
    EXPECT_EQ(47, l.left) << "a truncated row cannot widen the box";
    EXPECT_EQ(8, l.capacity);
}

// --- the two bounds, over every pane width and row length ------------------

TEST(HudCounterBox, the_box_never_passes_the_classic_edge_or_the_caption_floor)
{
    int checked = 0;
    for (const int lm : {0, 4, 106})
    {
        for (int width = 100; width <= 320; ++width)
        {
            const int rm = lm + width;
            const int floor =
                std::min(lm + og::hud_counter_box::kCaptionFloor,
                         rm - og::hud_counter_box::kClassicLeftInset);
            for (int len = 0; len <= 40; ++len)
            {
                Rows rows;
                rows[1] = split_row("FOES: " + std::string(
                                        static_cast<std::size_t>(len), 'X'));
                const Layout l = fit(rows, lm, rm);
                ASSERT_LE(l.left, rm - og::hud_counter_box::kClassicLeftInset)
                    << "the box may never be narrower than classic (lm=" << lm
                    << " rm=" << rm << " len=" << len << ")";
                ASSERT_GE(l.left, floor)
                    << "the box may never cross the caption floor (lm=" << lm
                    << " rm=" << rm << " len=" << len << ")";
                ASSERT_EQ(l.left + og::hud_counter_box::kTextInset, l.text_x)
                    << "one shared text column, always left + 2";
                ASSERT_LE(static_cast<int>(l.text[1].size()), l.capacity)
                    << "the fitted row must fit the capacity it was fitted to "
                       "(lm=" << lm << " rm=" << rm << " len=" << len << ")";
                ASSERT_EQ((rm - og::hud_counter_box::kRightMargin - l.left) /
                              og::hud_counter_box::kGlyphAdvance,
                          l.capacity)
                    << "capacity is the glyph count between the box's left "
                       "edge and the right margin";
                ++checked;
            }
        }
    }
    EXPECT_EQ(3 * 221 * 41, checked)
        << "every pane width 100..320 at three left margins, every row length "
           "0..40 -- an exerciser that early-returns pins nothing";
}
