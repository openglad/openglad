# Component Architecture Migration Plan

**Branch:** `feat/desingletonize`
**Date:** 2026-02-24
**Builds on:** `docs/plans/desingletonize-globals-plan.md` (largely complete),
`docs/audits/remaining-singletons-audit.md`

---

## Goal

Restructure OpenGlad from 10 fine-grained modules into 4 top-level components
plus a core foundation layer, with strict dependency rules enforced at build
time.

The **gameplay** component is fully self-sufficient: no calls to rendering,
input, sound, file I/O, or UI. It is a pure simulation sandbox.

---

## Target Architecture

### Components

| Component | Purpose |
|-----------|---------|
| **core** | Foundation: logging, math, constants, type utilities |
| **gameplay** | Self-sufficient game simulation: entities, rules, world state |
| **resources** | File I/O, asset loading, serialization (levels, saves, config, sprites) |
| **interface** | Menus, sprites, logical rendering, input mapping, buttons |
| **platform** | SDL/text backends, audio playback, window management, orchestration |

### Dependency Rules

```
core      →  (nothing)
gameplay  →  core
resources →  core, gameplay (needs gameplay type definitions to serialize them)
interface →  core, gameplay, resources
platform  →  core, gameplay, interface, resources
```

**Gameplay calls nothing outside core.** No SDL, no file I/O, no UI prompts,
no sound. It produces events; the caller drains them.

### The Thread-Local

Gameplay has exactly one thread-local global: `current_game`
(`og::gameplay::GameplayContext*`). Platform-level code creates and installs it
before entering gameplay.

### Diagram

```
┌─────────────────────────────────────────────────┐
│                   platform                      │
│  GameSession, main(), SDL init, audio playback  │
│  sdl_client/, text_client/                      │
└──────┬──────────────┬──────────────┬────────────┘
       │              │              │
┌──────▼──────┐  ┌────▼────┐  ┌─────▼──────┐
│  interface  │  │resources│  │ (gameplay)  │
│  menus, UI  │  │ file IO │  │ (accessed   │
│  sprites    │  │ assets  │  │  directly)  │
│  rendering  │  │ config  │  │             │
└──────┬──────┘  └────┬────┘  └─────────────┘
       │              │
       ▼              ▼
┌──────┴──────────────┴──────┐
│          gameplay           │
│  GameWorld, entities, sim   │
│  thread_local current_game  │
└──────────────┬──────────────┘
               │
         ┌─────▼─────┐
         │   core    │
         │ log, math │
         └───────────┘
```

---

## Current Module → Component Mapping

| Current Module | Target Component | Notes |
|---|---|---|
| `og_core` (math, logging, util) | **core** | Unchanged |
| `og_sim` (SimWorld, events, IRandom) | **gameplay** | Already headless |
| `og_entities` (walker hierarchy, families) | **gameplay** | Remove render/save/UI coupling |
| `og_data` — in-memory types: LevelData, guy, statistics | **gameplay** | Stripped of file I/O methods |
| `og_data` — file I/O: gloader, gparser, pixie_data, level/save serialization | **resources** | |
| `og_io` (PhysFS, zip, yaml) | **resources** | As-is |
| `og_render` (video, pixie, view, text, walker_draw, pal32, radar) | **interface** | Logical rendering |
| `og_input` (input mapping, buttons) | **interface** | User interaction |
| `og_ui` (picker, editor, intro, help, results) | **interface** | Menu system |
| `og_runtime` — game state: screen::act(), passability, entity finders | **gameplay** (as GameWorld) | Extracted from screen |
| `og_runtime` — display: screen::redraw(), viewscreens, score_panel | **interface** | |
| `og_runtime` — orchestration: GameSession, game loop driver | **platform** | |
| `og_platform` — sound loading | **resources** | |
| `og_platform` — sound playback | **platform** | |
| `sdl_client/` | **platform/sdl** | SDL backend |
| `text_client/` | **platform/text** | Headless backend |

---

## Key Type Changes

### GameWorld (new — gameplay component)

Extracted from the current `screen` class. Owns the pure game state:

