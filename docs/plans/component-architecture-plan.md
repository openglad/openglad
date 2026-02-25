# Component Architecture Migration Plan

**Branch:** `feat/desingletonize`
**Date:** 2026-02-24
**Builds on:** `docs/plans/desingletonize-globals-plan.md` (largely complete),
`docs/audits/remaining-singletons-audit.md` (deleted — relevant content inlined
into Phase 12 below)

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
| `og_sim` (SimWorld, events, IRandom) | **gameplay** | SimWorld absorbed into GameWorld |
| `og_entities` (walker hierarchy, families) | **gameplay** | Remove render/save/UI coupling |
| `og_data` — in-memory types: guy, statistics | **gameplay** | LevelData eliminated — replaced by GameWorld |
| `og_data` — file I/O: gloader, gparser, pixie_data, level/save serialization | **resources** | gloader source is in `og_runtime` (`src/runtime/gloader.cpp`), header in `og_data` (`include/openglad/data/gloader.h`) |
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

**Replaces `LevelData` entirely.** GameWorld absorbs all gameplay-relevant fields
from both `screen` and `LevelData`, plus the tick logic from `SimWorld`.
`LevelData` and `SimWorld` both cease to exist as classes. Rendering data
(`renderer_`, `pixdata[]`, draw offsets) moves to a new `LevelVisuals` type
owned by `screen` in the interface layer.

```cpp
// include/openglad/gameplay/game_world.h
namespace og::gameplay {

class GameWorld {
public:
    // Level metadata
    int id;
    std::string title;
    short par_value;
    short time_bonus_limit;
    char type;                            // level flags (TYPE_CAN_EXIT_WHENEVER, etc.)

    // Entity storage (currently in LevelData)
    std::list<std::unique_ptr<walker>> oblist;
    std::list<std::unique_ptr<walker>> weaplist;
    std::list<std::unique_ptr<walker>> fxlist;
    std::list<std::unique_ptr<walker>> dead_list;  // dead entities kept for pointer validity
    int living_count;                     // count of Order::Living entities only (was numobs)

    // Spatial data
    std::unique_ptr<obmap> myobmap;
    PixieData grid;                       // tile type grid (gameplay uses for passability)
    smoother mysmoother;                  // terrain type grid + genre queries
    std::int32_t pixmaxx, pixmaxy;

    // Simulation state (absorbed from SimWorld)
    SimRandom rng_;                       // deterministic LCG
    std::uint32_t tick_count_ = 0;        // total ticks across all levels
    std::uint32_t level_tick_count_ = 0;  // ticks in current level

    // Game state flags (currently on screen / TickResult)
    short level_done = 0;                 // 0=foes remain, 1=no foes+exits, 2=auto-advance
    bool game_ended = false;              // true when game over condition reached
    short next_level = -1;               // level to transition to (-1=none)
    short ending = 0;                     // 0=win, 1=loss, SCEN_TYPE_SAVE_ALL=failure
    std::int32_t enemy_freeze = 0;
    signed char timer_wait = 6;
    char end = 0;
    bool retry = false;
    float control_hp = 0;
    bool withdraw_requested = false;    // set by WithdrawToLevel event; tick() early-outs
    bool create_hit_effects = true;    // gates FX entity creation in combat (from cfg hit_anim)

    // Per-team score accumulator (read back by outer layer after level ends)
    std::uint32_t m_score[4] = {};

    // Gameplay-relevant settings (populated by outer layer before level)
    short my_team = 0;
    unsigned char allied_mode = 0;
    short current_scenario = 0;
    std::set<int> completed_levels;
    short difficulty = 100;               // enemy stat multiplier — resolved from difficulty_level[current_difficulty_] (values: 50, 100, 200)

    // Entity factory (wired by platform at setup time, backed by gloader in resources)
    std::function<std::unique_ptr<walker>(Order, int family)> entity_factory;

    // Queries
    bool query_passable(float x, float y, walker* ob);
    bool query_object_passable(float x, float y, walker* ob);
    bool query_grid_passable(float x, float y, walker* ob);
    walker* find_near_foe(walker* ob);
    walker* find_far_foe(walker* ob);
    // ... other finders ...

    // Entity creation (uses entity_factory callback)
    walker* add_ob(Order order, int family);
    walker* add_fx_ob(Order order, int family);
    walker* add_weap_ob(Order order, int family);

    // Tick (absorbed from SimWorld — updates level_done, entity lists, etc.)
    void tick(SimEventLog& events);
};

} // namespace og::gameplay
```

**Key design decisions:**

- **`SimWorld` is absorbed.** The tick logic, tick counters, and RNG all live
  directly on GameWorld. `SimWorld` as a separate class is deleted.
- **`smoother` stays whole in GameWorld.** Entity code needs
  `query_genre_x_y()` for gameplay decisions (forest walk, weapon visibility,
  door orientation). The `smooth()` method is editor-only but keeping it here
  avoids splitting a small class. The smoother is a terrain type grid, not a
  rendering interpolator.
- **`living_count` replaces `numobs`.** Tracks only `Order::Living` entities,
  not all objects in `oblist`. Used by generators and slimes for spawn caps.
  Renamed for clarity.
- **`type` field** holds level flags (`TYPE_CAN_EXIT_WHENEVER`,
  `TYPE_MUST_DESTROY_GENERATORS`, etc.) used by tick logic and exit treasure.
- **`difficulty`** is the resolved enemy stat multiplier (50, 100, or 200),
  looked up from `difficulty_level[current_difficulty_]` (defined in
  `src/ui/picker_common.cpp`) where `current_difficulty_` is an index (0, 1, 2)
  on `GameSession`. The outer layer resolves this index to the multiplier value
  and populates `GameWorld::difficulty` before level start. Entity code in
  `living.cpp:526-537` currently does this lookup inline — after migration it
  reads the pre-resolved value from `current_game->world->difficulty`.
- **No `GameplayConfig`.** Most `cfg_store` effect settings (`attack_lunge`,
  `hit_recoil`, `damage_numbers`, `hit_flash`, `heal_numbers`) do not affect
  `worldx_`/`worldy_` or damage calculations. Currently the config gates whether
  sim code sets these fields on walker at all (e.g.
  `if (sim_config->is_on("effects", "attack_lunge")) attack_lunge = ...`).
  **Migration approach:** sim code sets these fields unconditionally (remove
  the config gates), and the interface layer checks cfg before reading them
  for display. The fields accumulate even when visually disabled — this is
  harmless since they're only consumed by render code. The `sim_config`
  pointer is removed from `SimEntity`.
  **Exception — `hit_anim`:** At `walker_combat.cpp:90-96`, the `hit_anim`
  config gate controls creation of FX entities (`add_ob(Order::FX, FAMILY_HIT)`).
  This is NOT rendering-only — it creates entities that appear in entity lists,
  affecting gameplay (entity counts, iteration, potential performance). Removing
  this gate unconditionally would change behavior. Instead, a
  `bool create_hit_effects` flag on GameWorld (see field list above) is populated
  from the config by the outer layer. Sim code checks
  `current_game->world->create_hit_effects` instead of `sim_config->is_on()`.
- **No `special_name`/`alternate_name` tables.** These were redundant copies of
  data already in the family descriptor registry. Entity code fetches names
  directly from `get_family_descriptor(family)->special_names[index]`.
  Note: `special_name[NUM_FAMILIES][NUM_SPECIALS]` on `screen` and
  `special_names[FD_NUM_SPECIALS]` on `FamilyDescriptor` are separate arrays.
  Entity code (27 family files in `src/entities/families/`) already uses
  `get_family_descriptor()`. The `screen` arrays are consumed by UI code
  (`view.cpp`, `score_panel.cpp`, `input_event_bridge.cpp`, etc.). Consumer
  migration needs to route UI code to `get_family_descriptor()` directly.
