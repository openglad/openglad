#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/interface/input.h>
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
            buf[static_cast<std::size_t>(i + 0)] = 0xFF;                       // A
            buf[static_cast<std::size_t>(i + 1)] = static_cast<unsigned char>(x * 40 + y * 5); // R
            buf[static_cast<std::size_t>(i + 2)] = static_cast<unsigned char>(y * 40 + x * 3); // G
            buf[static_cast<std::size_t>(i + 3)] = static_cast<unsigned char>(x * 20);         // B
        }
    }
}

static void run_sai2x_clipped_entry_points_honour_the_destination_origin();
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

// Constructing a Screen rewrites the session's WINDOW metrics from its own
// window and then recomputes the four derived VIEWPORT fields from them
// (Screen::Screen -> update_overscan_setting). Restoring the window alone
// leaves the viewport at the test Screen's size for every later suite, so
// this guard covers both halves.
class SessionWindowAndViewportRestore
{
public:
    SessionWindowAndViewportRestore()
        : width_(og::runtime::current_session->window_w_),
          height_(og::runtime::current_session->window_h_),
          viewport_w_(og::runtime::current_session->viewport_w_),
          viewport_h_(og::runtime::current_session->viewport_h_),
          viewport_offset_x_(og::runtime::current_session->viewport_offset_x_),
          viewport_offset_y_(og::runtime::current_session->viewport_offset_y_)
    {
    }

