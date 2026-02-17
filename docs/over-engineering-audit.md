# Over-Engineering Audit: Sim/Rendering Split

## 1. Executive Summary
The SDL/headless split achieved a real architectural goal: the codebase now has a buildable headless client and substantially fewer SDL dependencies in core simulation paths. That is a meaningful improvement.

The refactor also introduced several layers that currently add complexity without commensurate value: unused picker abstractions, broad link-time dispatch stubs, duplicated headless/non-headless object creation paths, and interface-heavy context plumbing with only one production implementation. The main theme is not "too many abstractions" in general, but "too many abstractions that are either inactive or only thin wrappers around globals." This should be simplified now, before these patterns spread.

## 2. High Priority

### H1) Dead picker state-machine abstraction (implemented but not used)
Files: `include/openglad/ui/picker_state.h:20`, `src/text_client/text_picker.cpp:19`, `src/text_client/main.cpp:253`, `CMakeLists.txt:803`

Current approach: A full `IPickerClient` interface and `run_picker()` state machine exist, plus a `TextPickerClient` implementation. However, `run_picker()` is never called anywhere, and `text_picker.cpp` contributes no used entrypoint. `openglad_text` runs a command-loop protocol from `main.cpp` instead.

Simpler alternative: Remove `picker_state.h` and `text_picker.cpp` from the build until there is a concrete integration plan. If a text picker is desired, add one explicit function (for example `run_text_picker()`) and call it from `text_client/main.cpp`.

Why simpler is better: Removes dead architecture and false signaling. Right now the repository advertises a shared picker abstraction that does not participate in runtime behavior.

### H2) Link-time dispatch became a large no-op surface with silent failure behavior
Files: `src/runtime/level_data.cpp:43`, `src/sdl_client/runtime/sdl_context_services.cpp:79`, `src/text_client/platform_headless.cpp:75`, `src/text_client/platform_headless.cpp:126`

Current approach: `LevelData` and related paths use unresolved extern hooks (`create_level_render`, `level_data_draw_impl`, etc.) implemented in SDL, while headless provides stubs that do nothing/return false/null. `platform_headless.cpp` also stubs many platform functions (campaign mount, save/load, zip/unzip, I/O, prompts) as no-ops.

Simpler alternative: Move to explicit capability boundaries with narrow APIs:
- Keep only truly required headless symbols.
- For unsupported features, return typed errors at call sites (not silent false/no-op global shims).
- For `LevelData`, pass an optional render/context service object directly instead of global link-time hooks.

Why simpler is better: Reduces global hidden behavior and avoids "compiles but does nothing" failure modes that are hard to debug and easy to misuse.

### H3) `LevelData` has duplicated headless vs SDL object-creation paths
Files: `include/openglad/data/level_data.h:148`, `src/runtime/level_data.cpp:458`, `src/runtime/level_data.cpp:525`, `include/openglad/data/gloader.h:42`, `src/runtime/gloader.cpp:812`, `src/runtime/gloader.cpp:869`

Current approach: Parallel APIs and branches exist (`add_ob` vs `add_ob_headless`, `create_walker_owned` vs `create_walker_headless`), duplicating lifecycle/wiring logic and special-case routing based on `headless_`.

Simpler alternative: Keep one object-creation path and centralize render attachment as optional behavior:
- always construct the same entity types,
- attach render only when render capability is present,
- wire sim context once.

Why simpler is better: Fewer permutations, less duplicated code, lower maintenance risk, and fewer branch-specific regressions.

### H4) GameContext service interfaces are over-abstracted for current usage
Files: `include/openglad/runtime/game_context.h:42`, `src/runtime/game_context.cpp:28`, `src/sdl_client/runtime/sdl_context_services.cpp:42`

Current approach: `IConfigContextService`/`IRenderContextService`/`IInputContextService` add indirection over fields already present in `GameContext`, with only legacy wrappers in production.

Simpler alternative: Keep `GameContext` as plain data + helper functions. Remove service interfaces unless multiple non-legacy providers actually exist.

Why simpler is better: Cuts interface overhead and cognitive load; current service interfaces mostly re-express direct global/field access.

## 3. Medium Priority

