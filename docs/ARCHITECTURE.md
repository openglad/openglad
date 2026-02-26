# OpenGlad Architecture

This document describes the architecture as implemented in `CMakeLists.txt`, `src/`, and `include/openglad/` on branch `fix/architecture-docs-v2`.

## Repository Layout

```text
openglad/
├── src/
│   ├── core/
│   ├── gameplay/
│   │   └── families/
│   ├── interface/
│   │   ├── input/
│   │   ├── render/
│   │   └── ui/
│   ├── resources/
│   │   └── io/
│   ├── platform/
│   │   ├── sdl/
│   │   │   ├── io/
│   │   │   └── runtime/
│   │   └── text/
│   ├── runtime/
│   ├── fuzz/
│   └── render/                (legacy transitional headers)
│
├── include/openglad/
│   ├── core/
│   ├── gameplay/
│   │   └── families/
│   ├── resources/
│   ├── interface/
│   │   ├── input/
│   │   ├── render/
│   │   └── ui/
│   ├── platform/
│   │   └── sdl/
│   ├── sim/                   (event/sim bridge headers)
│   └── legacy/
│
├── tests/
│   └── unit/
├── scripts/
├── third_party/
├── CMakeLists.txt
└── CMakePresets.json
```

## Module Structure (Actual CMake Targets)

The native build creates these internal libraries:

- `og_core`
- `og_sim`
- `og_data`
- `og_resources`
- `og_entities`
- `og_io`
- `og_runtime`
- `og_render`
- `og_input`
- `og_ui`
- `og_platform`

`og_game` is an aggregate INTERFACE target linking all of the above.

### Source Ownership by Target

- `og_core`
: `src/gameplay/combat_math.cpp`, `src/core/util.cpp`

- `og_sim`
: `src/gameplay/sim_entity.cpp`, `src/gameplay/sim_event_log.cpp`

- `og_data`
: `src/resources/pixie_data.cpp`

- `og_resources`
: `src/resources/campaign_data.cpp`, `src/resources/gparser.cpp`, `src/resources/gloader.cpp`, `src/resources/level_io.cpp`, `src/resources/scenario_title_bridge.cpp`, `src/resources/save_io.cpp`

- `og_entities`
: `src/gameplay/effect.cpp`, `family_registry.cpp`, `weapon_family_registry.cpp`, `effect_family_registry.cpp`, `treasure_family_registry.cpp`, `generator_family_registry.cpp`, `guy.cpp`, `living.cpp`, `obmap.cpp`, `treasure.cpp`, `walker*.cpp`, `weap.cpp`, and all `src/gameplay/families/*.cpp` behavior files

- `og_io`
: `src/platform/sdl/io/platform_io.cpp`, `src/resources/platform_io.cpp`, `src/resources/io/{physfs_api.cpp,filesystem.cpp,zip_api.cpp,yaml_stream.cpp,og_file.cpp}`

- `og_runtime`
: `src/runtime/{game_context.cpp,platform_bridge.cpp,sim_input_handler.cpp}`, `src/interface/screen.cpp`, `src/gameplay/{stats.cpp,game_world.cpp,gameplay_context.cpp}`, and `src/platform/sdl/runtime/{game.cpp,game_loop.cpp,game_session.cpp,glad_gameplay.cpp,legacy_globals.cpp,score_panel.cpp,screen_lifecycle.cpp,guy_create.cpp,input_event_bridge.cpp,walker_render_bridge.cpp,sdl_context_services.cpp,cheat_handler.cpp}`

- `og_render`
: `src/interface/render/{graphlib.cpp,pal32.cpp,pixie.cpp,pixien.cpp,radar.cpp,sai2x.cpp,text.cpp,view.cpp,walker_draw.cpp,obmap_debug_draw.cpp,sdl_level_render.cpp}`, `src/platform/sdl/video.cpp`

- `og_input`
: `src/interface/input/input.cpp`, `src/interface/input/input_state.cpp`

- `og_ui`
: `src/interface/ui/{button.cpp,campaign_picker.cpp,help.cpp,intro.cpp,level_editor.cpp,level_editor_file_ops.cpp,level_editor_tools.cpp,level_editor_ui.cpp,level_picker.cpp,picker.cpp,picker_accessible_levels.cpp,picker_dialogs.cpp,picker_input.cpp,picker_main_menu.cpp,picker_team_build.cpp,menu_model.cpp,picker_common.cpp,picker_state.cpp,results_screen.cpp}`

- `og_platform`
: `src/platform/sdl/sound.cpp`

## Runtime/Game Loop (Actual Flow)

Primary frame flow (`src/platform/sdl/runtime/game_loop.cpp`):

1. `game_frame()` calls `game_frame_with_result()`.
2. `screen::act()` is invoked.
3. `screen::act()` sets up world flags, then calls `GameWorld::tick()`.
4. `GameWorld::tick()` updates simulation and pushes typed events into `SimEventLog`.
5. Back in `screen::act()`, events are drained via `events.drain()` and dispatched (sound, notifications, palette changes, redraw requests, exit/withdraw flow, endgame).

So the current authoritative chain is:

`game_frame() -> screen::act() -> GameWorld::tick() -> event drain/dispatch in screen::act()`

## Key Data Structures

### `GameContext` (`include/openglad/platform/game_context.h`)

`GameContext` currently owns/holds:

- `std::string mounted_campaign`
- `IRandom* rng`
- `InputState input`
- `std::unique_ptr<og::sim::SimEventLog> sim_events`

It does not own screen/prefs/config references.

### `screen` (`include/openglad/interface/screen.h`, `src/interface/screen.cpp`)

`screen` is the runtime-integrated world/view object. Key members include:

