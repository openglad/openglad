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
extern screen* myscreen;
extern options* theprefs;

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
    cfg.load_settings();
    overscan_percentage = static_cast<float>(
        parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
    update_overscan_setting();
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}", 100 * overscan_percentage));
    cfg.save_settings();

    // Optional test filter: ./openglad_test [filter_substring]
    if (argc > 1)
        g_test_filter = argv[1];

    static options test_prefs;
    ctx().prefs = &test_prefs;
    theprefs = ctx().prefs;
    create_global_screen(1);
    init_input();

    // Initialize sim context so walkers created for testing have a valid RNG etc.
    static og::sim::SimEventLog test_events;
    static ProductionRandom test_rng;
    myscreen->level_data.set_sim_context(&myscreen->save_data, &myscreen->enemy_freeze,
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
