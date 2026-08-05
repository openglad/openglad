#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// sai2x.cpp does not expose these functions via a header.
extern int Init_2xSaI();
extern void Super2xSaI_ex2(unsigned char* src, int srcx, int srcy, int srcw, int srch,
                           int src_pitch, int src_height, unsigned char* dst,
                           int dstx, int dsty, int dst_pitch);
extern void Scale_SuperEagle(unsigned char* src, int srcx, int srcy, int srcw, int srch,
                             int src_pitch, int src_height, unsigned char* dst,
                             int dstx, int dsty, int dst_pitch);
extern void Super2xSaI_ex(unsigned char* src, Uint32 src_pitch, unsigned char* unused,
                          unsigned char* dest, Uint32 dest_pitch, Uint32 width, Uint32 height);
extern void Super2xSaI(SDL_Surface* src, SDL_Surface* dest, int s_x, int s_y, int d_x, int d_y, int w, int h);

static void fill_pattern(std::vector<unsigned char>& buf, int w, int h)
{
    // ARGB8888 pixels, 4 bytes per pixel.
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const int i = (y * w + x) * 4;
            buf[i + 0] = 0xFF;                       // A
            buf[i + 1] = static_cast<unsigned char>(x * 40 + y * 5); // R
            buf[i + 2] = static_cast<unsigned char>(y * 40 + x * 3); // G
            buf[i + 3] = static_cast<unsigned char>(x * 20);         // B
        }
    }
}

static void run_sai2x_ex2_and_supereagle_write_output();
static void run_sai2x_surface_wrapper_guards_and_scaling();
static void run_sai2x_screen_class_paths();

namespace
{
struct SurfaceDeleter
{
    void operator()(SDL_Surface* surface) const
    {
        if (surface != nullptr)
            SDL_DestroySurface(surface);
    }
};

using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

class SessionWindowMetricsRestore
{
public:
    SessionWindowMetricsRestore()
        : width_(og::runtime::current_session->window_w_),
          height_(og::runtime::current_session->window_h_)
    {
    }

