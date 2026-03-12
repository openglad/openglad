# Test Infrastructure Refactor Plan

## Problem Statement

The current test infrastructure has several compounding issues:

1. **Monolithic binaries stuffed with unrelated tests.** `openglad_test` has 1239 tests,
   `og_runtime_tests` has 1047 (790 overlap with openglad_test), `og_data_tests` has 128
   (100% subset of openglad_test; 121 overlap with og_runtime_tests, 7 unique via
   test_util.cpp). Tests run sequentially within each binary — one hang kills the whole suite.

2. **No config isolation.** All test binaries share `~/.openglad/`. When CI runs
   `og_data_tests` then `og_runtime_tests` sequentially (or in parallel under ASan), they
   clobber each other's preferences, saves, and campaign data. The coverage workflow even has
   manual `rm -f "$HOME/.openglad/campaigns/org.openglad.test."*.glad` cleanup as a band-aid.

3. **442 orphaned tests.** `openglad_test` contains 442 tests not present in any module
   binary (`og_data_tests`, `og_runtime_tests`). But CI's `test.yml` only runs the module
   binaries — those 442 tests are **never executed in CI**.

4. **Massive redundancy.** `og_data_tests` (128 tests) is a 100% subset of `openglad_test`.
   `og_runtime_tests` shares 790 tests with `openglad_test`. CI runs the same tests multiple
   times while missing 442 others.

5. **Opaque failures.** When a test hangs, you see `[847/1047] test_foo ...` and then nothing
   for 10 minutes until the outer `timeout` kills the process. No way to know which test hung
   without reproducing locally.

## Goals

- **Every test runs exactly once** — no duplication, no orphans
- **Parallel execution** — CTest runs 24 binaries concurrently
- **Config isolation** — each binary gets its own temp directory, no `~/.openglad` clobbering
- **Per-binary timeouts** — 3 minutes each; a hang kills one group, not the whole suite
- **Standard test framework** — replace custom macros with GoogleTest (system dep, like SDL)
- **Coverage expanded** — remove gcovr directory excludes, backfill coverage to maintain thresholds
- **Total test count preserved** — 1787 tests (291 unit + 1496 integration)
- **Phased rollout** — each phase is independently testable and deployable

## Architecture

### Before

```
og_unit_tests      (291 tests, headless)
og_data_tests      (128 tests, subset of openglad_test)
og_runtime_tests   (1047 tests, mostly subset + 257 EXTRA)
openglad_test      (1239 tests, 442 unique to this binary)
─────────────────────────────────
Total unique: 1787  |  Actually run in CI: 1345 (missing 442)
```

### After

```
og_unit_sim             ( 72 tests)  ─┐
og_unit_families        ( 83 tests)   ├── 4 unit groups (headless, no SDL)
og_unit_entity          ( 82 tests)   │
og_unit_data            ( 54 tests)  ─┘
og_test_walker_combat   ( 58 tests)  ─┐
og_test_walker_move     ( 36 tests)   │
og_test_walker_core     ( 98 tests)   │
og_test_families        ( 88 tests)   │
og_test_effects         ( 76 tests)   │
og_test_living          ( 90 tests)   │
og_test_stats           ( 79 tests)   │
og_test_guy             ( 76 tests)   │
og_test_game_core       ( 85 tests)   ├── 20 integration groups (SDL)
og_test_screen          ( 61 tests)   │
og_test_view            ( 78 tests)   │
og_test_rendering       (113 tests)   │
og_test_picker          ( 56 tests)   │
og_test_menu_ui         ( 38 tests)   │
og_test_input           ( 55 tests)   │
og_test_level           ( 94 tests)   │
og_test_io              (107 tests)   │
og_test_smooth          ( 57 tests)   │
og_test_external        ( 35 tests)   │
og_test_mass_coverage   (116 tests)  ─┘
────────────────────────────────────
24 binaries  |  Total: 1787  |  All run in CI: 1787
```

## Detailed Group Assignments

Each integration group binary is built from the shared test main + test framework +
the group's specific test source files.

(`glad.cpp` is compiled into `og_game_test`, not per-binary.)

### Group 1: `og_test_walker_combat` (58 tests)

Walker combat mechanics, damage, death, blood, score.

| File | Tests |
|------|------:|
| test_walker_combat.cpp | 36 |
| test_walker_death.cpp | 22 |

### Group 2: `og_test_walker_move` (36 tests)

