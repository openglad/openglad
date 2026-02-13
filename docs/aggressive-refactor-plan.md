# OpenGlad Aggressive Refactor and Modularization Plan

Branch: `cpp-modernization-plan`  
Deliverable date: 2026-02-13  
Scope: C++ architecture + build/test structure. This is a plan only (no refactor implementation).

This plan is intentionally aggressive in end-state design, but structured to land as incremental PRs with clear completion criteria and low regression risk.

Related docs (already in repo):
- `MODERNIZATION_PLAN.md` (execution-ready modernization plan with concrete file references)
- `modernization-plan-2.md` (design-level push toward deterministic simulation)
- `docs/modernization-audit-v2.md` (audit findings, priorities)
- `docs/pointer-modernization-plan.md` / `docs/raw-pointer-audit.md` (ownership/RAII modernization)

---

## Goals

- Make module boundaries real (in code + build), not just folders under `src/`.
- Enforce dependency direction rules so “everything includes everything” stops.
- Convert global lifecycle to explicit ownership with RAII (no implicit “who deletes what”).
- Separate pure logic (simulation/data) from side effects (SDL, filesystem, audio, rendering).
- Make tests faster, more isolated, and less dependent on process-wide globals.
- Reduce translation unit size and coupling so refactors are cheaper and safer.
- Establish long-term sustainable practices: header hygiene, API stability, tooling, CI gating.

## Non-Goals

- Rewriting gameplay rules “for cleanliness”.
- Replacing SDL2, PhysFS, libzip, libyaml, etc. (but we will aggressively wrap them behind internal interfaces).
- Converting the project to C++20 Modules (language feature) in this roadmap. Re-evaluate later after boundaries stabilize.
- Mass reformatting purely for style; churn should buy modularity/testability.

---

## Current Pain Points Observed (Concrete)

These are “symptoms” that block modularity and drive regressions:

- **Global mutable state and `extern` coupling**
  - Example: `src/base.h` exposes `extern screen * myscreen;` and other global interfaces.
  - Many subsystems reach across boundaries via `extern` (picker/menu, input, runtime, etc.), making lifetimes and ordering fragile.

- **Umbrella headers and include fan-in**
  - `base.h` and `graph.h` act as umbrella includes (effectively a pseudo-precompiled header), encouraging transitive dependency reliance.
  - As a result, files compile “by accident” and are hard to move between modules.

- **“Modules” exist in folders, but the build does not enforce boundaries**
  - CMake already defines object targets per folder (`og_core`, `og_data`, `og_entities`, `og_runtime`, `og_render`, `og_input`, `og_ui`, `og_platform`).
  - However, headers are largely flat under `src/` and are not split into public vs private; include paths are broad, so layering isn’t enforced.

- **External library headers leak into internal code**
  - Example signal: `zipint.h` is included widely. That effectively makes libzip “part of the engine API”.
  - This prevents swapping implementations and increases build times/churn.

- **Oversized files / low cohesion**
  - Very large translation units (e.g., picker/editor/walker/view) mix orchestration, domain logic, and side effects.
  - This blocks parallel work and makes “small changes” risky.

- **Tests are integration-heavy and share global state**
  - Many tests construct objects with manual `new`/`delete` and rely on globals rather than fixtures with explicit ownership.
  - This increases flakiness risk and makes it hard to add cheap unit tests.

---

## Target Architecture (Aggressive End State)

### Principles

- **Ports and adapters:** pure modules define interfaces; platform/UI/render provide implementations.
- **Explicit ownership:** “root” objects own everything; subsystems hold references/borrows.
- **No transitive includes:** each `.cpp` includes what it uses; headers are minimal and forward-declare when possible.
- **Public vs private headers:** public headers are stable and do not pull in SDL or third-party headers unless that is explicitly part of the contract.

### Proposed Module Set (Normalized)

We keep today’s directory-driven modules, but tighten and rename them conceptually:

- `og::core`
  - Pure utilities: types, math, time/duration wrappers, logging, error/result types, small containers, compile-time constants.
  - No SDL, no filesystem, no threads (beyond standard library primitives).

- `og::sim` (split from today’s `entities` + parts of `runtime`)
  - Deterministic simulation: entity state, combat resolution, AI decisions, pathing, rules.
  - Takes input snapshots + RNG + dt, emits events (sound requests, spawn FX, text notifications).