    ~SessionWindowMetricsRestore()
    {
        og::runtime::current_session->window_w_ = width_;
        og::runtime::current_session->window_h_ = height_;
    }

private:
    float width_;
    float height_;
};

enum class DirectScaler
{
    Super2xSai,
    Super2xSaiClipped,
    SuperEagle,
};

std::vector<Uint32> scale_pixels(DirectScaler scaler,
                                 const std::vector<Uint32>& source,
                                 int width,
                                 int height)
{
    std::vector<Uint32> mutable_source = source;
    std::vector<Uint32> destination(
        static_cast<size_t>(width * 2 * height * 2), 0xDEADBEEFu);
    const int source_pitch = width * static_cast<int>(sizeof(Uint32));
    const int destination_pitch = width * 2 * static_cast<int>(sizeof(Uint32));

    switch (scaler)
    {
    case DirectScaler::Super2xSai:
        Super2xSaI_ex(reinterpret_cast<unsigned char*>(mutable_source.data()),
                     static_cast<Uint32>(source_pitch), nullptr,
                     reinterpret_cast<unsigned char*>(destination.data()),
                     static_cast<Uint32>(destination_pitch),
                     static_cast<Uint32>(width), static_cast<Uint32>(height));
        break;
    case DirectScaler::Super2xSaiClipped:
        Super2xSaI_ex2(reinterpret_cast<unsigned char*>(mutable_source.data()),
                      0, 0, width, height, source_pitch, height,
                      reinterpret_cast<unsigned char*>(destination.data()),
                      0, 0, destination_pitch);
        break;
    case DirectScaler::SuperEagle:
        Scale_SuperEagle(reinterpret_cast<unsigned char*>(mutable_source.data()),
                         0, 0, width, height, source_pitch, height,
                         reinterpret_cast<unsigned char*>(destination.data()),
                         0, 0, destination_pitch);
        break;
    }

    EXPECT_EQ(source, mutable_source) << "scalers must not modify source pixels";
    return destination;
}

Uint64 hash_pixels(const std::vector<Uint32>& pixels)
{
    Uint64 hash = 1469598103934665603ull;
    for (Uint32 pixel : pixels)
    {
        hash ^= pixel;
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace

TEST(Sai2xScaler, sai2x_super2xsai_ex_runs_on_small_buffer)
{
    // Initialize masks for 32bpp.
    Init_2xSaI();

    const int w = 8;
    const int h = 8;
    const int src_pitch = w * 4;
    const int dw = w * 2;
    const int dh = h * 2;
    const int dst_pitch = dw * 4;

    std::vector<unsigned char> src(src_pitch * h);
    std::vector<unsigned char> dst(dst_pitch * dh);
    fill_pattern(src, w, h);

    // Smoke the legacy entry point here; the pattern and wrapper-equivalence
    // tests below pin deterministic pixel behavior in detail.
    Super2xSaI_ex(src.data(), src_pitch, nullptr, dst.data(), dst_pitch, w, h);

    // Basic sanity: output buffer is non-zero somewhere.
    bool any = false;
    for (unsigned char b : dst)
    {
        if (b != 0)
        {
            any = true;
            break;
        }
    }
    ASSERT_TRUE(any) << "Super2xSaI_ex should write to the output buffer";

    run_sai2x_ex2_and_supereagle_write_output();
    run_sai2x_surface_wrapper_guards_and_scaling();
    run_sai2x_screen_class_paths();
}

TEST(Sai2xScaler, deterministic_pattern_battery_preserves_pixels_and_exercises_edges)
{
    ASSERT_EQ(0, Init_2xSaI());

    constexpr int width = 8;
    constexpr int height = 8;
    constexpr size_t sample_count = 8192;
    constexpr std::array<Uint32, 4> test_colors{
        0x00112233u, 0x00446688u, 0x00995511u, 0x00DDAA77u,
    };
    constexpr std::array<DirectScaler, 3> scalers{
        DirectScaler::Super2xSai,
        DirectScaler::Super2xSaiClipped,
        DirectScaler::SuperEagle,
    };

    // A flat image is an exact fixed point for every interpolation mode.
    const std::vector<Uint32> flat_source(width * height, test_colors[2]);
    const std::vector<Uint32> flat_expected(width * height * 4, test_colors[2]);
    for (DirectScaler scaler : scalers)
        EXPECT_EQ(flat_expected, scale_pixels(scaler, flat_source, width, height));

    std::array<Uint64, scalers.size()> aggregate_hashes{};
    for (size_t sample = 0; sample < sample_count; ++sample)
    {
        std::vector<Uint32> source(width * height);
        Uint32 state = static_cast<Uint32>(sample + 1) * 0x9E3779B9u;
        const Uint32 palette_mask = sample < sample_count / 2 ? 1u : 3u;
        for (size_t pixel = 0; pixel < source.size(); ++pixel)
        {
            // Fixed xorshift input makes this exhaustive regression battery
            // stable while supplying both equality-heavy two-color edges and
            // four-color corners to the scaler's neighborhood decisions.
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            source[pixel] = test_colors[(state >> 16) & palette_mask];
        }

        for (size_t scaler_index = 0; scaler_index < scalers.size(); ++scaler_index)
        {
            const std::vector<Uint32> first =
                scale_pixels(scalers[scaler_index], source, width, height);
            const std::vector<Uint32> second =
                scale_pixels(scalers[scaler_index], source, width, height);
            ASSERT_EQ(first, second)
                << "scaler output changed between identical calls at sample " << sample;
            ASSERT_EQ(first.end(), std::find(first.begin(), first.end(), 0xDEADBEEFu))
                << "scaler left destination pixels unwritten at sample " << sample;
            aggregate_hashes[scaler_index] ^=
                hash_pixels(first) + static_cast<Uint64>(sample) * 0x9E3779B97F4A7C15ull;
        }
    }

    EXPECT_NE(aggregate_hashes[0], aggregate_hashes[1]);
    EXPECT_NE(aggregate_hashes[0], aggregate_hashes[2]);
    EXPECT_NE(aggregate_hashes[1], aggregate_hashes[2]);
}

TEST(Sai2xScaler, surface_wrapper_matches_direct_scaler_output)
{
    ASSERT_EQ(0, Init_2xSaI());
    constexpr int width = 8;
    constexpr int height = 8;

    SurfacePtr source(
        SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888));
    SurfacePtr destination(
        SDL_CreateSurface(width * 2, height * 2, SDL_PIXELFORMAT_ARGB8888));
    ASSERT_NE(nullptr, source);
    ASSERT_NE(nullptr, destination);

    std::vector<unsigned char> pattern(width * height * sizeof(Uint32));
    fill_pattern(pattern, width, height);
    std::memcpy(source->pixels, pattern.data(), pattern.size());
    SDL_FillSurfaceRect(destination.get(), nullptr, 0xDEADBEEFu);

    std::vector<unsigned char> expected(
        static_cast<size_t>(destination->pitch * destination->h), 0xA5u);
    std::vector<unsigned char> direct_source(
        static_cast<size_t>(source->pitch * source->h));
    std::memcpy(direct_source.data(), source->pixels, direct_source.size());
    Super2xSaI_ex(direct_source.data(), static_cast<Uint32>(source->pitch), nullptr,
                  expected.data(), static_cast<Uint32>(destination->pitch),
                  width, height);

    Super2xSaI(source.get(), destination.get(), 0, 0, 0, 0, width, height);
    EXPECT_EQ(0, std::memcmp(expected.data(), destination->pixels, expected.size()));
    EXPECT_EQ(0, std::memcmp(pattern.data(), source->pixels, pattern.size()));
}

TEST(Sai2xScaler, surface_wrapper_rejects_mismatched_pixel_depths)
{
    ASSERT_EQ(0, Init_2xSaI());
    SurfacePtr source(
        SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGB24));
    SurfacePtr destination(
        SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888));
    ASSERT_NE(nullptr, source);
    ASSERT_NE(nullptr, destination);
    ASSERT_TRUE(
        SDL_FillSurfaceRect(destination.get(), nullptr, 0x0055AA33u));

