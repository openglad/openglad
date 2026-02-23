# Codebase Size Reduction Analysis

**Date:** 2026-02-23
**Codebase:** OpenGlad (commit `851e885`, branch `master`)
**Scope:** All source code (`src/`, `include/`), tests (`tests/`), build infrastructure, and assets. Excludes `third_party/`.

---

## Executive Summary

The OpenGlad codebase contains approximately **108,000 lines** of non-third-party code (source + headers + tests). This analysis identifies **~4,800–6,200 lines** of concrete reduction opportunities across source code, tests, and infrastructure, broken into 30 specific findings.

| Category | Estimated Lines Saveable | Risk |
|----------|------------------------:|------|
| Code Duplication | ~800–1,000 | Low–Medium |
| Code Reuse Opportunities | ~500–700 | Low–Medium |
| Dead Code | ~180–220 | Low |
| Over-Engineering | ~600–800 | Low–Medium |
| Redundant Assets | ~38 KB / 13 files | Low |
| Build/Infrastructure | ~300–400 | Low |
| Test Consolidation | ~2,500–3,100 | Medium |
| **Total** | **~4,800–6,200** | |

---

## Findings (Prioritized by Impact)

---

### FINDING 1: Table-Driven Data in gloader.cpp

**Impact: ~350 lines saveable | Risk: Medium**

**File:** `src/runtime/gloader.cpp:377–720` (995 lines total)

**Issue:** The `loader` constructor contains 369 repetitive assignment lines setting properties (graphics, hitpoints, act_types, animations, stepsizes, lineofsight, damage, fire_frequency) for every entity family. Each property is a separate statement like:

```cpp
graphics[PIX(Order::Living, FAMILY_SOLDIER)] = read_pixie_file("footman.pix");
hitpoints[PIX(Order::Living, FAMILY_SOLDIER)] = 10;
act_types[PIX(Order::Living, FAMILY_SOLDIER)] = ACT_RANDOM;
// ... repeated for 21 living families, 20 weapon families, 12 treasures, 4 generators
```

**Proposed Solution:** Replace with a static data table (array of structs):

```cpp
struct EntityDef {
    Order order; int family; const char* pix_file;
    float hp; char act_type; const AnimData* anim;
    float stepsize; int los; float damage; float fire_freq;
};
static constexpr EntityDef entity_defs[] = {
    {Order::Living, FAMILY_SOLDIER, "footman.pix", 10, ACT_RANDOM, &animan, ...},
    // ...
};
for (auto& e : entity_defs) { /* single loop sets all properties */ }
```

**Lines:** Current 369 repetitive lines → ~80 lines (table + loop)
**Savings:** ~290 lines
**Risk:** Medium — requires careful mapping of all current values; gore toggle needs special handling (lines 485–494)

---

### FINDING 2: Versioned Test File Proliferation

**Impact: ~2,500–3,100 lines saveable | Risk: Medium**

**Files:** 34 versioned test files (`*_r11.cpp` through `*_r21.cpp`) plus 9 `*_coverage_push.cpp` files plus 5 `test_coverage_r16–r20.cpp` files.

| Group | File Count | Total Lines |
|-------|----------:|------------:|
| `*_r1x` / `*_r2x` versioned tests | 34 | 6,902 |
| `*_coverage_push` tests | 9 | 975 |
| `test_coverage_r16–r20` | 5 | 1,954 |
| **Total** | **48** | **9,831** |

**Issue:** Tests are organized by "coverage round" rather than by feature. For example, the cleric family alone has 5 test files: `test_family_cleric_coverage_push.cpp`, `test_family_cleric_r11.cpp`, `test_family_cleric_r12.cpp`, `test_family_cleric_r14.cpp`, `test_family_cleric_r15.cpp`. Each round adds tests incrementally rather than consolidating into a single test file per feature.

**Proposed Solution:** Consolidate each feature's tests into a single file. For example, merge all `test_family_cleric_*.cpp` (5 files, 846 lines) into one `test_family_cleric.cpp`. Similarly for walker, stats, smooth, level_data, etc. Conservatively, ~30% of lines are duplicated fixture/boilerplate.