    ~SessionWindowAndViewportRestore()
    {
        og::runtime::current_session->window_w_ = width_;
        og::runtime::current_session->window_h_ = height_;
        og::runtime::current_session->viewport_w_ = viewport_w_;
        og::runtime::current_session->viewport_h_ = viewport_h_;
        og::runtime::current_session->viewport_offset_x_ = viewport_offset_x_;
        og::runtime::current_session->viewport_offset_y_ = viewport_offset_y_;
    }

private:
    float width_;
    float height_;
    float viewport_w_;
    float viewport_h_;
    float viewport_offset_x_;
    float viewport_offset_y_;
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

// A 4x4 white/black strip whose 8x8 upscale exercises every 2xSaI corner
// rule: plain copies, INTERPOLATE halves and (SuperEagle) Q_INTERPOLATE
// quarters. The golden outputs below were captured from the implementation
// and are recaptured ONLY on an intentional change to the scaler rules.
constexpr Uint32 kW = 0x00FFFFFFu;       // source white
constexpr Uint32 kB = 0x00000000u;       // source black
constexpr Uint32 kH = 0x007F7F7Fu;       // INTERPOLATE(white, black)
constexpr Uint32 kE7 = 0x00DFDFDFu;      // Q_INTERPOLATE, 3 white + 1 black
constexpr Uint32 kQ3 = 0x00BFBFBFu;      // INTERPOLATE(white, half)
constexpr Uint32 kE1 = 0x001F1F1Fu;      // Q_INTERPOLATE, 1 white + 3 black

const std::vector<Uint32>& wb_strip()
{
    static const std::vector<Uint32> strip{ kW, kW, kW, kW,
                                            kW, kW, kW, kW,
                                            kW, kB, kB, kB,
                                            kB, kW, kW, kW };
    return strip;
}

const std::vector<Uint32>& super2xsai_strip_golden()
{
    static const std::vector<Uint32> out{
        kW, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kH, kB, kB, kB, kB, kB,
        kW, kB, kB, kB, kB, kB, kB, kB,
        kB, kB, kW, kW, kW, kW, kW, kW,
        kB, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kW, kW, kW, kW, kW, kW,
    };
    return out;
}

const std::vector<Uint32>& clipped_strip_golden()
{
    static const std::vector<Uint32> out{
        kW, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kW, kW, kW, kW, kW, kW,
        kW, kW, kH, kB, kB, kB, kB, kB,
        kW, kB, kB, kB, kB, kB, kB, kB,
        kB, kB, kW, kW, kW, kW, kW, kW,
        kB, kH, kW, kW, kW, kW, kW, kW,
        kB, kH, kW, kW, kW, kW, kW, kW,
    };
    return out;
}

const std::vector<Uint32>& supereagle_strip_golden()
{
    static const std::vector<Uint32> out{
        kW,  kW,  kW,  kW,  kW,  kW,  kW,  kW,
        kW,  kW,  kW,  kW,  kW,  kW,  kW,  kW,
        kW,  kW,  kE7, kE7, kE7, kE7, kE7, kE7,
        kW,  kQ3, kE1, kE1, kE1, kE1, kE1, kE1,
        kH,  kB,  kE1, kE1, kE1, kE1, kE1, kE1,
        kB,  kH,  kE7, kE7, kE7, kE7, kE7, kE7,
        kE1, kE7, kW,  kW,  kW,  kW,  kW,  kW,
        kE1, kE7, kW,  kW,  kW,  kW,  kW,  kW,
    };
    return out;
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

// The three direct scaler entry points are a fixed integer pixel transform:
// each 4x4 neighbourhood picks a corner rule (plain copy / INTERPOLATE /
// Q_INTERPOLATE) and writes four exact destination pixels. Pin the whole
// 8x8 output of one hand-picked strip per scaler, plus a hash of the larger
// gradient pattern, so ANY change to the rules turns this red.
TEST(Sai2xScaler, sai2x_entry_points_write_the_pinned_2xsai_neighbourhood)
{
    ASSERT_EQ(0, Init_2xSaI());

    EXPECT_EQ(super2xsai_strip_golden(),
              scale_pixels(DirectScaler::Super2xSai, wb_strip(), 4, 4))
        << "Super2xSaI_ex must reproduce the pinned 2xSaI neighbourhood";
    EXPECT_EQ(clipped_strip_golden(),
              scale_pixels(DirectScaler::Super2xSaiClipped, wb_strip(), 4, 4))
        << "Super2xSaI_ex2 must reproduce the pinned 2xSaI neighbourhood";
    EXPECT_EQ(supereagle_strip_golden(),
              scale_pixels(DirectScaler::SuperEagle, wb_strip(), 4, 4))
        << "Scale_SuperEagle must reproduce the pinned SuperEagle "
           "neighbourhood";

    // The same three scalers over the 8x8 ARGB gradient, hashed. Golden;
    // recaptured only on an intentional scaler change.
    std::vector<unsigned char> bytes(8 * 8 * 4);
    fill_pattern(bytes, 8, 8);
    std::vector<Uint32> gradient(64);
    std::memcpy(gradient.data(), bytes.data(), bytes.size());
    EXPECT_EQ(0xB079AC32C1C35083ull,
              hash_pixels(scale_pixels(DirectScaler::Super2xSai, gradient, 8, 8)))
        << "Super2xSaI_ex gradient golden";
    EXPECT_EQ(0xC9EAB5AA273DD083ull,
              hash_pixels(
                  scale_pixels(DirectScaler::Super2xSaiClipped, gradient, 8, 8)))
        << "Super2xSaI_ex2 gradient golden";
    EXPECT_EQ(0x7DB2584DD4CF9A83ull,
              hash_pixels(scale_pixels(DirectScaler::SuperEagle, gradient, 8, 8)))
        << "Scale_SuperEagle gradient golden";

    run_sai2x_clipped_entry_points_honour_the_destination_origin();
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
    // Golden aggregate over all 8192 samples, one per scaler: FNV over the
    // Uint32 outputs of integer-only math, so it is portable. Recaptured
    // ONLY on an intentional change to the 2xSaI/SuperEagle corner rules —
    // without these the battery only compared each scaler against itself.
    constexpr std::array<Uint64, 3> golden_aggregates{
        0x0C880AD94088FA98ull, // Super2xSaI_ex
        0xEA9966C0BD553805ull, // Super2xSaI_ex2
        0x5AF99628E68D2C2Eull, // Scale_SuperEagle
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

    EXPECT_EQ(golden_aggregates[0], aggregate_hashes[0])
        << "Super2xSaI_ex changed its output over the 8192-sample battery";
    EXPECT_EQ(golden_aggregates[1], aggregate_hashes[1])
        << "Super2xSaI_ex2 changed its output over the 8192-sample battery";
    EXPECT_EQ(golden_aggregates[2], aggregate_hashes[2])
        << "Scale_SuperEagle changed its output over the 8192-sample battery";
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

// renderer_output_rect (sai2x.cpp:73): SDL window coordinates stay LOGICAL on
// a HiDPI display while the renderer backbuffer is measured in physical
// pixels, so the present rect is scaled by output_size / logical_size — and
// returned UNSCALED when the session's logical window metrics are
// unavailable (window_w_/h_ <= 0; without that guard the scale is inf and
// the frame never reaches the display).
//
// The software renderer presents into the window surface, so both branches
// are observable: paint the canvas red|blue down the middle and watch where
// the seam lands.
TEST(Sai2xScaler, present_rect_falls_back_unscaled_and_otherwise_scales_by_output)
{
    SessionWindowAndViewportRestore metrics_restore;
    Screen fullscreen(RenderEngine::NoZoom, 320, 200, 1);
    ASSERT_NE(nullptr, fullscreen.window);
    ASSERT_NE(nullptr, fullscreen.renderer);
    EXPECT_NE(0u, SDL_GetWindowFlags(fullscreen.window) &
                      SDL_WINDOW_FULLSCREEN)
        << "the fullscreen flag must reach the window";

    int output_w = 0;
    int output_h = 0;
    ASSERT_TRUE(SDL_GetRenderOutputSize(fullscreen.renderer, &output_w,
                                        &output_h));
    ASSERT_GT(output_w, 3);
    ASSERT_GT(output_h, 3);

    const int canvas_w = fullscreen.canvas_w();
    const SDL_Rect left{0, 0, canvas_w / 2, fullscreen.canvas_h()};
    const SDL_Rect right{canvas_w / 2, 0, canvas_w - canvas_w / 2,
                         fullscreen.canvas_h()};
    const Uint32 red = SDL_MapSurfaceRGB(fullscreen.render, 220, 20, 20);
    const Uint32 blue = SDL_MapSurfaceRGB(fullscreen.render, 20, 20, 220);
    ASSERT_TRUE(SDL_FillSurfaceRect(fullscreen.render, &left, red));
    ASSERT_TRUE(SDL_FillSurfaceRect(fullscreen.render, &right, blue));

    const std::vector<Uint8> before(
        static_cast<const Uint8*>(fullscreen.render->pixels),
        static_cast<const Uint8*>(fullscreen.render->pixels) +
            fullscreen.render->pitch * fullscreen.render->h);

    auto presented_rgb = [&](int x, int y) {
        SDL_Surface* const window_surface =
            SDL_GetWindowSurface(fullscreen.window);
        EXPECT_NE(nullptr, window_surface) << SDL_GetError();
        std::array<int, 3> rgb{-1, -1, -1};
        if (window_surface == nullptr)
            return rgb;
        const SDL_PixelFormatDetails* const details =
            SDL_GetPixelFormatDetails(window_surface->format);
        SDL_LockSurface(window_surface);
        const Uint32 value = *reinterpret_cast<const Uint32*>(
            static_cast<const Uint8*>(window_surface->pixels) +
            y * window_surface->pitch + x * 4);
        Uint8 r = 0, g = 0, b = 0;
        SDL_GetRGB(value, details, nullptr, &r, &g, &b);
        SDL_UnlockSurface(window_surface);
        return std::array<int, 3>{r, g, b};
    };
    const std::array<int, 3> expect_red{220, 20, 20};
    const std::array<int, 3> expect_blue{20, 20, 220};

    // Logical metrics unavailable: the rect is used UNSCALED, so the canvas
    // maps 1:1 onto the output and the seam sits at the middle.
    og::runtime::current_session->window_w_ = 0.0f;
    og::runtime::current_session->window_h_ = 0.0f;
    fullscreen.swap(0, 0, fullscreen.canvas_w(), fullscreen.canvas_h());
    EXPECT_EQ(expect_red, presented_rgb(output_w / 4, output_h / 2))
        << "unscaled present: the left half of the canvas is on the left";
    EXPECT_EQ(expect_blue, presented_rgb(output_w * 3 / 4, output_h / 2))
        << "unscaled present: the right half of the canvas is on the right";

    // Logical window half the renderer output (density 2): the same canvas
    // is presented at 2x, pushing the seam off the right edge.
    og::runtime::current_session->window_w_ = static_cast<float>(output_w) / 2.0f;
    og::runtime::current_session->window_h_ = static_cast<float>(output_h) / 2.0f;
    fullscreen.swap(0, 0, fullscreen.canvas_w(), fullscreen.canvas_h());
    EXPECT_EQ(expect_red, presented_rgb(output_w / 4, output_h / 2))
        << "2x present: the left quarter of the canvas covers the left half";
    EXPECT_EQ(expect_red, presented_rgb(output_w * 3 / 4, output_h / 2))
        << "2x present: the seam is pushed off the right edge";

    // Secondary: presentation is read-only on the CPU canvas either way.
    EXPECT_EQ(0, std::memcmp(
                     before.data(), fullscreen.render->pixels, before.size()))
        << "presentation must never write the CPU canvas";
}

TEST(Sai2xScaler, render_backend_failure_retries_without_losing_cpu_pixels)
{
    SessionWindowAndViewportRestore metrics_restore;
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
    SessionWindowAndViewportRestore metrics_restore;
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

// P5: the clipped scaler's left-neighbour sample. sub1 is the stride back to
// the previous source column; with it pinned to 0 at every x the four left
// samples collapse onto the centre column and two of the four 2xSaI corner
// rules become self-contradictory (measured: zero hits over 200k images).
TEST(Sai2xScaler, clipped_scaler_samples_the_previous_column_not_the_current_one)
{
    ASSERT_EQ(0, Init_2xSaI());
    // A 4x4 strip built so exactly one destination pixel depends on the
    // left-neighbour sample. At src(1,1): color5=(1,1)=W, color6=(2,1)=W,
    // color2=(1,2)=B, color3=(2,2)=B, and the LEFT column supplies
    // color4=(0,1)=W, color1=(0,2)=W, colorA0=(0,3)=B, so the 2xSaI
    // product2a rule "color5==color1 && color6==color5 && color4!=color2 &&
    // color5!=colorA0" fires and blends: INTERPOLATE(B, W) == 0x007F7F7F.
    constexpr Uint32 W = 0x00FFFFFFu;
    constexpr Uint32 B = 0x00000000u;
    const std::vector<Uint32> source{ W, W, W, W,
                                      W, W, W, W,
                                      W, B, B, B,
                                      B, W, W, W };
    const std::vector<Uint32> out =
        scale_pixels(DirectScaler::Super2xSaiClipped, source, 4, 4);
    EXPECT_EQ(0x007F7F7Fu, out[3 * 8 + 2]) // dst(2,3) == product2a of src(1,1)
        << "the left-neighbour sample must be the previous column, not the current one";
}

// T3b: a Screen constructed as an ordinary test object writes the process-wide
// session window metrics from its own window and recomputes the viewport from
// them (sai2x.cpp Screen::Screen -> update_overscan_setting). Every scaler test
// below builds a 320x200 Screen while the real window is 640x400; if the
// derived viewport is left behind at 320x200, the next suite's native world
// plane is sized at 1x instead of 2x.
TEST(Sai2xScaler, constructing_a_screen_leaves_the_session_viewport_untouched)
{
    // Rederive the viewport from the live window first. Another
    // Screen-constructing test may already have left it at its own 320x200,
    // and comparing that against itself would make this a tautology.
    update_overscan_setting();
    const float viewport_w = og::runtime::current_session->viewport_w_;
    const float viewport_h = og::runtime::current_session->viewport_h_;
    const float offset_x = og::runtime::current_session->viewport_offset_x_;
    const float offset_y = og::runtime::current_session->viewport_offset_y_;

    {
        SessionWindowAndViewportRestore metrics_restore;
        Screen value(RenderEngine::NoZoom, 320, 200, 0);
        ASSERT_NE(nullptr, value.window);
    }

    EXPECT_EQ(viewport_w, og::runtime::current_session->viewport_w_)
        << "a scaler Screen must not leave the session viewport at its own 320x200";
    EXPECT_EQ(viewport_h, og::runtime::current_session->viewport_h_);
    EXPECT_EQ(offset_x, og::runtime::current_session->viewport_offset_x_);
    EXPECT_EQ(offset_y, og::runtime::current_session->viewport_offset_y_);
}

// The clipped entry points take a destination ORIGIN. Scaling into an
// offset must reproduce the offset-0 output exactly, shifted, and must not
// disturb one byte outside the written block (swap() relies on this to
// double a sub-rect of the canvas in place).
static void run_sai2x_clipped_entry_points_honour_the_destination_origin()
{
    ASSERT_EQ(0, Init_2xSaI());

    constexpr int w = 8;
    constexpr int h = 8;
    constexpr int src_pitch = w * 4;
    constexpr int dst_w = 24;
    constexpr int dst_pitch = dst_w * 4;
    constexpr int dst_h = 24;
    constexpr int off_x = 4;
    constexpr int off_y = 6;
    constexpr Uint32 sentinel = 0xDEADBEEFu;

    std::vector<unsigned char> src(static_cast<std::size_t>(src_pitch * h));
    fill_pattern(src, w, h);

    std::vector<Uint32> reference(static_cast<std::size_t>(w * 2 * h * 2), 0u);
    std::vector<Uint32> offset(static_cast<std::size_t>(dst_w * dst_h),
                               sentinel);

    for (int scaler = 0; scaler < 2; ++scaler)
    {
        std::fill(reference.begin(), reference.end(), sentinel);
        std::fill(offset.begin(), offset.end(), sentinel);
        if (scaler == 0)
        {
            Super2xSaI_ex2(src.data(), 0, 0, w, h, src_pitch, h,
                           reinterpret_cast<unsigned char*>(reference.data()),
                           0, 0, w * 2 * 4);
            Super2xSaI_ex2(src.data(), 0, 0, w, h, src_pitch, h,
                           reinterpret_cast<unsigned char*>(offset.data()),
                           off_x, off_y, dst_pitch);
        }
        else
        {
            Scale_SuperEagle(src.data(), 0, 0, w, h, src_pitch, h,
                             reinterpret_cast<unsigned char*>(reference.data()),
                             0, 0, w * 2 * 4);
            Scale_SuperEagle(src.data(), 0, 0, w, h, src_pitch, h,
                             reinterpret_cast<unsigned char*>(offset.data()),
                             off_x, off_y, dst_pitch);
        }

        ASSERT_EQ(reference.end(),
                  std::find(reference.begin(), reference.end(), sentinel))
            << "scaler " << scaler << " left destination pixels unwritten";

        for (int y = 0; y < dst_h; ++y)
            for (int x = 0; x < dst_w; ++x)
            {
                const Uint32 got =
                    offset[static_cast<std::size_t>(y * dst_w + x)];
                const bool inside = x >= off_x && x < off_x + w * 2 &&
                                    y >= off_y && y < off_y + h * 2;
                const Uint32 want = inside
                    ? reference[static_cast<std::size_t>(
                          (y - off_y) * w * 2 + (x - off_x))]
                    : sentinel;
                ASSERT_EQ(want, got)
                    << "scaler " << scaler << " destination (" << x << ","
                    << y << ") "
                    << (inside ? "must match the offset-0 output"
                               : "lies outside the block and must be untouched");
            }
    }
}

static void run_sai2x_surface_wrapper_guards_and_scaling()
{
    ASSERT_EQ(0, Init_2xSaI());

    // Guard path: null surfaces return without touching anything.
    Super2xSaI(nullptr, nullptr, 0, 0, 0, 0, 8, 8);

    SurfacePtr src(SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888));
    SurfacePtr dst(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_ARGB8888));
    ASSERT_NE(nullptr, src) << "surfaces should allocate";
    ASSERT_NE(nullptr, dst) << "surfaces should allocate";

    std::vector<unsigned char> pattern(8 * 8 * 4);
    fill_pattern(pattern, 8, 8);
    std::memcpy(src->pixels, pattern.data(), pattern.size());
    ASSERT_TRUE(SDL_FillSurfaceRect(dst.get(), nullptr, 0xDEADBEEFu));
    const std::vector<Uint8> untouched(
        static_cast<const Uint8*>(dst->pixels),
        static_cast<const Uint8*>(dst->pixels) + dst->pitch * dst->h);

    // Guard path: an image narrower/shorter than 4 is too small to 2xSaI, so
    // the destination must come back byte-identical.
    Super2xSaI(src.get(), dst.get(), 0, 0, 0, 0, 3, 3);
    EXPECT_EQ(0, std::memcmp(untouched.data(), dst->pixels, untouched.size()))
        << "a source smaller than 4x4 must leave the destination alone";

    // Happy path: the wrapper is exactly Super2xSaI_ex over the surface
    // pixels.
    std::vector<unsigned char> expected(
        static_cast<std::size_t>(dst->pitch * dst->h), 0xA5u);
    Super2xSaI_ex(reinterpret_cast<unsigned char*>(src->pixels),
                  static_cast<Uint32>(src->pitch), nullptr, expected.data(),
                  static_cast<Uint32>(dst->pitch), 8, 8);
    Super2xSaI(src.get(), dst.get(), 0, 0, 0, 0, 8, 8);
    EXPECT_EQ(0, std::memcmp(expected.data(), dst->pixels, expected.size()))
        << "the surface wrapper must scale exactly like Super2xSaI_ex";
    EXPECT_EQ(0, std::memcmp(pattern.data(), src->pixels, pattern.size()))
        << "the surface wrapper must not modify its source";
}

namespace
{
Uint32 render_pixel(const Screen& s, int x, int y)
{
    return static_cast<const Uint32*>(s.render->pixels)[
        x + y * s.render->pitch / static_cast<int>(sizeof(Uint32))];
}
} // namespace

static void run_sai2x_screen_class_paths()
{
    SessionWindowAndViewportRestore metrics_restore;
    constexpr Uint32 sentinel = 0x00335577u;
    {
        Screen s(RenderEngine::NoZoom, 320, 200, 0);
        ASSERT_TRUE(SDL_FillSurfaceRect(s.render, nullptr, sentinel));
        s.clear();
        EXPECT_EQ(0u, render_pixel(s, 0, 0))
            << "clear() must fill the whole render surface with background";
        EXPECT_EQ(0u, render_pixel(s, 160, 100));
        EXPECT_EQ(0u, render_pixel(s, s.canvas_w() - 1, s.canvas_h() - 1));

        ASSERT_TRUE(SDL_FillSurfaceRect(s.render, nullptr, sentinel));
        s.clear(10, 10, 20, 20);
        EXPECT_EQ(0u, render_pixel(s, 15, 15))
            << "clear(rect) must clear inside the rect";
        EXPECT_EQ(0u, render_pixel(s, 29, 29)) << "rect is [10,30)";
        EXPECT_EQ(sentinel, render_pixel(s, 30, 30))
            << "clear(rect) must leave everything outside the rect alone";
        EXPECT_EQ(sentinel, render_pixel(s, 9, 9));

        s.swap(0, 0, 40, 40);
        ASSERT_TRUE(SDL_FillSurfaceRect(s.render, nullptr, sentinel));
        s.clear_window();
        EXPECT_EQ(0u, render_pixel(s, 5, 5))
            << "clear_window() blanks the CPU canvas before presenting it";
    }

    {
        Screen s(RenderEngine::SAI, 320, 200, 0);
        s.clear();
        s.swap(0, 0, 40, 40);
        EXPECT_EQ(0u, render_pixel(s, 20, 20))
            << "presenting through the smart scaler must not alter the canvas";
    }

    {
        Screen s(RenderEngine::Eagle, 320, 200, 0);
        ASSERT_TRUE(SDL_FillSurfaceRect(s.render, nullptr, sentinel));
        s.swap(0, 0, 40, 40);
        EXPECT_EQ(sentinel, render_pixel(s, 20, 20))
            << "presenting through Eagle must not alter the canvas";

        const std::string path = std::filesystem::temp_directory_path() / "openglad_sai2x_test.bmp";
        char bmp_path[512] = {};
        std::snprintf(bmp_path, sizeof(bmp_path), "%s", path.c_str());
        s.SaveBMP(s.render, bmp_path);
        ASSERT_TRUE(std::filesystem::exists(path)) << "SaveBMP should create output file";
        SurfacePtr saved(SDL_LoadBMP(path.c_str()));
        ASSERT_NE(nullptr, saved) << "SaveBMP must write a readable BMP";
        EXPECT_EQ(s.render->w, saved->w) << "SaveBMP writes the render canvas";
        EXPECT_EQ(s.render->h, saved->h);
        std::filesystem::remove(path);
    }
}