    const std::vector<Uint8> before(
        static_cast<const Uint8*>(destination->pixels),
        static_cast<const Uint8*>(destination->pixels) +
            destination->pitch * destination->h);
    Super2xSaI(source.get(), destination.get(), 0, 0, 0, 0, 8, 8);
    EXPECT_EQ(0, std::memcmp(
                     before.data(), destination->pixels, before.size()))
        << "a depth mismatch must not modify destination storage";
}

TEST(Sai2xScaler, screen_fullscreen_and_output_fallback_preserve_pixels)
{
    SessionWindowMetricsRestore metrics_restore;
    Screen fullscreen(RenderEngine::NoZoom, 320, 200, 1);
    ASSERT_NE(nullptr, fullscreen.window);
    EXPECT_NE(0u, SDL_GetWindowFlags(fullscreen.window) &
                      SDL_WINDOW_FULLSCREEN);
    ASSERT_TRUE(SDL_FillSurfaceRect(
        fullscreen.render, nullptr,
        SDL_MapSurfaceRGB(fullscreen.render, 15, 35, 75)));

    const std::vector<Uint8> before(
        static_cast<const Uint8*>(fullscreen.render->pixels),
        static_cast<const Uint8*>(fullscreen.render->pixels) +
            fullscreen.render->pitch * fullscreen.render->h);
    og::runtime::current_session->window_w_ = 0.0f;
    og::runtime::current_session->window_h_ = 0.0f;
    fullscreen.swap(0, 0, fullscreen.canvas_w(), fullscreen.canvas_h());
    EXPECT_EQ(0, std::memcmp(
                     before.data(), fullscreen.render->pixels, before.size()))
        << "presentation with unavailable logical metrics is read-only";
}