**Savings:** ~2,500–3,100 lines (consolidation removes duplicate includes, fixtures, and redundant setup)
**Risk:** Medium — must verify no test is accidentally dropped; use test counts before/after

---

### FINDING 3: Entity Family level_up() Duplication

**Impact: ~140 lines saveable | Risk: Low**

**Files:** 12 family files in `src/entities/families/`:
- `family_archer.cpp:120–134`, `family_archmage.cpp`, `family_barbarian.cpp:19–35`,
  `family_big_orc.cpp`, `family_cleric.cpp`, `family_druid.cpp`, `family_elf.cpp`,
  `family_faerie.cpp`, `family_fire_elemental.cpp:43–57`, `family_ghost.cpp`,
  `family_mage.cpp`, `family_orc.cpp:133–149`, `family_skeleton.cpp`,
  `family_thief.cpp:65–80`

**Issue:** All 12 functions are identical in structure (15 lines each):

```cpp
static void {family}_level_up(guy* self, std::int32_t level_diff) {
    std::int32_t s = 8 * level_diff;   // base strength
    std::int32_t d = 6 * level_diff;   // base dexterity
    std::int32_t c = 8 * level_diff;   // base constitution
    std::int32_t it = 8 * level_diff;  // base intelligence
    std::int32_t a = 1 * level_diff;   // base armor
    // family-specific multiplier adjustments (1–2 lines differ)
    self->strength = static_cast<short>(static_cast<std::int32_t>(self->strength) + s);
    self->dexterity = static_cast<short>(...);
    // ... 5 stat assignments
}
```

Only the 1–2 lines of modifier adjustments differ per family (e.g., `s /= 2; d = (d * 3) / 2;`).

**Proposed Solution:** Create a data-driven helper:

```cpp
struct LevelUpMods { float str, dex, con, intel, armor; };
void apply_level_up(guy* self, int32_t level_diff, const LevelUpMods& m);
```

Store per-family modifiers in the family descriptor. Each family's level_up becomes a 1-line call.

**Savings:** ~140 lines (12 × 15 lines → 12 × 1 line + 15 line helper)
**Risk:** Low — pure data extraction, behavior identical

---

### FINDING 4: Difficulty Scaling Duplication

**Impact: ~50 lines saveable | Risk: Low**

**Files:** At least 7 family files have near-identical `set_difficulty()` functions:
- `family_archer.cpp:110–118`, `family_cleric.cpp:68–76`, `family_mage.cpp:81–89`,
  `family_druid.cpp:29–37`, `family_soldier.cpp:175–184`, `family_orc.cpp`,
  `family_barbarian.cpp`

**Issue:** Each follows the same formula with different constants:

```cpp
const float levmult = static_cast<float>(level) * static_cast<float>(level);
const float level_f = static_cast<float>(level);
self->stats()->max_hitpoints   += HP_MULT * levmult;
self->stats()->max_magicpoints += MP_MULT * levmult;
self->damage += DMG_MULT * level_f;
self->stats()->armor += ARMOR_MULT * levmult;
```

**Proposed Solution:** Store difficulty scaling coefficients in the family descriptor struct.

**Savings:** ~50 lines
**Risk:** Low — pure data extraction

---

### FINDING 5: check_special_ai() Exact Duplicates

**Impact: ~48 lines saveable | Risk: Low**

**Files:**
- `family_archer.cpp:74–86`
- `family_fire_elemental.cpp:20–32`
- `family_ghost.cpp:18–30`
- `family_orc.cpp:109–121`

**Issue:** Four functions are structurally identical, differing only in the distance threshold constant (130 for most):

```cpp
static bool {family}_check_special_ai(living* self) {
    if (self->foe) {
        uint32_t distance = static_cast<uint32_t>(self->distance_to_ob(self->foe));
        return (distance < THRESHOLD);
    }
    self->foe = self->sim_level->find_near_foe(self);
    if (!self->foe) return false;
    uint32_t distance = static_cast<uint32_t>(self->distance_to_ob(self->foe));
    return (distance < THRESHOLD);
}
```