Walker movement, facing, pathfinding.

| File | Tests |
|------|------:|
| test_walker_movement.cpp | 33 |
| test_walker_pathing.cpp | 3 |

### Group 3: `og_test_walker_core` (98 tests)

Walker specials, core logic, collision, extended behavior.

| File | Tests |
|------|------:|
| test_walker_specials.cpp | 44 |
| test_walker_core_more.cpp | 25 |
| test_walker_extended.cpp | 19 |
| test_walker_more.cpp | 10 |

### Group 4: `og_test_families` (88 tests)

Per-family behaviors (all 15 families), difficulty, upgrade, death, AI.

| File | Tests |
|------|------:|
| test_family_behaviors.cpp | 83 |
| test_family_data.cpp | 5 |

### Group 5: `og_test_effects` (76 tests)

Effect entities: act logic, orbit, explosions, shields, doors, collisions.

| File | Tests |
|------|------:|
| test_effect_act.cpp | 22 |
| test_effect_chain_and_door.cpp | 3 |
| test_effect_death_more.cpp | 2 |
| test_effect_extended.cpp | 14 |
| test_effect_helpers.cpp | 18 |
| test_effect_more_paths.cpp | 16 |
| test_effect_weapon_interactions.cpp | 1 |

### Group 6: `og_test_living` (90 tests)

Living entities, weapons, treasures, entity coverage.

| File | Tests |
|------|------:|
| test_living_combat.cpp | 34 |
| test_living_funcs.cpp | 10 |
| test_weap_behavior.cpp | 24 |
| test_treasure_eat.cpp | 18 |
| test_entity_coverage.cpp | 4 |

### Group 7: `og_test_stats` (79 tests)

Statistics/command system: walk, fire, follow, hit response, blocked directions.

| File | Tests |
|------|------:|
| test_stats.cpp | 3 |
| test_stats_commands.cpp | 24 |
| test_stats_extended.cpp | 18 |
| test_stats_hit_response_more.cpp | 1 |
| test_stats_hit_response_teleport.cpp | 1 |
| test_stats_more_paths.cpp | 19 |
| test_stats_navigation.cpp | 1 |
| test_stats_right_walk_direct_walk.cpp | 4 |
| test_stats_coverage.cpp | 8 |

### Group 8: `og_test_guy` (76 tests)

Character (guy) data: creation, stats, leveling, costs, upgrades per family.

| File | Tests |
|------|------:|
| test_guy.cpp | 19 |
| test_guy_calcs.cpp | 31 |
| test_guy_extended.cpp | 26 |

### Group 9: `og_test_game_core` (85 tests)

Combat math, game context, startup, loaders, palette, game loop, full game sim.

| File | Tests |
|------|------:|
| test_combat_math.cpp | 39 |
| test_game_context.cpp | 13 |
| test_startup.cpp | 2 |
| test_trace_buffer.cpp | 2 |
| test_palette.cpp | 3 |
| test_loader_and_walker.cpp | 4 |
| test_gloader_funcs.cpp | 12 |
| test_game_loop.cpp | 4 |
| test_game_launch.cpp | 2 |
| test_go_no_team.cpp | 2 |
| test_fairy_death.cpp | 1 |
| test_overpowered_team.cpp | 1 |

### Group 10: `og_test_screen` (61 tests)

Screen/world: add/remove objects, find, targeting, runtime coverage paths.

| File | Tests |
|------|------:|
| test_screen_extended.cpp | 20 |
| test_screen_funcs.cpp | 16 |
| test_runtime_coverage_paths.cpp | 25 |

### Group 11: `og_test_view` (78 tests)

Viewscreen: drawing, redraw, resize, lifecycle, input, HUD, radar, scaler.

| File | Tests |
|------|------:|
| test_view_draw.cpp | 9 |
| test_view_redraw.cpp | 10 |
| test_view_funcs.cpp | 20 |
| test_view_lifecycle.cpp | 2 |
| test_view_resize.cpp | 10 |
| test_view_team_list.cpp | 1 |
| test_radar_more.cpp | 2 |
| test_glad_hud.cpp | 3 |
| test_sai2x_scaler.cpp | 1 |
| test_view_input_paths.cpp | 3 |
| test_view_input_more_paths.cpp | 1 |
| test_view_input_smoke.cpp | 2 |
| test_view_input_prefs_and_redraw.cpp | 3 |
| test_view_get_keypress_and_edge_cases.cpp | 4 |
| test_view_menu_driven.cpp | 1 |
| test_view_options_menu_driven.cpp | 1 |
| test_view_team.cpp | 5 |

