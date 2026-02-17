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

This section turns findings H1-H4, M1-M3, L1-L3 into executable work items.

### H1) Dead picker state-machine abstraction

Current confirmed state:
- `run_picker()` exists in `include/openglad/ui/picker_state.h:93` and is not invoked.
- `TextPickerClient` is defined in `src/text_client/text_picker.cpp:19` and has no externally used symbol.
- `src/text_client/main.cpp:253` runs a command-loop protocol directly.
- `src/text_client/text_picker.cpp` is still compiled via `CMakeLists.txt:803`.

Remediation changes:
1. Remove unused picker abstraction from build and source surface.
   - Delete `include/openglad/ui/picker_state.h`.
   - Delete `src/text_client/text_picker.cpp`.
   - Remove `${SRC_DIR}/text_client/text_picker.cpp` from `HEADLESS_SOURCES` in `CMakeLists.txt:798-827` (currently at line 803).
2. Remove empty public picker header pair.
   - Delete `include/openglad/ui/picker.h` (currently empty at `:18`).
   - Delete `src/ui/picker.h` shim.
3. Verify no picker-state references remain.
   - `rg -n "picker_state|run_picker|IPickerClient|TextPickerClient|PickerTransition" include src`.
4. Rebuild both targets:
   - `cmake --build <build-dir> --target openglad_text openglad`.

Scope and risk:
- Files touched: 2-4 deletions + 1 CMake edit (small).
- Risk: Low.

Dependencies:
- Can be done independently; no prerequisite.

Watchouts:
- Ensure no docs/tests still reference `run_picker`.

Commit strategy:
- Standalone commit: `refactor(text): remove unused picker state-machine and text picker stub`.

### H2) Link-time dispatch no-op surface and silent failure behavior

Current confirmed state:
- `src/runtime/level_data.cpp:43-47` declares extern hooks (`level_data_draw_impl`, `create_level_render`, etc.).
- SDL implementation in `src/sdl_client/runtime/sdl_context_services.cpp:82-118`.
- Headless no-op stubs in `src/text_client/platform_headless.cpp:82-85`.
- Many unrelated headless stubs return false/null in `src/text_client/platform_headless.cpp:126-207`.

Remediation changes (two-phase to reduce breakage):
1. Replace `LevelData` extern-hook dispatch with explicit function pointers stored on `LevelData`.
   - In `include/openglad/data/level_data.h` add a small `LevelDataPlatformHooks` struct with members for:
     - `clear_stale_view_controls(LevelData*)`
     - `wire_entity_from_runtime(walker*)`
     - `draw(LevelData*, screen*)`
     - `create_level_render(PixieData[])`
   - Add `set_platform_hooks(const LevelDataPlatformHooks*)` and default safe hooks on construction.
   - Update `src/runtime/level_data.cpp`:
     - remove `extern` declarations at `:43-47`.
     - route calls in `add_ob/add_fx_ob/add_weap_ob` (`:471`, `:490`, `:507`) and `draw` (`:1862`) through hooks.
     - keep default no-op behavior local and explicit (not link-time hidden).
2. SDL wiring.
   - In `src/sdl_client/runtime/sdl_context_services.cpp` expose one `const LevelDataPlatformHooks&` provider and install it into active `LevelData` instances during screen/session init path (via `screen` lifecycle path that owns `level_data`).
   - Remove free-function hook exports from this file after call sites are migrated.
3. Headless wiring.
   - In `src/text_client/main.cpp`, immediately after level construction (`:289`) set headless platform hooks explicitly to no-render hooks.
   - Delete now-obsolete hook stubs from `src/text_client/platform_headless.cpp:75-85`.
4. Stop silent false/no-op on unsupported I/O operations in headless path.
   - In `src/text_client/platform_headless.cpp`, keep function signatures for API compatibility but add explicit one-time logging per unsupported operation group (`campaign`, `save/load`, `zip/unzip`, settings) to make failures diagnosable.
   - Where `_with_error` APIs exist (e.g., `zip_contents_with_error`, `mount_campaign_package_with_error`), return the typed error enum as already done; ensure bool wrappers call the typed versions (single source of truth).
5. Verify:
   - SDL and headless builds.
   - Smoke run `openglad_text --level 1 --seed 42` and ensure no missing symbols.

Scope and risk:
- Files touched: ~4-8.
- Risk: High (touches core level wiring and build/link behavior).