```cpp
// include/openglad/gameplay/game_world.h
namespace og::gameplay {

class GameWorld {
public:
    // Entity storage (currently in LevelData)
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> dead_list;

    // Spatial data
    std::unique_ptr<obmap> myobmap;
    PixieData grid;                     // tile type grid (gameplay uses for passability)
    std::int32_t pixmaxx, pixmaxy;

    // Simulation engine
    SimWorld sim_world;

    // Game state flags
    short level_done = 0;
    std::int32_t enemy_freeze = 0;
    signed char timer_wait = 6;
    char end = 0;
    bool retry = false;
    float control_hp = 0;

    // Per-team score accumulator (read back by outer layer after level ends)
    std::uint32_t m_score[4] = {};

    // Gameplay-relevant settings (populated by outer layer from SaveData before level)
    short my_team = 0;
    unsigned char allied_mode = 0;
    short current_scenario = 0;
    std::set<int> completed_levels;

    // Gameplay-relevant config (populated from cfg_store at setup time)
    GameplayConfig config;

    // Entity name tables
    std::string special_name[NUM_FAMILIES][NUM_SPECIALS];
    std::string alternate_name[NUM_FAMILIES][NUM_SPECIALS];

    // Queries
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    // ... other finders ...

    // Tick
    void act();  // delegates to sim_world.tick()
};

} // namespace og::gameplay
```

**What stays in `screen` (interface/platform layer):**
- `video` base class (pixel buffer, SDL surface, draw primitives)
- `viewscreen[4]` (cameras into the game world)
- `redraw()` (rendering pipeline)
- `soundp` (sound playback)
- `newpalette`, `palmode` (rendering state)
- `numviews`, `framecount`, `timerstart` (display config)

`screen` holds a `GameWorld*` (non-owning — platform owns the world) and
delegates game logic to it.

### GameplayContext (new — gameplay component)

```cpp
// include/openglad/gameplay/gameplay_context.h
namespace og::gameplay {

struct GameplayContext {
    GameWorld*    world = nullptr;
    IRandom*      rng = nullptr;
    SimEventLog*  sim_events = nullptr;
    std::string   mounted_campaign;
};

extern thread_local GameplayContext* current_game;

} // namespace og::gameplay
```

Replaces the per-entity `sim_*` pointer injection. Entity code accesses
`current_game->world`, `current_game->rng`, etc. instead of per-walker injected
pointers.

### GameplayConfig (new — gameplay component)

Subset of `cfg_store` with just the gameplay-relevant values:

```cpp
struct GameplayConfig {
    bool attack_lunge = true;
    bool alive_check_strict = false;
    // ... other gameplay-affecting config flags
};
```

Populated by the outer layer from `cfg_store` at level start. Gameplay never
touches `cfg_store` directly.

### IRenderComponent (new — gameplay component)

```cpp
// include/openglad/gameplay/render_component_base.h
namespace og::gameplay {

class IRenderComponent {
public:
    virtual ~IRenderComponent() = default;
    // Gameplay never calls methods on this. It just owns the lifetime.
    // The interface layer downcasts to the concrete type when rendering.
};

} // namespace og::gameplay
```

`walker` holds `std::unique_ptr<IRenderComponent> render_`. The current
`WalkerRender` (wrapping `pixieN`) extends `IRenderComponent` and lives in the
interface layer.

---

## Phase Plan

### Phase 1: Extract GameWorld from screen

**Goal:** Create `GameWorld` as a member of `screen`. Move pure game state into
it. `screen` forwards game logic calls to `GameWorld`.

**Steps:**
1. Create `include/openglad/gameplay/game_world.h` with the GameWorld class
2. Move entity lists, spatial data, SimWorld, and game state flags from `screen`
   (and from `LevelData` where appropriate) into `GameWorld`
3. Add `GameWorld world_;` member to `screen`
4. `screen::act()` becomes `world_.act()` + event dispatch (stays in screen)
5. Passability queries and entity finders move to GameWorld; screen forwards
6. Update all code that accesses `myscreen->level_data.oblist` etc. to go
   through `myscreen->world()` — or initially provide forwarding accessors
   on screen to minimize churn

**Key decision:** LevelData currently holds both entity lists (gameplay) and
rendering data (`renderer_`, `pixdata[]`, `myloader`). For Phase 1, GameWorld
can own the gameplay-relevant parts while LevelData keeps the rendering parts.
The full split happens in Phase 5.

**Files changed (major):**
- New: `include/openglad/gameplay/game_world.h`, `src/gameplay/game_world.cpp`
- Modified: `include/openglad/runtime/screen.h`, `src/sdl_client/runtime/screen.cpp`
- Modified: `src/runtime/sim_world.cpp` (tick takes GameWorld& instead of separate args)
- Modified: Various files that access screen's game state members

