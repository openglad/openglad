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
