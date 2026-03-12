# Phase 3: Swap to GoogleTest

**Goal:** Replace the custom test framework with real GoogleTest. Since test files
already use GTest-compatible syntax (from Phase 2), this phase only changes
infrastructure: main files, CMake linking, and framework deletion.

## Dependency

GoogleTest is a system dependency, same as SDL2 — not vendored.

**CMakeLists.txt:**
```cmake
find_package(GTest REQUIRED)
```

**CI (`apt-get`):**
```bash
sudo apt-get install -y libgtest-dev
```

**Local dev:**
```bash
sudo apt-get install libgtest-dev   # Debian/Ubuntu
brew install googletest             # macOS
```

## New `integration_main.cpp`

Replace `test_main.cpp` + `test_framework.cpp` with a GoogleTest main:

```cpp
// tests/integration_main.cpp
#include <gtest/gtest.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
// ... SDL, PhysFS, screen includes ...

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

static void handle_test_signal(int sig)
{
#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    _exit(128 + sig);
}

class WorldCleanupListener : public ::testing::EmptyTestEventListener {
    void OnTestEnd(const ::testing::TestInfo&) override {
        if (og::runtime::current_session &&
            og::runtime::current_session->myscreen_ != nullptr)
            og::runtime::current_session->myscreen_->world().delete_objects();
    }
};

int main(int argc, char** argv) {
    // Kill this process if parent (CTest) exits — prevents orphaned SDL threads
    // lingering on CI runners when CTest times out.
#ifdef __linux__
    (void)prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1)
        _exit(1);
#endif
    signal(SIGINT, handle_test_signal);
    signal(SIGTERM, handle_test_signal);

    ::testing::InitGoogleTest(&argc, argv);

    // Config isolation
    auto test_config_dir = std::filesystem::temp_directory_path()
        / ("openglad_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    // SDL + PhysFS init (same as current test_main.cpp)
    SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    init_logging();
    SDL_Init(SDL_INIT_VIDEO);
    io_init(argc, argv);
    cfg.apply_setting("graphics", "overscan_percentage", "0");
    create_global_screen(1);
    init_input();

    // Apply overscan from cfg and initialize sim context
    og::runtime::current_session->overscan_percentage_ = static_cast<float>(
        parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
    update_overscan_setting();

    static og::sim::SimEventLog test_events;
    static ProductionRandom test_rng;
    og::runtime::current_session->myscreen_->level_runtime_data().set_sim_context(
        &og::runtime::current_session->myscreen_->save_data,
        &og::runtime::current_session->myscreen_->world().enemy_freeze,
        &test_events, &test_rng, &cfg);

    // Register cleanup listener
    ::testing::TestEventListeners& listeners =
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new WorldCleanupListener);

    int result = RUN_ALL_TESTS();

    // Cleanup temp config dir
    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);

#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    fflush(nullptr);
    _exit(result);
}
```

## Rewritten `unit_main.cpp`

```cpp
// tests/unit_main.cpp (rewritten)
#include <gtest/gtest.h>
// ... GameSession, registries ...

#ifdef ENABLE_COVERAGE
extern "C" void __gcov_dump(void);
#endif

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Config isolation
    auto test_config_dir = std::filesystem::temp_directory_path()
        / ("openglad_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(test_config_dir);
    setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);

    // Headless session (no screen, no prefs)
    og::runtime::GameSession::Config cfg{};
    cfg.allocate_screen = false;
    cfg.allocate_prefs = false;
    cfg.install_legacy_globals = true;
    og::runtime::GameSession session(cfg);
    // ... fallback world, save, events setup ...

    init_all_registries();

    int result = RUN_ALL_TESTS();

    std::error_code ec;
    std::filesystem::remove_all(test_config_dir, ec);

#ifdef ENABLE_COVERAGE
    __gcov_dump();
#endif
    return result;
}
```

## CMake Helper Updates

Update `og_add_test_group()`:
- Replace `test_main.cpp` + `test_framework.cpp` with `integration_main.cpp`
- Add `GTest::gtest` to `target_link_libraries`

Update `og_add_unit_group()`:
- Add `GTest::gtest` to `target_link_libraries`

## Test File Changes

Replace includes in all test files:
- `#include "test_framework.h"` -> `#include <gtest/gtest.h>`
- `#include "unit.h"` -> `#include <gtest/gtest.h>`

The compatibility macros (TEST, ASSERT_TRUE, etc.) are now provided by the real
`<gtest/gtest.h>` instead of the custom wrappers. Since Phase 2 already rewrote
all test files to use GTest-compatible syntax, this is a clean swap.

## What Gets Deleted

| File | Reason |
|------|--------|
| `tests/test_framework.h` | Replaced by `<gtest/gtest.h>` |
| `tests/test_framework.cpp` | Replaced by GoogleTest runner + WorldCleanupListener |
| `tests/unit/unit.h` | Replaced by `<gtest/gtest.h>` |
| `tests/test_main.cpp` | Replaced by `tests/integration_main.cpp` |
| `include/openglad/legacy/test_trace.h` | Consolidated into `core/test_trace.h` in Phase 2 |
| `src/test_trace.h` | Transitional shim — no longer needed |

## GoogleTest Output

GoogleTest produces structured output by default:

```
[==========] Running 58 tests from 2 test suites.
[----------] 36 tests from WalkerCombat
[ RUN      ] WalkerCombat.attack_hits
[       OK ] WalkerCombat.attack_hits (2 ms)
[ RUN      ] WalkerCombat.attack_friendly_fire
[       OK ] WalkerCombat.attack_friendly_fire (1 ms)
...
[==========] 58 tests from 2 test suites ran. (145 ms total)
[  PASSED  ] 58 tests.
```

Hang diagnosis is immediate — if `og_test_menu_ui` times out after 180s, CTest
reports which binary, and the captured GoogleTest output shows the last test that
started running.

Additional features:
- **`--gtest_output=xml:report.xml`** — JUnit XML output for CI dashboards
- **`--gtest_shuffle`** — randomize test order to surface order-dependent bugs
- **`--gtest_filter='WalkerCombat.*'`** — run specific suites/tests locally
- **`--gtest_repeat=N`** — repeat tests to find flaky failures

## Update CLAUDE.md

Replace the Testing section to document: new test binaries (24 groups), GoogleTest
framework (`TEST()`, `ASSERT_*`, fixtures), `og_add_test_group`/`og_add_unit_group`
CMake helpers, and remove references to old binaries/macros.

## Verification

1. All 1787 tests pass
2. Run `--gtest_shuffle` locally to find order-dependent tests, fix any that surface
3. Confirm no `~/.openglad` artifacts left on CI runners
4. CI passes (all jobs including coverage)