**Proposed Solution:** Single parameterized helper: `check_special_ai_distance(living* self, uint32_t threshold)`.

**Savings:** ~48 lines (4 × 13 → 13 + 4 × 1)
**Risk:** Low

---

### FINDING 6: MenuNav Factory Method Bloat

**Impact: ~84 lines saveable | Risk: Low**

**Files:**
- `include/openglad/input/button.h:59–75` (17 declarations)
- `src/sdl_client/ui/button.cpp:43–109` (67 lines of implementations)

**Issue:** A simple 4-field struct (`up`, `down`, `left`, `right`) has 17 static factory methods for every combination of directions:

```cpp
static MenuNav Up(int up);
static MenuNav Down(int down);
static MenuNav UpDown(int up, int down);
static MenuNav UpDownLeft(int up, int down, int left);
// ... 13 more
```

Each is a one-liner returning `MenuNav(up, -1, -1, -1)` with appropriate slots filled.

**Proposed Solution:** Remove all factories. Use aggregate initialization directly: `MenuNav{5, -1, -1, -1}` or C++20 designated initializers `MenuNav{.up=5}`.

**Savings:** ~84 lines
**Risk:** Low — requires updating call sites (search for `MenuNav::`)

---

### FINDING 7: ButtonAction Enum Legacy Wrappers

**Impact: ~103 lines saveable | Risk: Low**

**File:** `include/openglad/input/button.h:203–320`

**Issue:** Three layers of redundancy for button action IDs:
1. `enum class ButtonAction : Sint32 { BeginMenu = 1, ... }` — the enum itself (correct)
2. `button_action_from_id()` — a 52-case switch that just returns `static_cast<ButtonAction>(id)` (redundant)
3. 50+ `#define BEGINMENU button_action_id(ButtonAction::BeginMenu)` aliases (legacy)

**Proposed Solution:**
- Replace `button_action_from_id()` with `static_cast<ButtonAction>(id)` at call sites
- Replace `#define` aliases with `inline constexpr` or update call sites to use enum directly

**Savings:** ~103 lines
**Risk:** Low — mechanical replacement

---

### FINDING 8: Duplicate I/O Error Wrapper Functions

**Impact: ~72 lines saveable | Risk: Low**

**Files:**
- `include/openglad/platform/io_common.h:50–113`
- `src/io/platform_io_common.cpp`, `src/sdl_client/io/platform_io.cpp`, `src/text_client/platform_headless.cpp`

**Issue:** Every I/O operation has two variants: one returning `bool` and one returning an error enum. The bool variant is always a trivial wrapper:

```cpp
bool mount_campaign_package(const std::string& id) {
    return mount_campaign_package_with_error(id) == CampaignPackageIoError::None;
}
```

8 pairs identified: `mount_campaign_package`, `unmount_campaign_package`, `remount_campaign_package`, `zip_contents`, `unzip_into`, `create_new_map_pix`, `create_new_pix`, `create_new_campaign_descriptor`.

**Proposed Solution:** Keep only the `_with_error()` variant. Callers check `== ErrorType::None` directly.

**Savings:** ~72 lines (8 × 3 declaration + 8 × 6 implementation)
**Risk:** Low — callers must update, but it's a mechanical change

---

### FINDING 9: Dead Code in walker.cpp::fire_check()

**Impact: ~37 lines removable | Risk: Low**

**File:** `src/entities/walker.cpp`

**Issue 9a:** Lines 1051–1069 (19 lines) — **unreachable code** after `return 0;` at line 1049. This is dead code that can never execute.

**Issue 9b:** Lines 1009–1026 (18 lines) — large commented-out code block with historical questions about wall collision logic.

**Proposed Solution:** Delete both blocks.

**Savings:** 37 lines
**Risk:** Low — 9a is provably dead; 9b is already commented out

---

### FINDING 10: Commented-Out Code Blocks Throughout Codebase

**Impact: ~170 lines removable | Risk: Low**

