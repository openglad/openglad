# SIM/Rendering Split Audit V2

Date: 2026-02-16  
Branch: `feat/sim-rendering-split`  
Commit audited: `32f102f`

## 1. Executive Summary
The split is materially improved versus the older audit, but it is not clean. Sim headers and entity headers are now SDL-free, and entity-to-runtime direct calls have mostly been replaced by events; however, the architecture contract is still broken in multiple core places: `sim/` still depends on `entities/` and `data/`, `input/` directly depends on runtime/render, `data/` directly depends on runtime/render, and `core/` is not a pure base layer (it pulls SDL, runtime, entities, and legacy). Transitive leakage is still severe: 50/51 entity source TUs pull SDL through `core/` -> `legacy/base.h` -> `input/input.h` -> `render/video.h`.

## 2. Violation Table
| File:Line | Violation | Severity |
|---|---|---|
| `src/sim/sim_world.cpp:10` | `sim/` includes `entities/` (`walker.h`) | critical |
| `src/sim/sim_world.cpp:12` | `sim/` includes `data/level_data.h` (forbidden by `sim -> core,input` rule) | critical |
| `src/sim/sim_world.cpp:13` | `sim/` includes `data/save_data.h` (forbidden by `sim -> core,input` rule) | critical |
| `src/sim/sim_input_handler.cpp:14` | `sim/` includes `entities/` (`walker.h`) | critical |
| `src/sim/sim_input_handler.cpp:15` | `sim/` includes `data/level_data.h` | critical |
| `src/sim/sim_input_handler.cpp:17` | `sim/` includes `legacy/base.h` (transitively pulls render/SDL) | critical |
| `src/sim/sim_input_handler.cpp:18` | `sim/` includes `legacy/test_trace.h` (legacy coupling in sim) | major |
| `src/entities/walker.cpp:33` | `entities/` source includes `render/pixien.h` (backwards dependency) | major |
| `src/entities/treasure.cpp:28` | `entities/` source includes `render/pixien.h` (backwards dependency) | major |
| `include/openglad/entities/guy.h:37` | Entity public API requires `screen*` (runtime type in entities interface) | major |
| `include/openglad/entities/guy.h:38` | Entity public API requires `screen*` (runtime type in entities interface) | major |
| `include/openglad/input/input.h:28` | `input/` includes `render/video.h` | critical |
| `include/openglad/input/button.h:21` | `input/` includes `render/pixien.h` | critical |
| `include/openglad/input/button.h:22` | `input/` includes `render/text.h` | critical |
| `include/openglad/input/button.h:23` | `input/` includes `runtime/screen.h` | critical |
| `src/input/input.cpp:24` | `input/` includes `runtime/game_context.h` | critical |
| `src/input/input.cpp:26` | `input/` includes `runtime/screen.h` | critical |
| `src/input/input.cpp:280` | `input/` includes `runtime/screen.h` (in-file include) | critical |
| `src/input/input.cpp:281` | `input/` includes `render/view.h` (in-file include) | critical |
| `src/input/button.cpp:19` | `input/` includes `runtime/game_context.h` | critical |
| `include/openglad/data/level_data.h:36` | `data/` includes `render/smooth.h` | critical |
| `src/data/level_data.cpp:23` | `data/` includes `render/pixie.h` | critical |
| `src/data/level_data.cpp:24` | `data/` includes `render/pixien.h` | critical |
| `src/data/level_data.cpp:28` | `data/` includes `render/smooth.h` | critical |
| `src/data/level_data.cpp:29` | `data/` includes `runtime/screen.h` | critical |
| `src/data/level_data.cpp:30` | `data/` includes `runtime/game_context.h` | critical |
| `src/data/level_data.cpp:31` | `data/` includes `render/view.h` | critical |
| `src/data/gloader.cpp:19` | `data/` includes `render/pixien.h` | critical |
| `src/data/gloader.cpp:20` | `data/` includes `runtime/game_context.h` | critical |
| `src/data/gloader.cpp:21` | `data/` includes `runtime/screen.h` | critical |
| `include/openglad/core/combat_math.h:9` | `core/` includes `SDL.h` | critical |
| `include/openglad/core/util.h:24` | `core/` includes `SDL.h` | critical |
| `include/openglad/core/stats.h:21` | `core/` includes `legacy/base.h` (massive non-core dependency) | critical |
| `src/core/combat_math.cpp:9` | `core/` includes `runtime/game_context.h` | critical |
| `src/core/stats.cpp:23` | `core/` includes `entities/family_descriptor.h` | critical |
| `src/core/stats.cpp:24` | `core/` includes `entities/family_registry.h` | critical |
| `src/core/stats.cpp:25` | `core/` includes `entities/guy.h` | critical |
| `src/core/stats.cpp:26` | `core/` includes `entities/walker.h` | critical |
| `src/core/stats.cpp:27` | `core/` includes `runtime/screen.h` | critical |
| `src/core/stats.cpp:28` | `core/` includes `render/view.h` | critical |
| `src/core/stats.cpp:30` | `core/` includes `runtime/game_context.h` | critical |
| `src/core/util.cpp:33` | `core/` includes `legacy/base.h` | critical |
| `src/core/util.cpp:39` | `core/` includes `windows.h` (platform coupling in core) | major |
| `src/render/view.cpp:449` | Render layer mutates sim ownership state (`control->user`) | major |
| `src/render/view.cpp:450` | Render layer mutates sim behavior state (`set_act_type`) | major |
| `src/render/view.cpp:486` | Render layer mutates combat state directly (`hitpoints = -1`) | major |
| `src/render/view.cpp:487` | Render layer invokes combat action (`control->attack`) | major |
| `src/render/view.cpp:488` | Render layer invokes entity death path (`w->death()`) | major |
| `src/render/view.cpp:510` | Render layer spawns sim entity (`level_data.add_ob`) | major |
| `src/render/view.cpp:512` | Render layer mutates spawned entity team state | major |
| `src/render/view.cpp:527` | Render layer mutates HP directly | major |
| `src/render/view.cpp:540` | Render layer mutates MP directly | major |
| `src/render/view.cpp:544` | Render layer mutates speed bonus state | major |
| `src/render/view.cpp:551` | Render layer performs transform/gameplay mutation | major |
| `tests/unit/test_sim_world_headless.cpp:5` | Sim-focused unit test depends on `runtime/game_context.h` | minor |

