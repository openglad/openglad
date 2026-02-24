# Remaining Singletons / Global State Audit

Repo: `/home/ubuntu/openglad-desingle`  
Branch: `feat/desingletonize`  
Audit date: 2026-02-24

## Executive Summary

This was a fresh post-refactor audit using systematic `rg` sweeps (not manual-only browsing) across `src/`, `include/`, and test hooks in first-party code.

Primary scans used:
- `thread_local`, `extern`, file-scope `static`, and namespace-scope definitions
- known singleton/global accessors (`ctx()`, `set_global_context()`, `active_config()`, `g_frame_state()`)
- targeted re-check of items from:
  - `docs/audits/global-variables-audit.md`
  - `docs/plans/desingletonize-globals-plan.md`

### High-level result
- Refactor progress is substantial and real:
  - `g_frame_state` is no longer global (session-owned)
  - input state (`mouse_state`, `player_joy`, touch/control maps) moved to `GameSession::input_hw_`
  - large animation tables in `gloader.cpp` are now `const`
  - many picker/editor globals moved into `PickerState` / `LevelEditorState`
- But the strict goal is **not yet met**:
  - there are still multiple mutable globals and multiple `thread_local` variables beyond `current_session`
  - some “global-by-accessor” patterns remain (`ctx()` override path, `active_config()` wrappers)

## Remaining Thread-Local Variables

### Inventory

1. `og::runtime::current_session` (`thread_local GameSession*`)
- `src/sdl_client/runtime/game_session.cpp:36`
- `src/text_client/main.cpp:64` (headless target definition)
- Scope: namespace-scope `thread_local`
- Category: session state (intended)
- Blocks one-thread-local goal: `No` (this is the intended one)
- Thread-safety: per-thread pointer; safety depends on session lifetime discipline.

2. `g_reset_time_ptr` (`thread_local std::chrono::steady_clock::time_point*`)
- `src/core/util.cpp:46`
- Scope: namespace-scope `thread_local`
- Category: thread-local scratch / timer anchor
- Moved into GameSession?: partially (points at `GameSession::reset_time_`)
- Blocks one-thread-local goal: `Yes` (extra thread-local game-adjacent state)
- Thread-safety: pointer itself is per-thread; lifetime depends on session wiring/reset.

3. `s_test_context_override` (`static thread_local GameContext*`)
- `src/runtime/game_context.cpp:38`
- Scope: static file-scope `thread_local`
- Category: test-only override path
- Moved into GameSession?: no
- Blocks one-thread-local goal: `Yes`
- Thread-safety: per-thread override, but global mutation API (`set_global_context`) is process-visible.

4. `path_walker` (`thread_local walker*`)
- `src/entities/walker_pathing.cpp:31`
- Scope: anonymous-namespace `thread_local`
- Category: thread-local scratch
- Blocks one-thread-local game-state goal: `No` (scratch, not authoritative game state)
- Thread-safety: per-thread, intended.

5. `path_map` (`thread_local Map`)
- `src/entities/walker_pathing.cpp:106`
- Scope: anonymous-namespace `thread_local`
- Category: thread-local scratch
- Blocks goal: `No`
- Thread-safety: per-thread, intended.

6. `pather` (`thread_local micropather::MicroPather`)
- `src/entities/walker_pathing.cpp:107`
- Scope: anonymous-namespace `thread_local`
- Category: thread-local scratch
- Blocks goal: `No`
- Thread-safety: per-thread, intended.

7. `grass_rng` (`static thread_local std::mt19937`)
- `src/sdl_client/io/platform_io.cpp:432` (function-local static `thread_local`)
- Scope: function-local static `thread_local`
- Category: thread-local scratch
- Blocks one-thread-local game-state goal: `No` (not session state)
- Thread-safety: per-thread RNG state.

### Thread-local count assessment
- Distinct thread-local storages found in codebase: **7 unique names** (`8` definitions including SDL + text-client `current_session`).
- Strict “only one thread-local global (`current_session`)” goal is **not met**.
- If interpreted as “only one thread-local game-state global,” then blockers are primarily:
  - `g_reset_time_ptr`
  - `s_test_context_override`

## Remaining Mutable Globals by Category

### Session State (should be in `GameSession` but still standalone)

