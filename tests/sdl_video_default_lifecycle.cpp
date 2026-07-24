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
#include <string>
#include <unistd.h>

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
        std::unique_ptr<video> display(bridge.create_surface(320, 200));
        ok &= require(dynamic_cast<sdl_video*>(display.get()) != nullptr,
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

        display.reset();
        ok &= require(E_Screen == nullptr,
                      "owning SDL video destruction releases the global Screen");
        ok &= require(SDL_WasInit(SDL_INIT_VIDEO) == 0,
                      "owning SDL video destruction shuts down SDL");
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