- **No `description` field.** Level description text is UI-only — loaded from
  the level file by resources and handed to the interface layer for display.
  Note: `LevelData::description` (`std::list<std::string>`) is currently loaded
  by `load()` and saved by `save()`. The resources layer's `load_level()`
  function must return/populate the description separately from GameWorld,
  passing it to the interface layer for display.
- **`dead_list`** grows throughout a level and is only cleared when the outer
  layer calls a cleanup method at level end (`delete_objects()`). Stale pointer
  cleanup (nullifying `foe_`, `owner_`, etc.) happens during the tick before
  entities move to `dead_list`.
- **`level_done` and other tick results** are stored on GameWorld and written
  directly by `tick()`. The previous `TickResult` return pattern is replaced —
  tick updates `level_done`, `game_ended`, `next_level`, and `ending` in place.
  The outer layer reads these fields after each tick to drive level transitions.
- **Entity factory callback** (`std::function`) is wired by platform at setup
  time, pointing to `gloader` in the resources layer. `GameWorld::add_ob()`
  calls it to create entities mid-tick (generators spawning enemies, slimes
  duplicating, etc.). Headless mode provides a minimal factory.
- **Pathfinding state** (`path_walker`, `path_map`, `pather` thread-locals in
  `walker_pathing.cpp`) moves onto **GameplayContext**, not GameWorld. Reasons:
  `MicroPather` has internal pointers and is not trivially moveable — putting it
  on GameWorld would make GameWorld non-moveable. These are scratch computation
  state, not game state (no serialization needed). Moving to GameplayContext
  eliminates the `path_walker` thread-local hack — `Map::AdjacentCost()` can
  access `current_game->world->query_grid_passable()` and
  `current_game->world->myobmap->obmap_get_list()` directly instead of going
  through a thread-local walker pointer. `pather.Reset()` is still needed per
  call (graph validity, not storage location). See GameplayContext struct below.
- **Sound is already event-driven.** Entity code uses `emit_sound(sim_events,
  SOUND_ID)` to push `PlaySound` events into `SimEventLog`. The outer layer
  drains these after each tick. No direct audio calls exist in entity code —
  no migration needed.

### LevelVisuals (new — interface component)

Holds the rendering data that was previously on `LevelData`. Named
`LevelVisuals` (not `LevelRender`) to avoid confusion with the existing
`LevelRender` class that it wraps:

```cpp
// include/openglad/interface/level_visuals.h
struct LevelVisuals {
    PixieData pixdata[PIX_MAX];          // tile sprite graphics
    std::unique_ptr<LevelRender> renderer_;
    std::int32_t topx, topy;             // draw offset
};
```

### screen (refactored — interface/platform layer)

**What stays in `screen`:**
- `video` base class (pixel buffer, SDL surface, draw primitives)
- `viewob[5]` (`std::unique_ptr<viewscreen>` — camera viewports)
- `numviews` (current viewport count)
- `redraw()` and the rendering pipeline
- `soundp` (`std::unique_ptr<soundob>` — audio subsystem)
- `newpalette`, `palmode` (palette/rendering state)
- `framecount`, `timerstart` (frame timing)
- `redrawme` (redraw flag)
- `special_name[NUM_FAMILIES][NUM_SPECIALS]`, `alternate_name[NUM_FAMILIES][NUM_SPECIALS]` (UI display names — consumed by `view.cpp`, `score_panel.cpp`, `input_event_bridge.cpp`; eventually routed to `get_family_descriptor()` directly)
- `LevelVisuals level_visuals_` (rendering data extracted from old LevelData)

`screen` holds a `GameWorld*` (non-owning — platform owns the world) and
delegates game logic to it.

**Ownership transfer:** During Phases 1–3, `screen` temporarily owns GameWorld
as `GameWorld world_;` (a direct member). In Phase 10, when screen moves to the
interface layer, GameWorld ownership transfers to platform (`GameSession`).
`screen` is refactored to hold a non-owning `GameWorld*` pointer instead.

### GameplayContext (new — gameplay component)

```cpp
// include/openglad/gameplay/gameplay_context.h
namespace og::gameplay {

struct GameplayContext {
    GameWorld*    world = nullptr;
    SimEventLog*  sim_events = nullptr;

    // Pathfinding scratch state (eliminates thread-local hack in walker_pathing.cpp)
    std::unique_ptr<PathfindingState> pathfinding;  // owns Map + MicroPather
};

extern thread_local GameplayContext* current_game;

} // namespace og::gameplay
```

`mounted_campaign` is NOT part of GameplayContext — gameplay has no need for the
campaign identifier. It stays at the platform layer (on `GameSession` / the
existing `GameContext`).

Replaces the per-entity `sim_*` pointer injection. Entity code accesses
`current_game->world`, `current_game->world->rng_`, etc. instead of per-walker
injected pointers. The RNG is accessed through `world->rng_` rather than a
separate pointer — having a redundant `rng` alias on the context would be a
stale-pointer footgun if the world is ever swapped. For tests that need a mock
RNG, `GameWorld::rng_` is an `IRandom` interface and can be swapped directly.

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

### Phases 1a–3: Create GameWorld (keeping LevelData alive)

**Goal:** Create `GameWorld` with entity lists, spatial data, tick logic (from
`SimWorld`), RNG, and game state flags. `screen` holds both `GameWorld` and
`LevelData` temporarily during the transition.

These four phases (1a, 1b, 2, 3) keep each change independently testable and
bisectable.

**What moves into GameWorld (across Phases 1a–3):**
- Entity lists (`oblist`, `weaplist`, `fxlist`, `dead_list`) from `LevelData`
- `living_count` (renamed from `numobs`) from `LevelData`
- Spatial data (`myobmap`, `grid`, `mysmoother`, `pixmaxx/pixmaxy`) from `LevelData`
- Level metadata (`id`, `title`, `par_value`, `time_bonus_limit`, `type`) from `LevelData`
- Tick logic, tick counters, and RNG from `SimWorld` (absorbing `SimWorld` entirely)
- Game state flags (`level_done`, `enemy_freeze`, `end`, etc.) from `screen`
- `tick()` writes `level_done`, `game_ended`, `next_level`, and `ending` directly on GameWorld instead of returning `TickResult`

**What stays on `LevelData` temporarily:**
- `myloader` (entity factory) — moves to resources in Phase 6
- `renderer_`, `pixdata[]`, `topx/topy` — moves to `LevelVisuals` in Phase 7
- Sim context wiring fields (`sim_ctx_*`) — transitional (eliminated by Phase 4)

### Phase 1a: Move entity lists to GameWorld

**Goal:** Create the GameWorld class. Move entity lists and `living_count` from
`LevelData` into `GameWorld`. Also move `delete_objects()` (list clearing
portion) and `remaining_foes()` (operates on oblist). `screen` gets a
`GameWorld world_;` member. `LevelData` keeps forwarding accessors temporarily.

**Steps:**
1. Create `include/openglad/gameplay/game_world.h` with GameWorld class (entity
   lists, `living_count`)
