# Global Variables Audit

Repo: `/home/ubuntu/openglad-desingle`  
Branch: `feat/desingletonize`  
Audit date: 2026-02-24

## Executive Summary

This audit scanned first-party C/C++ source in `src/` and `include/` (excluding `third_party/`) using `rg` pattern sweeps for:
- `extern` declarations and matching definitions
- `thread_local`
- file/namespace-scope declarations in `.cpp`
- `static` globals and static class members
- global `const`/`constexpr`
- macro-style global access patterns

### Counts (first-party source)
- Thread-local globals: **7**
- Extern mutable globals (declared/used across files): **31**
- Namespace-scope mutable globals (non-`static`, incl. test-only): **152**
- Static file-scope mutable globals: **24**
- Static class members: **18** (all constant, no mutable static data members found)
- Global constants (`const`/`constexpr`): **large set** (clustered in `include/openglad/core/constants.h`, `include/openglad/legacy/*.h`, plus file-local constant tables)
- Macro-based global access still present: **limited**, mostly compile-time constants and legacy compatibility macros

Overall: de-singletonization has reduced legacy global state significantly in gameplay/session state, but mutable globals remain concentrated in:
- input/UI glue (`src/sdl_client/input`, `src/sdl_client/ui`)
- animation data tables (`src/runtime/gloader.cpp`)
- rendering internals (`src/sdl_client/render`)

## Thread-Local Variables

| Variable | Type | Definition | Scope | Mutable | Use files | Notes | Thread safety |
|---|---|---|---|---|---|---|---|
| `og::runtime::current_session` | `GameSession*` | `src/sdl_client/runtime/game_session.cpp:27` | namespace-scope `thread_local` | Yes | `include/openglad/runtime/game_session.h`, many `src/sdl_client/*`, `src/entities/*` | Active session pointer for legacy access path | Per-thread pointer; safe by isolation, but pointed object lifetime must outlive thread use |
| `og::runtime::current_session` | `GameSession*` | `src/text_client/main.cpp:64` | namespace-scope `thread_local` | Yes | `src/text_client/*` | Headless text-client symbol definition | Same as above |
| `s_active_context` | `GameContext*` | `src/runtime/game_context.cpp:36` | static file-scope `thread_local` | Yes | `src/runtime/game_context.cpp` | Per-thread context override for `ctx()` | Per-thread pointer, no lock required |
| `g_reset_time` | deduced chrono timepoint | `src/core/util.cpp:41` | static file-scope `thread_local` | Yes | `src/core/util.cpp` | Thread-local timer reset anchor | Per-thread |
| `path_walker` | `walker*` | `src/entities/walker_pathing.cpp:31` | anonymous-namespace `thread_local` | Yes | `src/entities/walker_pathing.cpp` | Current walker for path callback state | Per-thread state; avoids cross-thread contamination |
| `path_map` | `Map` | `src/entities/walker_pathing.cpp:106` | anonymous-namespace `thread_local` | Yes | `src/entities/walker_pathing.cpp` | Per-thread graph object | Per-thread |
| `pather` | `micropather::MicroPather` | `src/entities/walker_pathing.cpp:107` | anonymous-namespace `thread_local` | Yes | `src/entities/walker_pathing.cpp` | Per-thread path solver instance | Per-thread |

## Extern Mutable Globals