### Group 12: `og_test_rendering` (113 tests)

Video primitives, pixel ops, draw modes, fading, text rendering and input.

| File | Tests |
|------|------:|
| test_video_buffers.cpp | 2 |
| test_video_draw.cpp | 25 |
| test_video_extended.cpp | 14 |
| test_video_extra.cpp | 28 |
| test_video_fade.cpp | 4 |
| test_video_modes_more.cpp | 3 |
| test_video_pixel_ops.cpp | 3 |
| test_video_primitives.cpp | 1 |
| test_text_render.cpp | 26 |
| test_text_rendering.cpp | 1 |
| test_text_input_and_width.cpp | 4 |
| test_text_input_ex_value.cpp | 2 |

### Group 13: `og_test_picker` (56 tests)

Picker menu: state machine, costs, dialogs, navigation, ownership, menu model.

| File | Tests |
|------|------:|
| test_picker_accessible_levels.cpp | 3 |
| test_picker_costs.cpp | 3 |
| test_picker_detail_menu_driven.cpp | 4 |
| test_picker_dialogs_real.cpp | 3 |
| test_picker_funcs.cpp | 13 |
| test_picker_menu_nav.cpp | 2 |
| test_picker_ownership_fixture.cpp | 1 |
| test_picker_state_machine.cpp | 17 |
| test_picker_timed_dialog.cpp | 1 |
| test_picker_uncovered.cpp | 4 |
| test_menu_model.cpp | 5 |

### Group 14: `og_test_menu_ui` (38 tests)

Menu navigation flows: main menu, hire, train, save, options, help, difficulty,
results screen, campaign/level picker, intro, level progress.

This group has the highest concentration of thread-based interactive tests.

| File | Tests |
|------|------:|
| test_back_to_mainmenu.cpp | 1 |
| test_menu.cpp | 3 |
| test_menu_layout.cpp | 7 |
| test_new_game.cpp | 1 |
| test_hire_team.cpp | 1 |
| test_train_team.cpp | 1 |
| test_save_menu.cpp | 1 |
| test_save_load_team.cpp | 2 |
| test_options_menu.cpp | 1 |
| test_difficulty.cpp | 1 |
| test_help.cpp | 1 |
| test_help_parsing.cpp | 2 |
| test_help_smoke.cpp | 2 |
| test_results_screen_branches.cpp | 2 |
| test_results_screen_full_ui.cpp | 1 |
| test_results_screen_internal_helper.cpp | 1 |
| test_intro_smoke.cpp | 1 |
| test_campaign_and_level_picker.cpp | 8 |
| test_level_progress.cpp | 1 |

### Group 15: `og_test_input` (55 tests)

Input handling: keyboard, joystick, keybinds, event dispatch, sim input handler.

| File | Tests |
|------|------:|
| test_input.cpp | 5 |
| test_input_event_dispatch.cpp | 1 |
| test_input_joystick.cpp | 3 |
| test_input_keybinds.cpp | 15 |
| test_input_more.cpp | 4 |
| test_sim_input_handler.cpp | 27 |

### Group 16: `og_test_level` (94 tests)

Level data: load/save versions, grid ops, error paths, level editor.

| File | Tests |
|------|------:|
| test_level_data_error_paths.cpp | 8 |
| test_level_data_load_versions.cpp | 26 |
| test_level_data_ops.cpp | 19 |
| test_level_editor_helpers.cpp | 4 |
| test_level_editor_interactions.cpp | 2 |
| test_level_editor_prompt_block.cpp | 3 |
| test_level_editor_smoke.cpp | 1 |
| test_level_data_coverage.cpp | 31 |

### Group 17: `og_test_io` (107 tests)

IO/filesystem, zip, config parsing, save/load, platform IO coverage.

| File | Tests |
|------|------:|
| test_io_filesystem.cpp | 2 |
| test_io_funcs.cpp | 14 |
| test_io_new_file_fixture.cpp | 2 |
| test_io_zip_unzip.cpp | 13 |
| test_gparser_funcs.cpp | 18 |
| test_util.cpp | 7 |
| test_save_data_versions.cpp | 16 |
| test_save_load.cpp | 3 |
| test_load_levels.cpp | 4 |
| test_io_coverage.cpp | 2 |
| test_io_platform_coverage.cpp | 26 |

