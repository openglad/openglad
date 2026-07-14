#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <openglad/interface/screen.h>

#include <array>
#include <memory>


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

