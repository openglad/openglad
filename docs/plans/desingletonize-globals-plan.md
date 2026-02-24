# Desingletonize Globals: Comprehensive Refactoring Plan

**Branch:** `feat/desingletonize`
**Date:** 2026-02-24
**Based on:** `docs/audits/global-variables-audit.md`

## Goal

Outside the renderer, there should be **exactly one thread-local global variable**: `current_session` (the `GameSession*`). Every other mutable game-simulation-related global becomes a member of `GameSession`, a member of something `GameSession` owns, or is proven truly process-global / immutable.

## Design Principles

1. **Incremental batches** — each batch compiles and passes `ctest --preset ci-test` before the next.
2. **Mechanical transforms first** — const-ify read-only data before moving mutable state.
3. **Minimal API churn** — prefer accessor helpers over touching every call-site when possible.
4. **Preserve the GameContext boundary** — `GameContext` stays as a sim-facing DI sub-object of `GameSession`; it is not absorbed.

## Classification Summary

| Category | Count | Disposition |
|----------|-------|-------------|
| Mutable globals → move to `GameSession` | ~45 | Phases 1–8 |
| Read-only-after-init → make `const` | ~100 | Phase 0 |
| Truly process-global (renderer, SDL devices) | ~8 | Stay global (justified) |
| Test-only (`#ifdef TESTING`) | ~10 | Stay as-is |
| Emscripten-only state machine | 2 | Stay as-is |
| File-local working buffers (text_buffer, src_line/dst_line) | 3 | Stay file-local |
| Immutable registries (lazy-init-once) | 5 | Stay as-is |
| Thread-local pathing state | 3 | Stay thread-local |

---

## Globals That Stay Global (With Justification)

These do **not** move into `GameSession`:

| Global | Location | Justification |
|--------|----------|---------------|
| `current_session` | `game_session.cpp:27` | The ONE allowed thread-local (project goal) |
| `primary_session` | `game_session.cpp:28` | Atomic bootstrap for child threads; paired with `current_session` |
| `E_Screen` | `video.cpp:53` | Process-wide SDL_Renderer; main-thread-only. Renderer-layer global. |
| `joysticks[10]` | `input.cpp:87` | SDL_Joystick device handles; process-wide hardware. Not session state. |
| `cfg` | `gparser.cpp:40` | Process-wide config. Accessed pre-session (io_init, commandline). See Phase 8 for access-pattern improvement. |
| `g_app_start` | `util.cpp:40` | Immutable after init (process start time). File-local static. |
| `g_session_owner` | `screen_lifecycle.cpp:16` | Owns the global session lifetime. File-local, anonymous namespace. |
| `s_registry` ×5 | `family_registry.cpp` etc. | Immutable type-metadata registries; init-once, never mutated. Not session state. |
| `path_walker`, `path_map`, `pather` | `walker_pathing.cpp` | Thread-local worker scratch state for A* pathfinding. Not logically session-owned — these are reusable computation resources. Each thread already gets its own copy. |
| `g_game_state`, `g_state_initialized` | `glad.cpp:80-81` | `#ifdef __EMSCRIPTEN__` only. Platform state machine. |
| `g_trace_buffer`, `g_trace_mutex` | `test_trace.cpp:15-16` | Test infrastructure (mutex-protected). |
| `idbfs_sync_done` | `platform_io.cpp:259` | `#ifdef __EMSCRIPTEN__` only. Platform sync flag. |
| All `#ifdef TESTING` globals | various | Test control knobs. See Phase 9 for inventory. |
| `letters1`, `letters_big` | `text.cpp:27-28` | File-local static font caches (render layer). |
| `text_buffer` | `text.cpp:115` | File-local static working buffer (render layer). |
| `src_line`, `dst_line` | `sai2x.cpp:27-28` | File-local static working buffers (render layer). |

---

## Phase 0: Const-Correctness Sweep

**Goal:** Mark ~100 read-only-after-init globals as `const` or `constexpr`, removing them from the "mutable global" count with zero behavioral change.

