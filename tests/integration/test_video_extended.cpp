#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
screen* vx_screen()
{
    return og::runtime::current_session->myscreen_;
}

// Palette index of one canvas pixel.  get_pixel() answers with the first
// palette entry whose rgb matches; every index asserted on below (0, 10..15,
// 40, 50, 60, 77, 88, 99, 100, 200) is its own first match in
// src/resources/our_palette.cpp, so index equality is exact.
int vx_index(int x, int y)
{
    int index = -1;
    return vx_screen()->get_pixel(x, y, &index);
}

// Rows in [y0,y1] on column x that hold `color`.
std::vector<int> vx_column_hits(int x, int y0, int y1, int color)
{
    std::vector<int> rows;
    for (int y = y0; y <= y1; ++y)
        if (vx_index(x, y) == color)
            rows.push_back(y);
    return rows;
}
} // namespace

// ---------------------------------------------------------------------------
// draw_box
// ---------------------------------------------------------------------------

// filled=0 must ink the four edges and NOTHING else; filled=1 is the paired
// positive control that the same rect does fill its interior.  A hollow box
// that quietly filled solid (or a filled box that drew only its frame) is the
// break this pins (video_sdl.cpp draw_box).
TEST(VideoExtended, draw_box_hollow_inks_only_its_edges_and_filled_inks_the_interior)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_box(10, 10, 50, 30, 100, 0, 1); // hollow

    EXPECT_EQ(100, vx_index(10, 10)) << "hollow box: top-left corner is inked";
    EXPECT_EQ(100, vx_index(50, 10)) << "hollow box: top-right corner is inked";
    EXPECT_EQ(100, vx_index(10, 30)) << "hollow box: bottom-left corner is inked";
    EXPECT_EQ(100, vx_index(50, 30)) << "hollow box: bottom-right corner is inked";
    EXPECT_EQ(100, vx_index(30, 10)) << "hollow box: top edge is inked";
    EXPECT_EQ(100, vx_index(30, 30)) << "hollow box: bottom edge is inked";
    EXPECT_EQ(100, vx_index(10, 20)) << "hollow box: left edge is inked";
    EXPECT_EQ(100, vx_index(50, 20)) << "hollow box: right edge is inked";

    EXPECT_EQ(0, vx_index(30, 20)) << "hollow box: the interior stays cleared";
    EXPECT_EQ(0, vx_index(11, 11)) << "hollow box: just inside the top-left stays cleared";
    EXPECT_EQ(0, vx_index(49, 29)) << "hollow box: just inside the bottom-right stays cleared";
    EXPECT_EQ(0, vx_index(9, 20)) << "hollow box: nothing left of x1";
    EXPECT_EQ(0, vx_index(51, 20)) << "hollow box: nothing right of x2";
    EXPECT_EQ(0, vx_index(30, 9)) << "hollow box: nothing above y1";
    EXPECT_EQ(0, vx_index(30, 31)) << "hollow box: nothing below y2";

    s->draw_box(60, 10, 100, 30, 200, 1, 1); // filled

    EXPECT_EQ(200, vx_index(80, 20)) << "filled box: the interior IS inked";
    EXPECT_EQ(200, vx_index(60, 10)) << "filled box: reaches its top-left corner";
    EXPECT_EQ(200, vx_index(100, 30)) << "filled box: reaches its bottom-right corner";
    EXPECT_EQ(0, vx_index(59, 20)) << "filled box: nothing left of x1";
    EXPECT_EQ(0, vx_index(101, 20)) << "filled box: nothing right of x2";
    EXPECT_EQ(0, vx_index(80, 31)) << "filled box: nothing below y2";
}

// ---------------------------------------------------------------------------
// draw_button
// ---------------------------------------------------------------------------