**Locations:**
| File | Lines | Content |
|------|------:|---------|
| `src/runtime/save_data.cpp:137,554` | 18 | Commented variable declarations and blocks |
| `src/entities/walker.cpp:178,193,204,219,792,1130,1504` | 35 | Various commented code/variables |
| `src/entities/guy.cpp:298–315` | 18 | Large commented block |
| `src/entities/living.cpp:400,527,651` | 6 | Commented variable declarations |
| `src/sdl_client/render/video.cpp:135–149` | 15 | Commented fullscreen code |
| `src/sdl_client/render/pal32.cpp:52–65,96–110` | 29 | Commented I/O and buffer code |
| `src/sdl_client/render/sai2x.cpp` (multiple blocks) | 87 | Commented matrix/color code |
| `src/runtime/gloader.cpp:152–159` | 8 | Commented block |
| `src/runtime/stats.cpp:403–414,860–864` | 17 | Commented variables and blocks |
| `src/entities/weap.cpp:166` | 6 | Commented block |
| Misc single-line commented code | ~20 | Scattered across files |

**Proposed Solution:** Delete all commented-out code. It's preserved in git history.

**Savings:** ~170 lines
**Risk:** Low

---

### FINDING 11: smooth.cpp Repetitive Terrain Switch Logic

**Impact: ~200 lines saveable | Risk: Medium**

**File:** `src/runtime/smooth.cpp:200–884` (686 lines in `smoother::smooth(Sint32 x, Sint32 y)`)

**Issue:** The `smooth()` function is a massive switch statement over terrain types (TYPE_GRASS, TYPE_GRASS_DARK, TYPE_WALL, etc.) with highly repetitive patterns. Each case:
1. Checks neighbor terrain types
2. Calls `rng(N)` to pick a random variant
3. Assigns the appropriate PIX_* constant

The random-variant-selection pattern repeats dozens of times:
```cpp
switch (rng(4)) {
    case 0: newvalue = PIX_GRASS1; break;
    case 1: newvalue = PIX_GRASS2; break;
    case 2: newvalue = PIX_GRASS3; break;
    case 3: newvalue = PIX_GRASS4; break;
}
```

Additionally, `query_genre_x_y()` (lines 61–178) is a 118-line switch mapping PIX_* constants to TYPE_* constants, which could be a lookup table.

**Proposed Solution:**
1. Replace `query_genre_x_y()` switch with a `constexpr` lookup array: `PIX_to_genre[PIX_MAX]`
2. Replace random variant selection with table: `const int grass_variants[] = {PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4};` → `newvalue = grass_variants[rng(4)];`

**Savings:** ~200 lines (conservative — the switch structure is deep and interleaved)
**Risk:** Medium — terrain smoothing is visible to players; needs visual regression testing

---

### FINDING 12: Text Formatting Boilerplate

**Impact: ~50 lines saveable | Risk: Low**

**File:** `src/sdl_client/render/text.cpp:116–216`

**Issue:** Five text rendering functions repeat identical `va_start/vsnprintf/va_end` boilerplate:
- `write_xy()` (lines 116–134)
- `write_xy_shadow()` (lines 136–155)
- `write_xy_center()` (lines 157–175)
- `write_xy_center_alpha()` (lines 177–195)
- `write_xy_center_shadow()` (lines 197–216)

Each has:
```cpp
char text_buffer[256];
va_list lst;
va_start(lst, formatted_string);
vsnprintf(text_buffer, 255, formatted_string, lst);
va_end(lst);
```

**Proposed Solution:** Extract into a private helper or use `std::format`.

**Savings:** ~50 lines
**Risk:** Low

---

### FINDING 13: Potion Notification Duplication

**Impact: ~20 lines saveable | Risk: Low**

**File:** `src/entities/families/treasure_family_consumables.cpp:39–97`

**Issue:** Five potion `on_eat` functions repeat:
```cpp
if (eater->user != -1) {
    std::string message = std::format("Potion of {Name}({})!", self->stats()->level);
    og::sim::emit_notification(self->sim_events, message);
}
self->dead = 1;
```

**Proposed Solution:** Helper: `notify_potion_consume(walker* eater, treasure* self, string_view name)`.