**Risk:** Very Low
**Testing:** Compile + full `ctest`. If anything fails, the variable was mutated somewhere we missed.

### Batch 0a: gloader.cpp Animation Tables (~91 variables)

All 91 animation frame arrays and pointer-indirection tables in `src/runtime/gloader.cpp` are initialized at file scope and **never written after initialization**. They are only accessed within gloader.cpp by the static function `animation_for_type()`.

**Action:**
- Add `const` to every `signed char NAME[]` → `const signed char NAME[]`
- Add `const` to every `signed char *NAME[]` → `const signed char * const NAME[]`
- Update `animation_for_type()` return type: `signed char**` → `const signed char * const *`
- Update `loader::animations` vector element type to match: `const signed char * const *`
- Update `walker::ani` pointer type in `include/openglad/entities/walker.h` if it stores the result

**Files changed:**
- `src/runtime/gloader.cpp` (91 variable declarations + `animation_for_type()` + `loader` class usage)
- `include/openglad/entities/walker.h` (if `ani` member type needs `const`)
- Any file that reads `walker::ani` and passes it to non-const APIs

**Variable list (all in gloader.cpp):**
`bit1`–`bit8`, `att1`–`att8`, `bitm2`/`bitm4`/`bitm6`/`bitm8`, `mageatt1`–`mageatt8`, `tele_out1`/`tele_in1`–`tele_in4`, `gs_down`/`gs_up`, `skel_grow`/`skel_shrink`, `slime_pulse`/`slime_split`/`small_slime`, `series_8`/`aniexpand8`/`series_16`/`ani16`, `bomb1`/`anibomb1`, `explosion1`/`aniexplosion1`, `hit1`/`hit2`/`hit3`/`anihit`, `cloud_cycle`/`anicloud`, `marker_cycle`/`animarker`, `animan`/`aniskel`/`animage`/`anigs`/`anislime`/`ani_small_slime`, `kni1`/`kni2`/`anikni`, `rock1`/`anirock`, `grow1`/`anitree`, `door1`/`door2`/`anidoor`, `dooropen1`/`dooropen2`/`anidooropen`, `arrow1`–`arrow8`/`aniarrow`, `blob1`/`aniblob1`, `none1`/`aninone`, `towerglow1`/`anitower`, `tent1`/`anitent`, `blood1`/`aniblood`, `glowgrow`/`glowpulse`/`aniglowgrow`, `food1`/`anifood`

### Batch 0b: view.cpp Default Key Arrays

`key1`, `key2`, `key3`, `key4` (SDL keycode arrays) and `normalkeys` (pointer array) in `src/sdl_client/render/view.cpp` are never mutated after initialization.

**Action:**
- `int key1[]` → `constexpr int key1[]` (and key2–key4)
- `int *normalkeys[]` → `constexpr const int * normalkeys[]`

**Files changed:**
- `src/sdl_client/render/view.cpp` (5 declarations)

### Batch 0c: difficulty_level Default Values

`difficulty_level[DIFFICULTY_SETTINGS]` in `src/ui/picker_common.cpp` is `{50, 100, 200}` and is only ever read (indexed by `current_session->current_difficulty_`).

**Action:**
- `std::int32_t difficulty_level[...]` → `const std::int32_t difficulty_level[...]`
- Update extern declarations in `living.cpp:40`, `walker.cpp:60`, `picker.cpp:144` to `extern const`
- Update `include/openglad/ui/picker_common.h` extern to `extern const`

**Files changed:**
- `src/ui/picker_common.cpp` (definition)
- `src/entities/walker.cpp` (extern)
- `src/entities/living.cpp` (extern)
- `src/sdl_client/ui/picker.cpp` (extern)
- `include/openglad/ui/picker_common.h` (extern)

### Batch 0d: sai2x Pixel Masks

The 8 mask variables (`colorMask`, `lowPixelMask`, `qcolorMask`, `qlowpixelMask`, `redblueMask`, `greenMask`, `PixelsPerMask`, `xsai_depth`) are file-local statics set once in `Init_2xSaI()` and then read-only.

