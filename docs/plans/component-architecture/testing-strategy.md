# Testing Strategy

**Part of:** [Component Architecture Plan](README.md)

---

**All tests must pass at the end of every phase.** Test migration happens
incrementally — each phase owns its test updates.

## Test Helpers (RAII)

**Phase 1a** creates `TestGameWorld` — a minimal RAII helper that sets up a
GameWorld and SimEventLog for unit tests. No thread-local installation needed
at this stage.

**Phase 4** extends this into `TestGameplayContext` — additionally installs
and tears down the `current_game` thread-local:

```cpp
// tests/unit/test_gameplay_context.h
class TestGameplayContext {
public:
    TestGameplayContext() {
        // Creates a minimal GameWorld (with deterministic RNG), event log
        // Sets current_game thread-local
    }
    ~TestGameplayContext() {
        // Tears down current_game
    }

    GameWorld& world();          // includes world().rng_ for RNG access
    SimEventLog& events();
};
```

## Difficulty in Tests

Tests set `world.difficulty` directly with explicit values — no dependency
on `GameSession` or config parsing:

```cpp
OG_UNIT_TEST(test_enemy_scaling) {
    TestGameplayContext ctx;
    ctx.world().difficulty = 200;  // Slaughter mode
    // ... test that enemy stats scale correctly ...
}
```

## Migration per Phase

- **Phase 1a/1b:** Tests that access `myscreen->level_data.*` update to
  `myscreen->world().*`. `TestGameWorld` RAII helper created in 1a.
- **Phase 2:** Tests that reference `world_` update to `world_.tick()`.
- **Phase 3:** Tests that read `level_done` etc. from screen update to
  read from `world_`. `TickResult` assertions removed.
- **Phase 4:** `TestGameWorld` extended to `TestGameplayContext` (installs
  `current_game`). Tests switch from `ctx()` / `sim_*` pointer setup to
  `current_game->` access. Wiring code in test fixtures simplified.
- **Phase 5:** Tests for exit/withdrawal behavior added. Tests stop
  referencing `sim_save` and `sim_config` from entity code.
- **Phase 6:** Entity creation tests verify factory callback path.
- **Phase 7:** Remaining `level_data.*` references cleaned up in tests
  (including level editor test paths).
- **Phase 8:** Rendering tests update downcast patterns.
- **Phase 9:** Data loading tests use new free functions in resources.
- **Phase 10:** Include paths updated in test files.
- **Phase 12:** Test-only globals (`#ifdef TESTING`) audited and documented.
