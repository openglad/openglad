// Pins for og::ui::compute_scroll_view_layout (issue #156): the scrolling
// text viewer's scroll-chrome geometry. The "not scrollable" pins are the
// byte-identity contract — every short-briefing campaign renders exactly the
// legacy dialog.
#include <gtest/gtest.h>

#include <openglad/interface/ui/scroll_view_layout.h>

using og::ui::compute_scroll_view_layout;
using og::ui::ScrollRect;
using og::ui::ScrollViewLayout;

namespace {

// bottomrow exactly as scroll_text_view derives it (clamped at 0).
int bottomrow_for(int num_lines)
{
    const int raw = num_lines * 8 - 14 * 8;
    return raw > 0 ? raw : 0;
}

ScrollViewLayout scenario_layout(int num_lines, int linesdown = 0)
{
    return compute_scroll_view_layout(num_lines, 200, linesdown,
                                      bottomrow_for(num_lines), 0, 0, 320,
                                      200);
}

ScrollViewLayout intro_layout(int num_lines, int linesdown = 0)
{
    return compute_scroll_view_layout(num_lines, 240, linesdown,
                                      bottomrow_for(num_lines), 36, 28, 244,
                                      119);
}

bool rects_disjoint(const ScrollRect& a, const ScrollRect& b)
{
    return a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y ||
           b.y + b.h <= a.y;
}

} // namespace

TEST(ScrollViewLayout, short_text_pins_the_literal_legacy_scenario_frame)
{
    for (int num_lines = 0; num_lines <= 14; ++num_lines)
    {
        const ScrollViewLayout l = scenario_layout(num_lines);
        EXPECT_FALSE(l.scrollable) << num_lines;
        EXPECT_EQ(36, l.frame_x1);
        EXPECT_EQ(28, l.frame_y1);
        EXPECT_EQ(240, l.frame_x2);
        EXPECT_EQ(147, l.frame_y2);
        EXPECT_EQ(0, l.blit_x);
        EXPECT_EQ(0, l.blit_y);
        EXPECT_EQ(320, l.blit_w);
        EXPECT_EQ(200, l.blit_h);
    }
}

TEST(ScrollViewLayout, short_text_pins_the_literal_legacy_intro_frame)
{
    const ScrollViewLayout l = intro_layout(9);
    EXPECT_FALSE(l.scrollable);
    EXPECT_EQ(36, l.frame_x1);
    EXPECT_EQ(28, l.frame_y1);
    EXPECT_EQ(280, l.frame_x2);
    EXPECT_EQ(147, l.frame_y2);
    // The caller's blit rect is untouched (244x119 from (36,28)).
    EXPECT_EQ(36, l.blit_x);
    EXPECT_EQ(28, l.blit_y);
    EXPECT_EQ(244, l.blit_w);
    EXPECT_EQ(119, l.blit_h);
}

TEST(ScrollViewLayout, gladiator_scen1_gets_the_gutter)
{
    // 21 lines = the first level a new player sees.
    const ScrollViewLayout l = scenario_layout(21);
    EXPECT_TRUE(l.scrollable);
    EXPECT_EQ(258, l.frame_x2) << "frame widens by the 18px gutter";
    EXPECT_EQ(147, l.frame_y2);
    EXPECT_EQ(320, l.blit_w) << "full-screen blit stays full-screen";
}

TEST(ScrollViewLayout, long_intro_blit_widens_to_exactly_the_widened_frame)
{
    // The widen arm is `blit_w = frame_x2 + 1 - blit_x` and NOTHING else: the
    // campaign intro's 244x119 rect at (36,28) becomes exactly 263 wide, not
    // "at least wide enough" and not "out to the screen edge" (284).
    const ScrollViewLayout l = intro_layout(34);
    EXPECT_TRUE(l.scrollable);
    EXPECT_EQ(298, l.frame_x2) << "240px box + the 18px gutter";
    EXPECT_EQ(36, l.blit_x) << "the widen arm never moves the blit origin";
    EXPECT_EQ(263, l.blit_w) << "frame_x2 + 1 - blit_x, exactly";
    EXPECT_EQ(28, l.blit_y) << "the caller's y is untouched";
    EXPECT_EQ(119, l.blit_h) << "the caller's height is untouched";
}

TEST(ScrollViewLayout, controls_sit_inside_the_widened_frame_and_are_disjoint)
{
    for (int num_lines : {15, 21, 34, 60})
    {
        const ScrollViewLayout l = scenario_layout(num_lines);
        ASSERT_TRUE(l.scrollable);
        for (const ScrollRect* r : {&l.up, &l.down, &l.track})
        {
            EXPECT_GE(r->x, l.frame_x1);
            EXPECT_LE(r->x + r->w, l.frame_x2);
            EXPECT_GE(r->y, l.frame_y1);
            EXPECT_LE(r->y + r->h, l.frame_y2);
        }
        EXPECT_TRUE(rects_disjoint(l.up, l.track));
        EXPECT_TRUE(rects_disjoint(l.down, l.track));
        EXPECT_TRUE(rects_disjoint(l.up, l.down));
    }
}

TEST(ScrollViewLayout, thumb_spans_the_track_endpoints)
{
    for (int num_lines : {15, 21, 34, 60})
    {
        const int bottom = bottomrow_for(num_lines);
        const ScrollViewLayout top = scenario_layout(num_lines, 0);
        EXPECT_EQ(top.track.y, top.thumb.y) << num_lines;
        const ScrollViewLayout end = scenario_layout(num_lines, bottom);
        EXPECT_EQ(end.track.y + end.track.h, end.thumb.y + end.thumb.h)
            << num_lines;
    }
}

