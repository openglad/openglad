#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <openglad/interface/screen.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/interface/render/pal32.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>
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

// putpixel writes the mapped value at the surface's bytes_per_pixel stride;
// blend_pixel mixes each channel as c + (((target - c) * alpha) >> 8) for
// 1/2/3/4 bytes per pixel (video_sdl.cpp). Every case below reads the bytes
// back, so a writer that stored at the wrong stride, or a blender that
// returned the destination untouched, is red.
TEST(VideoPixelOps, video_putpixel_and_blend_pixel_write_exact_bytes_for_every_bpp)
{
    // The 32-bpp blender has a second, premultiplied path that only runs while
    // the gameplay-UI overlay canvas is active; these surfaces exercise the
    // legacy masked path, so state that from the start.
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(CanvasTarget::GameplayUI, E_Screen->active_canvas())
        << "these cases pin the legacy masked blend, not the overlay compositor";

    // 8bpp (palette-indexed). SDL3: indexed surfaces no longer come with a
    // palette automatically -- create one so the palette branch runs.
    SurfacePtr s8 = make_surface_8bpp(8, 8);
    ASSERT_NE(nullptr, s8) << "8bpp surface created";
    SDL_Palette* pal = SDL_CreateSurfacePalette(s8.get());
    ASSERT_NE(nullptr, pal) << "8bpp palette created";
    std::array<SDL_Color, 256> colors{};
    for (int i = 0; i < 256; i++) {
        colors[static_cast<std::size_t>(i)].r = static_cast<Uint8>(i);
        colors[static_cast<std::size_t>(i)].g = static_cast<Uint8>(255 - i);
        colors[static_cast<std::size_t>(i)].b = static_cast<Uint8>((i * 3) & 0xFF);
        colors[static_cast<std::size_t>(i)].a = 255;
    }
    ASSERT_TRUE(SDL_SetPaletteColors(pal, colors.data(), 0, static_cast<int>(colors.size())));
    ASSERT_TRUE(SDL_FillSurfaceRect(s8.get(), nullptr, 0));
    const Uint8* const bytes8 = static_cast<const Uint8*>(s8->pixels);
    putpixel(s8.get(), 1, 1, 3);
    EXPECT_EQ(3, static_cast<int>(bytes8[1 * s8->pitch + 1]))
        << "1 byte per pixel: the index lands at y*pitch + x";
    EXPECT_EQ(0, static_cast<int>(bytes8[1 * s8->pitch + 0]))
        << "the neighbouring index is untouched";
    // destination palette[3] = (3,252,9), source palette[7] = (7,248,21):
    // (3 + ((4*128)>>8), 252 + ((-4*128)>>8), 9 + ((12*128)>>8)) = (5,250,15),
    // which is exactly palette entry 5.
    blend_pixel(s8.get(), 1, 1, 7, 128);
    EXPECT_EQ(5, static_cast<int>(bytes8[1 * s8->pitch + 1]))
        << "the 8bpp blend mixes through the palette and remaps to entry 5";

    // 16bpp RGB565.
    SurfacePtr s16 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_RGB565);
    ASSERT_NE(nullptr, s16) << "16bpp surface created";
    ASSERT_TRUE(SDL_FillSurfaceRect(s16.get(), nullptr, 0));
    const Uint32 dest16 = SDL_MapSurfaceRGB(s16.get(), 10, 20, 30);
    ASSERT_EQ(2211u, dest16) << "RGB565 packs (10,20,30) as 1:5:3";
    putpixel(s16.get(), 2, 2, dest16);
    const Uint16* const words16 = reinterpret_cast<const Uint16*>(
        static_cast<const Uint8*>(s16->pixels) + 2 * s16->pitch);
    EXPECT_EQ(2211u, static_cast<unsigned>(words16[2]))
        << "2 bytes per pixel: the mapped word lands at y*pitch/2 + x";
    EXPECT_EQ(0u, static_cast<unsigned>(words16[1]))
        << "the neighbouring word is untouched";
    // Masked 565 blend of 2211 toward 51265 at alpha 200: red climbs to
    // 0x9800, and the two channels whose source sits BELOW the destination
    // wrap through the unsigned subtraction and land on 0x0040 / 0x0001.
    blend_pixel(s16.get(), 2, 2, SDL_MapSurfaceRGB(s16.get(), 200, 10, 10), 200);
    EXPECT_EQ(38977u, static_cast<unsigned>(words16[2]))
        << "the 16bpp blend is masked per channel";

    // 24bpp RGB24: the legacy blender indexes destination bytes by shift/8,
    // so state the format's shifts rather than assume them.
    SurfacePtr s24 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_RGB24);
    ASSERT_NE(nullptr, s24) << "24bpp surface created";
    const SDL_PixelFormatDetails* const d24 =
        SDL_GetPixelFormatDetails(s24->format);
    ASSERT_NE(nullptr, d24);
    ASSERT_EQ(3, d24->bytes_per_pixel);
    ASSERT_EQ(0, static_cast<int>(d24->Rshift));
    ASSERT_EQ(8, static_cast<int>(d24->Gshift));
    ASSERT_EQ(16, static_cast<int>(d24->Bshift));
    ASSERT_EQ(0u, d24->Amask);
    ASSERT_TRUE(SDL_FillSurfaceRect(s24.get(), nullptr, 0));
    const Uint32 c24 = SDL_MapSurfaceRGB(s24.get(), 1, 2, 3);
    putpixel(s24.get(), 3, 3, c24);
    const Uint8* const row24 =
        static_cast<const Uint8*>(s24->pixels) + 3 * s24->pitch;
    EXPECT_EQ(static_cast<int>(c24 & 0xFFu), static_cast<int>(row24[3 * 3 + 0]));
    EXPECT_EQ(static_cast<int>((c24 >> 8) & 0xFFu), static_cast<int>(row24[3 * 3 + 1]));
    EXPECT_EQ(static_cast<int>((c24 >> 16) & 0xFFu), static_cast<int>(row24[3 * 3 + 2]))
        << "3 bytes per pixel: the mapped value is split little-endian";
    // Blend a still-black pixel (4,3) toward (100,110,120) at alpha 128: each
    // channel is 0 + ((s*128)>>8) == s/2, stored at byte index shift/8. The
    // alpha-less format leaves Ashift at 0, so the blender also recomputes
    // "alpha" from the red byte and writes it back over byte 0 -- the same
    // value, which is why this legacy quirk is invisible in practice.
    blend_pixel(s24.get(), 4, 3, SDL_MapSurfaceRGB(s24.get(), 100, 110, 120), 128);
    EXPECT_EQ(50, static_cast<int>(row24[4 * 3 + 0]));
    EXPECT_EQ(55, static_cast<int>(row24[4 * 3 + 1]));
    EXPECT_EQ(60, static_cast<int>(row24[4 * 3 + 2]))
        << "the 24bpp blend halves each channel and writes it at shift/8";

    // 32bpp ARGB8888.
    SurfacePtr s32 = make_surface_with_format(8, 8, SDL_PIXELFORMAT_ARGB8888);
    ASSERT_NE(nullptr, s32) << "32bpp surface created";
    ASSERT_TRUE(SDL_FillSurfaceRect(s32.get(), nullptr, 0));
    putpixel(s32.get(), 4, 4, SDL_MapSurfaceRGBA(s32.get(), 1, 2, 3, 255));
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 0;
    ASSERT_TRUE(SDL_ReadSurfacePixel(s32.get(), 4, 4, &r, &g, &b, &a));
    EXPECT_EQ(1, static_cast<int>(r));
    EXPECT_EQ(2, static_cast<int>(g));
    EXPECT_EQ(3, static_cast<int>(b));
    EXPECT_EQ(255, static_cast<int>(a));
    ASSERT_TRUE(SDL_ReadSurfacePixel(s32.get(), 3, 4, &r, &g, &b, &a));
    EXPECT_EQ(0, static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b))
        << "4 bytes per pixel: the neighbouring pixel is untouched";
    // (1,2,3) blended toward (200,210,220) at alpha 180, masked per channel:
    // 1 + ((199*180)>>8) -> 140, 2 + ((208*180)>>8) -> 148,
    // 3 + ((217*180)>>8) -> 155; destination alpha is preserved.
    blend_pixel(s32.get(), 4, 4, SDL_MapSurfaceRGBA(s32.get(), 200, 210, 220, 255), 180);
    ASSERT_TRUE(SDL_ReadSurfacePixel(s32.get(), 4, 4, &r, &g, &b, &a));
    EXPECT_EQ(140, static_cast<int>(r));
    EXPECT_EQ(148, static_cast<int>(g));
    EXPECT_EQ(155, static_cast<int>(b));
    EXPECT_EQ(255, static_cast<int>(a))
        << "the 32bpp blend keeps the destination alpha";
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