| Variable | Type | Definition | Extern declaration(s) | Use files | Thread safety |
|---|---|---|---|---|---|
| `cfg` | `cfg_store` | `src/data/gparser.cpp:40` | `include/openglad/data/gparser.h:38`; also local `extern` in `src/text_client/main.cpp:69`, `src/text_client/text_protocol.cpp:29` | `src/data/gparser.cpp`, `src/runtime/gloader.cpp`, `src/sdl_client/*`, `src/text_client/*` | Not synchronized; effectively process-global config singleton |
| `g_trace_buffer` | `std::vector<TraceEntry>` | `src/test_trace.cpp:15` | `include/openglad/legacy/test_trace.h:16` | `include/openglad/legacy/test_trace.h`, `src/test_trace.cpp` | Protected by `g_trace_mutex` in all observed accessors |
| `g_trace_mutex` | `std::mutex` | `src/test_trace.cpp:16` | `include/openglad/legacy/test_trace.h:17` | `include/openglad/legacy/test_trace.h`, `src/test_trace.cpp` | Synchronization primitive |
| `current_session` | `thread_local GameSession*` | `src/sdl_client/runtime/game_session.cpp:27`, `src/text_client/main.cpp:64` | `include/openglad/runtime/game_session.h:136` | Broad use across runtime/render/ui/entity code | Thread-local; pointer lifetime/ownership remains risk area |
| `primary_session` | `std::atomic<GameSession*>` | `src/sdl_client/runtime/game_session.cpp:28`, `src/text_client/main.cpp:65` | `include/openglad/runtime/game_session.h:141` | `include/openglad/runtime/game_session.h`, `src/sdl_client/runtime/game_session.cpp`, `src/text_client/main.cpp` | Atomic pointer handoff; object lifetime still external |
| `player_joy` | `JoyData[4]` | `src/sdl_client/input/input.cpp:83` | `include/openglad/input/input.h:197` | `include/openglad/input/input.h`, `src/sdl_client/input/input.cpp` | No lock; expected main-thread input ownership |
| `mouse_state` | `MouseState` | `src/sdl_client/input/input.cpp:67` | `include/openglad/input/input.h:329` | `include/openglad/input/input.h`, `src/sdl_client/input/input.cpp` | No lock; main-thread input ownership |
| `difficulty_level` | `std::int32_t[DIFFICULTY_SETTINGS]` | `src/ui/picker_common.cpp:24` | local `extern` in `src/entities/living.cpp:40`, `src/entities/walker.cpp:60`, `src/sdl_client/ui/picker.cpp:144` | `src/ui/picker_common.cpp`, entity and picker code | Mutable global difficulty multipliers; no lock |
| `E_Screen` | `std::unique_ptr<Screen>` | `src/sdl_client/render/video.cpp:53` | `include/openglad/render/sai2x.h:50` | `src/sdl_client/render/video.cpp`, `src/sdl_client/runtime/game_session.cpp`, `src/sdl_client/demo.cpp` | Not thread-safe; comments indicate main-thread-only render access |
| `moving` | `bool` | `src/sdl_client/input/input.cpp:56` | `src/sdl_client/runtime/input_event_bridge.cpp:108` | touch input + bridge | No lock; expected input thread == main thread |
| `moving_touch_x` | `int` | `src/sdl_client/input/input.cpp:57` | `src/sdl_client/runtime/input_event_bridge.cpp:109` | touch input + bridge | same |
| `moving_touch_y` | `int` | `src/sdl_client/input/input.cpp:58` | `src/sdl_client/runtime/input_event_bridge.cpp:110` | touch input + bridge | same |
| `moving_touch_target_x` | `int` | `src/sdl_client/input/input.cpp:59` | `src/sdl_client/runtime/input_event_bridge.cpp:111` | touch input + bridge | same |
| `moving_touch_target_y` | `int` | `src/sdl_client/input/input.cpp:60` | `src/sdl_client/runtime/input_event_bridge.cpp:112` | touch input + bridge | same |
| `g_frame_state` | `GameLoopFrameState` | `src/sdl_client/runtime/glad_gameplay.cpp:24` | `src/sdl_client/glad.cpp:118` | `src/sdl_client/glad.cpp`, `src/sdl_client/runtime/glad_gameplay.cpp` | No lock; main loop state |
| `g_test_remove_exits` (`#ifdef TESTING`) | `bool` | `src/sdl_client/runtime/glad_gameplay.cpp:28` | `src/sdl_client/glad.cpp:87` | test/gameplay glue | Test-only mutable global |
| `mainmenu_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:419` (platform-dependent duplicate definitions also at :440/:465/:478) | `src/sdl_client/ui/picker_sdl_defs.h:15` | picker UI files | Main-thread UI only |
| `createmenu_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:547` | `src/sdl_client/ui/picker_sdl_defs.h:16` | picker/team-build files | Main-thread UI only |
| `viewteam_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:563` | `src/sdl_client/ui/picker_sdl_defs.h:17` | picker/team-build files | Main-thread UI only |
| `details_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:572` | `src/sdl_client/ui/picker_sdl_defs.h:18` | picker files | Main-thread UI only |
| `trainmenu_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:578` | `src/sdl_client/ui/picker_sdl_defs.h:19` | picker/team-build files | Main-thread UI only |
| `hiremenu_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:603` | `src/sdl_client/ui/picker_sdl_defs.h:20` | picker/team-build files | Main-thread UI only |
| `saveteam_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:614` | `src/sdl_client/ui/picker_sdl_defs.h:21` | picker/team-build files | Main-thread UI only |
| `loadteam_buttons` | `button[]` | `src/sdl_client/ui/picker.cpp:630` | `src/sdl_client/ui/picker_sdl_defs.h:22` | picker/team-build files | Main-thread UI only |
| `redraw` | `Sint32` | `src/sdl_client/ui/level_editor.cpp:110` | `src/sdl_client/ui/level_editor_tools.cpp:21` | level editor files | Main-thread UI loop |
| `main_title_logo_pix` | `std::unique_ptr<pixieN>` | `src/sdl_client/ui/picker.cpp:104` | `src/sdl_client/ui/picker_main_menu.cpp:39` | picker main menu files | Main-thread UI |
| `main_columns_pix` | `std::unique_ptr<pixieN>` | `src/sdl_client/ui/picker.cpp:105` | `src/sdl_client/ui/picker_main_menu.cpp:40` | picker main menu files | Main-thread UI |
| `old_guy` | `guy*` | `src/sdl_client/ui/picker.cpp:102` | `src/sdl_client/ui/picker_team_build.cpp:56` | picker/team-build files | Main-thread UI |
| `backdrops` | `std::array<std::unique_ptr<pixieN>, 5>` | `src/sdl_client/ui/picker.cpp:92` | `src/sdl_client/ui/button.cpp:30` | picker/button files | Main-thread UI |
| `scen_level` | `short` | **No definition found in scanned first-party sources** | `src/sdl_client/ui/button.cpp:29` | declaration observed only | Stale extern risk; verify linker path/legacy object |

