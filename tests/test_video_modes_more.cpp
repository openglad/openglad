#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/io_common.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>


// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct SurfaceDeleter
{
    void operator()(SDL_Surface* s) const
    {
        if (s)
            SDL_DestroySurface(s);
    }
};
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

static SurfacePtr make_surface(int w, int h)
{
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    return SurfacePtr(s);
}

static void cleanup_screenshots()
{
    // save_screenshot() uses a static counter and writes through PhysFS into
    // the configured user directory.
    // Keep tests idempotent and avoid accumulating files across runs.
    std::error_code ec;
    for (const auto& p : std::filesystem::directory_iterator(get_user_path(), ec))
    {
        if (ec)
            break;
        const auto name = p.path().filename().string();
        if (name.rfind("screenshot", 0) == 0 && (p.path().extension() == ".png" || p.path().extension() == ".bmp"))
            std::filesystem::remove(p.path(), ec);
    }
}

static std::vector<std::filesystem::path> screenshot_files()
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& p : std::filesystem::directory_iterator(get_user_path(), ec))
    {
        if (ec)
            break;
        const auto name = p.path().filename().string();
        if (name.rfind("screenshot", 0) == 0 &&
            (p.path().extension() == ".png" || p.path().extension() == ".bmp"))
            out.push_back(p.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

static std::pair<int, int> saved_image_dimensions(const std::filesystem::path& path)
{
    std::array<unsigned char, 26> bytes{};
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()))
        return {0, 0};

    const auto be32 = [&bytes](std::size_t offset) {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
               static_cast<std::uint32_t>(bytes[offset + 3]);
    };
    const auto le32 = [&bytes](std::size_t offset) {
        return static_cast<std::uint32_t>(bytes[offset]) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    };

    // PNG IHDR stores width/height big-endian at byte offsets 16/20.
    if (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
        return {static_cast<int>(be32(16)), static_cast<int>(be32(20))};
    // SDL's BMP writer emits a BITMAPINFOHEADER with little-endian dimensions.
    if (bytes[0] == 'B' && bytes[1] == 'M')
        return {static_cast<int>(le32(18)), static_cast<int>(le32(22))};
    return {0, 0};
}

struct ScreenshotStateRestore
{
    ~ScreenshotStateRestore()
    {
        if (E_Screen)
        {
            E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
            E_Screen->set_active_canvas(CanvasTarget::UI);
        }
        cleanup_screenshots();
    }
};
} // namespace

TEST(VideoModesMore, display_mode_sizes_are_reported_in_physical_pixels)
{
	SDL_DisplayMode mode{};
	mode.w = 1920;
	mode.h = 1080;
	mode.pixel_density = 2.0f;
	EXPECT_EQ(std::make_pair(3840, 2160),
	          og::platform::display_mode_pixel_size(mode));

	// SDL rounds drawable pixel sizes upward for fractional densities.
	mode.w = 1365;
	mode.h = 767;
	mode.pixel_density = 1.25f;
	EXPECT_EQ(std::make_pair(1707, 959),
	          og::platform::display_mode_pixel_size(mode));

	// Invalid/unspecified density safely retains the logical dimensions.
	mode.pixel_density = 0.0f;
	EXPECT_EQ(std::make_pair(1365, 767),
	          og::platform::display_mode_pixel_size(mode));
}

TEST(VideoModesMore, native_window_requests_a_physical_hidpi_backbuffer)
{
	ASSERT_NE(nullptr, E_Screen);
	ASSERT_NE(nullptr, E_Screen->window);
	EXPECT_NE(0u, SDL_GetWindowFlags(E_Screen->window) &
	                  SDL_WINDOW_HIGH_PIXEL_DENSITY);

	int logical_w = 0;
	int logical_h = 0;
	int output_w = 0;
	int output_h = 0;
	ASSERT_TRUE(SDL_GetWindowSize(E_Screen->window, &logical_w, &logical_h));
	ASSERT_TRUE(SDL_GetRenderOutputSize(E_Screen->renderer, &output_w, &output_h));
	EXPECT_GE(output_w, logical_w);
	EXPECT_GE(output_h, logical_h);
}

