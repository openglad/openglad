# Finding 25 Deep Audit: GameContext Migration Status and Remaining Work

## Executive Summary

Commit `75c1828` removed the transitional forwarding fields (`game_screen`, `prefs`, `config`) and their accessor methods from `GameContext`, eliminating ~142 lines across 43 files. What remains in `GameContext` today is a 4-field struct serving two distinct roles: (1) a genuine DI point for test-injectable RNG, and (2) a grab-bag of loosely related runtime state (input snapshot, sim event log, mounted campaign string). The "real DI" claim holds for `rng` — 30+ test files actively inject mock RNG implementations. It does not hold for the other three fields, which are always accessed through the same global singleton and never swapped in tests.

**Bottom line:** GameContext is no longer transitional — it has settled into a permanent role as a DI container for RNG plus a convenience holder for three other pieces of state. Full elimination would require ~60 file changes and yield only ~80 lines of reduction, with significant risk to the well-tested RNG injection mechanism. The pragmatic path is to leave it as-is or do minor cleanup (extract `mounted_campaign`, remove stale includes).

---

## 1. What Commit 75c1828 Changed

### Removed from `GameContext` struct
| Item | Type | Purpose |
|------|------|---------|
| `game_screen` | `screen*` | Forwarded `myscreen` global |
| `prefs` | `options*` | Forwarded `theprefs` global |
| `config` | `cfg_store*` | Forwarded `cfg` global |
| `valid()` | `bool` | Checked `game_screen != nullptr` |
| `active_screen()` | `screen*` | Returned `game_screen` |
| `active_prefs()` | `options*` | Returned `prefs` |
| `active_config()` | `cfg_store*` | Returned `config` |
| `active_input()` | `InputState*` | Returned `&input` |

### How call sites were updated
- All `ctx().active_screen()` → `myscreen`
- All `ctx().active_prefs()` → `theprefs`
- All `ctx().active_config()` → `cfg`
- All `ctx().valid()` → removed or replaced with `myscreen != nullptr`
- Forward declarations of `screen`, `options`, `cfg_store` removed from header
- `#include <functional>` removed (no longer needed)
- `game_context.cpp`: removed lazy population of `game_screen` and `config` from globals, removed `extern screen* myscreen`
- `sdl_context_services.cpp`: `install_sdl_context_services()` became an empty stub (the fields it populated no longer exist)

### Impact
- 43 files changed, net ~142 lines removed
- Clean separation: globals (`myscreen`, `theprefs`, `cfg`) are accessed directly, not through an indirection layer

---

## 2. What Remains in GameContext Today

### Header: `include/openglad/runtime/game_context.h`

```cpp
struct GameContext {
    GameContext();                              // Allocates sim_events
    ~GameContext();
    // Non-copyable, moveable

    std::string mounted_campaign;              // Currently mounted campaign package ID
    IRandom*    rng         = nullptr;         // Injectable RNG interface
    InputState  input       = {};              // Per-frame input snapshot (4 players)
    std::unique_ptr<og::sim::SimEventLog> sim_events;  // Simulation event accumulator

    void poll_input();                         // Calls input_state_from_sdl(input)
};
```

### Supporting infrastructure
| Item | Location | Purpose |
|------|----------|---------|
| `ProductionRandom` | `game_context.h/.cpp` | `IRandom` impl wrapping legacy `random()` |
| `input_state_from_sdl()` | `game_context.h`, impl in `sdl_context_services.cpp` | Populates `InputState` from SDL |
| `ctx()` | `game_context.cpp` | Returns global singleton or overridden context |
| `set_global_context()` | `game_context.cpp` | Installs test/session context |
| `s_default_context` | `game_context.cpp` | Static default instance |
| `s_active_context` | `game_context.cpp` | Override pointer (null = use default) |
| `src/runtime/game_context.h` | Transitional shim | `#include <openglad/runtime/game_context.h>` redirect |

### Size
- Header: 63 lines
- Implementation: 53 lines
- **Total: ~116 lines** of GameContext infrastructure

---

## 3. Analysis of Each Remaining Field

### 3.1 `IRandom* rng` — **Genuine DI, heavily used**