// putblack zeroes videoptr_[x + y*canvas_w] for the requested rect, but only
// where 0 < curpoint < canvas_size -- so index 0 survives (the historical
// `curpoint > 0` quirk), a fully negative rect writes nothing at all, and a
// rect hanging off the bottom-right clears exactly the one cell still inside.
TEST(VideoPixelOps, video_putblack_zeroes_only_the_in_range_rect_cells)
{
    // Legacy putblack writes to `videoptr`. In the original DOS codebase this
    // was linear VGA memory. Override it in tests to ensure it remains safe.
    unsigned char* saved = og::runtime::current_session->videoptr_;
    struct VideoPtrRestore
    {
        unsigned char* saved;
        ~VideoPtrRestore() { og::runtime::current_session->videoptr_ = saved; }
    } restore{saved};
    auto buffer = std::make_unique<std::array<unsigned char, 64000>>();
    buffer->fill(42);
    og::runtime::current_session->videoptr_ = buffer->data();

    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    ASSERT_EQ(320, s->canvas_w())
        << "the offsets below assume the classic 320x200 canvas";
    ASSERT_EQ(200, s->canvas_h());

    s->putblack(0, 0, 10, 10);
    EXPECT_EQ(42, static_cast<int>((*buffer)[0]))
        << "offset 0 is never cleared: the guard is curpoint > 0";
    EXPECT_EQ(0, static_cast<int>((*buffer)[1])) << "the rest of row 0 is cleared";
    EXPECT_EQ(0, static_cast<int>((*buffer)[9 + 9 * 320]))
        << "the bottom-right cell of the rect is cleared";
    EXPECT_EQ(42, static_cast<int>((*buffer)[10]))
        << "the column just past the rect is left alone";
    EXPECT_EQ(42, static_cast<int>((*buffer)[10 * 320]))
        << "the row just past the rect is left alone";

    buffer->fill(42);
    s->putblack(-10, -10, 10, 10);
    EXPECT_TRUE(std::all_of(buffer->begin(), buffer->end(),
                            [](unsigned char v) { return v == 42; }))
        << "a rect entirely above/left of the canvas writes nothing";

    buffer->fill(42);
    s->putblack(319, 199, 5, 5);
    EXPECT_EQ(0, static_cast<int>((*buffer)[63999]))
        << "the single in-range cell of an overhanging rect is cleared";
    EXPECT_EQ(42, static_cast<int>((*buffer)[63998]))
        << "its neighbour is outside the rect and survives";
    EXPECT_EQ(1, std::count(buffer->begin(), buffer->end(),
                            static_cast<unsigned char>(0)))
        << "exactly one cell was cleared";
}