### M1) Render interfaces have only one concrete implementation each
Files: `include/openglad/entities/walker_render.h:15`, `src/sdl_client/runtime/walker_render_bridge.cpp:25`, `include/openglad/data/level_render.h:18`, `src/sdl_client/render/sdl_level_render.h:17`

Current approach: `IWalkerRender` and `ILevelRender` are pure virtual interfaces with one SDL implementation and headless null behavior.

Simpler alternative: Use concrete SDL render classes behind nullable ownership in entities/levels, or a lightweight function-table/strategy only where needed.

Why simpler is better: Current polymorphism adds type and file overhead without real implementation diversity.

### M2) Loader API has legacy parameters and duplicated concerns
Files: `include/openglad/data/gloader.h:42`, `src/runtime/gloader.cpp:814`

Current approach: `create_walker_owned(..., screen* screenp = nullptr, bool cache_weapons = true)` keeps unused/legacy parameters (`[[maybe_unused]]`) while headless has a separate constructor path.

Simpler alternative: Replace with one minimal signature that matches real call sites; remove dead params and route render setup through a single optional capability.

Why simpler is better: Shrinks API surface and avoids preserving historical call contracts that no longer matter.

### M3) Transitional shim layer is larger than necessary and hides ownership boundaries
Files: `src/ui/picker.h:1`, `src/runtime/game_context.h:1`, `src/render/view.h:1`, `src/base.h:1`

Current approach: Many `src/*` compatibility headers forward to `include/openglad/*` headers.

Simpler alternative: Timebox shim removal and migrate all includes to canonical paths. Keep only a very small temporary shim set with a deletion deadline.

Why simpler is better: Fewer include paths, less indirection, fewer "which header is canonical" questions.

## 4. Low Priority / Nitpicks

### L1) `PickerTransition` appears unused
Files: `include/openglad/ui/picker_state.h:49`

Current approach: Struct is defined but not used by `run_picker()` or call sites.

Simpler alternative: Remove until needed or convert `run_picker()` to actually use it.

Why simpler is better: Avoids speculative API fragments.

### L2) Empty picker public header adds noise
Files: `include/openglad/ui/picker.h:18`, `src/ui/picker.h:3`

Current approach: Public header exists but is effectively empty; shim header points at it.

Simpler alternative: Delete empty header pair or populate with real public API.

Why simpler is better: Reduces misleading surface area.

### L3) Dead audio abstraction
Files: `include/openglad/platform/audio.h:10`

Current approach: `IAudio` exists but runtime still directly uses `soundob` and no `IAudio` implementation is wired.

Simpler alternative: Remove `IAudio` until audio backend polymorphism is truly implemented.

Why simpler is better: Prevents architecture drift into "future abstraction" placeholders.

## 5. Well-Designed

### W1) SDL source relocation is a good separation move
Files: `src/sdl_client/*`, `CMakeLists.txt:795`

Current approach: SDL-bound code now lives under `src/sdl_client/`, and headless has its own target.

Why this is good: Physical separation improves dependency hygiene and makes SDL coupling visible in file layout and build rules.

### W2) OgFile abstraction is justified and actively used
Files: `include/openglad/io/og_file.h:14`, `src/io/og_file.cpp:20`

Current approach: `OgFile` has concrete PhysFS and stdio backends that are both used for SDL-free file access.

Why this is good: This abstraction removes a real dependency constraint and has genuine implementation diversity.

### W3) Event-log boundary between sim and runtime is appropriate
Files: `include/openglad/sim/sim_event_log.h:16`, `include/openglad/sim/event.h:9`, `src/sdl_client/runtime/screen.cpp:461`

Current approach: simulation emits events; runtime dispatches them to presentation/audio.

Why this is good: This is a meaningful architectural seam with clear ownership and testability benefits.

### W4) Render bridge split for walker methods has a real compile-time purpose
Files: `src/sdl_client/runtime/walker_render_bridge.cpp:9`, `src/entities/walker.cpp:132`

Current approach: pixieN-dependent walker methods are isolated from SDL-free entity code.

Why this is good: It enforces module boundaries and avoids pulling render dependencies into core entity compilation units.

## 6. Remediation Plan

This section replaces the earlier plan and assumes the current objective is a complete text-mode game flow, not just a simulation harness.

### 6.1 Target End State