2. Create `src/gameplay/game_world.cpp`
3. Add `GameWorld world_;` member to `screen`
4. Move entity lists (`oblist`, `weaplist`, `fxlist`, `dead_list`) and
   `living_count` (renamed from `numobs`) from `LevelData` into `GameWorld`
5. Move `delete_objects()` (list clearing portion) and `remaining_foes()` free
   function to GameWorld
6. Provide forwarding accessors on both `screen` and `LevelData` to minimize
   churn (callers migrate incrementally)
7. `GameWorld::add_ob()` delegates to `LevelData::myloader` temporarily

**`add_ob()` callsite scope:** `add_ob()` is called from ~28 locations across
entity code, loader code, and level editor code. Entity code goes through
`sim_level->add_ob()` which will forward to `world_->add_ob()` via LevelData
forwarding during the transition. This is a high-touch migration point.

**Entity wiring during transition:** Entity creation flows
`GameWorld::add_ob()` → `LevelData::myloader->create_walker_owned()` →
`LevelData::wire_entity()` (sets `sim_*` pointers via `LevelDataHooks`).
This circular delegation (`GameWorld` → `LevelData` → hooks → back to screen
state) is ugly but temporary — Phase 4 eliminates `sim_*` pointer wiring,
and Phase 6 replaces the loader path entirely.

**Files changed (major):**
- New: `include/openglad/gameplay/game_world.h`, `src/gameplay/game_world.cpp`
- Modified: `include/openglad/data/level_data.h` (fields removed, forwarding added)
- Modified: `include/openglad/runtime/screen.h` (new `world_` member)
- Modified: All files that reference `level_data.oblist` etc. (high churn but mechanical)

**Screen lifecycle methods:**
- `screen::ready_for_battle()` — display-only (views, UI flags, timer). Not
  affected by GameWorld extraction.
- `screen::reset()` — calls `save_data.reset()` and `level_data.clear()`.
  After Phase 1a, the `level_data.clear()` call must also clear `world_`
  state (or be replaced by a `world_.clear()` + `level_data.clear()` pair).
- `screen::endgame()` — mixed concerns: shows results_screen (display),
  updates save_data scores (persistence), calls save_data.save() (I/O).
  Stays on screen during Phases 1-3 but needs splitting in Phase 10 when
  screen moves to interface.

**Risk:** High — `LevelData` is referenced widely. But keeping `LevelData`
alive as a shell with forwarding accessors makes this incremental.

**Testing:** Full ctest. Create `TestGameplayContext` RAII helper early in this
phase for use in all subsequent test updates.

---

### Phase 1b: Move spatial data, queries, finders, and metadata to GameWorld

**Goal:** Complete the GameWorld shell by moving spatial data, passability
queries, entity finders, and level metadata from `LevelData` into `GameWorld`.
The end result is identical to the original Phase 1 — the split just makes
each commit smaller and independently testable.

**Steps:**
1. Move spatial data (`myobmap`, `grid`, `mysmoother`, `pixmaxx`/`pixmaxy`)
   from `LevelData` into `GameWorld`
2. Move level metadata (`id`, `type`, `title`, `par_value`, `time_bonus_limit`,
   `difficulty`) into `GameWorld`
3. Move passability queries: `query_passable()`, `query_object_passable()`,
   `query_grid_passable()` to GameWorld
4. Move entity finders: `find_near_foe()`, `find_far_foe()`,
   `find_nearest_blood()`, `find_nearest_player()`, `find_in_range()`,
   `find_foes_in_range()`, `find_foe_weapons_in_range()`,
   `find_friends_in_range()` to GameWorld
5. Update forwarding accessors on `LevelData` for new fields
6. Move grid management methods to GameWorld: `create_new_grid()`,
   `resize_grid()`, `delete_grid()`, `clear()`. Note: these currently touch
   `mysmoother` (rendering state) — the `mysmoother.set_target(grid)` calls
   stay since `mysmoother` moves to GameWorld (plan already specifies this).
   `resize_grid()` also removes entities that fall off the map — pure
   gameplay concern.

**Files changed (major):**
- Modified: `include/openglad/gameplay/game_world.h` (spatial + metadata fields added)
- Modified: `include/openglad/data/level_data.h` (more fields removed, forwarding added)
- Modified: All files that reference passability queries or entity finders

**Risk:** Medium — lower than Phase 1a since the GameWorld shell already exists.

**Testing:** Full ctest.

---

### Phase 2: Absorb SimWorld into GameWorld

**Goal:** Move tick logic, tick counters, and RNG from `SimWorld` directly onto
`GameWorld`. Delete `SimWorld` as a class. `tick()` still returns `TickResult`
at this point — the signature change comes in Phase 3.

**Steps:**
1. Move `tick()`, `tick_count_`, `level_tick_count_`, `rng_` from `SimWorld`
   onto `GameWorld`
2. Move `TickResult` struct definition from `sim_world.h` to `game_world.h`
   (temporary home — Phase 3 eliminates it)
3. `GameWorld::tick()` takes the same parameters `SimWorld::tick()` currently
   takes (LevelData&, SaveData&, enemy_freeze&, end, SimEventLog&) — except
   LevelData references become self-references since entity lists are now local
4. `screen::act()` calls `world_.tick(...)` instead of `sim_world_.tick(...)`
5. Delete `SimWorld` class and `sim_world.h`

**Files changed (major):**
- Deleted: `include/openglad/sim/sim_world.h`, `src/runtime/sim_world.cpp`
- Modified: `include/openglad/gameplay/game_world.h` (tick logic + TickResult added)
- Modified: `include/openglad/runtime/screen.h` (remove `sim_world_` member)
- Modified: `src/sdl_client/runtime/screen.cpp` (act() delegation)

**Risk:** Medium — SimWorld is well-encapsulated. The merge is mechanical.

**Testing:** Full ctest.

---

### Phase 3: Consolidate game state flags, eliminate TickResult

**Goal:** Move game state flags (`level_done`, `enemy_freeze`, `end`, etc.)
from `screen` into `GameWorld`. `tick()` writes these directly instead of
returning `TickResult`.

**Note on `level_done` triple-storage:** Currently `level_done` exists on
`screen` (the authoritative copy), on `LevelData` (a copy set by `screen::act`),
and as a return value in `TickResult`. All three are set in `screen::act()`:
```cpp
level_done = result.level_done;
level_data.level_done = result.level_done;
```
This sub-phase consolidates to a single authoritative location on `GameWorld`.
The `LevelData` forwarding accessor (from Phase 1a/1b) points to `world_.level_done`.
The `screen`-level field is removed and replaced by `world_.level_done` access.

**Steps:**
1. Move `level_done`, `game_ended`, `next_level`, `ending`, `enemy_freeze`,
   `end`, `retry`, `control_hp`, `timer_wait` from `screen` into `GameWorld`
2. Add `m_score[4]`, `my_team`, `allied_mode`, `current_scenario`,
   `completed_levels`, `difficulty` fields to GameWorld
3. `tick()` drops `TickResult` return — writes state flags in place on self
4. `screen::act()` reads `world_.level_done` etc. after tick() returns
5. Remove the `LevelData.level_done` copy and the `screen.level_done` field —
   single source of truth on `GameWorld`

**Files changed (major):**
- Modified: `include/openglad/gameplay/game_world.h` (state flags added)
- Modified: `include/openglad/runtime/screen.h` (state fields removed)
- Modified: `src/sdl_client/runtime/screen.cpp` (act() reads from world_)
- Deleted: `TickResult` struct from `game_world.h` (moved there in Phase 2,
  now eliminated — tick() writes state flags in place)