**Action:** These cannot be `constexpr` because their values depend on runtime bit-depth detection. Leave as mutable file-local statics but add a comment documenting they are init-once. This is acceptable — they are file-local render internals.

**No files changed.**

### Batch 0e: picker Button Layout Arrays

The button arrays in `picker.cpp` (`mainmenu_buttons`, `createmenu_buttons`, `viewteam_buttons`, `details_buttons`, `trainmenu_buttons`, `hiremenu_buttons`, `saveteam_buttons`, `loadteam_buttons`) and in `picker_dialogs.cpp` (`yes_or_no_buttons`, `no_or_yes_buttons`, `popup_dialog_buttons`) are layout descriptors.

**Action:**
- Verify the `button` struct fields are not mutated at runtime (only used to initialize `vbutton` instances)
- If immutable: add `const` to all button array definitions
- Update extern declarations in `picker_sdl_defs.h` to `extern const`
- Also mark `main_options_buttons`, `control_options_buttons` as `const` if immutable

**Files changed:**
- `src/sdl_client/ui/picker.cpp` (~10 array definitions)
- `src/sdl_client/ui/picker_dialogs.cpp` (3 array definitions)
- `src/sdl_client/ui/picker_sdl_defs.h` (extern declarations)

**Risk:** Low-Medium. If any code mutates button struct fields at runtime, this will fail to compile and we'll skip those arrays.

### Batch 0f: help.cpp Lazy-Loaded Content

`classes_help_lines`, `editor_help_lines` in `help.cpp:326-327` are loaded once (`help_files_loaded` guard) and then read-only. These are file-local statics.

**Action:** Leave as-is. They are file-local statics with lazy init — acceptable as render/UI internals.

**No files changed.**

---

## Phase 1: Eliminate Global Context Machinery

**Goal:** Remove the `s_active_context` / `s_default_context` / `s_production_rng` globals from `game_context.cpp`. The free function `ctx()` becomes a simple dereference of `current_session->ctx_`.

**Risk:** Medium (touches 27 files that call `ctx()`)
**Testing:** Full `ctest`

### What changes

**Current pattern:**
```cpp
// game_context.cpp
static ProductionRandom s_production_rng;      // ELIMINATED
static GameContext s_default_context;            // ELIMINATED
static thread_local GameContext* s_active_context = nullptr;  // ELIMINATED

GameContext& ctx() {
    if (s_active_context) return *s_active_context;
    return s_default_context;
}
void set_global_context(GameContext* context) {
    s_active_context = context;
}
```

**New pattern:**
```cpp
// game_context.cpp (or inline in game_context.h)
GameContext& ctx() {
    return og::runtime::current_session->ctx_;
}
// set_global_context() REMOVED — no longer needed
```

### Migration steps

1. **Remove `set_global_context()` calls:**
   - `src/sdl_client/runtime/game_session.cpp:54` (constructor) — remove, `ctx_` is already a member
   - `src/sdl_client/runtime/game_session.cpp:111` (destructor) — remove
   - `src/sdl_client/runtime/game_session.cpp:143` (SessionScope ctor) — remove, switching `current_session` is sufficient since `ctx()` reads from it
   - `src/sdl_client/runtime/game_session.cpp:165-168` (SessionScope dtor) — remove

2. **Update `ctx()` implementation** in `src/runtime/game_context.cpp`:
   - Replace body with `return og::runtime::current_session->ctx_;`
   - Add `#include <openglad/runtime/game_session.h>`
   - Remove `s_active_context`, `s_default_context`, `s_production_rng` statics

3. **Remove `set_global_context()` declaration** from `include/openglad/runtime/game_context.h`

4. **Handle pre-session `ctx()` calls:** Search for any `ctx()` calls that happen before a `GameSession` exists. If found, guard with `if (current_session)` or restructure init order. Key risk areas:
   - `src/io/platform_io_common.cpp` — uses `ctx().mounted_campaign`. This runs during `io_init()` which may precede session creation. **Solution:** `ctx()` returns a static default context if `current_session == nullptr`:
     ```cpp
     GameContext& ctx() {
         if (og::runtime::current_session)
             return og::runtime::current_session->ctx_;
         static GameContext s_fallback;
         return s_fallback;
     }
     ```

