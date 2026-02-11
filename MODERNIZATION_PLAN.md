# OpenGlad Modernization and Refactor Plan

## Scope and Intent
This document is an execution-ready modernization plan for the OpenGlad codebase in `/home/ubuntu/openglad`. It is based on direct inspection of current source/build/test files and is designed to improve maintainability, modularity, safety, and iteration speed while minimizing behavioral regressions.

The plan is organized into 12 required sections and includes concrete file/line references for each major recommendation.

---

## 1) Current State Assessment

### 1.1 Language standard and coding model
- C++ standard is already set to C++20 (`CMakeLists.txt:4-7`), with C11 for vendored C libraries (`CMakeLists.txt:9-11`).
- The codebase is hybrid-modern:
  - Modern features already present: `std::unique_ptr`, `std::array`, `std::span`, `std::format`, `std::erase_if` (`src/screen.cpp:153`, `src/video.h:147-152`, `src/radar.cpp:181`, `src/help.cpp:783`, `src/screen.cpp:820-826`).
  - Legacy patterns remain dominant in high-churn gameplay/UI code: raw pointers, globals, large translation units, manual memory ownership (`src/picker.cpp:96-154`, `src/view.h:126`, `src/save_data.h:49`, `src/walker.cpp:197-214`).

### 1.2 Build system and target structure
- Single top-level CMake file with one monolithic source list (`CMakeLists.txt:33-75`) compiled repeatedly into:
  - `openglad` (`CMakeLists.txt:284`)
  - `openscen` (`CMakeLists.txt:289-292`)
  - `openglad_test` (`CMakeLists.txt:410-418`)
  - `play`/Emscripten (`CMakeLists.txt:445-466`)
- Vendored third-party code is compiled directly into every executable (`CMakeLists.txt:77-219`, `410-415`, `445`).
- Warning policy suppresses external warnings via `-w`, but internal warnings are also weakly constrained (`-Wno-parentheses`) (`CMakeLists.txt:270`, `273-275`).
- No install/export/package rules detected (`CMakeLists.txt` has no `install(` / `export(` / `CPack`).

### 1.3 Architecture and coupling
- Heavy umbrella include pattern:
  - `graph.h` includes most engine/game headers (`src/graph.h:19-37`), then many source files include `graph.h` directly (broadly across `src` and `tests`).
  - `base.h` also aggregates many includes and globals (`src/base.h:27-40`, `86`).
- Core classes (`screen`, `walker`, `viewscreen`, `video`) still combine orchestration + domain logic + rendering concerns.
- Very large files indicate low cohesion and refactor pressure points:
  - `src/walker.cpp` (~4.7k lines)
  - `src/picker.cpp` (~4.4k)
  - `src/level_editor.cpp` (~4.3k)
  - `src/view.cpp` (~2.2k)

### 1.4 Dependency management
- Internal vendoring of YAML/PhysFS/libzip/zlib/micropather/yam under `src/external` (`CMakeLists.txt:77-219`).
- System dependencies resolved via pkg-config SDL2/SDL2_mixer (`CMakeLists.txt:266-270`).
- No `conanfile.*`/`vcpkg.json`/FetchContent/CPM present.

### 1.5 Testing state
- Custom test framework with global registry macros (`tests/test_framework.h:19-35`, `tests/test_framework.cpp:13-20`).
- Test binary links full production source set + external sources + tests (`CMakeLists.txt:410-415`).
- Test setup uses production globals and process-level forced exit:
  - globals allocated in `test_main` (`tests/test_main.cpp:42-44`)
  - `_exit(...)` used to bypass teardown hangs (`tests/test_main.cpp:48-57`).
- Many tests depend on `extern screen* myscreen`, indicating low isolation (e.g., `tests/test_help.cpp:9`, `tests/test_walker_combat.cpp:8`, and many others).

### 1.6 Code style/tooling
- No repo-level `.clang-format` or `.clang-tidy` integration found.
- Naming and style are mixed: legacy macro-heavy C-style and modern C++ coexist.
- Namespace usage is minimal in project code (except external and a `using namespace micropather` in `src/walker.cpp:1385`).

---

## 2) Module Decomposition Plan (Target Architecture)