**Risk:** Medium — touching screen's state fields affects many callers, but
forwarding accessors on screen ease the transition.

**Testing:** Full ctest.

**Ownership after Phases 1–3:**

```
screen
├── world_ (GameWorld) — entity lists, grid, smoother, obmap, tick logic, RNG, game state
├── level_data (LevelData) — temporarily holds myloader + rendering data
├── save_data (SaveData) — stays on screen for now (moves in Phase 5/9)
└── viewscreens, video base, sound, palette (display state — stays)
```

---

### Phase 4: Introduce GameplayContext and current_game

**Goal:** Entity and sim code accesses shared state through a single
`thread_local GameplayContext* current_game` instead of per-entity injected
pointers. This phase must run before Phase 5 (SaveData decoupling) because
Phase 5 needs `current_game->world->` to replace `sim_save->` references —
without `current_game`, entities have no path to reach GameWorld fields.

This phase only needs Phases 1–3 (GameWorld with entity lists, tick logic, and
game state flags). It does NOT need gloader moved or LevelData killed.

**`tick()` signature evolution:** Phase 2 absorbed `SimWorld` into `GameWorld`.
`GameWorld::tick()` currently takes `SimEventLog&` as an argument. This phase
completes the evolution: `tick()` takes no args and reads `sim_events` from
`current_game`.

**Steps:**
1. Create `include/openglad/gameplay/gameplay_context.h` with the struct and
   thread_local declaration
2. Define `current_game` in `src/gameplay/gameplay_context.cpp`
3. Platform code (GameSession) creates GameplayContext and sets `current_game`
4. `GameWorld::tick()` drops all args — reads from `current_game` instead
5. Migrate entity code from `sim_*` pointers to `current_game->` access:
   - `sim_level->query_passable(...)` → `current_game->world->query_passable(...)`
   - `sim_rng->next(n)` → `current_game->world->rng_.next(n)`
   - `sim_events` → `current_game->sim_events`
   - `sim_enemy_freeze` → `current_game->world->enemy_freeze` (direct field)
   - `stats.cpp:38` — `ctx().rng->next()` → `current_game->world->rng_.next()`
     (stat calculations during gameplay)
   - `smooth.cpp:25` — `ctx().rng->next()` → `current_game->world->rng_.next()`
     (terrain smoothing during gameplay)
6. Remove `sim_level`, `sim_rng`, `sim_events`,
   `sim_enemy_freeze` fields from `SimEntity`
7. Remove entity wiring from **both** sites that set `sim_*` pointers:
   - `LevelData::wire_entity()` (`level_data.cpp:517-526`) — sets `myobmap`,
     `sim_level`, and copies of `sim_ctx_*` stored on LevelData
   - `sdl_level_data_wire_entity_from_screen()` (`sdl_context_services.cpp:79-89`)
     — **overwrites** `sim_save`, `sim_enemy_freeze`, `sim_events`, `sim_rng`,
     `sim_config` with values from `current_session` and `ctx()`
   Both sites must be eliminated — removing only one leaves stale pointers
8. Migrate `walker::myobmap` (walker.h line 196) — a raw `obmap*` set by
   `wire_entity()` (`level_data.cpp:519`). Replace with
   `current_game->world->myobmap.get()` access and remove from walker

**CMake dependency shift:** Currently `og_entities` has
`target_include_directories(PRIVATE third_party/micropather)` for the
pathfinding scratch state (`walker_pathing.cpp`). When pathfinding state moves
to `GameplayContext` in this phase, the micropather include dependency shifts
from `og_entities` to the gameplay component target. Update `CMakeLists.txt`
accordingly.

**Note:** This phase migrates four of the six `sim_*` pointers. The remaining
two (`sim_save`, `sim_config`) are left on `SimEntity` until Phase 5, which
removes them as part of the SaveData decoupling.

**Threading note:** There is a `std::atomic<GameSession*> primary_session`
(`game_session.cpp:37`) with an `ensure_thread_session()` helper
(`game_session.h:167-171`). Child threads (test injectors, potentially demo
rendering) use this to inherit session context when their `thread_local
current_session` is null. If `current_game` becomes the gameplay entry point,
child threads that need gameplay access will need an analogous
`ensure_thread_game()` pattern, or the threading model needs to be documented as
single-threaded-only for gameplay.

**Subsumes desingletonize Phase 1:** The existing `ctx()` global accessor and
`set_global_context()` are retired. Code that used `ctx().rng` uses
`current_game->world->rng_`. Code that used `ctx().sim_events` uses
`current_game->sim_events`.

**RNG behavioral change:** Currently, `SimWorld` owns a deterministic
`SimRandom rng_` (LCG), but entity code uses `sim_rng` which is overwritten by
site 2 (`sdl_context_services.cpp:79-89`) to point at `ctx().rng` — a
`ProductionRandom` that wraps a non-deterministic global `random()` function.
After migration, entities use `current_game->world->rng_` (SimRandom). This
switches entity code from non-deterministic to deterministic RNG. This is
intentional (enables replay-safe gameplay) but is a behavioral change that
could affect game feel and should be regression-tested.

**`ctx().rng` call sites NOT migrated to `current_game`:** The following
render/platform `ctx().rng` usages use RNG for visual effects, not gameplay.
They are not migrated to `current_game->world->rng_`:
- `score_panel.cpp:31`, `view.cpp:410/509`, `video.cpp:36`, `radar.cpp:30`,
  `glad.cpp:64`
After `ctx()` retirement (Phase 12), these switch to
`current_session->ctx_.rng` or a dedicated render-layer RNG.

**Risk:** High churn — touches most entity/sim files. But fully mechanical
(search-replace per pointer).

**Testing:** Full ctest. Headless unit tests verify thread_local works.

---

### Phase 5: Decouple SaveData from Gameplay

**Goal:** Entity code stops referencing `SaveData`. Gameplay is save-agnostic.
`current_game` (introduced in Phase 4) provides the access path for
replacements.

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
| `yes_or_no_prompt()` — UI call from entity code | Emit `RequestExitConfirmation` event; outer layer shows prompt and handles transition |
| `clear_keyboard()` — input call from entity code | Remove (becomes unnecessary once prompt is event-driven) |

**Also in this phase:** Remove `sim_config` pointer from `SimEntity`. The
`cfg_store` effect settings (`attack_lunge`, `hit_recoil`, `damage_numbers`,
`hit_flash`, `hit_anim`, `heal_numbers`) don't affect `worldx_`/`worldy_` or
damage calculations. **Migration:** Remove the `sim_config->is_on()` gates
from entity code — sim code unconditionally sets `attack_lunge`, `hit_recoil`,
pushes `DamageNumber` entries, etc. The interface layer (`walker_draw.cpp`
etc.) checks `cfg.is_on()` before reading these fields for display. This
means the fields accumulate even when visually disabled, which is harmless.

**Additional `sim_config` call sites:**

| Usage | File | Migration |
|---|---|---|
| `sim_config->is_on("effects", "heal_numbers")` | `family_orc.cpp:94` | Remove gate — set heal number unconditionally |
| `sim_config->is_on("effects", "heal_numbers")` | `family_cleric.cpp:123` | Remove gate — set heal number unconditionally |

**The navigation treasure redesign** is the hardest part of this phase. The
current EXIT treasure behavior is a blocking interaction: entity code calls a UI
prompt and waits for the answer.

**New model — deferred action queue:** The treasure entity is a dumb sensor. It
detects "player stepped on exit" and emits an event. The outer layer owns the
entire exit orchestration flow. Entity code never re-enters for this decision.