**Savings:** ~20 lines
**Risk:** Low

---

### FINDING 14: Entity Name Resolution Duplication

**Impact: ~24 lines saveable | Risk: Low**

**Files:** `family_cleric.cpp:127–135`, `family_thief.cpp:136–142,188–198`, `family_orc.cpp:91–96`

**Issue:** Repeated 6-line pattern:
```cpp
if (self->myguy)
    message = std::format("{} ...", self->myguy->name);
else if (self->stats()->name.size())
    message = std::format("{} ...", self->stats()->name);
else
    message = "FALLBACK";
```

**Proposed Solution:** Helper: `std::string_view entity_display_name(walker* w)`.

**Savings:** ~24 lines
**Risk:** Low

---

### FINDING 15: Walker Pathfinding Macro Duplication

**Impact: 6 lines | Risk: Low**

**Files:**
- `src/entities/walker_pathing.cpp:25–30`
- `src/sdl_client/render/walker_draw.cpp:26–28`

**Issue:** `MAP_WIDTH`, `GET_STATE_X`, `GET_STATE_Y` macros are duplicated verbatim (acknowledged in comment: "Duplicated from walker_pathing.cpp").

**Proposed Solution:** Move to shared header.

**Savings:** 6 lines
**Risk:** Low

---

### FINDING 16: Legacy OuyaController Code

**Impact: ~73 lines + header removable | Risk: Low**

**Files:**
- `include/openglad/legacy/OuyaController.h` (73 lines)
- Referenced in `src/sdl_client/ui/results_screen.cpp:33,540–543`
- Referenced in `src/sdl_client/ui/level_editor.cpp:49,3302–3357`

**Issue:** The OUYA console has been discontinued since 2019. This is a complete controller abstraction class (`OuyaController` + `OuyaControllerManager`) that wraps SDL joystick input. Modern SDL2 gamepad API handles this natively.

**Proposed Solution:** Replace OUYA-specific code with standard SDL2 gamepad API calls. Remove `OuyaController.h` entirely.

**Savings:** ~73 lines header + ~60 lines usage code = ~133 lines
**Risk:** Low — OUYA is defunct; SDL2 gamepad API is more capable

---

### FINDING 17: Family Registry Initialization Boilerplate

**Impact: ~200 lines saveable | Risk: Medium**

**Files:**
- `src/entities/family_registry.cpp:45–93` (48 lines of defaults)
- `src/entities/weapon_family_registry.cpp:30–54` (25 lines)
- `src/entities/effect_family_registry.cpp:30–46` (17 lines)
- `src/entities/treasure_family_registry.cpp:31–46` (16 lines)
- `src/entities/generator_family_registry.cpp:17–64` (48 lines)

**Issue:** Five registries follow an identical pattern: static array + initialized flag + init function + getter with lazy-init check. The initialization boilerplate (setting default values for each field) is 60–70% identical across registries.

**Proposed Solution:** Create a template `FamilyRegistry<Descriptor, NUM>` class with common init/get logic. Each registry specialization only provides the default descriptor and the per-family overrides.

**Savings:** ~200 lines
**Risk:** Medium — template refactoring requires careful testing

---

### FINDING 18: Small Registry Header Consolidation

**Impact: 4 files → 1 file | Risk: Low**

**Files:**
- `include/openglad/entities/weapon_family_registry.h` (18 lines)
- `include/openglad/entities/effect_family_registry.h` (18 lines)
- `include/openglad/entities/treasure_family_registry.h` (14 lines)
- `include/openglad/entities/generator_family_registry.h` (14 lines)

**Issue:** Each header declares just 2–3 functions. They could be in a single `family_registries.h`.

**Proposed Solution:** Merge into one header.