Dependencies:
- Prefer after H3 starts or in same train, because H3 touches the same entity-wiring path.

Watchouts:
- `GameSession` and screen lifecycle ordering must set hooks before object creation paths call `add_ob`.
- Avoid circular includes when introducing hook struct types.

Commit strategy:
- Separate into two commits:
  1. `refactor(runtime): replace LevelData link-time hooks with explicit platform hooks`
  2. `fix(headless): surface unsupported platform operations via typed errors/logging`

### H3) Duplicated headless vs SDL object-creation paths in `LevelData`/`loader`

Current confirmed state:
- `LevelData::add_ob` branches on `headless_` (`src/runtime/level_data.cpp:460`) and fans out to duplicated `*_headless` methods (`:525-567`).
- `loader` exposes duplicated constructors (`include/openglad/data/gloader.h:42-43`).
- `create_walker_owned` vs `create_walker_headless` have mostly duplicate stats/setup logic (`src/runtime/gloader.cpp:812-913`).

Remediation changes:
1. Collapse to one loader creation API.
   - In `include/openglad/data/gloader.h`, remove `create_walker_headless`; keep one `create_walker_owned(Order, std::int32_t family)` signature.
   - In `src/runtime/gloader.cpp`, merge logic into a single implementation:
     - always instantiate entity with `PixieData` constructor (as current `create_walker_owned` does).
     - retain family clamping, stats setup, `set_walker`, and initial frame logic in one path.
     - remove unused params (`screen*`, `cache_weapons`) and `[[maybe_unused]]` markers.
2. Collapse `LevelData` add paths.
   - In `include/openglad/data/level_data.h`, remove `add_ob_headless`, `add_fx_ob_headless`, `add_weap_ob_headless` declarations.
   - In `src/runtime/level_data.cpp`, remove duplicated headless methods (`:525-567`) and `headless_` branching in `add_ob/add_fx_ob/add_weap_ob` (`:460`, `:482`, `:499`).
   - Always use one flow:
     - `myloader->create_walker_owned(...)`
     - `wire_entity(...)`
     - push to correct list.
   - Keep `headless_` only for tile-renderer initialization (`:422-426`, `:1566-1573`) unless H2 replaces this with explicit render capability.
3. Update all callers.
   - Remove any direct `create_walker_headless` callsites (currently `level_data.cpp:530/545/558`).
   - Update any `create_walker_owned` callsites that pass legacy args (e.g., `src/sdl_client/runtime/guy_create.cpp:19`, `src/sdl_client/ui/picker_team_build.cpp:1801`, `src/sdl_client/runtime/results_screen.cpp:283`).
4. Validate:
   - Build SDL/headless.
   - Smoke gameplay path that creates living/fx/weapon entities.

Scope and risk:
- Files touched: ~6-10.
- Risk: High (entity creation is hot-path).

Dependencies:
- Should be done before M2 (M2 is API cleanup residue of this work).
- Coordinate with H2 since both touch `level_data.cpp` and entity wiring.

Watchouts:
- Ensure headless still avoids render component creation (enforced by `walker_headless.cpp:20-27`).
- Preserve `numobs` semantics for living objects.

Commit strategy:
- 2 commits:
  1. `refactor(runtime): unify LevelData object creation paths`
  2. `refactor(loader): remove headless walker factory and legacy args`

### H4) `GameContext` service interfaces over-abstracted

Current confirmed state:
- Service interfaces and fields defined in `include/openglad/runtime/game_context.h:42-82`.
- Legacy service wrappers in `src/runtime/game_context.cpp:89-110` and `src/sdl_client/runtime/sdl_context_services.cpp:42-76`.
- Runtime callsites mostly use direct fields/fallback wrappers, not true provider diversity.

Remediation changes:
1. Remove service interface types from `game_context.h`.
   - Delete `IConfigContextService`, `IRenderContextService`, `IInputContextService` definitions.
   - Remove `config_service`, `render_service`, `input_service` members from `GameContext`.
2. Simplify `GameContext` methods in `src/runtime/game_context.cpp`.
   - `active_screen()` returns `game_screen`.
   - `active_prefs()` returns `prefs`.
   - `active_config()` returns `config`.
   - `active_input()` returns `&input`.
   - `poll_input()` calls `input_state_from_sdl(input)`.
   - Remove `LegacyConfigContextService` and default-service initialization (`:89-110`).
