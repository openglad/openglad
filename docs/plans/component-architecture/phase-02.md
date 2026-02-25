# Phase 2: Absorb legacy simulation layer into GameWorld

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 1b](phase-01b.md)
**Followed by:** [Phase 3](phase-03.md)
**Key types:** [GameWorld](key-types.md#gameworld-new--gameplay-component)

---

## Goal

Move tick logic, tick counters, and RNG from `legacy simulation layer` directly onto
`GameWorld`. Delete `legacy simulation layer` as a class. `tick()` still returns `TickResult`
at this point — the signature change comes in Phase 3.

## Steps

1. Move `tick()`, `tick_count_`, `level_tick_count_`, `rng_` from `legacy simulation layer`
   onto `GameWorld`
2. Move `TickResult` struct definition from `game_world.h` to `game_world.h`
   (temporary home — Phase 3 eliminates it)
3. `GameWorld::tick()` takes the same parameters `GameWorld::tick()` currently
   takes (LevelData&, SaveData&, enemy_freeze&, end, SimEventLog&) — except
   LevelData references become self-references since entity lists are now local
4. `screen::act()` calls `world_.tick(...)` instead of `world_.tick(...)`
5. Delete `legacy simulation layer` class and `game_world.h`

## Files Changed (major)

- Deleted: `include/openglad/sim/game_world.h`, `src/runtime/game_world.cpp`
- Modified: `include/openglad/gameplay/game_world.h` (tick logic + TickResult added)
- Modified: `include/openglad/runtime/screen.h` (remove `world_` member)
- Modified: `src/sdl_client/runtime/screen.cpp` (act() delegation)

## Risk

Medium — legacy simulation layer is well-encapsulated. The merge is mechanical.

## Testing

Full ctest.
