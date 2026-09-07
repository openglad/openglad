// Issue #248: booting when SDL cannot create a presenting renderer.
//
// SDL's Wayland backend implements no window framebuffer, so a process that
// cannot get an accelerated renderer there gets no renderer at all — and the
// Wayland surface, never handed a first buffer, is never mapped. OpenGlad used
// to log that and keep running invisibly forever, which is why starting the
// game under GNOME/Wayland needed an SDL_VIDEODRIVER=x11 override. The boot now
// reinitializes SDL video on XWayland instead, and a boot that still has no
// renderer is fatal.
//
// The transitions need their own process: the fallback quits and re-initializes
// the video subsystem, which would take the window out from under a shared
// integration runner. Each block leaves SDL torn down for the next one.
#include <openglad/core/test_trace.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unistd.h>

namespace {

// The TESTING build of the menu runner serializes button state through a
// mutex each test executable owns.
std::mutex allbuttons_mutex;

bool require(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "renderer fallback assertion failed: %s\n", message);
    return condition;
}

std::string current_video_driver()
{
    const char* const driver = SDL_GetCurrentVideoDriver();
    return driver != nullptr ? driver : "";
}

int live_window_count()
{
    int count = -1;
    SDL_free(SDL_GetWindows(&count));
    return count;
}

// Constructs the owning display and reports the unrecoverable error it threw,
// or an empty string when it unexpectedly succeeded.
std::string boot_display_expecting_failure(bool& ok)
{
    try
    {
        std::unique_ptr<sdl_video> display = std::make_unique<sdl_video>(true);
        ok &= require(false, "a boot with no renderer must not yield a display");
        return std::string();
    }
    catch (const std::runtime_error& error)
    {
        return error.what();
    }
}

} // namespace

std::mutex& get_allbuttons_mutex()
{
    return allbuttons_mutex;
}

