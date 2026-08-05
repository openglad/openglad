#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <openglad/interface/game_context.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/io_common.h>
#include <physfs.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

	mode.w = 0;
	mode.h = -1;
	EXPECT_EQ(std::make_pair(0, 0),
	          og::platform::display_mode_pixel_size(mode));

	mode.w = std::numeric_limits<int>::max();
	mode.h = std::numeric_limits<int>::max();
	mode.pixel_density = 2.0f;
	EXPECT_EQ(std::make_pair(std::numeric_limits<int>::max(),
	                        std::numeric_limits<int>::max()),
	          og::platform::display_mode_pixel_size(mode));
}

TEST(VideoModesMore, exclusive_mode_switch_rejects_only_multi_display_x11)
{
	EXPECT_TRUE(og::platform::exclusive_mode_switch_is_safe("x11", 1));
	EXPECT_FALSE(og::platform::exclusive_mode_switch_is_safe("x11", 2));
	EXPECT_FALSE(og::platform::exclusive_mode_switch_is_safe("x11", 3));
	EXPECT_TRUE(og::platform::exclusive_mode_switch_is_safe("wayland", 2));
	EXPECT_TRUE(og::platform::exclusive_mode_switch_is_safe("windows", 2));
	EXPECT_TRUE(og::platform::exclusive_mode_switch_is_safe("offscreen", 2));
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

TEST(VideoModesMore, css_mouse_events_scale_to_the_live_window_exactly)
{
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->window);
    ASSERT_FALSE(SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST))
        << "the native event queue must be clean on test entry";

    int window_w = 0;
    int window_h = 0;
    ASSERT_TRUE(SDL_GetWindowSize(
        E_Screen->window, &window_w, &window_h));
    ASSERT_GT(window_w, 0);
    ASSERT_GT(window_h, 0);

    og::input_native::push_mouse_button_event_css(
        true, SDL_BUTTON_LEFT, window_w, window_h,
        window_w * 2, window_h * 2);
    const void* const event = og::input_native::poll_event();
    ASSERT_NE(nullptr, event);
    og::input_native::EventData decoded{};
    ASSERT_TRUE(og::input_native::decode_event(event, decoded));
    EXPECT_EQ(og::input_native::EventType::MouseButtonDown, decoded.type);
    EXPECT_EQ(SDL_BUTTON_LEFT, decoded.button);
    EXPECT_EQ(window_w / 2, decoded.button_x);
    EXPECT_EQ(window_h / 2, decoded.button_y);
    EXPECT_EQ(nullptr, og::input_native::poll_event());
}

TEST(VideoModesMore, text_input_falls_back_to_the_open_unfocused_window)
{
    ASSERT_NE(nullptr, E_Screen);
    SDL_Window* const window = E_Screen->window;
    ASSERT_NE(nullptr, window);
    const bool was_focusable =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_NOT_FOCUSABLE) == 0;
    const bool was_hidden =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) != 0;
    const bool text_input_was_active = SDL_TextInputActive(window);
    struct FocusRestore
    {
        SDL_Window* window;
        bool focusable;
        bool hidden;
        bool text_input_active;
        ~FocusRestore()
        {
            if (text_input_active)
                og::input_native::start_text_input();
            else
                og::input_native::stop_text_input();
            SDL_SetWindowFocusable(window, focusable);
            if (!hidden)
                SDL_ShowWindow(window);
            SDL_PumpEvents();
            SDL_FlushEvents(
                SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
        }
    } restore{window, was_focusable, was_hidden, text_input_was_active};

    ASSERT_TRUE(SDL_SetWindowFocusable(window, false));
    ASSERT_TRUE(SDL_HideWindow(window));
    SDL_PumpEvents();
    ASSERT_EQ(nullptr, SDL_GetKeyboardFocus());
    og::input_native::start_text_input("initial", 12, "prompt", false);
    EXPECT_TRUE(SDL_TextInputActive(window));
    og::input_native::stop_text_input();
    EXPECT_FALSE(SDL_TextInputActive(window));
}