### Group 18: `og_test_smooth` (57 tests)

Terrain auto-smoothing: query, genre mapping, tile masks, coverage branches.

| File | Tests |
|------|------:|
| test_smooth_matrix.cpp | 5 |
| test_smooth_more_branches.cpp | 8 |
| test_smooth_ops.cpp | 28 |
| test_smoother.cpp | 3 |
| test_smooth_coverage.cpp | 13 |

### Group 19: `og_test_external` (35 tests)

Vendored library smoke tests: PhysFS, zlib, libzip, libyaml.

| File | Tests |
|------|------:|
| test_external_libzip.cpp | 4 |
| test_external_physfs_api_edges.cpp | 1 |
| test_external_physfs_archivers.cpp | 2 |
| test_external_physfs_byteorder.cpp | 2 |
| test_external_physfs_ops.cpp | 1 |
| test_external_physfs_unicode.cpp | 3 |
| test_external_physfs_zip.cpp | 1 |
| test_external_yaml.cpp | 4 |
| test_external_yaml_api_builder.cpp | 2 |
| test_external_yaml_document.cpp | 3 |
| test_external_yaml_more.cpp | 3 |
| test_external_zlib.cpp | 2 |
| test_external_zlib_api_edges.cpp | 3 |
| test_external_zlib_more.cpp | 4 |

### Group 20: `og_test_mass_coverage` (116 tests)

Broad coverage smoke tests exercising uncovered paths across many subsystems
(video, text, screen, viewscreen, buttons, YAML, entities).

| File | Tests |
|------|------:|
| test_mass_coverage.cpp | 116 |

### Unit Groups

All unit groups are headless (no SDL). Each binary is built from `unit_main.cpp`
(headless GameSession) + its source files, linked against `og_game`.

### Unit Group 1: `og_unit_sim` (72 tests)

Session lifecycle, sim events, sim world, sim entity, sim input.

| File | Tests |
|------|------:|
| tests/unit/test_session_raii.cpp | 14 |
| tests/unit/test_sim_event_log.cpp | 7 |
| tests/unit/test_sim_world_headless.cpp | 30 |
| tests/unit/test_sim_entity.cpp | 10 |
| tests/test_sim_world.cpp | 5 |
| tests/test_sim_input_unit.cpp | 6 |

### Unit Group 2: `og_unit_families` (83 tests)

Family registry, per-family behaviors (cleric, druid, orc, thief), picker common logic.

| File | Tests |
|------|------:|
| tests/unit/test_family_registry.cpp | 11 |
| tests/test_family_cleric.cpp | 15 |
| tests/test_family_druid.cpp | 2 |
| tests/test_family_orc.cpp | 3 |
| tests/test_family_thief.cpp | 4 |
| tests/unit/test_picker_common.cpp | 48 |

### Unit Group 3: `og_unit_entity` (82 tests)

Walker, living, stats, smooth — pure logic unit tests + cross-cutting coverage.

| File | Tests |
|------|------:|
| tests/test_walker_unit.cpp | 12 |
| tests/test_walker_movement_unit.cpp | 10 |
| tests/test_walker_specials_unit.cpp | 2 |
| tests/test_living_unit.cpp | 7 |
| tests/test_stats_unit.cpp | 11 |
| tests/test_smooth_unit.cpp | 9 |
| tests/test_coverage_misc.cpp | 31 |

### Unit Group 4: `og_unit_data` (54 tests)

Level data, save data, config parsing, zip API, PhysFS wrappers, input actions, spectator.

| File | Tests |
|------|------:|
| tests/test_level_data_unit.cpp | 16 |
| tests/test_save_data_unit.cpp | 1 |
| tests/test_gparser_unit.cpp | 3 |
| tests/test_zip_api.cpp | 2 |
| tests/unit/test_physfs_wrappers.cpp | 2 |
| tests/unit/test_input_actions.cpp | 28 |
| tests/unit/test_spectator_mode.cpp | 2 |

## Phase 1: Split into 24 Binaries

**Goal:** Replace 4 overlapping monolithic test binaries with 24 disjoint groups.
No test *source* file changes — uses the existing custom test framework throughout.
(Infrastructure files `test_main.cpp` and `unit_main.cpp` are modified for config
isolation; `test_level_data_coverage.cpp` is modified to write temp files under the
config isolation directory instead of the source tree.)

