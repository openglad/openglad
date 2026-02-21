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

This section supersedes the previous rewrite and incorporates a full audit against Sections 2-4 plus current code in `src/text_client/`, `src/sdl_client/`, `CMakeLists.txt`, and test infrastructure.

### 6.1 Scope, Objectives, and Guardrails

Primary objective:
- Ship a complete text-mode gameplay flow in `openglad_text` (interactive picker + playable mission loop + save/load/settings/campaign flows) without silent no-op behavior.

Secondary objective:
- Preserve a build-green migration path while reducing over-engineered seams called out in H1-H4, M1-M3, L1-L3.

Guardrails:
- No commit in the sequence should knowingly leave default CI targets broken (`openglad`, `openglad_text`, test binaries).
- Unsupported behavior must be explicit and observable (typed error or one-time warning), never silent `false`/empty stubs on critical paths.
- Keep SDL and text responsibilities separated by capability boundaries, not link-time surprises.

### 6.2 Coverage of Audit Findings (H1-H4, M1-M3, L1-L3)

H1 (dead picker abstraction):
- Keep picker abstraction, but make it real: add a concrete text-picker entrypoint and wire `main.cpp` mode selection so picker is the default interactive path, with protocol mode retained behind explicit flag.
- Fix stdin handling pitfalls during integration (`scanf`/`getline` mixing) by using one consistent input strategy.

H2 (silent link-time no-op surface):
- Replace required `platform_headless.cpp` stubs with functional implementations or shared SDL-free helpers.
- For intentionally unsupported features, return deterministic fallback + typed/logged diagnostics.
- Eliminate hidden behavior drift by introducing a single header for level-data dispatch contract signatures used by both SDL and headless implementations.

H3 (duplicated headless/SDL object creation):
- Not indefinite deferral. This is phase-2 work in this same remediation stream, executed immediately after text-mode baseline is stable.
- Collapse `create_walker_headless` / `add_*_headless` duplication into one creation path with optional render attachment.

H4 (GameContext interface over-abstraction):
- Defer until after H2/H3 but keep in-scope for this plan with explicit exit criteria and a target commit window.

M1 (single-implementation render interfaces):
- Execute after H3. Reduce virtual-interface overhead only after construction/capability flow is unified.

M2 (legacy loader params):
- Bundle with H3 unification to avoid churn.

M3 (large transitional shim layer):
- Keep minimal necessary shims during migration.
- Add CI guard to block new `src/*` shim includes.
- Remove obsolete shims only after include scan proves zero users.

L1 (`PickerTransition` unused):
- Resolve during picker integration: either use it in transition flow or remove it.

L2 (empty `include/openglad/ui/picker.h`):
- Promote it to real exported picker API surface (text + SDL entrypoints or state-machine façade).

L3 (`IAudio` dead abstraction):
- Remove after functional work and dependency cleanup.

### 6.3 Headless Platform Completion Matrix (audited against `platform_headless.cpp` + SDL `platform_io.cpp`)

Primary files:
- `src/text_client/platform_headless.cpp`
- `src/sdl_client/io/platform_io.cpp`
- `src/io/og_file.cpp`

#### A) Must be implemented for text-mode completeness

Campaign/filesystem/runtime operations currently stubbed but required:
- `list_files`
- `mount_campaign_package_with_error` / `unmount_campaign_package_with_error` / `remount_campaign_package_with_error`
- bool wrappers `mount_campaign_package` / `unmount_campaign_package` / `remount_campaign_package`
- `list_campaigns`, `list_levels`, `list_levels_v`
- `restore_default_campaigns`
- `create_dir`
- `io_init`, `io_exit`, `sync_filesystem` (or remove/replace `headless_io_init` so there is exactly one headless init/teardown path)
- Archive helpers: `zip_contents_with_error`, `unzip_into_with_error`, wrappers

Save/config operations:
- Remove headless `SaveData::{load,save,is_level_completed}` overrides from `platform_headless.cpp`.
- Ensure real `src/runtime/save_data.cpp` is linked for `openglad_text` and used via `load_with_error` / `save_with_error` at call sites.
- Route settings persistence through `cfg_store::load_settings()` / `cfg_store::save_settings()` and remove or repurpose orphan free-function wrappers `load_settings`/`save_settings` in `io_common.h` (currently declared but not implemented in SDL).