- `short end_of_file` — `src/sdl_client/ui/help.cpp:43` (namespace-scope)
- `char helptext[HELP_WIDTH][MAX_LINES]` — `src/sdl_client/ui/help.cpp:44` (namespace-scope)
- `short end_of_file` — `src/text_client/platform_headless.cpp:192` (namespace-scope)
- `Sint32 backgrounds[]` — `src/sdl_client/ui/level_editor.cpp:175` (namespace-scope)
- `std::vector<ObjectType> object_pane` — `src/sdl_client/ui/level_editor.cpp:250` (namespace-scope)

Thread-safety: generally main-thread UI/editor use only; unsynchronized.

### Renderer / Hardware (legitimately global)

- `std::unique_ptr<Screen> E_Screen` — `src/sdl_client/render/video.cpp:53` (extern/global)
- `SDL_Joystick* joysticks[MAX_NUM_JOYSTICKS]` — `src/sdl_client/input/input.cpp:72` (namespace-scope)
- `static PixieData letters1` — `src/sdl_client/render/text.cpp:27`
- `static PixieData letters_big` — `src/sdl_client/render/text.cpp:28`
- `static char text_buffer[255]` — `src/sdl_client/render/text.cpp:115`
- `static Uint32 colorMask` — `src/sdl_client/render/sai2x.cpp:18`
- `static Uint32 lowPixelMask` — `src/sdl_client/render/sai2x.cpp:19`
- `static Uint32 qcolorMask` — `src/sdl_client/render/sai2x.cpp:20`
- `static Uint32 qlowpixelMask` — `src/sdl_client/render/sai2x.cpp:21`
- `static Uint32 redblueMask` — `src/sdl_client/render/sai2x.cpp:22`
- `static Uint32 greenMask` — `src/sdl_client/render/sai2x.cpp:23`
- `static int PixelsPerMask` — `src/sdl_client/render/sai2x.cpp:24`
- `static int xsai_depth` — `src/sdl_client/render/sai2x.cpp:25`
- `static std::array<unsigned char*, 4> src_line` — `src/sdl_client/render/sai2x.cpp:27`
- `static std::array<unsigned char*, 2> dst_line` — `src/sdl_client/render/sai2x.cpp:28`
- `std::array<std::array<int, 3>, 256> pal` — `src/sdl_client/ui/intro.cpp:38`
- `std::array<unsigned char, 768> mypalette` — `src/sdl_client/ui/intro.cpp:39`

Thread-safety: mostly assumes single render thread / main thread.

### Process-Wide Config

- `cfg_store cfg` — `src/data/gparser.cpp:40` (extern/global config singleton)

Thread-safety: unsynchronized mutable process-global configuration.

### Immutable Data / Registries (mutable storage, init-once semantics)

- `static FamilyRegistryBase<FamilyDescriptor, NUM_FAMILIES> s_registry` — `src/entities/family_registry.cpp:47`
- `static FamilyRegistryBase<EffectFamilyDescriptor, NUM_EFFECT_FAMILIES> s_registry` — `src/entities/effect_family_registry.cpp:29`
- `static FamilyRegistryBase<TreasureFamilyDescriptor, NUM_TREASURE_FAMILIES> s_registry` — `src/entities/treasure_family_registry.cpp:31`
- `static FamilyRegistryBase<GeneratorFamilyDescriptor, NUM_GENERATOR_FAMILIES> s_registry` — `src/entities/generator_family_registry.cpp:17`
- `static FamilyRegistryBase<WeaponFamilyDescriptor, NUM_WEAPON_FAMILIES> s_registry` — `src/entities/weapon_family_registry.cpp:33`
- `std::unique_ptr<og::runtime::GameSession> g_session_owner` — `src/sdl_client/runtime/screen_lifecycle.cpp:16`
- `static auto g_app_start` — `src/core/util.cpp:40`
- `static auto s_fallback_reset_time` — `src/core/util.cpp:45`
- `alignas(GameSession) static char headless_session_buf[sizeof(GameSession)]` — `src/text_client/main.cpp:63`

Thread-safety: mostly benign if init order and one-time init discipline hold.

### Test-Only (`#ifdef TESTING`)