**Risk:** Medium — screen is the central class but the forwarding approach is
incremental.

**Testing:** Full ctest after each sub-step.

---

### Phase 2: Decouple SaveData from Gameplay

**Goal:** Entity code stops referencing `SaveData`. Gameplay is save-agnostic.

**Current `sim_save` usages in entity code:**

| Usage | File | Replacement |
|---|---|---|
| `sim_save->m_score[team_num] += N` | `walker_combat.cpp:287,315` | `current_game->world->m_score[team_num] += N` |
| `sim_save->m_score[team_num] += N` | `treasure_family_valuables.cpp:33,45,57` | Same |
| `sim_save->allied_mode` | `walker.cpp:1531,1587` | `current_game->world->allied_mode` |
| `sim_save->is_level_completed(n)` | `treasure_family_navigation.cpp:76,77` | `current_game->world->completed_levels.count(n)` |
| `sim_save->scen_num` | `treasure_family_navigation.cpp:77,106` | `current_game->world->current_scenario` |
| `sim_save->load("save0")` | `treasure_family_navigation.cpp:103` | Emit `WithdrawToLevel` event |
| `sim_save->save("save0")` | `treasure_family_navigation.cpp:110` | Emit `WithdrawToLevel` event |

**Layering violations in `treasure_family_navigation.cpp` (also fix):**

| Violation | Replacement |
|---|---|
| `yes_or_no_prompt()` — UI call from entity code | Emit `RequestExitConfirmation` event; outer layer shows prompt, feeds result back on next tick |
| `clear_keyboard()` — input call from entity code | Remove (becomes unnecessary once prompt is event-driven) |

**The navigation treasure redesign** is the hardest part of this phase. The
current EXIT treasure behavior is a blocking interaction: entity code calls a UI
prompt and waits for the answer. Under the new model:

1. Entity detects it's on an exit tile
2. Entity emits `RequestExitConfirmation { level_id, prompt_text }` event
3. `SimWorld::tick()` returns with a "pending interaction" flag
4. The outer layer (interface/platform) shows the prompt
5. On the next tick, the answer is available in GameWorld (e.g., `pending_exit_response`)
6. Entity reads the response and either emits `EndGame` or continues

This is a meaningful behavior change in how the game loop works for exits.
Needs careful testing.

**Steps:**
1. Add score accumulators, `allied_mode`, `current_scenario`, `completed_levels`
   to GameWorld (may already be done in Phase 1)
2. Replace `sim_save->m_score` and `sim_save->allied_mode` references in entity
   code with GameWorld access (mechanical find-and-replace)
3. Redesign exit treasure as event-driven (the hard part)
4. Remove `sim_save` pointer from `SimEntity`
5. Update outer layer (screen::act / platform code) to populate GameWorld from
   SaveData before level start and read results back after level end

**Risk:** Medium-High — the exit treasure redesign requires careful thought.
Score/allied_mode migration is trivial.

**Testing:** Full ctest. Add specific tests for exit and withdrawal behavior.

---

### Phase 3: Introduce GameplayContext and current_game

**Goal:** Entity and sim code accesses shared state through a single
`thread_local GameplayContext* current_game` instead of per-entity injected
pointers.

**Steps:**
1. Create `include/openglad/gameplay/gameplay_context.h` with the struct and
   thread_local declaration
2. Define `current_game` in `src/gameplay/gameplay_context.cpp`
3. `SimWorld::tick()` sets `current_game` before calling entity `act()` methods
4. Migrate entity code from `sim_*` pointers to `current_game->` access:
   - `sim_level->query_passable(...)` → `current_game->world->query_passable(...)`
   - `sim_rng->next(n)` → `current_game->rng->next(n)`
   - `sim_events` → `current_game->sim_events`
   - `sim_config->setting` → `current_game->world->config.setting`
   - `sim_enemy_freeze` → `current_game->world->enemy_freeze` (direct field)
5. Remove `sim_level`, `sim_rng`, `sim_events`, `sim_config`,
   `sim_enemy_freeze` fields from `SimEntity`
6. Remove the per-entity wiring in `sdl_context_services.cpp` and
   `level_data.cpp` that sets these pointers
7. Platform code (GameSession) creates GameplayContext and sets `current_game`

**Subsumes desingletonize Phase 1:** The existing `ctx()` global accessor and
`set_global_context()` are retired. Code that used `ctx().rng` uses
`current_game->rng`. Code that used `ctx().sim_events` uses
`current_game->sim_events`.

