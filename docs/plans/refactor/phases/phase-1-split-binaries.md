# Phase 1: Split into 24 Binaries

**Goal:** Replace 4 overlapping monolithic test binaries with 24 disjoint groups.
No test *source* file changes — uses the existing custom test framework throughout.
(Infrastructure files `test_main.cpp` and `unit_main.cpp` are modified for config
isolation; `test_level_data_coverage.cpp` is modified to write temp files under the
config isolation directory instead of the source tree.)

See `docs/plans/refactor/common/group-assignments.md` for the full list of files
and test counts per group.

## CMakeLists.txt Changes

### Source List

Define `ALL_INTEGRATION_TEST_SOURCES` — the union of current `TEST_SOURCES` (143
test files) and the 10 EXTRA files from `RUNTIME_TEST_SOURCES`, **minus**
`test_framework.cpp` and `test_main.cpp` (infrastructure files compiled separately):

```cmake
set(ALL_INTEGRATION_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/tests/test_trace_buffer.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_startup.cpp
    # ... all 143 test files from TEST_SOURCES (excluding test_framework.cpp, test_main.cpp) ...
    # ... plus the 10 EXTRA files from RUNTIME_TEST_SOURCES ...
    ${CMAKE_SOURCE_DIR}/tests/test_entity_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_io_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_io_platform_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_level_data_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_mass_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_menu_model.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_runtime_coverage_paths.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_sim_input_handler.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_smooth_coverage.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_stats_coverage.cpp
)
```

### Move `glad.cpp` into `og_game_test`

Currently each test binary compiles `glad.cpp` separately. Move it into the shared
static library (compiled once with `-DTESTING`, which excludes `main()`):

```cmake
add_library(og_game_test STATIC
    ${GAME_SOURCES_NO_MAIN}
    ${SRC_DIR}/platform/sdl/glad.cpp   # NEW — was per-binary
    ${SRC_DIR}/test_trace.cpp
)
```

### Integration Group Helper

```cmake
function(og_add_test_group NAME)
    cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})

    # Select files from ALL_INTEGRATION_TEST_SOURCES by basename
    set(selected)
    foreach(src IN LISTS ALL_INTEGRATION_TEST_SOURCES)
        cmake_path(GET src FILENAME fname)
        if(fname IN_LIST ARG_FILES)
            list(APPEND selected "${src}")
        endif()
    endforeach()

    add_executable(${NAME}
        ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
        ${CMAKE_SOURCE_DIR}/tests/test_framework.cpp
        ${selected}
    )
    configure_openglad_library(${NAME})
    configure_openglad_sdl_target(${NAME})
    target_compile_definitions(${NAME} PRIVATE TESTING)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
        ${OG_THIRD_PARTY_INCLUDE_DIRS}
    )
    target_link_libraries(${NAME} PRIVATE og_game_test)
    configure_openglad_runtime_target(${NAME})
    add_runtime_assets_dependency(${NAME})

    # Coverage: configure_openglad_library() already adds --coverage compile/link
    # flags. Test binaries additionally need -O1 -g and the ENABLE_COVERAGE define
    # (for __gcov_dump() calls in test_main.cpp).
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${NAME} PRIVATE -O1 -g)
        target_compile_definitions(${NAME} PRIVATE ENABLE_COVERAGE)
    endif()

    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 180
        LABELS "integration"
    )
    if(ENABLE_SANITIZERS)
        set_tests_properties(${NAME} PROPERTIES ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
        )
    endif()
endfunction()
```

### Unit Group Helper

