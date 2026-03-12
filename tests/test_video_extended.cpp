#include <openglad/interface/screen.h>
#include "test_framework.h"

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// draw_box tests
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_draw_box_hollow)
{
    og::runtime::current_session->myscreen_->draw_box(10, 10, 50, 30, 100, 0, 1);
    // Should not crash - visual smoke test
}


TEST(VideoExtended, video_draw_box_filled)
{
    og::runtime::current_session->myscreen_->draw_box(60, 10, 100, 30, 200, 1, 1);
}


// ---------------------------------------------------------------------------
// draw_button tests
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_draw_button_basic)
{
    og::runtime::current_session->myscreen_->draw_button(10, 50, 80, 70, 1, 1);
}


TEST(VideoExtended, video_draw_button_depth2)
{
    og::runtime::current_session->myscreen_->draw_button(90, 50, 160, 70, 2, 1);
}


TEST(VideoExtended, video_draw_button_inverted)
{
    og::runtime::current_session->myscreen_->draw_button_inverted(10, 80, 80, 100);
}


TEST(VideoExtended, video_draw_button_colored)
{
    og::runtime::current_session->myscreen_->draw_button_colored(90, 80, 160, 100, 1, 50, 60, 40);
}


// ---------------------------------------------------------------------------
// draw_text_bar tests
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_draw_text_bar)
{
    og::runtime::current_session->myscreen_->draw_text_bar(10, 110, 200, 130);
}


// ---------------------------------------------------------------------------
// draw_dialog tests
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_draw_dialog)
{
    int left = og::runtime::current_session->myscreen_->draw_dialog(20, 20, 280, 180, "Test Dialog");
    ASSERT_TRUE(left > 0) << "draw_dialog should return positive left margin";
}


// ---------------------------------------------------------------------------
// draw_line tests (Bresenham)
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_draw_line_horizontal)
{
    og::runtime::current_session->myscreen_->draw_line(10, 150, 100, 150, 77);
}


TEST(VideoExtended, video_draw_line_vertical)
{
    og::runtime::current_session->myscreen_->draw_line(150, 10, 150, 100, 88);
}


TEST(VideoExtended, video_draw_line_diagonal)
{
    og::runtime::current_session->myscreen_->draw_line(10, 10, 100, 80, 99);
}


TEST(VideoExtended, video_draw_line_reverse)
{
    og::runtime::current_session->myscreen_->draw_line(100, 80, 10, 10, 55);
}


// ---------------------------------------------------------------------------
// clearbuffer test
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_clearbuffer)
{
    og::runtime::current_session->myscreen_->clearbuffer();
}


// ---------------------------------------------------------------------------
// fastbox test
// ---------------------------------------------------------------------------

TEST(VideoExtended, video_fastbox)
{
    og::runtime::current_session->myscreen_->fastbox(50, 50, 20, 20, 100);
}


// (putblack, darken_screen, buffer_to_screen, fadeblack, do_cycle removed -
// they interact with SDL surfaces/palette and can crash in offscreen mode)
