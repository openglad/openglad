#include <gtest/gtest.h>

#include <openglad/interface/screen.h>

#include <cstring>
#include <span>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// walkputbuffer - the big pixel-copying function in video.cpp
// ---------------------------------------------------------------------------

TEST(VideoDraw, video_walkputbuffer_basic)
{
    // Create a small test bitmap
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40);
}


TEST(VideoDraw, video_walkputbuffer_outline)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40,
                           OUTLINE_MODE, 0, OUTLINE_NAMED, 0);
}


TEST(VideoDraw, video_walkputbuffer_phantom)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40,
                           PHANTOM_MODE, 0, 0, SHIFT_RANDOM);
}


TEST(VideoDraw, video_walkputbuffer_invisible)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40,
                           INVISIBLE_MODE, 128, 1, 0);
}


TEST(VideoDraw, video_walkputbuffer_flash)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(50, 50, 16, 16,
                                  0, 0, 319, 199,
                                  bmp_span, 40);
}


// ---------------------------------------------------------------------------
// putdata - another big pixel function
// ---------------------------------------------------------------------------

TEST(VideoDraw, video_putdata_basic)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 50, sizeof(testbmp));

    og::runtime::current_session->myscreen_->putdata(50, 50, 16, 16, testbmp);
}


TEST(VideoDraw, video_putdata_at_origin)
{
    unsigned char testbmp[8*8];
    memset(testbmp, 75, sizeof(testbmp));

    og::runtime::current_session->myscreen_->putdata(0, 0, 8, 8, testbmp);
}


// ---------------------------------------------------------------------------
// draw_box variations
// ---------------------------------------------------------------------------

TEST(VideoDraw, box_zero_size)
{
    og::runtime::current_session->myscreen_->draw_box(50, 50, 50, 50, 100, 0, 1);
}


TEST(VideoDraw, box_large)
{
    og::runtime::current_session->myscreen_->draw_box(0, 0, 319, 199, 50, 1, 1);
}


// ---------------------------------------------------------------------------
// draw_button variations with different depths
// ---------------------------------------------------------------------------

TEST(VideoDraw, button_depth0)
{
    og::runtime::current_session->myscreen_->draw_button(10, 10, 50, 30, 0, 1);
}


TEST(VideoDraw, button_depth3)
{
    og::runtime::current_session->myscreen_->draw_button(60, 10, 100, 30, 3, 1);
}


// ---------------------------------------------------------------------------
// draw_dialog
// ---------------------------------------------------------------------------

TEST(VideoDraw, dialog_small)
{
    int result = og::runtime::current_session->myscreen_->draw_dialog(10, 10, 100, 60, "Small");
    ASSERT_TRUE(result > 0) << "draw_dialog should return positive margin";
}


TEST(VideoDraw, dialog_large)
{
    int result = og::runtime::current_session->myscreen_->draw_dialog(5, 5, 310, 190, "Large Dialog Title");
    ASSERT_TRUE(result > 0) << "draw_dialog should return positive margin";
}


// ---------------------------------------------------------------------------
// draw_line variations (Bresenham algorithm)
// ---------------------------------------------------------------------------

TEST(VideoDraw, line_steep_positive)
{
    og::runtime::current_session->myscreen_->draw_line(50, 10, 60, 100, 77);
}


TEST(VideoDraw, line_steep_negative)
{
    og::runtime::current_session->myscreen_->draw_line(60, 100, 50, 10, 88);
}


TEST(VideoDraw, line_single_point)
{
    og::runtime::current_session->myscreen_->draw_line(50, 50, 50, 50, 99);
}


// ---------------------------------------------------------------------------
// draw_rect_filled with various alpha values
// ---------------------------------------------------------------------------

TEST(VideoDraw, rect_filled_zero_alpha)
{
    og::runtime::current_session->myscreen_->draw_rect_filled(50, 50, 30, 20, 150, 0);
}


TEST(VideoDraw, rect_filled_half_alpha)
{
    og::runtime::current_session->myscreen_->draw_rect_filled(80, 50, 30, 20, 200, 128);
}


TEST(VideoDraw, rect_filled_opaque)
{
    og::runtime::current_session->myscreen_->draw_rect_filled(110, 50, 30, 20, 250, 255);
}


// ---------------------------------------------------------------------------
// fastbox
// ---------------------------------------------------------------------------

TEST(VideoDraw, video_fastbox_large)
{
    og::runtime::current_session->myscreen_->fastbox(0, 0, 100, 100, 50);
}


TEST(VideoDraw, video_fastbox_small)
{
    og::runtime::current_session->myscreen_->fastbox(100, 100, 1, 1, 200);
}


// ---------------------------------------------------------------------------
// text rendering through video's text objects
// ---------------------------------------------------------------------------

TEST(VideoDraw, video_text_write_xy)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 10, "Test text", WHITE);
}


TEST(VideoDraw, video_text_write_xy_center)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center(160, 100, WHITE, "Centered");
}


TEST(VideoDraw, video_text_write_xy_shadow)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_shadow(10, 30, WHITE, "Shadow text");
}


TEST(VideoDraw, video_text_big)
{
    og::runtime::current_session->myscreen_->text_big.write_xy(10, 50, "Big text", WHITE);
}

