#include <gtest/gtest.h>

#include <openglad/interface/screen.h>

#include <cstring>
#include <span>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// video::pointb variants
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_pointb_basic)
{
    og::runtime::current_session->myscreen_->pointb(50, 50, 100);
}


TEST(VideoExtra, video_pointb_alpha)
{
    og::runtime::current_session->myscreen_->pointb(60, 50, 100, 128);
}


TEST(VideoExtra, video_pointb_rgb)
{
    og::runtime::current_session->myscreen_->pointb(70, 50, 200, 100, 50);
}


TEST(VideoExtra, video_pointb_offset)
{
    og::runtime::current_session->myscreen_->pointb(50 + 50*320, (unsigned char)150);
}


// ---------------------------------------------------------------------------
// video::hor_line / ver_line
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_hor_line)
{
    og::runtime::current_session->myscreen_->hor_line(10, 10, 50, 100);
}


TEST(VideoExtra, video_hor_line_to_buffer)
{
    og::runtime::current_session->myscreen_->hor_line(10, 20, 50, 100, 1);
}


TEST(VideoExtra, video_hor_line_alpha)
{
    og::runtime::current_session->myscreen_->hor_line_alpha(10, 30, 50, 100, 128);
}


TEST(VideoExtra, video_ver_line)
{
    og::runtime::current_session->myscreen_->ver_line(10, 10, 50, 100);
}


TEST(VideoExtra, video_ver_line_to_buffer)
{
    og::runtime::current_session->myscreen_->ver_line(10, 10, 50, 100, 1);
}


// ---------------------------------------------------------------------------
// video::putdata variants
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_putdata_span)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 50, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->putdata(50, 50, 16, 16, span);
}


TEST(VideoExtra, video_putdata_alpha)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 80, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->putdata_alpha(70, 50, 16, 16, span, 128);
}


TEST(VideoExtra, video_putdatatext)
{
    unsigned char testbmp[8*8];
    memset(testbmp, 60, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 64);
    og::runtime::current_session->myscreen_->putdatatext(90, 50, 8, 8, span);
}


TEST(VideoExtra, video_putdata_color)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 248, sizeof(testbmp)); // > 247 triggers team color
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->putdata(50, 70, 16, 16, span, (unsigned char)100);
}


TEST(VideoExtra, video_putdatatext_color)
{
    unsigned char testbmp[8*8];
    memset(testbmp, 248, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 64);
    og::runtime::current_session->myscreen_->putdatatext(70, 70, 8, 8, span, (unsigned char)100);
}


// ---------------------------------------------------------------------------
// video::fastbox_outline
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_fastbox_outline)
{
    og::runtime::current_session->myscreen_->fastbox_outline(10, 10, 40, 30, 100);
}


// ---------------------------------------------------------------------------
// video::point (screen buffer version)
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_point)
{
    og::runtime::current_session->myscreen_->point(50, 50, 100);
}


// ---------------------------------------------------------------------------
// video::putbuffer / putbuffer_alpha
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_putbuffer)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->putbuffer(50, 50, 16, 16, 0, 0, 319, 199, span);
}


TEST(VideoExtra, video_putbuffer_alpha)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->putbuffer_alpha(50, 50, 16, 16, 0, 0, 319, 199, span, 128);
}


// ---------------------------------------------------------------------------
// video::draw_rect_filled (more alpha values)
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_draw_rect_filled_quarter)
{
    og::runtime::current_session->myscreen_->draw_rect_filled(10, 10, 30, 20, 150, 64);
}


TEST(VideoExtra, video_draw_rect_filled_3quarter)
{
    og::runtime::current_session->myscreen_->draw_rect_filled(50, 10, 30, 20, 200, 192);
}


// ---------------------------------------------------------------------------
// video::walkputbuffer clipping edge cases
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_walkputbuffer_clipped_left)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(-8, 50, 16, 16, 0, 0, 319, 199, span, 40);
}


TEST(VideoExtra, video_walkputbuffer_clipped_right)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(310, 50, 16, 16, 0, 0, 319, 199, span, 40);
}


TEST(VideoExtra, video_walkputbuffer_clipped_top)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffer(50, -8, 16, 16, 0, 0, 319, 199, span, 40);
}


// A sprite that hangs off the bottom of a viewport must be cut at the port
// edge, not drawn over the pane below it (the classic split-screen bug: the
// lower seat's view gains a stripe of the upper seat's sprites). The clip
// belongs to the port rectangle, so the SAME sprite at the SAME place draws
// its remaining rows once the port extends far enough to hold them.
TEST(VideoExtra, video_walkputbuffer_clips_at_the_port_bottom_edge)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    constexpr int kSpriteColor = 47;
    constexpr int kSpriteX = 50;
    constexpr int kSpriteY = 112;
    constexpr int kPortBottom = 120;
    unsigned char testbmp[16*16];
    memset(testbmp, kSpriteColor, sizeof(testbmp));
    const auto span = std::span<const unsigned char>(testbmp, 256);

    scr->clearbuffer();
    scr->walkputbuffer(kSpriteX, kSpriteY, 16, 16, 0, 0, 319, kPortBottom,
                       span, 40, NORMAL_MODE, 0, 0, 0);
    int index = 0;
    EXPECT_EQ(kSpriteColor, scr->get_pixel(kSpriteX, kSpriteY, &index))
        << "the sprite's first row is inside the port and must be drawn";
    EXPECT_EQ(kSpriteColor,
              scr->get_pixel(kSpriteX, kPortBottom - 1, &index))
        << "the last row inside the port must be drawn";
    EXPECT_EQ(0, scr->get_pixel(kSpriteX, kPortBottom, &index))
        << "the first row past the port edge must be untouched";
    EXPECT_EQ(0, scr->get_pixel(kSpriteX, kSpriteY + 15, &index))
        << "and so must the sprite's own last row";

    // Same sprite, taller port: the rows the clip removed come back, so the
    // zeros above are the clip and not a sprite that never drew.
    scr->clearbuffer();
    scr->walkputbuffer(kSpriteX, kSpriteY, 16, 16, 0, 0, 319, 199,
                       span, 40, NORMAL_MODE, 0, 0, 0);
    EXPECT_EQ(kSpriteColor, scr->get_pixel(kSpriteX, kPortBottom, &index));
    EXPECT_EQ(kSpriteColor, scr->get_pixel(kSpriteX, kSpriteY + 15, &index));
    scr->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::walkputbuffertext
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_walkputbuffertext)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffertext(50, 50, 16, 16, 0, 0, 319, 199, span, 40);
}


TEST(VideoExtra, video_walkputbuffertext_alpha)
{
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto span = std::span<const unsigned char>(testbmp, 256);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(50, 50, 16, 16, 0, 0, 319, 199, span, 40, 128);
}


// ---------------------------------------------------------------------------
// video::clearbuffer with rect
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_clearbuffer_rect)
{
    og::runtime::current_session->myscreen_->clearbuffer(10, 10, 100, 100);
}


// ---------------------------------------------------------------------------
// video::draw_text_bar (already tested but exercise more)
// ---------------------------------------------------------------------------

TEST(VideoExtra, video_draw_text_bar_wide)
{
    og::runtime::current_session->myscreen_->draw_text_bar(0, 0, 320, 10);
}

