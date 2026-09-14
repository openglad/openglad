#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <openglad/interface/game_context.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/pal32.h>
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
#include <optional>
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

// Issue #248: the boot renderer fallback decision. Only an unpinned Wayland
// boot with this process's sole window may reinitialize video on XWayland.
TEST(VideoModesMore, renderer_fallback_only_reboots_an_unpinned_sole_wayland_window)
{
	using OptDriver = std::optional<std::string_view>;
	EXPECT_EQ(OptDriver("x11"),
	          og::platform::renderer_fallback_video_driver("wayland", false, 1));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("wayland", true, 1));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("x11", false, 1));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("dummy", false, 1));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("offscreen", false, 1));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("emscripten", false, 1));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("wayland", false, 2));
	EXPECT_EQ(OptDriver(std::nullopt),
	          og::platform::renderer_fallback_video_driver("wayland", false, 0));
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

// The DISPLAY selector's three product rules, in the order they can be
// observed here:
//  * display_resolutions() is exactly SDL's fullscreen mode list for this
//    window's display, in physical pixels, filtered to >= 640x400, deduped
//    and sorted strictly descending -- and EMPTY when this topology forbids
//    an exclusive mode switch or SDL enumerates nothing that qualifies.
//  * desktop_resolution() is the desktop mode's physical pixel size.
//  * windowed_desktop_resolution() is the display's usable bounds.
// The last two are pinned unconditionally (their SDL sources are asserted
// non-degenerate first, so the comparisons cannot pass on a pair of zeroes);
// the mode-list comparison is honest about the drivers that enumerate no
// mode at all and says so instead of reporting a pass.
TEST(VideoModesMore, display_selector_reports_sdls_modes_the_desktop_and_the_usable_bounds)
{
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->window);
    sdl_video video(false);
    const std::vector<std::pair<int, int>> resolutions =
        video.display_resolutions();
    // Strictly descending subsumes "sorted" and "unique" in one pass: equal
    // neighbours (a lost dedupe) and an out-of-order pair both fail here.
    for (std::size_t i = 1; i < resolutions.size(); ++i)
    {
        EXPECT_GT(resolutions[i - 1], resolutions[i])
            << "entry " << i << " must be strictly smaller than its predecessor";
    }
    for (const auto& [width, height] : resolutions)
    {
        EXPECT_GE(width, 640)
            << "modes narrower than the classic 2x window are filtered out";
        EXPECT_GE(height, 400)
            << "modes shorter than the classic 2x window are filtered out";
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
    // Both sides of that comparison come from SDL. If this driver enumerates
    // no fullscreen mode at or above 640x400 they are both empty, and the
    // comparison would also hold for a display_resolutions() that returned
    // nothing unconditionally -- so say so instead of reporting a pass.
    const bool resolution_rule_is_observable = !expected_resolutions.empty();

    const auto desktop = video.desktop_resolution();
    const auto usable = video.windowed_desktop_resolution();
    const SDL_DisplayMode* const desktop_mode =
        SDL_GetDesktopDisplayMode(display);
    const std::pair<int, int> expected_desktop =
        desktop_mode != nullptr
            ? og::platform::display_mode_pixel_size(*desktop_mode)
            : std::pair<int, int>{0, 0};
    // Assert the SDL side is a real screen size FIRST: without this the
    // comparison below would also hold for a desktop_resolution() that had
    // been reduced to `return {0, 0};`.
    ASSERT_GT(expected_desktop.first, 0)
        << "SDL must report a desktop mode for this driver";
    ASSERT_GT(expected_desktop.second, 0)
        << "SDL must report a desktop mode for this driver";
    EXPECT_EQ(expected_desktop, desktop)
        << "desktop_resolution() is the desktop mode's physical pixel size";

    SDL_Rect usable_bounds{};
    const std::pair<int, int> expected_usable =
        SDL_GetDisplayUsableBounds(display, &usable_bounds) &&
                usable_bounds.w > 0 && usable_bounds.h > 0
            ? std::pair<int, int>{usable_bounds.w, usable_bounds.h}
            : std::pair<int, int>{0, 0};
    ASSERT_GT(expected_usable.first, 0)
        << "SDL must report usable bounds for this driver";
    ASSERT_GT(expected_usable.second, 0)
        << "SDL must report usable bounds for this driver";
    EXPECT_EQ(expected_usable, usable)
        << "windowed_desktop_resolution() is the display's usable bounds";
    // Windowed sizing must never be offered a resolution the desktop cannot
    // hold, so the usable bounds never exceed the desktop itself.
    EXPECT_LE(expected_usable.first, expected_desktop.first)
        << "the usable width fits inside the desktop";
    EXPECT_LE(expected_usable.second, expected_desktop.second)
        << "the usable height fits inside the desktop";

    // Only the mode-LIST rule is unobservable on such a driver; the desktop
    // and usable-bounds rules above have already run and are reported by the
    // skip message.
    if (!resolution_rule_is_observable)
        GTEST_SKIP() << "video driver '"
                     << (driver != nullptr ? driver : "(none)")
                     << "' enumerates no fullscreen mode at or above 640x400, "
                        "so the resolution comparison above proves nothing "
                        "here; the desktop/usable pins above still ran";
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
        static_cast<std::size_t>(E_Screen->render->h);
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

// putbuffer_surface forwards to putbuffer(SDL_Surface*), which blits the tile
// into the port CLIPPED to it: a tile entirely outside draws nothing, and a
// tile hanging off an edge draws only the part that fits.
TEST(VideoModesMore, putbuffer_surface_blits_the_tile_clipped_to_its_port)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->render);

    SurfacePtr surf = make_surface(32, 32);
    ASSERT_NE(nullptr, surf) << "SDL surface created";
    ASSERT_TRUE(SDL_FillSurfaceRect(
        surf.get(), nullptr, SDL_MapSurfaceRGB(surf.get(), 10, 20, 30)));

    const auto rgb = [s](int x, int y) {
        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        s->get_pixel(x, y, &r, &g, &b);
        return std::array<int, 3>{static_cast<int>(r), static_cast<int>(g),
                                  static_cast<int>(b)};
    };
    const std::array<int, 3> kTile{10, 20, 30};
    const std::array<int, 3> kBlack{0, 0, 0};

    s->clearbuffer();
    const std::size_t bytes = static_cast<std::size_t>(E_Screen->render->pitch) *
                              static_cast<std::size_t>(E_Screen->render->h);
    const std::vector<Uint8> cleared(
        static_cast<const Uint8*>(E_Screen->render->pixels),
        static_cast<const Uint8*>(E_Screen->render->pixels) + bytes);

    // Entirely outside the clipping region: not one byte changes.
    s->putbuffer_surface(500, 500, 16, 16, 0, 0, 319, 199, surf.get());
    EXPECT_EQ(0, std::memcmp(cleared.data(), E_Screen->render->pixels, bytes))
        << "a tile past the port's right/bottom edge must draw nothing";

    // Unclipped: a 16x16 tile at (10,10) covers exactly (10..25, 10..25).
    s->putbuffer_surface(10, 10, 16, 16, 0, 0, 319, 199, surf.get());
    EXPECT_EQ(kTile, rgb(10, 10)) << "top-left of the unclipped tile";
    EXPECT_EQ(kTile, rgb(25, 25)) << "bottom-right of the unclipped tile";
    EXPECT_EQ(kBlack, rgb(9, 10)) << "one pixel left of the tile";
    EXPECT_EQ(kBlack, rgb(26, 10)) << "one pixel right of the tile";

    // Clipped left/top: (-5,-5) draws source (5..15, 5..15) at (0..10, 0..10).
    s->putbuffer_surface(-5, -5, 16, 16, 0, 0, 319, 199, surf.get());
    EXPECT_EQ(kTile, rgb(0, 0)) << "the clipped tile starts at the port origin";
    EXPECT_EQ(kTile, rgb(10, 0)) << "its last drawn column is 10, not 15";
    EXPECT_EQ(kBlack, rgb(11, 0)) << "the clipped-away columns stay black";
    EXPECT_EQ(kBlack, rgb(0, 11)) << "the clipped-away rows stay black";

    // Clipped right/bottom: (310,190) with a 32x32 tile stops at the port.
    s->putbuffer_surface(310, 190, 32, 32, 0, 0, 319, 199, surf.get());
    EXPECT_EQ(kTile, rgb(310, 190)) << "top-left of the clipped tile";
    EXPECT_EQ(kTile, rgb(318, 198)) << "last drawn pixel inside the port";
    EXPECT_EQ(kBlack, rgb(319, 190)) << "the port's last column is excluded";
    EXPECT_EQ(kBlack, rgb(310, 199)) << "the port's last row is excluded";

    s->clearbuffer();
}