1. Entity detects player on exit tile
2. Entity emits `RequestExitConfirmation { dest_level, prompt_text }` event
3. Tick finishes normally — treasure's job is done
4. Outer layer (platform) drains events, sees `RequestExitConfirmation`
5. Outer layer shows the prompt via interface layer
6. Player confirms → outer layer handles the level transition directly
   (it already owns the game loop, SaveData, level loading)
7. Player denies → outer layer does nothing, next tick proceeds normally

**Why this approach:** No state machine on the entity, no "pending response"
field on GameWorld, no multi-tick continuation protocol. The exit/withdrawal
logic (`sim_save->load/save`) was already an outer-layer concern shoved into
entity code — this moves it where it belongs.

**Behavior change note:** In the current code, the withdrawal path immediately
kills all living entities mid-tick. In the new model, the tick finishes normally
and the outer layer handles the transition afterward. This means other entities
get one extra tick of actions before the level is discarded.

**Withdrawal edge case:** During that extra tick, enemies can damage the player,
the player can die, and score can change — all in a tick whose results are about
to be discarded. The save happens in the outer layer AFTER the tick, so those
spurious changes would be persisted. **Mitigation:** When `tick()` sees a
`withdraw_requested` flag on GameWorld (set by the `WithdrawToLevel` event
emission), it skips entity updates for the remainder of the current tick. This
preserves the current behavior of "nothing happens after withdrawal" without
requiring mid-tick save I/O. The outer layer checks `withdraw_requested` after
tick, performs the save, and loads the target level.

**Steps:**
1. Add score accumulators, `allied_mode`, `current_scenario`, `completed_levels`
   to GameWorld (may already be done in Phase 3)
2. Replace `sim_save->m_score` and `sim_save->allied_mode` references in entity
   code with `current_game->world->` access (mechanical find-and-replace)
3. Remove `sim_config->is_on()` gates from entity code (sim sets fields
   unconditionally); add `cfg.is_on()` checks in interface layer before
   reading those fields for display (`walker_draw.cpp` etc.)
4. Remove `sim_config` pointer from `SimEntity`
5. Redesign exit treasure as event-driven (the hard part)
6. Remove `sim_save` pointer from `SimEntity`
7. Update outer layer (screen::act / platform code) to populate GameWorld from
   SaveData before level start and read results back after level end

**Risk:** Medium-High — the exit treasure redesign requires careful thought.
Score/allied_mode migration is trivial. `sim_config` removal is mechanical.

**Testing:** Full ctest. Add specific tests for exit and withdrawal behavior.

---

### Phase 6: Move gloader to resources

**Goal:** Move the entity factory (`gloader`) out of `LevelData` into the
resources layer. `GameWorld::add_ob()` uses a factory callback instead of
owning a loader directly. This establishes clean gameplay-layer dependencies
from the start.

Since Phase 4 already introduced `current_game` and eliminated `sim_*` pointer
wiring, the factory callback does NOT need transitional entity wiring logic.
Newly created entities use `current_game->` from the start.

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

**GameWorld side:**

```cpp
// Set by platform at setup time
std::function<std::unique_ptr<walker>(Order, int family)> entity_factory;

walker* GameWorld::add_ob(Order order, int family) {
    auto w = entity_factory(order, family);
    // insert into oblist, update living_count, etc.
}
```

**Animation tables:** The animation frame arrays (~23 animation tables + ~30 frame cycle arrays) in `gloader.cpp` are
authored in gloader but baked into entities at creation time via the `ani`
pointer. Entity code indexes `ani[curdir + ani_type * NUM_FACINGS][cycle]`
at runtime but never goes back to gloader. The tables stay with gloader
in resources — no cross-component dependency.

**`popup_dialog()` layering violation:** `gloader.cpp:553` calls
`popup_dialog()` (a UI function) when entity creation fails due to missing
graphics. This must be resolved when moving gloader to resources. Options:
- Return a null/error and let the caller handle the dialog
- Use an error callback wired by platform

**Note:** `loader` currently takes `LevelData*` in its constructor
(`explicit loader(LevelData* owner)`) and stores it as `owner_level`. This
pointer is used in `create_walker_owned()` to call
`owner_level->wire_entity()`. Phase 6 must eliminate this constructor
parameter entirely — the factory callback pattern replaces both the
`LevelData*` owner and the wire_entity call (since Phase 4 already
removed sim_* wiring in favor of `current_game`).

**Steps:**
1. Move `gloader` from `LevelData` to resources layer
2. Define `EntityFactory` callback struct for render component attachment
3. Add `entity_factory` callback on `GameWorld`
4. Platform wires gloader + attach_render callback at setup time
5. `GameWorld::add_ob()` calls `entity_factory` instead of `myloader`

**Risk:** Medium — requires the callback plumbing. But this is a clean,
well-defined interface boundary.

**Testing:** Full ctest. Verify entity creation works through the callback
in both SDL and headless modes.

---

### Phase 7: Kill LevelData

**Goal:** Move remaining rendering data from `LevelData` to a new `LevelVisuals`
type on `screen`. Delete the `LevelData` class entirely.

**Steps:**
1. Create `include/openglad/interface/level_visuals.h` with the LevelVisuals struct
2. Move `renderer_`, `pixdata[]`, `topx/topy` from `LevelData` into `LevelVisuals`
3. Add `LevelVisuals level_visuals_;` member to `screen`
4. Delete the `LevelData` class — it is fully replaced
5. Update all remaining code that references `level_data.*` to go through
   either `world()` (gameplay data) or `level_visuals_` (rendering data)

**Editor migration:** `level_editor.cpp` heavily manipulates `LevelData`
(entity lists, grid operations, `mysmoother.smooth()`). All entity-list and
grid references become `world()` access. Smoother calls (`smooth()`,
`set_target()`) go through `world().mysmoother`. Rendering data
(`pixdata[]`, `renderer_`, `topx/topy`) goes through `level_visuals_`. This
is mechanical but high-churn for the editor file specifically.

**Files changed (major):**
- New: `include/openglad/interface/level_visuals.h`
- Deleted: `include/openglad/data/level_data.h`, `src/data/level_data.cpp`
- Modified: `include/openglad/runtime/screen.h`, `src/sdl_client/runtime/screen.cpp`
- Modified: `src/sdl_client/ui/level_editor.cpp` (heavy — entity list + grid +
  rendering data references all change)
- Modified: All remaining files that reference `level_data.` (should be few by now)

**Risk:** Medium — most references were already migrated in Phases 1–3, but
the level editor is a significant remaining consumer of `LevelData`.

**Testing:** Full ctest.

---

### Phase 8: Define IRenderComponent in Gameplay

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
5. Same pattern for `LevelVisuals` → `ILevelVisuals` base in gameplay
6. Remove `#include <openglad/entities/walker_render.h>` from walker.h
   (only forward-declare `IRenderComponent`)

**Note:** This phase modifies `walker.h` in its current location
(`include/openglad/entities/`). The physical move to `gameplay/` happens in
Phase 10. This phase runs after Phases 4–5 because all three modify
`walker.h` / `SimEntity` — sequencing avoids merge conflicts.

**Risk:** Low — small, mechanical change. The hard part is making sure all
downcast sites are updated.

---

### Phase 9: Split the Data Layer

**Goal:** Separate gameplay data structures from file I/O serialization.
`LevelData` was already eliminated in Phase 7. `gloader` was already moved to
resources in Phase 6. This phase moves the remaining serialization code.

