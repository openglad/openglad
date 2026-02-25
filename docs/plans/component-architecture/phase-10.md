# Phase 10: Reorganize Directory Structure

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 9](phase-09.md)
**Followed by:** [Phase 11](phase-11.md)
**Key types:** [Target Architecture](target-architecture.md)

---

## Goal

Move source files into the four component directories. This phase
also completes the screen display split: `screen::redraw()`, viewscreens, and
other display code move to `interface/`. The logical split was already done in
Phases 1–3 (GameWorld extracted, screen became a display shell). Phase 10 makes the
physical move.

## Target Layout

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
│   (no game_world.h — absorbed into GameWorld in Phase 2)
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
│   ├── game_world.cpp        (includes absorbed game_world tick logic)
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

## Approach

Move files in batches, updating CMakeLists.txt and `#include`
paths after each batch. Order of moves:

1. **gameplay/** — entities, sim, game_world, core game types
2. **resources/** — io, data serialization, gloader
3. **interface/** — render, input, ui
4. **platform/** — sdl_client, text_client

Use `git mv` to preserve history. Update includes with a script or
find-and-replace.

## The video/screen Inheritance Problem

`screen` currently inherits from
`video`, which wraps `SDL_Surface` and SDL rendering primitives. Moving `screen`
to the interface layer requires breaking this inheritance — interface code
cannot depend on SDL types. **Solution:** Split `video` into an abstract base
class (drawing primitives, pixel buffer interface) in interface and a concrete
SDL implementation in platform. `screen` inherits from the abstract base.
Platform provides the concrete `video` via `PlatformBridge::create_surface` or
constructor injection. This split should happen as part of the interface batch
move (step 3 above), not deferred to Phase 11.

## GameWorld Ownership Transfer

During Phases 1 through 9, `screen` owns
`GameWorld` as a direct member (`GameWorld world_;`). In this phase, when
`screen` moves to the interface layer, GameWorld ownership transfers to
`GameSession` in platform. `screen` is refactored to hold a non-owning
`GameWorld*` pointer. This is a behavioral change — level transition code that
currently creates/reinitializes `world_` inside `screen` must move to
`GameSession`. Plan this as a dedicated sub-step before the physical file moves.

## Note on `og_runtime` SDL Mixing

`og_runtime` currently has 13 source files
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

## Risk

High churn on include paths and CMake. The video split, ownership
transfer, and per-file `og_runtime` triage add logic changes on top of the
mechanical moves. Run full ctest after each batch.