**Production path:** `ctx()` lazily assigns `&s_production_rng` (a `ProductionRandom` that wraps the legacy `random()` function). Six production files access `ctx().rng->next()` through local `rng()` wrapper functions:

| File | Pattern |
|------|---------|
| `src/runtime/smooth.cpp:25` | `static inline Uint32 rng(Uint32 x) { return ctx().rng->next(x); }` |
| `src/runtime/stats.cpp:38` | Same pattern |
| `src/sdl_client/glad.cpp:64` | Same pattern |
| `src/sdl_client/runtime/score_panel.cpp:31` | Same pattern |
| `src/sdl_client/render/radar.cpp:30` | Same pattern |
| `src/sdl_client/render/video.cpp:36` | Same pattern |

Additionally, `sdl_context_services.cpp:86` copies `ctx().rng` into `walker::sim_rng` for entity-level access without going through the global.

**Test usage:** 30+ test files inject custom `IRandom` implementations via `set_global_context()` or direct `ctx().rng = &mock` assignment. Custom implementations include:
- `FixedRandom` (returns constant value)
- `SeededRandom` (deterministic LCG)
- `SequenceRandom` (cycles through predetermined values)
- Various `SeqRandom`, `ConstRandom`, `MaxRandom` one-off implementations

**Verdict:** This is **real dependency injection**. Tests actively substitute mock RNGs to get deterministic, reproducible behavior for combat math, smoothing, walker specials, etc. Removing this would require either (a) making `random()` itself injectable (modifying a legacy C function), or (b) passing `IRandom&` to every function that needs randomness (massive signature changes). The current approach is the least-invasive way to make game logic testable.

### 3.2 `InputState input` — **Convenient but not injected**

**Production path:** `game_loop.cpp:131` calls `ctx().poll_input()` once per frame, which calls `input_state_from_sdl(input)`. Then `game_loop.cpp:134` passes `ctx().input` to `screen::process_input()`. `view.cpp:410` reads `ctx().input.players[mynum]` for per-player input.

**Test usage:** `test_view_input_paths.cpp` directly writes to `ctx().input.players[0].held[...]` to simulate input, but this is manipulating the global singleton, not injecting a different `InputState`. No test ever creates a separate `GameContext` with a custom `InputState` to test input behavior.

**Verdict:** This is **not DI** — it's a global input buffer that happens to live inside `GameContext` instead of being a standalone global. It could be a free-standing `InputState g_input;` global with identical behavior. However, since only 3 production files read it, the coupling is modest. Moving it out would be straightforward but low-value.

### 3.3 `std::unique_ptr<SimEventLog> sim_events` — **Useful ownership, not injected**

**Production path:** `screen.cpp:449` gets `*ctx().sim_events` to pass to `SimWorld::tick()`. `sdl_context_services.cpp:84-85` copies the raw pointer into `walker::sim_events` for entity-level event emission. `view.cpp:474` passes `ctx().sim_events.get()` to input handling.

**Test usage:** No test ever creates a `GameContext` with a custom `SimEventLog` or swaps the event log. The only interaction is through the global singleton.

**Verdict:** This is **ownership, not DI**. `GameContext` owns the `SimEventLog` via `unique_ptr`, which is useful for RAII lifecycle, but the "injection" aspect is unused. This could be a `std::unique_ptr<SimEventLog>` owned by `GameSession` or even a standalone global. The sim_events pointer is propagated to entities via `sdl_context_services.cpp`, so some wiring mechanism is needed regardless.

### 3.4 `std::string mounted_campaign` — **Pure state, not injected**

**Production path:** `platform_io_common.cpp` reads and writes `ctx().mounted_campaign` in 5 locations for campaign mount/unmount operations. `game_session.cpp:34` preserves the mounted campaign across session creation.

**Test usage:** `test_io_platform_coverage.cpp` manipulates `ctx().mounted_campaign` directly in ~20 locations, saving/restoring it manually. This is test fixture management, not DI — no separate `GameContext` is created.

**Verdict:** This is a **plain global string** that happens to live inside `GameContext`. It's functionally equivalent to `static std::string g_mounted_campaign;` with getter/setter functions. It was likely placed here during the "Phase 1: thin wrapper" era when the plan was to centralize all game state. It could trivially be extracted to a standalone global or into the IO module.

