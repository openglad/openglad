# Phase 0: Migrate `rand()` to `SimRandom`

> **See also:** [Context & Key Decisions](../common/context.md) | [Verification Strategy](../common/verification-strategy.md)

Gameplay code uses C stdlib `rand()` in ~17 call sites, making the simulation non-deterministic across runs. While the snapshot-based architecture tolerates this (server is authoritative), fixing it enables future deterministic replay, easier debugging, and lockstep optimization.

**Call sites to migrate (all in `src/gameplay/`):**

| File | Count | Usage |
|------|-------|-------|
| `game_world.cpp:692,714` | 2 | Level generation `rand()%4` |
| `walker.cpp:121,252` | 2 | `path_check_counter = 5 + rand()%10` |
| `smooth.cpp:36` | 1 | `std::rand() % max_exclusive` |
| `walker_combat.cpp:117` | 1 | Blood splatter animation `rand()%3` |
| `stats.cpp:979` | 1 | AI pathfinding re-path delay |
| `family_elf.cpp:33-80` | 10 | Elf projectile spread `rand()%101` |

**Changes:**
- Each call site gets access to `IRandom&` via the thread-local `GameplayContext` (which holds `GameWorld*` containing `rng_`, already accessible in `walker::act()` chains as `current_game->world->rng_`) or via a direct `GameWorld&` reference
- Replace `rand() % N` -> `rng.next(N)` using the existing `SimRandom::next(uint32_t max_exclusive)` API
- `smooth.cpp` has a local `rng()` function (line 29-37) with a layered fallback: first checks `gameplay_rng_override()` (test injection), then tries `current_game->world->rng_` (SimRandom), then falls back to `std::rand()`. The `std::rand()` fallback at line 36 should never execute if GameplayContext is properly installed — remove the fallback and assert instead
- `walker_init_common()` at `walker.cpp:121` and the walker constructor at `walker.cpp:252` use `rand()` for `path_check_counter` — pass `IRandom&` to these functions
- Verify no `rand()` or `std::rand()` calls remain in `src/gameplay/` after migration

**Out-of-scope `rand()` calls (UI-only, don't affect simulation):**
- `src/interface/screen.cpp:161` — `screen::random()` wrapper around `rand()`. Verified not called from any `src/gameplay/` code. Used for UI effects only (e.g., random screen shakes). Does not affect simulation determinism.
- `src/interface/ui/picker_common.cpp:161` — `GET_RAND_ELEM` macro for character name generation. UI-only.
- `src/platform/sdl/glad.cpp:253` and `src/platform/sdl/demo.cpp:291` — `srand()` calls at startup. Seeds the stdlib RNG for the above UI-only callers.

These are explicitly left as `rand()` — they don't touch simulation state and converting them would add unnecessary `IRandom&` plumbing to UI code.

**Pre-migration check for `smooth.cpp`:** Before removing the `std::rand()` fallback and replacing with an assert, verify that **all callers** of `smooth()` execute with a valid `GameplayContext` installed (`current_game != nullptr` with a valid `world`). `smooth()` is called from entity movement code in `src/gameplay/` — grep for all call sites of `smooth(` and `rng(` within `smooth.cpp`'s translation unit. If any caller runs during level loading or outside a `GameWorld::tick()` context (before `current_game` is installed), the assert will fire. In that case, keep the `std::rand()` fallback for the non-gameplay path, guarded by a comment explaining why.

**Additional determinism fix — `pow()` in obmap:**
`obmap.cpp:297` uses `pow(2.0, door_level)` for door unlock checks — a floating-point `pow()` in simulation code. This can diverge across platforms/optimization levels. Replace with `1 << level` (bit shift) — same math for integer levels, deterministic everywhere. **Bounds guard:** Assert or clamp `door_level` to `[0, 30]` before the shift — `1 << 31` is undefined behavior in C++, and `1 << 32+` is always UB. In practice `door_level` is a small integer (0-5 range), but a defensive clamp costs nothing and prevents a latent UB. While here, grep `src/gameplay/` for any other `math.h`/`cmath` function calls (`sin`, `cos`, `sqrt`, `fmod`, `pow`, `atan2`) and audit whether they affect simulation state:
- `atan2f` in `walker_combat.cpp:144` — sets `hit_recoil_angle` and `attack_lunge_angle`. These are serialized in snapshots (Phase 5), but since the server computes them and clients receive the result, cross-platform divergence doesn't matter for the snapshot model. Only becomes a concern if client-side prediction is added later. **No action needed now**, but document as a future determinism consideration.
- Other `cmath` calls in `src/gameplay/` should be audited during this phase for simulation impact.

**Dead code cleanup — `statistics::walkrounds`:**
`statistics::walkrounds` (private, `statistics.h:117`) is dead code: commented out in usage (`stats.cpp:730-731`), only ever zeroed (`stats.cpp:911,921,933,946`). Delete the field and remove the 4 zeroing sites. Less surface area to maintain, and one fewer field to reason about when building the snapshot field table in Phase 5.

**Verify:** `grep -rn 'rand()' src/gameplay/` returns zero results. `grep -rn 'pow(' src/gameplay/` returns zero results (or only non-simulation-affecting calls). `walkrounds` removed. All existing tests pass. Seed the RNG with a known value and verify two identical runs produce identical `path_check_counter` values and elf projectile spreads.
