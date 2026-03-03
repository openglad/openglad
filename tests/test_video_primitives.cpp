#include <openglad/interface/screen.h>
#include "test_framework.h"

#include <cstring>

// myscreen is now a macro defined in base.h (via game_session.h)

void test_video_draw_primitives_modify_buffer()
{
    // These primitives render via the SDL-backed Screen in this port.
    // The test is primarily a smoke test to cover the call paths without
    // relying on a specific backing buffer layout.
    og::runtime::current_session->myscreen_->draw_box(10, 10, 50, 20, 200, 0, 1);        // hollow
    og::runtime::current_session->myscreen_->draw_box(60, 10, 90, 25, 123, 1, 1);        // filled
    og::runtime::current_session->myscreen_->draw_rect_filled(100, 10, 30, 10, 77, 200); // alpha
}
REGISTER_TEST(test_video_draw_primitives_modify_buffer);