// darken_screen blends PURE_BLACK over every canvas pixel at alpha 100, and
// fastbox returns immediately when any of startx/starty/xsize/ysize is
// negative (video_sdl.cpp).
TEST(VideoPixelOps, darken_screen_dims_every_pixel_and_negative_fastbox_draws_nothing)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->render);
    ASSERT_NE(CanvasTarget::GameplayUI, E_Screen->active_canvas())
        << "this pins the legacy masked blend, not the overlay compositor";

    // Ground: one flat palette colour over the whole canvas.
    constexpr unsigned char kGround = 7;
    s->clearbuffer();
    s->fastbox(0, 0, s->canvas_w(), s->canvas_h(), kGround, 1);
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    s->get_pixel(160, 100, &r, &g, &b);
    const int bright_r = static_cast<int>(r);
    const int bright_g = static_cast<int>(g);
    const int bright_b = static_cast<int>(b);
    ASSERT_NE(0, bright_r + bright_g + bright_b) << "the ground must be visible";

    // The masked alpha-100 blend toward black (palette entry 0 is 0,0,0)
    // leaves floor(c * 39936 / 65536) in each 8-bit channel: the blend adds
    // ((0 - (c<<16)) * 100) >> 8 to c<<16 and then masks the low bits off.
    const auto darkened = [](int c) {
        return static_cast<int>((static_cast<unsigned>(c) * 39936u) >> 16) & 0xFF;
    };

    s->darken_screen();
    for (const auto& [x, y] : {std::pair<int, int>{0, 0},
                              std::pair<int, int>{160, 100},
                              std::pair<int, int>{319, 199}})
    {
        s->get_pixel(x, y, &r, &g, &b);
        EXPECT_EQ(darkened(bright_r), static_cast<int>(r))
            << "red at (" << x << ", " << y << ')';
        EXPECT_EQ(darkened(bright_g), static_cast<int>(g))
            << "green at (" << x << ", " << y << ')';
        EXPECT_EQ(darkened(bright_b), static_cast<int>(b))
            << "blue at (" << x << ", " << y << ')';
    }

    // Every negative argument is an early return, not a clamped box.
    const std::size_t bytes =
        static_cast<std::size_t>(E_Screen->render->pitch) *
        static_cast<std::size_t>(E_Screen->render->h);
    const std::vector<Uint8> before(
        static_cast<const Uint8*>(E_Screen->render->pixels),
        static_cast<const Uint8*>(E_Screen->render->pixels) + bytes);
    s->fastbox(-1, 0, 10, 10, 1, 1);
    s->fastbox(0, -1, 10, 10, 1, 1);
    s->fastbox(0, 0, -10, 10, 1, 1);
    s->fastbox(0, 0, 10, -10, 1, 1);
    EXPECT_EQ(0, std::memcmp(before.data(), E_Screen->render->pixels, bytes))
        << "a fastbox with any negative argument must not touch the canvas";

    // Positive control: the same call with legal arguments does paint.
    s->fastbox(0, 0, 10, 10, 1, 1);
    EXPECT_NE(0, std::memcmp(before.data(), E_Screen->render->pixels, bytes))
        << "the guard above must be the sign check, not a dead fastbox";
    int index = -1;
    EXPECT_EQ(1, s->get_pixel(3, 3, &index)) << "the legal box painted colour 1";

    s->clearbuffer();
}


