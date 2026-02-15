#include "test_framework.h"

#include <openglad/runtime/screen.h>

#include <array>
#include <memory>

#include "SDL.h"

extern screen* myscreen;

// Defined in src/render/video.cpp (not exposed in a header).
extern void putpixel(SDL_Surface* surface, int x, int y, Uint32 pixel);
extern void blend_pixel(SDL_Surface* surface, int x, int y, Uint32 color, Uint8 alpha);

// Global in src/render/video.cpp. In the SDL port, some legacy code still
// uses `videoptr` directly; override it in tests to avoid writing to 0xA0000.
extern unsigned char* videoptr;

namespace
{
struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { if (s) SDL_FreeSurface(s); }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

static SurfacePtr make_surface_with_format(int w, int h, Uint32 fmt)
{
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, SDL_BITSPERPIXEL(fmt), fmt);
    return SurfacePtr(s);
}

static SurfacePtr make_surface_8bpp(int w, int h)
{
    SDL_Surface* s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
    return SurfacePtr(s);
}
} // namespace

void test_video_putpixel_and_blend_pixel_all_bpp_cases_smoke()
{
    // 8bpp (palette-indexed) case.
    SurfacePtr s8 = make_surface_8bpp(8, 8);
    TEST_ASSERT(s8 != nullptr, "8bpp surface created");
    if (s8 && s8->format && s8->format->palette) {
        std::array<SDL_Color, 256> colors{};
        for (int i = 0; i < 256; i++) {
            colors[i].r = static_cast<Uint8>(i);
            colors[i].g = static_cast<Uint8>(255 - i);
            colors[i].b = static_cast<Uint8>((i * 3) & 0xFF);
            colors[i].a = 255;
        }
        SDL_SetPaletteColors(s8->format->palette, colors.data(), 0, static_cast<int>(colors.size()));
        putpixel(s8.get(), 1, 1, 3);
        blend_pixel(s8.get(), 1, 1, 7, 128);
    }

    // 16bpp case.
    SurfacePtr s16 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_RGB565);
    TEST_ASSERT(s16 != nullptr, "16bpp surface created");
    if (s16) {
        Uint32 c = SDL_MapRGB(s16->format, 10, 20, 30);
        putpixel(s16.get(), 2, 2, c);
        blend_pixel(s16.get(), 2, 2, SDL_MapRGB(s16->format, 200, 10, 10), 200);
    }

    // 24bpp case.
    SurfacePtr s24 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_RGB24);
    TEST_ASSERT(s24 != nullptr, "24bpp surface created");
    if (s24) {
        Uint32 c = SDL_MapRGB(s24->format, 1, 2, 3);
        putpixel(s24.get(), 3, 3, c);
        blend_pixel(s24.get(), 3, 3, SDL_MapRGB(s24->format, 100, 110, 120), 64);
    }

    // 32bpp case.
    SurfacePtr s32 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_ARGB8888);
    TEST_ASSERT(s32 != nullptr, "32bpp surface created");
    if (s32) {
        Uint32 c = SDL_MapRGBA(s32->format, 1, 2, 3, 255);
        putpixel(s32.get(), 4, 4, c);
        blend_pixel(s32.get(), 4, 4, SDL_MapRGBA(s32->format, 200, 210, 220, 255), 180);
    }
}
REGISTER_TEST(test_video_putpixel_and_blend_pixel_all_bpp_cases_smoke);

void test_video_putblack_uses_overridden_videoptr_buffer()
{
    // Legacy putblack writes to `videoptr`. In the original DOS codebase this
    // was linear VGA memory. Override it in tests to ensure it remains safe.
    unsigned char* saved = videoptr;
    std::array<unsigned char, 64000> buffer{};
    buffer.fill(42);
    videoptr = buffer.data();

    myscreen->putblack(0, 0, 10, 10);
    myscreen->putblack(-10, -10, 10, 10); // bounds check via curpoint
    myscreen->putblack(319, 199, 5, 5);   // partial bounds

    videoptr = saved;
}
REGISTER_TEST(test_video_putblack_uses_overridden_videoptr_buffer);

void test_video_darken_and_fastbox_negative_inputs_smoke()
{
    myscreen->darken_screen();

    // Exercise fastbox early-return for invalid sizes/coords.
    myscreen->fastbox(-1, 0, 10, 10, 1, 1);
    myscreen->fastbox(0, -1, 10, 10, 1, 1);
    myscreen->fastbox(0, 0, -10, 10, 1, 1);
    myscreen->fastbox(0, 0, 10, -10, 1, 1);
}
REGISTER_TEST(test_video_darken_and_fastbox_negative_inputs_smoke);