**Risk:** High churn — touches most entity/sim files. But fully mechanical
(search-replace per pointer).

**Testing:** Full ctest. Headless unit tests verify thread_local works.

---

### Phase 4: Define IRenderComponent in Gameplay

**Goal:** `walker` holds a gameplay-defined interface base for its render
component. Concrete implementation stays in the interface layer.

**Steps:**
1. Create `include/openglad/gameplay/render_component_base.h` with
   `IRenderComponent` (virtual destructor only)
2. Change `walker::render_` from `std::unique_ptr<WalkerRender>` to
   `std::unique_ptr<og::gameplay::IRenderComponent>`
3. Update `WalkerRender` to extend `IRenderComponent` (in interface layer)
4. Interface rendering code downcasts:
   `static_cast<WalkerRender*>(w->render_component())`
5. Same pattern for `LevelRender` → `ILevelRender` base in gameplay
6. Remove `#include <openglad/entities/walker_render.h>` from walker.h
   (only forward-declare `IRenderComponent`)

**Risk:** Low — small, mechanical change. The hard part is making sure all
downcast sites are updated.

---

### Phase 5: Split the Data Layer

**Goal:** Separate gameplay data structures from file I/O serialization.

**Gameplay keeps:**
- `LevelData` as the in-memory container (entity lists, grid, obmap) — stripped
  of `load()`/`save()` methods
- `guy` struct (character stat block)
- `statistics` struct
- Family descriptor types and registries

**Resources gets:**
- Level file I/O: `load_level(path) → LevelData`, `save_level(LevelData, path)`
- Save file I/O: `SaveData` struct + `load_save()`/`save_save()`
- `gloader` (sprite/animation loading + entity factory)
- `gparser` / `cfg_store` (config parsing)
- `pixie_data` loading from .pix files
- All PhysFS/zip/yaml code (already in `io`)

**The gloader challenge:** `gloader` is a factory that touches all layers:
- Reads pixel data from disk → **resources**
- Creates walker objects → **gameplay** types
- Attaches render components (pixieN) → **interface** types
- Populates stats from family descriptors → **gameplay**

**Solution:** gloader lives in resources and uses a factory/callback pattern:

```cpp
// resources layer
struct EntityFactory {
    // Provided by platform at setup time
    std::function<void(walker&, const PixieData&)> attach_render;
    // nullptr for headless — walkers get no render component
};

class loader {
public:
    loader(EntityFactory factory);
    std::unique_ptr<walker> create_walker(Order, int family);
    // ...
};
```

**Steps:**
1. Extract `LevelData::load()`/`save()` into free functions in resources
2. Move `SaveData` entirely to resources
3. Move `gloader`, `gparser`, `pixie_data` to resources
4. Define `EntityFactory` callback struct for render component attachment
5. Move level file format code (`load_scenario_data()` etc.) to resources
6. LevelData in gameplay becomes a pure in-memory container

**Risk:** Medium — gloader refactoring requires the callback pattern.

---

### Phase 6: Reorganize Directory Structure

**Goal:** Move source files into the four component directories.

**Target layout:**

```
include/openglad/
├── core/                    (unchanged)
├── gameplay/
│   ├── game_world.h
│   ├── gameplay_context.h
│   ├── gameplay_config.h
│   ├── render_component_base.h
│   ├── walker.h, living.h, weap.h, treasure.h, effect.h
│   ├── guy.h, statistics.h, obmap.h
│   ├── sim_entity.h, sim_world.h, sim_event_log.h, event.h
│   ├── sim_emit.h, irandom.h
│   ├── level_data.h         (in-memory representation only)
│   └── families/             (descriptors, registries)
├── resources/
│   ├── io.h                  (filesystem abstraction API)
│   ├── gloader.h
│   ├── gparser.h, cfg_store.h
│   ├── save_data.h
│   ├── level_io.h
│   └── pixie_data.h
├── interface/
│   ├── render/               (video, view, pixie, pixien, text, radar, pal32)
│   ├── walker_draw.h, walker_render.h
│   ├── input.h, button.h, input_state.h
│   ├── screen.h              (display shell — holds GameWorld ref + viewscreens)
│   └── ui/                   (picker, editor, intro, help, results)
└── platform/
    ├── game_session.h
    └── sound.h

src/
├── core/                    (unchanged)
├── gameplay/
│   ├── game_world.cpp
│   ├── gameplay_context.cpp
│   ├── walker*.cpp, living.cpp, weap.cpp, treasure.cpp, effect.cpp
│   ├── guy.cpp, obmap.cpp, combat_math.cpp, stats.cpp
│   ├── sim_world.cpp, sim_event_log.cpp
│   └── families/
├── resources/
│   ├── io/                   (physfs_api, zip_api, yaml_stream, og_file)
│   ├── gloader.cpp, gparser.cpp
│   ├── level_io.cpp, save_io.cpp, pixie_data.cpp
│   └── platform_io.cpp
├── interface/
│   ├── render/               (video, view, pixie, pixien, text, radar, etc.)
│   ├── input/                (input.cpp, button.cpp)
│   └── ui/                   (picker, editor, intro, help, results)
└── platform/
    ├── sdl/                  (glad.cpp, game_session.cpp, sound.cpp, ...)
    └── text/                 (main.cpp, platform_headless.cpp, ...)
```