## 3. Transitive Dependency Analysis
### Sim
- Header transitive check (`include/openglad/sim/*.h`): **0 headers leak SDL/runtime/render/data/entities**.
- Source transitive check (`src/sim/*.cpp`): **2/4 files leak forbidden deps** (`sim_world.cpp`, `sim_input_handler.cpp`).
- Concrete chain proven with compiler include tree (`g++ -H -fsyntax-only src/sim/sim_world.cpp`):
  - `src/sim/sim_world.cpp` -> `include/openglad/core/stats.h` -> `include/openglad/legacy/base.h` -> `include/openglad/input/input.h` -> `include/openglad/render/video.h` -> `/usr/include/SDL2/SDL.h`.

### Entities
- Header transitive SDL check (`include/openglad/entities/*.h`): **0 leaks**.
- Source transitive check: **50/51 entity source TUs include SDL transitively** (primarily through `core/stats.h` -> `legacy/base.h`).
- Direct backwards deps remain in `src/entities/walker.cpp` and `src/entities/treasure.cpp` (`render/pixien.h`).

### Input
- Not split-clean. `input.h`/`button.h` include render/runtime; `input.cpp`/`button.cpp` include runtime directly.

### Data
- Not split-clean. `level_data.h` and data sources directly include render and runtime headers.

### Core
- Not split-clean. `core` includes SDL, runtime, entities, render, and legacy headers; this inverts the intended dependency direction.

## 4. Remaining Technical Debt
### A) Boundary Violations (must-fix)
- Make `core/` truly foundational: remove SDL/runtime/entities/render/legacy dependencies.
- Break `sim/` dependence on `entities/` and `data/` (introduce pure sim-facing interfaces/types).
- Split legacy `input/` into platform input vs pure input abstractions; keep `input_state` as the clean path.
- Remove render/runtime dependencies from `data/` (separate runtime-only loaders/draw paths).

### B) Transitive Leak Sources
- `include/openglad/core/stats.h` including `legacy/base.h` is the primary contamination path.
- `include/openglad/legacy/base.h` pulls `input/input.h`, which pulls render/SDL.

### C) Render-Side Sim Logic
- `src/render/view.cpp` still performs state mutation, combat actions, and entity spawning in cheat/debug paths.
- These should move to sim commands/events so render only maps raw input and presents results.

### D) Headless Gaps
- Headless walker creation exists and passes tests, but sim-world testing is still shallow:
  - no unit test calls `SimWorld::tick()` headlessly,
  - `tests/unit/test_sim_world_headless.cpp` still imports runtime context.

## 5. What's Actually Clean
- `include/openglad/sim/` headers are clean and do not transitively pull SDL/runtime/render.
- `include/openglad/entities/` headers are clean with respect to SDL leakage (old `walker.h` -> `pixien.h` -> `SDL.h` style leak is gone).
- Entity direct runtime calls (`myscreen->...`/`active_screen()->...`) are largely removed and replaced with event emission (`EndGame`, `DamageTile`, `SetEnd`, `PlaySound`, `Notification`, `SetPalette`, `RequestRedraw`).
- Runtime event dispatch in `src/runtime/screen.cpp` is coherent and complete for currently defined event kinds.
- Build and tests on this branch are green:
  - `cmake --preset ci-test && cmake --build build/ci-test -j$(nproc)` passed.
  - `ctest --output-on-failure` passed (7/7, with `emscripten_build_test` skipped).

## 6. Recommendations
1. **Fix `core/` first**: remove `legacy/base.h`, SDL types, and runtime/entities/render includes from `core` headers/sources. This is the root of most transitive leakage.
2. **Define strict sim-facing interfaces** for world state and entities; stop including `data/` and `entities/` directly in `sim/`.
3. **Split legacy input**: isolate SDL/runtime/render-specific code from pure input abstractions; keep `input_state` and action mapping independent.
4. **Move render cheat mutations into sim commands/events** so `render/view.cpp` does not mutate gameplay state directly.
5. **Decouple `data/` from render/runtime** by extracting drawing/runtime helpers into runtime/render modules.
6. **Strengthen headless tests**: add `SimWorld::tick()` tests using sim-only fixtures and remove runtime includes from sim unit tests.
