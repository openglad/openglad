#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <openglad/interface/screen.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <vector>


// myscreen is now a macro defined in base.h (via game_session.h)

// Defined in src/render/video.cpp (not exposed in a header).
extern void putpixel(SDL_Surface* surface, int x, int y, Uint32 pixel);
extern void blend_pixel(SDL_Surface* surface, int x, int y, Uint32 color, Uint8 alpha);

// videoptr lives in GameSession — access via current_session->videoptr_.

namespace
{
struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { if (s) SDL_DestroySurface(s); }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

static SurfacePtr make_surface_with_format(int w, int h, SDL_PixelFormat fmt)
{
    SDL_Surface* s = SDL_CreateSurface(w, h, fmt);
    return SurfacePtr(s);
}

static SurfacePtr make_surface_8bpp(int w, int h)
{
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_INDEX8);
    return SurfacePtr(s);
}
} // namespace

TEST(VideoPixelOps, video_putpixel_and_blend_pixel_all_bpp_cases_smoke)
{
    // 8bpp (palette-indexed) case. SDL3: indexed surfaces no longer come with
    // a palette automatically — create one so the palette branch of
    // putpixel/blend_pixel actually executes.
    SurfacePtr s8 = make_surface_8bpp(8, 8);
    ASSERT_TRUE(s8 != nullptr) << "8bpp surface created";
    SDL_Palette* pal = s8 ? SDL_CreateSurfacePalette(s8.get()) : nullptr;
    if (pal) {
        std::array<SDL_Color, 256> colors{};
        for (int i = 0; i < 256; i++) {
            colors[i].r = static_cast<Uint8>(i);
            colors[i].g = static_cast<Uint8>(255 - i);
            colors[i].b = static_cast<Uint8>((i * 3) & 0xFF);
            colors[i].a = 255;
        }
        SDL_SetPaletteColors(pal, colors.data(), 0, static_cast<int>(colors.size()));
        putpixel(s8.get(), 1, 1, 3);
        blend_pixel(s8.get(), 1, 1, 7, 128);
    }

    // 16bpp case.
    SurfacePtr s16 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_RGB565);
    ASSERT_TRUE(s16 != nullptr) << "16bpp surface created";
    if (s16) {
        Uint32 c = SDL_MapSurfaceRGB(s16.get(), 10, 20, 30);
        putpixel(s16.get(), 2, 2, c);
        blend_pixel(s16.get(), 2, 2, SDL_MapSurfaceRGB(s16.get(), 200, 10, 10), 200);
    }

    // 24bpp case.
    SurfacePtr s24 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_RGB24);
    ASSERT_TRUE(s24 != nullptr) << "24bpp surface created";
    if (s24) {
        Uint32 c = SDL_MapSurfaceRGB(s24.get(), 1, 2, 3);
        putpixel(s24.get(), 3, 3, c);
        blend_pixel(s24.get(), 3, 3, SDL_MapSurfaceRGB(s24.get(), 100, 110, 120), 64);
    }

    // 32bpp case.
    SurfacePtr s32 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_ARGB8888);
    ASSERT_TRUE(s32 != nullptr) << "32bpp surface created";
    if (s32) {
        Uint32 c = SDL_MapSurfaceRGBA(s32.get(), 1, 2, 3, 255);
        putpixel(s32.get(), 4, 4, c);
        blend_pixel(s32.get(), 4, 4, SDL_MapSurfaceRGBA(s32.get(), 200, 210, 220, 255), 180);
    }
}

TEST(VideoPixelOps, pixel_format_guards_and_alpha_bits_have_exact_results)
{
    SurfacePtr argb1555 =
        make_surface_with_format(3, 3, SDL_PIXELFORMAT_ARGB1555);
    ASSERT_NE(nullptr, argb1555);
    const SDL_PixelFormatDetails* const details =
        SDL_GetPixelFormatDetails(argb1555->format);
    ASSERT_NE(nullptr, details);
    ASSERT_EQ(2, details->bytes_per_pixel);
    ASSERT_NE(0u, details->Amask);

    const Uint32 destination =
        SDL_MapSurfaceRGBA(argb1555.get(), 12, 74, 139, 255);
    const Uint32 source =
        SDL_MapSurfaceRGBA(argb1555.get(), 225, 98, 31, 0);
    putpixel(argb1555.get(), 1, 1, destination);
    blend_pixel(argb1555.get(), 1, 1, source, 128);

    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 255;
    ASSERT_TRUE(SDL_ReadSurfacePixel(
        argb1555.get(), 1, 1, &red, &green, &blue, &alpha));
    EXPECT_EQ(115, red);
    EXPECT_EQ(82, green);
    EXPECT_EQ(82, blue);
    EXPECT_EQ(0, alpha)
        << "half-blending an opaque destination with transparent input must "
           "clear the one-bit alpha channel";

    SurfacePtr rgba = make_surface_with_format(2, 2, SDL_PIXELFORMAT_ARGB8888);
    ASSERT_NE(nullptr, rgba);
    ASSERT_TRUE(SDL_FillSurfaceRect(rgba.get(), nullptr, 0));
    const std::vector<Uint8> before(
        static_cast<const Uint8*>(rgba->pixels),
        static_cast<const Uint8*>(rgba->pixels) + rgba->pitch * rgba->h);
    putpixel(rgba.get(), -1, 0, 0xFFFFFFFFu);
    putpixel(rgba.get(), rgba->w, rgba->h - 1, 0xFFFFFFFFu);
    EXPECT_EQ(0, std::memcmp(before.data(), rgba->pixels, before.size()))
        << "out-of-bounds writes must leave every surface byte unchanged";
}