### CMakeLists.txt Changes

#### Source List

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

#### Move `glad.cpp` into `og_game_test`

Currently each test binary compiles `glad.cpp` separately. Move it into the shared
static library (compiled once with `-DTESTING`, which excludes `main()`):

```cmake
add_library(og_game_test STATIC
    ${GAME_SOURCES_NO_MAIN}
    ${SRC_DIR}/platform/sdl/glad.cpp   # NEW — was per-binary
    ${SRC_DIR}/test_trace.cpp
)
```

#### Integration Group Helper

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

#### Unit Group Helper

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

#### Group Definitions

```cmake
og_add_test_group(og_test_walker_combat FILES
    test_walker_combat.cpp
    test_walker_death.cpp
)

og_add_test_group(og_test_walker_move FILES
    test_walker_movement.cpp
    test_walker_pathing.cpp
)

# ... 18 more integration groups (see Detailed Group Assignments) ...

og_add_unit_group(og_unit_sim FILES
    ${CMAKE_SOURCE_DIR}/tests/unit/test_session_raii.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_event_log.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_world_headless.cpp
    ${CMAKE_SOURCE_DIR}/tests/unit/test_sim_entity.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_sim_world.cpp
    ${CMAKE_SOURCE_DIR}/tests/test_sim_input_unit.cpp
)

# ... 3 more unit groups (see Detailed Group Assignments) ...
```

#### What Gets Deleted from CMakeLists.txt

- `openglad_test` executable target
- `og_data_tests` executable target
- `og_runtime_tests` executable target
- `og_unit_tests` executable target
- `TEST_SOURCES`, `DATA_TEST_SOURCES`, `RUNTIME_TEST_SOURCES`, `OG_UNIT_TEST_SOURCES` lists
- `og_test_subset()` helper function
- Filtered CTest entries (`openglad_test_menu`, `openglad_test_picker`)
- All `set_tests_properties` and per-target `ENABLE_COVERAGE` blocks for old targets

#### What Stays in CMakeLists.txt

The following existing CTest entries are **preserved unchanged**:

- `openglad_text_sim` — script-based text client test (60s timeout)
- `openglad_text_picker_interactive` — script-based text client test (60s timeout)
- `openglad_text_unsupported` — script-based text client test (60s timeout)
- `emscripten_build_test` — WASM build verification (300s timeout, `LABELS "emscripten;build"`)

These test the `openglad_text` binary and Emscripten build respectively — they are
outside the scope of the 1787 C++ tests being reorganized.

### Config Isolation

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

### Fix Source-Tree File Writes

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

### CI Workflow Changes

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

### Labels

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

### Coverage Note

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

### Verification

1. Build all 24 binaries
2. Run `--list-tests` on each binary, sum test counts
3. Confirm total = 1496 integration + 291 unit = 1787
4. Run `ctest --parallel $(nproc)` locally, confirm all pass
5. Push to branch, confirm all CI jobs pass

## Phase 2: GTest-Compatible Naming

**Goal:** Rewrite all test files to use GoogleTest-style syntax (`TEST()`,
`ASSERT_TRUE()`, etc.) while still backed by the custom framework. No GoogleTest
dependency required. This is a mechanical, scriptable transformation.

### Compatibility Macros

Add GTest-compatible macros to `test_framework.h`. These wrap the existing custom
framework with GTest-style signatures.

**Streaming assertion support:**

```cpp
// Accumulates << messages, prints on destruction
struct OgTestMessage {
    std::string str;
    template<typename T>
    OgTestMessage& operator<<(const T& val) {
        std::ostringstream oss;
        oss << val;
        str += oss.str();
        return *this;
    }
};

// Records assertion failure; used with `return` to exit test on failure
struct OgAssertHelper {
    const char* file;
    int line;
    const char* expr;

    void operator=(OgTestMessage msg) {
        fprintf(stderr, "  FAIL: %s", expr);
        if (!msg.str.empty())
            fprintf(stderr, " - %s", msg.str.c_str());
        fprintf(stderr, " (%s:%d)\n", file, line);
        g_tests_failed++;
    }
};
```

**Assertion macros (fatal — test stops on failure):**

