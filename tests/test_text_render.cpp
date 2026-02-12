#include "graph.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// text::query_width
// ---------------------------------------------------------------------------

void test_text_query_width_empty()
{
    Sint32 w = myscreen->text_normal.query_width("");
    TEST_ASSERT_EQ(0, (int)w, "empty string width is 0");
}
REGISTER_TEST(test_text_query_width_empty);

void test_text_query_width_single()
{
    Sint32 w = myscreen->text_normal.query_width("A");
    TEST_ASSERT(w > 0, "single char has width > 0");
}
REGISTER_TEST(test_text_query_width_single);

void test_text_query_width_long()
{
    Sint32 w = myscreen->text_normal.query_width("Hello World");
    TEST_ASSERT(w > 0, "long string has positive width");

    Sint32 w2 = myscreen->text_normal.query_width("Hi");
    TEST_ASSERT(w > w2, "longer string is wider");
}
REGISTER_TEST(test_text_query_width_long);

void test_text_query_width_big()
{
    Sint32 w = myscreen->text_big.query_width("Test");
    TEST_ASSERT(w > 0, "big font width > 0");
}
REGISTER_TEST(test_text_query_width_big);

// ---------------------------------------------------------------------------
// text::write_xy variations (all to buffer)
// ---------------------------------------------------------------------------

void test_text_write_xy_to_buffer()
{
    myscreen->text_normal.write_xy(10, 10, "Buffer text", (short)1);
}
REGISTER_TEST(test_text_write_xy_to_buffer);

void test_text_write_xy_color_to_buffer()
{
    myscreen->text_normal.write_xy(10, 20, "Colored buffer", (unsigned char)WHITE, (short)1);
}
REGISTER_TEST(test_text_write_xy_color_to_buffer);

void test_text_write_xy_no_color()
{
    myscreen->text_normal.write_xy(10, 30, "No color text");
}
REGISTER_TEST(test_text_write_xy_no_color);

// ---------------------------------------------------------------------------
// text::write_y variations
// ---------------------------------------------------------------------------

void test_text_write_y_basic()
{
    myscreen->text_normal.write_y(50, "Y text");
}
REGISTER_TEST(test_text_write_y_basic);

void test_text_write_y_color()
{
    myscreen->text_normal.write_y(60, "Y colored", (unsigned char)RED);
}
REGISTER_TEST(test_text_write_y_color);

void test_text_write_y_to_buffer()
{
    myscreen->text_normal.write_y(70, "Y buffer", (short)1);
}
REGISTER_TEST(test_text_write_y_to_buffer);

void test_text_write_y_color_to_buffer()
{
    myscreen->text_normal.write_y(80, "Y color buf", (unsigned char)DARK_GREEN, (short)1);
}
REGISTER_TEST(test_text_write_y_color_to_buffer);

// ---------------------------------------------------------------------------
// text::write_xy_center variations
// ---------------------------------------------------------------------------

void test_text_write_xy_center_alpha()
{
    myscreen->text_normal.write_xy_center_alpha(160, 100, WHITE, 128, "Alpha center");
}
REGISTER_TEST(test_text_write_xy_center_alpha);

void test_text_write_xy_center_shadow()
{
    myscreen->text_normal.write_xy_center_shadow(160, 110, WHITE, "Center shadow");
}
REGISTER_TEST(test_text_write_xy_center_shadow);

// ---------------------------------------------------------------------------
// text::write_char_xy variations
// ---------------------------------------------------------------------------

void test_text_write_char_xy_basic()
{
    myscreen->text_normal.write_char_xy(10, 120, 'A');
}
REGISTER_TEST(test_text_write_char_xy_basic);

void test_text_write_char_xy_color()
{
    myscreen->text_normal.write_char_xy(20, 120, 'B', (unsigned char)RED);
}
REGISTER_TEST(test_text_write_char_xy_color);

void test_text_write_char_xy_to_buffer()
{
    myscreen->text_normal.write_char_xy(30, 120, 'C', (short)1);
}
REGISTER_TEST(test_text_write_char_xy_to_buffer);

void test_text_write_char_xy_color_to_buffer()
{
    myscreen->text_normal.write_char_xy(40, 120, 'D', (unsigned char)DARK_BLUE, (short)1);
}
REGISTER_TEST(test_text_write_char_xy_color_to_buffer);

// ---------------------------------------------------------------------------
// text::write_y with viewscreen
// ---------------------------------------------------------------------------

void test_text_write_y_viewscreen()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    myscreen->text_normal.write_y(50, "VS Y text", vs);
}
REGISTER_TEST(test_text_write_y_viewscreen);

void test_text_write_y_color_viewscreen()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    myscreen->text_normal.write_y(60, "VS Y color", (unsigned char)RED, vs);
}
REGISTER_TEST(test_text_write_y_color_viewscreen);

// ---------------------------------------------------------------------------
// text::write_xy with viewscreen
// ---------------------------------------------------------------------------

void test_text_write_xy_viewscreen()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    myscreen->text_normal.write_xy(10, 50, "VS text", vs);
}
REGISTER_TEST(test_text_write_xy_viewscreen);

void test_text_write_xy_color_viewscreen()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    myscreen->text_normal.write_xy(10, 60, "VS color", (unsigned char)WHITE, vs);
}
REGISTER_TEST(test_text_write_xy_color_viewscreen);

// ---------------------------------------------------------------------------
// text::write_char_xy with viewscreen
// ---------------------------------------------------------------------------

void test_text_write_char_xy_viewscreen()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    myscreen->text_normal.write_char_xy(10, 70, 'X', vs);
}
REGISTER_TEST(test_text_write_char_xy_viewscreen);

void test_text_write_char_xy_color_viewscreen()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    myscreen->text_normal.write_char_xy(20, 70, 'Y', (unsigned char)DARK_GREEN, vs);
}
REGISTER_TEST(test_text_write_char_xy_color_viewscreen);

// ---------------------------------------------------------------------------
// text::write_xy_shadow
// ---------------------------------------------------------------------------

void test_text_write_xy_shadow_color()
{
    myscreen->text_normal.write_xy_shadow(50, 50, RED, "Red shadow");
}
REGISTER_TEST(test_text_write_xy_shadow_color);

// ---------------------------------------------------------------------------
// big text
// ---------------------------------------------------------------------------

void test_text_big_write_xy_color()
{
    myscreen->text_big.write_xy(10, 150, "Big colored", (unsigned char)WHITE);
}
REGISTER_TEST(test_text_big_write_xy_color);

void test_text_big_write_y()
{
    myscreen->text_big.write_y(160, "Big centered");
}
REGISTER_TEST(test_text_big_write_y);