**Gameplay keeps:**
- `GameWorld` (the in-memory game state — already exists from Phases 1–3)
- `guy` struct (character stat block)
- `statistics` struct
- Family descriptor types and registries

**Resources gets:**
- Level file I/O: `load_level(path, GameWorld&)`, `save_level(GameWorld&, path)`
  (free functions that populate/serialize the gameplay-relevant fields)
- Save file I/O: `SaveData` struct + `load_save()`/`save_save()`
- `gparser` / `cfg_store` (config parsing)
- `pixie_data` loading from .pix files
- All PhysFS/zip/yaml code (already in `io`)

**(gloader already moved in Phase 6.)**

**Note on `grid_file`:** `LevelData::grid_file` (`std::string`,
`level_data.h:114`) stores the source filename for the grid tilemap. It's a
resources concern — loaded from the level file, used by save. It does NOT
belong on `GameWorld` since it's a file path, not gameplay state. The resources
layer's `load_level()` / `save_level()` functions pass it alongside GameWorld
data during load/save (e.g. as a separate output/input parameter or in a
`LevelFileMetadata` bag).

**Steps:**
1. Move `SaveData` entirely to resources
2. Move `gparser`, `pixie_data` to resources
3. Move level file format code (`load_scenario_data()` etc.) to resources

**Note:** `LevelData::load()` / `save()` currently serialize both gameplay data
(entity lists, grid, metadata) and visual data (pixdata[], grid_file). The
resources-layer `load_level()` must populate both `GameWorld` and
`LevelVisuals` (or return visual data separately). Similarly `save_level()`
must read from both. Consider a `LevelFileData` bag struct that holds
`GameWorld&` + `LevelVisuals&` + `LevelFileMetadata` (grid_file, description)
as the serialization interface.
   as free functions operating on `GameWorld&`

**Risk:** Low-Medium — straightforward moves. Smaller than originally planned
since gloader was already handled.

**Depends on Phase 5.** SaveData must be fully decoupled from entity code
before moving it to resources — otherwise you'd move a type that still has
entity-layer coupling and need to rework the boundary.

---

### Phase 10: Reorganize Directory Structure

**Goal:** Move source files into the four component directories. This phase
also completes the screen display split: `screen::redraw()`, viewscreens, and
other display code move to `interface/`. The logical split was already done in
Phases 1–3 (GameWorld extracted, screen became a display shell). Phase 10 makes the
physical move.

**Target layout:**

```
include/openglad/
├── core/                    (unchanged)
├── gameplay/
│   ├── game_world.h
│   ├── gameplay_context.h
│   ├── render_component_base.h
│   ├── walker.h, living.h, weap.h, treasure.h, effect.h
│   ├── guy.h, statistics.h, obmap.h
│   ├── sim_entity.h, sim_event_log.h, event.h
│   ├── sim_emit.h, irandom.h
│   └── families/             (descriptors, registries)
│   (no level_data.h — eliminated in Phase 7)
│   (no sim_world.h — absorbed into GameWorld in Phase 2)
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
│   ├── game_world.cpp        (includes absorbed sim_world tick logic)
│   ├── gameplay_context.cpp
│   ├── walker*.cpp, living.cpp, weap.cpp, treasure.cpp, effect.cpp
│   ├── guy.cpp, obmap.cpp, combat_math.cpp, stats.cpp
│   ├── sim_event_log.cpp
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

**The video/screen inheritance problem:** `screen` currently inherits from
`video`, which wraps `SDL_Surface` and SDL rendering primitives. Moving `screen`
to the interface layer requires breaking this inheritance — interface code
cannot depend on SDL types. **Solution:** Split `video` into an abstract base
class (drawing primitives, pixel buffer interface) in interface and a concrete
SDL implementation in platform. `screen` inherits from the abstract base.
Platform provides the concrete `video` via `PlatformBridge::create_surface` or
constructor injection. This split should happen as part of the interface batch
move (step 3 above), not deferred to Phase 11.

**GameWorld ownership transfer:** During Phases 1 through 9, `screen` owns
`GameWorld` as a direct member (`GameWorld world_;`). In this phase, when
`screen` moves to the interface layer, GameWorld ownership transfers to
`GameSession` in platform. `screen` is refactored to hold a non-owning
`GameWorld*` pointer. This is a behavioral change — level transition code that
currently creates/reinitializes `world_` inside `screen` must move to
`GameSession`. Plan this as a dedicated sub-step before the physical file moves.

**Note on `og_runtime` SDL mixing:** `og_runtime` currently has 13 source files
from `src/sdl_client/runtime/` (`guy_create.cpp`, `walker_render_bridge.cpp`,
`score_panel.cpp`, `legacy_globals.cpp`, `game_loop.cpp`, `game.cpp`,
`sdl_context_services.cpp`, `screen.cpp`, `glad_gameplay.cpp`,
`screen_lifecycle.cpp`, `game_session.cpp`, `input_event_bridge.cpp`,
`cheat_handler.cpp`). Additionally, `og_runtime` includes 8 files from
`src/runtime/` (non-SDL): `game_context.cpp`, `game_session_core.cpp`,
`screen_core.cpp`, etc. So Phase 10 triage covers ~21 files total. The
directory reorganization needs to split these between interface and platform —
it's not a matter of moving whole directories. Each file needs individual
triage based on its dependencies.

**Risk:** High churn on include paths and CMake. The video split, ownership
transfer, and per-file `og_runtime` triage add logic changes on top of the
mechanical moves. Run full ctest after each batch.

---

### Phase 11: Define Inter-Component Interfaces

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

    // Render surface management — returns abstract video base, NOT SDL_Surface*
    // Phase 10 splits video into an abstract base (interface) and SDL concrete
    // (platform). This callback returns the abstract base type.
    std::function<video*(int w, int h)> create_surface;
};
```

**Important:** No SDL types in this interface. The whole point of PlatformBridge
is that the interface layer doesn't know about SDL. Platform registers concrete
SDL (or headless no-op) implementations. Interface calls these instead of SDL
functions directly. This replaces the current `LevelDataHooks` pattern with a more general
mechanism.

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

### Phase 12: Enforce Dependencies and Clean Up

**Goal:** Build-time enforcement of component dependency rules. Clean up all
remaining global state identified in the singletons audit.

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
4. Remove `set_global_context()` and the `ctx()` fallback path.
   **Note:** `src/io/platform_io_common.cpp` has 5 call sites to
   `ctx().mounted_campaign`. After `ctx()` retirement, these switch to a
   resources-layer API that receives the campaign identifier from the platform
   layer (e.g., a `set_mounted_campaign()` / `get_mounted_campaign()` pair on
   a resources namespace, populated by GameSession at mount time).

**Remaining globals cleanup (inlined from `remaining-singletons-audit.md`):**

**Thread-locals to resolve:**

5. `g_reset_time_ptr` (`thread_local` in `src/core/util.cpp:46`) — remove
   indirection, wire timer through session APIs or pass timer anchor explicitly.
   Currently points at `GameSession::reset_time_`.
6. `s_test_context_override` (`static thread_local` in
   `src/runtime/game_context.cpp:38`) — retire along with `ctx()` fallback
   (step 4 above).
7. `path_walker`, `path_map`, `pather` (`thread_local` in
   `src/entities/walker_pathing.cpp:31,106,107`) — moved to
   `GameplayContext` in Phase 4 (already handled by step 5 of Phase 4).