- `openglad_text` has an interactive picker/menu flow (campaign selection, team setup, options/settings, save/load, play, quit) driven by `run_picker()` and a concrete text client.
- Headless platform support is explicit: required features are implemented, intentionally unsupported features return typed errors with visible diagnostics (never silent no-op behavior).
- Runtime/documentation cleanup follows once text-mode completeness is in place.
- Temporary worker artifacts (`io_tmp_*.txt`) are removed from the repo root and prevented from returning.

### 6.2 Reframed Findings (H1-H4, M1-M3, L1-L3)

#### H1 (revised): Complete and integrate the picker state machine

Previous decision to remove picker code was incorrect for product goals. `include/openglad/ui/picker_state.h` and `src/text_client/text_picker.cpp` are the intended foundation for text mode and must be wired into `src/text_client/main.cpp`.

Implementation details:
1. Make `text_picker.cpp` export a real entrypoint (for example `int run_text_client_picker(int argc, char** argv)` or equivalent façade around `run_picker()`).
2. Expand `TextPickerClient` so methods are functional, not placeholders:
   - `show_campaign_select()` should use `list_campaigns()`, `CampaignData::load_with_error()`, and `mount_campaign_package_with_error()`.
   - `show_team_build()` should support selecting families/count and updating in-memory team/save structures.
   - `show_options()` should load/edit/save config through `cfg_store` (`load_settings()`, `save_settings()` path).
   - `load_game()` / `save_game()` should call real `SaveData` serialization (`SaveData::load_with_error`, `save_with_error`) and surface precise error states.
   - `run_game()` should launch the existing tick/state/events loop path (currently in `main.cpp`) with the selected campaign/team/level state.
3. Convert `src/text_client/main.cpp` from “JSON command loop only” to mode-based entry:
   - default interactive picker mode (stdin/stdout menu),
   - retain current protocol mode behind an explicit flag (for automation/tests), not as the only behavior.
4. Keep `src/text_client/text_picker.cpp` in `HEADLESS_SOURCES` (`CMakeLists.txt`) and add/adjust tests to exercise picker entry and one end-to-end menu path.

#### H2 (revised): Replace no-op headless platform surface with capability-complete behavior

The core issue is not that headless symbols exist; it is that many required operations currently return false/empty with no behavior in `src/text_client/platform_headless.cpp`.

Implementation details:
1. Split stubs into three categories and implement accordingly:
   - Required for text-client completeness: campaign/package mount/list, level listing, settings load/save/reset, save/load plumbing, archive helpers used by campaign flows, filesystem init/sync/dirs.
   - Optional for text mode but should be explicit unsupported: audio/UI-only prompts, SDL event pumping, visual rendering hooks.
   - Transitional shims that should be removed once call sites are migrated.
2. Move shared non-SDL filesystem logic out of SDL-only code:
   - extract reusable logic from `src/sdl_client/io/platform_io.cpp` into SDL-free helpers under `src/io/` (for example campaign list/mount helpers, archive wrappers, file creation helpers),
   - keep SDL-only RWops/input code in SDL compilation units.
3. In `src/text_client/platform_headless.cpp`, replace false-return stubs with real implementations where required, and ensure bool wrappers delegate to `_with_error` functions as single source of truth.
4. For intentionally unsupported functions (for example `yes_or_no_prompt`, `get_input_events`, draw-only hooks), return deterministic defaults plus one-time `LogWarn` diagnostics.
5. Preserve link compatibility during migration, then remove obsolete shim stubs once all required behavior is implemented.

#### H3 (unchanged goal, deferred until completeness baseline): Unify duplicated headless vs SDL entity creation

Implementation details:
1. Remove `create_walker_headless` from `include/openglad/data/gloader.h` and `src/runtime/gloader.cpp`; keep one `create_walker_owned(Order, std::int32_t family)` path.
2. Remove `LevelData::add_*_headless` declarations from `include/openglad/data/level_data.h` and implementations from `src/runtime/level_data.cpp`.
3. Keep headless behavior via render attachment policy (`walker_headless.cpp` / render hooks), not duplicate construction branches.

#### H4 (unchanged, later pass): Simplify `GameContext` service indirection