5. **Remove `saved_context_` from `SessionScope`** — it's no longer needed since context follows `current_session`.

6. **Update tests** that call `set_global_context()` directly:
   - `tests/test_game_context.cpp` — update to test via session activation instead
   - `tests/unit/test_session_raii.cpp` — update if it tests context switching

**Files changed:**
- `src/runtime/game_context.cpp` (rewrite ctx(), remove 3 statics, remove set_global_context)
- `include/openglad/runtime/game_context.h` (remove set_global_context declaration)
- `src/sdl_client/runtime/game_session.cpp` (remove set_global_context calls, simplify SessionScope)
- `include/openglad/runtime/game_session.h` (remove saved_context_ from SessionScope)
- `tests/test_game_context.cpp` (update tests)
- `tests/unit/test_session_raii.cpp` (update tests)

**Globals eliminated:** 3 (`s_active_context`, `s_default_context`, `s_production_rng`)

---

## Phase 2: Fold `g_frame_state` Into Session

**Goal:** Remove the extern `g_frame_state` global. `GameSession` already has `frame_state_` member (line 110 of game_session.h).

**Risk:** Low
**Testing:** Full `ctest`

### What changes

**Current:** `g_frame_state` defined in `glad_gameplay.cpp:24`, extern in `glad.cpp:118`.
**New:** All access becomes `current_session->frame_state_` (or `og::runtime::current_session->frame_state_` in non-session code).

**Files changed:**
- `src/sdl_client/runtime/glad_gameplay.cpp` — remove `GameLoopFrameState g_frame_state{};` definition, change all `g_frame_state.` to `og::runtime::current_session->frame_state_.`
- `src/sdl_client/glad.cpp` — remove `extern GameLoopFrameState g_frame_state;`, change all `g_frame_state.` to `og::runtime::current_session->frame_state_.`

**Access pattern change:**
```cpp
// Before:
g_frame_state.done = false;
// After:
og::runtime::current_session->frame_state_.done = false;
```

**Globals eliminated:** 1 (`g_frame_state`)

---

## Phase 3: Move Remaining Input State Into Session

**Goal:** Move `mouse_state`, `player_joy`, touch state, control modes, and key maps into `GameSession`.

**Risk:** Medium (touches input.cpp heavily + input_event_bridge.cpp)
**Testing:** Full `ctest`, manual input testing recommended

### Sub-struct: InputHardwareState

Add to `GameSession` (or a new header `include/openglad/runtime/input_hardware_state.h`):

```cpp
struct InputHardwareState {
    MouseState mouse{};
    JoyData player_joy[4]{};
    int player_control_modes[4]{};
    int player_mode_keys[4][2][16]{};  // [player][mode][key]
    Sint32 mouse_buttons{0};

    // Touch input (compiled in on all platforms, gated by USE_TOUCH_INPUT at call sites)
    bool tapping{false};
    int start_tap_x{0}, start_tap_y{0};
    bool moving{false};
    int moving_touch_x{0}, moving_touch_y{0};
    int moving_touch_target_x{0}, moving_touch_target_y{0};
    SDL_FingerID movingTouch{0};
    bool firing{false};
    SDL_FingerID firingTouch{0};
    bool touch_keystate[4][16]{};
};
```

Add member to `GameSession`:
```cpp
InputHardwareState input_hw_;
```

### Migration

**Step 1:** Create `InputHardwareState` struct in `include/openglad/runtime/game_session.h` (or a new header included by it). Add `input_hw_` member.

**Step 2:** In `src/sdl_client/input/input.cpp`:
- Remove all ~19 namespace-scope global variable definitions
- Add accessor at top of file:
  ```cpp
  static InputHardwareState& hw() {
      return og::runtime::current_session->input_hw_;
  }
  ```
