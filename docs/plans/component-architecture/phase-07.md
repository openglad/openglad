# Phase 7: Kill LevelData, create LevelVisuals

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 6](phase-06.md)
**Followed by:** [Phase 8](phase-08.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component), [LevelVisuals](key-types.md#levelvisuals-new--interface-component)

---

## Goal

Move remaining rendering data from `LevelData` to a new `LevelVisuals`
type on `screen`. Delete the `LevelData` class entirely.

## Steps

1. Create `include/openglad/interface/level_visuals.h` with the LevelVisuals struct
2. Move `renderer_`, `pixdata[]`, `topx/topy` from `LevelData` into `LevelVisuals`
3. Add `LevelVisuals level_visuals_;` member to `screen`
4. Delete the `LevelData` class — it is fully replaced
5. Update all remaining code that references `level_data.*` to go through
   either `world()` (gameplay data) or `level_visuals_` (rendering data)

## Editor Migration

`level_editor.cpp` heavily manipulates `LevelData`
(entity lists, grid operations, `mysmoother.smooth()`). All entity-list and
grid references become `world()` access. Smoother calls (`smooth()`,
`set_target()`) go through `world().mysmoother`. Rendering data
(`pixdata[]`, `renderer_`, `topx/topy`) goes through `level_visuals_`. This
is mechanical but high-churn for the editor file specifically.

## Editor and GameWorld Access Model

The level editor operates outside the
gameplay tick loop — it directly manipulates entity lists, grids, and the
smoother. After Phase 4, gameplay code reaches GameWorld through
`current_game->world`, but editor code does NOT run under a GameplayContext.
The editor accesses GameWorld through `screen::world()` (the non-owning pointer
or member) directly, bypassing `current_game`. This is fine because the editor
never calls `tick()` or entity `act()` methods that depend on the thread-local.
Editor-specific operations (`add_ob()`, `resize_grid()`, `smooth()`) must
remain callable without `current_game` being set. In particular,
`GameWorld::add_ob()` must handle the case where `current_game` is null —
the entity_factory callback is still valid (wired by platform at setup time),
but the created entity won't have `current_game` available until the next
gameplay session. Editor-created entities are serialized to disk and
re-instantiated when the level is loaded for play.

## Files Changed (major)

- New: `include/openglad/interface/level_visuals.h`
- Deleted: `include/openglad/data/level_data.h`, `src/data/level_data.cpp`
- Modified: `include/openglad/runtime/screen.h`, `src/sdl_client/runtime/screen.cpp`
- Modified: `src/sdl_client/ui/level_editor.cpp` (heavy — entity list + grid +
  rendering data references all change)
- Modified: All remaining files that reference `level_data.` (should be few by now)

## Risk

Medium — most references were already migrated in Phases 1–3, but
the level editor is a significant remaining consumer of `LevelData`.

## Testing

Full ctest.
