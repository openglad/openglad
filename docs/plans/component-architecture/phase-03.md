# Phase 3: Consolidate game state flags, eliminate TickResult

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 2](phase-02.md)
**Followed by:** [Phase 4](phase-04.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Goal

Move game state flags (`level_done`, `enemy_freeze`, `end`, etc.)
from `screen` into `GameWorld`. `tick()` writes these directly instead of
returning `TickResult`.

## Note on `level_done` Triple-Storage

Currently `level_done` exists on
`screen` (the authoritative copy), on `LevelData` (a copy set by `screen::act`),
and as a return value in `TickResult`. All three are set in `screen::act()`:
```cpp
level_done = result.level_done;
level_data.level_done = result.level_done;
```
This sub-phase consolidates to a single authoritative location on `GameWorld`.
The `LevelData` forwarding accessor (from Phase 1a/1b) points to `world_.level_done`.
The `screen`-level field is removed and replaced by `world_.level_done` access.

## Steps

1. Move `level_done`, `game_ended`, `next_level`, `ending`, `enemy_freeze`,
   `end`, `retry`, `control_hp`, `timer_wait` from `screen` into `GameWorld`
2. Add `m_score[4]`, `my_team`, `allied_mode`, `current_scenario`,
   `completed_levels`, `difficulty` fields to GameWorld
3. `tick()` drops `TickResult` return — writes state flags in place on self
4. `screen::act()` reads `world_.level_done` etc. after tick() returns
5. Remove the `LevelData.level_done` copy and the `screen.level_done` field —
   single source of truth on `GameWorld`

## Files Changed (major)

- Modified: `include/openglad/gameplay/game_world.h` (state flags added)
- Modified: `include/openglad/runtime/screen.h` (state fields removed)
- Modified: `src/sdl_client/runtime/screen.cpp` (act() reads from world_)
- Deleted: `TickResult` struct from `game_world.h` (moved there in Phase 2,
  now eliminated — tick() writes state flags in place)

## Risk

Medium — touching screen's state fields affects many callers, but
forwarding accessors on screen ease the transition.

## Testing

Full ctest.

---

## Ownership after Phases 1–3

```
screen
├── world_ (GameWorld) — entity lists, grid, smoother, obmap, tick logic, RNG, game state
├── level_data (LevelData) — temporarily holds myloader + rendering data
├── save_data (SaveData) — stays on screen for now (moves in Phase 5/9)
└── viewscreens, video base, sound, palette (display state — stays)
```