### 3.5 `poll_input()` method — **Thin wrapper**

Calls `input_state_from_sdl(input)`. Could be a free function `poll_global_input()` that writes to a global `InputState` instead.

---

## 4. Complete GameContext Usage Map

### Production source files (18 that actively use `ctx()`)

| File | Uses |
|------|------|
| `src/runtime/game_context.cpp` | Defines `ctx()`, `set_global_context()`, `ProductionRandom` |
| `src/runtime/smooth.cpp` | `ctx().rng` |
| `src/runtime/stats.cpp` | `ctx().rng` |
| `src/runtime/level_data.cpp` | includes header (uses `ctx()` indirectly) |
| `src/io/platform_io_common.cpp` | `ctx().mounted_campaign` |
| `src/sdl_client/glad.cpp` | `ctx().rng` |
| `src/sdl_client/render/radar.cpp` | `ctx().rng` |
| `src/sdl_client/render/video.cpp` | `ctx().rng` |
| `src/sdl_client/render/view.cpp` | `ctx().input`, `ctx().sim_events` |
| `src/sdl_client/runtime/game_loop.cpp` | `ctx().poll_input()`, `ctx().input` |
| `src/sdl_client/runtime/game_session.cpp` | `set_global_context()`, `ctx().mounted_campaign` |
| `src/sdl_client/runtime/screen.cpp` | `ctx().sim_events` |
| `src/sdl_client/runtime/score_panel.cpp` | `ctx().rng` |
| `src/sdl_client/runtime/sdl_context_services.cpp` | `ctx().sim_events`, `ctx().rng` |
| `src/sdl_client/runtime/screen_lifecycle.cpp` | Includes `game_session.h` (which includes `game_context.h`) |
| `src/text_client/text_protocol.cpp` | Creates `GameContext`, `set_global_context()` |
| `src/text_client/main.cpp` | Includes header (comment-only reference) |

### Stale includes (3 files include header but don't use any symbols)

| File | Notes |
|------|-------|
| `src/sdl_client/ui/help.cpp` | Include can be removed |
| `src/sdl_client/io/platform_io.cpp` | Include can be removed |
| `src/sdl_client/runtime/guy_create.cpp` | Include can be removed |

### Transitional shim (1 file)
| File | Notes |
|------|-------|
| `src/runtime/game_context.h` | Redirect to `include/openglad/runtime/game_context.h`, can be removed |

### Test files (39 files include `game_context.h`)

Usage patterns in tests:
- **`set_global_context(&custom_ctx)`**: 20+ test files create a `GameContext` with a mock RNG
- **`ctx().rng = &mock`**: ~5 test files directly swap the RNG pointer on the global
- **`ctx().mounted_campaign = "..."`**: `test_io_platform_coverage.cpp` (1 file, ~20 uses)
- **`ctx().input.players[0]...`**: `test_view_input_paths.cpp` (1 file)
- **`GlobalContextGuard` RAII helper**: Duplicated identically in 9 test files

### GameSession integration
`GameSession` owns a `GameContext ctx_` member, sets up `rng` (production or seeded), preserves `mounted_campaign` across sessions, and calls `set_global_context(&ctx_)` / `set_global_context(nullptr)` in constructor/destructor.

---

## 5. Critical Assessment of the "Real DI" Claim

The commit message claims the remaining fields provide "real DI." Here is a field-by-field verdict:

| Field | Actually injected in tests? | Different impls in prod vs test? | Verdict |
|-------|---------------------------|----------------------------------|---------|
| `rng` | **Yes** — 30+ test files | **Yes** — `ProductionRandom` vs `FixedRandom`/`SeededRandom`/custom | **Real DI** |
| `input` | No — tests write to global directly | No — always `InputState` on the singleton | **Not DI** |
| `sim_events` | No — no test swaps it | No — always default `SimEventLog` | **Not DI** (just RAII ownership) |
| `mounted_campaign` | No — tests mutate global directly | No — always a `std::string` on the singleton | **Not DI** (just global state) |