```cmake
function(og_add_unit_group NAME)
    cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})

    add_executable(${NAME}
        ${CMAKE_SOURCE_DIR}/tests/unit/unit_main.cpp
        ${ARG_FILES}
    )
    configure_openglad_library(${NAME})
    configure_openglad_sdl_target(${NAME})
    target_compile_definitions(${NAME} PRIVATE TESTING)
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_link_libraries(${NAME} PRIVATE og_game)
    configure_openglad_runtime_target(${NAME})

    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${NAME} PRIVATE -O1 -g)
        target_compile_definitions(${NAME} PRIVATE ENABLE_COVERAGE)
    endif()

    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES
        TIMEOUT 180
        LABELS "unit"
    )
    if(ENABLE_SANITIZERS)
        set_tests_properties(${NAME} PROPERTIES ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
        )
    endif()
endfunction()
```

### Group Definitions

```cmake
og_add_test_group(og_test_walker_combat FILES
    test_walker_combat.cpp
    test_walker_death.cpp
)

og_add_test_group(og_test_walker_move FILES
    test_walker_movement.cpp
    test_walker_pathing.cpp
)

# ... 18 more integration groups (see docs/plans/refactor/common/group-assignments.md) ...

og_add_unit_group(og_unit_sim FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_session_raii.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_event_log.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_world_headless.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_entity.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_sim_world.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_sim_input_unit.cpp
)

# ... 3 more unit groups (see docs/plans/refactor/common/group-assignments.md) ...
```

### What Gets Deleted from CMakeLists.txt

- `openglad_test` executable target
- `og_data_tests` executable target
- `og_runtime_tests` executable target
- `og_unit_tests` executable target
- `TEST_SOURCES`, `DATA_TEST_SOURCES`, `RUNTIME_TEST_SOURCES`, `OG_UNIT_TEST_SOURCES` lists
- `og_test_subset()` helper function
- Filtered CTest entries (`openglad_test_menu`, `openglad_test_picker`)
- All `set_tests_properties` and per-target `ENABLE_COVERAGE` blocks for old targets

### What Stays in CMakeLists.txt

The following existing CTest entries are **preserved unchanged**:

- `openglad_text_sim` — script-based text client test (60s timeout)
- `openglad_text_picker_interactive` — script-based text client test (60s timeout)
- `openglad_text_unsupported` — script-based text client test (60s timeout)
- `emscripten_build_test` — WASM build verification (300s timeout, `LABELS "emscripten;build"`)

These test the `openglad_text` binary and Emscripten build respectively — they are
outside the scope of the 1787 C++ tests being reorganized.

## Config Isolation

Add PID-based temp directory isolation to the **existing** `test_main.cpp` and
`unit_main.cpp`. This uses the existing `OPENGLAD_CONFIG_DIR` environment variable
that `get_user_path()` in `platform_io.cpp` already respects (line 111).

Add early in `main()` of both files:

```cpp
#include <filesystem>

// Config isolation — each binary gets its own temp dir
auto test_config_dir = std::filesystem::temp_directory_path()
    / ("openglad_test_" + std::to_string(getpid()));
std::filesystem::create_directories(test_config_dir);
setenv("OPENGLAD_CONFIG_DIR", test_config_dir.c_str(), 1);
```

And cleanup before exit:

```cpp
std::error_code ec;
std::filesystem::remove_all(test_config_dir, ec);
```

Each process gets its own directory, so parallel binaries never collide. For
crash/timeout cases, `/tmp` is cleaned by the OS periodically, or CI can wipe
`/tmp/openglad_test_*` before a run.

## Fix Source-Tree File Writes

`test_level_data_coverage.cpp` writes `.fss` files to the source tree via relative
paths (`scen/scen9301.fss` through `scen/scen9304.fss`, `scen/scen9401.fss` through
`scen/scen9403.fss`). Config isolation does not help here — `OPENGLAD_CONFIG_DIR`
only affects the config directory, not `WORKING_DIRECTORY`.

Fix by changing the test to write under the config isolation temp dir:

```cpp
// BEFORE
const int id_parse = 9301;
TEST_ASSERT(write_bytes(fs::path("scen") / std::format("scen{}.fss", id_parse), ...));

// AFTER — use OPENGLAD_CONFIG_DIR so files go to the per-process temp dir
const auto scen_dir = fs::path(std::getenv("OPENGLAD_CONFIG_DIR")) / "scen";
fs::create_directories(scen_dir);
TEST_ASSERT(write_bytes(scen_dir / std::format("scen{}.fss", id_parse), ...));
```

