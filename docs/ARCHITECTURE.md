# OpenGlad Architecture

OpenGlad is a cross-platform C++ port of the DOS game **Gladiator** (1995) — a top-down, gauntlet-style action RPG with up to 4-player split-screen multiplayer, 15+ character classes, a built-in scenario editor, and campaign support. Licensed under GPL v2.

The codebase has been through an aggressive modernization (branch `cpp-modernization-plan`) that introduced modular architecture with enforced dependency rules, RAII ownership, a deterministic simulation layer, and a modern CMake build system targeting C++20.

---

## Table of Contents

- [Repository Layout](#repository-layout)
- [Module Structure](#module-structure)
- [Dependency Direction Rules](#dependency-direction-rules)
- [Key Data Structures](#key-data-structures)
- [Game Loop](#game-loop)
- [Campaigns, Levels, and Scenarios](#campaigns-levels-and-scenarios)
- [Build System](#build-system)
- [Test Structure](#test-structure)
- [Important Files and Entry Points](#important-files-and-entry-points)

---

## Repository Layout

```
openglad/
├── include/openglad/       Public headers (stable module API surface)
│   ├── core/               Foundation utilities and shared contracts
│   ├── gameplay/           Simulation + entities + world state
│   ├── resources/          Save/level/config/filesystem/archive APIs
│   ├── interface/          UI, input translation, rendering-facing APIs
│   ├── platform/           Runtime/session/platform bridges (SDL, text client)
│   └── legacy/             Transitional headers (base.h, etc.)
│
├── src/                    Private implementation
│   ├── core/               combat_math.cpp, util.cpp
│   ├── gameplay/           walker/living/weap/treasure/effect, game_world, sim input
│   ├── resources/          gloader, gparser, level/save IO, filesystem/zip/yaml
│   ├── interface/          screen/view/render/text, picker/help/editor/results UI
│   └── platform/           sdl runtime/session/loop/audio/video, text client
│
├── tests/                  Integration test suite (~140 test files)
│   └── unit/               Headless unit tests (no SDL)
│
├── third_party/            Vendored external libraries
│   ├── physfs/             PhysicsFS 3.2.0 (virtual filesystem)
│   │   └── zlib123/        zlib 1.3.1 (compression, used by libzip)
│   ├── libzip/             libzip 1.11.3 (ZIP archive I/O)
│   ├── libyaml/            libyaml 0.2.5 (YAML parser)
│   ├── yam/                C++ adapter over libyaml
│   ├── micropather/        MicroPather (A* pathfinding)
│   └── VENDORED_LIBS.md    Version tracking and upgrade policy
│
├── cmake/                  CMake support files
├── scripts/                Build, test, and CI scripts
├── web/                    Emscripten HTML shell and landing page
├── docs/                   Architecture documentation
│
├── cfg/                    Runtime configuration (openglad.yaml)
├── pix/                    Sprite and tileset assets (.pix format)
├── sound/                  Audio files (WAV, OGG)
├── builtin/                Core game resources
├── extra_campaigns/        Additional game scenarios
├── scen/                   Scenario data files
│
├── CMakeLists.txt          Main build definition
├── CMakePresets.json        Build presets (dev, CI, web)
└── CLAUDE.md               AI assistant instructions
```

---

## Module Structure

OpenGlad now uses **4 top-level components + a core foundation layer**. The
component targets are:

- `og_core`
- `og_gameplay`
- `og_resources`
- `og_interface`
- `og_platform_sdl`

Legacy fine-grained module names (`sim`, `data`, `entities`, `runtime`,
`render`, `input`, `ui`, etc.) remain visible in paths and headers, but
build/dependency enforcement is now done at the component level.

### `og_core` (foundation)

Pure utilities and fundamental types (`combat_math`, `util`, constants, common
interfaces). No gameplay/session ownership logic.

### `og_gameplay`

Deterministic simulation and entity behavior:

- `GameWorld`, `GameplayContext`
- `walker` family (`living`, `weap`, `treasure`, `effect`)
- pathing, combat, AI families, simulation input handling

Gameplay is intentionally sandboxable and does not own rendering/audio devices.

### `og_resources`

Data and persistence:

- campaign/level/save/config loading and parsing
- filesystem/archive/yaml wrappers
- pixie/asset loading

### `og_interface`

UI, view, and presentation logic:

- menus (`picker`, dialogs, campaign/level picker, results/help/intro)
- rendering helpers (`view`, `text`, `radar`, `walker_draw`)
- level editor tooling/state
- player input translation and UI model state

### `og_platform_sdl`

SDL-specific platform/runtime wiring:

- `GameSession`, lifecycle, loop bridges, native input event bridge
- SDL video/audio integration and startup entrypoint support

This is the outermost layer and can include all component headers.

---

## Dependency Direction Rules

Dependencies now flow through components:

```
og_core
  ↑
og_gameplay
  ↑
og_resources
  ↑
og_interface
  ↑
og_platform_sdl
```

### Include Boundary Rules

Each component target is built with restricted include roots:

- `og_gameplay`: `core/`, `gameplay/`
- `og_resources`: `core/`, `gameplay/`, `resources/`
- `og_interface`: `core/`, `gameplay/`, `resources/`, `interface/`
- `og_platform_sdl`: full tree

### Enforcement

Build and CI checks enforce boundaries:

1. CMake target-level include root restriction per component
2. `scripts/check_vendor_leaks.sh` vendor include checks
3. `scripts/check_vendor_leaks.sh` component dependency include checks

### Runtime Context and Thread-Local Rules

Phase 12 retired global context fallback behavior:

- `set_global_context()` is removed
- `ctx()` is strictly session-backed (`current_session->ctx_`) and asserts when no session exists
- gameplay thread-local game-state pointer: `current_game`
- platform thread-local game-state pointer: `current_session`

### Global State Audit (Phase 12)

Allowed process-level globals (documented exceptions):

- `cfg` (process configuration)
- `E_Screen` (global display singleton handle)
- joystick handles and hardware scratch globals (`joysticks`, etc.)
- text/render process scratch state (`letters1`, `letters_big`, `text_buffer`)
- SAI2x color masks/line buffers
- intro palette globals (`pal`, `mypalette`)

Allowed thread-local exception:

- `grass_rng` (rendering scratch RNG; non-authoritative game state)

Accepted immutable registries:

- family/effect/treasure/generator/weapon registries (`s_registry` init-once statics)

---

## Key Data Structures

### Entity System

**`walker`** is the base class for all game entities. It extends `SimEntity` (SDL-free base providing position, size, identity, state, and animation frames) and holds an optional `WalkerRender` component for graphics. It provides movement, combat, AI, and lifecycle management.

```
walker
├── Order order     — entity type (FAMILY_SOLDIER, FAMILY_MAGE, ...)
├── statistics stats — HP, MP, attack, defense, speed, ...
├── guy* myguy      — link to persistent character data (for player characters)
├── obmap* myobmap  — spatial hash for collision queries
├── commands[]      — AI command queue
└── virtual methods: act(), animate(), collide(), walk(), fire(), ...
    (draw methods live in render layer: draw_walker(), draw_walker_tile())
```

**`guy`** is the persistent player character record, tracking name, family (class), stats, experience, and level. Stored in `save_data.team_list[]` as `unique_ptr<guy>`.

**`obmap`** is a 2D spatial hash grid. Entities register their positions; collision queries are O(1) for nearby entities.

### Game World

**`screen`** (extends `video`) is the game world container:

```
screen
├── level_data     — current level tiles, objects, and metadata
│   ├── grid[][]   — tile array
│   ├── oblist     — list<walker*> of all entities
│   └── fxlist     — list<walker*> of effects/FX layer
├── save_data      — current game progress, team roster, scores
│   ├── team_list[MAX_TEAM_SIZE] — array<unique_ptr<guy>, 24>
│   ├── current_campaign
│   └── completed_levels set
├── viewscreen[4]  — split-screen viewports (1–4 players)
└── timer_wait     — frame rate control
```

### Session and Context

**`GameSession`** is the RAII root that owns all runtime state:

```
GameSession
├── screen_owner_  — unique_ptr<screen>
├── ctx_           — GameContext (dependency injection)
│   ├── game_screen — pointer to screen
│   ├── prefs      — unique_ptr<options>
│   ├── config     — cfg_store*
│   ├── rng        — IRandom* (injectable: ProductionRandom, SeededRandom, FixedRandom)
│   ├── input      — InputState (per-frame snapshot)
│   └── sim_events — unique_ptr<SimEventLog> (event accumulator for sim/render decoupling)
└── session pointers — `myscreen_` and `theprefs_` are owned by SessionState and
   accessed directly via `og::runtime::current_session`
```

### Simulation Events and Event Log

The `og::sim` module defines typed events for decoupling game logic from rendering/audio. Entity code emits events during simulation ticks via `SimEventLog`; the runtime layer drains and dispatches them after each tick.

```cpp
enum class EventKind : uint32_t {
    None = 0,
    PlaySound = 4,     // Request sound: a=sound_id, b=0
    Notification = 8,  // Text notification: message in text field
    SetPalette = 11,   // Request palette change: a=0 normal, a=1 blue/freeze
    RequestRedraw = 12 // Force full screen redraw
};

struct Event {
    uint32_t tick;
    EventKind kind;
    uint32_t a, b;       // event-specific payload
    std::string text;    // optional text payload for Notification events
};
```

**Event flow:**
```
Entity code (walker::act, combat, specials, treasure pickup, ...)
  → og::sim::emit_sound(id)           // instead of myscreen->soundp->play_sound()
  → og::sim::emit_notification(msg)   // instead of viewob->set_display_text()
  → og::sim::emit_event(kind, a, b)   // generic structured event
  → SimEventLog accumulates events
       ↓
Runtime layer (after SimWorld::tick() returns)
  → drain SimEventLog
  → dispatch: play sounds, show notifications, apply palette/redraw requests
```

**`SimEventLog`** is owned by `GameContext` (`ctx().sim_events`), making it globally accessible to entity code without passing extra parameters through the call chain.

---

## Game Loop

### Native Build Flow

```
main() [src/glad.cpp]
  ├── io_init()                    Initialize PhysFS filesystem
  ├── cfg.load_settings()          Load openglad.yaml configuration
  ├── GameSession session(cfg)     RAII: create screen, prefs, install globals
  ├── init_input()                 Initialize keyboard/controller mappings
  ├── intro_main()                 Display splash screen
  ├── picker_main()                Enter team picker (blocking menu loop)
  │   ├── picker_mainmenu_loop()   Main menu: New Game / Continue / Load / Quit
  │   │   └── picker buttons → HireMenu, TrainMenu, ViewMenu, ...
  │   └── glad_main()              Start gameplay (called when player clicks GO)
  │       └── game loop (blocking)
  ├── text_shutdown()              Clean up text system
  └── ~GameSession()               Restore globals, free screen
```

### Per-Frame Game Loop

```
game_frame(screen& s, GameLoopFrameState& st)
  ├── SDL_PollEvent → screen::input()    Handle input events
  ├── screen::continuous_input()         Process held keys
  ├── screen::act()                      Game logic tick
  │   └── SimWorld::tick(level, save, ...)  Deterministic simulation
  │       ├── for each entity in oblist:
  │       │   └── walker::act()            AI, movement, combat, specials
  │       ├── dead entity cleanup
  │       ├── treasure/effect lifecycle
  │       ├── check level completion
  │       └── emit events → SimEventLog
  ├── dispatch_sim_events()              Play sounds, show notifications
  └── screen::redraw()                   Render frame
      ├── Clear buffer
      ├── Draw tile grid
      ├── draw_walker() for each visible entity (render layer)
      ├── Score panel / HUD
      ├── Radar minimap
      └── Present to display
```

### Emscripten (Web) Build Flow

The web build cannot use blocking loops. Instead, a state machine drives the game from `requestAnimationFrame`:

```
main()
  ├── ... same init as native ...
  ├── picker_init()
  └── emscripten_set_main_loop(emscripten_frame_wrapper)

emscripten_frame_wrapper()    (called ~60 FPS by browser)
  ├── Accumulate delta time
  ├── Check if enough time for a game frame
  └── switch (g_game_state):
      ├── Picker:  picker_frame()  → if start requested → Playing
      ├── Playing: game_frame()    → if done → Picker
      └── Quit:    cancel main loop
```

---

## Campaigns, Levels, and Scenarios

### Structure

- **Campaign** — A collection of levels with a progression order. Stored as directories under `builtin/` and `extra_campaigns/`.
- **Level** — A single scenario file defining a tile grid, entity placements, objectives, and intro text.
- **Scenario** — The in-game term for a level. Each has a numeric ID; the player progresses through them sequentially.

### Level Data Format

Levels are stored in a binary `.fss` format (versioned). The `level_data` class handles loading and saving:

```
level_data
├── grid[GRID_SIZE][GRID_SIZE]  — tile type array (grass, stone, water, ...)
├── oblist                       — entity spawn positions and types
├── title, description           — level metadata
├── par_value                    — target completion score
├── exits[]                      — connections to other levels
└── version                      — format version for migration
```

### Save Data

`save_data` stores the player's progress across levels:

```
save_data
├── team_list[24]           — array of unique_ptr<guy> (player characters)
├── current_campaign        — active campaign name
├── current_level           — next level to play
├── completed_levels        — set of finished level IDs
├── score, cash             — team resources
└── numplayers              — 1–4 player count
```

Save slots are numbered 0–9. Slot 0 is the auto-save. The game saves after each completed level.

### Campaign Packages

Campaigns can be distributed as ZIP archives. The `zip_api` module handles creation and extraction. PhysFS mounts campaign directories as virtual filesystems, allowing the game to load assets from ZIP files transparently.

---

## Build System

### CMake (Primary)

The project uses CMake 3.25+ with preset-based configuration. The root `CMakeLists.txt` defines all module targets, external library targets, and test binaries.

**Configure and build:**
```bash
cmake --preset ci-test        # Configure
cmake --build --preset ci-test # Build
ctest --preset ci-test         # Run tests
```

### CMake Presets

| Preset | Purpose |
|--------|---------|
| `dev-debug` | Development debug build (Ninja, tests enabled) |
| `dev-release` | Optimized build (RelWithDebInfo, tests off) |
| `ci-test` | CI standard build + all tests |
| `ci-asan` | ASan + UBSan sanitizer build |
| `dev-debug-vcpkg` | With vcpkg toolchain |
| `dev-debug-conan` | With Conan toolchain |
| `web-emscripten` | Emscripten/WebAssembly build |

### CMake Targets

**Module libraries** (native builds):
`og_core`, `og_sim`, `og_data`, `og_entities`, `og_io`, `og_runtime`, `og_render`, `og_input`, `og_ui`, `og_platform`

**External libraries:**
`og_ext_micropather`, `og_ext_yam`, `og_ext_yaml`, `og_ext_zlib`, `og_ext_libzip`, `og_ext_physfs`

**Aggregate target:**
`og_game` — INTERFACE library linking all modules with `--start-group`/`--end-group` for cyclic resolution.

**Executables:**
- `openglad` — The game
- `openscen` — The level editor (same source as openglad, compiled with `-DOPENSCEN`)

**Test executables:**
- `og_unit_tests` — Headless unit tests (no SDL/video init)
- `openglad_test` — Full integration test suite (~140 tests)
- `og_data_tests` — Data/IO module tests (subset)
- `og_runtime_tests` — Runtime module tests (subset)

### Compiler Settings

- **C++ Standard:** C++20 (`CMAKE_CXX_STANDARD 20`)
- **C Standard:** C11 (for vendored C libraries)
- **Warnings:** `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` (project code only)
- **Vendored code:** Compiled with `-w` (all warnings suppressed)
- **Sanitizers:** Optional ASan + UBSan via `ENABLE_SANITIZERS`

### SDL and Headless Build Targets

The project builds two executables from shared source with platform-specific implementations:

- **`openglad`** (SDL client) — Full graphical game with rendering, audio, and input via SDL2. Platform-specific code lives in `src/sdl_client/`.
- **`openglad_text`** (headless client) — SDL-free text-mode client for simulation, testing, and scripting. Platform-specific code lives in `src/text_client/`.

Both targets link the same core modules (`og_core`, `og_sim`, `og_data`, `og_entities`, `og_io`). The boundary is enforced via link-time dispatch: shared code calls functions declared in `level_data_hooks.h` (e.g., `create_level_render`, `level_data_wire_entity_from_screen`), which have separate implementations in `sdl_context_services.cpp` (SDL) and `platform_headless.cpp` (headless).

**Key boundary files:**

| File | Purpose |
|------|---------|
| `src/sdl_client/runtime/sdl_context_services.cpp` | SDL implementations: view control wiring, entity rendering hooks, level draw |
| `src/sdl_client/runtime/walker_render_bridge.cpp` | SDL `walker` member functions: render component, destructor, frame management |
| `src/text_client/platform_headless.cpp` | Headless stubs: filesystem init, no-op/warning render functions |
| `src/text_client/walker_headless.cpp` | Headless `walker` member functions: no render component, sim-only frame tracking |
| `include/openglad/data/level_data_hooks.h` | Shared declarations enforcing signature parity between SDL and headless |

**Render component pattern:** `walker` holds an optional `std::unique_ptr<WalkerRender> render_`. SDL builds create the component in `attach_render()`; headless builds leave it null. Entity code checks `if (render_)` before delegating to the render component. `LevelData` follows the same pattern with `std::unique_ptr<LevelRender> renderer_`.

### Legacy Build Scripts

Shell scripts in `scripts/` provide convenience wrappers:

| Script | Purpose |
|--------|---------|
| `build_native.sh` | Quick native build via CMake dev-release preset |
| `build_test.sh` | Build test binary via CMake |
| `build_web.sh` | Emscripten/WASM build to `dist/` |
| `build_coverage.sh` | Coverage instrumentation with lcov report |
| `collect_baseline_metrics.sh` | Performance/size tracking for CI |

### Web Build

The Emscripten build compiles to WebAssembly with SDL2 ports:

```bash
source /path/to/emsdk/emsdk_env.sh
./scripts/build_web.sh
# Output: dist/play.html, play.js, play.wasm, play.data
cd dist && python3 -m http.server 8080
```

Key flags: `-sUSE_SDL=2`, `-sUSE_SDL_MIXER=2`, `-sASYNCIFY`, `-sALLOW_MEMORY_GROWTH=1`, `-sINITIAL_MEMORY=67108864` (64MB).

---

## Test Structure

### Test Pyramid

```
┌─────────────────────────────────────────┐
│         End-to-End / Smoke Tests        │  Full game flows, menu navigation
│         (openglad_test, ~140 tests)     │  Requires SDL (offscreen driver)
├─────────────────────────────────────────┤
│         Module Integration Tests        │  Data/IO, runtime subsystems
│   (og_data_tests, og_runtime_tests)     │  Requires SDL (offscreen driver)
├─────────────────────────────────────────┤
│           Headless Unit Tests           │  Pure logic, no SDL init
│           (og_unit_tests)               │  GameSession RAII, sim determinism
└─────────────────────────────────────────┘
```

### Test Frameworks

**Integration tests** (`tests/test_framework.h`):
- Self-registering via `REGISTER_TEST(func)` macro
- Assertions: `TEST_ASSERT(cond, msg)`, `TEST_ASSERT_EQ(expected, actual, msg)`
- Optional fixtures: `REGISTER_TEST_WITH_FIXTURE(func, setup, teardown)`
- Substring filtering: `./openglad_test picker` runs only picker-related tests
- Trace system: `TRACE("category", "message")` for behavioral verification

**Unit tests** (`tests/unit/unit.h`):
- Lightweight `OG_UNIT_TEST(name)` macro, `OG_ASSERT(cond)`
- No SDL initialization; pure logic only

### UI/Menu Testing Pattern

Menu functions block in event loops. Tests use a separate thread to drive navigation:

```cpp
static int injector_thread(void* data) {
    wait_for_interactable("button_id", 5000);  // Wait for button to exist
    SDL_Delay(1500);                            // Wait for fadeblack animation
    interact("button_id");                      // Click by ID
    return 0;
}

void test_menu_flow() {
    SDL_Thread* t = SDL_CreateThread(injector_thread, "inj", nullptr);
    g_picker_max_mainmenu_calls = 1;  // Limit loop iterations
    picker_main(0, NULL);             // Blocks until menus unwind
    SDL_WaitThread(t, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
}
REGISTER_TEST(test_menu_flow);
```

### CI Pipeline

The GitHub Actions workflow (`.github/workflows/test.yml`) runs:

1. **test** — Build and run `og_unit_tests`, `og_data_tests`, `og_runtime_tests`
2. **build** — Native release build (`openglad`, `openscen`)
3. **asan** — ASan + UBSan build and test
4. **baseline-metrics** — Build time, test time, binary size tracking

---

## Important Files and Entry Points

| File | Purpose |
|------|---------|
| `src/glad.cpp` | **Entry point.** `main()`, Emscripten frame wrapper, game state machine |
| `src/runtime/screen.cpp` | Game world: `act()` delegates to `SimWorld::tick()`, `redraw()` renders + dispatches events |
| `src/runtime/game_loop.cpp` | Per-frame loop: `game_frame()` and `game_frame_with_result()` |
| `src/runtime/game_session.cpp` | RAII root: creates screen, prefs, installs legacy globals |
| `src/runtime/game_context.cpp` | `GameContext` and `ctx()` global accessor |
| `src/entities/walker.cpp` | Base entity class — all game objects inherit from this |
| `src/entities/living.cpp` | AI behavior for enemies and NPCs |
| `src/ui/picker.cpp` | Team selection UI — main menu loop |
| `src/ui/level_editor.cpp` | Scenario editor (openscen binary) |
| `src/render/video.cpp` | SDL2 graphics layer — pixel buffer management |
| `src/render/view.cpp` | Viewport/camera system, split-screen rendering |
| `src/data/level_data.cpp` | Level file loading and saving |
| `src/data/save_data.cpp` | Save game serialization |
| `src/input/input.cpp` | Keyboard/controller event handling |
| `src/sim/sim_world.cpp` | Live game simulation tick (extracted from `screen::act()`) |
| `src/sim/sim_event_log.cpp` | Event accumulator: decouples sim from rendering/audio |
| `src/render/walker_draw.cpp` | Entity draw methods (extracted from `walker.cpp`) |
| `CMakeLists.txt` | Build system — module targets, test binaries, install rules |
| `CMakePresets.json` | Build presets for dev, CI, and web |
| `docs/architecture-rules.md` | Enforced module dependency rules |
| `tests/test_main.cpp` | Integration test runner entry point |
| `tests/unit/unit_main.cpp` | Unit test runner entry point |