## Namespace-Scope Mutable Globals

Notable non-`static` mutable globals (in addition to extern-backed items above):

- `src/sdl_client/input/input.cpp`
  - `tapping`, `start_tap_x`, `start_tap_y`, `movingTouch`, `firing`, `firingTouch`, `mouse_buttons`, `joysticks`, `player_control_modes`, `player_mode_keys`, `touch_keystate`.
- `src/sdl_client/ui/picker.cpp`
  - `backpics`, `g_start_game_requested` (`__EMSCRIPTEN__`), `g_picker_mainmenu_calls` / `g_picker_max_mainmenu_calls` (`TESTING`), `g_test_in_game` / `g_test_game_epoch` (`TESTING`).
- `src/sdl_client/ui/picker_dialogs.cpp`
  - `yes_or_no_buttons`, `no_or_yes_buttons`, `popup_dialog_buttons`, plus test globals `s_yes_or_no_overrides`, `s_force_real_dialogs`.
- `src/sdl_client/ui/picker_input.cpp`
  - `menu_nav_enabled`, `menu_nav_enabled_time`.
- `src/sdl_client/ui/help.cpp`
  - `end_of_file`, `helptext`.
- `src/sdl_client/ui/level_editor.cpp`
  - `scenpalette`, `campaignchanged`, `levelchanged`, `cyclemode`, `start_time_s`, `backgrounds`, `object_pane`, `rowsdown`, `maxrows`, `mouse_up_button`, `mouse_motion_x`, `mouse_motion_y`, `mouse_last_x`, `mouse_last_y`, `pan_left`, `pan_right`, `pan_up`, `pan_down`.
- `src/sdl_client/render/view.cpp`
  - `key1`, `key2`, `key3`, `key4`, `normalkeys`, `allkeys`.
- `src/runtime/sim_world.cpp`
  - `g_test_level_tick_limit_override` (`TESTING`).
- `src/text_client/platform_headless.cpp`
  - `end_of_file`.