TEST(VideoPixelOps, transparent_gameplay_overlay_keeps_zero_alpha_zero)
{
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_FALSE(E_Screen->gameplay_ui_overlay_active())
        << "the gameplay overlay must be inactive on test entry";
    struct CanvasRestore
    {
        int zoom = E_Screen->world_zoom_steps();
        og::WorldScaleMode smoothing = E_Screen->world_scale().mode;
        CanvasTarget target = E_Screen->active_canvas();
        float window_w = og::runtime::current_session->window_w_;
        float window_h = og::runtime::current_session->window_h_;

        ~CanvasRestore()
        {
            E_Screen->discard_gameplay_ui_frame();
            E_Screen->set_world_zoom(
                zoom, smoothing,
                static_cast<int>(window_w), static_cast<int>(window_h));
            E_Screen->set_active_canvas(target);
        }
    } restore;

    E_Screen->set_world_zoom(
        og::kZoomStepsMax, og::WorldScaleMode::Sai, 320, 200);
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    E_Screen->set_active_canvas(CanvasTarget::GameplayUI);

    SDL_Surface* const overlay =
        E_Screen->gameplay_ui_overlay_surface();
    ASSERT_NE(nullptr, overlay);
    ASSERT_EQ(overlay, E_Screen->render)
        << "the GameplayUI target must route writes to the real overlay";
    ASSERT_TRUE(SDL_FillSurfaceRect(overlay, nullptr, 0));
    blend_pixel(
        overlay, 1, 1,
        SDL_MapSurfaceRGBA(overlay, 210, 90, 30, 255), 0);
    Uint8 red = 255;
    Uint8 green = 255;
    Uint8 blue = 255;
    Uint8 alpha = 255;
    ASSERT_TRUE(SDL_ReadSurfacePixel(
        overlay, 1, 1, &red, &green, &blue, &alpha));
    EXPECT_EQ((std::array<Uint8, 4>{0, 0, 0, 0}),
              (std::array<Uint8, 4>{red, green, blue, alpha}))
        << "zero source and destination coverage must remain transparent";
}

TEST(VideoPixelOps, clipped_and_transparent_blits_pin_visible_pixels)
{
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->render);
    sdl_video video(false);
    video.clearbuffer();

    int black = -1;
    ASSERT_EQ(0, video.get_pixel(0, 0, &black));

    const std::array<unsigned char, 2> transparent_then_color{0, 42};
    video.putdata_alpha(10, 10, 2, 1, transparent_then_color, 255);
    int first = -1;
    int second = -1;
    EXPECT_EQ(0, video.get_pixel(10, 10, &first));
    EXPECT_EQ(42, video.get_pixel(11, 10, &second));
    EXPECT_EQ(black, first)
        << "the transparent alpha entry must preserve its destination";

    const std::array<unsigned char, 2> transparent_then_team{0, 250};
    video.putdata(12, 10, 2, 1, transparent_then_team, 77);
    EXPECT_EQ(0, video.get_pixel(12, 10, &first));
    EXPECT_EQ(77, video.get_pixel(13, 10, &second));
    EXPECT_EQ(black, first)
        << "the transparent team-color entry must preserve its destination";

    const std::array<unsigned char, 4> clipped_tile{11, 12, 13, 14};
    video.putbuffer(-1, -1, 2, 2, -1, -1, 1, 1, clipped_tile);
    EXPECT_EQ(14, video.get_pixel(0, 0, &second))
        << "only the lower-right source pixel lies on the real surface";
    EXPECT_EQ(0, video.get_pixel(1, 0, &first));
    EXPECT_EQ(0, video.get_pixel(0, 1, &first));

    SurfacePtr source =
        make_surface_with_format(2, 2, SDL_PIXELFORMAT_ARGB8888);
    ASSERT_NE(nullptr, source);
    ASSERT_TRUE(SDL_FillSurfaceRect(
        source.get(), nullptr, SDL_MapSurfaceRGB(source.get(), 200, 40, 20)));
    const Uint32 sample_before = static_cast<const Uint32*>(
        E_Screen->render->pixels)[5 + 5 * E_Screen->render->pitch / 4];
    video.putbuffer(5, 5, 0, 2, 0, 0, 20, 20, source.get());
    const Uint32 sample_after = static_cast<const Uint32*>(
        E_Screen->render->pixels)[5 + 5 * E_Screen->render->pitch / 4];
    EXPECT_EQ(sample_before, sample_after)
        << "a zero-width surface blit must be a no-op";
}

