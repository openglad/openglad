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

int main(int argc, char* argv[]) {
    // Force offscreen rendering - no display needed
    SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    // Do global init once
    init_logging();
    SDL_Init(SDL_INIT_VIDEO);
    io_init(argc, argv);
    cfg.load_settings();
    cfg.save_settings();

    theprefs = new options;
    myscreen = new screen(1);
    init_input();

    run_all_tests();

    // Use _exit() to terminate immediately. SDL_Quit() (called by the video
    // destructor during normal cleanup) hangs on some drivers/configurations.
    // This is a test binary so we don't need graceful teardown — the OS
    // reclaims all resources on process exit.
    fflush(NULL); // _exit() doesn't flush stdio — do it explicitly
    _exit(g_tests_failed > 0 ? 1 : 0);
}