// draw_button recurses once per border level: depth 2 is two nested bevel
// rings and only then the face.  Ring colours are top 15, bottom 11, left 14,
// right 12 (left/right are drawn last, so they own the corners); face 13.
TEST(VideoExtended, draw_button_depth2_draws_two_nested_bevel_rings_then_the_face)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_button(90, 50, 160, 70, 2, 1);

    // Outer ring.
    EXPECT_EQ(15, vx_index(120, 50)) << "outer ring: top row is 15";
    EXPECT_EQ(11, vx_index(120, 70)) << "outer ring: bottom row is 11";
    EXPECT_EQ(14, vx_index(90, 60)) << "outer ring: left column is 14";
    EXPECT_EQ(12, vx_index(160, 60)) << "outer ring: right column is 12";
    EXPECT_EQ(14, vx_index(90, 50)) << "outer ring: the left column owns the top-left corner";
    EXPECT_EQ(12, vx_index(160, 50)) << "outer ring: the right column owns the top-right corner";

    // Second ring, inset by one: this is what the border-1 recursion draws.
    EXPECT_EQ(15, vx_index(120, 51)) << "inner ring: top row is 15";
    EXPECT_EQ(11, vx_index(120, 69)) << "inner ring: bottom row is 11";
    EXPECT_EQ(14, vx_index(91, 60)) << "inner ring: left column is 14";
    EXPECT_EQ(12, vx_index(159, 60)) << "inner ring: right column is 12";
    EXPECT_EQ(14, vx_index(91, 51)) << "inner ring: its own top-left corner";

    // Face, inset by two.
    EXPECT_EQ(13, vx_index(92, 52)) << "face: starts two pixels in on a depth-2 button";
    EXPECT_EQ(13, vx_index(120, 60)) << "face: the middle of the button is 13";
    EXPECT_EQ(13, vx_index(158, 68)) << "face: reaches the inner bottom-right";

    EXPECT_EQ(0, vx_index(89, 60)) << "nothing left of x1";
    EXPECT_EQ(0, vx_index(161, 60)) << "nothing right of x2";
    EXPECT_EQ(0, vx_index(120, 71)) << "nothing below y2";
}

// draw_button_inverted(x, y, WIDTH, HEIGHT) — the width/height overload, not
// the corner one (see src/interface/ui/picker_sdl_defs.h).  It forwards to
// draw_text_bar over (x, y)-(x+w-1, y+h-1): face 12, top 10, bottom 15,
// left 11, right 14.
TEST(VideoExtended, draw_button_inverted_takes_width_and_height_and_sinks_the_bevel)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_button_inverted(10, 80, 80, 100); // -> (10,80)-(89,179)

    EXPECT_EQ(12, vx_index(50, 120)) << "inverted button: the face is 12";
    EXPECT_EQ(10, vx_index(50, 80)) << "inverted button: the top row is the dark 10";
    EXPECT_EQ(15, vx_index(50, 179)) << "inverted button: the bottom row is the light 15";
    EXPECT_EQ(11, vx_index(10, 120)) << "inverted button: the left column is 11";
    EXPECT_EQ(14, vx_index(89, 120)) << "inverted button: the right column is 14";

    EXPECT_EQ(0, vx_index(90, 120)) << "width 80 from x=10 ends at x=89, not x=90";
    EXPECT_EQ(0, vx_index(50, 180)) << "height 100 from y=80 ends at y=179, not y=180";
    EXPECT_EQ(0, vx_index(9, 120)) << "nothing left of x";
    EXPECT_EQ(0, vx_index(50, 79)) << "nothing above y";
}

// draw_button_colored(use_border=true): the caller's three colours are the
// whole contract — base fills the interior, high paints the top row and left
// column, shadow the bottom row and right column.
TEST(VideoExtended, draw_button_colored_paints_base_high_and_shadow_where_the_caller_asked)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_button_colored(90, 80, 160, 100, 1, 50, 60, 40);

    EXPECT_EQ(60, vx_index(120, 80)) << "high colour paints the top row";
    EXPECT_EQ(60, vx_index(90, 90)) << "high colour paints the left column";
    EXPECT_EQ(40, vx_index(120, 100)) << "shadow colour paints the bottom row";
    EXPECT_EQ(40, vx_index(160, 90)) << "shadow colour paints the right column";
    EXPECT_EQ(50, vx_index(120, 90)) << "base colour fills the interior";
    EXPECT_EQ(50, vx_index(91, 81)) << "the base fill starts one pixel inside the border";
    EXPECT_EQ(50, vx_index(159, 99)) << "the base fill ends one pixel inside the border";
    EXPECT_EQ(60, vx_index(90, 80)) << "the left column owns the top-left corner";
    EXPECT_EQ(40, vx_index(160, 80)) << "the right column owns the top-right corner";

    EXPECT_EQ(0, vx_index(89, 90)) << "nothing left of x1";
    EXPECT_EQ(0, vx_index(161, 90)) << "nothing right of x2";
    EXPECT_EQ(0, vx_index(120, 101)) << "nothing below y2";
}

// ---------------------------------------------------------------------------
// draw_text_bar — the widget behind every dialog
// ---------------------------------------------------------------------------