TEST(ScrollViewLayout, thumb_never_leaves_the_track)
{
    for (int num_lines = 15; num_lines <= 60; ++num_lines)
    {
        const int bottom = bottomrow_for(num_lines);
        for (int linesdown = 0; linesdown <= bottom; ++linesdown)
        {
            const ScrollViewLayout l = scenario_layout(num_lines, linesdown);
            ASSERT_GE(l.thumb.y, l.track.y);
            ASSERT_LE(l.thumb.y + l.thumb.h, l.track.y + l.track.h);
            ASSERT_GE(l.thumb.x, l.track.x);
            ASSERT_LE(l.thumb.x + l.thumb.w, l.track.x + l.track.w);
        }
    }
}

TEST(ScrollViewLayout, thumb_height_is_proportional_and_floors_at_six)
{
    // thumb_h = track.h * 14 / num_lines, floored at the 6px grab minimum.
    // 200 lines computes 64*14/200 = 4 and the floor lifts it to exactly 6 --
    // not to the track height, and not to some other minimum.
    const ScrollViewLayout l = scenario_layout(200);
    EXPECT_TRUE(l.scrollable);
    EXPECT_EQ(6, l.thumb.h) << "a 200-line briefing gets exactly the 6px floor";
    EXPECT_EQ(64, l.track.h) << "the track the thumb is measured against";

    // 28 lines sits above the floor, so the proportional rule itself is pinned
    // on the same case: 64*14/28 = 32.
    EXPECT_EQ(32, scenario_layout(28).thumb.h)
        << "track.h * 14 / num_lines when it clears the 6px floor";
}

TEST(ScrollViewLayout, out_of_range_linesdown_is_clamped)
{
    const int bottom = bottomrow_for(21);
    const ScrollViewLayout below = scenario_layout(21, -50);
    EXPECT_EQ(below.track.y, below.thumb.y);
    const ScrollViewLayout above =
        compute_scroll_view_layout(21, 200, bottom + 999, bottom, 0, 0, 320,
                                   200);
    EXPECT_EQ(above.track.y + above.track.h, above.thumb.y + above.thumb.h);
}

TEST(ScrollViewLayout, widened_blit_is_clamped_to_the_screen)
{
    // A caller blit rect near the right edge cannot push past x=320.
    const ScrollViewLayout l =
        compute_scroll_view_layout(21, 240, 0, bottomrow_for(21), 100, 0,
                                   244, 119);
    EXPECT_TRUE(l.scrollable);
    EXPECT_EQ(320, l.blit_x + l.blit_w);
}

TEST(ScrollViewLayout, inconsistent_inputs_pin_the_clamped_thumb_exactly)
{
    // Defensive arms: a num_lines that disagrees with bottomrow, and a zero
    // num_lines. Both land on a KNOWN geometry, so they are pinned by value —
    // "inside the track" is equally true of a 1px thumb parked at the top.

    // 5 lines against a 16px bottomrow computes 64 * 14 / 5 = 179, and the
    // `thumb_h > track.h` cap pulls it back to exactly the 64px track.
    const ScrollViewLayout tiny =
        compute_scroll_view_layout(5, 200, 0, 16, 0, 0, 320, 200);
    ASSERT_TRUE(tiny.scrollable) << "bottomrow > 0 is the whole scrollable rule";
    EXPECT_EQ(242, tiny.track.x) << "the gutter column is at 40 + 200 + 2";
    EXPECT_EQ(56, tiny.track.y);
    EXPECT_EQ(14, tiny.track.w);
    EXPECT_EQ(64, tiny.track.h);
    EXPECT_EQ(244, tiny.thumb.x) << "thumb.x = track.x + 2";
    EXPECT_EQ(10, tiny.thumb.w) << "thumb.w = track.w - 4";
    EXPECT_EQ(64, tiny.thumb.h)
        << "179 is capped at the track height, not left to overflow it";
    EXPECT_EQ(56, tiny.thumb.y) << "linesdown 0 sits the thumb on track.y";

    // num_lines == 0 takes the divide-by-zero guard, whose fallback is the
    // FULL track height (not the 6px grab floor); a full-height thumb has no
    // travel, so even a mid-scroll linesdown maps back onto track.y exactly.
    const ScrollViewLayout zero =
        compute_scroll_view_layout(0, 200, 8, 16, 0, 0, 320, 200);
    ASSERT_TRUE(zero.scrollable);
    EXPECT_EQ(64, zero.thumb.h)
        << "num_lines == 0 falls back to the whole track height";
    EXPECT_EQ(244, zero.thumb.x);
    EXPECT_EQ(10, zero.thumb.w);
    EXPECT_EQ(56, zero.thumb.y)
        << "(track.h - thumb.h) == 0, so linesdown 8 still lands on track.y";
}

TEST(ScrollViewLayout, contains_is_left_inclusive_right_exclusive)
{
    const ScrollRect r{10, 20, 14, 14};
    EXPECT_TRUE(r.contains(10, 20));
    EXPECT_TRUE(r.contains(23, 33));
    EXPECT_FALSE(r.contains(24, 20));
    EXPECT_FALSE(r.contains(10, 34));
    EXPECT_FALSE(r.contains(9, 20));
}

TEST(ScrollViewLayout, contains_padded_grows_every_side)
{
    const ScrollRect r{10, 20, 14, 14};
    EXPECT_TRUE(r.contains_padded(7, 20, 3));
    EXPECT_TRUE(r.contains_padded(26, 36, 3));
    EXPECT_FALSE(r.contains_padded(6, 20, 3));
    EXPECT_FALSE(r.contains_padded(27, 20, 3));
    EXPECT_FALSE(r.contains_padded(10, 37, 3));
    // pad 0 degenerates to contains().
    EXPECT_TRUE(r.contains_padded(10, 20, 0));
    EXPECT_FALSE(r.contains_padded(24, 20, 0));
}
