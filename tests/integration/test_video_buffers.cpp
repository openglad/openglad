#include <openglad/interface/screen.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/platform/sai2x.h>
#include <gtest/gtest.h>

#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
screen* buffers_screen()
{
    return og::runtime::current_session->myscreen_;
}

std::array<Uint8, 3> rgb_at(int x, int y)
{
    std::array<Uint8, 3> value{};
    buffers_screen()->get_pixel(x, y, &value[0], &value[1], &value[2]);
    return value;
}

// A cleared canvas cell, read back through the same reverse-palette lookup
// every other assertion here uses. Pinning "untouched" against this value
// rather than a hardcoded 0 keeps the oracles honest if the clear colour
// ever moves.
int cleared_index()
{
    int index = -1;
    return buffers_screen()->get_pixel(300, 190, &index);
}
} // namespace

// putdata is the transparent sprite blit every glyph and icon rides on: it
// copies each NON-ZERO source byte verbatim to (startx + col, starty + row)
// and SKIPS index 0, leaving whatever was already on the canvas. A version
// that painted index 0 too would black-box every sprite; one that dropped
// the copy would paint nothing at all.
TEST(VideoBuffers, putdata_copies_non_zero_bytes_and_leaves_index_zero_alone)
{
    screen* const s = buffers_screen();
    s->clearbuffer();

    // 4x4 image of distinct palette indices with one transparent byte in the
    // middle (row 1, column 1).
    std::array<unsigned char, 16> img{};
    for (int i = 0; i < 16; i++)
        img[static_cast<std::size_t>(i)] = static_cast<unsigned char>(10 + i);
    img[5] = 0;

    // A known ground under the sprite so the skipped byte is visible.
    constexpr unsigned char kGround = 7;
    s->fastbox(0, 0, 8, 8, kGround);
    int index = -1;
    ASSERT_EQ(static_cast<int>(kGround), s->get_pixel(0, 0, &index))
        << "fastbox/get_pixel must round-trip a palette index for the oracles"
           " below to mean anything";

    s->putdata(0, 0, 4, 4, img);

    index = -1;
    EXPECT_EQ(10, s->get_pixel(0, 0, &index)) << "first source byte";
    EXPECT_EQ(10, index) << "get_pixel reports the index through the out-param";
    EXPECT_EQ(13, s->get_pixel(3, 0, &index)) << "last byte of row 0";
    EXPECT_EQ(14, s->get_pixel(0, 1, &index)) << "first byte of row 1";
    EXPECT_EQ(25, s->get_pixel(3, 3, &index)) << "last source byte";
    EXPECT_EQ(static_cast<int>(kGround), s->get_pixel(1, 1, &index))
        << "source index 0 is transparent: the ground must show through";
    EXPECT_EQ(static_cast<int>(kGround), s->get_pixel(4, 0, &index))
        << "putdata must not write past xsize";
    EXPECT_EQ(static_cast<int>(kGround), s->get_pixel(0, 4, &index))
        << "putdata must not write past ysize";

    // putdata_alpha blends the same non-zero bytes at the given coverage and
    // still skips index 0. The reference cell runs the scalar primitive by
    // hand, exactly as VideoDraw.video_walkputbuffer_alpha_matches_point_blending.
    s->fastbox(10, 0, 12, 8, kGround);
    const std::array<Uint8, 3> ground_rgb = rgb_at(21, 0);
    s->putdata_alpha(10, 0, 4, 4, img, 128);
    s->pointb(20, 0, kGround);
    s->pointb(20, 0, 10, 128);
    EXPECT_EQ(rgb_at(20, 0), rgb_at(10, 0))
        << "putdata_alpha must blend like pointb(color, alpha)";
    EXPECT_NE(ground_rgb, rgb_at(10, 0))
        << "the blend must actually change the ground";
    EXPECT_EQ(ground_rgb, rgb_at(11, 1))
        << "source index 0 stays transparent under alpha too";
    EXPECT_EQ(ground_rgb, rgb_at(14, 0))
        << "putdata_alpha must not write past xsize";
}

