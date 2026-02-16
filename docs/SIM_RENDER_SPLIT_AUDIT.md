# Sim/Rendering Split Audit

**Branch:** `feat/sim-rendering-split`
**Base:** `master` (merge-base: `f7b39ec`)
**Date:** 2026-02-16
**Commits:** 17 (86 files changed, +2071 / -2763 lines)
**Build:** PASSES (ci-test preset)
**Tests:** ALL PASS (57 unit + 74 data + 117 runtime = 248 total)

---

## 1. Executive Summary

**This branch is conditionally ready to merge.** The sim/rendering split has made meaningful progress: SimWorld extracts the core tick loop, SimEventLog provides a clean event channel, and entity sound/notification emissions are fully event-driven. However, the split is incomplete. Entities still reach through `myscreen` and `active_screen()` to call runtime methods like `endgame()`, `damage_tile()`, `find_foes_in_range()`, and `query_passable()`. The sim module itself violates its own documented dependency rule by including `game_context.h` from the runtime layer. Walker inherits from `pixieN` which embeds `SDL_Surface*`, making true headless entity operation impossible. These are known limitations of an incremental migration, not regressions. The branch is safe to merge as a checkpoint if the remaining violations are tracked and prioritized for follow-up work.

---

## 2. Architecture Review

### 2.1 Dependency Direction

**Claimed:** `sim -> core` only; `entities -> core, sim, render(pixieN only)`
**Actual:** Multiple violations exist.

#### sim -> runtime violation

`sim_emit.h` (line 10) includes `<openglad/runtime/game_context.h>`. The `emit_sound()`, `emit_notification()`, and `emit_event()` helpers call the global `ctx()` accessor, binding the sim layer to the runtime layer's global context infrastructure.

`sim_world.cpp` (line 10) also includes `<openglad/runtime/game_context.h>` and calls `ctx().rng->next()` at line 40 for AI targeting.

**Impact:** This breaks the documented rule at architecture-rules.md line 33: "og_sim must NOT depend on SDL, render, ui, or platform." While `game_context.h` itself is SDL-free (it forward-declares screen), the dependency direction is sim -> runtime, which is backwards.

**Fix:** Pass `SimEventLog&` and `IRandom&` as parameters through the call chain instead of accessing them via `ctx()`.

#### entities -> runtime violation

Entity code directly accesses the `screen` object in 7 files (43 occurrences):

| File | Occurrences | What it accesses |
|------|-------------|------------------|
| `src/entities/families/treasure_family_navigation.cpp` | 17 | `endgame()`, `save_data`, `level_data`, `end`, `query_passable()` |
| `src/entities/families/effect_family_shield.cpp` | 10 | `find_foes_in_range()`, `find_foe_weapons_in_range()` |
| `src/entities/obmap.cpp` | 5 | `draw_box()`, `text_normal`, `viewob[0]->topx/topy` |
| `src/entities/walker.cpp` | 5 | `endgame()`, `remaining_foes()` declaration |
| `src/entities/families/effect_family_bomb.cpp` | 3 | `damage_tile()` |
| `src/entities/walker_combat.cpp` | 2 | `remaining_foes(myscreen, this)` |
| `src/entities/walker_movement.cpp` | 1 | commented-out reference |

#### entities -> render (beyond pixieN) violation

Entity `.cpp` files include render headers beyond the allowed pixieN base class:

| File | Line | Include | Used For |
|------|------|---------|----------|
| `src/entities/walker.cpp` | 35 | `render/view.h` | `compute_outline()` needs viewer_control |
| `src/entities/walker.cpp` | 36 | `render/smooth.h` | terrain smoothing in death logic |
| `src/entities/walker_pathing.cpp` | 20 | `render/view.h` | pathfinding visualization |
| `src/entities/obmap.cpp` | 24 | `render/view.h` | debug draw viewscreen offset |
| `src/entities/living.cpp` | 23 | `render/smooth.h` | terrain-based movement cost |
| `src/entities/weap.cpp` | 28 | `render/smooth.h` | terrain-based movement cost |
| `src/entities/treasure.cpp` | 30 | `render/text.h` | text rendering |
| `src/entities/families/weapon_family_door.cpp` | 12 | `render/smooth.h` | terrain query |