- `og::data`
  - Serialization/parsing for campaigns/levels/save/config.
  - Knows file formats, does not know SDL/UI.
  - Should depend on `og::core` and (minimally) on `og::sim` types only where required by the data model.

- `og::io` (a child of platform, but modeled as a service)
  - Wrap PhysFS/libzip and any OS filesystem details behind a narrow interface: `IFileSystem`, `IArchive`, `IStream`.
  - No gameplay logic here.

- `og::runtime`
  - Orchestration: game session state machine, glue between UI and sim, save/load triggers.
  - Owns `GameSession`, `GameContext`, and wiring of services.

- `og::render`
  - Rendering implementation (SDL surfaces/textures), palette, blitters, view rendering.
  - Consumes read-only views of sim state + runtime view models.

- `og::input`
  - Produces `InputSnapshot` from SDL events/devices; input mapping and debouncing live here.

- `og::ui`
  - Menu/editor controllers (ideally “state machines” producing view models + commands).
  - Should not own renderer resources directly; should request assets through services.

- `og::platform`
  - SDL initialization, audio device, threading helpers, platform hooks (Emscripten).
  - Provides implementations for runtime services.

### Dependency Direction Rules (Enforced)

The rule is: dependencies flow inward toward purity.

Allowed arrows:

```
apps -> ui -> runtime -> sim -> core
apps -> render -> core
apps -> input  -> core
runtime -> data -> core
runtime -> io -> core
platform -> io -> core
render -> platform (only through small adapters; prefer runtime-owned service handles)
```

Forbidden (examples):
- `sim` must not include or link `SDL*` or `render/*` or `ui/*`.
- `data` must not depend on `screen`, UI globals, or rendering types.
- `ui` must not directly mutate deep sim internals; it issues commands to runtime/sim.

Enforcement mechanism:
- Each module has its own target with restricted include dirs.
- Public headers live under `include/openglad/<module>/...`.
- Private headers live under `src/<module>/...` and are only visible to that target.

### ASCII Diagram (End-State)

```
                +------------------+
                |       apps       |
                | openglad/openscen|
                +----+--------+----+
                     |        |
              +------v--+  +--v------+
              |  og::ui |  | og::render |
              +-----+---+  +-----+------+
                    |            |
                    v            v
                 +--+------------+--+
                 |    og::runtime   |
                 +----+-------+-----+
                      |       |
              +-------v-+   +-v------+
              | og::sim |   | og::data|
              +----+----+   +----+----+
                   |             |
                   v             v
                 +------------------+
                 |     og::core     |
                 +------------------+

        platform/io/input are adapters owned by runtime/apps.
```

---

## File/Folder Reorganization (Aggressive but Incremental)

### Target Layout

Introduce a conventional modern C++ layout while preserving incremental moves:

```
include/
  openglad/
    core/
    sim/
    data/
    runtime/
    render/
    input/
    ui/
    platform/

src/
  core/
  sim/
  data/
  runtime/
  render/
  input/
  ui/
  platform/

apps/
  openglad/main.cpp
  openscen/main.cpp

third_party/
  physfs/  libzip/  libyaml/  micropather/  yam/  zlib/

tests/
  unit/
  integration/
  support/
```

Notes:
- Today’s `src/glad.cpp` is effectively “app + runtime glue”. In end state it becomes `apps/openglad/main.cpp` with minimal wiring.
- Vendored code under `src/external/**` should move to `third_party/**` to make “what is ours” explicit.
- Introduce transitional “shim” headers if needed to avoid flag-days:
  - Old path header includes the new header and emits a deprecation warning comment.

### Naming and Namespaces

- Introduce namespaces by module: `namespace og::core`, `og::sim`, etc.
- Avoid trying to rename every type at once. Start by namespacing new APIs and adapters, then migrate old types gradually.

---

## API and Ownership Modernization (High Leverage)

### 1) Replace global lifecycle with explicit roots

Target end state:
- `GameSession` owns runtime state (including the equivalent of “screen”) and has explicit start/stop.
- UI menus run inside `UiSession` / `PickerSession` objects whose destructors free resources.

