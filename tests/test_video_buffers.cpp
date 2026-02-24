#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <array>

// myscreen is now a macro defined in base.h (via game_session.h)

void test_video_putdata_putdata_alpha_and_get_pixel_smoke()
{
    myscreen->clearbuffer();

    // 4x4 image with non-zero palette indices.
    std::array<unsigned char, 16> img{};
    for (int i = 0; i < 16; i++)
        img[i] = static_cast<unsigned char>(10 + i);

    myscreen->putdata(0, 0, 4, 4, img);
    myscreen->putdata_alpha(10, 0, 4, 4, img, 128);

    // Flush buffer to the render surface so get_pixel reads something valid.
    myscreen->swap();

    int idx = -1;
    int found = myscreen->get_pixel(0, 0, &idx);
    TEST_ASSERT(found >= 0 && found <= 255, "get_pixel(x,y) should return a palette index-ish value");

    int found2 = myscreen->get_pixel(0);
    TEST_ASSERT(found2 >= 0 && found2 <= 255, "get_pixel(offset) should return a palette index-ish value");
}
REGISTER_TEST(test_video_putdata_putdata_alpha_and_get_pixel_smoke);

void test_video_draw_rect_and_alpha_lines_smoke()
{
    myscreen->clearbuffer();
    myscreen->draw_rect_filled(0, 0, 20, 20, 100, 128);
    myscreen->hor_line_alpha(0, 30, 50, 77, 128);
    myscreen->ver_line(10, 40, 20, 88);
    myscreen->swap();
}
REGISTER_TEST(test_video_draw_rect_and_alpha_lines_smoke);