### `gloader.cpp` mutable animation tables

`src/runtime/gloader.cpp` defines a large namespace-scope mutable data set of animation frame tables and pointer indirection tables (read-heavy, process-wide):

`bit1`, `bit2`, `bit3`, `bit4`, `bit5`, `bit6`, `bit7`, `bit8`, `att1`, `att2`, `att3`, `att4`, `att5`, `att6`, `att7`, `att8`, `bitm2`, `bitm4`, `bitm6`, `bitm8`, `mageatt1`, `mageatt2`, `mageatt3`, `mageatt4`, `mageatt5`, `mageatt6`, `mageatt7`, `mageatt8`, `tele_out1`, `tele_in1`, `tele_in2`, `tele_in3`, `tele_in4`, `gs_down`, `gs_up`, `skel_grow`, `skel_shrink`, `slime_pulse`, `slime_split`, `small_slime`, `series_8`, `aniexpand8`, `series_16`, `ani16`, `bomb1`, `anibomb1`, `explosion1`, `aniexplosion1`, `hit1`, `hit2`, `hit3`, `anihit`, `cloud_cycle`, `anicloud`, `marker_cycle`, `animarker`, `animan`, `aniskel`, `animage`, `anigs`, `anislime`, `ani_small_slime`, `kni1`, `kni2`, `anikni`, `rock1`, `anirock`, `grow1`, `anitree`, `door1`, `door2`, `anidoor`, `dooropen1`, `dooropen2`, `anidooropen`, `arrow1`, `arrow2`, `arrow3`, `arrow4`, `arrow5`, `arrow6`, `arrow7`, `arrow8`, `aniarrow`, `blob1`, `aniblob1`, `none1`, `aninone`, `towerglow1`, `anitower`, `tent1`, `anitent`, `blood1`, `aniblood`, `glowgrow`, `glowpulse`, `aniglowgrow`, `food1`, `anifood`.

All are defined and used within `src/runtime/gloader.cpp`.

## Static File-Scope Mutable Variables (by file)

- `src/core/util.cpp`
  - `g_app_start` (line 40)
- `src/runtime/game_context.cpp`
  - `s_production_rng` (34), `s_default_context` (35)
- `src/entities/family_registry.cpp`
  - `s_registry` (47)
- `src/entities/effect_family_registry.cpp`
  - `s_registry` (29)
- `src/entities/treasure_family_registry.cpp`
  - `s_registry` (31)
- `src/entities/generator_family_registry.cpp`
  - `s_registry` (17)
- `src/entities/weapon_family_registry.cpp`
  - `s_registry` (33)
- `src/entities/guy.cpp`
  - `guy_id_counter` (37)
- `src/sdl_client/runtime/cheat_handler.cpp`
  - `changedteam` (22)
- `src/sdl_client/glad.cpp`
  - `g_game_state` (80, emscripten), `g_state_initialized` (81, emscripten)
- `src/sdl_client/runtime/screen_lifecycle.cpp`
  - `g_session_owner` (16, anonymous namespace)
- `src/sdl_client/io/platform_io.cpp`
  - `idbfs_sync_done` (259, emscripten)
- `src/sdl_client/ui/picker.cpp`
  - `g_picker_intercept_scope` (153), `g_picker_selected_menu_item` (154)
- `src/sdl_client/ui/picker_team_build.cpp`
  - `g_hire_session` (59), `g_train_session` (60)
- `src/sdl_client/ui/button.cpp`
  - `owned_buttons` (35, anonymous namespace), `dumbcount` (37)
- `src/sdl_client/ui/help.cpp`
  - `classes_help_lines` (326), `editor_help_lines` (327), `help_files_loaded` (328)
- `src/sdl_client/render/text.cpp`
  - `letters1` (27), `letters_big` (28), `text_buffer` (115)
- `src/sdl_client/render/sai2x.cpp`
  - `colorMask` (18), `lowPixelMask` (19), `qcolorMask` (20), `qlowpixelMask` (21), `redblueMask` (22), `greenMask` (23), `PixelsPerMask` (24), `xsai_depth` (25), `src_line` (27), `dst_line` (28)

## Static Class Members

