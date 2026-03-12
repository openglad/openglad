#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// text::query_width
// ---------------------------------------------------------------------------

TEST(TextRender, text_query_width_empty)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("");
    ASSERT_EQ(0, (int)w) << "empty string width is 0";
}


TEST(TextRender, text_query_width_single)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("A");
    ASSERT_TRUE(w > 0) << "single char has width > 0";
}


TEST(TextRender, text_query_width_long)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("Hello World");
    ASSERT_TRUE(w > 0) << "long string has positive width";

    Sint32 w2 = og::runtime::current_session->myscreen_->text_normal.query_width("Hi");
    ASSERT_TRUE(w > w2) << "longer string is wider";
}


TEST(TextRender, text_query_width_big)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_big.query_width("Test");
    ASSERT_TRUE(w > 0) << "big font width > 0";
}


// ---------------------------------------------------------------------------
// text::write_xy variations (all to buffer)
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 10, "Buffer text", (short)1);
}


TEST(TextRender, text_write_xy_color_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 20, "Colored buffer", (unsigned char)WHITE, (short)1);
}


TEST(TextRender, text_write_xy_no_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 30, "No color text");
}


// ---------------------------------------------------------------------------
// text::write_y variations
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_y_basic)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(50, "Y text");
}


TEST(TextRender, text_write_y_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(60, "Y colored", (unsigned char)RED);
}


TEST(TextRender, text_write_y_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(70, "Y buffer", (short)1);
}


TEST(TextRender, text_write_y_color_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(80, "Y color buf", (unsigned char)DARK_GREEN, (short)1);
}


// ---------------------------------------------------------------------------
// text::write_xy_center variations
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_center_alpha)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center_alpha(160, 100, WHITE, 128, "Alpha center");
}


TEST(TextRender, text_write_xy_center_shadow)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center_shadow(160, 110, WHITE, "Center shadow");
}


// ---------------------------------------------------------------------------
// text::write_char_xy variations
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_char_xy_basic)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(10, 120, 'A');
}


TEST(TextRender, text_write_char_xy_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(20, 120, 'B', (unsigned char)RED);
}


TEST(TextRender, text_write_char_xy_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(30, 120, 'C', (short)1);
}


TEST(TextRender, text_write_char_xy_color_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(40, 120, 'D', (unsigned char)DARK_BLUE, (short)1);
}


// ---------------------------------------------------------------------------
// text::write_y with viewscreen
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_y_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_y(50, "VS Y text", vs);
}


TEST(TextRender, text_write_y_color_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_y(60, "VS Y color", (unsigned char)RED, vs);
}


// ---------------------------------------------------------------------------
// text::write_xy with viewscreen
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 50, "VS text", vs);
}


TEST(TextRender, text_write_xy_color_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 60, "VS color", (unsigned char)WHITE, vs);
}


// ---------------------------------------------------------------------------
// text::write_char_xy with viewscreen
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_char_xy_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(10, 70, 'X', vs);
}


TEST(TextRender, text_write_char_xy_color_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(20, 70, 'Y', (unsigned char)DARK_GREEN, vs);
}


// ---------------------------------------------------------------------------
// text::write_xy_shadow
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_shadow_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_shadow(50, 50, RED, "Red shadow");
}


// ---------------------------------------------------------------------------
// big text
// ---------------------------------------------------------------------------

TEST(TextRender, text_big_write_xy_color)
{
    og::runtime::current_session->myscreen_->text_big.write_xy(10, 150, "Big colored", (unsigned char)WHITE);
}


TEST(TextRender, text_big_write_y)
{
    og::runtime::current_session->myscreen_->text_big.write_y(160, "Big centered");
}