### 2.1 Proposed module map
Create explicit library boundaries with stable interfaces and keep executables thin.

1. `og_core` (platform-neutral domain primitives)
- Candidate files: `src/combat_math.cpp`, `src/stats.cpp`, `src/util.cpp`, data model headers.
- Public API: math/stat/rules/value objects only.

2. `og_data` (campaign/level/save/config serialization)
- Candidate files: `src/level_data.cpp`, `src/save_data.cpp`, `src/gparser.cpp`, part of `src/io.cpp` for file-format persistence.
- Public API:
  - `CampaignRepository`
  - `LevelRepository`
  - `SaveRepository`
  - `ConfigRepository`

3. `og_entities` (runtime object model and combat behavior)
- Candidate files: `src/walker.cpp`, `src/living.cpp`, `src/weap.cpp`, `src/treasure.cpp`, `src/effect.cpp`, `src/guy.cpp`.
- Public API:
  - `Entity`, `LivingEntity`, `ProjectileEntity`, etc. (can be wrappers over existing classes initially)
  - action interfaces + stat transfer APIs.

4. `og_runtime` (game loop, orchestration, state machine)
- Candidate files: `src/game_loop.cpp`, `src/game.cpp`, selected parts of `src/screen.cpp`, `src/glad.cpp` (state orchestration only).
- Public API:
  - `GameSession`
  - `FrameRunner`
  - `GameContext` integration.

5. `og_render` (video, view, radar, palette, text)
- Candidate files: `src/video.cpp`, `src/sai2x.cpp`, `src/view.cpp`, `src/radar.cpp`, `src/pal32.cpp`, `src/text.cpp`.
- Public API:
  - `IRenderDevice`
  - `IHudRenderer`
  - `IRadarRenderer`.

6. `og_input` (input mapping + state capture)
- Candidate files: `src/input.cpp`, `src/button.cpp`.
- Public API:
  - `InputSnapshot`
  - `InputMapper`
  - event pumps.

7. `og_ui` (menus, picker, help, results, editor UI glue)
- Candidate files: `src/picker.cpp`, `src/help.cpp`, `src/results_screen.cpp`, `src/level_editor.cpp`, `src/campaign_picker.cpp`, `src/level_picker.cpp`.
- Public API:
  - menu controllers and command invocations.

8. `og_platform` (filesystem + SDL/PhysFS integration)
- Candidate files: `src/io.cpp` platform blocks, mount logic.
- Public API:
  - `FilesystemService`
  - `CampaignMountService`.

### 2.2 Boundary rules
- `og_entities` must not include rendering headers (`video.h`, `view.h`); use callbacks/interfaces.
- `og_data` must not depend on `screen` or rendering.
- `og_ui` talks to `og_runtime` through command interfaces, not globals.
- `graph.h` and `base.h` are transitional-only headers; no new includes of either after Phase 2.

### 2.3 Concrete current coupling to break first
- `graph.h` umbrella coupling (`src/graph.h:19-37`) used across runtime and tests.
- `SaveData` includes `walker.h` at bottom (`src/save_data.h:69`) and stores raw `guy*` (`:49`), tying persistence to runtime objects.
- `viewscreen` pulls global `theprefs` and `myscreen` (`src/view.cpp:133`, `164`, `183`).
- `picker` directly deletes `myscreen` and `theprefs` (`src/picker.cpp:254`, `3528`), creating lifecycle entanglement.

---

## 3) Modern C++ Adoption Plan (Specific Patterns and Files)

### 3.1 Ownership and RAII conversions
1. `viewscreen::myradar`
- Current: raw pointer member + manual `new/delete` (`src/view.h:126`, `src/view.cpp:183`, `198-200`).
- Refactor: `std::unique_ptr<radar> myradar;` + `std::make_unique` in ctor.

2. `radar::bmp`
- Current: `unsigned char* bmp` + manual `new[]/delete[]` (`src/radar.h:46`, `src/radar.cpp:112-115`, `123-127`).
- Refactor: `std::vector<unsigned char> bmp;` with resize.

3. `video::FadeBetween` temporary buffers
- Current: raw `new[]` buffers (`src/video.cpp:1923-1924`, `1965-1966`).
- Refactor: `std::vector<Uint8> colorsf(size), colorst(size);`.