### 2.2 SimWorld Design

SimWorld is thin and well-scoped. `sim_world.cpp` (220 lines) extracts entity act(), dead entity cleanup, foe targeting, and level completion detection from the former monolithic `screen::act()`. The delegation in `screen::act()` (screen.cpp:426-489) is clean: it calls `sim_world_.tick()`, then cleans up viewscreen control pointers (correctly identified as a rendering concern), then dispatches events.

The `TickResult` struct is a clean interface for communicating sim outcomes to the runtime layer.

### 2.3 Event System

**EventKind values:** `PlaySound(4)`, `Notification(8)`, `SetPalette(11)`, `RequestRedraw(12)`

**Emission:** 63 calls to `emit_sound()`, `emit_notification()`, and `emit_event()` across 18 entity files. All sound and notification emissions are properly event-driven. No direct `soundp->play_sound()` or `set_display_text()` calls remain in entity code.

**Consumption:** Single dispatch loop in `screen::act()` (screen.cpp:446-476) handles all 4 event kinds correctly.

**Assessment:** The event set is appropriate for the current scope. Missing events that will be needed for full decoupling: `DamageTile`, `EndGame`, `QueryPassable`, `FindFoes`, `LevelTransition`.

### 2.4 GameContext

GameContext contains: `screen*`, `unique_ptr<options>`, `string mounted_campaign`, `cfg_store*`, `IRandom*`, `InputState`, service interfaces, and `unique_ptr<SimEventLog>`.

It is a transitional god object by design (documented as "Phase 1" in the header). The service interfaces (`IConfigContextService`, `IRenderContextService`, `IInputContextService`) suggest intent to move toward proper DI, but only `active_screen()` and `active_prefs()` are currently used through them. The `ctx()` global accessor makes dependencies invisible; any code can call `ctx()` and access anything.

**Assessment:** Acceptable for incremental migration. The key concern is that the sim layer uses `ctx()` for RNG and event log access, which should be parameter-passed instead.

---

## 3. Completeness Assessment

### 3.1 Entities (walker, living, guy, stats)

**Sound/notification:** Fully decoupled via events.
**SDL types in headers:** None in `include/openglad/entities/`. Clean.
**SDL types in impl:** `SDL_Rect` in `obmap.cpp:76` (debug draw only).
**screen access:** 43 occurrences across 7 files (see table in 2.1). Key coupling points:
- `walker::death()` calls `myscreen->endgame()` directly (walker.cpp:1373)
- `walker_combat.cpp` calls `remaining_foes(myscreen, this)` (line 342)
- `treasure_family_navigation.cpp` extensively manipulates `active_screen()->save_data` and calls `endgame()` for level transitions
- `effect_family_shield.cpp` calls `active_screen()->find_foes_in_range()` and `find_foe_weapons_in_range()`
- `effect_family_bomb.cpp` calls `active_screen()->damage_tile()`

**Inheritance chain:** `walker -> pixieN -> pixie`. `pixie.h` includes `SDL.h` and has `SDL_Surface* bmp_surface`. This is a fundamental coupling: every entity carries an SDL surface.

**Verdict:** Sound is clean. Direct screen access and SDL inheritance are the major remaining couplings.

### 3.2 Viewscreen (view.cpp)

**Location:** `src/render/view.cpp` (in the render layer)
**input():** 460 lines of input handling that mutates entity state (control switching, movement, combat). Contains sim logic: entity user assignment, act_type changes, endgame calls, team switching, cheat mode entity spawning.
**continuous_input():** 145 lines of per-frame input that calls `control->walkstep()`, `control->init_fire()`, `control->special()`, `control->animate()`.