int main(int argc, char* argv[])
{
    const std::filesystem::path test_root =
        std::filesystem::temp_directory_path() /
        ("openglad_renderer_fallback_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_root);
    setenv("OPENGLAD_CONFIG_DIR", test_root.c_str(), 1);
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_RENDER_DRIVER", "software", 1);

    bool ok = true;
    try
    {
        init_logging();
        io_init(argc, argv);

        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;
        og::runtime::GameSession session(session_cfg);

        cfg.apply_setting("graphics", "fullscreen", "windowed");
        cfg.apply_setting("graphics", "width", "640");
        cfg.apply_setting("graphics", "height", "400");
        cfg.apply_setting("graphics", "windowed_width", "640");
        cfg.apply_setting("graphics", "windowed_height", "400");
        cfg.apply_setting("graphics", "zoom", "1.0");
        cfg.apply_setting("graphics", "smoothing", "off");

        // 1. The renderer cannot be created and the video driver is the user's
        //    own choice: no fallback, and the boot fails loudly instead of
        //    running with a null renderer.
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER,
                                "openglad-intentionally-invalid",
                                SDL_HINT_OVERRIDE);
        const std::string pinned_error = boot_display_expecting_failure(ok);
        ok &= require(
            pinned_error.find("SDL_CreateRenderer failed") != std::string::npos,
            "the unrecoverable error names the failed renderer creation");
        ok &= require(pinned_error.find("'dummy'") != std::string::npos,
                      "the unrecoverable error names the live video driver");
        ok &= require(
            pinned_error.find("openglad-intentionally-invalid not available") !=
                std::string::npos,
            "the unrecoverable error carries SDL's own reason");
        ok &= require(
            pinned_error.find("SDL_VIDEODRIVER=x11") == std::string::npos,
            "a non-Wayland failure never advertises the XWayland escape hatch");
        ok &= require(E_Screen == nullptr,
                      "a failed boot publishes no global Screen");
        ok &= require(live_window_count() == 0,
                      "a failed boot leaves no window behind");
        ok &= require(current_video_driver() == "dummy",
                      "a pinned video driver is never switched");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) != 0,
                      "the video subsystem survives the failed boot");
        SDL_Quit();
        SDL_ResetHint(SDL_HINT_RENDER_DRIVER);
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "the failed pinned boot leaves a quiescent SDL");

        // 2. The renderer fails on an unpinned driver the fallback covers, but
        //    the fallback driver itself cannot be initialized: the previous
        //    driver is restored and the boot fails with both reasons.
        og::video_testing::g_renderer_fallback_probe_override =
            og::video_testing::RendererFallbackProbe{
                .current_driver = "wayland",
                .driver_pinned = false,
                .fallback_driver = "openglad-intentionally-invalid"};
        og::video_testing::g_renderer_create_failures_to_inject = 1;
        const std::string fallback_error = boot_display_expecting_failure(ok);
        ok &= require(
            fallback_error.find("injected renderer failure") != std::string::npos,
            "the unrecoverable error carries the original renderer reason");
        ok &= require(
            fallback_error.find(
                "fallback to 'openglad-intentionally-invalid' failed:") !=
                std::string::npos,
            "the unrecoverable error names the failed fallback driver");
        ok &= require(og::video_testing::g_renderer_create_failures_to_inject == 0,
                      "the boot attempted exactly one renderer before falling back");
        ok &= require(live_window_count() == 0,
                      "a failed fallback leaves no window behind");
        ok &= require(current_video_driver() == "dummy",
                      "a failed fallback restores the previous video driver");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) != 0,
                      "a failed fallback leaves the video subsystem initialized");
        og::video_testing::g_renderer_fallback_probe_override.reset();

        // The restored subsystem is a working one, not a husk.
        std::unique_ptr<sdl_video> restored =
            std::make_unique<sdl_video>(true);
        ok &= require(E_Screen != nullptr && E_Screen->renderer != nullptr,
                      "the restored video driver still boots a renderer");
        restored.reset();
        ok &= require(E_Screen == nullptr,
                      "the restored boot releases its Screen");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "the restored boot balances the SDL lifecycle");

        // 3. The reporter's case: no renderer on an unpinned Wayland boot, and
        //    the fallback driver works. The game starts, on the fallback
        //    driver, with a real renderer.
        trace_clear();
        og::video_testing::g_renderer_fallback_probe_override =
            og::video_testing::RendererFallbackProbe{.current_driver = "wayland",
                                                     .driver_pinned = false,
                                                     .fallback_driver = "dummy"};
        og::video_testing::g_renderer_create_failures_to_inject = 1;
        std::unique_ptr<sdl_video> recovered =
            std::make_unique<sdl_video>(true);
        ok &= require(E_Screen != nullptr && E_Screen->window != nullptr,
                      "the fallback boot creates a window on the new driver");
        ok &= require(E_Screen != nullptr && E_Screen->renderer != nullptr,
                      "the fallback boot creates the presenting renderer");
        if (E_Screen != nullptr && E_Screen->renderer != nullptr)
        {
            const char* const renderer_name =
                SDL_GetRendererName(E_Screen->renderer);
            ok &= require(
                renderer_name != nullptr &&
                    std::string(renderer_name) == "software",
                "the fallback driver supplies SDL's software renderer");
        }
        ok &= require(og::video_testing::g_renderer_create_failures_to_inject == 0,
                      "the fallback boot creates exactly one more renderer");
        ok &= require(
            trace_contains("video",
                           "renderer fallback: reinitialized video on 'dummy'"),
            "the fallback reinitialized SDL video on the fallback driver");
        ok &= require(trace_contains("video",
                                     "renderer fallback succeeded on 'dummy'"),
                      "the fallback boot reports the driver it recovered on");
        ok &= require(current_video_driver() == "dummy",
                      "the fallback boot runs on the fallback driver");
        ok &= require(live_window_count() == 1,
                      "the fallback boot leaves exactly its own window");
        og::video_testing::g_renderer_fallback_probe_override.reset();
        recovered.reset();
        ok &= require(E_Screen == nullptr,
                      "the fallback boot releases its Screen");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "the fallback boot balances the SDL lifecycle");
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);

        // 4. The same unpinned-Wayland shape as 3, except that the pin is read
        //    where production reads it (SDL_HINT_VIDEO_DRIVER, which SDL3
        //    folds SDL_VIDEODRIVER into) instead of being substituted. This
        //    process pins SDL_VIDEODRIVER=dummy, so the user's choice stands:
        //    no reboot onto the fallback driver, and a loud failure instead.
        og::video_testing::g_renderer_fallback_probe_override =
            og::video_testing::RendererFallbackProbe{
                .current_driver = "wayland",
                .driver_pinned = std::nullopt,
                .fallback_driver = "dummy"};
        og::video_testing::g_renderer_create_failures_to_inject = 1;
        const std::string derived_pin_error = boot_display_expecting_failure(ok);
        ok &= require(
            derived_pin_error.find("injected renderer failure") !=
                std::string::npos,
            "the pinned Wayland boot fails with SDL's own renderer reason");
        ok &= require(derived_pin_error.find("fallback to") == std::string::npos,
                      "an environment-pinned driver is never rebooted");
        ok &= require(
            derived_pin_error.find("set SDL_VIDEODRIVER=x11") !=
                std::string::npos,
            "a Wayland failure names the XWayland escape hatch");
        ok &= require(og::video_testing::g_renderer_create_failures_to_inject == 0,
                      "the pinned boot creates exactly one renderer");
        ok &= require(current_video_driver() == "dummy",
                      "the pinned boot leaves the live video driver alone");
        ok &= require(live_window_count() == 0,
                      "the pinned failure leaves no window behind");
        og::video_testing::g_renderer_fallback_probe_override.reset();
        SDL_Quit();
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "the pinned failure leaves a quiescent SDL");

        io_exit();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "renderer fallback exception: %s\n", error.what());
        ok = false;
    }

    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    return ok ? 0 : 1;
}
