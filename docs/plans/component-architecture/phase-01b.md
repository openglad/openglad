# Phase 1b: Move spatial data, queries, finders, and metadata to GameWorld

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 1a](phase-01a.md)
**Followed by:** [Phase 2](phase-02.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Goal

Complete the GameWorld shell by moving spatial data, passability
queries, entity finders, and level metadata from `LevelData` into `GameWorld`.
The end result is identical to the original Phase 1 — the split just makes
each commit smaller and independently testable.

## Steps

1. Move spatial data (`myobmap`, `grid`, `mysmoother`, `pixmaxx`/`pixmaxy`)
   from `LevelData` into `GameWorld`
2. Move level metadata (`id`, `type`, `title`, `par_value`, `time_bonus_limit`,
   `difficulty`) into `GameWorld`
3. Move passability queries: `query_passable()`, `query_object_passable()`,
   `query_grid_passable()` to GameWorld
4. Move entity finders: `find_near_foe()`, `find_far_foe()`,
   `find_nearest_blood()`, `find_nearest_player()`, `find_in_range()`,
   `find_foes_in_range()`, `find_foe_weapons_in_range()`,
   `find_friends_in_range()` to GameWorld
5. Update forwarding accessors on `LevelData` for new fields
6. Move grid management methods to GameWorld: `create_new_grid()`,
   `resize_grid()`, `delete_grid()`, `clear()`. Note: these currently touch
   `mysmoother` (rendering state) — the `mysmoother.set_target(grid)` calls
   stay since `mysmoother` moves to GameWorld (plan already specifies this).
   `resize_grid()` also removes entities that fall off the map — pure
   gameplay concern.

## Files Changed (major)

- Modified: `include/openglad/gameplay/game_world.h` (spatial + metadata fields added)
- Modified: `include/openglad/data/level_data.h` (more fields removed, forwarding added)
- Modified: All files that reference passability queries or entity finders

## Risk

Medium — lower than Phase 1a since the GameWorld shell already exists.

## Testing

Full ctest.