8. `grass_rng` (`static thread_local std::mt19937` in
   `src/sdl_client/io/platform_io.cpp:432`) — **accepted exception**.
   Rendering scratch RNG, not game state. Document as legitimate.

**Session state still standalone:**

9. Migrate UI globals to interface-layer owned state:
   - `helptext` (`char[HELP_WIDTH][MAX_LINES]`), `end_of_file` (`short`) in
     `help.cpp:43-44` → interface component state
   - `backgrounds[]` (`Sint32[]`) in `level_editor.cpp:175`, `object_pane`
     (`std::vector<ObjectType>`) in `level_editor.cpp:250` → editor state

**UI layout globals:**

10. 13 button descriptor arrays in `picker.cpp` (lines 416–627) and
    `picker_dialogs.cpp` (lines 37–49) → const-ify where possible. These are
    `button[]` structs used for menu layout. Some have mutable fields
    updated at runtime; those stay mutable but move to interface component state.

**Renderer/hardware globals (accepted — process-level):**

11. These are legitimate process globals and stay as-is. Document them:
    - `E_Screen` (`std::unique_ptr<Screen>`, `video.cpp:53`)
    - `joysticks` (`SDL_Joystick*[MAX_NUM_JOYSTICKS]`, `input.cpp:72`)
    - `letters1`, `letters_big` (`PixieData`, `text.cpp:27-28`)
    - `text_buffer` (`char[255]`, `text.cpp:115`)
    - sai2x color masks and line buffers (`sai2x.cpp:18-28`)
    - `pal`, `mypalette` (`intro.cpp:38-39`)

**Process config:**

12. `cfg` (`cfg_store`, `gparser.cpp:40`) — keep as process global. Reduce
    `active_config()` wrapper indirection where it adds no value.

**Immutable registries (no action needed):**

13. Family registries (`s_registry` in `family_registry.cpp`,
    `effect_family_registry.cpp`, `treasure_family_registry.cpp`,
    `generator_family_registry.cpp`, `weapon_family_registry.cpp`) — init-once
    static data. Acceptable as-is.

**Test-only and platform-specific globals (no action needed):**

14. All `#ifdef TESTING` globals (`g_test_remove_exits`, `g_picker_mainmenu_calls`,
    `g_picker_max_mainmenu_calls`, `g_test_in_game`, `g_test_game_epoch`,
    `s_yes_or_no_overrides`, `s_force_real_dialogs`, `g_trace_buffer`,
    `g_trace_mutex`, `g_test_level_tick_limit_override`) and `#ifdef __EMSCRIPTEN__`
    globals (`g_start_game_requested`, `g_game_state`, `g_state_initialized`,
    `idbfs_sync_done`) — acceptable, no migration needed.

15. Update `docs/ARCHITECTURE.md` with the new component model.

**Final audit checklist (replaces re-run step):**

16. Verify final state:
    - `current_game` is the only thread-local in gameplay
    - `current_session` is the only thread-local in platform (game-state
      category)
    - `grass_rng` is an accepted rendering scratch exception
    - All other globals are: const/init-once, `#ifdef TESTING`-only,
      `#ifdef __EMSCRIPTEN__`-only, or documented process globals
      (`cfg`, `E_Screen`, `joysticks`, family registries)

**Risk:** Low — enforcement catches violations at compile time.

---

## Phase Sequence

All phases run sequentially, one at a time. Each phase must pass full ctest
before the next begins.

```
Phase 1a (GameWorld shell, entity lists)
   ▼
Phase 1b (spatial data, queries, finders, metadata)
   ▼
Phase 2  (absorb SimWorld into GameWorld)
   ▼
Phase 3  (game state flags, eliminate TickResult)
   ▼
Phase 4  (current_game thread-local)          ← moved up: only needs Phases 1a–3
   ▼
Phase 5  (SaveData decoupling + sim_config removal)
   ▼
Phase 6  (move gloader to resources)
   ▼
Phase 7  (kill LevelData, create LevelVisuals)
   ▼
Phase 8  (IRenderComponent)
   ▼
Phase 9  (remaining data layer split)
   ▼
Phase 10 (directory reorganization + video split + ownership transfer)
   ▼
Phase 11 (inter-component interfaces)
   ▼
Phase 12 (enforcement + cleanup)
```

**Why this order:**

- **Phases 1a–3** create GameWorld incrementally: entity lists (1a), spatial
  data + queries (1b), then tick logic (2), then state flags (3). Each is
  independently testable and bisectable.
- **Phase 4** (current_game) comes right after Phase 3. It only needs GameWorld
  to exist with the right fields (Phases 1a–3) — it does NOT need gloader moved
  or LevelData killed. Moving it up front unblocks Phase 5 (SaveData decoupling)
  earlier.
- **Phase 5** (SaveData decoupling) needs `current_game->world->` to replace
  `sim_save->` references — entities have no other path to reach GameWorld fields.
- **Phase 6** (gloader) can now use the cleaner wiring: since Phase 4 already
  introduced `current_game`, no transitional `sim_*` pointer wiring is needed
  in the factory callback. Entities use `current_game->` from the start.
- **Phase 7** (kill LevelData) depends on Phase 6 (gloader moved out of
  LevelData).
- **Phase 8** (IRenderComponent) runs after Phases 4–5 to avoid merge conflicts
  in `walker.h` / `SimEntity` — all three modify these files.
- **Phase 9** depends on Phase 5. SaveData must be fully decoupled from entity
  code before moving it to resources.
- **Phase 10** is the big mechanical move — much easier after the logical splits
  (1–9) are in place. Includes the video/screen inheritance split and the
  GameWorld ownership transfer from screen to GameSession.
- **Phases 11–12** are cleanup/enforcement after reorganization.

---

## Known Cyclic Dependencies

The CMakeLists.txt uses `--start-group`/`--end-group` (`CMakeLists.txt:749-766`)
to paper over link-order issues caused by these inter-module cycles. Phase 12
wants to enforce acyclic dependencies — these are the cycles to break:

1. **og_render ↔ og_runtime** — `view.h` includes `game_session.h`; runtime
   includes render headers
2. **og_input ↔ og_runtime** — Header-level cycle path: `input.h` →
   `game_session.h` → (runtime depends on) `game_context.h` →
   `input/input_state.h`. Link-level cycle: `og_input` uses `GameSession`
   types, `og_runtime` uses `InputState` types.
3. **og_entities ↔ og_data** — entity source files include `level_data.h`,
   `save_data.h`, `gparser.h`; data code includes `walker.h`, `guy.h`
4. **og_entities ↔ og_runtime** — entity code includes `game_session.h`;
   runtime includes entity headers
5. **og_ui ↔ og_runtime** — UI headers include `screen.h`; runtime includes UI
   headers
6. **og_ui → og_entities** — `results_screen.h` includes `guy.h`
7. **og_render → og_data** — `view.h` and `radar.h` include `level_data.h`;
   `pixie.h` includes `pixie_data.h`

**Which phases break each cycle:**
- **Cycles 3, 4:** Broken by Phases 1–5. Entities stop reaching into
  data/runtime for `sim_*` access — they use `current_game->` instead.
- **Cycles 1, 2:** Broken by Phase 10. Render/input stop needing
  `game_session.h` directly after the directory reorg and video split.
- **Cycles 5, 6, 7:** Broken by Phase 10. UI/render references to
  `screen.h`, `level_data.h`, `guy.h` resolve when files move to their
  target components and dependencies flow inward.

---

## Relationship to Existing Desingletonize Work