TEST(Sai2xScaler, render_backend_failure_retries_without_losing_cpu_pixels)
{
    SessionWindowMetricsRestore metrics_restore;
    Screen value(RenderEngine::NoZoom, 320, 200, 0);
    ASSERT_NE(nullptr, value.window);
    ASSERT_NE(nullptr, value.renderer);
    ASSERT_TRUE(SDL_FillSurfaceRect(
        value.render, nullptr,
        SDL_MapSurfaceRGB(value.render, 27, 61, 143)));
    const Uint32 pixel_before =
        static_cast<const Uint32*>(value.render->pixels)[17 +
            19 * value.render->pitch / static_cast<int>(sizeof(Uint32))];

    SDL_Window* const window = value.window;
    {
        struct WindowRestore
        {
            Screen& value;
            SDL_Window* window;
            ~WindowRestore()
            {
                if (value.window == nullptr)
                    value.window = window;
            }
        } restore{value, window};
        value.window = nullptr;
        request_render_backend_recreate();
        ASSERT_TRUE(render_backend_recreate_pending());
        value.swap(0, 0, value.canvas_w(), value.canvas_h());
        EXPECT_TRUE(render_backend_recreate_pending());
        EXPECT_EQ(nullptr, value.renderer);
        EXPECT_EQ(pixel_before,
                  static_cast<const Uint32*>(value.render->pixels)[17 +
                      19 * value.render->pitch /
                          static_cast<int>(sizeof(Uint32))]);
    }
    ASSERT_EQ(window, value.window);
    value.swap(0, 0, value.canvas_w(), value.canvas_h());
    EXPECT_FALSE(render_backend_recreate_pending());
    EXPECT_NE(nullptr, value.renderer);
    EXPECT_NE(nullptr, value.render_tex);
    EXPECT_EQ(pixel_before,
              static_cast<const Uint32*>(value.render->pixels)[17 +
                  19 * value.render->pitch /
                      static_cast<int>(sizeof(Uint32))])
        << "renderer recovery must preserve the CPU canvas";
}

TEST(Sai2xScaler, smart_scaler_allocation_and_invalid_source_fail_closed)
{
    SessionWindowMetricsRestore metrics_restore;
    Screen value(RenderEngine::SAI, 320, 200, 0);
    ASSERT_NE(nullptr, value.renderer);
    ASSERT_EQ(nullptr, value.render2);
    ASSERT_EQ(nullptr, value.render2_tex);

    SDL_Renderer* const renderer = value.renderer;
    {
        struct RendererRestore
        {
            Screen& value;
            SDL_Renderer* renderer;
            ~RendererRestore()
            {
                if (value.renderer == nullptr)
                    value.renderer = renderer;
            }
        } restore{value, renderer};
        value.renderer = nullptr;
        value.swap(0, 0, value.canvas_w(), value.canvas_h());
        EXPECT_EQ(nullptr, value.render2);
        EXPECT_EQ(nullptr, value.render2_tex);
        EXPECT_FALSE(value.last_world_present_used_smart_surface());
    }
    ASSERT_EQ(renderer, value.renderer);

    value.Engine = RenderEngine::Eagle;
    value.swap(0, 0, value.canvas_w(), value.canvas_h());
    EXPECT_EQ(nullptr, value.render2)
        << "the failed-size latch must reject the same scratch dimensions";
    EXPECT_FALSE(value.last_world_present_used_smart_surface());

    value.Engine = RenderEngine::SAI;
    {
        struct WidthRestore
        {
            SDL_Surface* surface;
            int width;
            ~WidthRestore() { surface->w = width; }
        } restore{value.render, value.render->w};
        value.render->w = 0;
        value.swap(0, 0, value.canvas_w(), value.canvas_h());
    }
    EXPECT_EQ(nullptr, value.render2);
    EXPECT_FALSE(value.last_world_present_used_smart_surface());
    EXPECT_NE(nullptr, value.renderer);
}


