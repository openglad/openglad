# OpenGlad Architecture

OpenGlad is a cross-platform C++ port of the DOS game **Gladiator** (1995) — a top-down, gauntlet-style action RPG with up to 4-player split-screen multiplayer, 15+ character classes, a built-in scenario editor, and campaign support. Licensed under GPL v2.

The codebase has been through an aggressive modernization (branch `cpp-modernization-plan`) that introduced modular architecture with enforced dependency rules, RAII ownership, a deterministic simulation layer, and a modern CMake build system targeting C++20.

---

## Table of Contents

- [Repository Layout](#repository-layout)
- [Module Structure](#module-structure)
- [Component Boundaries (Phase 12)](#component-boundaries-phase-12)
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
│   ├── core/               Pure utilities, math, constants
│   ├── sim/                Deterministic simulation types
│   ├── data/               Serialization and file format types
│   ├── entities/           Game entity class hierarchy
│   ├── io/                 Filesystem abstraction interfaces
│   ├── runtime/            Game session, context, loop
│   ├── render/             Graphics and viewport types
│   ├── input/              Input handling and button types
│   ├── ui/                 Menu state machines and view models
│   ├── platform/           Platform I/O abstraction
│   └── legacy/             Transitional headers (base.h, etc.)
│
├── src/                    Private implementation
│   ├── core/               combat_math.cpp, stats.cpp, util.cpp
│   ├── sim/                sim_event_log
│   ├── gameplay/           game_world, gameplay_context
│   ├── data/               pixie_data
│   ├── resources/          gloader, gparser, level_file_io, save_data, campaign_data
│   ├── entities/           walker, living, weap, treasure, effect, guy, obmap
│   ├── io/                 physfs_api, platform_io, yaml_stream, zip_api
│   ├── runtime/            screen, game_loop, game_session, game_context, ...
│   ├── render/             video, view, pixie, pixien, text, radar, pal32, walker_draw, ...
│   ├── input/              input.cpp, button.cpp
│   ├── ui/                 picker, level_editor, campaign_picker, intro, help, ...
│   ├── platform/           sound.cpp
│   └── glad.cpp            Entry point (main function)
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

The codebase is organized into 11 internal modules, each built as a separate static library with restricted include paths. Public headers live under `include/openglad/<module>/`; private implementation lives under `src/<module>/`.

### og_core — Pure Utilities

Pure math, logging, time helpers, and compile-time constants. No SDL, no filesystem, no threads beyond the standard library.

| File | Purpose |
|------|---------|
| `core/combat_math.cpp` | Damage calculation, hit/miss formulas |
| `core/stats.cpp` | Entity statistics, command queue management |
| `core/util.cpp` | Logging (`Log`/`LogWarn`/`LogError` via `std::format`), timer functions, safe int parsing |

### og_sim — Deterministic Simulation

Headless, SDL-free simulation layer. Given a seed + input sequence, produces identical state and typed events. This is the foundation for separating game logic from rendering.

`screen::act()` delegates to `GameWorld::tick()`, which executes all entity logic, handles dead entity cleanup, checks level completion, and emits events into a `SimEventLog`. The runtime layer then drains events and dispatches them to sound, HUD, and visual effects.

Entity code emits events via `sim_emit.h` helpers (`emit_sound()`, `emit_notification()`, `emit_event()`) instead of making direct rendering/audio calls, completing the decoupling of simulation from presentation.

| File | Purpose |
|------|---------|
| `gameplay/game_world.cpp` | `GameWorld::tick()` — live game simulation tick (extracted from `screen::act()`) |
| `sim/sim_event_log.cpp` | `SimEventLog` — accumulates events during a tick for deferred dispatch |
| `sim/sim_emit.h` | Convenience helpers: `emit_sound()`, `emit_notification()`, `emit_event()` |
| `sim/event.h` | `EventKind` enum: PlaySound, Notification, SetPalette, RequestRedraw |

### og_data — Data Types

Gameplay data structure definitions (structs, types) that don't do file I/O. Public headers in `include/openglad/data/` define types used across modules.

| File | Purpose |
|------|---------|
| `data/pixie_data.cpp` | Sprite/animation metadata (`PixieData` struct) |

### og_resources — File I/O and Serialization

All file format parsing and serialization: campaigns, levels, saves, configuration, and content loading. Knows file formats but not SDL or UI.

| File | Purpose |
|------|---------|
| `resources/gloader.cpp` | Loads game content from packages (sprites, AI definitions) |
| `resources/gparser.cpp` | Configuration and metadata parsing (`cfg_store`) |
| `resources/level_file_io.cpp` | Level file load/save free functions (`load_level()`, `save_level()`) |
| `resources/save_data.cpp` | Save game serialization with versioning (`SaveDataIoError` enum) |
| `resources/campaign_data.cpp` | Campaign metadata and progression tracking |

### og_entities — Game Object Hierarchy

All game objects (players, enemies, projectiles, items, effects) derive from `walker`. This module contains entity behavior, AI, combat, movement, and pathfinding. Drawing methods have been extracted to the render layer (`render/walker_draw.cpp`) to enforce the sim/rendering split — entity code no longer depends on viewscreen or video types.

| File | Purpose |
|------|---------|
| `entities/walker.cpp` | Base entity: position, animation, stats, lifecycle |
| `entities/walker_combat.cpp` | Combat: `attack()`, `fire_check()`, damage dealing |
| `entities/walker_movement.cpp` | Movement: `walk()`, `walkstep()`, `setworldxy()` |
| `entities/walker_pathing.cpp` | A* pathfinding via MicroPather integration |
| `entities/walker_specials.cpp` | Special abilities: teleport, summon, turn undead |
| `entities/living.cpp` | AI-controlled entities (enemies, NPCs) extending `walker` |
| `entities/weap.cpp` | Projectiles and weapons extending `walker` |
| `entities/treasure.cpp` | Collectible items (gold, potions, food) |
| `entities/effect.cpp` | Visual effects (explosions, spells, blood) |
| `entities/guy.cpp` | Persistent player character data (stats, XP, name) |
| `entities/obmap.cpp` | Spatial hash grid for O(1) collision detection |

**Class hierarchy:**
```
SimEntity (SDL-free base: position, size, identity, state, animation frames)
└── walker (base entity, optional WalkerRender component for graphics)
    ├── living (AI entities with health, behavior, specials)
    ├── weap (projectiles and weapons)
    ├── treasure (collectible items)
    └── effect (visual effects)

guy — persistent player character data (not a walker; bound via walker::myguy)
obmap — spatial hash for collision queries
WalkerRender — concrete render component wrapping pixieN (SDL only; nullptr in headless)
LevelRender — concrete tile renderer wrapping PIX_MAX pixieN sprites (SDL only; nullptr in headless)
```

### og_io — Filesystem Abstraction

Wraps PhysFS, libzip, and libyaml behind narrow interfaces. Only this module (and og_platform) may include vendor headers.

| File | Purpose |
|------|---------|
| `io/physfs_api.cpp` | PhysFS virtual filesystem mounting and file I/O |
| `io/platform_io.cpp` | Platform-specific I/O operations |
| `io/yaml_stream.cpp` | YAML configuration parsing via libyaml |
| `io/zip_api.cpp` | ZIP archive creation and extraction |

### Resources Filesystem and Campaign I/O API

`include/openglad/resources/filesystem.h` exposes the SDL-free `og::resources` filesystem API used by both SDL and headless clients:

- `mount(archive, mountpoint, append_to_path)` / `unmount(archive)` for PhysFS mount stack management
- `read_file(path)` / `write_file(path, data, len)` for binary file I/O
- `exists(path)` for path existence checks
- `enumerate_files_sorted(dirname)` for deterministic directory listings
- `last_error()` for backend error detail propagation

`include/openglad/platform/io_common.h` adds shared campaign/archive helpers used by both `src/platform/sdl` and `src/platform/text`:

- Campaign package lifecycle: `mount_campaign_package_with_error`, `unmount_campaign_package_with_error`, `remount_campaign_package_with_error`, `load_campaign_with_error`
- Campaign/level discovery: `list_campaigns`, `list_levels`, `list_levels_v`
- Archive packing/unpacking: `zip_contents_with_error`, `unzip_into_with_error`, `repack_campaign`, `unpack_campaign`

### og_runtime — Game Session Orchestration

Owns the game session lifecycle, wires services together, manages the game loop and screen state machine.

| File | Purpose |
|------|---------|
| `runtime/screen.cpp` | Game world: delegates to `GameWorld::tick()` for logic, owns redraw/event dispatch |
| `runtime/game_loop.cpp` | Per-frame loop: `game_frame()` → input → act → render |
| `runtime/game_session.cpp` | `GameSession` — RAII root owning screen, prefs, RNG |
| `runtime/game_context.cpp` | `GameContext` — dependency injection container (`ctx()`) |
| `runtime/game.cpp` | Game logic integration point |
| `runtime/glad_gameplay.cpp` | Gameplay initialization and level transitions |
| `runtime/score_panel.cpp` | HUD: HP/MP bars, score, team/foe counts, radar |
| `runtime/screen_lifecycle.cpp` | Screen creation, reset, cleanup |
| `runtime/legacy_globals.cpp` | Transitional global variable shims |

**Key types:**

- **`GameSession`** — RAII root for all runtime state. Owns the `screen`, `options`, and RNG. Installs legacy global shims (`myscreen`, `theprefs`). Production `main()` constructs one; tests construct one per test.
- **`GameContext`** — Dependency injection container. Holds direct references to screen, prefs, config, RNG, and input state. Accessed globally via `ctx()`.
- **`screen`** — The game world container. Extends `video` (graphics layer). Contains `level_data`, `save_data`, and up to 4 `viewscreen` objects for split-screen. The `act()` method delegates to `GameWorld::tick()` for game logic; `redraw()` handles rendering and event dispatch.

### og_render — Graphics and Display

SDL2 rendering: pixel buffers, viewports, sprites, text, radar, palette management, and image scaling.

| File | Purpose |
|------|---------|
| `render/video.cpp` | SDL2 graphics abstraction: pixel buffer, drawing primitives |
| `render/view.cpp` | Viewport/camera system (1–4 split-screen views), HUD overlay |
| `render/pixie.cpp` | Base sprite/image class |
| `render/pixien.cpp` | Animated sprite with frame management |
| `render/text.cpp` | Bitmap font rendering system |
| `render/radar.cpp` | Minimap display |
| `render/pal32.cpp` | 8-bit to 32-bit palette conversion |
| `render/smooth.cpp` | Tile edge smoothing algorithm |
| `render/sai2x.cpp` | SAI2x pixel-art upscaling |
| `render/walker_draw.cpp` | Entity rendering: `draw_walker()`, `draw_walker_tile()`, `draw_walker_path()`, health bars |
| `render/graphlib.cpp` | Graphics utility functions |

**Rendering pipeline:**
```
screen::redraw()
  → video::clearbuffer()
  → draw tile grid (level_data)
  → draw_walker(w, view_buf) for each visible entity  (render layer, not entity layer)
  → score_panel() — HUD overlay (HP, MP, score, specials)
  → radar::draw() — minimap
  → dispatch_sim_events() — play sounds, show notifications from tick
  → video::buffer_to_screen() — present to SDL window
```

### og_input — User Input

Translates SDL keyboard, mouse, and joystick events into game actions. Manages per-player key bindings and the UI button widget system.

| File | Purpose |
|------|---------|
| `input/input.cpp` | Event polling, key mapping, per-player input state |
| `input/button.cpp` | UI button widgets (`vbutton`), `allbuttons[]` global array |

### og_ui — User Interface

Menu controllers, the team picker, level editor, intro screen, help, and results display.

| File | Purpose |
|------|---------|
| `ui/picker.cpp` | Main team selection and hiring UI |
| `ui/picker_main_menu.cpp` | Main menu state machine flow |
| `ui/picker_team_build.cpp` | Character stat editing and purchasing |
| `ui/picker_input.cpp` | Menu input event handling |
| `ui/picker_dialogs.cpp` | Dialog boxes and confirmations |
| `ui/picker_accessible_levels.cpp` | Level accessibility and gating logic |
| `ui/campaign_picker.cpp` | Campaign selection UI |
| `ui/level_picker.cpp` | Level selection UI |
| `ui/level_editor.cpp` | Scenario editor (openscen) |
| `ui/level_editor_file_ops.cpp` | Editor file I/O |
| `ui/level_editor_tools.cpp` | Editor placement/deletion tools |
| `ui/level_editor_ui.cpp` | Editor UI rendering |
| `ui/intro.cpp` | Splash/intro screen |
| `ui/help.cpp` | In-game help display |
| `ui/results_screen.cpp` | Post-battle results and level completion |

### og_platform — Platform Services

SDL initialization, audio device management, and platform-specific hooks (Emscripten async support).

| File | Purpose |
|------|---------|
| `platform/sound.cpp` | SDL_mixer audio: sound effects and music playback |

---

## Component Boundaries (Phase 12)

Phase 12 finalizes dependency enforcement with component-level rules:

- `gameplay` component may include only `core/` and `gameplay/` headers
- `resources` component may include only `core/`, `gameplay/`, and `resources/` headers
- `interface` component may include only `core/`, `gameplay/`, `resources/`, and `interface/` headers
- `platform/sdl` is the top integration layer and may include all component headers

CI enforces this through `scripts/check_vendor_leaks.sh`:

- vendor header leak checks
- component include dependency checks

Accepted process globals:

- Renderer/hardware globals: `E_Screen`, `joysticks`, `letters1`, `letters_big`, `text_buffer`, SAI2x masks/buffers, `pal`, `mypalette`
- Process config global: `cfg`
- Immutable registries: family/effect/treasure/generator/weapon init-once registries
- Rendering scratch exception: `grass_rng` thread-local in SDL rendering
- Lightweight-client context override: `set_global_context()` thread-local override for text/headless clients that do not construct a `GameSession` (must be set/cleared by the client session boundary)
- Resources-layer fallback mount cache: `s_mounted_campaign` in `src/resources/platform_io.cpp` for campaign mount operations that run before any runtime session exists
- Test-only globals under `#ifdef TESTING` and Emscripten-only globals under `#ifdef __EMSCRIPTEN__`

Final thread-local audit state:

- Gameplay game-state thread-local: `current_game`
- Platform game-state thread-local: `current_session`

## Dependency Direction Rules

Dependencies flow **inward toward purity**. Outer layers depend on inner layers; never the reverse.

```
                    ┌───────────────────┐
                    │       apps        │
                    │ openglad / openscen│
                    └────┬─────────┬────┘
                         │         │
                  ┌──────▼──┐  ┌──▼────────┐
                  │  og_ui  │  │ og_render  │
                  └────┬────┘  └─────┬──────┘
                       │             │
                       ▼             ▼
                  ┌────┴─────────────┴───┐
                  │      og_runtime      │
                  └──┬──────┬────────┬───┘
                     │      │        │
              ┌──────▼──┐ ┌─▼──────┐ │
              │ og_sim  │ │og_data │ │
              └────┬────┘ └───┬────┘ │
                   │          │      │
                   │   ┌──────▼──────▼──┐
                   │   │ og_resources   │
                   │   └──────┬─────────┘
                   │          │
                   ▼          ▼
                  ┌───────────────────┐
                  │     og_core       │
                  └───────────────────┘

   og_input → og_core
   og_io → og_core
   og_platform → og_io → og_core
```

### Allowed Dependencies

| Module | May depend on |
|--------|---------------|
| `og_core` | Standard library only |
| `og_sim` | `og_core` |
| `og_data` | `og_core` |
| `og_resources` | `og_core`, `og_data`, `og_io`, `og_entities`, micropather |
| `og_io` | `og_core`, vendored I/O libs (physfs, libzip, libyaml) |
| `og_entities` | `og_core`, `og_sim` (event emission), `og_render` (for pixieN base), micropather |
| `og_runtime` | `og_core`, `og_sim`, `og_data`, `og_resources`, `og_entities`, `og_io`, `og_render`, `og_input` |
| `og_render` | `og_core`, SDL2 |
| `og_input` | `og_core`, SDL2 |
| `og_ui` | `og_runtime`, `og_render`, `og_input`, `og_data`, `og_resources` |
| `og_platform` | `og_io`, SDL2 |

### Forbidden Dependencies

- `og_sim` must NOT include SDL, render, ui, or platform headers
- `og_data` must NOT depend on screen, UI globals, or rendering types
- `og_core` must NOT depend on any other module
- `og_entities` must NOT call rendering/audio/UI directly — use `og::sim::emit_*()` helpers instead
- `og_ui` must NOT directly mutate deep sim internals — it issues commands to runtime

### Enforcement

A build-time check enforces these boundaries:

1. **`check_vendor_leaks.sh`** — Vendor headers (physfs, libzip, libyaml, zipint.h) may only appear in `src/resources/io/` (and SDL-specific I/O shims under `src/platform/sdl/io/`). Public headers under `include/openglad/` must never include vendor headers.

This check runs as a custom CMake target (`check_vendor_leaks`) that executes before the `og_game` aggregate target.

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
└── legacy shims   — installs myscreen, theprefs globals
```

`GameSession` owns `GameWorld` directly as member `world_` (`include/openglad/platform/game_session.h`). `screen` receives and stores a non-owning `GameWorld*` (`include/openglad/interface/screen.h`). This replaced the older ownership model where `screen` owned the world.

### PlatformBridge Callback Pattern

`include/openglad/interface/platform_bridge.h` defines `og::interface::PlatformBridge`, an SDL-free callback table used by interface/runtime code:

- `present_frame`
- `play_sound`
- `play_music`
- `stop_music`
- `create_surface`
- `clear_stale_view_controls`

Platform-specific code installs one implementation at startup:

- SDL build: `src/platform/sdl/runtime/sdl_context_services.cpp`
- Headless build: `src/platform/text/platform_headless.cpp`

This keeps interface-layer headers and call sites free of SDL types while still allowing platform-specific rendering/audio behavior.

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
Runtime layer (after GameWorld::tick() returns)
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
  │   └── GameWorld::tick(level, save, ...)  Deterministic simulation
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
`og_core`, `og_sim`, `og_data`, `og_resources`, `og_entities`, `og_io`, `og_runtime`, `og_render`, `og_input`, `og_ui`, `og_platform`

**External libraries:**
`og_ext_micropather`, `og_ext_yam`, `og_ext_yaml`, `og_ext_zlib`, `og_ext_libzip`, `og_ext_physfs`

**Aggregate target:**
`og_game` — INTERFACE library linking all modules with `--start-group`/`--end-group` for cyclic resolution.

Include-boundary note: `configure_openglad_library()` currently exposes the shared public include root to module targets. Component include boundaries are enforced in CI by `scripts/check_vendor_leaks.sh` (component include dependency checks and vendor leak checks), rather than by separate per-target public include roots in CMake.

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

- **`openglad`** (SDL client) — Full graphical game with rendering, audio, and input via SDL2. Platform-specific code lives in `src/platform/sdl/`.
- **`openglad_text`** (headless client) — SDL-free text-mode client for simulation, testing, and scripting. Platform-specific code lives in `src/platform/text/`.

Both targets link the same core modules (`og_core`, `og_sim`, `og_data`, `og_entities`, `og_io`). The boundary is enforced via link-time dispatch: shared code depends on SDL-free bridge declarations (notably `PlatformBridge` and `LevelRender` interfaces), which are wired by platform-specific implementations in `sdl_context_services.cpp` (SDL) and `platform_headless.cpp` (headless).

**Key boundary files:**

| File | Purpose |
|------|---------|
| `src/platform/sdl/runtime/sdl_context_services.cpp` | SDL implementations: bridge wiring, audio callbacks, frame presentation |
| `src/platform/sdl/runtime/walker_render_bridge.cpp` | SDL `walker` member functions: render component lifecycle and frame wiring |
| `src/platform/text/platform_headless.cpp` | Headless stubs: filesystem init, bridge no-ops/warnings, `LevelRender` stubs |
| `src/platform/text/walker_headless.cpp` | Headless `walker` member functions: no render component, sim-only frame tracking |
| `include/openglad/interface/platform_bridge.h` | Shared SDL-free callback bridge used by both SDL and headless builds |

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
| `src/platform/sdl/glad.cpp` | **Entry point.** `main()`, Emscripten frame wrapper, game state machine |
| `src/interface/screen.cpp` | Game world: `act()` delegates to `GameWorld::tick()`, `redraw()` renders + dispatches events |
| `src/platform/sdl/runtime/game_loop.cpp` | Per-frame loop: `game_frame()` and `game_frame_with_result()` |
| `src/platform/sdl/runtime/game_session.cpp` | RAII root: creates screen, prefs, installs legacy globals |
| `src/runtime/game_context.cpp` | `GameContext` and `ctx()` global accessor |
| `src/gameplay/walker.cpp` | Base entity class — all game objects inherit from this |
| `src/gameplay/living.cpp` | AI behavior for enemies and NPCs |
| `src/interface/ui/picker.cpp` | Team selection UI — main menu loop |
| `src/interface/ui/level_editor.cpp` | Scenario editor (openscen binary) |
| `src/platform/sdl/video.cpp` | SDL2 graphics layer — pixel buffer management |
| `src/interface/render/view.cpp` | Viewport/camera system, split-screen rendering |
| `src/resources/level_file_io.cpp` | Level file loading and saving |
| `src/resources/save_io.cpp` | Save game serialization |
| `src/resources/gloader.cpp` | Game content loading from packages |
| `src/interface/input/input.cpp` | Keyboard/controller event handling |
| `src/gameplay/game_world.cpp` | Live game simulation tick (extracted from `screen::act()`) |
| `src/gameplay/sim_event_log.cpp` | Event accumulator: decouples sim from rendering/audio |
| `src/interface/render/walker_draw.cpp` | Entity draw methods (extracted from `walker.cpp`) |
| `CMakeLists.txt` | Build system — module targets, test binaries, install rules |
| `CMakePresets.json` | Build presets for dev, CI, and web |
| `scripts/check_vendor_leaks.sh` | Enforced module include/vendor boundary rules |
| `tests/test_main.cpp` | Integration test runner entry point |
| `tests/unit/unit_main.cpp` | Unit test runner entry point |