This plan builds on the desingletonize work (largely complete). Some remaining
desingletonize phases are subsumed:

| Desingletonize Phase | Status | Disposition |
|---|---|---|
| Phase 0 (const sweep) | Done | N/A |
| Phase 1 (eliminate ctx globals) | Partial | Subsumed by Phase 4 here (`current_game` replaces `ctx()`) |
| Phases 2-9 | Done | N/A |
| Phase 10 (final verification) | Not started | Subsumed by Phase 12 here |

The key change: instead of `ctx()` → `current_session->ctx_`, we go to
`current_game->` in gameplay code (and `current_game->world->rng_` for the
RNG). `current_session` remains at the platform level for interface/UI code
that needs session state.

---

## Hard Parts and Mitigations

### 1. Splitting screen (Phases 1–3 + 10)

`screen` is THE central class — ~1300 lines, extends `video`, owns both game
state and display state, referenced from everywhere.

**Mitigation (Phases 1–3):** Keep `screen` as a forwarding shell initially.
Add `GameWorld` as a member. Provide forwarding accessors on `screen` so
existing code doesn't break. Phases 1a–3 split the work into four steps
(entity lists → spatial data/queries → tick logic → state flags) to keep
changes bisectable.

**Mitigation (Phase 10):** The `screen` → `video` inheritance must be broken
to move `screen` to the interface layer. `video` splits into an abstract base
(interface) and SDL concrete (platform). GameWorld ownership transfers from
`screen` to `GameSession`. Both changes happen as dedicated sub-steps before
the physical file moves.

### 2. Navigation treasure redesign (Phase 5)

`treasure_family_navigation.cpp` currently makes blocking UI calls
(`yes_or_no_prompt()`) and does save I/O (`sim_save->load/save`) from inside
entity behavior code. This is the worst layering violation.

**Mitigation:** Deferred action queue. The treasure entity becomes a dumb
sensor — it detects "player on exit tile" and emits a
`RequestExitConfirmation` event. The tick finishes normally. The outer layer
(platform) drains the event, shows the prompt, and handles the entire level
transition flow directly. Entity code never re-enters for this decision.

No state machine on the entity, no "pending response" field on GameWorld, no
multi-tick continuation. The exit/withdrawal logic was already an outer-layer
concern — this moves it where it belongs.

### 3. gloader factory pattern (Phase 6)

`gloader` creates entities with graphics — it touches all layers.

**Mitigation:** Use callback injection. Resources provides the loader, interface
provides a `attach_render(walker&, PixieData&)` callback, platform wires them
together. Headless mode passes a no-op callback. GameWorld gets a
`std::function<std::unique_ptr<walker>(Order, int)>` entity_factory callback
that `add_ob()` delegates to.

### 4. Include path churn (Phase 10)

Hundreds of `#include` changes when files move to new directories.

**Mitigation:** Write a migration script. Move one component at a time. Full
ctest after each batch. Use `git mv` to preserve history.

### 5. video/screen inheritance split (Phase 10)

`screen` extends `video` (SDL rendering). Moving `screen` to the interface
layer means `video` must be split into an abstract base and an SDL concrete.
This is a logic change hiding inside a "directory reorganization" phase.

**Mitigation:** Do the video split as a dedicated sub-step before the physical
file moves. The abstract `video` base defines the drawing primitive interface.
Platform provides the concrete SDL (or headless) implementation via constructor
injection or `PlatformBridge`.

---

## Testing Strategy

**All tests must pass at the end of every phase.** Test migration happens
incrementally — each phase owns its test updates.

### TestGameplayContext (RAII helper)

Created in Phase 1a and used throughout all subsequent phases:

```cpp
// tests/unit/test_gameplay_context.h
class TestGameplayContext {
public:
    TestGameplayContext() {
        // Creates a minimal GameWorld (with deterministic RNG), event log
        // Sets current_game thread-local
    }
    ~TestGameplayContext() {
        // Tears down current_game
    }

    GameWorld& world();          // includes world().rng_ for RNG access
    SimEventLog& events();
};
```

### Difficulty in Tests

Tests set `world.difficulty` directly with explicit values — no dependency
on `GameSession` or config parsing:

```cpp
OG_UNIT_TEST(test_enemy_scaling) {
    TestGameplayContext ctx;
    ctx.world().difficulty = 200;  // Slaughter mode
    // ... test that enemy stats scale correctly ...
}
```

### Migration per Phase

- **Phase 1a/1b:** Tests that access `myscreen->level_data.*` update to
  `myscreen->world().*`. `TestGameplayContext` helper created in 1a.
- **Phase 2:** Tests that reference `sim_world_` update to `world_.tick()`.
- **Phase 3:** Tests that read `level_done` etc. from screen update to
  read from `world_`. `TickResult` assertions removed.
- **Phase 4:** Tests switch from `ctx()` / `sim_*` pointer setup to
  `current_game->` access. Wiring code in test fixtures simplified.
- **Phase 5:** Tests for exit/withdrawal behavior added. Tests stop
  referencing `sim_save` and `sim_config` from entity code.
- **Phase 6:** Entity creation tests verify factory callback path.
- **Phase 7:** Remaining `level_data.*` references cleaned up in tests
  (including level editor test paths).
- **Phase 8:** Rendering tests update downcast patterns.
- **Phase 9:** Data loading tests use new free functions in resources.
- **Phase 10:** Include paths updated in test files.
- **Phase 12:** Test-only globals (`#ifdef TESTING`) audited and documented.

---

## Summary: Before vs After

### Before (current)

```
10 modules (core, sim, data, entities, io, runtime, render, input, ui, platform)
+ sdl_client/ and text_client/ overlays
screen class: game state + rendering + sound in one object
SimWorld: separate class with tick logic, tick counters, RNG
LevelData: entity lists + rendering data + loader in one class
Entity code: holds 6 sim_* pointers, references SaveData, calls UI prompts
Thread-local: current_session (GameSession*) at platform level
Global accessor: ctx() with fallback/override machinery
```

### After (target)

```
4 components + core (gameplay, resources, interface, platform)
GameWorld class: pure game state + tick logic + RNG, no rendering, no I/O
No SimWorld class (absorbed into GameWorld)
No LevelData class (replaced by GameWorld + LevelVisuals)
Entity code: accesses current_game->world-> for everything (including RNG),
  no SaveData, no UI calls, no sim_config (effects checks in interface layer)
Thread-local in gameplay: current_game (GameplayContext*)
Thread-local in platform: current_session (GameSession*) — for UI/display code
No ctx() — retired
```

### New Ownership Model

```
Platform (GameSession)
├── Creates and owns GameplayContext → sets current_game
├── Creates and owns GameWorld → populates from save/level files via resources
│   ├── GameWorld.rng_ (deterministic LCG, accessed via current_game->world->rng_)
│   ├── GameWorld.entity_factory (callback wired to gloader in resources)
│   ├── GameWorld.difficulty (copied from GameSession at level start)
│   └── GameWorld.withdraw_requested (set by WithdrawToLevel event, tick() early-outs)
├── Creates and owns screen (display shell) → references GameWorld
├── Orchestrates game loop:
│   1. Poll input (platform)
│   2. Translate to game commands (interface)
│   3. Set current_game, call GameWorld::tick() (gameplay)
│   4. Drain sim events (platform dispatches sound, interface dispatches visuals)
│   5. Call screen::redraw() (interface)
│   6. Present frame (platform)
└── After level: read scores/stats from GameWorld, update SaveData, save via resources
```
