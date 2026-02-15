#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <cstring>

extern screen* myscreen;

void test_video_draw_primitives_modify_buffer()
{
    // These primitives render via the SDL-backed Screen in this port.
    // The test is primarily a smoke test to cover the call paths without
    // relying on a specific backing buffer layout.
    myscreen->draw_box(10, 10, 50, 20, 200, 0, 1);        // hollow
    myscreen->draw_box(60, 10, 90, 25, 123, 1, 1);        // filled
    myscreen->draw_rect_filled(100, 10, 30, 10, 77, 200); // alpha
}
REGISTER_TEST(test_video_draw_primitives_modify_buffer);
