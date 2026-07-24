#include <openglad/interface/platform_bridge.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool require(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "sdl lifecycle assertion failed: %s\n", message);
    return condition;
}

} // namespace

int main(int argc, char* argv[])
{
    const std::filesystem::path test_root =
        std::filesystem::temp_directory_path() /
        ("openglad_sdl_lifecycle_" + std::to_string(getpid()));
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
        ok &= require(SDL_Init(SDL_INIT_VIDEO), "SDL dummy video initializes");

        og::runtime::GameSession::Config session_cfg;
        session_cfg.allocate_screen = false;
        session_cfg.allocate_prefs = false;
        session_cfg.install_legacy_globals = true;
        og::runtime::GameSession session(session_cfg);

        // A non-Windowed boot first creates this remembered Windowed base,
        // reflects it, and then must reapply the saved Borderless request.
        cfg.apply_setting("graphics", "fullscreen", "borderless");
        cfg.apply_setting("graphics", "width", "997");
        cfg.apply_setting("graphics", "height", "613");
        cfg.apply_setting("graphics", "windowed_width", "643");
        cfg.apply_setting("graphics", "windowed_height", "411");
        cfg.apply_setting("graphics", "zoom", "1.0");
        cfg.apply_setting("graphics", "smoothing", "off");

        const PlatformBridge& bridge = platform_bridge();
        ok &= require(static_cast<bool>(bridge.create_surface),
                      "SDL platform bridge exposes an owning surface factory");
        ok &= require(static_cast<bool>(bridge.play_music),
                      "SDL platform bridge exposes the music callback");
        ok &= require(static_cast<bool>(bridge.stop_music),
                      "SDL platform bridge exposes the stop-music callback");
        bridge.play_music("unused-test-track");
        bridge.stop_music();
        std::unique_ptr<video> display(bridge.create_surface(320, 200));
        auto* const sdl_display =
            dynamic_cast<sdl_video*>(display.get());
        ok &= require(sdl_display != nullptr,
                      "factory creates the SDL video implementation");
        ok &= require(E_Screen != nullptr, "owning constructor creates Screen");
        ok &= require(E_Screen != nullptr && E_Screen->window != nullptr,
                      "owning constructor creates an SDL window");

        if (E_Screen != nullptr && E_Screen->window != nullptr)
        {
            const bool fullscreen =
                (SDL_GetWindowFlags(E_Screen->window) & SDL_WINDOW_FULLSCREEN) != 0;
            ok &= require(fullscreen,
                          "saved Borderless request is reapplied after baseline reflection");
            ok &= require(
                cfg.get_setting("graphics", "fullscreen") == "borderless",
                "confirmed Borderless mode remains persisted");
            ok &= require(cfg.get_setting("graphics", "width") == "997" &&
                              cfg.get_setting("graphics", "height") == "613",
                          "saved logical fullscreen dimensions survive boot");
            ok &= require(
                cfg.get_setting("graphics", "windowed_width") == "997" &&
                    cfg.get_setting("graphics", "windowed_height") == "613",
                "explicit logical size becomes the confirmed Windowed restore");
        }

        if (sdl_display != nullptr && E_Screen != nullptr)
        {
            // This executable links the production (non-TESTING) renderer:
            // exercise the actual timed fade loop with a short, deterministic
            // duration and verify both terminal frames.
            sdl_display->fadeDuration = 60;
            sdl_display->clearbuffer();
            sdl_display->pointb(20, 20, 200, 80, 40);
            Uint8 expected_red = 0;
            Uint8 expected_green = 0;
            Uint8 expected_blue = 0;
            sdl_display->get_pixel(
                20, 20,
                &expected_red, &expected_green, &expected_blue);
            ok &= require(
                expected_red != 0 || expected_green != 0 ||
                    expected_blue != 0,
                "fade reference pixel is visible");
            ok &= require(display->fadeblack(false) == 1,
                          "production fade-to-black completes");
            Uint8 black_red = 1;
            Uint8 black_green = 1;
            Uint8 black_blue = 1;
            sdl_display->get_pixel(
                20, 20, &black_red, &black_green, &black_blue);
            ok &= require(
                black_red == 0 && black_green == 0 && black_blue == 0,
                "fade-to-black presents the terminal black frame");

            ok &= require(
                SDL_FillSurfaceRect(
                    E_Screen->render, nullptr,
                    SDL_MapSurfaceRGB(E_Screen->render, 37, 91, 173)),
                "fade target frame is prepared");
            sdl_display->pointb(20, 20, 211, 67, 29);
            const std::size_t frame_bytes =
                static_cast<std::size_t>(E_Screen->render->pitch) *
                static_cast<std::size_t>(E_Screen->render->h);
            const auto* const frame_begin =
                static_cast<const Uint8*>(E_Screen->render->pixels);
            const std::vector<Uint8> expected_frame(
                frame_begin, frame_begin + frame_bytes);
            ok &= require(display->fadeblack(true) == 1,
                          "production fade-from-black completes");
            ok &= require(
                std::memcmp(
                    E_Screen->render->pixels,
                    expected_frame.data(), frame_bytes) == 0,
                "fade-from-black restores the complete current frame");
        }

        display.reset();
        ok &= require(E_Screen == nullptr,
                      "owning SDL video destruction releases the global Screen");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "owning SDL video destruction shuts down SDL");

        // Exercise the explicit owning constructor independently. Persisted
        // dimensions below the supported minimum must fall back to the safe
        // boot resolution before the SDL window is created.
        ok &= require(SDL_Init(SDL_INIT_VIDEO),
                      "SDL dummy video reinitializes");
        cfg.apply_setting("graphics", "fullscreen", "windowed");
        cfg.apply_setting("graphics", "width", "101");
        cfg.apply_setting("graphics", "height", "99");
        cfg.apply_setting("graphics", "windowed_width", "101");
        cfg.apply_setting("graphics", "windowed_height", "99");
        std::unique_ptr<sdl_video> explicit_display =
            std::make_unique<sdl_video>(true);
        ok &= require(E_Screen != nullptr && E_Screen->window != nullptr,
                      "explicit owning constructor creates an SDL window");
        if (E_Screen != nullptr && E_Screen->window != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(E_Screen->window, &width, &height);
            ok &= require(width == 640 && height == 400,
                          "explicit constructor uses the safe Windowed fallback");
            ok &= require(
                cfg.get_setting("graphics", "width") == "640" &&
                    cfg.get_setting("graphics", "height") == "400" &&
                    cfg.get_setting("graphics", "windowed_width") == "640" &&
                    cfg.get_setting("graphics", "windowed_height") == "400",
                "fallback Windowed size is reflected into configuration");
        }
        explicit_display.reset();
        ok &= require(E_Screen == nullptr,
                      "explicit owning constructor releases its Screen");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "explicit owning constructor balances SDL lifecycle");
        io_exit();
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "sdl lifecycle exception: %s\n", ex.what());
        ok = false;
    }

    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    return ok ? 0 : 1;
}