Implementation details:
1. Remove `IConfigContextService`, `IRenderContextService`, `IInputContextService` from `include/openglad/runtime/game_context.h` after platform wiring is stable.
2. Simplify `src/runtime/game_context.cpp` to direct fields and direct `input_state_from_sdl()` call path.
3. Reduce `src/sdl_client/runtime/sdl_context_services.cpp` to SDL-specific input/render bridge code only.

#### M1 (unchanged, later pass): Reduce single-implementation render interface overhead

After H2/H3 settle:
1. Collapse `ILevelRender` indirection in `LevelData` toward concrete SDL-side ownership.
2. Collapse `IWalkerRender` if compile-boundary constraints remain satisfied.

#### M2 (bundled into H3): Remove legacy loader params

Implementation details:
1. Remove unused `screen*` / `cache_weapons` parameters from `create_walker_owned`.
2. Update call sites such as `src/sdl_client/runtime/guy_create.cpp`, `src/sdl_client/ui/picker_team_build.cpp`, and `src/sdl_client/ui/results_screen.cpp`.

#### M3 (re-scoped): Remove only truly obsolete shims, keep necessary migration aids

Because picker integration now depends on UI-facing headers:
1. Do not auto-delete picker headers as part of H1.
2. Remove unrelated transitional headers only after include scan confirms zero users (`src/runtime/game_context.h`, `src/render/view.h`, `src/base.h`, etc.).
3. Add an include-path guard in CI to prevent reintroduction of new `src/*` shim includes.

#### L1 (revised): `PickerTransition` is either used or removed based on final picker design

With picker retained:
1. Either wire `PickerTransition` into `run_picker()` transitions and redraw semantics,
2. Or remove it from `include/openglad/ui/picker_state.h` if the final state machine does not need it.

#### L2 (revised): `include/openglad/ui/picker.h` should become real API surface

Instead of deleting:
1. Populate `include/openglad/ui/picker.h` with exported picker entrypoints for text/SDL callers.
2. Keep `src/ui/picker.h` shim only if external compatibility requires it; otherwise remove it in M3 cleanup.

#### L3 (unchanged): Remove dead `IAudio` abstraction after behavior work

`include/openglad/platform/audio.h` can still be removed once no pending branch depends on it.

### 6.3 Required Headless Stub Completion Matrix

Primary implementation file: `src/text_client/platform_headless.cpp`.

Must implement now (functional text client):
- Campaign package operations:
  - `mount_campaign_package_with_error`, `unmount_campaign_package_with_error`, `remount_campaign_package_with_error`
  - `mount_campaign_package`, `unmount_campaign_package`, `remount_campaign_package`
  - `get_mounted_campaign`, `list_campaigns`, `list_levels`, `list_levels_v`
- Settings/config operations:
  - `load_settings`, `save_settings`, `restore_default_settings`
- Save/load operations:
  - stop overriding `SaveData::load/save` with hardcoded false stubs in headless build; use real serialization path
- Filesystem/archive support used by campaign/editor-adjacent flows:
  - `zip_contents_with_error`, `unzip_into_with_error`, wrappers
  - `create_dir`, `sync_filesystem`, `restore_default_campaigns`

Keep as explicit unsupported (with warnings):
- `yes_or_no_prompt` (SDL dialog replacement)
- `get_input_events` (SDL input pump)
- draw/render hooks that are genuinely no-op in text mode
- audio-only behaviors

Transitional:
- Keep compatibility wrappers only while migrating call sites; remove once unused.

### 6.4 `og_file` vs PhysFS Write-up (required documentation output)

The plan keeps `OgFile` and documents why:
- `PhysFS` is a backend API and mount abstraction, not a full high-level file object used uniformly by runtime/data code.
- `OgFile` provides one RAII read/write/seek interface used by save/load, scenario/campaign parsing, and pixie loading across SDL and headless builds.
- `OgFile` enables fallback paths (PhysFS + stdio filesystem) that are required in current runtime behavior (`src/io/og_file.cpp`).
- `physfs_api` remains useful as a narrow vendor wrapper for mount/enumeration/error management.

Required doc updates:
1. Add a dedicated subsection to `docs/ARCHITECTURE.md` under I/O architecture describing:
   - `physfs_api` responsibilities,
   - `OgFile` responsibilities,
   - what is intentionally redundant vs accidental duplication.
2. Add a short “headless/text client architecture” and “I/O layering” summary to `CLAUDE.md` so contributor guidance matches current structure.

