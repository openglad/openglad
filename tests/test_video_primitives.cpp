#include <openglad/interface/screen.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

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

TEST(VideoPrimitives, sdl_rectangle_button_and_full_swap_wrappers)
{
    sdl_video video(false);
    const CanvasTarget saved_target = video.active_canvas();
    struct CanvasRestore
    {
        sdl_video& video;
        CanvasTarget target;
        ~CanvasRestore() { video.set_active_canvas(target); }
    } restore{video, saved_target};

    video.clearbuffer();
    const SDL_Rect rect{12, 14, 30, 18};

    video.draw_button(rect, 1);
    int raised_border = 0;
    int raised_face = 0;
    EXPECT_EQ(14, video.get_pixel(rect.x, rect.y, &raised_border));
    EXPECT_EQ(13, video.get_pixel(rect.x + 1, rect.y + 1, &raised_face));

    video.draw_button_inverted(rect);
    int inverted_border = 0;
    int inverted_face = 0;
    EXPECT_EQ(11, video.get_pixel(rect.x, rect.y, &inverted_border));
    EXPECT_EQ(12, video.get_pixel(rect.x + 1, rect.y + 1, &inverted_face));
    EXPECT_NE(raised_border, inverted_border);
    EXPECT_NE(raised_face, inverted_face);

    video.set_active_canvas(CanvasTarget::UI);
    video.swap();
    ASSERT_EQ(CanvasTarget::UI, video.last_presented_canvas());
    video.set_active_canvas(CanvasTarget::World);
    video.swap();
    EXPECT_EQ(CanvasTarget::World, video.last_presented_canvas());

    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->render);
    SDL_Surface* const render_surface = E_Screen->render;
    SDL_Window* const window = E_Screen->window;
    const int surface_w = render_surface->w;
    const int surface_h = render_surface->h;
    const int surface_pitch = render_surface->pitch;
    ASSERT_NE(nullptr, render_surface->pixels);
    ASSERT_GT(surface_pitch, 0);
    ASSERT_GT(surface_h, 0);
    const std::size_t surface_bytes =
        static_cast<std::size_t>(surface_pitch) *
        static_cast<std::size_t>(surface_h);
    std::vector<unsigned char> pixels_before(surface_bytes);
    std::memcpy(pixels_before.data(), render_surface->pixels, surface_bytes);
    const int canvas_w = video.canvas_w();
    const int canvas_h = video.canvas_h();
    const int world_w = video.world_canvas_w();
    const int world_h = video.world_canvas_h();
    const CanvasTarget active_canvas = video.active_canvas();
    const CanvasTarget presented_canvas = video.last_presented_canvas();
    const float session_window_w =
        og::runtime::current_session->window_w_;
    const float session_window_h =
        og::runtime::current_session->window_h_;
    int window_w = 0;
    int window_h = 0;
    if (window != nullptr)
        SDL_GetWindowSize(window, &window_w, &window_h);

    restore_web_canvas_backing_size(123, 77);

    EXPECT_EQ(render_surface, E_Screen->render);
    EXPECT_EQ(window, E_Screen->window);
    EXPECT_EQ(surface_w, E_Screen->render->w);
    EXPECT_EQ(surface_h, E_Screen->render->h);
    EXPECT_EQ(surface_pitch, E_Screen->render->pitch);
    EXPECT_EQ(canvas_w, video.canvas_w());
    EXPECT_EQ(canvas_h, video.canvas_h());
    EXPECT_EQ(world_w, video.world_canvas_w());
    EXPECT_EQ(world_h, video.world_canvas_h());
    EXPECT_EQ(active_canvas, video.active_canvas());
    EXPECT_EQ(presented_canvas, video.last_presented_canvas());
    EXPECT_FLOAT_EQ(session_window_w,
                    og::runtime::current_session->window_w_);
    EXPECT_FLOAT_EQ(session_window_h,
                    og::runtime::current_session->window_h_);
    EXPECT_EQ(0, std::memcmp(pixels_before.data(), E_Screen->render->pixels,
                             surface_bytes));
    if (window != nullptr)
    {
        int window_w_after = 0;
        int window_h_after = 0;
        SDL_GetWindowSize(window, &window_w_after, &window_h_after);
        EXPECT_EQ(window_w, window_w_after);
        EXPECT_EQ(window_h, window_h_after);
    }
}