TEST(VideoModesMore, virtual_joystick_events_decode_to_the_device_index)
{
    const bool joystick_was_initialized =
        og::input_native::joystick_subsystem_initialized();
    struct JoystickSubsystemRestore
    {
        bool was_initialized;
        ~JoystickSubsystemRestore()
        {
            if (!was_initialized &&
                og::input_native::joystick_subsystem_initialized())
            {
                og::input_native::joystick_quit_subsystem();
            }
        }
    } subsystem_restore{joystick_was_initialized};
    if (!joystick_was_initialized)
        og::input_native::joystick_init_subsystem();
    ASSERT_TRUE(og::input_native::joystick_subsystem_initialized());

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 2;
    desc.nbuttons = 2;
    desc.nhats = 1;
    desc.name = "OpenGlad coverage virtual joystick";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    struct JoystickRestore
    {
        SDL_JoystickID id;
        ~JoystickRestore()
        {
            if (id == 0)
                return;
            SDL_DetachVirtualJoystick(id);
            SDL_PumpEvents();
            SDL_FlushEvent(SDL_EVENT_JOYSTICK_REMOVED);
        }
    } restore{id};
    ASSERT_NE(0u, id);
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_EVENT_JOYSTICK_ADDED);

    struct JoystickIds
    {
        SDL_JoystickID* value;
        ~JoystickIds() { SDL_free(value); }
    };
    int count = 0;
    JoystickIds ids{SDL_GetJoysticks(&count)};
    ASSERT_NE(nullptr, ids.value);
    const SDL_JoystickID* const found =
        std::find(ids.value, ids.value + count, id);
    ASSERT_NE(ids.value + count, found);
    const int expected_index = static_cast<int>(found - ids.value);

    SDL_Event event{};
    event.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    event.jaxis.which = id;
    event.jaxis.axis = 1;
    event.jaxis.value = 12345;
    og::input_native::EventData decoded{};
    ASSERT_TRUE(og::input_native::decode_event(&event, decoded));
    EXPECT_EQ(og::input_native::EventType::JoyAxisMotion, decoded.type);
    EXPECT_EQ(expected_index, decoded.joy_axis_which);
    EXPECT_EQ(1, decoded.joy_axis_axis);
    EXPECT_EQ(12345, decoded.joy_axis_value);
}

TEST(VideoModesMore, no_screen_paths_return_the_documented_canvas_defaults)
{
    sdl_video video(false);
    std::unique_ptr<Screen> detached = std::move(E_Screen);
    ASSERT_NE(nullptr, detached);
    struct ScreenRestore
    {
        std::unique_ptr<Screen>& value;
        ~ScreenRestore() { E_Screen = std::move(value); }
    } restore{detached};

    video.reapply_world_scale();
    video.reflect_display_settings_from_window();
    video.set_active_canvas(CanvasTarget::World);
    EXPECT_EQ(kUiCanvasW, video.canvas_w());
    EXPECT_EQ(kUiCanvasH, video.canvas_h());
    EXPECT_EQ(kUiCanvasW, video.world_canvas_w());
    EXPECT_EQ(kUiCanvasH, video.world_canvas_h());
    EXPECT_EQ(kUiCanvasW, video.gameplay_ui_canvas_w());
    EXPECT_EQ(kUiCanvasH, video.gameplay_ui_canvas_h());
    EXPECT_TRUE(video.gameplay_ui_canvas_available());
    EXPECT_EQ(CanvasTarget::UI, video.active_canvas());
    EXPECT_EQ(CanvasTarget::UI, video.last_presented_canvas());
}