Transitional strategy:
- Keep `extern screen* myscreen` as a non-owning alias for a limited period, sourced from `GameSession`, then delete it once unused.

### 2) Standardize ownership vocabulary in APIs

Rules:
- Owning: `std::unique_ptr<T>` (default), `std::shared_ptr<T>` (rare, only if lifetimes truly overlap).
- Non-owning borrow: `T&` / `T*` with a clear lifetime rule, or `std::span<T>` for buffers.
- Optional: `T*` may represent “nullable borrow”; prefer `std::optional<std::reference_wrapper<T>>` for “sometimes present”.

### 3) Data model decoupling

Aim: save/load and level parsing shouldn’t require live runtime objects.

- Introduce “plain data” types (POD-ish) for serialized entities (e.g., `SimState`, `EntityState`, `LevelState`).
- Provide conversions/adapters between serialized state and runtime/sim objects.

### 4) Rendering decoupling via events and view models

- Simulation emits semantic events: `DamageEvent`, `PlaySoundEvent`, `SpawnFxEvent`, `TextPopupEvent`.
- Runtime aggregates these and hands them to `render`/`audio` implementations.
- UI produces a `UiViewModel` that renderer consumes (no UI code directly owning renderer resources).

---

## Build System and Developer Workflow Improvements

### CMake Target Strategy (Incremental)

1. Turn directory targets into real libraries with public/private includes.
  - Today’s object targets are a good start; the next step is to convert to `STATIC` libraries (or keep OBJECT internally but with strict includes).
2. Centralize compile options:
  - `project_warnings` and `project_sanitizers` already exist: keep and strengthen them per-target.
3. Add explicit “module” targets and link graphs:
  - `og_core`, `og_sim`, `og_data`, `og_runtime`, `og_render`, `og_input`, `og_ui`, `og_platform`, `og_io`.
4. Make external deps link only where needed:
  - Only `og_io` and `og_platform` should link third-party archive/filesystem deps.

### Tooling Gates

Add (as phased opt-in, then enforce):
- `clang-tidy` with a curated check set for modernizing and bugprone checks.
- `clang-format` only after header boundaries are stable; otherwise churn is too expensive.
- Include hygiene checks:
  - Optional: IWYU or a lightweight script to detect new uses of `base.h`/`graph.h`.

---

## Test Strategy (Make It Sustainable)

### Testing Pyramid

- Unit tests (fast, no SDL):
  - `og::core` utilities, math, error/result logic.
  - `og::sim` tick determinism: given seed + input, expected state diff and emitted events.
  - `og::data` parse/encode roundtrips (golden files + property-ish invariants).

- Integration tests (moderate, minimal SDL):
  - Runtime session flows using fake render/audio/filesystem implementations.

- End-to-end tests (slower, real SDL path):
  - Keep a smaller set that exercises picker flows and full game loop.

### Determinism as a Test Contract

Adopt:
- Seeded RNG and explicit time step in sim.
- Snapshot-based regression tests:
  - Serialize a minimal `SimState` and compare stable fields.
  - Compare emitted event sequences (types and essential payload).

### Fixture Ownership

- Tests should construct `GameSession` / `Simulation` objects with RAII, no manual global teardown.
- Ban new test code that calls `new` without immediately wrapping in `unique_ptr`.

---

## Risk Mitigation

Key risks and how to manage them:

- **Churn and merge conflicts**
  - Mitigation: small PRs, mechanical moves separated from semantic changes, and “shim headers” during transitions.

- **Behavior regressions from re-ownership**
  - Mitigation: run ASan/UBSan CI on ownership-heavy phases; add focused regression tests before migrating lifetimes.

- **Save/level format compatibility**
  - Mitigation: treat existing formats as contracts; add decode/encode golden tests before refactoring serializers.

- **Performance regressions**
  - Mitigation: keep performance benchmarks or at minimum timing logs for critical loops (sim tick, pathing).

---

## Staged Roadmap (8 Phases, Incremental PRs)

Each phase is designed to be landed as a sequence of small PRs. Completion criteria are objective gates.

### Phase 1: “Make Boundaries Enforceable” (Build + Headers)

Steps:
1. Create `include/openglad/**` and start moving *new* stable interfaces there (do not move everything at once).
2. In CMake, restrict include directories per target:
   - module targets only see their own `src/<module>` and `include/`.
