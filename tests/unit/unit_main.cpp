#include "unit.h"
#include <openglad/gameplay/family_registries.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

int main()
{
    // Entity code (living/walker) dereferences current_session->current_difficulty_.
    // Provide a zero-initialized session so set_difficulty() doesn't segfault.
    og::runtime::GameSession::Config cfg{};
    cfg.allocate_screen = false;
    cfg.allocate_prefs = false;
    cfg.install_legacy_globals = true;
    og::runtime::GameSession session(cfg);

    init_all_registries();

    int passed = 0;
    int failed = 0;
    for (const auto& tc : og::unit::registry())
    {
        std::fprintf(stderr, "[ RUN      ] %s\n", tc.name);
        try {
            tc.fn();
            ++passed;
            std::fprintf(stderr, "[       OK ] %s\n", tc.name);
        } catch (...) {
            ++failed;
            std::fprintf(stderr, "[  FAILED  ] %s (threw)\n", tc.name);
        }

        // Keep tests isolated: some tests swap current_game and do not restore it.
        // Rebind to a stable world/context after each test.
        static og::sim::SimEventLog stable_events;
        static og::gameplay::GameplayContext stable_game_ctx;
        stable_game_ctx.world = nullptr;
        stable_game_ctx.sim_events = &stable_events;
        og::gameplay::current_game = &stable_game_ctx;
    }

    std::fprintf(stderr, "\n=== Unit Results: %d passed, %d failed, %d total ===\n\n",
                 passed, failed, passed + failed);
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    return failed == 0 ? 0 : 1;
}