- non-owning `GameWorld* world_` with `attach_world()/world()` accessors
- `SaveData save_data`
- `std::unique_ptr<video> video_`
- `std::unique_ptr<viewscreen> viewob[5]` and `numviews`
- `std::unique_ptr<loader> myloader`
- `LevelVisuals level_visuals_`
- per-frame flags/counters (`redrawme`, `framecount`, `timerstart`, etc.)

`screen::act()` is now the simulation-to-runtime dispatch boundary.

### `SaveData` (`include/openglad/resources/save_io.h`)

Current persisted in-memory fields:

- `save_name`
- `current_campaign`
- `scen_num`
- `completed_levels` (campaign -> set of level indices)
- `current_levels` (campaign -> current level)
- `score`, `m_score[4]`
- `totalcash`, `m_totalcash[4]`
- `totalscore`, `m_totalscore[4]`
- `my_team`
- `team_list` (`array<unique_ptr<guy>, MAX_TEAM_SIZE>`)
- `team_size`
- `numplayers`
- `allied_mode`

## Simulation Events

`EventKind` values in `include/openglad/gameplay/event.h`:

- `None = 0`
- `PlaySound = 4`
- `Notification = 8`
- `SetPalette = 11`
- `RequestRedraw = 12`
- `EndGame = 13`
- `DamageTile = 14`
- `SetEnd = 15`
- `RequestExitConfirmation = 16`
- `WithdrawToLevel = 17`
- `ScoreChange = 18`

## Component Boundaries (Enforced)

`scripts/check_vendor_leaks.sh` enforces four rule groups:

1. Public headers in `include/openglad/` may not include vendor headers (`physfs`, `libzip`, `libyaml`, `yam`, `zlib`).
2. Filesystem/archive vendor headers may only be included from `src/resources/io/` and `src/platform/sdl/io/` (plus `ogfile_yaml` exception).
3. Source include-root rules:
- `src/core/*` and `src/gameplay/*` may include only `openglad/core` and `openglad/gameplay`.
- `src/resources/*` may include only `openglad/core`, `openglad/gameplay`, `openglad/resources`.
- `src/interface/*` may include only `openglad/core`, `openglad/gameplay`, `openglad/resources`, `openglad/interface`.
- `src/platform/sdl/*` is unrestricted.
4. Public header graph guardrail: headers under `include/openglad/{core,gameplay,resources,interface}` may not include `openglad/platform`.

## Build System

### Presets (`CMakePresets.json`)

Configure/build presets currently include:

- `dev-debug`
- `dev-release`
- `ci-test`
- `ci-asan`
- `dev-debug-vcpkg`
- `dev-debug-conan`
- `ci-coverage`
- `ci-fuzz`
- `web-emscripten`

Notable CI additions present now:

- `ci-coverage` (`ENABLE_COVERAGE=ON`)
- `ci-fuzz` (`ENABLE_FUZZING=ON`, clang/libFuzzer toolchain)

## Test Structure

Current CMake test binaries:

- `og_unit_tests` (headless unit binary; `OG_UNIT_TEST_SOURCES` has 27 source files)
- `openglad_test` (full SDL integration suite; `TEST_SOURCES` has 145 source files)
- `og_data_tests` (`DATA_TEST_SOURCES` subset entries: 13)
- `og_runtime_tests` (`RUNTIME_TEST_SOURCES` entries: 80, including `EXTRA` coverage files)

Additional scripted/auxiliary CTest entries:

- `openglad_test_menu`
- `openglad_test_picker`
- `openglad_text_sim`
- `openglad_text_picker_interactive`
- `openglad_text_unsupported`
- `emscripten_build_test`

`add_test(NAME ...)` registrations in `CMakeLists.txt`: 10 total.

## Remaining Intentional Globals

These globals remain intentionally, with current rationale:

- `cfg` (`src/resources/gparser.cpp`)
: process-wide runtime configuration store used across startup/runtime paths.

- `thread_local GameSession* current_session` (`src/platform/sdl/runtime/game_session.cpp`, headless mirror in `src/platform/text/main.cpp`)
: legacy access bridge and per-thread active runtime session.

- `std::atomic<GameSession*> primary_session` and `std::atomic<uint64_t> primary_session_generation`
: cross-thread session inheritance/versioning for worker/demo/test threads.

- `thread_local GameplayContext* current_game` (`src/gameplay/gameplay_context.cpp`)
: legacy gameplay call-site bridge to active world/sim context.

- `std::unique_ptr<Screen> E_Screen` (`src/platform/sdl/video.cpp`)
: SDL display/render backend singleton used by legacy video path.

- `SDL_Joystick* joysticks[MAX_NUM_JOYSTICKS]` (`src/interface/input/input.cpp`)
: process-level SDL joystick handles.

- `extern const int32_t difficulty_level[DIFFICULTY_SETTINGS]` and `kDifficultyNames`
: shared immutable difficulty tables for picker/UI logic.

- test-only globals
: `g_test_level_tick_limit_override` (simulation time limit override), `g_test_remove_exits` (integration behavior override), `g_trace_buffer`/`g_trace_mutex` (test trace capture).

- platform bridge install state in `src/runtime/platform_bridge.cpp`
: `g_platform_bridge` and one-time install guard are intentionally process-global to keep one callback table per process.

## Entry Points

- SDL app entry: `src/platform/sdl/glad.cpp`
- Headless text client: `src/platform/text/main.cpp`
- Frame loop: `src/platform/sdl/runtime/game_loop.cpp`
- Runtime world adapter: `src/interface/screen.cpp`
- Simulation tick: `src/gameplay/game_world.cpp`