No mutable static class data members with out-of-class definitions were found.

Static class constants found:
- `JoyData::{NONE,BUTTON,POS_AXIS,NEG_AXIS,HAT_UP,HAT_UP_RIGHT,HAT_RIGHT,HAT_DOWN_RIGHT,HAT_DOWN,HAT_DOWN_LEFT,HAT_LEFT,HAT_UP_LEFT}` in `include/openglad/input/input.h:168-179`
- `LevelData::{TYPE_CAN_EXIT_WHENEVER,TYPE_MUST_DESTROY_GENERATORS,TYPE_MUST_PROTECT_NAMED_NPCS}` in `include/openglad/data/level_data.h:109-111`
- `GameSession::{kNumKeys,kMaxButtons,kNumFamilies}` in `include/openglad/runtime/game_session.h:72,85,99`

## Global Constants (brief)

High-volume constant globals were found in:
- `include/openglad/core/constants.h` (families, commands, flags, limits)
- `include/openglad/legacy/base.h`, `include/openglad/legacy/colors.h`, `include/openglad/legacy/view_sizes.h`, `include/openglad/legacy/soundob.h`
- `include/openglad/render/view.h`, `include/openglad/input/input.h`
- file-local `static const/constexpr` tables in runtime/render/UI modules (e.g., help tab names, smoothing lookup tables, descriptor name arrays)

Representative examples:
- `kDifficultyNames` (`src/ui/picker_common.cpp:30`, extern in `include/openglad/ui/picker_common.h:43`)
- `PIX_to_genre` and terrain variant tables (`src/runtime/smooth.cpp:132+`)
- class/family name arrays (`src/entities/families/*`)

## Remaining Macro-Based Global Access

Remaining macro-based patterns acting as global access or global-style constants:
- Build/platform and legacy control macros in `include/openglad/legacy/base.h` (`PROT_MODE`, `CHEAT_MODE`, etc.)
- Input key alias macros in `include/openglad/input/input.h` (`KEYSTATE_*`, `CONTINUE_ACTION_STRING`)
- Local menu-state constants via macros in UI/editor code (`src/sdl_client/ui/level_editor.cpp`, `src/sdl_client/ui/button.cpp`)

Notably, prior direct macro aliases for major singleton members were largely removed in favor of `current_session->...` direct access.

## Thread-Safety Concerns

1. `cfg` is process-global and mutable with no synchronization.
- Risk: races if background threads mutate settings while game/UI threads read.

2. `current_session` + `primary_session` cross-thread bootstrap relies on pointer lifetime discipline.
- `primary_session` is atomic, but pointed object ownership/lifetime is not encoded in the type.

3. SDL/input/render globals (`E_Screen`, `mouse_state`, joystick/touch globals, menu/button arrays) are unsynchronized.
- Current assumption appears to be single-threaded main-loop ownership.

4. `gloader.cpp` animation tables are mutable globals.
- Practically treated as read-only after init, but not declared `const`.

5. One stale extern declaration appears present (`extern short scen_level` in `src/sdl_client/ui/button.cpp`) without an in-tree definition.

## Recommendations for Further De-Singletonization

1. Move `cfg` behind a context/service object passed through `GameContext` and UI entry points.
2. Convert read-mostly global tables in `gloader.cpp` to `const`/`constexpr` and place in anonymous namespace or typed data objects.
3. Replace UI global arrays/state (`picker`, `level_editor`, `help`) with explicit state structs owned by the screen/session.
4. Consolidate touch/input globals into session-owned input state (mirroring existing migration of `player_keys_` etc.).
5. Replace `primary_session` raw pointer atomic with explicit ownership model (`shared_ptr` or lifetime-bound session manager).
6. Remove or resolve stale `extern` declarations (`scen_level`) to avoid hidden link/order coupling.

## Appendix: Full Variable Index (alphabetical)

This index lists mutable globals and cross-file global constants identified in first-party source. (Very large inline constexpr families in `constants.h`/legacy headers are grouped under file sections above.)

