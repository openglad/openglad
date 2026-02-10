#include "graph.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// video::pointb variants
// ---------------------------------------------------------------------------

void test_video_pointb_basic()
{
    myscreen->pointb(50, 50, 100);
}
REGISTER_TEST(test_video_pointb_basic);

void test_video_pointb_alpha()
{
    myscreen->pointb(60, 50, 100, 128);
}
REGISTER_TEST(test_video_pointb_alpha);

void test_video_pointb_rgb()
{
    myscreen->pointb(70, 50, 200, 100, 50);
}
REGISTER_TEST(test_video_pointb_rgb);

void test_video_pointb_offset()
{
    myscreen->pointb(50 + 50*320, (unsigned char)150);
}
REGISTER_TEST(test_video_pointb_offset);

// ---------------------------------------------------------------------------
// video::hor_line / ver_line
// ---------------------------------------------------------------------------

void test_video_hor_line()
{
    myscreen->hor_line(10, 10, 50, 100);
}
REGISTER_TEST(test_video_hor_line);

void test_video_hor_line_to_buffer()
{
    myscreen->hor_line(10, 20, 50, 100, 1);
}
REGISTER_TEST(test_video_hor_line_to_buffer);

void test_video_hor_line_alpha()
{
    myscreen->hor_line_alpha(10, 30, 50, 100, 128);
}
REGISTER_TEST(test_video_hor_line_alpha);

void test_video_ver_line()
{
    myscreen->ver_line(10, 10, 50, 100);
}
REGISTER_TEST(test_video_ver_line);

void test_video_ver_line_to_buffer()
{
    myscreen->ver_line(10, 10, 50, 100, 1);
}
REGISTER_TEST(test_video_ver_line_to_buffer);

// ---------------------------------------------------------------------------
// video::putdata variants
// ---------------------------------------------------------------------------

void test_video_putdata_span()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 50, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->putdata(50, 50, 16, 16, span);
}
REGISTER_TEST(test_video_putdata_span);

void test_video_putdata_alpha()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 80, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->putdata_alpha(70, 50, 16, 16, span, 128);
}
REGISTER_TEST(test_video_putdata_alpha);

void test_video_putdatatext()
{
    unsigned char testbmp[8*8];
    memset(testbmp, 60, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 64);
    myscreen->putdatatext(90, 50, 8, 8, span);
}
REGISTER_TEST(test_video_putdatatext);

void test_video_putdata_color()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 248, sizeof(testbmp)); // > 247 triggers team color
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->putdata(50, 70, 16, 16, span, (unsigned char)100);
}
REGISTER_TEST(test_video_putdata_color);

void test_video_putdatatext_color()
{
    unsigned char testbmp[8*8];
    memset(testbmp, 248, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 64);
    myscreen->putdatatext(70, 70, 8, 8, span, (unsigned char)100);
}
REGISTER_TEST(test_video_putdatatext_color);

// ---------------------------------------------------------------------------
// video::fastbox_outline
// ---------------------------------------------------------------------------

void test_video_fastbox_outline()
{
    myscreen->fastbox_outline(10, 10, 40, 30, 100);
}
REGISTER_TEST(test_video_fastbox_outline);

// ---------------------------------------------------------------------------
// video::point (screen buffer version)
// ---------------------------------------------------------------------------

void test_video_point()
{
    myscreen->point(50, 50, 100);
}
REGISTER_TEST(test_video_point);

// ---------------------------------------------------------------------------
// video::putbuffer / putbuffer_alpha
// ---------------------------------------------------------------------------

void test_video_putbuffer()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->putbuffer(50, 50, 16, 16, 0, 0, 319, 199, span);
}
REGISTER_TEST(test_video_putbuffer);

void test_video_putbuffer_alpha()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->putbuffer_alpha(50, 50, 16, 16, 0, 0, 319, 199, span, 128);
}
REGISTER_TEST(test_video_putbuffer_alpha);

// ---------------------------------------------------------------------------
// video::draw_rect_filled (more alpha values)
// ---------------------------------------------------------------------------

void test_video_draw_rect_filled_quarter()
{
    myscreen->draw_rect_filled(10, 10, 30, 20, 150, 64);
}
REGISTER_TEST(test_video_draw_rect_filled_quarter);

void test_video_draw_rect_filled_3quarter()
{
    myscreen->draw_rect_filled(50, 10, 30, 20, 200, 192);
}
REGISTER_TEST(test_video_draw_rect_filled_3quarter);

// ---------------------------------------------------------------------------
// video::walkputbuffer clipping edge cases
// ---------------------------------------------------------------------------

void test_video_walkputbuffer_clipped_left()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->walkputbuffer(-8, 50, 16, 16, 0, 0, 319, 199, span, 40);
}
REGISTER_TEST(test_video_walkputbuffer_clipped_left);

void test_video_walkputbuffer_clipped_right()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->walkputbuffer(310, 50, 16, 16, 0, 0, 319, 199, span, 40);
}
REGISTER_TEST(test_video_walkputbuffer_clipped_right);

void test_video_walkputbuffer_clipped_top()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->walkputbuffer(50, -8, 16, 16, 0, 0, 319, 199, span, 40);
}
REGISTER_TEST(test_video_walkputbuffer_clipped_top);

void test_video_walkputbuffer_clipped_bottom()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->walkputbuffer(50, 192, 16, 16, 0, 0, 319, 199, span, 40);
}
REGISTER_TEST(test_video_walkputbuffer_clipped_bottom);

// ---------------------------------------------------------------------------
// video::walkputbuffertext
// ---------------------------------------------------------------------------

void test_video_walkputbuffertext()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->walkputbuffertext(50, 50, 16, 16, 0, 0, 319, 199, span, 40);
}
REGISTER_TEST(test_video_walkputbuffertext);

void test_video_walkputbuffertext_alpha()
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    myscreen->walkputbuffertext_alpha(50, 50, 16, 16, 0, 0, 319, 199, span, 40, 128);
}
REGISTER_TEST(test_video_walkputbuffertext_alpha);

// ---------------------------------------------------------------------------
// video::clearbuffer with rect
// ---------------------------------------------------------------------------

void test_video_clearbuffer_rect()
{
    myscreen->clearbuffer(10, 10, 100, 100);
}
REGISTER_TEST(test_video_clearbuffer_rect);

// ---------------------------------------------------------------------------
// video::draw_text_bar (already tested but exercise more)
// ---------------------------------------------------------------------------

void test_video_draw_text_bar_wide()
{
    myscreen->draw_text_bar(0, 0, 320, 10);
}
REGISTER_TEST(test_video_draw_text_bar_wide);
