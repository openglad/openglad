#include <openglad/interface/screen.h>
#include "test_framework.h"

#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

void test_video_putdata_putdata_alpha_and_get_pixel_smoke()
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
    TEST_ASSERT(found >= 0 && found <= 255, "get_pixel(x,y) should return a palette index-ish value");

    int found2 = og::runtime::current_session->myscreen_->get_pixel(0);
    TEST_ASSERT(found2 >= 0 && found2 <= 255, "get_pixel(offset) should return a palette index-ish value");
}
REGISTER_TEST(test_video_putdata_putdata_alpha_and_get_pixel_smoke);

void test_video_draw_rect_and_alpha_lines_smoke()
{
    og::runtime::current_session->myscreen_->clearbuffer();
    og::runtime::current_session->myscreen_->draw_rect_filled(0, 0, 20, 20, 100, 128);
    og::runtime::current_session->myscreen_->hor_line_alpha(0, 30, 50, 77, 128);
    og::runtime::current_session->myscreen_->ver_line(10, 40, 20, 88);
    og::runtime::current_session->myscreen_->swap();
}
REGISTER_TEST(test_video_draw_rect_and_alpha_lines_smoke);