// get_pixel(x, y, &index) answers "which palette entry is this pixel?" for
// callers that read back what they drew. When the pixel is a colour the game
// palette does not contain — anything blended, alpha-composited or written
// through the RGB pointb overload — there is no honest answer, and the
// contract is that the caller's own variable is LEFT ALONE rather than being
// silently set to the black at index 0. A caller that seeded its variable with
// a sentinel must be able to tell "no match" from "matched entry 0".
TEST(VideoPixelOps, off_palette_get_pixel_reports_no_match_without_touching_index)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);

    // Find a 6-bit colour the palette does not hold. get_pixel compares
    // r/4, g/4, b/4 against query_palette_reg, so search that space.
    std::array<bool, 64 * 64 * 64> present{};
    for (int i = 0; i < 256; ++i)
    {
        int pr = 0;
        int pg = 0;
        int pb = 0;
        query_palette_reg(static_cast<unsigned char>(i), &pr, &pg, &pb);
        if (pr >= 0 && pr < 64 && pg >= 0 && pg < 64 && pb >= 0 && pb < 64)
            present[static_cast<std::size_t>((pr * 64 + pg) * 64 + pb)] = true;
    }
    int off_r = -1;
    int off_g = -1;
    int off_b = -1;
    for (std::size_t i = 0; i < present.size() && off_r < 0; ++i)
    {
        if (present[i])
            continue;
        off_b = static_cast<int>(i % 64);
        off_g = static_cast<int>((i / 64) % 64);
        off_r = static_cast<int>(i / (64 * 64));
    }
    ASSERT_GE(off_r, 0) << "the palette cannot hold all 262144 6-bit colours";

    // Control: a real palette entry is reported, and the out-param is set.
    scr->pointb(31, 29, static_cast<unsigned char>(47));
    int index = -7;
    EXPECT_EQ(47, scr->get_pixel(31, 29, &index));
    EXPECT_EQ(47, index);

    // The off-palette pixel: no match, and the sentinel survives.
    scr->pointb(32, 29, static_cast<unsigned char>(off_r * 4),
                static_cast<unsigned char>(off_g * 4),
                static_cast<unsigned char>(off_b * 4));
    index = -7;
    EXPECT_EQ(0, scr->get_pixel(32, 29, &index));
    EXPECT_EQ(-7, index)
        << "an unmatched colour must leave the caller's index untouched";
}