- Replace all bare references: `mouse_state` → `hw().mouse`, `player_joy[i]` → `hw().player_joy[i]`, etc.

**Step 3:** In `include/openglad/input/input.h`:
- Remove `extern MouseState mouse_state;` and `extern JoyData player_joy[4];`
- Add inline accessors:
  ```cpp
  inline MouseState& mouse_state() {
      return og::runtime::current_session->input_hw_.mouse;
  }
  inline JoyData* player_joy() {
      return og::runtime::current_session->input_hw_.player_joy;
  }
  ```
- Or: keep the extern names but as inline functions returning references. All existing code using `mouse_state.x` would need to become `mouse_state().x` — moderate churn.

**Alternative (lower churn):** Use macros temporarily:
```cpp
#define mouse_state (og::runtime::current_session->input_hw_.mouse)
#define player_joy  (og::runtime::current_session->input_hw_.player_joy)
```
This preserves all existing access patterns with zero call-site changes. Macros can be replaced with inline functions in a follow-up.

**Step 4:** In `src/sdl_client/runtime/input_event_bridge.cpp`:
- Remove extern declarations for `moving`, `moving_touch_x`, etc.
- Access via `og::runtime::current_session->input_hw_.moving` (or the macro)

**Step 5:** Leave `joysticks[MAX_NUM_JOYSTICKS]` as a file-local global in `input.cpp` — these are SDL device handles, truly process-global.

**Files changed:**
- `include/openglad/runtime/game_session.h` (add InputHardwareState struct + member)
- `src/sdl_client/input/input.cpp` (remove ~19 globals, add accessor, update all references)
- `include/openglad/input/input.h` (change extern declarations to accessors/macros)
- `src/sdl_client/runtime/input_event_bridge.cpp` (remove extern declarations, use session access)
- Any other files that reference `mouse_state` or `player_joy` directly

**Globals eliminated:** ~19

---

## Phase 4: Move Entity-Layer Globals

**Goal:** Move `guy_id_counter` and `changedteam` into `GameSession`.

**Risk:** Low
**Testing:** Full `ctest`

### 4a: guy_id_counter

**Current:** `static int guy_id_counter = 0;` in `src/entities/guy.cpp:37`, incremented at lines 69, 113.

**Action:** Add `int guy_id_counter_ = 0;` to `GameSession`. In `guy.cpp`, access via `og::runtime::current_session->guy_id_counter_++`.

**Files changed:**
- `include/openglad/runtime/game_session.h` (add member)
- `src/entities/guy.cpp` (remove static, change 2 increment sites)

### 4b: changedteam

**Current:** `static short changedteam[6]` in `src/sdl_client/runtime/cheat_handler.cpp:22`.

**Action:** Add `short changedteam_[6] = {};` to `GameSession`. Update `cheat_handler.cpp` to use `og::runtime::current_session->changedteam_[i]`.

**Files changed:**
- `include/openglad/runtime/game_session.h` (add member)
- `src/sdl_client/runtime/cheat_handler.cpp` (remove static, update accesses)

**Globals eliminated:** 2

---

## Phase 5: Move `allkeys` Into Session

**Goal:** Move the mutable `allkeys[4][16]` array (per-session key bindings loaded from prefs) from `view.cpp` into `GameSession`.

**Risk:** Low
**Testing:** Full `ctest`

**Current:** `int allkeys[4][16]` in `src/sdl_client/render/view.cpp:143`. Populated from preferences in `options` constructor, read by input handling.

**Action:**
- Add `int allkeys_[4][16] = {};` to `GameSession`
- In `view.cpp`, replace `allkeys` references with `og::runtime::current_session->allkeys_`
- In `options` constructor (view.cpp), populate `current_session->allkeys_` from prefs
- The const `key1`–`key4` / `normalkeys` arrays (default keybindings) stay as file-local constexpr (from Phase 0b)

**Files changed:**
- `include/openglad/runtime/game_session.h` (add member)
- `src/sdl_client/render/view.cpp` (remove `allkeys` global, update all references)