- `bool g_test_remove_exits` — `src/sdl_client/runtime/glad_gameplay.cpp:31`
- `int g_picker_mainmenu_calls` — `src/sdl_client/ui/picker.cpp:103`
- `int g_picker_max_mainmenu_calls` — `src/sdl_client/ui/picker.cpp:104`
- `std::atomic<bool> g_test_in_game` — `src/sdl_client/ui/picker.cpp:107`
- `std::atomic<int> g_test_game_epoch` — `src/sdl_client/ui/picker.cpp:110`
- `std::vector<bool> s_yes_or_no_overrides` — `src/sdl_client/ui/picker_dialogs.cpp:120`
- `bool s_force_real_dialogs` — `src/sdl_client/ui/picker_dialogs.cpp:121`
- `std::vector<TraceEntry> g_trace_buffer` — `src/test_trace.cpp:15`
- `std::mutex g_trace_mutex` — `src/test_trace.cpp:16`
- `std::int32_t g_test_level_tick_limit_override` — `src/runtime/sim_world.cpp:22`

Thread-safety: mixed; some atomic/mutex-protected, some plain test globals.

### Platform-Specific (`#ifdef __EMSCRIPTEN__`)

- `bool g_start_game_requested` — `src/sdl_client/ui/picker.cpp:97`
- `static GameState g_game_state` — `src/sdl_client/glad.cpp:80`
- `static bool g_state_initialized` — `src/sdl_client/glad.cpp:81`
- `static std::atomic<bool> idbfs_sync_done` — `src/sdl_client/io/platform_io.cpp:259`

Thread-safety: mostly event-loop state; `idbfs_sync_done` is atomic.

### UI Layout Descriptor Globals (currently mutable)

- `button mainmenu_buttons[]` — `src/sdl_client/ui/picker.cpp:416` (platform-specific definitions at 437/462/475)
- `button main_options_buttons[]` — `src/sdl_client/ui/picker.cpp:494`
- `button control_options_buttons[]` — `src/sdl_client/ui/picker.cpp:520`
- `button createmenu_buttons[]` — `src/sdl_client/ui/picker.cpp:544`
- `button viewteam_buttons[]` — `src/sdl_client/ui/picker.cpp:560`
- `button details_buttons[]` — `src/sdl_client/ui/picker.cpp:569`
- `button trainmenu_buttons[]` — `src/sdl_client/ui/picker.cpp:575`
- `button hiremenu_buttons[]` — `src/sdl_client/ui/picker.cpp:600`
- `button saveteam_buttons[]` — `src/sdl_client/ui/picker.cpp:611`
- `button loadteam_buttons[]` — `src/sdl_client/ui/picker.cpp:627`
- `button yes_or_no_buttons[]` — `src/sdl_client/ui/picker_dialogs.cpp:37`
- `button no_or_yes_buttons[]` — `src/sdl_client/ui/picker_dialogs.cpp:43`
- `button popup_dialog_buttons[]` — `src/sdl_client/ui/picker_dialogs.cpp:49`

Thread-safety: main-thread UI only; not synchronized.

## Globals That Were Supposed to Be Moved But Weren't

Based on `docs/plans/desingletonize-globals-plan.md` and current code:

1. Context override machinery was expected to be removed in Phase 1.
- Still present via `set_global_context()` + `s_test_context_override` (`src/runtime/game_context.cpp:38`, `:53`).
- `ctx()` still has global fallback singleton (`static GameContext s_fallback` at `:49`).

2. Phase 0e expected const-ification of picker/dialog button descriptor arrays where immutable.
- Arrays remain mutable `button ...[]` in `picker.cpp` and `picker_dialogs.cpp`.

3. “Only one thread-local global (`current_session`)” target not met.
- Additional thread-local storages remain (`g_reset_time_ptr`, `s_test_context_override`, pathing scratch trio, `grass_rng`).

4. Level-editor state migration is incomplete.
- `backgrounds[]` and `object_pane` remain namespace-scope globals rather than session/editor-owned state.

## New Globals Introduced by Refactoring

Compared to the previous audit and migration intent:

1. `g_reset_time_ptr` (`src/core/util.cpp:46`)
- New thread-local indirection introduced to wire timer APIs to `GameSession::reset_time_`.
- Replaces the old direct thread-local timer anchor; still global.

2. `s_test_context_override` (`src/runtime/game_context.cpp:38`)
- New test override replacing prior context-global strategy; keeps a thread-local context singleton path.

3. `headless_session_buf` (`src/text_client/main.cpp:63`)
- New headless buffer backing a fake global `current_session` in text client.

4. Macro-based indirection for moved input state:
- `#define mouse_state (og::runtime::current_session->input_hw_->mouse)`
- `#define player_joy (og::runtime::current_session->input_hw_->player_joy)`
- `include/openglad/input/input.h:215-216`