// get_pixel(x, y, &index) does raw pointer arithmetic into E_Screen->render.
// Without the bounds guard, x == w reads the first pixel of the NEXT row and
// x == -1 reads the last pixel of the PREVIOUS one. Both must report black
// instead. The offset overload converts offset -> (offset % w, offset / w) on
// the active canvas raster and rejects anything outside the surface.
TEST(VideoBuffers, get_pixel_rejects_out_of_range_coordinates)
{
    screen* const s = buffers_screen();
    const int w = E_Screen->render->w;
    s->clearbuffer();

    // Ink exactly the two cells a wrapped / underflowed read would land on.
    constexpr unsigned char kTell = 47;
    s->pointb(0, 1, kTell);
    s->pointb(w - 1, 4, kTell);
    int index = -1;
    ASSERT_EQ(static_cast<int>(kTell), s->get_pixel(0, 1, &index))
        << "wrap-target cell must be readable in bounds";
    ASSERT_EQ(static_cast<int>(kTell), s->get_pixel(w - 1, 4, &index))
        << "underflow-target cell must be readable in bounds";

    EXPECT_EQ(0, s->get_pixel(w, 0, &index))
        << "x == w must report black, not wrap into (0, 1)";
    EXPECT_EQ(0, s->get_pixel(-1, 5, &index))
        << "x == -1 must report black, not read (w - 1, 4)";
    EXPECT_EQ(0, s->get_pixel(1000000, 1000000, &index))
        << "far out-of-range coordinates must report black";
    EXPECT_EQ(0, s->get_pixel(-50, -50, &index))
        << "negative coordinates must report black";

    // The offset overload's raster mapping, pinned positively and negatively.
    const int canvas_w = s->canvas_w();
    s->pointb(3, 2, kTell);
    EXPECT_EQ(static_cast<int>(kTell), s->get_pixel(2 * canvas_w + 3))
        << "offset maps to (offset % canvas_w, offset / canvas_w)";
    EXPECT_EQ(0, s->get_pixel(-1)) << "negative offset must be rejected";
    EXPECT_EQ(0, s->get_pixel(1000000000)) << "huge offset must be rejected";
}

// draw_rect_filled fills [x, x+w) x [y, y+h) through hor_line_alpha, and the
// line primitives ink exactly `length` cells from (x, y). An off-by-one in
// any of them leaves a seam in every panel border and HUD bar.
TEST(VideoBuffers, filled_rect_and_lines_ink_exactly_their_span)
{
    screen* const s = buffers_screen();
    s->clearbuffer();
    const int blank = cleared_index();
    int index = -1;

    s->draw_rect_filled(0, 0, 20, 20, 100, 255);
    EXPECT_EQ(100, s->get_pixel(0, 0, &index)) << "first cell of the rect";
    EXPECT_EQ(100, s->get_pixel(19, 19, &index)) << "last cell of the rect";
    EXPECT_EQ(blank, s->get_pixel(20, 0, &index))
        << "w == 20 means the last column is 19";
    EXPECT_EQ(blank, s->get_pixel(0, 20, &index))
        << "h == 20 means the last row is 19";

    s->ver_line(10, 40, 20, 88);
    EXPECT_EQ(88, s->get_pixel(10, 40, &index)) << "first cell of the column";
    EXPECT_EQ(88, s->get_pixel(10, 59, &index)) << "last cell of the column";
    EXPECT_EQ(blank, s->get_pixel(10, 60, &index))
        << "ver_line inks exactly `length` cells";
    EXPECT_EQ(blank, s->get_pixel(11, 40, &index))
        << "ver_line inks one column only";

    // hor_line_alpha blends, so its oracle is the scalar reference blend.
    constexpr unsigned char kGround = 7;
    s->fastbox(0, 28, 60, 6, kGround);
    const std::array<Uint8, 3> ground_rgb = rgb_at(58, 30);
    s->hor_line_alpha(0, 30, 50, 77, 128);
    s->pointb(55, 30, kGround);
    s->pointb(55, 30, 77, 128);
    EXPECT_EQ(rgb_at(55, 30), rgb_at(0, 30))
        << "hor_line_alpha blends like pointb(color, alpha)";
    EXPECT_EQ(rgb_at(55, 30), rgb_at(49, 30)) << "last inked cell";
    EXPECT_EQ(ground_rgb, rgb_at(50, 30))
        << "hor_line_alpha inks exactly `length` cells";
    EXPECT_EQ(ground_rgb, rgb_at(0, 31))
        << "hor_line_alpha inks one row only";
}