**Summary:** Only `rng` justifies the DI infrastructure (`ctx()`, `set_global_context()`, `IRandom` interface). The other three fields are passengers — they live in `GameContext` for convenience, not because they benefit from injection.

---

## 6. What Would Full Elimination Require?

### Option A: Remove GameContext entirely

This would mean finding a new home for each field:

| Field | Replacement | Difficulty |
|-------|-------------|------------|
| `rng` | Global `IRandom* g_rng` with getter/setter | Low — but loses the RAII grouping with `GameSession` |
| `input` | Global `InputState g_input` | Trivial |
| `sim_events` | Owned by `GameSession`, accessed via global pointer | Medium — lifecycle management |
| `mounted_campaign` | Global `std::string` in io module | Trivial |
| `ctx()`/`set_global_context()` | `set_global_rng()` / `get_global_rng()` | Medium — 30+ test files to update |

**Estimated changes:**
- ~60 files (18 production + 39 test + 3 header/shim)
- ~250-300 lines changed (mostly mechanical: `ctx().rng` → `global_rng()`, etc.)
- Net reduction: ~80 lines (the GameContext struct/impl themselves)
- Risk: **Medium** — the RNG injection is load-bearing for test determinism

### Option B: Keep GameContext as an RNG-only DI container

Move `input`, `sim_events`, and `mounted_campaign` to standalone globals or `GameSession`. Keep `GameContext` with just `rng` + the `ctx()`/`set_global_context()` mechanism.

**Estimated changes:**
- ~25 files
- ~100 lines changed
- Net reduction: ~30 lines
- Risk: **Low-Medium**

### Option C: Minimal cleanup (recommended)

Leave `GameContext` as-is but:
1. Remove 3 stale includes (`help.cpp`, `platform_io.cpp`, `guy_create.cpp`)
2. Remove the transitional shim at `src/runtime/game_context.h`
3. Extract the duplicated `GlobalContextGuard` from 9 test files into a shared test helper
4. Optionally move `mounted_campaign` to the IO module (it's the odd one out — an IO concern living in a runtime struct)

**Estimated changes:**
- ~15 files
- ~40 lines changed, ~50 lines removed (dedup of GlobalContextGuard)
- Net reduction: ~50 lines
- Risk: **Very low**

---

## 7. Conclusions and Recommendations

### What was accomplished
Commit `75c1828` successfully removed the purely transitional parts of `GameContext` — the three forwarding fields that duplicated globals. This was the right call and eliminated real indirection overhead.

### What remains is defensible
The remaining `GameContext` is no longer "transitional" — it has matured into a small, useful service locator. The RNG injection via `IRandom*` is genuinely valuable: it's actively used by 30+ test files to achieve deterministic simulation testing. This is a real architectural benefit, not accidental complexity.

### The honest assessment of the other three fields
`input`, `sim_events`, and `mounted_campaign` don't need to be in `GameContext`, but their presence there is harmless. They add ~20 lines to the struct definition and impose no runtime cost. Moving them elsewhere would be a lateral refactor — more churn than value.

### Recommended actions (in priority order)

1. **Do nothing major.** The 100+ line reduction estimated in the original analysis has already been mostly achieved (142 lines removed). The remaining ~80 lines of GameContext infrastructure are load-bearing for test determinism.

2. **Optional cleanup (~15 min, very low risk):**
   - Remove 3 stale `#include <openglad/runtime/game_context.h>` from `help.cpp`, `platform_io.cpp`, `guy_create.cpp`
   - Remove the transitional shim `src/runtime/game_context.h`
   - Extract `GlobalContextGuard` into a shared test utility header (dedups 9 copies)

3. **Don't pursue full elimination.** The cost (~60 file changes, medium risk to test infrastructure) vastly exceeds the benefit (~80 lines removed). The `ctx()` + `set_global_context()` mechanism is the least-invasive way to achieve injectable RNG in a codebase with pervasive global state.

### Updated line count assessment

| Category | Original Estimate | Actual |
|----------|------------------|--------|
| Commit 75c1828 (done) | 100+ lines | 142 lines removed |
| Remaining achievable cleanup | — | ~50 lines (stale includes, shim, dedup) |
| Full elimination (not recommended) | — | ~80 more lines, 60 files, medium risk |