- `allkeys` — `src/sdl_client/render/view.cpp:143`
- `aniblood` — `src/runtime/gloader.cpp:314`
- `aniblob1` — `src/runtime/gloader.cpp:288`
- `anicloud` — `src/runtime/gloader.cpp:165`
- `anidoor` — `src/runtime/gloader.cpp:260`
- `anidooropen` — `src/runtime/gloader.cpp:268`
- `aniexpand8` — `src/runtime/gloader.cpp:116`
- `aniexplosion1` — `src/runtime/gloader.cpp:138`
- `anifood` — `src/runtime/gloader.cpp:332`
- `ani16` — `src/runtime/gloader.cpp:123`
- `aniglowgrow` — `src/runtime/gloader.cpp:322`
- `anigs` — `src/runtime/gloader.cpp:209`
- `anihit` — `src/runtime/gloader.cpp:155`
- `anikni` — `src/runtime/gloader.cpp:240`
- `animage` — `src/runtime/gloader.cpp:197`
- `animan` — `src/runtime/gloader.cpp:180`
- `animarker` — `src/runtime/gloader.cpp:174`
- `anibomb1` — `src/runtime/gloader.cpp:132`
- `aninone` — `src/runtime/gloader.cpp:294`
- `anirock` — `src/runtime/gloader.cpp:247`
- `aniskel` — `src/runtime/gloader.cpp:185`
- `anislime` — `src/runtime/gloader.cpp:216`
- `ani_small_slime` — `src/runtime/gloader.cpp:229`
- `anitent` — `src/runtime/gloader.cpp:308`
- `anitower` — `src/runtime/gloader.cpp:301`
- `anitree` — `src/runtime/gloader.cpp:253`
- `aniarrow` — `src/runtime/gloader.cpp:281`
- `arrow1`..`arrow8` — `src/runtime/gloader.cpp:273-280`
- `att1`..`att8` — `src/runtime/gloader.cpp:65-72`
- `backgrounds` — `src/sdl_client/ui/level_editor.cpp:177`
- `backdrops` — `src/sdl_client/ui/picker.cpp:92`
- `backpics` — `src/sdl_client/ui/picker.cpp:91`
- `bit1`..`bit8` — `src/runtime/gloader.cpp:56-63`
- `bitm2`, `bitm4`, `bitm6`, `bitm8` — `src/runtime/gloader.cpp:74-77`
- `blob1` — `src/runtime/gloader.cpp:287`
- `blood1` — `src/runtime/gloader.cpp:313`
- `campaignchanged` — `src/sdl_client/ui/level_editor.cpp:111`
- `cfg` — `src/data/gparser.cpp:40`
- `changedteam` — `src/sdl_client/runtime/cheat_handler.cpp:22`
- `classes_help_lines` — `src/sdl_client/ui/help.cpp:326`
- `cloud_cycle` — `src/runtime/gloader.cpp:164`
- `colorMask` — `src/sdl_client/render/sai2x.cpp:18`
- `createmenu_buttons` — `src/sdl_client/ui/picker.cpp:547`
- `current_session` — `src/sdl_client/runtime/game_session.cpp:27`, `src/text_client/main.cpp:64`
- `cyclemode` — `src/sdl_client/ui/level_editor.cpp:113`
- `details_buttons` — `src/sdl_client/ui/picker.cpp:572`
- `difficulty_level` — `src/ui/picker_common.cpp:24`
- `door1`, `door2`, `dooropen1`, `dooropen2` — `src/runtime/gloader.cpp:258-259,266-267`
- `dumbcount` — `src/sdl_client/ui/button.cpp:37`
- `dst_line` — `src/sdl_client/render/sai2x.cpp:28`
- `E_Screen` — `src/sdl_client/render/video.cpp:53`
- `editor_help_lines` — `src/sdl_client/ui/help.cpp:327`
- `end_of_file` — `src/sdl_client/ui/help.cpp:43`, `src/text_client/platform_headless.cpp:192`
- `explosion1` — `src/runtime/gloader.cpp:137`
- `firing`, `firingTouch` — `src/sdl_client/input/input.cpp:62-63`
- `food1` — `src/runtime/gloader.cpp:331`
- `g_app_start` — `src/core/util.cpp:40`
- `g_frame_state` — `src/sdl_client/runtime/glad_gameplay.cpp:24`
- `g_game_state` — `src/sdl_client/glad.cpp:80` (`__EMSCRIPTEN__`)
- `g_hire_session` — `src/sdl_client/ui/picker_team_build.cpp:59`
- `g_picker_intercept_scope` — `src/sdl_client/ui/picker.cpp:153`
- `g_picker_mainmenu_calls` — `src/sdl_client/ui/picker.cpp:110` (`TESTING`)
- `g_picker_max_mainmenu_calls` — `src/sdl_client/ui/picker.cpp:111` (`TESTING`)
- `g_picker_selected_menu_item` — `src/sdl_client/ui/picker.cpp:154`
- `g_reset_time` — `src/core/util.cpp:41` (`thread_local`)
- `g_session_owner` — `src/sdl_client/runtime/screen_lifecycle.cpp:16`
- `g_start_game_requested` — `src/sdl_client/ui/picker.cpp:99` (`__EMSCRIPTEN__`)
- `g_state_initialized` — `src/sdl_client/glad.cpp:81` (`__EMSCRIPTEN__`)
- `g_test_game_epoch` — `src/sdl_client/ui/picker.cpp:117` (`TESTING`)
- `g_test_in_game` — `src/sdl_client/ui/picker.cpp:114` (`TESTING`)
- `g_test_level_tick_limit_override` — `src/runtime/sim_world.cpp:22` (`TESTING`)
- `g_test_remove_exits` — `src/sdl_client/runtime/glad_gameplay.cpp:28` (`TESTING`)
- `g_trace_buffer` — `src/test_trace.cpp:15`
- `g_trace_mutex` — `src/test_trace.cpp:16`
- `g_train_session` — `src/sdl_client/ui/picker_team_build.cpp:60`
- `greenMask` — `src/sdl_client/render/sai2x.cpp:23`
- `grow1` — `src/runtime/gloader.cpp:252`
- `gs_down`, `gs_up` — `src/runtime/gloader.cpp:96-97`
- `help_files_loaded` — `src/sdl_client/ui/help.cpp:328`
- `helptext` — `src/sdl_client/ui/help.cpp:44`
- `hiremenu_buttons` — `src/sdl_client/ui/picker.cpp:603`
- `hit1`, `hit2`, `hit3` — `src/runtime/gloader.cpp:152-154`
- `idbfs_sync_done` — `src/sdl_client/io/platform_io.cpp:259` (`__EMSCRIPTEN__`)
- `joysticks` — `src/sdl_client/input/input.cpp:87`
- `key1`, `key2`, `key3`, `key4` — `src/sdl_client/render/view.cpp:83,95,107,119`
- `kni1`, `kni2` — `src/runtime/gloader.cpp:238-239`
- `kDifficultyNames` (const global) — `src/ui/picker_common.cpp:30`
- `letters1`, `letters_big` — `src/sdl_client/render/text.cpp:27-28`
- `levelchanged` — `src/sdl_client/ui/level_editor.cpp:112`
- `loadteam_buttons` — `src/sdl_client/ui/picker.cpp:630`
- `lowPixelMask` — `src/sdl_client/render/sai2x.cpp:19`
- `mageatt1`..`mageatt8` — `src/runtime/gloader.cpp:79-86`
- `main_columns_pix` — `src/sdl_client/ui/picker.cpp:105`
- `main_title_logo_pix` — `src/sdl_client/ui/picker.cpp:104`
- `mainmenu_buttons` — `src/sdl_client/ui/picker.cpp:419`
- `marker_cycle` — `src/runtime/gloader.cpp:170`
- `maxrows` — `src/sdl_client/ui/level_editor.cpp:255`
- `menu_nav_enabled`, `menu_nav_enabled_time` — `src/sdl_client/ui/picker_input.cpp:40-41`
- `mouse_buttons` — `src/sdl_client/input/input.cpp:68`
- `mouse_last_x`, `mouse_last_y` — `src/sdl_client/ui/level_editor.cpp:1616-1617`
- `mouse_motion_x`, `mouse_motion_y` — `src/sdl_client/ui/level_editor.cpp:1614-1615`
- `mouse_state` — `src/sdl_client/input/input.cpp:67`
- `mouse_up_button` — `src/sdl_client/ui/level_editor.cpp:1604`
- `moving`, `movingTouch` — `src/sdl_client/input/input.cpp:56,61`
- `moving_touch_x`, `moving_touch_y`, `moving_touch_target_x`, `moving_touch_target_y` — `src/sdl_client/input/input.cpp:57-60`
- `none1` — `src/runtime/gloader.cpp:293`
- `normalkeys` — `src/sdl_client/render/view.cpp:141`
- `object_pane` — `src/sdl_client/ui/level_editor.cpp:252`
- `old_guy` — `src/sdl_client/ui/picker.cpp:102`
- `owned_buttons` — `src/sdl_client/ui/button.cpp:35`
- `pan_down`, `pan_left`, `pan_right`, `pan_up` — `src/sdl_client/ui/level_editor.cpp:3001-3004`
- `path_map`, `path_walker`, `pather` — `src/entities/walker_pathing.cpp:106,31,107`
- `PixelsPerMask` — `src/sdl_client/render/sai2x.cpp:24`
- `player_control_modes`, `player_mode_keys` — `src/sdl_client/input/input.cpp:203,209`
- `player_joy` — `src/sdl_client/input/input.cpp:83`
- `popup_dialog_buttons` — `src/sdl_client/ui/picker_dialogs.cpp:49`
- `primary_session` — `src/sdl_client/runtime/game_session.cpp:28`, `src/text_client/main.cpp:65`
- `qcolorMask`, `qlowpixelMask` — `src/sdl_client/render/sai2x.cpp:20-21`
- `redblueMask` — `src/sdl_client/render/sai2x.cpp:22`
- `redraw` — `src/sdl_client/ui/level_editor.cpp:110`
- `rock1` — `src/runtime/gloader.cpp:246`
- `rowsdown` — `src/sdl_client/ui/level_editor.cpp:254`
- `s_active_context` — `src/runtime/game_context.cpp:36` (`thread_local`)
- `s_default_context` — `src/runtime/game_context.cpp:35`
- `s_force_real_dialogs` — `src/sdl_client/ui/picker_dialogs.cpp:121` (`TESTING`)
- `s_production_rng` — `src/runtime/game_context.cpp:34`
- `s_yes_or_no_overrides` — `src/sdl_client/ui/picker_dialogs.cpp:120` (`TESTING`)
- `saveteam_buttons` — `src/sdl_client/ui/picker.cpp:614`
- `scen_level` (extern declaration only found) — `src/sdl_client/ui/button.cpp:29`
- `scenpalette` — `src/sdl_client/ui/level_editor.cpp:109`
- `series_8`, `series_16` — `src/runtime/gloader.cpp:115,122`
- `skel_grow`, `skel_shrink` — `src/runtime/gloader.cpp:100-101`
- `slime_pulse`, `slime_split`, `small_slime` — `src/runtime/gloader.cpp:104,106,109`
- `src_line` — `src/sdl_client/render/sai2x.cpp:27`
- `start_tap_x`, `start_tap_y` — `src/sdl_client/input/input.cpp:53-54`
- `start_time_s` — `src/sdl_client/ui/level_editor.cpp:116`
- `tab_names` (const global) — `src/sdl_client/ui/help.cpp:405`
- `tapping` — `src/sdl_client/input/input.cpp:52`
- `tele_in1`, `tele_in2`, `tele_in3`, `tele_in4`, `tele_out1` — `src/runtime/gloader.cpp:90-93,89`
- `text_buffer` — `src/sdl_client/render/text.cpp:115`
- `touch_keystate` — `src/sdl_client/input/input.cpp:212`
- `trainmenu_buttons` — `src/sdl_client/ui/picker.cpp:578`
- `viewteam_buttons` — `src/sdl_client/ui/picker.cpp:563`
- `xsai_depth` — `src/sdl_client/render/sai2x.cpp:25`
- `yes_or_no_buttons`, `no_or_yes_buttons` — `src/sdl_client/ui/picker_dialogs.cpp:37,43`