TEST(VideoExtended, draw_text_bar_fills_grey_12_and_sinks_its_border)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_text_bar(10, 110, 200, 130);

    EXPECT_EQ(12, vx_index(100, 120)) << "text bar: the face is filled with 12";
    EXPECT_EQ(12, vx_index(11, 111)) << "text bar: the fill reaches inside the top-left";
    EXPECT_EQ(12, vx_index(199, 129)) << "text bar: the fill reaches inside the bottom-right";
    EXPECT_EQ(10, vx_index(100, 110)) << "text bar: the top line is the dark 10";
    EXPECT_EQ(15, vx_index(100, 130)) << "text bar: the bottom line is the light 15";
    EXPECT_EQ(11, vx_index(10, 120)) << "text bar: the left line is 11";
    EXPECT_EQ(14, vx_index(200, 120)) << "text bar: the right line is 14";

    EXPECT_EQ(0, vx_index(9, 120)) << "nothing left of x1";
    EXPECT_EQ(0, vx_index(201, 120)) << "nothing right of x2";
    EXPECT_EQ(0, vx_index(100, 131)) << "nothing below y2";
}

// ---------------------------------------------------------------------------
// draw_dialog
// ---------------------------------------------------------------------------

// draw_dialog returns x1+6 (where the caller starts its text), frames the box
// with a raised button, lays two sunken text bars inside it, and centres the
// header in the upper one.
TEST(VideoExtended, draw_dialog_returns_x1_plus_6_and_centres_the_header_in_its_bar)
{
    screen* const s = vx_screen();

    trace_clear();
    s->clearbuffer();
    const int left = s->draw_dialog(20, 20, 280, 180, "Test Dialog");

    ASSERT_EQ(26, left) << "draw_dialog returns x1+6 as the text origin";
    ASSERT_TRUE(trace_contains("dialog", "header='Test Dialog' left=105 y=26"))
        << "draw_dialog centres the header it was handed at centerx - width/2, y1+6";

    EXPECT_EQ(14, vx_index(20, 20)) << "the frame is a raised button: left column 14 at the corner";
    EXPECT_EQ(12, vx_index(160, 120)) << "the body text bar's face fills the dialog interior";
    EXPECT_EQ(10, vx_index(160, 40)) << "the body text bar's top line sits at y1+20";
    EXPECT_EQ(12, vx_index(160, 30)) << "the header text bar's face sits above it";
    EXPECT_EQ(10, vx_index(160, 24)) << "the header text bar starts at y1+4";

    // The title is real ink: RED glyph pixels, and the leftmost of them is the
    // centred origin the function computed and returned.
    int red_min_x = 1 << 20;
    int red_min_y = 1 << 20;
    int red_count = 0;
    for (int y = 20; y <= 40; ++y)
        for (int x = 20; x <= 280; ++x)
            if (vx_index(x, y) == RED)
            {
                ++red_count;
                if (x < red_min_x) red_min_x = x;
                if (y < red_min_y) red_min_y = y;
            }
    const int titled_red_count = red_count;
    const int titled_red_min_x = red_min_x;
    const int titled_red_min_y = red_min_y;

    // Negative control: the same dialog with an empty header draws no title.
    s->clearbuffer();
    const int empty_left = s->draw_dialog(20, 20, 280, 180, "");
    int empty_red = 0;
    for (int y = 20; y <= 40; ++y)
        for (int x = 20; x <= 280; ++x)
            if (vx_index(x, y) == RED)
                ++empty_red;

    EXPECT_EQ(26, empty_left) << "the return value does not depend on the header";
    EXPECT_EQ(0, empty_red) << "an empty header must draw no title ink at all";
    // 165 RED cells is the glyph ink of "Test Dialog" in text_big; a title that
    // stopped after one character, or was drawn in the wrong colour, moves it.
    EXPECT_EQ(165, titled_red_count) << "the whole header is drawn, in RED";
    EXPECT_EQ(106, titled_red_min_x)
        << "the title's ink begins at the centred origin 105 (+1 glyph bearing), "
           "not at the x1+6 the function returns";
    EXPECT_EQ(26, titled_red_min_y) << "the title is drawn at y1+6";
}

// ---------------------------------------------------------------------------
// draw_line (Bresenham)
// ---------------------------------------------------------------------------

TEST(VideoExtended, draw_line_horizontal_inks_every_cell_between_the_endpoints)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_line(10, 150, 100, 150, 77);

    for (int x = 10; x <= 100; ++x)
        ASSERT_EQ(77, vx_index(x, 150)) << "a flat line inks every column, including x=" << x;
    EXPECT_EQ(0, vx_index(9, 150)) << "nothing before x1";
    EXPECT_EQ(0, vx_index(101, 150)) << "nothing after x2";
    EXPECT_EQ(0, vx_index(55, 151)) << "a flat line stays on its own row";
    EXPECT_EQ(0, vx_index(55, 149)) << "a flat line stays on its own row";
}