4. `loader::create_walker`
- Current: returns raw `walker*` and manually `new` derived types (`src/gloader.cpp:784-823`, `801-809`).
- Refactor: return `std::unique_ptr<walker>`; overload transitional API if needed.

5. `SaveData::team_list`
- Current: owning raw array `guy* team_list[MAX_TEAM_SIZE]` (`src/save_data.h:49`) with manual deletes (`src/save_data.cpp:56-62`, `81-85`, `185-189`, `430-434`).
- Refactor: `std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE>` or `std::vector<guy>` (preferred if variable size semantics fit).

6. `LevelEditorData` ownership
- Current: `campaign`/`level` manually allocated and deleted (`src/level_editor.cpp:983-985`, `1091-1095`).
- Refactor: `std::unique_ptr<CampaignData>`, `std::unique_ptr<LevelData>` or direct values if cheap.

### 3.2 Eliminate accidental ownership ambiguity
- `walker::myguy` is documented non-owning (`src/walker.h:141`) but deleted in destructor (`src/walker.cpp:212-214`).
- `walker::transfer_stats` allocates new `guy` (`src/walker.cpp:4335-4359`).
- Plan: split into explicit modes:
  - `guy* myguy_view` (non-owning), and
  - optional `std::unique_ptr<guy> owned_guy` for summoned/transformed clones,
  - or always value-copy and let owner container manage lifetime.

### 3.3 Replace legacy arrays/macros with typed constants
- Replace remaining macro constants in gameplay/UI (`src/picker.cpp:42-60`, `src/base.h:92-96`, etc.) with `inline constexpr` or `enum class` where domain-typed.
- Preserve serialized values where file format depends on exact integer layout.

### 3.4 Prefer STL algorithms and views
- Already using `std::erase_if` in `screen` (`src/screen.cpp:820-826`); extend to loops that filter/remove manually in:
  - `src/picker.cpp` button/object cleanup blocks,
  - `src/level_data.cpp` object iteration sections,
  - `src/save_data.cpp` copy/reset loops.

### 3.5 Structured bindings / stronger types
- Introduce structured bindings for map iteration in save/campaign serialization.
- Replace magic integral team/order/family arguments with wrappers:
  - `TeamId`, `FamilyId`, `Order` (already enum class), reducing accidental cross-use.

---

## 4) Build System Modernization Plan (CMake)

### 4.1 Convert to target-based architecture
Current issue: each executable recompiles almost entire engine (`CMakeLists.txt:284-290`, `410-415`, `445`).

Plan:
1. Create internal libs:
- `add_library(og_core ...)`
- `add_library(og_data ...)`
- `add_library(og_entities ...)`
- `add_library(og_runtime ...)`
- `add_library(og_render ...)`
- `add_library(og_input ...)`
- `add_library(og_ui ...)`
- `add_library(og_platform ...)`

2. Executables become thin:
- `openglad` links needed libs.
- `openscen` links runtime+editor UI subset.
- `openglad_test` links libraries + test main.

3. Extract third-party libs into separate CMake targets:
- `og_ext_yaml`, `og_ext_physfs`, `og_ext_libzip`, `og_ext_micropather`, `og_ext_yam`.
- Mark includes/system warnings at target level instead of global source properties.

### 4.2 Warnings and diagnostics policy
- Remove blanket suppression (`CMakeLists.txt:270`, `273-275`) for project code.
- Introduce `project_warnings` INTERFACE target:
  - GCC/Clang: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
  - Keep third-party warning suppression isolated to external targets only.

### 4.3 Install/export/package
- Add install rules for runtime binaries and assets:
  - `install(TARGETS openglad openscen ...)`
  - `install(DIRECTORY cfg pix sound builtin ...)`
- Add export set and package config for downstream packaging.

### 4.4 Testing integration modernization
- Keep `ctest` registration but split test executable(s) by module to reduce link/compile time.
- Enable sanitizer presets (`ASan/UBSan`) for CI debug matrix.

### 4.5 Presets and reproducibility
- Add `CMakePresets.json` profiles:
  - `dev-debug`, `dev-release`, `ci-test`, `ci-asan`, `web-emscripten`.