#### B) Keep as explicit unsupported for text mode (deterministic + one-time warning)

- `yes_or_no_prompt` (replace with text prompt implementation if picker/runtime path needs confirmations; otherwise explicit non-interactive default policy with warning)
- `get_input_events` and other SDL event-pump-only behavior
- `level_data_draw_impl` and visual draw hooks
- Audio-only behavior where simulation correctness does not depend on playback

Implementation rule:
- Use one-time diagnostics (`std::once_flag`) for unsupported calls so logs are visible but not spammy.

#### C) Keep minimal no-op/compat only where semantically safe

- `clear_stale_view_controls` in headless (safe no-op, but document why)
- `level_data_wire_entity_from_screen` only until LevelData wiring is made explicit without screen globals
- `input_state_from_sdl` should not remain silent forever; either route to text input sampling or explicitly annotate unsupported in non-interactive protocol mode

#### D) Defer or remove from text-mode critical path

Editor-adjacent APIs can stay non-functional short-term, but must not silently claim success:
- `delete_level`, `delete_campaign`
- `unpack_campaign`, `repack_campaign`, `cleanup_unpacked_campaign`
- `create_new_map_pix_with_error`, `create_new_pix_with_error`, `create_new_campaign_descriptor_with_error`, `create_new_scen_file_with_error`, wrappers
- `load_map_data`

Policy:
- Return typed failure with clear log if invoked from text mode until editor parity work is explicitly scheduled.

### 6.4 Additional Missing Concerns to Address

1. Init/teardown duplication risk:
- `openglad_text` currently uses `headless_io_init()` while `io_common.h` still exposes `io_init/io_exit/sync_filesystem` that are stubbed in headless.
- Plan must converge to one authoritative headless lifecycle path.

2. Error-handling consistency:
- Any bool wrapper should delegate to `_with_error` function and preserve diagnostic context.
- Avoid direct `false` returns without context on campaign/save/archive operations.

3. Include/link contract drift:
- `level_data.cpp` relies on local `extern` declarations for dispatch functions.
- Add shared declarations header and include from both implementations to prevent signature drift and hidden link failures.

4. Threading/concurrency constraints:
- Headless mode is single-threaded today; document this explicitly.
- Ensure one-time warning/logging helpers are thread-safe.
- For future multi-session/headless harnesses, identify global state blockers (`cfg`, difficulty globals, mounted campaign state) and mark non-thread-safe assumptions in docs/tests.

5. SDL-side impact containment:
- Any helper extracted from `platform_io.cpp` to `src/io/` must remain SDL-free.
- Keep SDL RWops/input glue in `src/sdl_client/` only.

### 6.5 Dependency-Safe Commit Sequence

This sequence replaces the previous 12-commit ordering to keep builds green between steps.

1. `chore(repo): remove transient io_tmp artifacts and ignore pattern`
- Delete tracked `io_tmp_*.txt` files and add ignore coverage.

2. `refactor(headless): establish shared non-SDL io helpers`
- Extract campaign/list/archive/fs helpers from `src/sdl_client/io/platform_io.cpp` into `src/io/*`.
- No behavior change yet for SDL/text callers.

3. `refactor(headless): replace critical platform_headless stubs with real implementations`
- Implement required APIs from 6.3A using shared helpers.
- Add explicit one-time warnings for unsupported paths from 6.3B.

4. `build(text): link real save_data into openglad_text and remove SaveData stub overrides`
- Update `CMakeLists.txt` and delete `SaveData` method bodies from `platform_headless.cpp`.
- Ensure headless save/load goes through real serialization.

5. `refactor(io): unify headless lifecycle entrypoints`
- Converge `headless_io_init` vs `io_init/io_exit/sync_filesystem` into one clear path.
- Preserve startup behavior parity (default campaign, cfg/assets mount).

6. `feat(text): add text-picker entrypoint and exported picker API`
- Implement concrete `run_text_picker(...)` (or equivalent) and populate `include/openglad/ui/picker.h`.
- Resolve `PickerTransition` (use or remove).

7. `feat(text): implement functional picker screens (campaign/team/options/help/load/save)`
- Complete `TextPickerClient` behavior with typed error reporting.
- Replace fragile mixed input parsing with consistent line-based parsing.