TEST(VideoPixelOps, sprite_degenerate_and_shift_paths_have_exact_results)
{
    sdl_video video(false);
    video.clearbuffer();
    const std::array<unsigned char, 4> sprite{31, 32, 33, 34};

    const Uint32 untouched = static_cast<const Uint32*>(
        E_Screen->render->pixels)[20 + 20 * E_Screen->render->pitch / 4];
    video.walkputbuffer_alpha(
        20, 20, 0, 2, 0, 0, 40, 40, sprite, 40, 255);
    video.walkputbuffertext(
        20, 20, 0, 2, 0, 0, 40, 40, sprite, 40);
    EXPECT_EQ(untouched, static_cast<const Uint32*>(
        E_Screen->render->pixels)[20 + 20 * E_Screen->render->pitch / 4]);

    video.walkputbuffer_shadow(
        24, 20, 2, 2, 0, 0, 40, 40, sprite, 255, 0, 0);
    int index = -1;
    EXPECT_EQ(PURE_BLACK, video.get_pixel(24, 22, &index));
    EXPECT_EQ(PURE_BLACK, video.get_pixel(25, 21, &index));

    const std::array<unsigned char, 4> solid{1, 1, 1, 1};
    video.walkputbuffer(
        38, 24, 4, 1, 0, 0, 40, 40, solid, 40,
        static_cast<unsigned char>(NORMAL_MODE), 0, 0, 0);
    EXPECT_EQ(1, video.get_pixel(38, 24, &index));
    EXPECT_EQ(1, video.get_pixel(39, 24, &index));
    EXPECT_EQ(0, video.get_pixel(40, 24, &index));

    video.pointb(30, 30, 10);
    video.walkputbuffer(
        30, 30, 1, 1, 0, 0, 40, 40, solid, 40,
        static_cast<unsigned char>(PHANTOM_MODE), 0, 0,
        static_cast<unsigned char>(SHIFT_LIGHTER));
    EXPECT_EQ(9, video.get_pixel(30, 30, &index));

    video.pointb(31, 30, 10);
    video.walkputbuffer(
        31, 30, 1, 1, 0, 0, 40, 40, solid, 40,
        static_cast<unsigned char>(PHANTOM_MODE), 0, 0,
        static_cast<unsigned char>(SHIFT_DARKER));
    EXPECT_EQ(11, video.get_pixel(31, 30, &index));
}

TEST(VideoPixelOps, null_render_line_guard_is_a_no_op)
{
    ASSERT_NE(nullptr, E_Screen);
    SDL_Surface* const saved_render = E_Screen->render;
    struct RenderRestore
    {
        SDL_Surface* saved;
        ~RenderRestore() { E_Screen->render = saved; }
    } restore{saved_render};
    E_Screen->render = nullptr;

    sdl_video video(false);
    video.draw_line(0, 0, 3, 3, 42);
    EXPECT_EQ(nullptr, E_Screen->render);
}


TEST(VideoPixelOps, video_putblack_uses_overridden_videoptr_buffer)
{
    // Legacy putblack writes to `videoptr`. In the original DOS codebase this
    // was linear VGA memory. Override it in tests to ensure it remains safe.
    unsigned char* saved = og::runtime::current_session->videoptr_;
    std::array<unsigned char, 64000> buffer{};
    buffer.fill(42);
    og::runtime::current_session->videoptr_ = buffer.data();

    og::runtime::current_session->myscreen_->putblack(0, 0, 10, 10);
    og::runtime::current_session->myscreen_->putblack(-10, -10, 10, 10); // bounds check via curpoint
    og::runtime::current_session->myscreen_->putblack(319, 199, 5, 5);   // partial bounds

    og::runtime::current_session->videoptr_ = saved;
}


TEST(VideoPixelOps, video_darken_and_fastbox_negative_inputs_smoke)
{
    og::runtime::current_session->myscreen_->darken_screen();

    // Exercise fastbox early-return for invalid sizes/coords.
    og::runtime::current_session->myscreen_->fastbox(-1, 0, 10, 10, 1, 1);
    og::runtime::current_session->myscreen_->fastbox(0, -1, 10, 10, 1, 1);
    og::runtime::current_session->myscreen_->fastbox(0, 0, -10, 10, 1, 1);
    og::runtime::current_session->myscreen_->fastbox(0, 0, 10, -10, 1, 1);
}
