# Phase 12: Enforce Dependencies and Clean Up

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 11](phase-11.md)
**Key types:** [Target Architecture](target-architecture.md)

---

## Goal

Build-time enforcement of component dependency rules. Clean up all
remaining global state identified in the singletons audit.

## Steps

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

## Remaining Globals Cleanup (inlined from `remaining-singletons-audit.md`)

### Thread-locals to Resolve

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

### Session State Still Standalone

9. Migrate UI globals to interface-layer owned state:
   - `helptext` (`char[HELP_WIDTH][MAX_LINES]`), `end_of_file` (`short`) in
     `help.cpp:43-44` → interface component state
   - `backgrounds[]` (`Sint32[]`) in `level_editor.cpp:175`, `object_pane`
     (`std::vector<ObjectType>`) in `level_editor.cpp:250` → editor state

### UI Layout Globals

10. 13 button descriptor arrays in `picker.cpp` (lines 416–627) and
    `picker_dialogs.cpp` (lines 37–49) → const-ify where possible. These are
    `button[]` structs used for menu layout. Some have mutable fields
    updated at runtime; those stay mutable but move to interface component state.

### Renderer/Hardware Globals (accepted — process-level)

11. These are legitimate process globals and stay as-is. Document them:
    - `E_Screen` (`std::unique_ptr<Screen>`, `video.cpp:53`)
    - `joysticks` (`SDL_Joystick*[MAX_NUM_JOYSTICKS]`, `input.cpp:72`)
    - `letters1`, `letters_big` (`PixieData`, `text.cpp:27-28`)
    - `text_buffer` (`char[255]`, `text.cpp:115`)
    - sai2x color masks and line buffers (`sai2x.cpp:18-28`)
    - `pal`, `mypalette` (`intro.cpp:38-39`)

### Process Config

12. `cfg` (`cfg_store`, `gparser.cpp:40`) — keep as process global. Reduce
    `active_config()` wrapper indirection where it adds no value.

### Immutable Registries (no action needed)

13. Family registries (`s_registry` in `family_registry.cpp`,
    `effect_family_registry.cpp`, `treasure_family_registry.cpp`,
    `generator_family_registry.cpp`, `weapon_family_registry.cpp`) — init-once
    static data. Acceptable as-is.

### Test-only and Platform-specific Globals (no action needed)

14. All `#ifdef TESTING` globals (`g_test_remove_exits`, `g_picker_mainmenu_calls`,
    `g_picker_max_mainmenu_calls`, `g_test_in_game`, `g_test_game_epoch`,
    `s_yes_or_no_overrides`, `s_force_real_dialogs`, `g_trace_buffer`,
    `g_trace_mutex`, `g_test_level_tick_limit_override`) and `#ifdef __EMSCRIPTEN__`
    globals (`g_start_game_requested`, `g_game_state`, `g_state_initialized`,
    `idbfs_sync_done`) — acceptable, no migration needed.

15. Update `docs/ARCHITECTURE.md` with the new component model.

## Final Audit Checklist (replaces re-run step)

16. Verify final state:
    - `current_game` is the only thread-local in gameplay
    - `current_session` is the only thread-local in platform (game-state
      category)
    - `grass_rng` is an accepted rendering scratch exception
    - All other globals are: const/init-once, `#ifdef TESTING`-only,
      `#ifdef __EMSCRIPTEN__`-only, or documented process globals
      (`cfg`, `E_Screen`, `joysticks`, family registries)

**Risk:** Low — enforcement catches violations at compile time.