**Globals eliminated:** 1

---

## Phase 6: Move `g_reset_time` Into Session

**Goal:** Move the per-thread timer anchor into `GameSession`.

**Risk:** Low
**Testing:** Full `ctest`

**Current:** `static thread_local auto g_reset_time = ...` in `src/core/util.cpp:41`.

**Action:**
- Add `std::chrono::steady_clock::time_point reset_time_ = std::chrono::steady_clock::now();` to `GameSession`
- In `util.cpp`, change `reset_time()` / `query_timer()` to read from `og::runtime::current_session->reset_time_`
- Guard with `if (!og::runtime::current_session)` fallback to a static for pre-session calls

**Files changed:**
- `include/openglad/runtime/game_session.h` (add member)
- `src/core/util.cpp` (remove thread_local, update accessor functions)

**Globals eliminated:** 1 thread-local

---

## Phase 7: Move Picker / UI State Into Session Sub-Objects

**Goal:** Move picker menu state and level editor state into session-owned sub-structs.

**Risk:** Medium-High (large number of variables, many files)
**Testing:** Full `ctest` + manual UI testing recommended

### 7a: PickerState Sub-Object

Create `PickerState` struct to hold picker-specific mutable state:

```cpp
struct PickerState {
    // Loaded assets
    PixieData backpics[5]{};
    std::array<std::unique_ptr<pixieN>, 5> backdrops{};
    PixieData main_title_logo_data{};
    PixieData main_columns_data{};
    std::unique_ptr<pixieN> main_title_logo_pix;
    std::unique_ptr<pixieN> main_columns_pix;

    // Team build state
    guy* old_guy = nullptr;

    // Menu navigation
    bool menu_nav_enabled = false;
    Uint32 menu_nav_enabled_time = 0;

    // Intercept state
    PickerInterceptScope intercept_scope = PickerInterceptScope::None;
    const og::ui::PickerMenuItem* selected_menu_item = nullptr;

    // Team build sessions (non-owning pointers, valid only during hire/train flows)
    og::ui::HireSession* hire_session = nullptr;
    og::ui::TrainSession* train_session = nullptr;
};
```

Add `PickerState picker_;` to `GameSession`.

**Access pattern change:**
```cpp
// Before:
backdrops[i] = ...;
old_guy = current_guy;
// After:
current_session->picker_.backdrops[i] = ...;
current_session->picker_.old_guy = current_guy;
```

**Files changed:**
- `include/openglad/runtime/game_session.h` (add PickerState + member)
- `src/sdl_client/ui/picker.cpp` (remove ~15 globals, update all references)
- `src/sdl_client/ui/picker_team_build.cpp` (remove g_hire_session, g_train_session, extern old_guy; update accesses)
- `src/sdl_client/ui/picker_input.cpp` (remove menu_nav globals, update accesses)
- `src/sdl_client/ui/picker_main_menu.cpp` (update extern refs for main_title_logo_pix, main_columns_pix)
- `src/sdl_client/ui/button.cpp` (update extern ref for backdrops)

### 7b: LevelEditorState Sub-Object

Create `LevelEditorState` struct:

```cpp
struct LevelEditorState {
    unsigned char scenpalette[768]{};
    Sint32 redraw{0};
    Sint32 campaignchanged{0};
    Sint32 levelchanged{0};
    Sint32 cyclemode{0};
    Sint32 start_time_s{0};
    Sint32 backgrounds[...]{};  // copy the initializer
    std::vector<ObjectType> object_pane;
    Sint32 rowsdown{0};
    Sint32 maxrows{0};

    // Mouse state
    int mouse_up_button{0};
    int mouse_motion_x{0}, mouse_motion_y{0};
    int mouse_last_x{0}, mouse_last_y{0};

    // Pan flags
    bool pan_left{false}, pan_right{false};
    bool pan_up{false}, pan_down{false};
};
```

Add `LevelEditorState editor_;` to `GameSession`.

