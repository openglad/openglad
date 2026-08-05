#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(VideoBuffers, video_putdata_putdata_alpha_and_get_pixel_smoke)
{
    og::runtime::current_session->myscreen_->clearbuffer();

    // 4x4 image with non-zero palette indices.
    std::array<unsigned char, 16> img{};
    for (int i = 0; i < 16; i++)
        img[i] = static_cast<unsigned char>(10 + i);

    og::runtime::current_session->myscreen_->putdata(0, 0, 4, 4, img);
    og::runtime::current_session->myscreen_->putdata_alpha(10, 0, 4, 4, img, 128);

    // Flush buffer to the render surface so get_pixel reads something valid.
    og::runtime::current_session->myscreen_->swap();

    int idx = -1;
    int found = og::runtime::current_session->myscreen_->get_pixel(0, 0, &idx);
    ASSERT_TRUE(found >= 0 && found <= 255) << "get_pixel(x,y) should return a palette index-ish value";

    int found2 = og::runtime::current_session->myscreen_->get_pixel(0);
    ASSERT_TRUE(found2 >= 0 && found2 <= 255) << "get_pixel(offset) should return a palette index-ish value";
}


TEST(VideoBuffers, get_pixel_rejects_out_of_range_coordinates)
{
    // get_pixel does raw pointer arithmetic + memcpy into the render surface;
    // out-of-range x/y or offset must be rejected (black / 0) rather than read
    // outside the surface bounds.
    auto* s = og::runtime::current_session->myscreen_;
    s->swap();
    int idx = -1;
    int v1 = s->get_pixel(1000000, 1000000, &idx); // exercises x/y bounds guard
    ASSERT_TRUE(v1 >= 0 && v1 <= 255) << "out-of-range get_pixel(x,y) must be a safe value";
    int v2 = s->get_pixel(-50, -50, &idx);
    ASSERT_TRUE(v2 >= 0 && v2 <= 255) << "negative get_pixel(x,y) must be a safe value";
    ASSERT_EQ(0, s->get_pixel(-1)) << "negative offset must be rejected";
    ASSERT_EQ(0, s->get_pixel(1000000000)) << "huge offset must be rejected";
}


TEST(VideoBuffers, video_draw_rect_and_alpha_lines_smoke)
{
    og::runtime::current_session->myscreen_->clearbuffer();
    og::runtime::current_session->myscreen_->draw_rect_filled(0, 0, 20, 20, 100, 128);
    og::runtime::current_session->myscreen_->hor_line_alpha(0, 30, 50, 77, 128);
    og::runtime::current_session->myscreen_->ver_line(10, 40, 20, 88);
    og::runtime::current_session->myscreen_->swap();
}

