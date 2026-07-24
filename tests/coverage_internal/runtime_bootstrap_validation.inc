#include <openglad/core/test_trace.h>

namespace og::runtime::testing
{
int run_runtime_bootstrap_lifecycle_validation(int argc, char* argv[])
{
    initialize_runtime_config(argc, argv);

    int failed_check = 0;
    int check_index = 0;
    const auto check = [&failed_check, &check_index](bool condition,
                                                     const char* message) {
        ++check_index;
        if (!condition && failed_check == 0)
        {
            failed_check = check_index;
            std::fprintf(stderr, "runtime bootstrap assertion failed: %s\n",
                         message);
        }
    };

    {
        GameSession session(default_session_config());
        bootstrap_runtime(argc, argv);

        check(current_session != nullptr,
              "the production session installs its legacy state");
        check(current_session != nullptr && current_session->myscreen_ != nullptr,
              "the production session allocates a screen");
        check(current_session != nullptr && current_session->theprefs_ != nullptr,
              "the production session allocates preferences");
        check(current_session != nullptr && current_session->keystates_ != nullptr,
              "bootstrap initializes the input keyboard state");
        check(current_session != nullptr &&
                  current_session->target_fps_ ==
                      og::core::target_fps_from_cfg(cfg),
              "bootstrap applies the configured frame rate to the session");
        check(current_session != nullptr &&
                  current_session->show_fps_ ==
                      cfg.is_on("graphics", "show_fps"),
              "bootstrap applies the configured FPS visibility");
        check(trace_contains("intro", "intro aborted by key"),
              "bootstrap enters the real intro and observes external input");

        // Production tears down IO while the session still owns the display;
        // mirror that ordering before the session destructor releases SDL.
        io_exit();
    }

    check(current_session == nullptr,
          "session destruction restores the process session pointer");
    check(SDL_WasInit(SDL_INIT_VIDEO) == 0,
          "owning video teardown shuts down SDL");
    return failed_check;
}
} // namespace og::runtime::testing