static void run_sai2x_ex2_and_supereagle_write_output()
{
    Init_2xSaI();

    const int w = 8;
    const int h = 8;
    const int src_pitch = w * 4;
    const int dw = w * 2;
    const int dh = h * 2;
    const int dst_pitch = dw * 4;

    std::vector<unsigned char> src(src_pitch * h);
    std::vector<unsigned char> dst_sai(dst_pitch * dh);
    std::vector<unsigned char> dst_eagle(dst_pitch * dh);
    fill_pattern(src, w, h);

    Super2xSaI_ex2(src.data(), 0, 0, w, h, src_pitch, h, dst_sai.data(), 0, 0, dst_pitch);
    Scale_SuperEagle(src.data(), 0, 0, w, h, src_pitch, h, dst_eagle.data(), 0, 0, dst_pitch);

    bool any_sai = false;
    for (unsigned char b : dst_sai) {
        if (b != 0) {
            any_sai = true;
            break;
        }
    }
    bool any_eagle = false;
    for (unsigned char b : dst_eagle) {
        if (b != 0) {
            any_eagle = true;
            break;
        }
    }

    ASSERT_TRUE(any_sai) << "Super2xSaI_ex2 should write to destination";
    ASSERT_TRUE(any_eagle) << "Scale_SuperEagle should write to destination";
}

static void run_sai2x_surface_wrapper_guards_and_scaling()
{
    Init_2xSaI();

    // Guard path: null surfaces should return without crashing.
    Super2xSaI(nullptr, nullptr, 0, 0, 0, 0, 8, 8);

    SDL_Surface* src = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface* dst = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888);
    ASSERT_TRUE(src != nullptr && dst != nullptr) << "surfaces should allocate";
    if (!src || !dst)
        return;

    std::vector<unsigned char> pattern(8 * 8 * 4);
    fill_pattern(pattern, 8, 8);
    std::memcpy(src->pixels, pattern.data(), pattern.size());
    SDL_FillSurfaceRect(dst, nullptr, 0x00000000u);

    // Guard path: tiny source should not run scaler.
    Super2xSaI(src, dst, 0, 0, 0, 0, 3, 3);

    // Happy path.
    Super2xSaI(src, dst, 0, 0, 0, 0, 8, 8);

    bool any = false;
    const unsigned char* out = reinterpret_cast<const unsigned char*>(dst->pixels);
    for (int i = 0; i < dst->pitch * dst->h; ++i) {
        if (out[i] != 0) {
            any = true;
            break;
        }
    }
    ASSERT_TRUE(any) << "Super2xSaI surface wrapper should write output on valid input";

    SDL_DestroySurface(src);
    SDL_DestroySurface(dst);
}

static void run_sai2x_screen_class_paths()
{
    SessionWindowMetricsRestore metrics_restore;
    {
        Screen s(RenderEngine::NoZoom, 320, 200, 0);
        s.clear();
        s.clear(10, 10, 20, 20);
        s.swap(0, 0, 40, 40);
        s.clear_window();
    }

    {
        Screen s(RenderEngine::SAI, 320, 200, 0);
        s.clear();
        s.swap(0, 0, 40, 40);
    }

    {
        Screen s(RenderEngine::Eagle, 320, 200, 0);
        s.clear();
        s.swap(0, 0, 40, 40);

        const std::string path = std::filesystem::temp_directory_path() / "openglad_sai2x_test.bmp";
        char bmp_path[512] = {};
        std::snprintf(bmp_path, sizeof(bmp_path), "%s", path.c_str());
        s.SaveBMP(s.render, bmp_path);
        ASSERT_TRUE(std::filesystem::exists(path)) << "SaveBMP should create output file";
        std::filesystem::remove(path);
    }
}
