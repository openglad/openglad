#ifndef _TEST_PHYSICAL_FRAME_H__
#define _TEST_PHYSICAL_FRAME_H__

// Pixel readers with no menu-thread machinery: the renderer's physical
// frame and the live draw target's RGB, read on the calling thread. Split
// from test_frame_capture.h (which includes this) so a direct-call suite can
// use them without the injector headers.

#include <openglad/platform/sai2x.h>  // E_Screen (the physical readback)

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// The PHYSICAL frame: what the window really shows, at the display's own
// resolution, read back from the renderer (SDL_RenderReadPixels, the
// tests/integration/test_canvas_scale.cpp precedent). A 320x200 canvas
// capture cannot see a native plane (the CHANGELOG! keyboard picture, a
// world preview, the split-screen hotbar): those are composited only at
// present time.
struct PhysicalFrame {
    std::string name;
    std::string path;       // "" = no output dir, nothing to write
    bool captured = false;
    bool written = false;
    int w = 0;
    int h = 0;
    std::vector<Uint8> rgb;  // w*h*3, RGB row-major

    // The colour at (x, y); black outside the frame.
    std::array<Uint8, 3> at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return {0, 0, 0};
        const std::size_t i =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
             static_cast<std::size_t>(x)) * 3u;
        return {rgb[i], rgb[i + 1], rgb[i + 2]};
    }
};

// The renderer readback itself, on the CALLING thread: the thread that
// renders (a direct-call test draws and presents on the test thread, as
// tests/integration/test_canvas_scale.cpp does). False when there is no
// renderer or the readback failed.
inline bool read_physical_frame_here(PhysicalFrame& frame)
{
    if (!E_Screen || E_Screen->renderer == nullptr)
        return false;
    SDL_Surface* const raw = SDL_RenderReadPixels(E_Screen->renderer, nullptr);
    if (raw == nullptr)
        return false;
    SDL_Surface* const rgb = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(raw);
    if (rgb == nullptr)
        return false;
    frame.w = rgb->w;
    frame.h = rgb->h;
    frame.rgb.resize(static_cast<std::size_t>(rgb->w) *
                     static_cast<std::size_t>(rgb->h) * 3u);
    for (int y = 0; y < rgb->h; ++y)
        std::memcpy(frame.rgb.data() + static_cast<std::size_t>(y) *
                                           static_cast<std::size_t>(rgb->w) * 3u,
                    static_cast<const Uint8*>(rgb->pixels) +
                        static_cast<std::ptrdiff_t>(y) * rgb->pitch,
                    static_cast<std::size_t>(rgb->w) * 3u);
    SDL_DestroySurface(rgb);
    frame.captured = true;
    return true;
}

// Writes a captured physical frame as <output_dir>/<name>.ppm when a
// directory was given (nullptr or "": nothing).
inline void write_physical_frame(PhysicalFrame& frame, const char* output_dir)
{
    if (!frame.captured || output_dir == nullptr || output_dir[0] == '\0')
        return;
    std::error_code error;
    std::filesystem::create_directories(output_dir, error);
    frame.path = std::string(output_dir) + "/" + frame.name + ".ppm";
    FILE* f = error ? nullptr : fopen(frame.path.c_str(), "wb");
    if (f == nullptr)
        return;
    fprintf(f, "P6\n%d %d\n255\n", frame.w, frame.h);
    fwrite(frame.rgb.data(), sizeof(Uint8), frame.rgb.size(), f);
    fclose(f);
    std::error_code exists_error;
    frame.written = std::filesystem::exists(frame.path, exists_error) &&
                    !exists_error;
    fprintf(stderr, "  [physical shot] wrote %s (%dx%d)\n", frame.path.c_str(),
            frame.w, frame.h);
}

// The live draw target's pixels as RGB triples (E_Screen->render, public:
// tests/integration/test_video_primitives.cpp reads it the same way). In
// the shipped classic mode that is the shared 320x200 canvas the HUD blends
// into, where a palette-index readback cannot name a blended pixel.
struct RenderRgb {
    int w = 0;
    int h = 0;
    std::vector<Uint8> rgb;  // w*h*3, RGB row-major

    std::array<Uint8, 3> at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return {0, 0, 0};
        const std::size_t i =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
             static_cast<std::size_t>(x)) * 3u;
        return {rgb[i], rgb[i + 1], rgb[i + 2]};
    }
};

inline RenderRgb capture_render_rgb()
{
    RenderRgb out;
    if (!E_Screen || E_Screen->render == nullptr)
        return out;
    SDL_Surface* const surface = E_Screen->render;
    out.w = surface->w;
    out.h = surface->h;
    out.rgb.resize(static_cast<std::size_t>(out.w) *
                   static_cast<std::size_t>(out.h) * 3u);
    const SDL_PixelFormatDetails* const details =
        SDL_GetPixelFormatDetails(surface->format);
    for (int y = 0; y < out.h; ++y)
    {
        const auto* const row = reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(surface->pixels) +
            static_cast<std::ptrdiff_t>(y) * surface->pitch);
        for (int x = 0; x < out.w; ++x)
        {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.w) +
                 static_cast<std::size_t>(x)) * 3u;
            SDL_GetRGB(row[x], details, nullptr, &out.rgb[i], &out.rgb[i + 1],
                       &out.rgb[i + 2]);
        }
    }
    return out;
}

#endif  // _TEST_PHYSICAL_FRAME_H__