5. Global-access wrappers that hide singleton usage remain common:
- `active_config()` helpers returning `cfg` in multiple files
- `g_frame_state()` inline wrappers returning `current_session->frame_state_`
- `ctx()` global accessor with fallback/override behavior

## Assessment: How Close to "One Thread-Local Global"?

Status: **Not at goal yet**.

- If the criterion is strict literal global `thread_local` count: not close (7 unique thread-local storages, 8 definitions).
- If the criterion is “one thread-local game-state global”: closer, but still blocked by:
  - `g_reset_time_ptr`
  - `s_test_context_override`

Everything else thread-local is mostly scratch/per-thread helper state, but still violates strict literal interpretation.

## Remaining Work Needed

1. Remove `g_reset_time_ptr` indirection.
- Move timer calls behind session/context APIs or pass timer anchor explicitly.

2. Retire `set_global_context()` and `s_test_context_override`.
- Update tests to inject context through session activation only.
- Keep optional fallback, but avoid mutable global override pointer.

3. Complete editor/picker cleanup.
- Move `object_pane` and (if mutable) editor tables into `LevelEditorState`.
- Make static layout button arrays `const` where possible.

4. Normalize `cfg` access strategy.
- Keep process-global if required, but reduce hidden singleton wrappers (`active_config()`) and document ownership/lifecycle clearly.

5. Decide policy on thread-local scratch globals.
- Keep pathing and `grass_rng` as accepted exceptions, or convert to explicit per-session/per-call state if enforcing strict single-thread-local rule.

6. Unify headless session bootstrap.
- Replace `headless_session_buf` shim with explicit `GameSession` ownership lifecycle if feasible.

## Appendix: Full Remaining Mutable Global Index

Each entry: `name | type | file:line | scope | moved-to-session? | category | blocks current_session-only goal? | thread-safety`