### 6.5 Unified Commit Sequence

This is the single implementation sequence for the full remediation scope.

1. `chore(repo): remove transient io_tmp worker artifacts`
   - Delete `io_tmp_*.txt` from repo root.
   - Add ignore rule (for example in `.gitignore`) to prevent recurrence.

2. `feat(text): wire picker state machine into openglad_text entry`
   - Files: `src/text_client/main.cpp`, `src/text_client/text_picker.cpp`, `include/openglad/ui/picker_state.h`, `include/openglad/ui/picker.h`, `CMakeLists.txt`.
   - Outcome: interactive picker is the primary text-client flow; protocol mode preserved under explicit flag.

3. `feat(text): implement campaign and level selection in headless platform layer`
   - Files: `src/text_client/platform_headless.cpp`, shared helpers in `src/io/*`, related headers in `include/openglad/platform/io_common.h` / `include/openglad/io/*`.
   - Outcome: real mount/list/remount behavior and campaign selection in text mode.

4. `feat(text): implement settings and save/load behavior for text client`
   - Files: `src/text_client/platform_headless.cpp`, `src/text_client/text_picker.cpp`, `src/runtime/save_data.cpp` (and/or build lists), `include/openglad/data/save_data.h`.
   - Outcome: text picker can load/save games and persist settings.

5. `refactor(headless): classify remaining unsupported APIs and add explicit diagnostics`
   - Files: `src/text_client/platform_headless.cpp`.
   - Outcome: no silent false/no-op for critical flows; unsupported paths are visible and typed.

6. `refactor(runtime): unify LevelData/gloader object creation paths`
   - Files: `include/openglad/data/gloader.h`, `src/runtime/gloader.cpp`, `include/openglad/data/level_data.h`, `src/runtime/level_data.cpp`, affected SDL callers.
   - Outcome: remove duplicate headless constructors and legacy loader params (H3 + M2).

7. `refactor(runtime): replace LevelData link-time hook globals with explicit capability wiring`
   - Files: `include/openglad/data/level_data.h`, `src/runtime/level_data.cpp`, `src/sdl_client/runtime/sdl_context_services.cpp`, `src/text_client/platform_headless.cpp`, text runtime wiring in `src/text_client/main.cpp`.
   - Outcome: behavior is explicit per runtime, not hidden behind unresolved extern shims.

8. `refactor(context): simplify GameContext service layers`
   - Files: `include/openglad/runtime/game_context.h`, `src/runtime/game_context.cpp`, `src/sdl_client/runtime/sdl_context_services.cpp`, dependent callers.

9. `refactor(render): collapse single-implementation render interfaces`
   - Files: `include/openglad/entities/walker_render.h`, `include/openglad/data/level_render.h`, walker/level render bridge sources.
   - Execute in two commits if needed (`ILevelRender` then `IWalkerRender`) to isolate regressions.

10. `chore(headers): remove obsolete transitional shim headers`
    - Files: `src/ui/picker.h` (if no longer needed), `src/runtime/game_context.h`, `src/render/view.h`, `src/base.h`, plus CI include guard script.

11. `docs(architecture): update architecture and contributor guidance for post-refactor reality`
    - Files: `docs/ARCHITECTURE.md`, `CLAUDE.md`.
    - Include:
      - text client flow and capability boundaries,
      - updated directory/module map (`src/sdl_client`, `src/text_client`, link-time/capability dispatch decisions),
      - `OgFile` vs `PhysFS` rationale.

12. `chore(audio): remove unused IAudio abstraction`
    - File: `include/openglad/platform/audio.h` (or replace with temporary deprecation header if required).

### 6.6 Validation Gates

Run between commits, not only at the end:

1. Build gates:
   - `cmake --build <build-dir> --target openglad openglad_text`
2. Text-client gates:
   - Existing protocol smoke (`scripts/test_text_client.sh`) in protocol mode flag.
   - New interactive picker smoke test (campaign select -> team build -> start -> quit).
   - Save/load round-trip test in headless mode.
3. Runtime gates:
   - SDL startup and one mission flow sanity run.
4. Cleanup/documentation gates:
   - `rg -n "io_tmp_"` returns none in tracked files.
   - Architecture docs mention final module/layout and I/O layering.
