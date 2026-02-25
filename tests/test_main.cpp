#include <unistd.h>
#include <signal.h>
#include <cstdlib>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump();
#endif

#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include <openglad/data/gparser.h>
#include <openglad/platform/io.h>
#include <openglad/core/util.h>
#include <openglad/input/input.h>
#include <openglad/render/view.h> // options
#include <format>
#include <openglad/runtime/game_context.h>
#include <openglad/runtime/screen_lifecycle.h>
#include <openglad/runtime/screen.h>
#include <openglad/data/save_data.h>
#include <openglad/sim/sim_event_log.h>

static void handle_test_signal(int sig)
{
    // Async-signal-safe termination to avoid leaving orphaned UI test processes
    // (e.g. when CTest is interrupted).
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    _exit(128 + sig);
}

int main(int argc, char* argv[]) {
    bool list_tests = false;
    const char* positional_filter = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--list-tests") == 0) {
            list_tests = true;
        } else if (strcmp(arg, "--test") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --test requires an exact test name (or comma-separated exact names)\n");
                return 2;
            }
            g_test_exact = argv[++i];
        } else if (strcmp(arg, "--filter") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --filter requires a substring\n");
                return 2;
            }
            g_test_filter = argv[++i];
        } else if (arg[0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", arg);
            return 2;
        } else if (!positional_filter) {
            positional_filter = arg;
        } else {
            fprintf(stderr, "error: unexpected extra argument: %s\n", arg);
            return 2;
        }
    }

    if (!g_test_filter && positional_filter)
        g_test_filter = positional_filter;

    if (list_tests) {
        list_all_tests(stdout);
        fflush(nullptr);
        _exit(0);
    }

    // Ensure the test process is terminated when its parent (usually CTest)
    // exits, and exit promptly on interrupt/terminate signals.
#ifdef __linux__
    (void)prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1)
        _exit(1);
#endif
    signal(SIGINT, handle_test_signal);
    signal(SIGTERM, handle_test_signal);

    // Force offscreen rendering - no display needed
    SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    // Game-loop tests (test_fairy_death, test_overpowered_team) run the full
    // game simulation at max speed. Two compile-time optimizations make
    // this fast enough for CI:
    //   1. sai2x.cpp creates the renderer without SDL_RENDERER_PRESENTVSYNC
    //      so SDL_RenderPresent doesn't block ~16ms per call.
    //   2. glad.cpp skips redraw()/refresh() in game_frame() so we don't
    //      burn CPU on software rendering that nobody sees.

    // Do global init once
    init_logging();
    SDL_Init(SDL_INIT_VIDEO);
    io_init(argc, argv);
    // Avoid startup hangs in CI from filesystem-backed config loading in module suites.
    cfg.apply_setting("graphics", "overscan_percentage", "0");

    // Create a GameSession which owns screen + prefs and sets current_session.
    // The legacy macros (myscreen, theprefs, overscan_percentage, etc.)
    // dereference current_session, so this must precede any macro usage.
    create_global_screen(1);
    init_input();

    // Now that current_session exists, apply overscan from cfg.
    og::runtime::current_session->overscan_percentage_ = static_cast<float>(
        parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
    update_overscan_setting();
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}", 100 * og::runtime::current_session->overscan_percentage_));

    // Initialize sim context so walkers created for testing have a valid RNG etc.
    static og::sim::SimEventLog test_events;
    static ProductionRandom test_rng;
    og::runtime::current_session->myscreen_->level_data.set_sim_context(&og::runtime::current_session->myscreen_->save_data, &og::runtime::current_session->myscreen_->world_.enemy_freeze,
                                         &test_events, &test_rng, &cfg);

    run_all_tests();

    // Default behavior uses _exit() to avoid SDL shutdown hangs seen in some
    // CI/runtime combinations. For coverage builds, explicitly flush gcov data
    // before _exit() since _exit() skips atexit handlers (where gcov normally
    // writes .gcda files).
    fflush(nullptr); // _exit() doesn't flush stdio — do it explicitly
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    _exit(g_tests_failed > 0 ? 1 : 0);
}