- Replace root-binary copy pattern in scripts (`scripts/build_native.sh:37-39`, `scripts/build_test.sh:36-37`) with preset-driven output directories.

---

## 5) Code Organization and Include Hygiene

### 5.1 Remove umbrella includes from most TUs
- `graph.h` (`src/graph.h:19-37`) and `base.h` (`src/base.h:27-40`) currently amplify compile-time coupling and hidden dependencies.

Plan:
1. For each `.cpp`, replace `#include "graph.h"` with minimal required headers.
2. Keep `graph.h` for transitional compatibility only.
3. Add include-what-you-use checks (or equivalent manual check in CI).

### 5.2 Header/source cleanup actions
- Move implementation-only includes out of headers:
  - e.g., `src/view.h` currently includes `level_data.h` (`src/view.h:22`) and `base.h` (`:21`); reduce to forward declarations where possible.
- Remove header cycles and late include hacks:
  - `src/save_data.h` includes `walker.h` at end (`:69`), indicating design leakage.

### 5.3 Namespace introduction
- Introduce project namespace `openglad` in new/updated files first.
- Avoid large bang rename; do staged migration by module.
- Ban `using namespace` in project source (replace `using namespace micropather;` in `src/walker.cpp:1385` with qualified usage).

### 5.4 File decomposition for oversized units
Prioritize splits by behavioral seams:
- `src/walker.cpp`: split into `walker_movement.cpp`, `walker_combat.cpp`, `walker_specials.cpp`, `walker_pathing.cpp`.
- `src/picker.cpp`: split into `picker_main_menu.cpp`, `picker_team_build.cpp`, `picker_dialogs.cpp`, `picker_input.cpp`.
- `src/level_editor.cpp`: split into `level_editor_ui.cpp`, `level_editor_tools.cpp`, `level_editor_file_ops.cpp`.

---

## 6) Error Handling Strategy

### 6.1 Current inconsistency
- Startup/IO throws exceptions (`src/io.cpp:507`, `526`); `main` catches runtime_error (`src/glad.cpp:198-255`).
- Most gameplay/data flows return `0/1` and log side effects (`src/save_data.cpp`, `src/level_data.cpp`, `src/screen.cpp`).
- `ASSERT` macro in `video` converts failed preconditions to silent returns (`src/video.cpp:36`, usage `1899-1919`).

### 6.2 Target model
Use a two-tier model:
1. Boundary layer (startup/file/mount/resource creation): exceptions allowed.
2. Runtime simulation/gameplay hot paths: `Result<T, ErrorCode>` (or lightweight status type) + explicit handling.

### 6.3 Concrete migrations
- Replace `bool`/`short` status returns in serialization with typed errors:
  - `SaveData::load/save` (`src/save_data.cpp:94`, `456`)
  - `CampaignData::load/save` and `LevelData::load/save` (`src/level_data.cpp:54`, `118`, `1215`, `1332`).
- Replace `ASSERT` macro in `video` with explicit precondition checks returning detailed errors/log context.
- Standardize `LogError` plus structured error propagation object.

---

## 7) Memory Management Modernization

### 7.1 High-priority ownership hotspots
1. `SaveData` team roster ownership (`src/save_data.h:49`, deletes in `src/save_data.cpp:56-62`, `81-85`, `185-189`, `430-434`).
2. `walker`/`guy` ownership contradiction (`src/walker.h:141`, `src/walker.cpp:212-214`, `4335-4359`).
3. `picker` lifecycle and manual deletes (`src/picker.cpp:195-215`, `241-258`, repeated `delete localbuttons` across file).
4. `LevelEditorData` owned pointers (`src/level_editor.cpp:983-985`, `1091-1095`).
5. `video`/`radar` heap arrays (`src/video.cpp:1923-1924`, `1965-1966`; `src/radar.cpp:112-115`, `123-127`).

### 7.2 Migration pattern
- Convert raw owning pointers first; preserve non-owning observers as raw pointers or `std::observer_ptr` equivalent.
- Ban new raw owning allocations in project code after Phase 2.
- Add static analysis/lint rule to flag `new`/`delete` outside constrained wrappers.