**Assessment:** Viewscreen input methods are the thickest remaining sim/render coupling. They read from SDL input infrastructure and directly drive entity behavior. The `refactor/semantic-input` branch (2 commits ahead, not yet merged) introduces `InputState` to abstract SDL key queries but does not move the entity-driving logic out of viewscreen.

### 3.3 Game Loop (game_loop.cpp)

**Frame order:** redraw check -> `s.act()` (sim) -> render -> input poll -> `s.continuous_input()` -> palette cycle -> FPS cap.

**Assessment:** High-level separation is clean. `s.act()` delegates to SimWorld. Render is behind `#ifndef TESTING` and `deps.enable_render` guards. `obmap->draw()` debug visualization in entities is called from the game loop (line 80), which is a minor boundary violation.

### 3.4 Level Data

Level loading (`level_data.cpp`) uses `SDL_RWops` for file I/O only, not for graphics. Grid, entity lists, and metadata are pure data structures. Level loading is properly decoupled from rendering.

### 3.5 Menus and UI

Menus remain tightly coupled to both sim and render. `picker.cpp`, `results_screen.cpp`, and `level_editor.cpp` directly create/destroy entities and manipulate screen state. This is expected — the sim split focused on the game loop, not the menu layer.

### 3.6 Sound

**Fully decoupled.** All entity sound emissions use `og::sim::emit_sound(sound_id)`. No direct `soundp->play_sound()` calls remain in entity code. The runtime dispatches sounds from the event log after each tick.

### 3.7 Screen Management

`screen` inherits from `video` (SDL rendering). `screen::act()` properly delegates to `SimWorld::tick()` and dispatches events. Screen transitions (endgame, level loading) are still reached directly from entity code via `myscreen->endgame()`.

---

## 4. Code Quality

### 4.1 Include Hygiene

- **graph.h:** Zero active includes. 3 commented-out includes serve as refactoring documentation. Rule enforced.
- **Forward declarations:** 20+ forward declarations used appropriately in public headers.
- **Include style:** All public headers use angle brackets (`<openglad/module/header.h>`). Correct.
- **base.h:** Legacy umbrella header includes `SDL.h`, `input.h`, `render/pal32.h`, and `soundob.h`. Included by `walker.h` and `effect.h`. This is the primary channel through which SDL leaks into the entity layer's compilation units.

### 4.2 SDL Leakage

| Layer | Direct SDL | Indirect SDL | Verdict |
|-------|-----------|--------------|---------|
| `src/sim/` | 0 | 0 (via game_context.h, but that header is SDL-free) | CLEAN |
| `include/openglad/sim/` | 0 | 0 | CLEAN |
| `include/openglad/entities/` | 0 | SDL comes through pixie.h, base.h | LEAKS via transitive includes |
| `src/entities/` | 1 (`SDL_Rect` in obmap.cpp:76) | Many via base.h | 1 direct + transitive |

### 4.3 Dead Code

- No orphaned function declarations found.
- PR #36 (commit 23b8e1b) removed the sim_commands, ui_state, and frame_timing scaffolding.
- PR preceding (commit 53011a8) removed the stub Simulator class.
- 18 commented-out code blocks remain, all with TODO/FIXME markers explaining their purpose.
- `walker_draw.cpp` (370 lines, new file) properly houses draw methods extracted from `walker.cpp`.

### 4.4 Consistency

- Naming: Consistent snake_case for modern code. Legacy camelCase in `assignKeyFromWaitEvent()` and similar.
- Event API: Consistent `emit_sound()`, `emit_notification()`, `emit_event()` naming.
- The `active_screen()` pattern (inline helper returning `ctx().game_screen ?: myscreen`) is duplicated identically in 3 entity family files. Should be centralized.

---

## 5. Test Coverage Analysis

### 5.1 Test Counts