- `current_session | GameSession* | src/sdl_client/runtime/game_session.cpp:36 | namespace thread_local | intended global | session state | no | per-thread pointer; lifetime-sensitive`
- `current_session | GameSession* | src/text_client/main.cpp:64 | namespace thread_local | intended global | session state | no | same as above`
- `primary_session | std::atomic<GameSession*> | src/sdl_client/runtime/game_session.cpp:37 | namespace | standalone | session bootstrap | no | atomic pointer only; pointee lifetime external`
- `primary_session | std::atomic<GameSession*> | src/text_client/main.cpp:65 | namespace | standalone | session bootstrap | no | same`
- `g_reset_time_ptr | thread_local std::chrono::steady_clock::time_point* | src/core/util.cpp:46 | namespace thread_local | partial | thread-local scratch | yes | per-thread pointer`
- `s_test_context_override | GameContext* | src/runtime/game_context.cpp:38 | static file thread_local | standalone | test-only/global accessor state | yes | per-thread pointer`
- `path_walker | walker* | src/entities/walker_pathing.cpp:31 | anonymous thread_local | standalone | thread-local scratch | no | per-thread`
- `path_map | Map | src/entities/walker_pathing.cpp:106 | anonymous thread_local | standalone | thread-local scratch | no | per-thread`
- `pather | micropather::MicroPather | src/entities/walker_pathing.cpp:107 | anonymous thread_local | standalone | thread-local scratch | no | per-thread`
- `grass_rng | std::mt19937 | src/sdl_client/io/platform_io.cpp:432 | function static thread_local | standalone | thread-local scratch | no | per-thread`
- `cfg | cfg_store | src/data/gparser.cpp:40 | namespace/global | standalone | process-wide config | no | unsynchronized mutable global`
- `E_Screen | std::unique_ptr<Screen> | src/sdl_client/render/video.cpp:53 | extern/global | standalone | renderer/hardware | no | main-thread renderer assumption`
- `joysticks | SDL_Joystick*[10] | src/sdl_client/input/input.cpp:72 | namespace/global | standalone | renderer/hardware | no | unsynchronized`
- `end_of_file | short | src/sdl_client/ui/help.cpp:43 | namespace/global | standalone | session/UI state | no | unsynchronized`
- `helptext | char[HELP_WIDTH][MAX_LINES] | src/sdl_client/ui/help.cpp:44 | namespace/global | standalone | session/UI state | no | unsynchronized`
- `end_of_file | short | src/text_client/platform_headless.cpp:192 | namespace/global | standalone | session/UI state | no | unsynchronized`
- `g_start_game_requested | bool | src/sdl_client/ui/picker.cpp:97 | namespace (`__EMSCRIPTEN__`) | standalone | platform-specific | no | single-thread loop assumption`
- `g_picker_mainmenu_calls | int | src/sdl_client/ui/picker.cpp:103 | namespace (`TESTING`) | standalone | test-only | no | unsynchronized`
- `g_picker_max_mainmenu_calls | int | src/sdl_client/ui/picker.cpp:104 | namespace (`TESTING`) | standalone | test-only | no | unsynchronized`
- `g_test_in_game | std::atomic<bool> | src/sdl_client/ui/picker.cpp:107 | namespace (`TESTING`) | standalone | test-only | no | atomic`
- `g_test_game_epoch | std::atomic<int> | src/sdl_client/ui/picker.cpp:110 | namespace (`TESTING`) | standalone | test-only | no | atomic`
- `mainmenu_buttons | button[] | src/sdl_client/ui/picker.cpp:416 | extern/global | standalone | UI layout | no | main-thread UI`
- `main_options_buttons | button[] | src/sdl_client/ui/picker.cpp:494 | namespace/global | standalone | UI layout | no | main-thread UI`
- `control_options_buttons | button[] | src/sdl_client/ui/picker.cpp:520 | namespace/global | standalone | UI layout | no | main-thread UI`
- `createmenu_buttons | button[] | src/sdl_client/ui/picker.cpp:544 | extern/global | standalone | UI layout | no | main-thread UI`
- `viewteam_buttons | button[] | src/sdl_client/ui/picker.cpp:560 | extern/global | standalone | UI layout | no | main-thread UI`
- `details_buttons | button[] | src/sdl_client/ui/picker.cpp:569 | extern/global | standalone | UI layout | no | main-thread UI`
- `trainmenu_buttons | button[] | src/sdl_client/ui/picker.cpp:575 | extern/global | standalone | UI layout | no | main-thread UI`
- `hiremenu_buttons | button[] | src/sdl_client/ui/picker.cpp:600 | extern/global | standalone | UI layout | no | main-thread UI`
- `saveteam_buttons | button[] | src/sdl_client/ui/picker.cpp:611 | extern/global | standalone | UI layout | no | main-thread UI`
- `loadteam_buttons | button[] | src/sdl_client/ui/picker.cpp:627 | extern/global | standalone | UI layout | no | main-thread UI`
- `yes_or_no_buttons | button[] | src/sdl_client/ui/picker_dialogs.cpp:37 | anonymous namespace | standalone | UI layout | no | main-thread UI`
- `no_or_yes_buttons | button[] | src/sdl_client/ui/picker_dialogs.cpp:43 | anonymous namespace | standalone | UI layout | no | main-thread UI`
- `popup_dialog_buttons | button[] | src/sdl_client/ui/picker_dialogs.cpp:49 | anonymous namespace | standalone | UI layout | no | main-thread UI`
- `s_yes_or_no_overrides | std::vector<bool> | src/sdl_client/ui/picker_dialogs.cpp:120 | anonymous namespace (`TESTING`) | standalone | test-only | no | unsynchronized`
- `s_force_real_dialogs | bool | src/sdl_client/ui/picker_dialogs.cpp:121 | anonymous namespace (`TESTING`) | standalone | test-only | no | unsynchronized`
- `g_test_remove_exits | bool | src/sdl_client/runtime/glad_gameplay.cpp:31 | namespace (`TESTING`) | standalone | test-only | no | unsynchronized`
- `g_game_state | GameState | src/sdl_client/glad.cpp:80 | static file (`__EMSCRIPTEN__`) | standalone | platform-specific | no | single event loop`
- `g_state_initialized | bool | src/sdl_client/glad.cpp:81 | static file (`__EMSCRIPTEN__`) | standalone | platform-specific | no | single event loop`
- `idbfs_sync_done | std::atomic<bool> | src/sdl_client/io/platform_io.cpp:259 | static file (`__EMSCRIPTEN__`) | standalone | platform-specific | no | atomic`
- `g_trace_buffer | std::vector<TraceEntry> | src/test_trace.cpp:15 | extern/global (`TESTING`) | standalone | test-only | no | mutex-protected`
- `g_trace_mutex | std::mutex | src/test_trace.cpp:16 | extern/global (`TESTING`) | standalone | test-only | no | synchronization primitive`
- `g_test_level_tick_limit_override | std::int32_t | src/runtime/sim_world.cpp:22 | namespace (`TESTING`) | standalone | test-only | no | unsynchronized`
- `pal | std::array<std::array<int,3>,256> | src/sdl_client/ui/intro.cpp:38 | namespace/global | standalone | renderer/UI state | no | unsynchronized`
- `mypalette | std::array<unsigned char,768> | src/sdl_client/ui/intro.cpp:39 | namespace/global | standalone | renderer/UI state | no | unsynchronized`
- `backgrounds | Sint32[] | src/sdl_client/ui/level_editor.cpp:175 | namespace/global | standalone | session/editor state | no | unsynchronized`
- `object_pane | std::vector<ObjectType> | src/sdl_client/ui/level_editor.cpp:250 | namespace/global | standalone | session/editor state | no | unsynchronized`
- `g_session_owner | std::unique_ptr<GameSession> | src/sdl_client/runtime/screen_lifecycle.cpp:16 | static file | standalone | lifecycle singleton | no | main-thread lifecycle`
- `s_registry | FamilyRegistryBase<...> | src/entities/family_registry.cpp:47 | static file | standalone | immutable registry | no | init-once expectation`
- `s_registry | FamilyRegistryBase<...> | src/entities/effect_family_registry.cpp:29 | static file | standalone | immutable registry | no | init-once expectation`
- `s_registry | FamilyRegistryBase<...> | src/entities/treasure_family_registry.cpp:31 | static file | standalone | immutable registry | no | init-once expectation`
- `s_registry | FamilyRegistryBase<...> | src/entities/generator_family_registry.cpp:17 | static file | standalone | immutable registry | no | init-once expectation`
- `s_registry | FamilyRegistryBase<...> | src/entities/weapon_family_registry.cpp:33 | static file | standalone | immutable registry | no | init-once expectation`
- `letters1 | PixieData | src/sdl_client/render/text.cpp:27 | static file | standalone | renderer cache | no | not synchronized`
- `letters_big | PixieData | src/sdl_client/render/text.cpp:28 | static file | standalone | renderer cache | no | not synchronized`
- `text_buffer | char[255] | src/sdl_client/render/text.cpp:115 | static file | standalone | renderer scratch | no | not synchronized`
- `colorMask | Uint32 | src/sdl_client/render/sai2x.cpp:18 | static file | standalone | renderer scratch | no | not synchronized`
- `lowPixelMask | Uint32 | src/sdl_client/render/sai2x.cpp:19 | static file | standalone | renderer scratch | no | not synchronized`
- `qcolorMask | Uint32 | src/sdl_client/render/sai2x.cpp:20 | static file | standalone | renderer scratch | no | not synchronized`
- `qlowpixelMask | Uint32 | src/sdl_client/render/sai2x.cpp:21 | static file | standalone | renderer scratch | no | not synchronized`
- `redblueMask | Uint32 | src/sdl_client/render/sai2x.cpp:22 | static file | standalone | renderer scratch | no | not synchronized`
- `greenMask | Uint32 | src/sdl_client/render/sai2x.cpp:23 | static file | standalone | renderer scratch | no | not synchronized`
- `PixelsPerMask | int | src/sdl_client/render/sai2x.cpp:24 | static file | standalone | renderer scratch | no | not synchronized`
- `xsai_depth | int | src/sdl_client/render/sai2x.cpp:25 | static file | standalone | renderer scratch | no | not synchronized`
- `src_line | std::array<unsigned char*,4> | src/sdl_client/render/sai2x.cpp:27 | static file | standalone | renderer scratch | no | not synchronized`
- `dst_line | std::array<unsigned char*,2> | src/sdl_client/render/sai2x.cpp:28 | static file | standalone | renderer scratch | no | not synchronized`
- `g_app_start | time_point (auto) | src/core/util.cpp:40 | static file | standalone | process timing anchor | no | effectively immutable`
- `s_fallback_reset_time | time_point (auto) | src/core/util.cpp:45 | static file | standalone | timer fallback singleton | no | unsynchronized fallback state`
- `headless_session_buf | char[sizeof(GameSession)] | src/text_client/main.cpp:63 | static file (namespace) | standalone | headless singleton shim | no | single-thread assumption`

