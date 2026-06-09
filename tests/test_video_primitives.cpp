#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <cstring>

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(VideoPrimitives, video_draw_primitives_modify_buffer)
{
    screen* s = og::runtime::current_session->myscreen_;
    s->clearbuffer();

    s->draw_box(10, 10, 50, 20, 200, 0, 1);        // hollow
    s->draw_box(60, 10, 90, 25, 123, 1, 1);        // filled
    s->draw_rect_filled(100, 10, 30, 10, 77, 200); // alpha

    int hollow_index = 0;
    int filled_index = 0;
    ASSERT_EQ(200, s->get_pixel(10, 10, &hollow_index));
    ASSERT_EQ(123, s->get_pixel(65, 15, &filled_index));

    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(105, 15, &r, &g, &b);
    ASSERT_TRUE(r != 0 || g != 0 || b != 0)
        << "alpha-filled rectangle should modify its target pixels";
}