3. Introduce a policy: no new includes of `src/base.h` or `src/graph.h` in new code.

Completion criteria:
- Each module target builds with restricted include paths.
- At least one module exports a small public header under `include/openglad/...`.
- CI (or local preset) builds all targets with the stricter include settings.

### Phase 2: “Kill Umbrella Includes” (Header Hygiene)

Steps:
1. Replace `#include "graph.h"` with explicit includes in a few leaf `.cpp` files first.
2. Split headers into:
   - `*_fwd.h` (forward declarations)
   - minimal public API headers
   - private implementation headers
3. Add a lightweight check (script or CI grep) that flags new transitive-include reliance patterns.

Completion criteria:
- New code paths compile without `graph.h`.
- Measurable reduction in “most included” internal umbrella headers.

### Phase 3: “Global State Containment” (Runtime Root Objects)

Steps:
1. Introduce/expand `GameSession` (or similar) as the single owner of the screen/runtime state.
2. Make globals (`myscreen`, `theprefs`, button arrays) derived views into session-owned state.
3. Convert teardown paths (picker/editor quit flows) to RAII session destructors.

Completion criteria:
- `openglad` main constructs a session and owns it with RAII.
- Tests can create/destroy sessions repeatedly without leaking or requiring `_exit`.
- No module other than runtime exposes ownership via raw globals.

### Phase 4: “External Boundary Hardening” (IO/Third Party)

Steps:
1. Create `og::io` API: file reads, writes, directory enumeration, mount points.
2. Wrap PhysFS/libzip/libyaml behind interfaces; move direct includes to `.cpp` files.
3. Move `src/external/**` to `third_party/**` and compile it as separate targets.

Completion criteria:
- No internal headers include vendor-private headers like `zipint.h`.
- Only `og_io`/`og_platform` targets link third-party archive/filesystem libs.

### Phase 5: “Simulation Extraction v1” (Headless Core)

Steps:
1. Define `Simulation::tick(InputSnapshot, dt)` and minimal `SimState`.
2. Move pure computations and deterministic logic under `og::sim`.
3. Introduce an event stream output from sim to runtime (sound/fx/text requests).

Completion criteria:
- A headless test binary can run core sim tests without SDL init.
- At least one end-to-end flow runs via runtime calling into sim tick (even if most logic is still legacy under the hood).

### Phase 6: “UI as State Machines” (Decouple Picker/Editor)

Steps:
1. Introduce UI controllers with explicit state and commands.
2. UI produces view models; renderer consumes them.
3. Remove direct UI ownership of renderer resources and direct deletion of runtime objects.

Completion criteria:
- Picker/editor teardown no longer manually deletes global runtime resources.
- UI tests can run with fake render/audio services.

### Phase 7: “Ownership and Type Safety Sweep” (RAII + Domain Types)

Steps:
1. Systematically replace remaining owning raw pointers with `unique_ptr`/`vector`.
2. Introduce domain enums (`enum class`) and `constexpr` constants for IDs and settings.
3. Replace “nullable borrowed pointer” patterns with explicit optionals where useful.

Completion criteria:
- Raw owning pointers are eliminated from non-vendored code (except explicit interop boundaries).
- Sanitizers pass on CI preset for core targets.

### Phase 8: “Stabilize, Document, and Enforce” (Long-Term Sustainability)

Steps:
1. Write a short “Architecture Rules” doc and keep it in `docs/`.
2. Add CI gates:
   - build all targets
   - unit tests (headless)
   - integration tests
   - optional sanitizers nightly
3. Deprecate and delete remaining compatibility shims (`myscreen`, umbrella headers).

Completion criteria:
- Architecture rules are enforced by build + CI, not just by convention.
- The “legacy compatibility layer” is removed or minimal and quarantined.

---

## What “Done” Looks Like (Measurable Outcomes)

- Developers can move a `.cpp` between modules without pulling half the project with it.
- `og::sim` and `og::data` build and test without SDL or platform initialization.
- External dependencies are isolated; most of the code never includes third-party headers.
- New features land by editing a small set of modules with stable interfaces, not global reach.
- Ownership bugs are caught quickly by sanitizers and by unit tests that don’t require full app boot.