TEST(VideoModesMore, enumerated_display_resolutions_are_unique_and_sorted)
{
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->window);
    sdl_video video(false);
    const std::vector<std::pair<int, int>> resolutions =
        video.display_resolutions();
    EXPECT_TRUE(std::is_sorted(
        resolutions.begin(), resolutions.end(), std::greater<>()));
    EXPECT_EQ(resolutions.end(),
              std::adjacent_find(resolutions.begin(), resolutions.end()));
    for (const auto& [width, height] : resolutions)
    {
        EXPECT_GE(width, 640);
        EXPECT_GE(height, 400);
    }

    int display_count = 0;
    struct DisplayIds
    {
        SDL_DisplayID* value;
        ~DisplayIds() { SDL_free(value); }
    } displays{SDL_GetDisplays(&display_count)};
    ASSERT_NE(nullptr, displays.value);
    ASSERT_GT(display_count, 0);
    const char* const driver = SDL_GetCurrentVideoDriver();
    const bool exclusive_safe =
        og::platform::exclusive_mode_switch_is_safe(
            driver != nullptr ? driver : "", display_count);
    SDL_DisplayID display = SDL_GetDisplayForWindow(E_Screen->window);
    if (display == 0)
        display = SDL_GetPrimaryDisplay();
    ASSERT_NE(0u, display);

    std::vector<std::pair<int, int>> expected_resolutions;
    if (exclusive_safe)
    {
        int mode_count = 0;
        struct DisplayModes
        {
            SDL_DisplayMode** value;
            ~DisplayModes() { SDL_free(value); }
        } modes{SDL_GetFullscreenDisplayModes(display, &mode_count)};
        if (modes.value != nullptr)
        {
            for (int i = 0; i < mode_count; ++i)
            {
                const auto dimensions =
                    og::platform::display_mode_pixel_size(*modes.value[i]);
                if (dimensions.first >= 640 &&
                    dimensions.second >= 400 &&
                    std::find(expected_resolutions.begin(),
                              expected_resolutions.end(), dimensions) ==
                        expected_resolutions.end())
                {
                    expected_resolutions.push_back(dimensions);
                }
            }
            std::sort(expected_resolutions.begin(),
                      expected_resolutions.end(), std::greater<>());
        }
    }
    EXPECT_EQ(expected_resolutions, resolutions)
        << "the selector must expose exactly SDL's safe physical modes";

    const auto desktop = video.desktop_resolution();
    const auto usable = video.windowed_desktop_resolution();
    const SDL_DisplayMode* const desktop_mode =
        SDL_GetDesktopDisplayMode(display);
    const std::pair<int, int> expected_desktop =
        desktop_mode != nullptr
            ? og::platform::display_mode_pixel_size(*desktop_mode)
            : std::pair<int, int>{0, 0};
    EXPECT_EQ(expected_desktop, desktop);

    SDL_Rect usable_bounds{};
    const std::pair<int, int> expected_usable =
        SDL_GetDisplayUsableBounds(display, &usable_bounds) &&
                usable_bounds.w > 0 && usable_bounds.h > 0
            ? std::pair<int, int>{usable_bounds.w, usable_bounds.h}
            : std::pair<int, int>{0, 0};
    EXPECT_EQ(expected_usable, usable);
}

#ifdef __linux__
TEST(VideoModesMore, screenshot_open_failure_preserves_the_frame)
{
    ASSERT_NE(nullptr, E_Screen);
    screen* const value = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, value);
    const CanvasTarget old_target = value->active_canvas();
    struct CanvasTargetRestore
    {
        screen* value;
        CanvasTarget target;
        ~CanvasTargetRestore()
        {
            value->set_active_canvas(target);
        }
    } canvas_target_restore{value, old_target};
    value->set_active_canvas(CanvasTarget::UI);
    value->clearbuffer();
    value->pointb(7, 9, 42);
    const std::size_t bytes =
        static_cast<std::size_t>(E_Screen->render->pitch) *
        E_Screen->render->h;
    const std::vector<Uint8> before(
        static_cast<const Uint8*>(E_Screen->render->pixels),
        static_cast<const Uint8*>(E_Screen->render->pixels) + bytes);

    const char* const write_dir_ptr = PHYSFS_getWriteDir();
    ASSERT_NE(nullptr, write_dir_ptr);
    const std::string write_dir = write_dir_ptr;
    const std::filesystem::path old_cwd =
        std::filesystem::current_path();
    {
        struct StateRestore
        {
            std::string write_dir;
            std::filesystem::path cwd;
            CanvasTarget target;
            ~StateRestore()
            {
                std::error_code ec;
                std::filesystem::current_path(cwd, ec);
                PHYSFS_setWriteDir(write_dir.c_str());
                og::runtime::current_session->myscreen_->set_active_canvas(
                    target);
            }
        } restore{write_dir, old_cwd, old_target};

        ASSERT_NE(0, PHYSFS_setWriteDir(nullptr));
        std::filesystem::current_path("/proc/self");
        EXPECT_FALSE(value->save_screenshot());
        EXPECT_EQ(0, std::memcmp(
                         before.data(), E_Screen->render->pixels, bytes))
            << "failing to open the output must not mutate the frame";
    }

    ASSERT_NE(nullptr, PHYSFS_getWriteDir());
    EXPECT_EQ(write_dir, PHYSFS_getWriteDir());
    EXPECT_EQ(old_cwd, std::filesystem::current_path());
    EXPECT_EQ(old_target, value->active_canvas());
}
#endif

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