// Every buffered blit clips to its port: a tile fully outside draws nothing, a
// zero-width tile is a no-op, and a (-4,-4) tile draws source (4..15, 4..15) at
// (0..11, 0..11). create_accel_surface rejects a non-positive size and a span
// too small for the requested tile.
TEST(VideoModesMore, buffered_blits_clip_to_their_port_and_accel_surfaces_guard_sizes)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    ASSERT_NE(nullptr, E_Screen);
    ASSERT_NE(nullptr, E_Screen->render);
    ASSERT_NE(CanvasTarget::GameplayUI, E_Screen->active_canvas())
        << "the alpha pins below assume the legacy masked blend";

    std::array<unsigned char, 16 * 16> pixels{};
    pixels.fill(42);
    pixels[0] = 0;
    auto span = std::span<const unsigned char>(pixels.data(), pixels.size());

    const std::size_t bytes = static_cast<std::size_t>(E_Screen->render->pitch) *
                              static_cast<std::size_t>(E_Screen->render->h);
    const auto snapshot = [&]() {
        return std::vector<Uint8>(
            static_cast<const Uint8*>(E_Screen->render->pixels),
            static_cast<const Uint8*>(E_Screen->render->pixels) + bytes);
    };
    int index = -1;

    s->clearbuffer();
    const std::vector<Uint8> cleared = snapshot();

    // Off-port and degenerate blits paint nothing at all.
    s->putbuffer(400, 400, 16, 16, 0, 0, 319, 199, span);
    s->putbuffer_alpha(400, 400, 16, 16, 0, 0, 319, 199, span, 128);
    s->putbuffer_alpha(10, 10, 0, 16, 0, 0, 319, 199, span, 128);
    s->walkputbuffer_flash(400, 400, 16, 16, 0, 0, 319, 199, span, 40);
    s->walkputbuffer_flash(10, 10, 0, 16, 0, 0, 319, 199, span, 40);
    s->walkputbuffertext_alpha(400, 400, 16, 16, 0, 0, 319, 199, span, 40, 128);
    s->walkputbuffertext_alpha(10, 10, 0, 16, 0, 0, 319, 199, span, 40, 128);
    EXPECT_EQ(0, std::memcmp(cleared.data(), E_Screen->render->pixels, bytes))
        << "off-port and zero-width blits must leave the canvas untouched";

    // putbuffer clipped at the top-left corner.
    s->putbuffer(-4, -4, 16, 16, 0, 0, 319, 199, span);
    EXPECT_EQ(42, s->get_pixel(0, 0, &index));
    EXPECT_EQ(42, index) << "the clipped tile starts at the port origin";
    EXPECT_EQ(42, s->get_pixel(11, 11, &index))
        << "twelve rows and columns of the tile survive the clip";
    EXPECT_EQ(0, s->get_pixel(12, 0, &index))
        << "column 12 is past the clipped tile";

    // create_accel_surface guards.
    EXPECT_EQ(nullptr, s->create_accel_surface(span, 0, 16))
        << "a zero width has no surface";
    EXPECT_EQ(nullptr, s->create_accel_surface(span.first(3), 4, 4))
        << "a span smaller than width*height has no surface";
    void* surface = s->create_accel_surface(span, 16, 16);
    ASSERT_NE(nullptr, surface);
    s->destroy_accel_surface(surface);
    s->destroy_accel_surface(nullptr);

    // walkputbuffer_flash brightens each palette colour by 100 per channel
    // (saturating above 155) and writes it as raw RGB.
    int pr = 0;
    int pg = 0;
    int pb = 0;
    query_palette_reg(42, &pr, &pg, &pb);
    const auto flashed = [](int channel) {
        return channel > 155 ? 255 : channel + 100;
    };
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    s->clearbuffer();
    s->walkputbuffer_flash(-4, -4, 16, 16, 0, 0, 319, 199, span, 40);
    s->get_pixel(0, 0, &r, &g, &b);
    EXPECT_EQ(flashed(pr * 4), static_cast<int>(r)) << "flash red at (0,0)";
    EXPECT_EQ(flashed(pg * 4), static_cast<int>(g)) << "flash green at (0,0)";
    EXPECT_EQ(flashed(pb * 4), static_cast<int>(b)) << "flash blue at (0,0)";
    s->get_pixel(12, 0, &r, &g, &b);
    EXPECT_EQ(0, static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b))
        << "the clipped-away flash columns stay black";
    // Clipped at the right edge: a 16-wide tile at 310 stops at 318.
    s->walkputbuffer_flash(310, 190, 16, 16, 0, 0, 319, 199, span, 40);
    s->get_pixel(318, 190, &r, &g, &b);
    EXPECT_EQ(flashed(pr * 4), static_cast<int>(r)) << "flash red at (318,190)";
    s->get_pixel(319, 190, &r, &g, &b);
    EXPECT_EQ(0, static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b))
        << "the port's last column is excluded";

    // walkputbuffertext_alpha stamps the TEAM colour (not the sprite index),
    // and at full alpha the blend is exactly that palette entry.
    s->clearbuffer();
    s->walkputbuffertext_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 40, 255);
    EXPECT_EQ(40, s->get_pixel(0, 0, &index));
    EXPECT_EQ(40, index) << "opaque text alpha writes the team colour itself";
    EXPECT_EQ(40, s->get_pixel(11, 11, &index));
    EXPECT_EQ(0, s->get_pixel(12, 0, &index))
        << "column 12 is past the clipped tile";

    // Half alpha over black halves every channel of that palette entry.
    query_palette_reg(40, &pr, &pg, &pb);
    s->clearbuffer();
    s->walkputbuffertext_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 40, 128);
    s->get_pixel(0, 0, &r, &g, &b);
    EXPECT_EQ(pr * 2, static_cast<int>(r)) << "half-alpha text red";
    EXPECT_EQ(pg * 2, static_cast<int>(g)) << "half-alpha text green";
    EXPECT_EQ(pb * 2, static_cast<int>(b)) << "half-alpha text blue";
    s->get_pixel(12, 0, &r, &g, &b);
    EXPECT_EQ(0, static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b))
        << "the clipped-away text columns stay black";

    // The FAR edges of the same blitter: a 16-wide/16-tall tile at (310,190)
    // runs past portendx 319 and portendy 199, so walkputbuffertext_alpha's
    // right-edge arm (xmax = portendx - walkerstartx) and bottom-edge arm
    // (ymax = portendy - walkerstarty) both fire. Columns 310..318 and rows
    // 190..198 draw; column 319 and row 199 are excluded, because portendx /
    // portendy are exclusive. Source byte 0 is the transparent one, so the
    // tile's own top-left corner is skipped and (311,190) is the first stamp.
    s->clearbuffer();
    s->walkputbuffertext_alpha(310, 190, 16, 16, 0, 0, 319, 199, span, 40, 255);
    EXPECT_EQ(40, s->get_pixel(311, 190, &index))
        << "the tile draws from its own x inside the port";
    EXPECT_EQ(40, s->get_pixel(318, 190, &index))
        << "the last column inside the port is drawn";
    EXPECT_EQ(0, s->get_pixel(319, 190, &index))
        << "portendx is exclusive: column 319 is clipped away";
    EXPECT_EQ(40, s->get_pixel(311, 198, &index))
        << "the last row inside the port is drawn";
    EXPECT_EQ(0, s->get_pixel(311, 199, &index))
        << "portendy is exclusive: row 199 is clipped away";

    // putbuffer_alpha takes the same clip and halves the SPRITE colour.
    query_palette_reg(42, &pr, &pg, &pb);
    s->clearbuffer();
    s->putbuffer_alpha(-4, -4, 16, 16, 0, 0, 319, 199, span, 128);
    s->get_pixel(0, 0, &r, &g, &b);
    EXPECT_EQ(pr * 2, static_cast<int>(r)) << "half-alpha tile red";
    EXPECT_EQ(pg * 2, static_cast<int>(g)) << "half-alpha tile green";
    EXPECT_EQ(pb * 2, static_cast<int>(b)) << "half-alpha tile blue";
    s->get_pixel(12, 0, &r, &g, &b);
    EXPECT_EQ(0, static_cast<int>(r) + static_cast<int>(g) + static_cast<int>(b))
        << "the clipped-away tile columns stay black";

    s->clearbuffer();
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


