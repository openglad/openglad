# Phase Remediation Plan — OpenGlad Component Architecture Migration

**Branch:** `feat/desingletonize`
**Date:** 2026-02-25
**Source:** 20-worker reaudit (10 phases × 2 auditors each)

---

## Table of Contents

1. [R1 — Phase 2: Stale ARCHITECTURE.md References](#r1--phase-2-stale-architecturemd-references)
2. [R2 — Phase 5: hit_anim Pragmatic Deviation](#r2--phase-5-hit_anim-pragmatic-deviation)
3. [R3 — Phase 6: gloader Header Path Caveat](#r3--phase-6-gloader-header-path-caveat)
4. [R4 — Phase 7: LevelData Not Deleted](#r4--phase-7-leveldata-not-deleted)
5. [R5 — Phase 8: No ILevelVisuals Interface](#r5--phase-8-no-ilevelvisuals-interface)
6. [R6 — Phase 9: No Resources Module](#r6--phase-9-no-resources-module)
7. [R7 — Phase 9: SaveData Still Owns Entity Data](#r7--phase-9-savedata-still-owns-entity-data)
8. [R8 — Phase 9: LevelData Thin Adapter Not Eliminated](#r8--phase-9-leveldata-thin-adapter-not-eliminated)
9. [Execution Order](#execution-order)
10. [Summary Table](#summary-table)

---

## R1 — Phase 2: Stale ARCHITECTURE.md References

**Severity:** Minor
**Phase:** 2
**Dependencies:** None

### Current State

`docs/ARCHITECTURE.md` references `game_world.cpp` as being in the `sim/` directory at three locations:

| Line | Current Text | Correct Path |
|------|-------------|-------------|
| 42 | `sim/                game_world, sim_event_log` | `gameplay/game_world` (sim_event_log remains in sim/) |
| 108 | `sim/game_world.cpp` \| `GameWorld::tick()` ... | `gameplay/game_world.cpp` |
| 715 | `src/sim/game_world.cpp` \| Live game simulation tick ... | `src/gameplay/game_world.cpp` |

The actual files are at:
- `include/openglad/gameplay/game_world.h`
- `src/gameplay/game_world.cpp`

The `gameplay/` directory also contains `gameplay_context.cpp`, which should be reflected in the docs.

### Target State

All three references updated to `gameplay/game_world.cpp`. Line 42's directory listing should split `sim/` and `gameplay/` entries.

### Proposed Fix

1. **Line 42:** Change `sim/                game_world, sim_event_log` to:
   ```
   sim/                sim_event_log
   gameplay/           game_world, gameplay_context
   ```
2. **Line 108:** Change `sim/game_world.cpp` to `gameplay/game_world.cpp`
3. **Line 715:** Change `src/sim/game_world.cpp` to `src/gameplay/game_world.cpp`

### Files Modified

| File | Change |
|------|--------|
| `docs/ARCHITECTURE.md` | 3 line edits |

### Estimated Scope

1 file, 3 line edits. Trivial.

---

## R2 — Phase 5: hit_anim Pragmatic Deviation

**Severity:** Minor / Acceptable
**Phase:** 5
**Dependencies:** None

### Current State

Phase 5 required all 6 visual effects to be decoupled from `sim_config` in entity code. 5 of 6 are fully decoupled — they unconditionally set display-only fields on walker (`attack_lunge`, `hit_recoil`, `hurt_flash`, `damage_numbers`, `heal_numbers`), and the render layer (`walker_draw.cpp:130-134`) checks `active_config()` to decide whether to display them.

The 6th effect — `hit_anim` — deviates. Instead of unconditionally creating hit FX entities, it reads `GameWorld::create_hit_effects` (a bool set by `screen.cpp:108,511` from config):

```cpp
// walker_combat.cpp:99
if(og::gameplay::current_game->world->create_hit_effects) {
    walker* newob = og::gameplay::current_game->world->add_ob(Order::FX, FAMILY_HIT);
    // ... position and configure the FX entity ...
}
```

This is architecturally different from the other 5 effects because `hit_anim` spawns actual `Order::FX` entities (memory allocation, entity lifecycle, obmap registration), whereas the others just set numeric/boolean display-only fields.

The `sim_config` coupling IS fully broken (confirmed: zero `sim_config` references in `src/entities/`). The config read happens in the outer layer (`screen.cpp`), and entity code only reads a plain bool from GameWorld.

### Target State — Two Options

**Option A: Accept as-is (RECOMMENDED)**

The current pattern is a defensible pragmatic deviation:
- `create_hit_effects` is a plain `bool` on `GameWorld` — no config coupling in entity code
- Unconditional creation would allocate FX entities even when the player has disabled hit animations, wasting memory and CPU
- The flag is set by the runtime layer, not entity code — dependency direction is correct

Document the deviation in `docs/plans/component-architecture/phase-05.md` with an "Accepted Deviation" note.

**Option B: Unconditional creation + lazy FX (NOT recommended)**

Unconditionally create the `FAMILY_HIT` entity, but give it a "dormant" flag so the render layer skips drawing it when `hit_anim` is off. This adds complexity for no practical benefit — the FX entity would still consume entity list slots, obmap entries, and lifecycle ticks.

### Proposed Fix (Option A)

Add an "Accepted Deviation" section to `docs/plans/component-architecture/phase-05.md`:

```markdown
## Accepted Deviation: hit_anim

`hit_anim` uses `GameWorld::create_hit_effects` (bool) instead of unconditional
entity creation. Unlike the other 5 display-only effects, hit_anim spawns actual
`Order::FX` entities with memory/CPU cost. The flag is set by the runtime layer
(`screen.cpp`), not by entity code — no sim_config coupling exists in entity code.
This is accepted as a pragmatic adaptation.
```

### Files Modified

| File | Change |
|------|--------|
| `docs/plans/component-architecture/phase-05.md` | Add deviation note |

### Estimated Scope

1 file, ~6 lines added. Trivial documentation.

---

## R3 — Phase 6: gloader Header Path Caveat

**Severity:** Minor
**Phase:** 6
**Dependencies:** R6 (resources module creation)

### Current State

Phase 6 calls for gloader to live in a "resources" layer. Currently:
- Public header: `include/openglad/data/gloader.h`
- Internal shim: `src/data/gloader.h` (forwards to public header)
- Implementation: `src/runtime/gloader.cpp`
- CMake target: listed in `OG_RUNTIME_SOURCES` (line ~314 in CMakeLists.txt)

The `EntityFactory` callback struct and `loader` class are functional and correctly decouple rendering from entity creation. The architecture is correct — only the module assignment is wrong (data/runtime instead of resources).

### Target State

Once the resources module exists (R6), gloader moves to:
- `include/openglad/resources/gloader.h`
- `src/resources/gloader.cpp`
- Listed in `OG_RESOURCES_SOURCES` in CMakeLists.txt

### Proposed Fix

Defer until R6 (resources module creation). When the resources module is created, move gloader as part of that work. No standalone action needed.

### Files Modified

None now. Part of R6 scope.

### Estimated Scope

N/A — deferred to R6.

---

## R4 — Phase 7: LevelData Not Deleted (MAJOR)

**Severity:** Major
**Phase:** 7
**Dependencies:** None (can proceed independently)

### Current State

Phase 7 calls for complete deletion of the `LevelData` class. Instead, it was hollowed into a compatibility shim:

- **Header:** `include/openglad/data/level_data.h` — 231 lines, ~30 public methods
- **Implementation:** `src/runtime/level_data.cpp` — 787 lines
- **Consumers:** 72 files include `level_data.h`

The shim contains:
1. **Pure forwarding methods** (~18 methods) that delegate directly to `world_ref_`:
   - `add_ob()`, `add_fx_ob()`, `add_weap_ob()` → `world_ref_.add_ob()` etc.
   - `remove_ob()` (has local logic for numobs decrement + list search)
   - `find_near_foe()`, `find_far_foe()`, `find_nearest_blood()`, `find_nearest_player()` → `world_ref_.*`
   - `find_in_range()`, `find_foes_in_range()`, `find_foe_weapons_in_range()`, `find_friends_in_range()` → `world_ref_.*`
   - `query_passable()`, `query_object_passable()`, `query_grid_passable()` → `world_ref_.*`
   - `delete_grid()`, `create_new_grid()`, `resize_grid()` → `world_ref_.*`
   - `clear()` → `world_ref_.clear()`

2. **Forwarding reference members** that alias GameWorld storage:
   - `id`, `title`, `type`, `par_value`, `time_bonus_limit`, `difficulty` — refs to world_ref_ fields
   - `grid`, `pixmaxx`, `pixmaxy` — refs to world_ref_ spatial data
   - `level_done` — ref to world_ref_.level_done
   - `numobs` — ref to world_ref_.living_count
   - `oblist`, `fxlist`, `weaplist`, `dead_list` — refs to world_ref_ entity lists
   - `myobmap` — ref to world_ref_.myobmap
   - `mysmoother` — ref to world_ref_.mysmoother

3. **Real logic that remains:**
   - **Constructors** (5 overloads, lines 148-153): Create/bind GameWorld, initialize loader, set up hooks
   - **`wire_entity_factory_callbacks()`** (lines 486-537): Wires loader into GameWorld's entity factory callbacks — this is the glue between loader and GameWorld
   - **`prepare_for_load()`** (lines 602-610): Pre-load setup (delete_objects + ensure loader exists + wire callbacks)
   - **`load()`/`save()`** (lines 644-686): Orchestrate `og::data::load_level()`/`save_level()` with error mapping, passing grid_file/description
   - **`delete_objects()`** (lines 576-600): Delegates to world_ref_ but also clears stale view controls via hooks and cleans obmap
   - **`set_sim_context()`** (lines 46-65): Synchronizes GameplayContext world pointer
   - **`remove_ob()`** (lines 539-568): Has entity-list search logic that duplicates what GameWorld should own
   - **`CampaignData`** class (lines 47-91 of header, lines 67-200+ of impl): Co-located in the same file, completely independent of LevelData

4. **Free functions** also in the file:
   - `remaining_foes(LevelData&, walker*)` → delegates to `world_ref_.remaining_foes()`
   - `get_scenario_title(const char*)` → delegates to `og::data::load_scenario_title()`

5. **Constants on LevelData** used externally:
   - `LevelData::TYPE_CAN_EXIT_WHENEVER` — used in `treasure_family_navigation.cpp:47`
   - `LevelData::TYPE_MUST_DESTROY_GENERATORS`
   - `LevelData::TYPE_MUST_PROTECT_NAMED_NPCS`

### Target State

`LevelData` class fully deleted. All consumers use `GameWorld&` directly (for gameplay data) or `og::data::load_level()`/`save_level()` (for I/O).

### Proposed Fix — 6 Sub-steps

**Step 7a: Move CampaignData to its own file**

`CampaignData` is completely independent of `LevelData`. Extract it:
- Create `include/openglad/data/campaign_data.h`
- Create `src/data/campaign_data.cpp`
- Move CampaignData class + implementation
- Update all `#include <openglad/data/level_data.h>` that only need CampaignData

Files: 2 new, ~5 modified (campaign_picker.cpp, level_editor.cpp, level_picker.cpp, etc.)

**Step 7b: Move TYPE_ constants to GameWorld**

Move `TYPE_CAN_EXIT_WHENEVER`, `TYPE_MUST_DESTROY_GENERATORS`, `TYPE_MUST_PROTECT_NAMED_NPCS` from `LevelData` to `GameWorld` (they describe level type, which is gameplay state already on GameWorld).

Files: `game_world.h`, `level_data.h`, `treasure_family_navigation.cpp`, and any other references.

**Step 7c: Move load/save orchestration out of LevelData**

The `load()`/`save()` methods on LevelData are thin wrappers around `og::data::load_level()`/`save_level()`. Move the orchestration (prepare_for_load, error mapping, grid_file/description management) into:
- A free function or a new `LevelLoader` helper class in `src/data/` or `src/runtime/`
- Callers (screen.cpp, level_editor.cpp, level_picker.cpp, tests) call the free function directly

Key callers to update:
- `src/sdl_client/runtime/screen.cpp` — main gameplay load
- `src/sdl_client/ui/level_editor.cpp` — editor load/save
- `src/sdl_client/ui/level_picker.cpp` — preview load
- `src/sdl_client/ui/results_screen.cpp:386` — creates a temporary LevelData
- `tests/test_load_levels.cpp`, `tests/test_level_data_*.cpp` — test loads

Files: ~10-15 modified.

**Step 7d: Move wire_entity_factory_callbacks to platform setup**

`wire_entity_factory_callbacks()` is the critical glue that connects the `loader` to `GameWorld`'s entity factory callbacks. This belongs in the platform/runtime layer:
- The SDL platform (`sdl_context_services.cpp`) already sets up `LevelDataHooks`
- Extend this to also wire entity factory callbacks onto GameWorld when a loader is created
- The headless platform does the same but without `attach_render`

Files: `sdl_context_services.cpp`, `platform_headless.cpp`, `screen.cpp`

**Step 7e: Move delete_objects cleanup logic to GameWorld**

`LevelData::delete_objects()` has real logic beyond forwarding:
- Calls `hooks_->clear_stale_view_controls()` to clear dangling viewscreen pointers
- Cleans up stale obmap entries after walker destruction

The stale view control cleanup should be a callback on GameWorld (it already has a hooks mechanism). The obmap cleanup is a GameWorld concern.

Files: `game_world.h`, `game_world.cpp`, `level_data.cpp`

**Step 7f: Update all 72 consumers and delete LevelData**

With all real logic migrated out, mechanically update remaining consumers:
- **Entity code (40 files):** These include `level_data.h` but grep confirms they access `current_game->world->` directly, not through a LevelData instance. They only need the header for `LevelData::TYPE_*` constants (moved in 7b) and the `remaining_foes()` free function. After 7b/7c, change includes to `<openglad/gameplay/game_world.h>`.
- **Test files (19 files):** Update to use GameWorld directly or the new load functions.
- **UI/editor files (4 files):** Already using `level->game_world()` pattern; update to use GameWorld directly.
- **Runtime files (3 files):** Update to new orchestration pattern.
- **Render files (2 files):** Update includes.

Delete:
- `include/openglad/data/level_data.h` (LevelData class only — CampaignData already extracted in 7a)
- `src/runtime/level_data.cpp` (after all logic migrated)
- Remove from `OG_RUNTIME_SOURCES` in CMakeLists.txt

Files: 72 modified, 2 deleted.

### Estimated Scope

~80 files touched across 6 sub-steps. This is the largest single remediation item. Recommend splitting into 3-4 PRs:
1. PR: Steps 7a + 7b (CampaignData extraction + constants move) — ~10 files
2. PR: Steps 7c + 7d (load/save + factory wiring migration) — ~15 files
3. PR: Steps 7e + 7f (cleanup logic + consumer migration + deletion) — ~60 files

---

## R5 — Phase 8: No ILevelVisuals Interface

**Severity:** Moderate
**Phase:** 8
**Dependencies:** None

### Current State

Phase 8 plan (step 5) calls for an `ILevelVisuals` interface base in the gameplay layer, mirroring the `IRenderComponent` pattern for walkers.

**What exists:**
- `IRenderComponent` at `include/openglad/gameplay/render_component_base.h` — virtual destructor only, used by `walker::render_` (line 222 of walker.h). ✅ Implemented.
- `walker::render_component()` returns `IRenderComponent*`, and rendering code downcasts to concrete `WalkerRender*`. ✅ Working.
- `LevelVisuals` at `include/openglad/interface/level_visuals.h` — a plain data struct with `pixdata[]`, `renderer_`, `topx`, `topy`. No base class.

**What's missing:**
- No `ILevelVisuals` base class in the gameplay layer
- `LevelVisuals` is not abstracted behind an interface
- The plan says: "Same pattern for `LevelVisuals` → `ILevelVisuals` base in gameplay"

### Target State — Evaluate Need

The `ILevelVisuals` interface was designed to allow gameplay code to hold a type-erased reference to level rendering data, matching the `IRenderComponent` pattern. However, the current architecture may not need it:

- `IRenderComponent` is needed because **walker** (a gameplay type) owns its render component — gameplay must hold the pointer but not know the concrete type.
- `LevelVisuals` is owned by **screen** (a runtime type), NOT by any gameplay type. Gameplay code does not hold or reference `LevelVisuals`.
- The rendering layer accesses `LevelVisuals` directly through `screen`, not through gameplay.

**If gameplay code never references LevelVisuals, the interface adds no value.**

### Proposed Fix — Two Options

**Option A: Skip ILevelVisuals (RECOMMENDED)**

The interface provides no benefit given the current ownership model. `LevelVisuals` is a runtime/render concern owned by `screen`. Gameplay code (GameWorld, walker, entity families) has no reference to it.

Document the decision in `docs/plans/component-architecture/phase-08.md`:
```markdown
## Decision: ILevelVisuals Not Needed

LevelVisuals is owned by screen (runtime layer), not by any gameplay type.
Gameplay code never references LevelVisuals. The ILevelVisuals interface
was designed for the case where gameplay needs to hold a type-erased
rendering reference, but this case does not arise for level visuals
(only for per-entity render components via IRenderComponent).
```

**Option B: Add ILevelVisuals anyway (for symmetry)**

Create `include/openglad/gameplay/level_visuals_base.h`:
```cpp
namespace og::gameplay {
class ILevelVisuals {
public:
    virtual ~ILevelVisuals() = default;
};
}
```
Make `LevelVisuals` inherit from it. This adds architectural symmetry but no functional value.

### Files Modified

**Option A:** 1 file (phase-08.md documentation)
**Option B:** 3 files (new header, level_visuals.h modified, CMakeLists.txt if needed)

### Estimated Scope

Option A: Trivial. Option B: Small (3 files, mechanical).

---

## R6 — Phase 9: No Resources Module (MAJOR)

**Severity:** Major
**Phase:** 9
**Dependencies:** R4 step 7c (load/save orchestration migrated out of LevelData)

### Current State

Phase 9 calls for a **resources** module that owns all file I/O serialization code. No such module exists:
- No `src/resources/` directory
- No `include/openglad/resources/` directory
- No `og_resources` CMake target
- No resources entry in the 10-module list

Serialization code is currently spread across two modules:
- **`og_data`** (`src/data/`): `level_file_io.cpp`, `save_data.cpp`, `gparser.cpp`, `pixie_data.cpp`
- **`og_runtime`** (`src/runtime/`): `gloader.cpp`, `level_data.cpp`

Phase 9 partially completed:
- ✅ `level_file_io.h/cpp` created with `load_level()`/`save_level()` free functions
- ✅ `save_data.cpp` moved from runtime to data module
- ✅ `LevelFileMetadata` struct created for serialization interface
- ❌ No standalone resources module
- ❌ gloader still in runtime (blocked on R3/R4)
- ❌ No clean module boundary between "gameplay data structures" and "file I/O"

### Target State

A new `og_resources` module containing all file I/O serialization:
- `src/resources/`: `level_file_io.cpp`, `save_data.cpp`, `gparser.cpp`, `pixie_data.cpp`, `gloader.cpp`
- `include/openglad/resources/`: corresponding public headers
- `og_resources` CMake target with dependency on `og_io`, `og_core`, `og_entities`

The existing `og_data` module retains only gameplay data structure definitions (structs, types) that don't do file I/O.

### Proposed Fix — 4 Sub-steps

**Step 9a: Create the resources module skeleton**

- Create `src/resources/` and `include/openglad/resources/`
- Add `OG_RESOURCES_SOURCES` list in CMakeLists.txt
- Add `og_resources` static library target
- Set up dependency rules: `og_resources` depends on `og_io`, `og_core`, `og_entities`
- Add to `docs/architecture-rules.md` dependency matrix

Files: CMakeLists.txt, `docs/architecture-rules.md`, 2 new directories.

**Step 9b: Move file I/O code to resources**

Move these files from `og_data` to `og_resources`:
- `src/data/level_file_io.cpp` → `src/resources/level_file_io.cpp`
- `include/openglad/data/level_file_io.h` → `include/openglad/resources/level_file_io.h`
- `src/data/save_data.cpp` → `src/resources/save_data.cpp`
- `include/openglad/data/save_data.h` → `include/openglad/resources/save_data.h`
- `src/data/gparser.cpp` → `src/resources/gparser.cpp`
- `src/data/pixie_data.cpp` → `src/resources/pixie_data.cpp`

Update all `#include` paths. Leave type-only headers (smooth.h, pixie_data.h struct def) in `og_data` if they define gameplay-used types.

Files: ~6 moved, ~40+ include-path updates.

**Step 9c: Move gloader to resources (depends on R4)**

After LevelData deletion (R4), gloader has no reason to stay in runtime:
- `src/runtime/gloader.cpp` → `src/resources/gloader.cpp`
- `include/openglad/data/gloader.h` → `include/openglad/resources/gloader.h`
- Delete internal shim `src/data/gloader.h`

Files: 2 moved, 1 deleted, ~15 include-path updates.

**Step 9d: Update ARCHITECTURE.md and module documentation**

Add the new module to the 10→11 module table, update dependency diagram, update directory listing.

Files: `docs/ARCHITECTURE.md`, `CLAUDE.md`

### Estimated Scope

~50 files touched across 4 sub-steps. Mostly mechanical `#include` path changes. Recommend:
1. PR: Step 9a (skeleton) — 3 files
2. PR: Steps 9b + 9d (move files + docs) — ~45 files
3. PR: Step 9c (gloader move, after R4) — ~18 files

---

## R7 — Phase 9: SaveData Still Owns Entity Data

**Severity:** Major
**Phase:** 9
**Dependencies:** R6 (resources module for SaveData's new home)

### Current State

`SaveData` (at `include/openglad/data/save_data.h:45-86`) still owns character/entity data:

```cpp
std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;  // line ~63
unsigned char team_size;                                      // line ~64
```

And has methods that copy entity data:
```cpp
void update_guys(const std::vector<const guy*>& guys);  // Copy team from runtime
```

Phase 9 plan says entity data should not live in SaveData — the `guy` struct is a gameplay type, and SaveData should be pure serialization.

However, the phase 9 plan also says "Gameplay Keeps: `guy` struct (character stat block)". The issue is that SaveData *owns* guy instances (`unique_ptr<guy>`) rather than just serializing them.

### Target State

Two viable approaches:

**Option A: SaveData keeps team_list but as serialization-only storage (RECOMMENDED)**

`SaveData` is fundamentally a save file representation. The `team_list` is the saved team — it's data that gets written to and read from disk. This is a resources/serialization concern. The ownership is fine as long as:
- Runtime gameplay code doesn't reach into `SaveData::team_list` during active gameplay
- The `guy` struct is defined in gameplay (it already is, at `include/openglad/entities/guy.h`)
- `update_guys()` is called only at save boundaries (level transitions, save points)

Check: Does entity code access `SaveData::team_list` during gameplay?

**Option B: Extract team storage to a GameplayTeam struct**

Create a `GameplayTeam` owned by `GameSession` or `GameContext` that holds the active team during gameplay. `SaveData` becomes purely a disk format — `load_save()` populates `GameplayTeam`, `save_save()` reads from it.

### Proposed Fix

**Investigate first**, then decide. Check all access patterns:

```
grep -r "save_data.*team_list\|save_data.*guy\|mysaves.*team_list" src/
```

If `team_list` is only accessed at save/load boundaries → Option A (document the decision, no code change needed beyond documentation).

If `team_list` is accessed during gameplay ticks → Option B (requires new struct, ~20 files).

### Files Modified

**Option A:** 1 file (phase-09.md documentation)
**Option B:** ~25 files (new GameplayTeam struct, update SaveData, update all team access sites)

### Estimated Scope

Investigation: 1 hour. Option A: trivial. Option B: medium (~25 files).

---

## R8 — Phase 9: LevelData Thin Adapter Not Eliminated

**Severity:** Major (but duplicate of R4)
**Phase:** 9
**Dependencies:** R4 (Phase 7 LevelData deletion)

### Current State

Phase 9 plan says: "LevelData was already eliminated in Phase 7." But Phase 7 did not eliminate it (see R4). This is a cascading dependency — Phase 9 cannot fully complete until Phase 7's LevelData deletion is done.

### Target State

After R4 completes, this finding is automatically resolved.

### Proposed Fix

No separate action. This is resolved by completing R4.

### Files Modified

None — resolved by R4.

### Estimated Scope

N/A — dependency only.

---

## Execution Order

### Wave 1 — Independent Quick Fixes (can run in parallel)

| Item | Scope | Risk |
|------|-------|------|
| **R1** — Fix ARCHITECTURE.md stale refs | 1 file, 3 edits | None |
| **R2** — Document hit_anim deviation | 1 file, 6 lines | None |
| **R5** — Document ILevelVisuals decision | 1 file, 6 lines | None |

**Estimated effort:** < 1 hour total.

### Wave 2 — LevelData Extraction (R4 steps 7a–7b)

| Item | Scope | Risk |
|------|-------|------|
| **R4/7a** — Extract CampaignData to own file | ~7 files | Low |
| **R4/7b** — Move TYPE_ constants to GameWorld | ~5 files | Low |

**Estimated effort:** 2-4 hours. No functional changes — purely structural.

### Wave 3 — LevelData Core Migration (R4 steps 7c–7e)

| Item | Scope | Risk | Depends on |
|------|-------|------|------------|
| **R4/7c** — Move load/save orchestration | ~15 files | Medium | Wave 2 |
| **R4/7d** — Move factory wiring to platform | ~5 files | Medium | Wave 2 |
| **R4/7e** — Move delete_objects logic to GameWorld | ~3 files | Low | Wave 2 |

**Estimated effort:** 4-8 hours. This is where the real logic migration happens.

### Wave 4 — LevelData Deletion (R4 step 7f)

| Item | Scope | Risk | Depends on |
|------|-------|------|------------|
| **R4/7f** — Update 72 consumers, delete LevelData | ~72 files | Medium | Wave 3 |

**Estimated effort:** 4-8 hours. High churn but mechanical.

### Wave 5 — Resources Module (R6, R7)

| Item | Scope | Risk | Depends on |
|------|-------|------|------------|
| **R6/9a** — Create resources module skeleton | ~3 files | Low | None |
| **R6/9b** — Move file I/O to resources | ~45 files | Low-Medium | R6/9a |
| **R7** — Investigate SaveData entity ownership | ~1-25 files | Low-Medium | R6/9a |
| **R6/9c** — Move gloader to resources | ~18 files | Low | R4 (Wave 4), R6/9b |
| **R6/9d** — Update documentation | ~3 files | None | R6/9b |

**Estimated effort:** 8-16 hours.

### Wave 6 — Final Cleanup

| Item | Scope | Risk | Depends on |
|------|-------|------|------------|
| **R3** — gloader path finalized | Resolved by R6/9c | None | Wave 5 |
| **R8** — LevelData adapter eliminated | Resolved by R4 | None | Wave 4 |

---

## Summary Table

| ID | Phase | Finding | Severity | Files | Depends On | Wave | Status |
|----|-------|---------|----------|-------|------------|------|--------|
| R1 | 2 | Stale ARCHITECTURE.md refs (`sim/` → `gameplay/`) | Minor | 1 | — | 1 | **DONE** (Wave 1) |
| R2 | 5 | hit_anim pragmatic deviation (document) | Minor | 1 | — | 1 | **DONE** (Wave 1) |
| R3 | 6 | gloader header path (defer to resources move) | Minor | 0 | R6 | 6 | **DONE** (resolved by R6/9c in Wave 5) |
| R4 | 7 | LevelData not deleted (~787 LOC shim, 72 consumers) | **Major** | ~80 | — | 2-4 | **DONE** (Waves 2-4) |
| R5 | 8 | No ILevelVisuals interface (document decision) | Moderate | 1 | — | 1 | **DONE** (Wave 1) |
| R6 | 9 | No resources module (og_resources) | **Major** | ~50 | R4 (partial) | 5 | **DONE** (Wave 5) |
| R7 | 9 | SaveData owns entity data (investigate) | **Major** | 1-25 | R6 | 5 | **DONE** (Wave 5 — Option A: boundary-only access confirmed) |
| R8 | 9 | LevelData thin adapter (duplicate of R4) | Major | 0 | R4 | 6 | **DONE** (resolved by R4 in Wave 4) |

**All 8 findings resolved.** Completed across 5 commits (Waves 1-5), with Wave 6 items resolved as dependencies of earlier waves.