### 7.3 Safety verification
- Add focused regression tests for ownership transitions:
  - load/save team lifecycle,
  - transform/summon/death object cleanup,
  - picker open/close loops.
- Run ASan+UBSan in CI on at least Linux debug matrix.

---

## 8) Global State Reduction and Encapsulation

### 8.1 Current globals to encapsulate
- `screen* myscreen` declared globally (`src/base.h:86`, defined `src/glad.cpp:27`).
- `options* theprefs` global (`src/view.cpp:133`, external use e.g. `src/glad.cpp:99`, `src/level_editor.cpp:99`).
- Global config singleton `cfg` (`src/gparser.h:37`, `src/gparser.cpp:40`).
- Global renderer pointer `Screen* E_Screen` (`src/video.cpp:41`).
- Input/menu globals (`src/input.cpp:44-80`, `95-146`; `src/picker.cpp:96-154`).

### 8.2 Existing modernization foothold
- `GameContext` exists as additive wrapper over globals (`src/game_context.h:1-131`, `src/game_context.cpp:82-111`) but not yet pervasive.

### 8.3 Encapsulation plan
1. Expand `GameContext` into the canonical dependency source for runtime/UI.
2. Replace direct extern usage in migrated modules with injected context/service references.
3. Introduce dedicated state objects:
- `InputStateStore`
- `PickerState`
- `RenderContext`
4. Keep compatibility bridge for untouched legacy code during migration.

### 8.4 Milestones
- Milestone A: no new `extern` globals in production code.
- Milestone B: `myscreen` access only in app/bootstrap layer.
- Milestone C: test code uses harness-provided context fixture, not direct global `extern`.

---

## 9) Testing Infrastructure Improvements

### 9.1 Immediate issues
- Custom framework has no fixtures/typed setup/teardown (`tests/test_framework.h:23-68`).
- Global process state and forced `_exit` (`tests/test_main.cpp:48-57`) masks destructor/resource issues.
- Monolithic test binary (`CMakeLists.txt:410-415`) increases coupling and build time.

### 9.2 Target test architecture
1. Introduce module-level test binaries:
- `og_core_tests`, `og_data_tests`, `og_entities_tests`, `og_ui_tests`.
2. Keep legacy integration suite separately (`openglad_integration_tests`) for full-game scenarios.
3. Add deterministic fixtures:
- context fixture with seeded RNG (`GameContext`, `SeededRandom` in `src/game_context.h:53-65`).
4. Add helpers for temp filesystem sandboxing (especially for save/campaign tests).

### 9.3 Framework migration strategy
- Phase 1: keep current framework, add fixture helpers and teardown discipline.
- Phase 2: optionally migrate to Catch2 or GoogleTest; if avoided, evolve custom framework with:
  - per-test setup/teardown callbacks,
  - death tests/timeouts,
  - expected-failure tagging.

### 9.4 Critical test additions
- Ownership/lifetime tests around `walker` and `SaveData`.
- Serialization compatibility tests for save versions (extend `tests/test_save_data_versions.cpp`).
- Editor save/load/remount behavior around `LevelEditorData::saveCampaign*` (`src/level_editor.cpp:1125-1145`).
- Exception path tests for `io_init` mount failures (`src/io.cpp:503-527`).

---

## 10) Dependency Management Strategy

### 10.1 Current model
- Strongly vendored third-party source compilation (`CMakeLists.txt:77-219`).
- System SDL dependencies from pkg-config (`CMakeLists.txt:266-270`).

### 10.2 Recommended hybrid approach
1. Keep vendored libs for deterministic builds initially.
2. Isolate each vendor as its own CMake target to avoid full recompilation in every executable.
3. Add `option(USE_SYSTEM_<LIB>)` toggles for YAML/PhysFS/libzip/zlib where feasible.
4. Introduce package manager only after target split stabilizes:
- Preferred: `vcpkg` for cross-platform ease and CMake integration.
- Alternative: Conan if lockfile/reproducibility requirements become stronger.

### 10.3 Concrete deliverables
- `third_party/` CMake wrappers with consistent include/link semantics.
- SBOM/dependency manifest file listing exact vendored versions and patch status.
- CI matrix variant building with system packages where supported.

---

## 11) Priority Ordering and Dependency Graph