TEST(VideoModesMore, video_putbuffer_surface_clipping_and_blit)
{
    SurfacePtr surf = make_surface(32, 32);
    ASSERT_TRUE(surf != nullptr) << "SDL surface created";
    if (!surf)
        return;

    // Fill with a non-zero pattern so the blit does something.
    SDL_FillSurfaceRect(surf.get(), nullptr, SDL_MapSurfaceRGB(surf.get(), 10, 20, 30));

    // Early-out: tile outside clipping region.
    og::runtime::current_session->myscreen_->putbuffer_surface(500, 500, 16, 16, 0, 0, 319, 199, surf.get());

    // Clip left/top.
    og::runtime::current_session->myscreen_->putbuffer_surface(-5, -5, 16, 16, 0, 0, 319, 199, surf.get());

    // Clip right/bottom.
    og::runtime::current_session->myscreen_->putbuffer_surface(310, 190, 32, 32, 0, 0, 319, 199, surf.get());

    // No clipping.
    og::runtime::current_session->myscreen_->putbuffer_surface(10, 10, 16, 16, 0, 0, 319, 199, surf.get());
}

TEST(VideoModesMore, video_clipping_and_accel_surface_edge_paths)
{
    std::array<unsigned char, 16 * 16> pixels{};
    pixels.fill(42);
    pixels[0] = 0;
    auto span = std::span<const unsigned char>(pixels.data(), pixels.size());

    og::runtime::current_session->myscreen_->fastbox(10, 10, 4, 4, 12, 0);

    og::runtime::current_session->myscreen_->putbuffer(-4, -4, 16, 16, 0, 0, 319, 199, span);
    og::runtime::current_session->myscreen_->putbuffer(400, 400, 16, 16, 0, 0, 319, 199, span);
    og::runtime::current_session->myscreen_->putbuffer_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 128);
    og::runtime::current_session->myscreen_->putbuffer_alpha(400, 400, 16, 16, 0, 0, 319, 199, span, 128);
    og::runtime::current_session->myscreen_->putbuffer_alpha(10, 10, 0, 16, 0, 0, 319, 199, span, 128);

    EXPECT_EQ(nullptr, og::runtime::current_session->myscreen_->create_accel_surface(span, 0, 16));
    EXPECT_EQ(nullptr, og::runtime::current_session->myscreen_->create_accel_surface(span.first(3), 4, 4));
    void* surface = og::runtime::current_session->myscreen_->create_accel_surface(span, 16, 16);
    ASSERT_NE(nullptr, surface);
    og::runtime::current_session->myscreen_->destroy_accel_surface(surface);
    og::runtime::current_session->myscreen_->destroy_accel_surface(nullptr);

    og::runtime::current_session->myscreen_->walkputbuffer_flash(400, 400, 16, 16, 0, 0, 319, 199, span, 40);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(-4, -4, 16, 16, 0, 0, 319, 199, span, 40);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(310, 190, 16, 16, 0, 0, 319, 199, span, 40);
    og::runtime::current_session->myscreen_->walkputbuffer_flash(10, 10, 0, 16, 0, 0, 319, 199, span, 40);

    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(400, 400, 16, 16, 0, 0, 319, 199, span, 40, 128);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 40, 128);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(310, 190, 16, 16, 0, 0, 319, 199, span, 40, 128);
    og::runtime::current_session->myscreen_->walkputbuffertext_alpha(10, 10, 0, 16, 0, 0, 319, 199, span, 40, 128);
}