```cpp
#define ASSERT_TRUE(cond) \
    if (cond) ; \
    else return OgAssertHelper{__FILE__, __LINE__, #cond} = OgTestMessage{}

#define ASSERT_EQ(expected, actual) \
    if ((expected) == (actual)) ; \
    else return OgAssertHelper{__FILE__, __LINE__, \
        "ASSERT_EQ(" #expected ", " #actual ")"} = \
        (OgTestMessage{} << "Expected: " << (expected) << ", Actual: " << (actual))

#define ASSERT_STREQ(expected, actual) \
    if (std::strcmp((expected), (actual)) == 0) ; \
    else return OgAssertHelper{__FILE__, __LINE__, \
        "ASSERT_STREQ(" #expected ", " #actual ")"} = \
        (OgTestMessage{} << "Expected: \"" << (expected) << "\", Actual: \"" << (actual) << "\"")
```

**Registration macros:**

```cpp
#define OG_TEST_PASTE(a, b) a##_##b

// TEST(Suite, Name) — self-registering test
#define TEST(suite, name) \
    static void OG_TEST_PASTE(suite, name)(); \
    REGISTER_TEST(OG_TEST_PASTE(suite, name)); \
    static void OG_TEST_PASTE(suite, name)()

// TEST_F(Fixture, Name) — test with fixture class (SetUp/TearDown)
#define TEST_F(fixture, name) \
    struct fixture##_##name##_cls : fixture { void TestBody(); }; \
    static void fixture##_##name##_fn() { \
        fixture##_##name##_cls inst; \
        inst.SetUp(); \
        inst.TestBody(); \
        inst.TearDown(); \
    } \
    REGISTER_TEST(fixture##_##name##_fn); \
    void fixture##_##name##_cls::TestBody()
```

**Unit test compatibility** (in `unit.h`):

```cpp
// TEST(Suite, Name) for unit tests — wraps OG_UNIT_TEST registration
#define TEST(suite, name) \
    static void suite##_##name(); \
    static ::og::unit::Registrar suite##_##name##_registrar( \
        #suite "." #name, &suite##_##name); \
    static void suite##_##name()

// ASSERT_TRUE — aborts on failure (matches current OG_ASSERT severity)
#define ASSERT_TRUE(cond) OG_ASSERT(cond)
```

### Mechanical Rewrites

Every test file gets a mechanical transformation. The patterns are:

**Registration:**
```cpp
// BEFORE                              // AFTER
REGISTER_TEST(test_foo);               // deleted — TEST() self-registers
void test_foo() {                      TEST(WalkerCombat, foo) {
```

**Assertions (fatal — test stops on failure):**
```cpp
// BEFORE                                      // AFTER
TEST_ASSERT(cond, "msg");                      ASSERT_TRUE(cond) << "msg";
TEST_ASSERT_EQ(expected, actual, "msg");       ASSERT_EQ(expected, actual) << "msg";
TEST_ASSERT_STR_EQ(expected, actual, "msg");   ASSERT_STREQ(expected, actual) << "msg";
OG_ASSERT(cond);                               ASSERT_TRUE(cond);
```

**Fixtures:**
```cpp
// BEFORE
void setup_foo() { ... }
void teardown_foo() { ... }
void test_bar() { ... }
REGISTER_TEST_WITH_FIXTURE(test_bar, setup_foo, teardown_foo);

// AFTER
class FooFixture {
public:
    void SetUp() { ... }
    void TearDown() { ... }
};
TEST_F(FooFixture, bar) { ... }
```

**File-local wrapper macros:**
```cpp
// BEFORE (test_walker_specials.cpp — 44 tests)
#define REGISTER_SPECIAL_TEST(func) \
    REGISTER_TEST_WITH_FIXTURE(func, nullptr, teardown_walker_special_test)
void test_walker_special_soldier_charge() { ... }
REGISTER_SPECIAL_TEST(test_walker_special_soldier_charge);

// AFTER
class WalkerSpecialFixture {
public:
    void SetUp() {}
    void TearDown() { teardown_walker_special_test(); }
};
TEST_F(WalkerSpecialFixture, soldier_charge) { ... }
```

```cpp
// BEFORE (test_mass_coverage.cpp — 116 tests)
#define MASS_TEST(name, ...) \
    void name() { __VA_ARGS__ } \
    REGISTER_TEST(name)
MASS_TEST(test_mass_menunav_up, { (void)MenuNav{.up=1}; });

// AFTER
TEST(MassCoverage, menunav_up) { (void)MenuNav{.up=1}; }
```

