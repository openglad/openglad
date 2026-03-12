#include <unistd.h>
#include <cstring>
#include <filesystem>
#include "unit.h"
#include <openglad/core/util.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/sim_event_log.h>

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

int main(int argc, char* argv[])
{
    bool list_tests = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--list-tests") == 0)
        {
            list_tests = true;
            continue;
        }

        std::fprintf(stderr, "error: unknown option: %s\n", argv[i]);
        return 2;
    }

    if (list_tests)
    {
        for (const auto& tc : og::unit::registry())
            std::fprintf(stdout, "%s\n", tc.name);
        std::fflush(stdout);
        return 0;
    }

    const auto test_config_dir = std::filesystem::temp_directory_path() /
        ("openglad_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    init_logging();
    io_init(argc, argv);

    // Entity code (living/walker) dereferences current_session->current_difficulty_.
    // Provide a zero-initialized session so set_difficulty() doesn't segfault.
    og::runtime::GameSession::Config cfg{};
    cfg.allocate_screen = false;
    cfg.allocate_prefs = false;
    cfg.install_legacy_globals = true;
    og::runtime::GameSession session(cfg);
    GameWorld fallback_world(0);
    SaveData fallback_save;
    og::sim::SimEventLog fallback_events;

    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = &fallback_events;
    current_game = &session.game_;

    auto gameplay_context_intact = [&]() {
        return current_game == &session.game_ &&
               session.game_.world == &fallback_world &&
               session.game_.save == &fallback_save &&
               session.game_.sim_events == &fallback_events;
    };

    init_all_registries();

    int passed = 0;
    int failed = 0;
    for (const auto& tc : og::unit::registry())
    {
        if (!gameplay_context_intact())
        {
            ++failed;
            std::fprintf(stderr, "[  FAILED  ] %s (gameplay context corrupted before test)\n", tc.name);
            break;
        }

        std::fprintf(stderr, "[ RUN      ] %s\n", tc.name);
        try {
            tc.fn();
            ++passed;
            std::fprintf(stderr, "[       OK ] %s\n", tc.name);
        } catch (...) {
            ++failed;
            std::fprintf(stderr, "[  FAILED  ] %s (threw)\n", tc.name);
        }

        if (!gameplay_context_intact())
        {
            ++failed;
            std::fprintf(stderr, "[  FAILED  ] %s (corrupted gameplay context)\n", tc.name);
            break;
        }
    }

    std::fprintf(stderr, "\n=== Unit Results: %d passed, %d failed, %d total ===\n\n",
                 passed, failed, passed + failed);
    io_exit();
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);
    return failed == 0 ? 0 : 1;
}