| Binary | Tests | SDL Required | What's Covered |
|--------|-------|-------------|----------------|
| `og_unit_tests` | 57 | No | SimEventLog (13), InputState (26), GameSession RAII (4), FamilyRegistry (11), event emission (3) |
| `og_data_tests` | 74 | Yes | Save/load (11), LevelData (16), IO (20), Campaign (8), GParser (7), LevelData binary formats (12) |
| `og_runtime_tests` | 117 | Yes | Game loop (4), Input handling (21), Walker behavior (73), Viewscreen input (5), Effects (8), misc (6) |
| **Total** | **248** | | |

### 5.2 Headless Coverage Assessment

The 57 headless unit tests cover:
- SimEventLog: push, clear, drain, tick tracking, all 4 event kinds
- InputState: all directions, diagonals, cancellation, multi-player isolation, combos
- GameSession: RAII lifecycle, RNG determinism, config access
- FamilyRegistry: all 20+ families' metadata

**Gap:** No headless test exercises `SimWorld::tick()` with actual entities. `test_sim_world_headless.cpp` exists (110 lines) but the sim world tick test requires entity creation which currently requires SDL (due to pixie/pixieN constructors needing SDL surfaces).

### 5.3 What Can Only Be Tested with SDL

- Walker creation and movement (requires pixel data via pixieN)
- Combat (requires entity instances)
- Level loading (uses `SDL_RWops`)
- Viewscreen input handling
- All rendering paths

**Assessment:** The inability to create entities without SDL is the fundamental blocker for headless sim testing. Until walker's inheritance from pixieN is broken (or pixie is given a headless mode), the headless test boundary is limited to SimEventLog, InputState, and metadata.

---

## 6. Gap Analysis

| # | Issue | Location | Severity | Fix |
|---|-------|----------|----------|-----|
| G1 | sim_emit.h includes game_context.h (sim -> runtime) | `include/openglad/sim/sim_emit.h:10` | Should fix before merge | Pass `SimEventLog&` as parameter; remove `ctx()` dependency from sim module |
| G2 | sim_world.cpp includes game_context.h, calls ctx().rng | `src/sim/sim_world.cpp:10,40` | Should fix before merge | Pass `IRandom&` as parameter to `SimWorld::tick()` |
| G3 | effect.h includes screen.h (entities -> runtime) | `include/openglad/entities/effect.h:23` | Should fix before merge | Remove include; effect.cpp doesn't use screen directly. Inherited via family callbacks. |
| G4 | walker inherits pixieN which contains SDL_Surface* | `include/openglad/entities/walker.h:35`, `include/openglad/render/pixie.h:70` | Can fix later | Extract position/size data into a SimEntity base class; make pixie a rendering component |
| G5 | Entity family callbacks access screen via active_screen() | `src/entities/families/treasure_family_navigation.cpp:19-111`, `effect_family_bomb.cpp:18-53`, `effect_family_shield.cpp:16-96` | Can fix later | Route through sim_level/sim_save references or emit events for endgame/damage_tile/find_foes |
| G6 | walker::death() calls myscreen->endgame() directly | `src/entities/walker.cpp:1373` | Can fix later | Set a flag on TickResult or emit an EndGame event instead |
| G7 | remaining_foes() takes screen* parameter | `src/entities/walker_combat.cpp:50,342` | Can fix later | Refactor to accept `LevelData&` or `const std::list<walker*>&` |
| G8 | obmap::draw() is a render function in entities module | `src/entities/obmap.cpp:57-116` | Can fix later | Move to `src/render/obmap_debug_draw.cpp`; pass obmap data in |
| G9 | walker.cpp includes render/view.h and render/smooth.h | `src/entities/walker.cpp:35-36` | Can fix later | Move compute_outline() to walker_draw.cpp; route terrain queries through sim_level |
| G10 | base.h umbrella includes SDL.h into all entity code | `include/openglad/legacy/base.h:46` | Can fix later | Incrementally replace base.h with narrow includes; eventually remove SDL from base.h |
| G11 | Viewscreen input methods drive entity sim logic | `src/render/view.cpp:443-1047` | Can fix later | Move entity-driving logic to a sim-layer input handler; viewscreen maps SDL events to InputState |
| G12 | active_screen() helper duplicated in 3 entity family files | `effect_family_bomb.cpp:18`, `effect_family_shield.cpp:16`, `treasure_family_navigation.cpp:19` | Can fix later | Centralize to a single helper or use ctx().active_screen() |
| G13 | semantic-input branch not merged | `refactor/semantic-input` (2 commits ahead) | Consider before merge | Merge or rebase; it adds InputState-driven viewscreen input which aligns with the split |