TEST(VideoModesMore, video_walkputbuffer_modes_invisible_outline_phantom)
{
    // A small sprite with edges and a transparent interior to exercise outline
    // and transparency checks. Use some >247 indices to hit team color remap.
    std::array<unsigned char, 8 * 8> sprite{};
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            const bool edge = (x == 0 || y == 0 || x == 7 || y == 7);
            sprite[y * 8 + x] = edge ? static_cast<unsigned char>(250) : static_cast<unsigned char>(0);
        }
    }
    auto span = std::span<const unsigned char>(sprite.data(), sprite.size());

    // Ensure get_pixel() has something to read (phantom modes sample from the screen).
    og::runtime::current_session->myscreen_->clearbuffer();
    og::runtime::current_session->myscreen_->putdata(0, 0, 8, 8, span);
    og::runtime::current_session->myscreen_->swap();

    IRandom* old_rng = ctx().rng;

    // INVISIBLE_MODE: cover both the "skip draw" and "draw" paths deterministically.
    FixedRandom rng0(0);
    ctx().rng = &rng0; // rng(1) -> 0
    og::runtime::current_session->myscreen_->walkputbuffer(50, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(INVISIBLE_MODE), /*invisibility*/ 1,
                            /*outline*/ 7, /*shifttype*/ 0);

    FixedRandom rng9(9);
    ctx().rng = &rng9; // rng(10) -> 9 (> 8)
    og::runtime::current_session->myscreen_->walkputbuffer(60, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(INVISIBLE_MODE), /*invisibility*/ 10,
                            /*outline*/ 7, /*shifttype*/ 0);

    // OUTLINE_MODE: border-only drawing.
    ctx().rng = &rng0;
    og::runtime::current_session->myscreen_->walkputbuffer(70, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(OUTLINE_MODE), /*invisibility*/ 0,
                            /*outline*/ 7, /*shifttype*/ 0);

    // PHANTOM_MODE: exercise shift type branches.
    og::runtime::current_session->myscreen_->walkputbuffer(80, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_LEFT));
    og::runtime::current_session->myscreen_->walkputbuffer(90, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_RIGHT));
    og::runtime::current_session->myscreen_->walkputbuffer(100, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_RIGHT_RANDOM));
    og::runtime::current_session->myscreen_->walkputbuffer(110, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_RANDOM));
    og::runtime::current_session->myscreen_->walkputbuffer(120, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_LIGHTER));
    og::runtime::current_session->myscreen_->walkputbuffer(130, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_DARKER));
    og::runtime::current_session->myscreen_->walkputbuffer(140, 50, 8, 8, 0, 0, 319, 199, span, 40,
                            static_cast<unsigned char>(PHANTOM_MODE), /*invisibility*/ 0,
                            /*outline*/ 0, static_cast<unsigned char>(SHIFT_BLOCKY));

    ctx().rng = old_rng;
}


TEST(VideoModesMore, video_save_screenshot_matches_active_canvas_smoothing)
{
    ASSERT_NE(nullptr, E_Screen);
    ScreenshotStateRestore restore;
    cleanup_screenshots();

    // The legacy Engine slot remains NoZoom, while live world smoothing is
    // held separately in world_engine(). Produce the 2x SAI scratch, then
    // verify the screenshot captures that presented 640x400 surface rather
    // than the raw 320x200 world canvas.
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Sai);
    ASSERT_EQ(RenderEngine::NoZoom, E_Screen->Engine);
    ASSERT_EQ(RenderEngine::SAI, E_Screen->world_engine());
    E_Screen->set_active_canvas(CanvasTarget::World);
    E_Screen->begin_gameplay_frame();
    ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
    SDL_FillSurfaceRect(E_Screen->render, nullptr, 0x00112233u);
	{
		ScopedGameplayUiCanvas gameplay_ui(
			*og::runtime::current_session->myscreen_);
		const SDL_Rect hud_pixel{10, 10, 1, 1};
		ASSERT_TRUE(SDL_FillSurfaceRect(
			E_Screen->render, &hud_pixel,
			SDL_MapSurfaceRGBA(E_Screen->render, 220, 20, 30, 255)));
	}
    E_Screen->swap(0, 0, E_Screen->world_w(), E_Screen->world_h());
    ASSERT_NE(nullptr, E_Screen->render2);
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_screenshot());

    std::vector<std::filesystem::path> files = screenshot_files();
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ(std::make_pair(640, 400), saved_image_dimensions(files.front()));

    // The fixed UI canvas must still be captured raw even while a valid
    // world-filter scratch exists.
    cleanup_screenshots();
    E_Screen->set_active_canvas(CanvasTarget::UI);
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_screenshot());
    files = screenshot_files();
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ(std::make_pair(320, 200), saved_image_dimensions(files.front()));
}