**Trace-based assertions:**
```cpp
// BEFORE
trace_clear();
// ... trigger behavior ...
TEST_ASSERT(trace_contains("combat", "hit"), "expected hit trace");

// AFTER
trace_clear();
// ... trigger behavior ...
ASSERT_TRUE(trace_contains("combat", "hit")) << "expected hit trace";
```

### Test Suite Naming

Each test file maps to a GoogleTest suite. The suite name comes from the file:

| File | Suite Name |
|------|-----------:|
| test_walker_combat.cpp | WalkerCombat |
| test_effect_act.cpp | EffectAct |
| test_io_funcs.cpp | IoFuncs |
| test_external_yaml.cpp | ExternalYaml |

### Trace Header Consolidation

Update all files that include the legacy trace header to use the core path:

- `src/test_trace.cpp` — change `#include <openglad/legacy/test_trace.h>` to
  `#include <openglad/core/test_trace.h>`
- `src/test_trace.h` (transitional shim) — change to redirect to `<openglad/core/test_trace.h>`
- `src/platform/sdl/game.cpp`, `src/platform/sdl/video_sdl.cpp` — same change
- 23 test files that include `<openglad/legacy/test_trace.h>` — same change

(27 files total across test and non-test code. The headers are currently identical;
this just normalizes the include path before the legacy header is deleted in Phase 3.)

### What Stays (unchanged)

| File | Reason |
|------|--------|
| `tests/test_interact.h` | SDL button interaction helpers — not test framework |
| `tests/test_input_helpers.h` | SDL event injection — not test framework |
| `tests/test_game_world_fixture.h` | Helper struct for minimal game world — tests create as local variable |
| `tests/test_gameplay_context_scope.h` | RAII scope guard — framework-independent |
| `include/openglad/core/test_trace.h` | Canonical trace header (all includes consolidated here) |
| `src/test_trace.cpp` | Trace buffer implementation |

### Verification

1. All 1787 tests still pass
2. No test file references old macros (`REGISTER_TEST`, `TEST_ASSERT`, `OG_UNIT_TEST`)
3. All trace includes use `<openglad/core/test_trace.h>`
4. CI passes

## Phase 3: Swap to GoogleTest

**Goal:** Replace the custom test framework with real GoogleTest. Since test files
already use GTest-compatible syntax (from Phase 2), this phase only changes
infrastructure: main files, CMake linking, and framework deletion.

### Dependency

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

### New `integration_main.cpp`

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

### Rewritten `unit_main.cpp`

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

### CMake Helper Updates

Update `og_add_test_group()`:
- Replace `test_main.cpp` + `test_framework.cpp` with `integration_main.cpp`
- Add `GTest::gtest` to `target_link_libraries`

Update `og_add_unit_group()`:
- Add `GTest::gtest` to `target_link_libraries`

### Test File Changes

Replace includes in all test files:
- `#include "test_framework.h"` → `#include <gtest/gtest.h>`
- `#include "unit.h"` → `#include <gtest/gtest.h>`

The compatibility macros (TEST, ASSERT_TRUE, etc.) are now provided by the real
`<gtest/gtest.h>` instead of the custom wrappers. Since Phase 2 already rewrote
all test files to use GTest-compatible syntax, this is a clean swap.

### What Gets Deleted

| File | Reason |
|------|--------|
| `tests/test_framework.h` | Replaced by `<gtest/gtest.h>` |
| `tests/test_framework.cpp` | Replaced by GoogleTest runner + WorldCleanupListener |
| `tests/unit/unit.h` | Replaced by `<gtest/gtest.h>` |
| `tests/test_main.cpp` | Replaced by `tests/integration_main.cpp` |
| `include/openglad/legacy/test_trace.h` | Consolidated into `core/test_trace.h` in Phase 2 |
| `src/test_trace.h` | Transitional shim — no longer needed |

### GoogleTest Output

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

### Update CLAUDE.md

Replace the Testing section to document: new test binaries (24 groups), GoogleTest
framework (`TEST()`, `ASSERT_*`, fixtures), `og_add_test_group`/`og_add_unit_group`
CMake helpers, and remove references to old binaries/macros.

### Verification

1. All 1787 tests pass
2. Run `--gtest_shuffle` locally to find order-dependent tests, fix any that surface
3. Confirm no `~/.openglad` artifacts left on CI runners
4. CI passes (all jobs including coverage)

