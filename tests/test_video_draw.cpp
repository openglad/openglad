#include "test_framework.h"

#include <openglad/runtime/screen.h>

#include <cstring>
#include <span>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// walkputbuffer - the big pixel-copying function in video.cpp
// ---------------------------------------------------------------------------

void test_video_walkputbuffer_basic()
{
    // Create a small test bitmap
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40);
}
REGISTER_TEST(test_video_walkputbuffer_basic);

void test_video_walkputbuffer_outline()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40,
                           OUTLINE_MODE, 0, OUTLINE_NAMED, 0);
}
REGISTER_TEST(test_video_walkputbuffer_outline);

void test_video_walkputbuffer_phantom()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40,
                           PHANTOM_MODE, 0, 0, SHIFT_RANDOM);
}
REGISTER_TEST(test_video_walkputbuffer_phantom);

void test_video_walkputbuffer_invisible()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40,
                           INVISIBLE_MODE, 128, 1, 0);
}
REGISTER_TEST(test_video_walkputbuffer_invisible);

void test_video_walkputbuffer_flash()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(50, 50, 16, 16,
                                  0, 0, 319, 199,
                                  bmp_span, 40);
}
REGISTER_TEST(test_video_walkputbuffer_flash);

// ---------------------------------------------------------------------------
// putdata - another big pixel function
// ---------------------------------------------------------------------------

void test_video_putdata_basic()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 50, sizeof(testbmp));

    og::runtime::current_session->myscreen_->putdata(50, 50, 16, 16, testbmp);
}
REGISTER_TEST(test_video_putdata_basic);

void test_video_putdata_at_origin()
{
    unsigned char testbmp[8*8];
    memset(testbmp, 75, sizeof(testbmp));

    og::runtime::current_session->myscreen_->putdata(0, 0, 8, 8, testbmp);
}
REGISTER_TEST(test_video_putdata_at_origin);

// ---------------------------------------------------------------------------
// draw_box variations
// ---------------------------------------------------------------------------

void test_video_draw_box_zero_size()
{
    og::runtime::current_session->myscreen_->draw_box(50, 50, 50, 50, 100, 0, 1);
}
REGISTER_TEST(test_video_draw_box_zero_size);

void test_video_draw_box_large()
{
    og::runtime::current_session->myscreen_->draw_box(0, 0, 319, 199, 50, 1, 1);
}
REGISTER_TEST(test_video_draw_box_large);

// ---------------------------------------------------------------------------
// draw_button variations with different depths
// ---------------------------------------------------------------------------

void test_video_draw_button_depth0()
{
    og::runtime::current_session->myscreen_->draw_button(10, 10, 50, 30, 0, 1);
}
REGISTER_TEST(test_video_draw_button_depth0);

void test_video_draw_button_depth3()
{
    og::runtime::current_session->myscreen_->draw_button(60, 10, 100, 30, 3, 1);
}
REGISTER_TEST(test_video_draw_button_depth3);

// ---------------------------------------------------------------------------
// draw_dialog
// ---------------------------------------------------------------------------

void test_video_draw_dialog_small()
{
    int result = og::runtime::current_session->myscreen_->draw_dialog(10, 10, 100, 60, "Small");
    TEST_ASSERT(result > 0, "draw_dialog should return positive margin");
}
REGISTER_TEST(test_video_draw_dialog_small);

void test_video_draw_dialog_large()
{
    int result = og::runtime::current_session->myscreen_->draw_dialog(5, 5, 310, 190, "Large Dialog Title");
    TEST_ASSERT(result > 0, "draw_dialog should return positive margin");
}
REGISTER_TEST(test_video_draw_dialog_large);

// ---------------------------------------------------------------------------
// draw_line variations (Bresenham algorithm)
// ---------------------------------------------------------------------------

void test_video_draw_line_steep_positive()
{
    og::runtime::current_session->myscreen_->draw_line(50, 10, 60, 100, 77);
}
REGISTER_TEST(test_video_draw_line_steep_positive);

void test_video_draw_line_steep_negative()
{
    og::runtime::current_session->myscreen_->draw_line(60, 100, 50, 10, 88);
}
REGISTER_TEST(test_video_draw_line_steep_negative);

void test_video_draw_line_single_point()
{
    og::runtime::current_session->myscreen_->draw_line(50, 50, 50, 50, 99);
}
REGISTER_TEST(test_video_draw_line_single_point);

// ---------------------------------------------------------------------------
// draw_rect_filled with various alpha values
// ---------------------------------------------------------------------------

void test_video_draw_rect_filled_zero_alpha()
{
    og::runtime::current_session->myscreen_->draw_rect_filled(50, 50, 30, 20, 150, 0);
}
REGISTER_TEST(test_video_draw_rect_filled_zero_alpha);

void test_video_draw_rect_filled_half_alpha()
{
    og::runtime::current_session->myscreen_->draw_rect_filled(80, 50, 30, 20, 200, 128);
}
REGISTER_TEST(test_video_draw_rect_filled_half_alpha);

void test_video_draw_rect_filled_opaque()
{
    og::runtime::current_session->myscreen_->draw_rect_filled(110, 50, 30, 20, 250, 255);
}
REGISTER_TEST(test_video_draw_rect_filled_opaque);

// ---------------------------------------------------------------------------
// fastbox
// ---------------------------------------------------------------------------

void test_video_fastbox_large()
{
    og::runtime::current_session->myscreen_->fastbox(0, 0, 100, 100, 50);
}
REGISTER_TEST(test_video_fastbox_large);

void test_video_fastbox_small()
{
    og::runtime::current_session->myscreen_->fastbox(100, 100, 1, 1, 200);
}
REGISTER_TEST(test_video_fastbox_small);

// ---------------------------------------------------------------------------
// text rendering through video's text objects
// ---------------------------------------------------------------------------

void test_video_text_write_xy()
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 10, "Test text", WHITE);
}
REGISTER_TEST(test_video_text_write_xy);

void test_video_text_write_xy_center()
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center(160, 100, WHITE, "Centered");
}
REGISTER_TEST(test_video_text_write_xy_center);

void test_video_text_write_xy_shadow()
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_shadow(10, 30, WHITE, "Shadow text");
}
REGISTER_TEST(test_video_text_write_xy_shadow);

void test_video_text_big()
{
    og::runtime::current_session->myscreen_->text_big.write_xy(10, 50, "Big text", WHITE);
}
REGISTER_TEST(test_video_text_big);