**Files changed:**
- `include/openglad/runtime/game_session.h` (add LevelEditorState + member)
- `src/sdl_client/ui/level_editor.cpp` (remove ~20 globals, update all references)
- `src/sdl_client/ui/level_editor_tools.cpp` (update extern for `redraw`)

### 7c: HelpState (file-local)

`end_of_file`, `helptext`, `classes_help_lines`, `editor_help_lines`, `help_files_loaded` in `help.cpp` are file-local statics used only within the help subsystem.

**Action:** Leave as file-local statics. They are UI render internals, not simulation state. They don't affect multi-session correctness because the help viewer is main-thread-only and presents the same content regardless of session.

**No changes needed.**

### 7d: Button Management (owned_buttons, dumbcount)

`owned_buttons` in `button.cpp:35` (anonymous namespace) and `dumbcount` at line 37 are file-local button management internals.

**Action for `owned_buttons`:** The `allbuttons_` array is already in `GameSession` (Batch 5 migration). `owned_buttons` is the backing storage. Move it to `GameSession` alongside `allbuttons_`:
```cpp
std::array<std::unique_ptr<vbutton>, kMaxButtons> owned_buttons_{};
```

**Action for `dumbcount`:** Investigate whether it's dead code. If dead, remove. If active, move to `GameSession`.

**Action for `scen_level` extern:** Remove the stale `extern short scen_level` declaration in `button.cpp:29`.

**Files changed:**
- `include/openglad/runtime/game_session.h` (add owned_buttons_ member)
- `src/sdl_client/ui/button.cpp` (remove owned_buttons, dumbcount, scen_level extern)

**Globals eliminated (Phase 7 total):** ~40

---

## Phase 8: cfg Access Pattern Improvement

**Goal:** `cfg` stays as a process-global, but formalize access and reduce coupling.

**Risk:** Low
**Testing:** Full `ctest`

### Rationale for keeping cfg global

`cfg` is populated before any `GameSession` exists (`commandline()` → `load_settings()` in `main()`). It represents process-wide configuration (video mode, sound volume, paths) that doesn't vary per-session. Making it per-session would require duplicating config parsing into session init, with no benefit.

### Improvements

1. **Add `const cfg_store&` accessor to `GameSession`:**
   ```cpp
   // game_session.h
   const cfg_store& config() const;

   // game_session.cpp
   const cfg_store& GameSession::config() const { return ::cfg; }
   ```
   New code should prefer `current_session->config()` over bare `cfg`. This makes the dependency explicit and allows future per-session config overrides.

2. **Remove scattered `extern cfg_store cfg` declarations:**
   - `src/text_client/main.cpp:69` — use the header
   - `src/text_client/text_protocol.cpp:29` — use the header
   These should include `<openglad/data/gparser.h>` instead of redeclaring.

3. **Mark `cfg` access as `const` where possible:** Most code only reads cfg. Add `const` qualifiers to reader functions in `cfg_store`.

**Files changed:**
- `include/openglad/runtime/game_session.h` (add config() accessor)
- `src/sdl_client/runtime/game_session.cpp` (implement config())
- `src/text_client/main.cpp` (remove local extern, use header)
- `src/text_client/text_protocol.cpp` (remove local extern, use header)

**Globals eliminated:** 0 (cfg stays global, but access is formalized)

---

## Phase 9: Test-Only Globals Inventory

**Goal:** Document and organize test-only globals. These are acceptable as globals because they're compiled out of production builds.

**No code changes unless a variable is found to be unused.**

### Inventory

| Variable | Location | Purpose |
|----------|----------|---------|
| `g_picker_mainmenu_calls` | `picker.cpp:110` | Counter for main menu loop iterations |
| `g_picker_max_mainmenu_calls` | `picker.cpp:111` | Limit for main menu iterations |
| `g_test_in_game` | `picker.cpp:114` | Atomic: signals if game is running |
| `g_test_game_epoch` | `picker.cpp:117` | Atomic: monotonic game start/finish counter |
| `g_test_remove_exits` | `glad_gameplay.cpp:28` | Flag: remove level exits in tests |
| `g_test_level_tick_limit_override` | `sim_world.cpp:22` | Override mission timeout |
| `s_yes_or_no_overrides` | `picker_dialogs.cpp:120` | Queue of dialog answers for testing |
| `s_force_real_dialogs` | `picker_dialogs.cpp:121` | Force real dialogs in tests |
| `g_trace_buffer` | `test_trace.cpp:15` | Trace log buffer |
| `g_trace_mutex` | `test_trace.cpp:16` | Trace log synchronization |

