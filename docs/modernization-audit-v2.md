# OpenGlad Modernization Audit v2

Branch: `cpp-modernization-plan`  
Date: 2026-02-12  
Scope audited: all files under `src/` and `tests/`.

## Audit Method
- Full tree scan of `src/` and `tests/` with pattern checks for ownership, casts, macros/magic numbers, globals, loop style, and C-APIs.
- Manual deep review of high-impact runtime files (`src/data/level_data.cpp`, `src/render/text.cpp`, `src/render/video.cpp`, `src/platform/io.cpp`, `src/ui/picker.cpp`, `src/input/button.cpp`, `src/ui/level_picker.cpp`, `src/ui/campaign_picker.cpp`).
- Note on vendored code: `src/external/**` is third-party upstream; modernization inside vendor trees is intentionally out-of-scope unless OpenGlad-specific wrapper bugs are present.

## What Was Implemented In This Pass
- `src/data/level_data.cpp`
  - Replaced unchecked scenario header/version reads with exact-read checks and explicit error assignment.
  - Priority addressed: **High** safety/corruption handling.
- `src/render/text.cpp`
  - Fixed null `begin` handling in `input_string*()` initialization (`snprintf(..., begin ? begin : "")`).
  - Replaced hard-coded buffer loop bound `100` with `sizeof(editstring)` loops.
  - Priority addressed: **High** correctness and buffer safety.
- `src/render/video.cpp`
  - Replaced C-style pointer casts with explicit `static_cast`/`reinterpret_cast` in pixel writes.
  - Priority addressed: **Medium** type safety/readability.
- `src/platform/io.cpp`
  - Replaced C-style `void*` conversion in RWops handlers with `static_cast`.
- `tests/*`
  - Replaced callback payload C-style casts (`(Type*)data`) with `static_cast<Type*>(data)` across menu/picker thread callback tests.
  - Replaced `living` C-style casts in `tests/test_living_combat.cpp` and `tests/test_living_funcs.cpp`.

## Priority Summary (Remaining)
- **High**
  - Global ownership/lifetime (`myscreen`, menu button globals).
  - Test ownership debt (many `new`/`delete` pairs, leak/UAF risk in failure paths).
  - Macro-heavy ID/type domains that should be `enum class`/`constexpr`.
  - Global mutable state and `extern` coupling.
- **Medium**
  - Legacy loop/index style where range-for/algorithms are clearer.
  - Header hygiene and dependency reduction in core headers.
  - Additional `[[nodiscard]]` and const-correctness tightening.
- **Low**
  - Dead/commented legacy blocks and naming consistency cleanup.

## File-By-File Findings Grouped By Category

### 1. Raw Pointers / Ownership / RAII
- `src/glad.cpp` (High): raw owning `myscreen = new screen(1)`; ownership lifecycle is external/manual.
  - Recommendation: centralize ownership in a context-owned `std::unique_ptr<screen>` and keep `myscreen` as non-owning alias only during migration.
- `src/ui/picker.cpp` (High): manual `delete myscreen` and manual deletion of `allbuttons[]`.
  - Recommendation: move teardown into explicit owner object (`PickerSession`) with deterministic RAII.
- `src/input/button.cpp` + `src/input/button.h` (High): `allbuttons` is a raw global pointer array with manual delete/new lifecycle.
  - Recommendation: `std::array<std::unique_ptr<vbutton>, MAX_BUTTONS>` with non-owning accessors for legacy callers.
- `tests/test_main.cpp` (High): raw owning setup for `myscreen`.
  - Recommendation: test fixture-level `std::unique_ptr<screen>` ownership.
- `tests/test_walker_combat.cpp`, `tests/test_walker_specials.cpp`, `tests/test_walker_movement.cpp`, `tests/test_stats_commands.cpp`, `tests/test_walker_extended.cpp`, `tests/test_walker_death.cpp`, `tests/test_living_combat.cpp`, `tests/test_living_funcs.cpp`, `tests/test_gloader_funcs.cpp`, `tests/test_treasure_eat.cpp`, `tests/test_weap_behavior.cpp` (High): heavy manual `new`/`delete` patterns.
  - Recommendation: migrate local test allocations to `std::unique_ptr` and pass raw borrows with `.get()` where APIs require.

### 2. C-Style Patterns and Unsafe Casts
- `src/render/video.cpp` (Medium): low-level pixel code still relies on aliasing-sensitive reinterpret casts and manual pointer arithmetic.
  - Recommendation: consider typed row helpers (`std::span<std::byte>`/format-specific accessors) to confine unsafe operations.
- `src/render/view.cpp` (Low): legacy commented C-style allocation remains.
  - Recommendation: remove dead commented allocation path.