**Savings:** 3 files eliminated, ~30 lines of boilerplate (#pragma once, includes)
**Risk:** Low

---

### FINDING 19: Input Wrapper Functions

**Impact: ~130 lines saveable | Risk: Medium**

**File:** `include/openglad/input/input.h:195–280`

**Issue:** ~30 wrapper functions that simply forward to JoyData state or keystates arrays without adding validation or logic:
```cpp
bool playerHasJoystick(int player_num);  // just checks JoyData array
bool isPlayerHoldingKey(int player_index, int key_enum);  // reads keystates
```

**Proposed Solution:** Expose the underlying data structures with a cleaner API, or inline these wrappers.

**Savings:** ~130 lines (declarations + implementations)
**Risk:** Medium — many call sites

---

### FINDING 20: Unreferenced Pixel Art Files

**Impact: 13 files / 38 KB | Risk: Low**

**Files in `pix/`:**
| File | Size |
|------|-----:|
| `16brick2.pix` | 259 B |
| `empty2.pix` | 2,403 B |
| `noblood.pix` | 1,027 B |
| `skelbig.pix` | 16,387 B |
| `skelsmal.pix` | 4,099 B |
| `test2.pix` | 2,403 B |
| `tomwalls.pix` | 1,603 B |
| `tower4b.pix` | 8,703 B |
| `tutor00.pix` – `tutor04.pix` | 403 B each |

**Issue:** These 13 `.pix` files are not referenced in any source file, scenario file, or campaign file. They appear to be leftover assets from development/testing.

**Proposed Solution:** Remove after confirming they aren't loaded dynamically (checked — `gloader.cpp` uses hardcoded filenames, not dynamic scanning).

**Savings:** 13 files, ~38 KB
**Risk:** Low — verify no scenario file references them

---

### FINDING 21: Graph.h Legacy Umbrella Header

**Impact: 36 lines removable | Risk: Low**

**File:** `include/openglad/legacy/graph.h`

**Issue:** Per CLAUDE.md, this header has "an empty allowlist" and should not be included. It exists only as a legacy reference containing 12 `#include` directives. No source file includes it (confirmed by grep). The file only appears in comments referencing its constants.

**Proposed Solution:** Delete the file. Update comments that reference "graph.h" to point to the actual header locations.

**Savings:** 36 lines + 1 file
**Risk:** Low — already unused

---

### FINDING 22: Entity Summon/Create Boilerplate

**Impact: ~30 lines saveable | Risk: Medium**

**Files:** `family_cleric.cpp:158–163`, `family_druid.cpp:73–77`, `family_mage.cpp:154–170`, `family_soldier.cpp:47–54`

**Issue:** After `sim_level->add_ob()`, the same property setup repeats:
```cpp
walker* newob = self->sim_level->add_ob(Order::Weapon, FAMILY_XXX);
if (!newob) return false;
newob->owner = self;
newob->team_num = self->team_num;
newob->center_on(self);
newob->stats()->level = self->stats()->level;
```

**Proposed Solution:** Helper: `walker* summon_entity(living* summoner, Order order, int family)`.

**Savings:** ~30 lines
**Risk:** Medium — some summons need custom setup after the common part

---

### FINDING 23: Build Script Duplication

**Impact: ~80 lines saveable | Risk: Low**

**Files:** `scripts/build_test.sh`, `scripts/build_native.sh`, `scripts/build_coverage.sh`, `scripts/build_web.sh`

**Issue:** All 4 scripts share identical boilerplate: PATH setup, SCRIPT_DIR/PROJECT_ROOT computation, and dependency checking (`pkg-config --exists sdl2 SDL2_mixer`).

**Proposed Solution:** Extract common functions into `scripts/build_common.sh` and source it.

**Savings:** ~80 lines
**Risk:** Low

---

### FINDING 24: CMakeLists.txt Test Source List Duplication

**Impact: ~70 duplicate entries | Risk: Low**

**File:** `CMakeLists.txt:1008–1255`

**Issue:** Three test source lists overlap significantly:
- `TEST_SOURCES`: 145 files (full integration suite)
- `DATA_TEST_SOURCES`: 13 files (100% subset of TEST_SOURCES)
- `RUNTIME_TEST_SOURCES`: 80 files (~87% subset)

70 files appear in multiple lists.

**Proposed Solution:** Use CMake `list(FILTER)` to derive specialized lists from the master TEST_SOURCES list, or tag tests with properties.

**Savings:** ~70 duplicate list entries
**Risk:** Low

---

### FINDING 25: GameContext / Incomplete Global Migration

**Impact: ~100 lines of transitional code | Risk: High**

**Files:**
- `include/openglad/runtime/game_context.h:42–71`
- `include/openglad/runtime/game_session.h:20–67`

**Issue:** The GameContext is explicitly documented as "Phase 1: thin wrapper around globals." It adds indirection without removing the underlying globals (`myscreen`, `theprefs`, `cfg`). This is architectural debt — the wrapper exists alongside the globals it was meant to replace.

**Proposed Solution:** Either complete the migration (remove globals, pass context explicitly) or remove the wrapper and use globals directly. Current state is worst-of-both-worlds.

**Savings:** ~100 lines if wrapper removed; much more if migration completed
**Risk:** High — architectural change affecting many files

---

### FINDING 26: Redundant Initialization Guard Flags

**Impact: 5 variables + ~25 lines | Risk: Low**

**Files:** 5 registry files each have `static bool s_registry_initialized`

**Issue:** Every `get_*_descriptor()` call checks a boolean flag. These registries could be initialized once at startup.

**Proposed Solution:** Call all `init_*_registry()` at startup, remove lazy-init guards.

**Savings:** ~25 lines, eliminates branch per lookup
**Risk:** Low

---

### FINDING 27: Excessive Getter/Accessor Methods

**Impact: ~40 lines saveable | Risk: Low**

**Files:** `include/openglad/entities/walker.h:64–142`, `include/openglad/runtime/game_context.h:62–69`

**Issue:** Simple pass-through accessors that add no validation or logic:
```cpp
short query_frame() const { return frame; }
screen* active_screen() const { return game_screen; }
options* active_prefs() const { return prefs; }
```

**Proposed Solution:** Make the underlying members public (they already are in practice through the accessor).

**Savings:** ~40 lines
**Risk:** Low

---

### FINDING 28: GPL License Header Overhead

**Impact: ~1,556 lines (informational) | Risk: N/A**

**Issue:** The 16-line GPL header appears in every source file (~97 files), totaling ~1,556 lines. This is legally required and **should not be removed**, but it's noted for completeness.

**Action:** None — required by GPL v2. Could use SPDX short-form identifiers to reduce to 2 lines per file, but this is a project policy decision.

---

### FINDING 29: Foe-Finding Iteration Pattern

**Impact: ~30 lines saveable | Risk: Medium**

**Files:** `family_thief.cpp:38–44,121–135,152–156`, `family_mage.cpp:36–44,257–270`, `family_soldier.cpp:75–93`

**Issue:** 6+ instances of the same find-foes-in-range-and-iterate pattern:
```cpp
int32_t howmany = 0;
auto newlist = self->sim_level->find_foes_in_range(..., &howmany, self);
if (howmany < threshold) return false;
for (auto* w : newlist) { if (w) { /* process */ } }
```

**Proposed Solution:** Template helper: `for_each_foe_in_range(living*, range, callback)`.

**Savings:** ~30 lines
**Risk:** Medium — callback pattern may obscure control flow

---

### FINDING 30: Asset Staging Copies in Build

**Impact: Disk space only (~500 MB across 8+ build dirs) | Risk: Low**

**File:** `CMakeLists.txt:781–788`

**Issue:** `stage_runtime_assets` target copies entire `builtin/`, `cfg/`, `pix/`, `sound/` directories to every build directory. With 8+ preset configurations, identical assets are duplicated 8+ times.

**Proposed Solution:** Use symbolic links on Linux/macOS, or stage to a shared location.

**Savings:** ~500 MB disk space
**Risk:** Low — only affects build, not functionality

---

## Summary Table

| # | Finding | Files | Est. Lines Saved | Risk |
|--:|---------|------:|------------------:|------|
| 1 | Table-driven gloader.cpp | 1 | 290 | Medium |
| 2 | Test file consolidation | 48 | 2,500–3,100 | Medium |
| 3 | level_up() deduplication | 12 | 140 | Low |
| 4 | Difficulty scaling dedup | 7 | 50 | Low |
| 5 | check_special_ai() dedup | 4 | 48 | Low |
| 6 | MenuNav factory methods | 2 | 84 | Low |
| 7 | ButtonAction legacy wrappers | 1 | 103 | Low |
| 8 | I/O error wrapper pairs | 4+ | 72 | Low |
| 9 | Dead code in fire_check() | 1 | 37 | Low |
| 10 | Commented-out code blocks | 15+ | 170 | Low |
| 11 | smooth.cpp terrain logic | 1 | 200 | Medium |
| 12 | Text formatting boilerplate | 1 | 50 | Low |
| 13 | Potion notification dedup | 1 | 20 | Low |
| 14 | Entity name resolution | 3 | 24 | Low |
| 15 | Pathfinding macro dedup | 2 | 6 | Low |
| 16 | OuyaController removal | 3 | 133 | Low |
| 17 | Registry init boilerplate | 5 | 200 | Medium |
| 18 | Small header consolidation | 4 | 30 | Low |
| 19 | Input wrapper functions | 1+ | 130 | Medium |
| 20 | Unreferenced pix files | 13 files | 38 KB | Low |
| 21 | graph.h removal | 1 | 36 | Low |
| 22 | Entity summon boilerplate | 4 | 30 | Medium |
| 23 | Build script dedup | 4 | 80 | Low |
| 24 | CMake test list dedup | 1 | 70 entries | Low |
| 25 | GameContext transitional | 2 | 100 | High |
| 26 | Registry init guards | 5 | 25 | Low |
| 27 | Excessive accessors | 2+ | 40 | Low |
| 28 | GPL headers (info only) | ~97 | 1,556 (keep) | N/A |
| 29 | Foe-finding pattern | 3 | 30 | Medium |
| 30 | Asset staging copies | 1 | 500 MB disk | Low |

---

## Recommended Execution Order

### Phase 1: Low-Hanging Fruit (Low Risk, Immediate Value)

1. **Finding 9:** Remove dead code in `walker.cpp::fire_check()` — 37 lines
2. **Finding 10:** Remove all commented-out code blocks — 170 lines
3. **Finding 21:** Delete `graph.h` — 36 lines
4. **Finding 15:** Consolidate pathfinding macros — 6 lines
5. **Finding 20:** Remove unreferenced pix files — 13 files
6. **Finding 6:** Remove MenuNav factory methods — 84 lines
7. **Finding 7:** Remove ButtonAction legacy wrappers — 103 lines
8. **Finding 16:** Remove OuyaController — 133 lines

**Subtotal: ~570 lines, very low risk**

### Phase 2: Data-Driven Refactoring (Low–Medium Risk)

9. **Finding 3:** Consolidate level_up() functions — 140 lines
10. **Finding 4:** Consolidate difficulty scaling — 50 lines
11. **Finding 5:** Consolidate check_special_ai() — 48 lines
12. **Finding 8:** Remove I/O error wrapper pairs — 72 lines
13. **Finding 12:** Consolidate text formatting — 50 lines
14. **Finding 13:** Consolidate potion notifications — 20 lines
15. **Finding 14:** Extract entity name helper — 24 lines
16. **Finding 23:** Consolidate build scripts — 80 lines

**Subtotal: ~484 lines, low-medium risk**

### Phase 3: Structural Refactoring (Medium Risk)

17. **Finding 1:** Table-driven gloader.cpp — 290 lines
18. **Finding 11:** Refactor smooth.cpp terrain logic — 200 lines
19. **Finding 17:** Template-ify family registries — 200 lines
20. **Finding 19:** Simplify input wrappers — 130 lines
21. **Finding 2:** Consolidate versioned test files — 2,500–3,100 lines

**Subtotal: ~3,320–3,920 lines, medium risk**

### Phase 4: Architectural (High Risk, Long-term)

22. **Finding 25:** Resolve GameContext transitional architecture — 100+ lines

---

*Total achievable reduction (Phases 1–3): ~4,370–4,970 lines of source code + 13 asset files + 500 MB disk space savings.*