// The three non-NORMAL sprite modes each have their own pixel rule
// (video_sdl.cpp):
//   INVISIBLE - an `outline` colour wins on every edge cell and on every
//               transparent cell touching ink; otherwise the cell is skipped
//               when rng(invisibility) > 8 and drawn (team-remapped) when not.
//   OUTLINE   - the same edge/adjacency rule, unconditionally.
//   PHANTOM   - the sprite is a stencil: every opaque cell REPLACES the canvas
//               pixel under it with a neighbouring canvas pixel (SHIFT_LEFT /
//               RIGHT / RIGHT_RANDOM / RANDOM / BLOCKY) or with a lighter /
//               darker palette index.
// Every case below reads the painted pixel back, so a mode that fell through
// to NORMAL is red.
TEST(VideoModesMore, walkputbuffer_invisible_outline_and_phantom_modes_paint_exact_pixels)
{
    screen* const s = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, s);
    ASSERT_EQ(320, s->canvas_w()) << "the phantom offset math assumes 320x200";
    ASSERT_EQ(200, s->canvas_h());

    // A ring sprite: opaque (250, i.e. team-remapped) border, transparent
    // interior. With teamcolor 40 an opaque cell resolves to 40+(255-250)=45.
    std::array<unsigned char, 8 * 8> ring{};
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            ring[static_cast<std::size_t>(y * 8 + x)] =
                (x == 0 || y == 0 || x == 7 || y == 7)
                    ? static_cast<unsigned char>(250)
                    : static_cast<unsigned char>(0);
    auto ring_span = std::span<const unsigned char>(ring.data(), ring.size());

    IRandom* const old_rng = ctx().rng;
    struct RngRestore
    {
        IRandom* saved;
        ~RngRestore() { ctx().rng = saved; }
    } rng_restore{old_rng};

    s->clearbuffer();
    int index = -1;

    // INVISIBLE with an outline: every opaque cell of this ring is an edge
    // cell, so it wears the outline before the rng gate is ever reached, and
    // the transparent cells touching the ring wear it too.
    FixedRandom rng0(0);
    ctx().rng = &rng0;
    s->walkputbuffer(50, 50, 8, 8, 0, 0, 319, 199, ring_span, 40,
                     static_cast<unsigned char>(INVISIBLE_MODE),
                     /*invisibility*/ 1, /*outline*/ 7, /*shifttype*/ 0);
    EXPECT_EQ(7, s->get_pixel(50, 50, &index));
    EXPECT_EQ(7, index) << "an edge cell wears the outline colour";
    EXPECT_EQ(7, s->get_pixel(51, 51, &index))
        << "a transparent cell beside the ring wears the outline colour";
    EXPECT_EQ(0, s->get_pixel(53, 53, &index))
        << "a transparent interior cell with no ink beside it stays empty";

    // INVISIBLE without an outline, rng(1) == 0 <= 8: the cell is DRAWN, with
    // the team remap applied.
    s->walkputbuffer(60, 50, 8, 8, 0, 0, 319, 199, ring_span, 40,
                     static_cast<unsigned char>(INVISIBLE_MODE),
                     /*invisibility*/ 1, /*outline*/ 0, /*shifttype*/ 0);
    EXPECT_EQ(45, s->get_pixel(60, 50, &index));
    EXPECT_EQ(45, index) << "250 remaps to teamcolor + (255 - 250)";

    // INVISIBLE without an outline, rng(10) == 9 > 8: the cell is SKIPPED.
    FixedRandom rng9(9);
    ctx().rng = &rng9;
    s->walkputbuffer(70, 50, 8, 8, 0, 0, 319, 199, ring_span, 40,
                     static_cast<unsigned char>(INVISIBLE_MODE),
                     /*invisibility*/ 10, /*outline*/ 0, /*shifttype*/ 0);
    EXPECT_EQ(0, s->get_pixel(70, 50, &index))
        << "rng(invisibility) > 8 must leave the canvas alone";

    // OUTLINE: edge cells and the transparent cells beside them wear the
    // outline; the empty interior does not.
    ctx().rng = &rng0;
    s->walkputbuffer(80, 50, 8, 8, 0, 0, 319, 199, ring_span, 40,
                     static_cast<unsigned char>(OUTLINE_MODE),
                     /*invisibility*/ 0, /*outline*/ 7, /*shifttype*/ 0);
    EXPECT_EQ(7, s->get_pixel(80, 50, &index));
    EXPECT_EQ(7, index) << "outline mode paints the ring itself";
    EXPECT_EQ(7, s->get_pixel(81, 51, &index))
        << "outline mode paints the transparent halo beside the ring";
    EXPECT_EQ(0, s->get_pixel(83, 53, &index))
        << "outline mode leaves the empty interior alone";

    // --- PHANTOM ---------------------------------------------------------
    // A 1x1 opaque stencil, so exactly one canvas pixel is replaced and the
    // source of the replacement is unambiguous.
    const std::array<unsigned char, 1> dot{250};
    auto dot_span = std::span<const unsigned char>(dot.data(), dot.size());
    const auto phantom = [&](int x, int y, unsigned char shift) {
        s->walkputbuffer(x, y, 1, 1, 0, 0, 319, 199, dot_span, 40,
                         static_cast<unsigned char>(PHANTOM_MODE),
                         /*invisibility*/ 0, /*outline*/ 0, shift);
    };

    s->clearbuffer();
    ctx().rng = &rng0;

    // SHIFT_LEFT copies the pixel one to the LEFT. (Palette index 30 reads
    // back as itself -- several low indices share an RGB triple, and
    // get_pixel answers with the first match, so the sentinels are checked.)
    s->pointb(119, 60, static_cast<unsigned char>(30));
    s->pointb(120, 60, static_cast<unsigned char>(10));
    ASSERT_EQ(30, s->get_pixel(119, 60, &index)) << "sentinel reads back";
    ASSERT_EQ(10, s->get_pixel(120, 60, &index)) << "sentinel reads back";
    phantom(120, 60, static_cast<unsigned char>(SHIFT_LEFT));
    EXPECT_EQ(30, s->get_pixel(120, 60, &index));
    EXPECT_EQ(30, index) << "SHIFT_LEFT samples buffoff - 1";

    // SHIFT_RIGHT copies the pixel one to the RIGHT.
    s->pointb(130, 60, static_cast<unsigned char>(10));
    s->pointb(131, 60, static_cast<unsigned char>(30));
    ASSERT_EQ(30, s->get_pixel(131, 60, &index)) << "sentinel reads back";
    phantom(130, 60, static_cast<unsigned char>(SHIFT_RIGHT));
    EXPECT_EQ(30, s->get_pixel(130, 60, &index));
    EXPECT_EQ(30, index) << "SHIFT_RIGHT samples buffoff + 1";

    // SHIFT_LIGHTER decrements the palette index (unless it is a multiple of 8
    // or zero); SHIFT_DARKER increments it (unless it is a multiple of 7).
    s->pointb(140, 60, static_cast<unsigned char>(10));
    phantom(140, 60, static_cast<unsigned char>(SHIFT_LIGHTER));
    EXPECT_EQ(9, s->get_pixel(140, 60, &index));
    EXPECT_EQ(9, index) << "SHIFT_LIGHTER steps 10 down to 9";

    s->pointb(150, 60, static_cast<unsigned char>(10));
    phantom(150, 60, static_cast<unsigned char>(SHIFT_DARKER));
    EXPECT_EQ(11, s->get_pixel(150, 60, &index));
    EXPECT_EQ(11, index) << "SHIFT_DARKER steps 10 up to 11";

    s->pointb(160, 60, static_cast<unsigned char>(8));
    phantom(160, 60, static_cast<unsigned char>(SHIFT_LIGHTER));
    EXPECT_EQ(8, s->get_pixel(160, 60, &index))
        << "SHIFT_LIGHTER holds a ramp boundary (index % 8 == 0)";
    s->pointb(170, 60, static_cast<unsigned char>(7));
    phantom(170, 60, static_cast<unsigned char>(SHIFT_DARKER));
    EXPECT_EQ(7, s->get_pixel(170, 60, &index))
        << "SHIFT_DARKER holds a ramp boundary (index % 7 == 0)";

    // The two rng-driven shifts: with rng(2) == 1 both sample one to the right.
    FixedRandom rng1(1);
    ctx().rng = &rng1;
    s->pointb(180, 60, static_cast<unsigned char>(10));
    s->pointb(181, 60, static_cast<unsigned char>(30));
    ASSERT_EQ(10, s->get_pixel(180, 60, &index)) << "sentinel reads back";
    phantom(180, 60, static_cast<unsigned char>(SHIFT_RIGHT_RANDOM));
    EXPECT_EQ(30, s->get_pixel(180, 60, &index))
        << "SHIFT_RIGHT_RANDOM picks its shift from rng(2)";

    s->pointb(190, 60, static_cast<unsigned char>(10));
    s->pointb(191, 60, static_cast<unsigned char>(30));
    ASSERT_EQ(10, s->get_pixel(190, 60, &index)) << "sentinel reads back";
    phantom(190, 60, static_cast<unsigned char>(SHIFT_RANDOM));
    EXPECT_EQ(30, s->get_pixel(190, 60, &index))
        << "SHIFT_RANDOM copies the RGB of buffoff + rng(2)";

    // SHIFT_BLOCKY needs a 2x2 stencil: on even rows only odd columns copy
    // (from two pixels left), on odd rows every column copies from the row
    // above. Transparent cells are skipped outright.
    const std::array<unsigned char, 4> block{0, 250, 250, 250};
    auto block_span = std::span<const unsigned char>(block.data(), block.size());
    s->pointb(199, 70, static_cast<unsigned char>(30));
    s->pointb(200, 70, static_cast<unsigned char>(10));
    s->pointb(201, 70, static_cast<unsigned char>(11));
    s->pointb(200, 71, static_cast<unsigned char>(12));
    s->pointb(201, 71, static_cast<unsigned char>(13));
    ASSERT_EQ(30, s->get_pixel(199, 70, &index)) << "sentinel reads back";
    ASSERT_EQ(11, s->get_pixel(201, 70, &index)) << "sentinel reads back";
    ASSERT_EQ(12, s->get_pixel(200, 71, &index)) << "sentinel reads back";
    ASSERT_EQ(13, s->get_pixel(201, 71, &index)) << "sentinel reads back";
    s->walkputbuffer(200, 70, 2, 2, 0, 0, 319, 199, block_span, 40,
                     static_cast<unsigned char>(PHANTOM_MODE), 0, 0,
                     static_cast<unsigned char>(SHIFT_BLOCKY));
    EXPECT_EQ(10, s->get_pixel(200, 70, &index))
        << "a transparent stencil cell leaves the canvas pixel alone";
    EXPECT_EQ(30, s->get_pixel(201, 70, &index))
        << "an odd column on an even row copies from two pixels left";
    EXPECT_EQ(10, s->get_pixel(200, 71, &index))
        << "an odd row copies from the row above";
    EXPECT_EQ(30, s->get_pixel(201, 71, &index))
        << "an odd row copies from the row above, after it was rewritten";

    s->clearbuffer();
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