**Disposition:** All stay as-is. They are test infrastructure, compiled out of release builds, and have well-defined ownership (the test harness).

---

## Phase 10: Final Cleanup and Verification

**Goal:** Verify the target state is achieved and clean up loose ends.

### Steps

1. **Re-run the global variables audit** using the same methodology as `docs/audits/global-variables-audit.md`. Verify:
   - Only `current_session` and `primary_session` remain as non-render, non-test, non-const thread-local/extern mutable globals
   - All other mutable state is either:
     - A member of `GameSession` (directly or via sub-objects)
     - File-local to a render module
     - `#ifdef TESTING` / `#ifdef __EMSCRIPTEN__` guarded
     - Truly process-global (cfg, joysticks, E_Screen)

2. **Remove stale extern declarations:**
   - `extern short scen_level` in `button.cpp:29` (no definition exists)

3. **Update `docs/audits/global-variables-audit.md`** with the post-migration state.

4. **Update `docs/ARCHITECTURE.md`** to document the session-centric state model.

---

## Dependency Graph

```
Phase 0 (const sweep)          ← Independent, do first
    ↓
Phase 1 (eliminate ctx globals) ← Unlocks cleaner session access
    ↓
Phase 2 (g_frame_state)        ← Independent of Phase 1 but cleaner after
Phase 3 (input globals)        ← Independent
Phase 4 (entity globals)       ← Independent
Phase 5 (allkeys)              ← Independent
Phase 6 (g_reset_time)         ← Independent
    ↓
Phase 7 (UI state)             ← Largest batch, can be done after 0-6
    ↓
Phase 8 (cfg access)           ← Independent, can happen anytime
Phase 9 (test inventory)       ← Documentation only
Phase 10 (verification)        ← Must be last
```

Phases 2–6 are independent of each other and can be done in any order (or in parallel by different workers). Phase 7 is the largest and benefits from Phases 0-1 being done first.

---

## Summary: Before vs After

### Before (current state)
- 7 thread-local globals
- 31 extern mutable globals
- 152 namespace-scope mutable globals
- 24 static file-scope mutable globals
- **Total mutable non-const globals: ~214**

### After (target state)
- **1 thread-local global:** `current_session` (+ `primary_session` atomic)
- **3 thread-local worker state:** `path_walker`, `path_map`, `pather` (justified, not session state)
- **~3 process-global:** `cfg`, `E_Screen`, `joysticks` (justified)
- **~100 variables const-ified** (Phase 0)
- **~65 variables moved to GameSession** (Phases 1-7)
- **~10 test-only globals** (acceptable, compiled out)
- **~5 emscripten/platform globals** (acceptable)
- **~20 file-local render internals** (acceptable, renderer-layer)
- **~5 immutable registries** (acceptable, init-once)

### New GameSession Members (approximate)

```
GameSession
├── ctx_                    (GameContext — existing)
├── frame_state_            (GameLoopFrameState — existing, Phase 2 removes extern)
├── input_hw_               (InputHardwareState — Phase 3)
├── guy_id_counter_         (int — Phase 4)
├── changedteam_[6]         (short — Phase 4)
├── allkeys_[4][16]         (int — Phase 5)
├── reset_time_             (chrono timepoint — Phase 6)
├── picker_                 (PickerState — Phase 7a)
├── editor_                 (LevelEditorState — Phase 7b)
├── owned_buttons_          (unique_ptr array — Phase 7d)
├── (existing members: myscreen_, theprefs_, player_keys_, viewport, palette, etc.)
└── config()                (const accessor to global cfg — Phase 8)
```