---

## 7. Merge Readiness Verdict

### Build Status
- **ci-test:** PASSES (clean build, 0 warnings examined)

### Test Status
- **og_unit_tests:** 57/57 passed
- **og_data_tests:** 74/74 passed
- **og_runtime_tests:** 117/117 passed

### Diff Size
- 86 files changed, +2071 / -2763 lines (net reduction of 692 lines)
- 17 commits since divergence from master

### Merge Conflicts
- None detected (clean merge-base)

### Breaking Changes
- `Simulator` class removed (was a stub, never used externally)
- `sim_commands.h`, `ui_state.h`, `frame_timing.h` removed (scaffolding, no external consumers)
- `EventKind` values renumbered (was 1-5, now None=0, PlaySound=4, Notification=8, SetPalette=11, RequestRedraw=12)
- `SimWorld` is new API; `screen::act()` still exists and delegates to it

### Verdict

**CONDITIONAL MERGE.** The branch is safe to merge if:

1. **G1 and G2 are fixed first:** The sim module must not include runtime headers. This is the documented core rule and it's violated. Fix by passing `SimEventLog&` and `IRandom&` as parameters to `emit_*()` and `SimWorld::tick()`.

2. **G3 is fixed first:** `effect.h` should not include `screen.h`. This is a public header pollution issue.

3. **G13 is decided:** Either merge `refactor/semantic-input` first (recommended) or confirm it can be cleanly rebased after.

Items G4-G12 are technical debt that can be tracked as follow-up issues. They represent the remaining 60% of the sim/rendering split work.

---

## 8. Recommendations (Priority Order)

1. **Fix sim -> runtime dependency (G1, G2).** Pass `SimEventLog&` and `IRandom&` as parameters through the entity call chain. The sim module's `emit_*()` helpers should accept a `SimEventLog&` parameter or entities should receive a reference at construction time. `SimWorld::tick()` should accept `IRandom&`. This is a ~2 hour change.

2. **Remove screen.h from effect.h (G3).** Check if effect.cpp actually needs screen; likely it gets it transitively. Remove the include from the public header. ~15 minute change.

3. **Merge semantic-input branch (G13).** The InputState abstraction is complementary to the sim split and should land together.

4. **Centralize active_screen() pattern (G12).** Three entity family files define identical `active_screen()` inline helpers. Create a single accessor.

5. **Move obmap::draw() to render layer (G8).** The debug visualization function directly calls screen rendering methods. Move it to `src/render/` and pass obmap data in.

6. **Route entity screen access through sim_level (G5, G6, G7).** The `sim_level`, `sim_save`, and `sim_enemy_freeze` pointers on walker are the right pattern. Extend it: add `endgame()` as an event emission, refactor `remaining_foes()` to use `LevelData&`, route `find_foes_in_range()` through level data.

7. **Break walker -> pixieN inheritance (G4).** This is the largest remaining task. Extract position, size, and collision data into a `SimEntity` base class. Make rendering data (pixel buffer, SDL_Surface) a component that the render layer attaches. This enables true headless entity testing.

8. **Extract sim logic from viewscreen::input (G11).** Move entity control logic out of `view.cpp` into a sim-layer controller that accepts `InputState` and drives entity behavior. The viewscreen should only translate SDL events into `InputState` and forward rendering-related operations.

9. **Eliminate base.h from entity headers (G10).** Replace `base.h` includes in `walker.h` and `effect.h` with the specific narrow includes they actually need. This removes SDL from the entity header compilation chain.