3. Reduce SDL context-services file.
   - Keep only truly SDL-specific functions still needed by runtime split (`input_state_from_sdl`, and temporary LevelData hook provider if H2 not complete).
   - Remove `install_sdl_context_services()` and related service objects if no longer used.
   - Remove declaration from headers/callers.
4. Validate and update callers.
   - Replace any service checks with direct field checks.
   - Confirm `ctx().poll_input()` path in `src/sdl_client/runtime/game_loop.cpp:131` still works.

Scope and risk:
- Files touched: ~3-6.
- Risk: Medium (broad include surface, but straightforward behavior).

Dependencies:
- Best after H2 design is settled to avoid reworking `sdl_context_services.cpp` twice.

Watchouts:
- `GameSession` initialization order in `src/sdl_client/runtime/game_session.cpp:48-70` must still guarantee `prefs`/`game_screen` availability for view constructors.

Commit strategy:
- Single focused commit: `refactor(context): remove unused GameContext service interfaces`.

### M1) Render interfaces have only one implementation each

Current confirmed state:
- `IWalkerRender` in `include/openglad/entities/walker_render.h:15` and one impl `PixieNWalkerRender` in `src/sdl_client/runtime/walker_render_bridge.cpp:25`.
- `ILevelRender` in `include/openglad/data/level_render.h:18` and one impl `SdlLevelRender` in `src/sdl_client/render/sdl_level_render.h:17`.

Remediation changes:
1. Keep behavior, reduce polymorphic surface gradually (avoid a large cross-cut in same train as H2/H3).
2. `ILevelRender` step-down:
   - Replace `std::unique_ptr<ILevelRender> renderer_` in `include/openglad/data/level_data.h:134` with `std::unique_ptr<SdlLevelRender>` once H2 removes cross-module factory hook constraints.
   - Move render-only use sites to SDL-side helper APIs where needed.
3. `IWalkerRender` step-down:
   - Change `walker::render_` from `std::unique_ptr<IWalkerRender>` to `std::unique_ptr<pixieN>` in `include/openglad/entities/walker.h:237`.
   - Inline tiny adapter calls (`bmp_data`, `set_frame`, `set_data`) directly against `pixieN` in `walker_render_bridge.cpp` and `walker_headless.cpp`.
   - Delete `include/openglad/entities/walker_render.h` after no references remain.

Scope and risk:
- Files touched: ~6-12.
- Risk: Medium/High (entity/render internals).

Dependencies:
- Should follow H2/H3 to avoid conflicts in render hooks and entity creation paths.

Watchouts:
- Preserve SDL-free compile boundaries (entity core files must not include `pixien.h`).
- If boundary breaks, stop at keeping interface but rename/document as temporary.

Commit strategy:
- Two separate commits (one for `ILevelRender`, one for `IWalkerRender`) to keep regressions isolatable.

### M2) Loader API legacy parameters and duplicated concerns

Current confirmed state:
- Signature includes unused legacy args in `include/openglad/data/gloader.h:42`.
- Implementation marks them `[[maybe_unused]]` in `src/runtime/gloader.cpp:814`.

Remediation changes:
1. Update API signature to `create_walker_owned(Order, std::int32_t family)` only.
2. Update all callsites that pass `screen*` or `cache_weapons`.
   - Confirmed examples: `src/sdl_client/runtime/guy_create.cpp:19`, `src/sdl_client/ui/picker_team_build.cpp:1801`, `src/sdl_client/ui/results_screen.cpp:283`.
3. Remove now-unneeded includes/forward declarations of `screen` from loader header if no longer referenced.

Scope and risk:
- Files touched: ~4-7.
- Risk: Medium.

Dependencies:
- Execute with H3 (same edit set).

Watchouts:
- Watch for any out-of-tree tooling/tests depending on old signature.

Commit strategy:
- Bundle with H3 loader unification commit.

### M3) Transitional shim layer larger than necessary

Current confirmed state:
- Shim headers include canonical headers only:
  - `src/ui/picker.h:1-4`
  - `src/runtime/game_context.h:1-4`
  - `src/render/view.h:1-4`
  - `src/base.h:1-4`
- In-repo includes already use canonical `<openglad/...>` paths for the above areas.

Remediation changes:
1. Remove shim headers with zero in-tree users first.
   - Delete `src/ui/picker.h`, `src/runtime/game_context.h`, `src/render/view.h`, `src/base.h`.