TEST(VideoExtended, draw_line_vertical_inks_every_cell_between_the_endpoints)
{
    screen* const s = vx_screen();
    s->clearbuffer();

    s->draw_line(150, 10, 150, 100, 88);

    for (int y = 10; y <= 100; ++y)
        ASSERT_EQ(88, vx_index(150, y)) << "a vertical line inks every row, including y=" << y;
    EXPECT_EQ(0, vx_index(150, 9)) << "nothing above y1";
    EXPECT_EQ(0, vx_index(150, 101)) << "nothing below y2";
    EXPECT_EQ(0, vx_index(151, 55)) << "a vertical line stays on its own column";
    EXPECT_EQ(0, vx_index(149, 55)) << "a vertical line stays on its own column";
}

// A shallow line plots exactly one pixel per column, stepping down by the
// Bresenham ratio, and the same segment drawn from the other end covers the
// same columns with the same endpoints.
TEST(VideoExtended, draw_line_shallow_diagonal_plots_one_pixel_per_column_either_way)
{
    screen* const s = vx_screen();

    s->clearbuffer();
    s->draw_line(10, 10, 100, 80, 99);

    std::vector<int> forward_rows;
    for (int x = 10; x <= 100; ++x)
    {
        const std::vector<int> hits = vx_column_hits(x, 0, s->canvas_h() - 1, 99);
        ASSERT_EQ(1u, hits.size()) << "exactly one pixel is inked in column x=" << x;
        forward_rows.push_back(hits[0]);
    }
    EXPECT_EQ(10, forward_rows.front()) << "the run starts at the first endpoint";
    EXPECT_EQ(80, forward_rows.back()) << "the run ends at the second endpoint";
    for (int i = 0; i < static_cast<int>(forward_rows.size()); ++i)
        ASSERT_EQ(10 + (i * 71) / 91, forward_rows[static_cast<std::size_t>(i)])
            << "column " << (10 + i) << " sits on the Bresenham row";
    EXPECT_EQ(0, vx_index(9, 10)) << "nothing before the first endpoint";
    EXPECT_EQ(0, vx_index(101, 80)) << "nothing after the last endpoint";

    s->clearbuffer();
    s->draw_line(100, 80, 10, 10, 99); // same segment, opposite order

    std::vector<int> reverse_rows;
    for (int x = 10; x <= 100; ++x)
    {
        const std::vector<int> hits = vx_column_hits(x, 0, s->canvas_h() - 1, 99);
        ASSERT_EQ(1u, hits.size()) << "reversed: exactly one pixel in column x=" << x;
        reverse_rows.push_back(hits[0]);
    }
    EXPECT_EQ(10, reverse_rows.front()) << "reversed: the run still reaches (10,10)";
    EXPECT_EQ(80, reverse_rows.back()) << "reversed: the run still reaches (100,80)";
    for (int i = 0; i < static_cast<int>(reverse_rows.size()); ++i)
        ASSERT_EQ(80 - ((90 - i) * 71) / 91, reverse_rows[static_cast<std::size_t>(i)])
            << "reversed: column " << (10 + i) << " sits on the mirrored Bresenham row";
}

// ---------------------------------------------------------------------------
// clearbuffer — the precondition every other drawing test leans on
// ---------------------------------------------------------------------------

TEST(VideoExtended, clearbuffer_zeroes_the_whole_canvas)
{
    screen* const s = vx_screen();
    const int w = s->canvas_w();
    const int h = s->canvas_h();
    ASSERT_GE(w, 320) << "the canvas is at least the classic 320x200";
    ASSERT_GE(h, 200) << "the canvas is at least the classic 320x200";

    s->fastbox(0, 0, w, h, 50, 1);
    ASSERT_EQ(50, vx_index(w / 2, h / 2)) << "the canvas really was dirtied first";
    ASSERT_EQ(50, vx_index(0, 0)) << "the canvas really was dirtied first";

    s->clearbuffer();

    EXPECT_EQ(0, vx_index(0, 0)) << "clearbuffer zeroes the first pixel";
    EXPECT_EQ(0, vx_index(w / 2, h / 2)) << "clearbuffer zeroes the middle";
    EXPECT_EQ(0, vx_index(w - 1, h - 1)) << "clearbuffer zeroes the last pixel";
    int dirty = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            Uint8 r = 0, g = 0, b = 0;
            s->get_pixel(x, y, &r, &g, &b);
            if (r != 0 || g != 0 || b != 0)
                ++dirty;
        }
    EXPECT_EQ(0, dirty) << "clearbuffer leaves no pixel behind";
}