**Approach:** Move files in batches, updating CMakeLists.txt and `#include`
paths after each batch. Order of moves:

1. **gameplay/** — entities, sim, game_world, core game types
2. **resources/** — io, data serialization, gloader
3. **interface/** — render, input, ui
4. **platform/** — sdl_client, text_client

Use `git mv` to preserve history. Update includes with a script or
find-and-replace.

**Risk:** High churn on include paths and CMake. No logic changes — purely
mechanical. Run full ctest after each batch.

---

### Phase 7: Define Inter-Component Interfaces

**Goal:** Formalize the boundaries between components with explicit interfaces
so that dependencies are through narrow, well-defined APIs.

**Gameplay → outside (events — already exists, extend):**

Current `EventKind` enum gains new types:

```cpp
ScoreChange,              // a=team, b=points
RequestExitConfirmation,  // a=dest_level; text=prompt
WithdrawToLevel,          // a=dest_level
```

The caller (interface/platform) drains `SimEventLog` after each tick and
handles these.

**Interface → platform (new callback bridge):**

```cpp
// include/openglad/interface/platform_bridge.h
struct PlatformBridge {
    // Rendering
    std::function<void()> present_frame;

    // Audio
    std::function<void(int sound_id)> play_sound;
    std::function<void(const char* music_file)> play_music;
    std::function<void()> stop_music;

    // Render surface management
    std::function<SDL_Surface*(int w, int h)> create_surface;
};
```

Platform registers concrete SDL (or headless no-op) implementations. Interface
calls these instead of SDL functions directly. This replaces the current
`LevelDataHooks` pattern with a more general mechanism.

**Resources API (narrow filesystem interface):**

```cpp
// include/openglad/resources/filesystem.h
namespace og::resources {
    bool mount(const char* archive, const char* mountpoint);
    std::vector<uint8_t> read_file(const char* path);
    bool write_file(const char* path, const void* data, size_t len);
    bool exists(const char* path);
    // ... etc
}
```

Implementation in resources/ wraps PhysFS. Other components use this API instead
of PhysFS directly.

**Risk:** Low — this is API design. Can be iterated as components are moved.

---

### Phase 8: Enforce Dependencies and Clean Up

**Goal:** Build-time enforcement of component dependency rules.

**Steps:**
1. Update CMakeLists.txt: each component becomes a CMake target with restricted
   `target_include_directories()`
   - `og_gameplay` can only include `core/` and `gameplay/` headers
   - `og_resources` can include `core/`, `gameplay/`, `resources/`
   - `og_interface` can include `core/`, `gameplay/`, `resources/`, `interface/`
   - `og_platform_sdl` can include everything
2. Add CI check script (extending `check_vendor_leaks.sh`) that verifies no
   component includes headers from a component it doesn't depend on
3. Remove legacy shims: `myscreen` macro, `theprefs` macro, dead extern
   declarations
4. Remove `set_global_context()` and the `ctx()` fallback path
5. Update `docs/ARCHITECTURE.md` with the new component model
6. Final audit: re-run the global state audit to verify `current_game` is the
   only thread-local in gameplay

**Risk:** Low — enforcement catches violations at compile time.

---

## Phase Dependencies

```
Phase 1 (GameWorld extraction)
    │
    ├──→ Phase 2 (SaveData decoupling)
    │        │
    │        ▼
    │    Phase 3 (current_game thread-local)
    │
    ├──→ Phase 4 (IRenderComponent) ← can run in parallel with 2-3
    │
    └──→ Phase 5 (data layer split) ← benefits from 2-3 being done
              │
              ▼
         Phase 6 (directory reorganization) ← needs 1-5 done
              │
              ▼
         Phase 7 (inter-component interfaces)
              │
              ▼
         Phase 8 (enforcement + cleanup)
```

- **Phase 1** is the prerequisite for everything.
- **Phases 2, 3, 4** can proceed roughly in parallel after Phase 1.
  Phase 3 benefits from Phase 2 being done (no `sim_save` to deal with) but
  doesn't strictly require it.
- **Phase 5** benefits from 2+3 being done but can start independently for the
  I/O extraction parts.
- **Phase 6** is the big mechanical move — much easier after the logical splits
  (1-5) are in place.
- **Phases 7-8** are cleanup/enforcement after reorganization.

---

## Relationship to Existing Desingletonize Work

This plan builds on the desingletonize work (largely complete). Some remaining
desingletonize phases are subsumed:

| Desingletonize Phase | Status | Disposition |
|---|---|---|
| Phase 0 (const sweep) | Done | N/A |
| Phase 1 (eliminate ctx globals) | Partial | Subsumed by Phase 3 here (`current_game` replaces `ctx()`) |
| Phases 2-9 | Done | N/A |
| Phase 10 (final verification) | Not started | Subsumed by Phase 8 here |

The key change: instead of `ctx()` → `current_session->ctx_`, we go to
`current_game->` in gameplay code. `current_session` remains at the platform
level for interface/UI code that needs session state.

---

## Hard Parts and Mitigations

### 1. Splitting screen (Phase 1)

`screen` is THE central class — ~1300 lines, extends `video`, owns both game
state and display state, referenced from everywhere.

**Mitigation:** Keep `screen` as a forwarding shell initially. Add `GameWorld`
as a member. Provide forwarding accessors on `screen` so existing code doesn't
break. Gradually migrate callers to access GameWorld directly.

### 2. Navigation treasure redesign (Phase 2)

`treasure_family_navigation.cpp` currently makes blocking UI calls
(`yes_or_no_prompt()`) and does save I/O (`sim_save->load/save`) from inside
entity behavior code. This is the worst layering violation.

**Mitigation:** Convert to an event-driven interaction:
1. Entity detects exit tile → emits `RequestExitConfirmation` event
2. SimWorld returns "pending interaction" status
3. Outer layer shows prompt, gets answer
4. Answer fed back into GameWorld for next tick
5. Entity reads answer and emits `EndGame` or continues

This requires a "pending interaction" protocol in the game loop. It's a real
behavior change, but it's the *correct* architecture.

### 3. gloader factory pattern (Phase 5)

`gloader` creates entities with graphics — it touches all layers.

**Mitigation:** Use callback injection. Resources provides the loader, interface
provides a `attach_render(walker&, PixieData&)` callback, platform wires them
together. Headless mode passes a no-op callback.

### 4. Include path churn (Phase 6)

Hundreds of `#include` changes when files move to new directories.

**Mitigation:** Write a migration script. Move one component at a time. Full
ctest after each batch. Use `git mv` to preserve history.

---

## Summary: Before vs After

### Before (current)

```
10 modules (core, sim, data, entities, io, runtime, render, input, ui, platform)
+ sdl_client/ and text_client/ overlays
screen class: game state + rendering + sound in one object
Entity code: holds 6 sim_* pointers, references SaveData, calls UI prompts
Thread-local: current_session (GameSession*) at platform level
Global accessor: ctx() with fallback/override machinery
```

### After (target)

```
4 components + core (gameplay, resources, interface, platform)
GameWorld class: pure game state, no rendering, no I/O
Entity code: accesses current_game-> for everything, no SaveData, no UI calls
Thread-local in gameplay: current_game (GameplayContext*)
Thread-local in platform: current_session (GameSession*) — for UI/display code
No ctx() — retired
```

### New Ownership Model

```
Platform (GameSession)
├── Creates and owns GameplayContext → sets current_game
├── Creates and owns GameWorld → populates from save/level files via resources
├── Creates and owns screen (display shell) → references GameWorld
├── Orchestrates game loop:
│   1. Poll input (platform)
│   2. Translate to game commands (interface)
│   3. Set current_game, call GameWorld::act() (gameplay)
│   4. Drain sim events (platform dispatches sound, interface dispatches visuals)
│   5. Call screen::redraw() (interface)
│   6. Present frame (platform)
└── After level: read scores/stats from GameWorld, update SaveData, save via resources
```