8. `feat(text): switch openglad_text default mode to interactive picker and retain protocol flag`
- Keep existing automation protocol under explicit `--protocol` (or equivalent).
- Update `scripts/test_text_client.sh` and related tests for explicit protocol mode.

9. `test(text): add interactive picker smoke + save/load round-trip coverage`
- Add non-flaky scripted text-mode interaction tests.
- Assert unsupported features emit expected warnings/errors.

10. `refactor(runtime): unify LevelData/gloader object creation paths (H3 + M2)`
- Remove `create_walker_headless` and `add_*_headless` variants.
- Remove legacy loader params and update SDL call sites.

11. `refactor/runtime: replace level_data link-time extern hooks with explicit capability wiring`
- Introduce explicit capability object or function-table injection for level render/wiring.
- Maintain headless + SDL behavior parity.

12. `refactor(context): simplify GameContext service indirection (H4)`
- Remove interfaces that only wrap globals/fields once alternate providers are no longer needed.

13. `refactor(render): collapse single-implementation render interfaces (M1)`
- Execute after H3/H4 to minimize churn and isolate regressions.

14. `chore(headers+audio+docs): remove obsolete shims, remove IAudio, update architecture docs`
- Remove stale `src/*` transitional headers (after include scan).
- Document final text/headless/SDL boundaries and I/O layering.

### 6.6 Validation Gates (expanded)

Run gates at each phase boundary, not only at the end.

Build/link gates:
- `cmake --build <build-dir> --target openglad openglad_text openglad_test og_unit_tests`
- Verify no unresolved-symbol regressions after each dispatch/lifecycle refactor commit.

Headless/text gates:
- Protocol mode smoke (`scripts/test_text_client.sh`) using explicit protocol flag.
- Interactive picker smoke (scripted stdin): campaign select -> team setup -> start mission -> quit.
- Headless save/load round-trip with real `SaveData` path.
- Failure-path checks: invalid campaign ID, missing save file, archive operation failures return typed errors and logs.

SDL regression gates:
- `ctest -R "openglad_test_picker|og_data_tests|og_runtime_tests" --output-on-failure`
- One mission startup sanity run for `openglad`.

Architecture hygiene gates:
- `rg -n "io_tmp_"` returns none for tracked files.
- Include hygiene check scripts still pass (`check_graph_h_includes`, vendor leak guard).
- CI guard rejects newly introduced `#include "src/..."` shims outside allowed temporary set.

Diagnostics and unsupported-surface gates:
- Tests assert one-time warnings for unsupported text-mode-only exclusions.
- No critical path returns raw `false` without diagnostic context.

### 6.7 Deferrals and Exit Criteria

Allowed temporary deferrals:
- H4/M1 internals may follow H2/H3, but remain in this remediation stream (no open-ended “later”).
- Editor-only APIs in 6.3D can remain unsupported for text mode if they return explicit typed failure.

Not allowed to defer:
- SaveData real linkage in headless.
- Campaign mount/list behavior needed by picker flow.
- Lifecycle unification (`headless_io_init` vs `io_init/io_exit`).
- Protocol-mode compatibility coverage when default mode changes.

### 6.8 Expected End State

- `openglad_text` supports interactive text picker and playable loop by default.
- Protocol mode remains available and tested explicitly.
- Required headless platform APIs are functional; unsupported features are explicit and observable.
- Duplicate headless construction paths and stale service abstractions are removed on a controlled schedule.
- Build/test/docs enforce architectural boundaries and prevent reintroduction of silent stubs.

### 6.9 Audit Notes (delta from previous Section 6)

Changes made after audit:
- Expanded stub matrix to include every current headless stub surface, including lifecycle APIs and SaveData overrides.
- Corrected sequencing to avoid breaking builds/tests by integrating picker before foundational headless IO/save support.
- Added missing concern: orphan `load_settings`/`save_settings` free-function API mismatch vs real `cfg_store` usage.
- Added lifecycle convergence requirement (`headless_io_init` vs `io_init/io_exit/sync_filesystem`).
- Added include/link-contract hardening for level-data dispatch signatures.
- Added concurrency assumptions and one-time-warning requirements for unsupported paths.
- Strengthened validation gates with explicit protocol-mode coverage, failure-path assertions, and unresolved-symbol checks.
- Converted open-ended deferrals into bounded phase ordering with explicit non-deferrable items.
