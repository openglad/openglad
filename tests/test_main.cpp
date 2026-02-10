#include <unistd.h>

#include "graph.h"
#include "test_trace.h"
#include "test_framework.h"
#include "gparser.h"
#include "io.h"
#include "util.h"
#include "input.h"

extern screen* myscreen;
extern options* theprefs;

#ifdef ENABLE_COVERAGE
// Ensure coverage data is flushed even though we terminate via _exit().
extern "C" void __gcov_dump();
#endif

int main(int argc, char* argv[]) {
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
    cfg.save_settings();

    // Optional test filter: ./openglad_test [filter_substring]
    if (argc > 1)
        g_test_filter = argv[1];

    theprefs = new options;
    myscreen = new screen(1);
    init_input();

    run_all_tests();

    // Use _exit() to terminate immediately. SDL_Quit() (called by the video
    // destructor during normal cleanup) hangs on some drivers/configurations.
    // This is a test binary so we don't need graceful teardown — the OS
    // reclaims all resources on process exit.
    fflush(nullptr); // _exit() doesn't flush stdio — do it explicitly
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    _exit(g_tests_failed > 0 ? 1 : 0);
}