## Phase 4: Expand Coverage

**Goal:** Remove the gcovr directory excludes that currently hide large portions of
the codebase from coverage measurement. Backfill test coverage as needed to maintain
the existing threshold requirements (line 85%, function 90%).

### Current Excludes

The `coverage.yml` gcovr config currently excludes:
- `src/interface/` — screen, viewscreen, level runtime, input, buttons
- `src/platform/sdl/` — game entry, video, audio
- `src/ui/` — picker, menus, level editor, intro
- `src/sdl_client/` — text client
- `src/fuzz/` — fuzz targets
- `src/test_trace.cpp` — test infrastructure

### Approach

Remove excludes incrementally, one directory at a time. For each:

1. Remove the exclude from gcovr config
2. Measure the coverage drop
3. Add targeted tests to recover the threshold
4. Verify CI coverage job passes

Suggested order (smallest coverage gap first):
1. `src/test_trace.cpp` — trivial, already well-exercised by tests
2. `src/sdl_client/` — small, has dedicated text client tests
3. `src/platform/sdl/` — game entry + video, exercised by integration tests
4. `src/ui/` — menus/picker, exercised by Group 13-14 tests
5. `src/interface/` — largest, includes screen/view/input

`src/fuzz/` should remain excluded (fuzz targets aren't test code).

### Verification

1. Coverage thresholds maintained (line 85%, function 90%) after each exclude removal
2. No regressions in existing tests

## Risk Mitigation

**Order-dependent tests:** Some tests assume state from prior tests (e.g.,
`cleanup_picker_state()` patterns). Splitting into smaller groups in Phase 1 may
surface these. Fix by adding proper setup/teardown. Run `--gtest_shuffle` during
Phase 3 verification to proactively find more.

**Mechanical rewrite errors (Phase 2):** The `REGISTER_TEST` → `TEST()` and
`TEST_ASSERT` → `ASSERT_*` rewrites are repetitive but error-prone in bulk. Mitigate
by scripting the transform (sed/python) and verifying test counts match exactly after
the rewrite. The compatibility macros ensure the rewritten tests still compile and
run against the custom framework before GTest is introduced.

**Build time:** 24 link steps instead of 4. Each integration binary links against
`og_game_test` (already built once as a static lib, now includes `glad.cpp`); each
unit binary links against `og_game`. Incremental cost is just linking. Ninja
parallelizes this. GoogleTest itself (Phase 3) is pre-built from the system package —
no compile overhead.

**Thread-based interactive tests in Group 14 (menu_ui):** These are the most likely to
hang. With 38 tests in the group and a 3-minute timeout, a hang is immediately
attributable. If this group proves chronically flaky, it can be further split.

**Phase 2 compatibility fidelity:** The custom compatibility macros approximate but
don't perfectly replicate GTest behavior. Key difference: `TEST_F` in the compatibility
layer calls `SetUp()`/`TearDown()` as regular methods, not via GTest's test runner.
This is sufficient for the 6 fixture-based tests and is replaced by real GTest in
Phase 3.

## Summary

| Metric | Before | After Phase 1 | After Phase 3 | After Phase 4 |
|--------|--------|---------------|---------------|---------------|
| Tests run in CI | 1345 | 1787 | 1787 | 1787 |
| Orphaned tests | 442 | 0 | 0 | 0 |
| Duplicate test runs | ~918 | 0 | 0 | 0 |
| Test binaries | 4 (overlapping) | 24 (disjoint) | 24 (disjoint) | 24 (disjoint) |
| Max binary size | 1239 tests | 116 tests | 116 tests | 116 tests |
| Test framework | Custom macros | Custom (GTest naming) | GoogleTest | GoogleTest |
| Config isolation | None | Per-process temp dir | Per-process temp dir | Per-process temp dir |
| Parallelism | Sequential | CTest --parallel | CTest --parallel | CTest --parallel |
| Per-binary timeout | None (10 min kill) | 180s per binary | 180s per binary | 180s per binary |
| Hang diagnosis | "somewhere in 1047" | "test_foo in og_test_Y" | "Suite.test in og_test_Y" | same |
| Test output | Custom stderr | Custom stderr | GoogleTest structured | same |
| Order dep detection | None | None | `--gtest_shuffle` | same |
| Coverage excludes | 5 directories | 5 directories | 5 directories | 1 (fuzz only) |