- `tests/test_external_zlib*.cpp`, `tests/test_external_yaml_api_builder.cpp` (Low): vendor API interop requires casts.
  - Recommendation: keep as interop boundary; prefer explicit `reinterpret_cast` for non-`void*` conversions.

### 3. Missing RAII / Manual Resource Handling
- `src/ui/picker.cpp` + `src/input/button.cpp` (High): manual menu object lifecycle.
  - Recommendation: RAII-managed menu model object.
- `src/platform/io.cpp` (Medium): RWops handlers are safe now, but wider I/O paths still rely on imperative open/read/close patterns.
  - Recommendation: continue rolling out `RwopsPtr` wrappers uniformly.

### 4. Magic Numbers / Macro IDs
- `src/input/input.h`, `src/input/button.h`, `src/pixdefs.h`, `src/soundob.h`, `src/view_sizes.h`, `src/runtime/screen.cpp`, `src/ui/picker.cpp`, `src/ui/level_editor.cpp`, `src/render/view.cpp` (High): large macro-constant surfaces, many represent type domains.
  - Recommendation: replace semantic sets with `enum class` + `constexpr` constants scoped by subsystem.
- `src/entities/guy.cpp` + `src/ui/picker.cpp` + `src/ui/picker_team_build.cpp` (Medium): duplicated `RAISE` constant and menu return-code defines.
  - Recommendation: central `constexpr` constants in shared header.

### 5. Global Mutable State / Coupling
- `src/base.h`, `src/glad.cpp`, `src/ui/picker.cpp`, `src/input/input.cpp`, `src/runtime/game_context.cpp`, `src/test_trace.cpp` (High): broad global mutable state (`myscreen`, button arrays, input arrays, trace buffer).
  - Recommendation: push remaining mutable state into `GameContext` or subsystem-local state structs.
- `tests/*.cpp` (High): extensive `extern` dependency on runtime globals.
  - Recommendation: fixture object exposing explicit setup handles instead of direct global pokes.

### 6. Old-Style Loops / Algorithm Opportunities
- `src/input/input.cpp`, `src/data/save_data.cpp`, `src/data/level_data.cpp`, `src/ui/level_picker.cpp`, `src/ui/results_screen.cpp`, `src/platform/io.cpp`, `src/runtime/game.cpp`, `src/runtime/screen.cpp` (Medium): frequent index loops over fixed-size arrays and container sizes.
  - Recommendation: range-for and `std::ranges`/`std::algorithm` where mutation semantics permit.

### 7. Header Hygiene / Include Coupling
- `src/base.h`, `src/graph.h`, `src/runtime/screen.h`, `src/input/input.h`, `src/input/button.h` (Medium): heavy include fan-in and global extern exposure.
  - Recommendation: forward declarations + split lightweight API headers from implementation-heavy includes.

### 8. Const Correctness / API Clarity
- `src/ui/level_picker.cpp`, `src/ui/campaign_picker.cpp`, `src/ui/picker_team_build.cpp`, `src/runtime/screen.cpp` (Medium): parameters and local temporaries can be further `const`-qualified; mutable globals still drive signatures.
  - Recommendation: tighten const across helper methods and introduce immutable view models for picker/editor UI.

### 9. Error Handling Consistency
- `src/data/level_data.cpp` (Medium): improved but still mixed bool returns and side-channel `last_io_error_`.
  - Recommendation: migrate to single return type (`expected` style) for parse/write APIs.
- `src/platform/io.cpp` (Medium): mostly structured now; continue converting remaining legacy call sites to typed error returns.

### 10. Dead Code / Legacy Branches
- `src/render/view.cpp`, `src/ui/help.cpp`, `src/ui/intro.cpp`, `src/entities/walker.cpp`, `src/entities/living.cpp` (Low): stale comments and disabled historical blocks.
  - Recommendation: remove or isolate behind documented `#if` feature flags.

## Estimated Scope of Remaining Changes
- Raw pointer ownership and lifetime refactors: **~56 files** (src + tests)
- Macro-to-`constexpr`/`enum class` modernization: **~36 files**
- Global state decoupling (`extern` reduction): **~110 files touched across declarations/usages**
- Loop/algorithm modernization: **~71 files**
- C-style cast cleanup (remaining non-vendor): **very low, ~1 src file + interop tests**
- Header hygiene passes: **~15-25 core headers/consumers**

## Recommended Execution Order
1. High-risk lifetime ownership (`myscreen`, `allbuttons`, test fixtures using raw owning pointers).
2. Global state reduction through `GameContext` and picker/menu session objects.
3. Macro domain typing (`enum class`) for input/actions/pix/sound IDs.
4. Header decoupling and include cleanup.
5. Loop/algorithm and const-correctness sweep.

## Validation Baseline For Each Batch
- `cmake --build build-test -j4`
- `ctest --test-dir build-test --output-on-failure`
- Optional: add ASan/UBSan run for ownership-heavy batches.