## Phase 0 (Preparation, low risk)
1. Add baseline metrics and guardrails
- compile time, binary size, test duration, crash/leak baseline.
2. Add CMake presets + stricter warnings for project code only.
3. Add ASan/UBSan CI job.

Dependencies: none.
Impact: high visibility, low regression risk.

## Phase 1 (Architecture foundations)
1. Create target-based CMake libraries without behavioral change.
2. Introduce minimal header hygiene policy and stop new `graph.h` includes.
3. Expand `GameContext` usage in newly touched files.

Dependencies: Phase 0.
Impact: unlocks all later refactors.

## Phase 2 (Ownership and lifetime safety)
1. Refactor `SaveData::team_list` ownership model.
2. Resolve `walker::myguy` ownership contradiction.
3. Convert `viewscreen/radar/video` raw heap buffers and pointers to RAII containers.
4. Convert `LevelEditorData` owned pointers to smart pointers/values.

Dependencies: Phase 1 for modular compile and easier testing.
Impact: highest reliability gain; medium regression risk.

## Phase 3 (Global state and module boundary reduction)
1. Introduce service interfaces for config/input/render context.
2. Reduce direct accesses to `myscreen`, `theprefs`, `cfg`.
3. Split `walker.cpp`, `picker.cpp`, `level_editor.cpp` by responsibility.

Dependencies: Phase 2 largely complete.
Impact: maintainability and velocity gain; higher merge/conflict risk.

## Phase 4 (Error model and API cleanup)
1. Standardize `Result`/exception boundaries.
2. Replace `0/1` status and macro assertions in key subsystems.
3. Normalize logging and diagnostics payloads.

Dependencies: Phase 2-3.
Impact: correctness and debuggability.

## Phase 5 (Test and dependency ecosystem)
1. Split test binaries by module.
2. Add fixture-based deterministic tests.
3. Introduce optional vcpkg/conan path and install/export rules.

Dependencies: Phases 1-4.
Impact: CI speed/reliability and distribution readiness.

---

## 12) Risk Assessment

### 12.1 Low-risk refactors (safe first)
- CMake target decomposition with no source logic changes.
- Replace local temporary raw arrays with vectors (`src/video.cpp:1923-1924`).
- `LevelEditorData` raw ownership -> `unique_ptr` (`src/level_editor.cpp:983-985`, `1091-1095`).
- Include hygiene in leaf modules.

### 12.2 Medium-risk refactors
- `SaveData` container ownership rewrite (`src/save_data.h:49`, `src/save_data.cpp` lifecycle methods).
- `loader::create_walker` return type migration (`src/gloader.cpp:784-823`) due to call-site spread.
- `viewscreen` and radar ownership refactor (`src/view.*`, `src/radar.*`).

### 12.3 High-risk refactors (need staged rollout)
- `walker`/`guy` lifetime model resolution (`src/walker.h:141`, `src/walker.cpp:212-214`, `4335-4359`).
- Global-state removal in menu/runtime flows (`src/picker.cpp`, `src/glad.cpp`, `src/input.cpp`).
- Large-file decomposition for `walker/picker/level_editor` due to hidden side effects.

### 12.4 Risk controls
1. Introduce characterization tests before each high-risk migration.
2. Use feature flags/adapters for temporary dual API support.
3. Land refactors in small PR-sized slices (one ownership domain at a time).
4. Require sanitizer-clean CI before merge for memory-related phases.
5. Freeze file-format compatibility with golden save/campaign fixtures.

---

## Execution Checklist (Condensed)

1. Establish guardrails: presets, warnings, sanitizers, baseline metrics.
2. Split CMake into internal libraries and vendor targets.
3. Stop new umbrella includes; begin include minimization.
4. Fix ownership in `SaveData`, `walker/guy`, `view/radar/video`, `level_editor`.
5. Expand `GameContext`, reduce `extern` globals.
6. Split monolithic files by behavior.
7. Standardize error handling (`Result` + exception boundaries).
8. Modernize tests (fixtures, module binaries, deterministic harness).
9. Add install/export and optional package manager path.

This ordering maximizes early confidence and minimizes the chance of destabilizing gameplay while still delivering substantial modernization in each phase.
