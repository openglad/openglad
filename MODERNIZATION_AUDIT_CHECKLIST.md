# Modernization Plan Audit Checklist

Audit date: 2026-02-11
Branch: `cpp-modernization-plan`
Plan source: `MODERNIZATION_PLAN.md`

## Section-by-section status

1. Current State Assessment: informational baseline, no implementation item.
2. Module Decomposition Plan: **DONE** for target map and boundary scaffolding.
3. Modern C++ Adoption Plan: **DONE** for all Phase 2 ownership/lifetime items.
4. Build System Modernization Plan: **DONE** (module targets, warnings/sanitizers, install/export, module test binaries, presets).
5. Code Organization and Include Hygiene: **DONE** (file splits, no project `using namespace`, transitional `graph.h` allowlist guard).
6. Error Handling Strategy: **DONE** (typed IO/load error enums + structured logging paths).
7. Memory Management Modernization: **DONE** (RAII/smart-pointer migrations and ownership tests).
8. Global State Reduction and Encapsulation: **DONE** (GameContext expansion + service interfaces + routed callsites).
9. Testing Infrastructure Improvements: **DONE** (module test binaries, fixture hooks, deterministic/context tests, serialization compatibility).
10. Dependency Management Strategy: **DONE** for optional vcpkg/conan path and isolated external targets.
11. Priority Ordering and Dependency Graph: **DONE** (all phase deliverables below verified).
12. Risk Assessment: informational controls, reflected in shipped tests/sanitizer path.

## Phase 0 (Preparation)

- [x] CMake presets (`dev-debug`, `dev-release`, `ci-test`, `ci-asan`, `web-emscripten`): `CMakePresets.json`
- [x] Warnings and sanitizer toggles: `CMakeLists.txt` (`project_warnings`, `project_sanitizers`)
- [x] Baseline metrics + guardrails:
  - `scripts/collect_baseline_metrics.sh`
  - `BASELINE_METRICS.md`
  - CI artifact upload in `.github/workflows/test.yml`
- [x] ASan/UBSan CI path: `.github/workflows/test.yml` (`asan` job + `ci-asan` preset)

## Phase 1 (Architecture foundations)

- [x] Target-based internal module libraries:
  - `og_core`, `og_data`, `og_entities`, `og_runtime`, `og_render`, `og_input`, `og_ui`, `og_platform` in `CMakeLists.txt`
- [x] Header hygiene guardrail to stop new `graph.h` spread:
  - `scripts/check_graph_includes.sh`
  - `cmake/graph_h_allowlist.txt`
  - `check_graph_h_includes` target in `CMakeLists.txt`
- [x] GameContext expansion in touched modules:
  - service-backed accessors in `src/game_context.h`/`src/game_context.cpp`
  - routed uses in `src/input.cpp`, `src/view.cpp`, `src/button.cpp`, `src/picker_main_menu.cpp`, `src/picker_accessible_levels.cpp`, `src/game_loop.cpp`

## Phase 2 (Ownership and lifetime safety)

- [x] `SaveData::team_list` ownership -> `std::array<std::unique_ptr<guy>, ...>`: `src/save_data.h`, `src/save_data.cpp`
- [x] `walker::myguy` ownership contradiction resolved (`owned_myguy_` + explicit transfer helpers): `src/walker.h`, `src/walker.cpp`
- [x] `viewscreen`/`radar`/`video` RAII:
  - `std::unique_ptr<radar>` in `src/view.h`/`src/view.cpp`
  - `std::vector<unsigned char> bmp` in `src/radar.h`/`src/radar.cpp`
  - `std::vector<Uint8>` fade buffers + explicit precondition checks in `src/video.cpp`
- [x] `LevelEditorData` owned pointers -> `std::unique_ptr`: `src/level_editor.cpp`
- [x] Loader unique ownership API present (`create_walker_owned`): `src/gloader.h`, `src/gloader.cpp`

## Phase 3 (Global state and module boundaries)

- [x] Service interfaces for config/input/render context: `src/game_context.h`, `src/game_context.cpp`
- [x] Reduced direct `myscreen/theprefs/cfg` accesses in migrated paths (GameContext-backed callsites above)
- [x] `walker.cpp` split (`walker_movement.cpp`, `walker_combat.cpp`, `walker_specials.cpp`, `walker_pathing.cpp`)
- [x] `picker.cpp` split (`picker_main_menu.cpp`, `picker_team_build.cpp`, `picker_dialogs.cpp`, `picker_input.cpp`, `picker_accessible_levels.cpp`)
- [x] `level_editor.cpp` split (`level_editor_ui.cpp`, `level_editor_tools.cpp`, `level_editor_file_ops.cpp`)
- [x] `using namespace` ban satisfied in project code (remaining uses only in external vendored code)

## Phase 4 (Error model and API cleanup)

- [x] Result/exception boundaries standardized on typed error enums for data/load/save paths:
  - `SaveDataIoError` (`src/save_data.h/.cpp`)
  - `CampaignData::IoError` and `LevelData::IoError` (`src/level_data.h/.cpp`)
  - `LoadSavedGameError` (`src/game.cpp`)
  - `ScenarioTitleError` (`src/screen.cpp`)
- [x] `bool/short` status APIs have typed counterparts in key subsystems (`*_with_error` APIs above)
- [x] `video` ASSERT replacement done in fade path (`src/video.cpp`)
- [x] Logging/diagnostics normalized with structured `LogError` records in IO/campaign/save/load paths (`src/io.cpp`, `src/save_data.cpp`, `src/level_data.cpp`, `src/game.cpp`, `src/screen.cpp`)

## Phase 5 (Test and dependency ecosystem)

- [x] Test binaries split by module:
  - `og_data_tests`, `og_runtime_tests` (plus full `openglad_test`) in `CMakeLists.txt`
- [x] Fixture-based deterministic tests/hooks present:
  - framework setup/teardown helpers (`tests/test_framework.h/.cpp`)
  - deterministic/context fixtures and ownership fixtures (`tests/test_game_context.cpp`, `tests/test_picker_ownership_fixture.cpp`)
- [x] Install/export/package rules: `CMakeLists.txt`, `cmake/OpenGladConfig.cmake.in`
- [x] Optional vcpkg/conan path: `CMakePresets.json`
- [x] Serialization compatibility tests:
  - `tests/test_save_data_versions.cpp`
  - compatibility/error-path coverage in save/io tests

## Final audit result

All actionable plan items for Phases 0-5 are implemented and verified in code.