2. Add a short migration note to contributor docs (e.g., `docs/` include conventions) that `src/*` shim headers are removed.
3. Add CI guard (optional but recommended) to prevent reintroduction:
   - script/grep check that rejects includes matching `#include <src/...>` or `#include "src/..."`.

Scope and risk:
- Files touched: 4 deletions + optional docs/CI file.
- Risk: Low.

Dependencies:
- Independent, but easiest after H1/L2 because picker headers are related cleanup.

Watchouts:
- External downstream projects may still include shims; if this is a concern, keep one release-cycle deprecation warning before deletion.

Commit strategy:
- Standalone commit: `chore(headers): remove transitional src/* shim headers`.

### L1) `PickerTransition` unused

Current confirmed state:
- Defined at `include/openglad/ui/picker_state.h:50` and unused.

Remediation changes:
- This is resolved by H1 if `picker_state.h` is removed.
- If H1 is deferred, remove `PickerTransition` immediately from `picker_state.h` and verify no compile impact.

Scope and risk:
- Files touched: 0 additional if bundled with H1.
- Risk: None.

Dependencies:
- Depends on H1 decision.

Commit strategy:
- Bundle into H1 commit.

### L2) Empty picker public header

Current confirmed state:
- `include/openglad/ui/picker.h` is empty.
- `src/ui/picker.h` only forwards to it.

Remediation changes:
- Delete both headers (covered by H1 + M3).

Scope and risk:
- Files touched: 0 additional if bundled.
- Risk: None.

Dependencies:
- None beyond H1/M3 sequence.

Commit strategy:
- Bundle with H1 cleanup commit.

### L3) Dead audio abstraction

Current confirmed state:
- `IAudio` exists in `include/openglad/platform/audio.h:13-20`.
- No in-tree references found beyond that header.

Remediation changes:
1. Delete `include/openglad/platform/audio.h`.
2. Run include/reference scan:
   - `rg -n "platform/audio.h|\bIAudio\b" include src`.
3. If downstream compatibility is required, replace with a temporary comment-only deprecation header for one release, then delete.

Scope and risk:
- Files touched: 1 deletion.
- Risk: Low.

Dependencies:
- Independent.

Watchouts:
- Confirm no pending branch work is introducing new `IAudio` consumers.

Commit strategy:
- Small standalone commit: `chore(audio): remove unused IAudio abstraction`.

### Overall Sequencing (minimum risk, maximum impact)

1. H1 + L1 + L2 (dead picker removal).
2. H3 + M2 (unify walker/object creation and loader API).
3. H2 (replace link-time dispatch with explicit hooks; tighten headless unsupported behavior).
4. H4 (remove GameContext service interfaces after H2 settles SDL context wiring).
5. M3 (delete shim headers + optional include-path CI check).
6. M1 (reduce render interface polymorphism in two controlled passes).
7. L3 (remove dead audio abstraction; can be moved earlier if desired because it is isolated).

Why this order:
- Early steps remove dead code with low blast radius.
- Shared hot paths (`level_data.cpp`, `gloader.cpp`) are consolidated before context/render abstraction cleanup.
- H2/H4 are ordered to avoid churn in `sdl_context_services.cpp`.
- M1 is deferred because it has the highest chance of boundary regressions despite being medium priority.

### Suggested Commit Batching

Recommended commit set for implementation phase:
1. `refactor(text): remove unused picker state-machine and picker headers` (H1/L1/L2)
2. `refactor(runtime): unify LevelData walker creation paths` (H3 part 1)
3. `refactor(loader): remove headless walker factory and legacy args` (H3 part 2 + M2)
4. `refactor(runtime): replace LevelData link-time hooks with explicit platform hooks` (H2 part 1)
5. `fix(headless): return explicit typed errors/logging for unsupported platform operations` (H2 part 2)
6. `refactor(context): remove unused GameContext service interfaces` (H4)
7. `chore(headers): remove transitional src/* shim headers` (M3)
8. `refactor(render): collapse single-impl render interfaces` (M1 split into 2 commits)
9. `chore(audio): remove unused IAudio abstraction` (L3)

Validation checkpoints between commits:
- After commits 1-3: full build (`openglad`, `openglad_text`).
- After commits 4-6: full build + headless smoke run + one SDL runtime smoke path.
- After commits 7-9: full build and include-scan guard passes.