This eliminates the `rm -f scen/scen93*.fss scen/scen94*.fss` cleanup hack in
`coverage.yml`.

## CI Workflow Changes

**`.github/workflows/test.yml`** — replace targeted builds with full build + parallel CTest:

```yaml
- name: Build all test binaries
  run: cmake --build --preset ci-test -j"$(nproc)"

- name: Run tests (parallel)
  run: ctest --test-dir build/ci-test --parallel $(nproc) --output-on-failure --timeout 180
```

Same for the ASan job. `ASAN_OPTIONS`/`UBSAN_OPTIONS` env vars can be removed from
the workflow — they're now set via `ENVIRONMENT` test properties in CMake when
`ENABLE_SANITIZERS` is on.

**`.github/workflows/coverage.yml`** — same pattern, plus remove cleanup hacks:

```yaml
- name: Build all test binaries
  run: cmake --build --preset ci-coverage -j"$(nproc)"

- name: Run tests (parallel)
  run: ctest --test-dir build/ci-coverage --parallel $(nproc) --output-on-failure --timeout 180
```

Both manual cleanup lines are no longer needed:
- `rm -f "$HOME/.openglad/campaigns/..."` — eliminated by config isolation
- `rm -f scen/scen93*.fss scen/scen94*.fss` — eliminated by the source-tree fix above

The manual `timeout` wrappers are no longer needed (CTest `TIMEOUT` property).

The existing `build` job (compiles `openglad` and `openscen`) is unchanged.

**Baseline metrics:** Delete `scripts/collect_baseline_metrics.sh` and the
`baseline-metrics` job from `test.yml`. The script's guardrail thresholds were never
enabled (env vars default to 0), and the uploaded artifact was never consumed by any
downstream process.

## Labels

Tests get labels for selective running:

```cmake
# Unit groups get LABELS "unit" (set by og_add_unit_group)
# Integration groups get LABELS "integration" (set by og_add_test_group)
# Text client tests and emscripten_build_test retain their existing labels (unchanged)
```

Run subsets:

```bash
ctest -L unit              # only unit tests
ctest -L integration       # only integration tests
ctest -R og_test_walker    # only walker groups
```

## Coverage Note

Coverage instrumentation (`--coverage`) is applied to game code via:
- `og_game_test` — monolithic static lib used by integration test binaries
- Component libraries (`og_core`, `og_gameplay`, `og_interface`, `og_resources`,
  `og_platform_sdl`) — linked via `og_game` by unit test binaries
- `configure_openglad_library()` and `configure_component_library()` already add
  `--coverage` compile/link flags when `ENABLE_COVERAGE` is on

The test binaries themselves don't need `--coverage` on their own code (gcovr's
`--filter src/` excludes test files from the coverage denominator), but they DO need
`-O1 -g` and the `ENABLE_COVERAGE` compile define (for `__gcov_dump()` calls in their
`main()`). Both helper functions handle this.

Note: `og_game` is an INTERFACE library (no sources), so `--coverage` goes on the
component libraries it aggregates.

**Important:** The current `coverage.yml` gcovr config excludes `src/interface/`,
`src/platform/sdl/`, `src/ui/`, and `src/sdl_client/` from the coverage denominator.
Many of the 442 previously-orphaned tests exercise exactly these directories (screen,
view, video, picker, menu tests). Coverage percentages may not change much in Phase 1
despite running more tests — Phase 4 addresses this by removing the excludes.

## Verification

1. Build all 24 binaries
2. Run `--list-tests` on each binary, sum test counts
3. Confirm total = 1496 integration + 291 unit = 1787
4. Run `ctest --parallel $(nproc)` locally, confirm all pass
5. Push to branch, confirm all CI jobs pass