// The exclusive-fullscreen mode picker's ranking rule. A player who asks the
// resolution menu for a physical size must get the smallest mode that still
// contains it, and among equally sized modes the one the desktop actually
// runs (its own logical layout and density), then the least scaled, then the
// refresh closest to the desktop's — never an arbitrary one, or the monitor
// switches to a stretched/HiDPI-doubled mode the menu never offered.
TEST(VideoModesMore, exclusive_mode_ranking_prefers_exact_desktop_then_density_then_refresh)
{
	// SDL owns the mode list, so the picker consumes borrowed pointers.
	const auto rank = [](const std::vector<SDL_DisplayMode>& storage,
	                     const SDL_DisplayMode* desktop, int w, int h) {
		std::vector<const SDL_DisplayMode*> modes;
		modes.reserve(storage.size());
		for (const SDL_DisplayMode& mode : storage)
			modes.push_back(&mode);
		return og::platform::best_fullscreen_mode_index(
			std::span<const SDL_DisplayMode* const>(modes.data(), modes.size()),
			desktop, w, h);
	};
	const auto mode = [](int w, int h, float density, float refresh) {
		SDL_DisplayMode m{};
		m.w = w;
		m.h = h;
		m.pixel_density = density;
		m.refresh_rate = refresh;
		return m;
	};

	// No mode is large enough in BOTH axes: refuse rather than attach a
	// smaller one (the caller then leaves the window where it is).
	const std::vector<SDL_DisplayMode> too_small{
		mode(1280, 720, 1.0f, 60.0f),   // short on both
		mode(1920, 1079, 1.0f, 60.0f),  // one pixel short on height
		mode(1919, 1080, 1.0f, 60.0f),  // one pixel short on width
	};
	EXPECT_EQ(-1, rank(too_small, nullptr, 1920, 1080));
	EXPECT_EQ(-1, rank({}, nullptr, 1920, 1080));
	// The same list satisfies a smaller request: the refusal above is the
	// size rule, not a broken loop.
	EXPECT_EQ(0, rank(too_small, nullptr, 1280, 720));

	// Smallest squared pixel error wins outright, even from the back.
	const std::vector<SDL_DisplayMode> by_error{
		mode(2560, 1440, 1.0f, 60.0f),  // error 640^2 + 360^2
		mode(1920, 1200, 1.0f, 60.0f),  // error 0 + 120^2
		mode(1920, 1080, 1.0f, 60.0f),  // error 0
	};
	EXPECT_EQ(2, rank(by_error, nullptr, 1920, 1080));

	// A request that equals the desktop's PHYSICAL size takes the desktop's
	// own logical layout, not the equally sized 1x mode: on a 2x display the
	// 1x entry would leave every window at half the size the user sees.
	const SDL_DisplayMode retina_desktop = mode(1920, 1080, 2.0f, 60.0f);
	const std::vector<SDL_DisplayMode> tie_on_pixels{
		mode(3840, 2160, 1.0f, 60.0f),  // same 3840x2160, density 1.0
		mode(1920, 1080, 2.0f, 60.0f),  // the desktop's own layout
	};
	EXPECT_EQ(1, rank(tie_on_pixels, &retina_desktop, 3840, 2160));
	// Ask for anything else and the desktop-layout tie-break is off, so the
	// density rule takes over and the 1x mode wins the same list.
	EXPECT_EQ(0, rank(tie_on_pixels, &retina_desktop, 3000, 1000));

	// Equal pixels, no desktop-layout claim: nearest density to 1.0.
	const SDL_DisplayMode plain_desktop = mode(1920, 1080, 1.0f, 60.0f);
	const std::vector<SDL_DisplayMode> tie_on_density{
		mode(640, 360, 2.0f, 60.0f),
		mode(1280, 720, 1.0f, 60.0f),
	};
	EXPECT_EQ(1, rank(tie_on_density, &plain_desktop, 1280, 720));

	// Equal pixels and density: nearest refresh to the desktop's 60 Hz.
	const std::vector<SDL_DisplayMode> tie_on_refresh{
		mode(1280, 720, 1.0f, 144.0f),
		mode(1280, 720, 1.0f, 60.0f),
	};
	EXPECT_EQ(1, rank(tie_on_refresh, &plain_desktop, 1280, 720));
	// With an unknown desktop refresh nothing separates them, so the list
	// order decides.
	const SDL_DisplayMode refreshless_desktop = mode(1920, 1080, 1.0f, 0.0f);
	EXPECT_EQ(0, rank(tie_on_refresh, &refreshless_desktop, 1280, 720));

	// Everything equal: lowest index, so the pick is stable across calls.
	const std::vector<SDL_DisplayMode> all_equal{
		mode(1280, 720, 1.0f, 60.0f),
		mode(1280, 720, 1.0f, 60.0f),
		mode(1280, 720, 1.0f, 60.0f),
	};
	EXPECT_EQ(0, rank(all_equal, &plain_desktop, 1280, 720));
}