TEST(VideoModesMore, generic_pixel_format_blitters_preserve_transparency_and_team_colors)
{
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->render);

    // Normal gameplay renders into ARGB8888 and uses optimized row writers.
    // SDL still permits other software-surface formats, so exercise the real
    // generic paths against RGB24 and verify their observable pixel results.
    SurfacePtr rgb24(SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGB24));
    ASSERT_NE(nullptr, rgb24);
    ASSERT_EQ(3, SDL_GetPixelFormatDetails(rgb24->format)->bytes_per_pixel);

    SDL_Surface* const saved_render = E_Screen->render;
    struct RenderSurfaceRestore
    {
        SDL_Surface*& slot;
        SDL_Surface* saved;
        ~RenderSurfaceRestore() { slot = saved; }
    } restore{E_Screen->render, saved_render};
    E_Screen->render = rgb24.get();

    sdl_video video(false);
    ASSERT_TRUE(SDL_FillSurfaceRect(rgb24.get(), nullptr, 0));

    // Select the no-buffer overload: screen's virtual forwarding normally
    // uses the legacy overload with an explicit destination flag.
    video.draw_box(1, 1, 5, 4, 200, 1);
    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    video.get_pixel(2, 2, &red, &green, &blue);
    EXPECT_NE(0, static_cast<int>(red) + static_cast<int>(green) +
                     static_cast<int>(blue));

    std::array<unsigned char, 16> pixels{
        0, 250, 42, 43,
        44, 45, 0, 46,
        47, 48, 49, 50,
        51, 52, 53, 54,
    };
    const auto indexed = std::span<const unsigned char>(pixels);

    // RGB24 selects putbuffer's format-independent conversion loop. The
    // indexed tile blitter is opaque, including palette index zero.
    video.putbuffer(2, 2, 4, 4, 0, 0, 16, 16, indexed);
    int tile_index = -1;
    EXPECT_EQ(250, video.get_pixel(3, 2, &tile_index));
    EXPECT_EQ(250, tile_index) << "tile pixels retain their original palette index";

    // Sprite zeroes are transparent and values above 247 are recolored from
    // the supplied team ramp. RGB24 forces the non-ARGB optimized fallback.
    ASSERT_TRUE(SDL_FillSurfaceRect(rgb24.get(), nullptr, 0));
    video.walkputbuffer(2, 2, 4, 4, 0, 0, 16, 16, indexed, 40);
    int transparent_index = -1;
    EXPECT_EQ(0, video.get_pixel(2, 2, &transparent_index));
    EXPECT_EQ(0, transparent_index);
    int team_index = -1;
    EXPECT_EQ(45, video.get_pixel(3, 2, &team_index));
    EXPECT_EQ(45, team_index) << "250 maps to team color 40 + (255 - 250)";

    // A negative destination that remains partly inside its clipping port
    // selects walkputbuffer_alpha's bounds-safe generic loop.
    video.walkputbuffer_alpha(-1, 7, 4, 4, -2, 0, 16, 16,
                              indexed, 40, 255);
    video.get_pixel(0, 7, &red, &green, &blue);
    EXPECT_NE(0, static_cast<int>(red) + static_cast<int>(green) +
                     static_cast<int>(blue));

    // The mode-aware overload's NORMAL behavior has its own legacy loop.
    // Pin both transparency and team remapping through visible pixels.
    video.walkputbuffer(2, 11, 4, 4, 0, 0, 16, 16, indexed, 40,
                        static_cast<unsigned char>(NORMAL_MODE), 0, 0, 0);
    EXPECT_EQ(0, video.get_pixel(2, 11, &transparent_index));
    EXPECT_EQ(45, video.get_pixel(3, 11, &team_index));
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
    // verify the screenshot captures that presented surface rather than the
    // raw aspect-relative world canvas.
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
    const std::pair<int, int> expected_world_capture{
        E_Screen->world_w() * 2, E_Screen->world_h() * 2};
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_screenshot());

    std::vector<std::filesystem::path> files = screenshot_files();
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ(expected_world_capture, saved_image_dimensions(files.front()));

    // The fixed UI canvas must still be captured raw even while a valid
    // world-filter scratch exists.
    cleanup_screenshots();
    E_Screen->set_active_canvas(CanvasTarget::UI);
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_screenshot());
    files = screenshot_files();
    ASSERT_EQ(1u, files.size());
    EXPECT_EQ(std::make_pair(320, 200), saved_image_dimensions(files.front()));
}
