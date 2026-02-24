#include "unit.h"
#include <openglad/entities/family_registries.h>
#include <openglad/runtime/game_session.h>

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
    }

    std::fprintf(stderr, "\n=== Unit Results: %d passed, %d failed, %d total ===\n\n",
                 passed, failed, passed + failed);
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    return failed == 0 ? 0 : 1;
}
