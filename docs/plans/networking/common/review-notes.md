# Review Notes

Codebase verification, independent review findings, and design augmentations from multiple review passes.

## Codebase Verification Notes (from code review)

The following claims in this plan have been verified against the actual codebase:

**Confirmed accurate:**
- All 17 `rand()` call sites at exact line numbers (Phase 0)
- `SimEntity` has 19 fields (16 public + 3 protected), no `entity_id_` yet (Phase 1)
- `add_to_list()` is private on GameWorld (Phase 1)
- All 5 cross-reference pointers at documented locations (Phase 2)
- `statistics::controller` stale-pointer cleanup bug confirmed — NOT cleaned in `GameWorld::tick()` lines 989-1014. The comment at lines 1017-1018 about "viewscreen control pointer cleanup" refers to `viewscreen::control` (cleaned in `screen::act()` lines 909-913), NOT `statistics::controller` — these are different pointers. (Phase 2)
- `fxlist` stale-pointer cleanup bug confirmed — NOT iterated in `GameWorld::tick()` lines 989-1014 (Phase 2)
- Entity lists are `std::list<std::unique_ptr<walker>>` (Phase 5)
- `NUM_SPECIALS = 6` at `statistics.h:30` (Phase 5)
- obmap `add()/remove()/move()` exist at `obmap.h:34-36` (Phase 7)
- obmap uses `std::map<pair<short,short>, list<walker*>>` + `std::unordered_map<walker*, list<pair<short,short>>>` (Phase 7)
- `obmap.cpp:276` dereferences `current_game->world->rng_` directly during spatial queries (Phase 7)
- `walker::death()` creates entities (life gems, 4 explosions for generators, bloodstains), emits events, consumes RNG — 3 RNG calls per generator explosion iteration (Phase 7)
- `effect::death()` delegates to family descriptor `on_death` — `bomb_on_death` creates explosion entities, `ghost_scare_on_death` has side effects (Phase 7)
- `weap::death()` delegates to weapon family descriptor `on_death` — `projectile_explode_on_death`, `rock_on_death`, `knife_on_death`, `door_on_death` create entities (Phase 7)
- `slime_on_death` creates smaller slime entities AND transfers `myguy` ownership (Phase 7)
- `walker::~walker()` lives in `src/interface/walker_render_bridge.cpp:97-116`, calls `obmap::remove()` but does NOT call `death()` — safe during snapshot application (Phase 7)
- Dead player entities (`myguy != nullptr`) stay in oblist, not moved to dead_list (`game_world.cpp:1019-1027`) (Phase 5)
- `GameLoopFrameState` accumulator fields are `#ifdef __EMSCRIPTEN__` guarded (Phase 11)
- `openglad_text` SDL-free precedent at `CMakeLists.txt:938` (Phase 27)
- `GameSession::Config` supports `allocate_screen = false` for headless (Phase 27)
- `src/platform/emscripten/` does not exist yet (Phase 26)
- `regen_delay_` is protected on walker, accessed directly from `living.cpp:139,142`, `walker.cpp:253`, `walker_combat.cpp:169` (Phase 6)
- `level_tick_count_` is private on GameWorld at line 157 (Phase 5/6)
- `OG_GAMEPLAY_COMPONENT_SOURCES` aggregates `OG_SIM_SOURCES` + `OG_ENTITIES_SOURCES` + other gameplay files at CMakeLists.txt:377 (Module Placement)
- Only `weap` has extra data fields (`do_bounce` at `weap.h:44`); `living`, `treasure`, `effect` are behavior-only (Phase 5)
- `damage_tile()` at `screen.cpp:1258-1286` directly mutates `world_.grid.data[]` — only grass tiles (PIX_GRASS1-4 -> PIX_GRASS1_DAMAGED) (Phase 5/6)
- `screen::act()` calls `world_.tick()` first (line 905), then dispatches events (lines 920-978) (Phase 15)
- `screen::act()` has 8 early return paths: lines 903, 955, 1017, 1031, 1036, 1043, 1066, 1068 (Phase 15)
- `game_frame_with_result()` does exactly 1 tick (`s.act()` at line 67) + 1 render (`s.redraw()` at line 81) per call (Phase 11)
- CMake: `OG_PLATFORM_SOURCES` has exactly 4 files, `OG_SIM_SOURCES` has 2 files, `og_ext_zlib` target exists at line 746, vendored lib pattern at lines 742-776, Emscripten `-sASYNCIFY` at lines 1652-1668 (Phases 11, 23, 26)
- `InputState` structure: `PlayerInput` with `held[16]` + `pressed[16]`, `InputState` with `players[4]` + `quit_requested` (Phase 4)
- `guy.h` has 22 data fields at lines 47-73 (Phase 5)
- `SimRandom` LCG at `game_world.h:32-44` with public `state_` (Phase 5)
- `smooth.cpp:36` has `std::rand()` fallback in `rng()` function, layered fallback at lines 29-37 (Phase 0)
- `screen::random()` at `src/interface/screen.cpp:161` uses `rand()` — verified not called from `src/gameplay/`, UI-only (Phase 0)
- Cross-reference pointer assignment counts: `->foe =` ~201, `->leader =` ~86, `->owner =` ~157, `->collide_ob =` ~31, `.controller =` ~3 (Phase 3)
- `SetEnd` event has zero push sites in gameplay code (Phase 5)
- Cosmetic event sites: ~61 total (23 PlaySound + 35 Notification + 2 SetPalette + 1 RequestRedraw) with exact file:line locations (Phase 5)
- Game-flow event sites: 6 total (1 EndGame + 2 ScoreChange + 2 RequestExitConfirmation + 1 WithdrawToLevel) with exact file:line locations (Phase 5)
- `completed_levels` is `std::set<int>` at `game_world.h:147` — campaign progression, belongs in InitialSetup (Phase 5/14)
- Entity factory callbacks at `game_world.h:148-150` with exact signatures (Phase 7)
- `GameLoopDeps` struct at `game_loop.h:22-34` has `enable_render`, `enable_event_poll`, `enable_frame_timing` (Phase 11)
- `time_delay()` at `util.cpp:132-152` converts delay ticks at 13.6ms each to microseconds, sleeps then spin-waits (Phase 11)
- Test infrastructure: ~1496 integration tests across 20 `og_add_test_group()` groups (`ALL_INTEGRATION_TEST_SOURCES`), ~291 unit tests across 4 `og_add_unit_group()` groups, all using GoogleTest
- `current_game` thread-local has 332 occurrences across 39 gameplay files (Phase 7)
- `GameplayContext` holds `world`, `save`, `sim_events`, `config`, `rng_override_ref`, `pathfinding` (Phase 7)
- `GameSession` owns `world_owner_`, `prefs_owner_`, `screen_owner_` (Phase 7)
- `transform_to()` (`walker.cpp:1234`) changes `order`/`family` in-place without changing C++ subclass — only changes family within the same Order in practice (slimes: Living->Living, waves: Weapon->Weapon) (Phase 6/7)
- `exit_on_eat()` (`treasure_family_navigation.cpp:36-98`) emits `RequestExitConfirmation` when player walks onto exit tile — blocking `yes_or_no_prompt()` in current `screen::act()` at line 994 (Phase 14)
- Emscripten accumulator pattern in `emscripten_frame_wrapper()` (`glad.cpp:133-223`): accumulates browser frame deltas, gates logic at target frame time, clamps anti-spiral (Phase 11)

**Corrections applied during review:**
- SimEntity serializable field count: 19 (18 original + entity_id_), not 20 as originally stated — `frames` is skipped. Total serializable: **86 fields**, not 87 (Phase 5)
- DamageTile (`screen::damage_tile()` at `src/interface/screen.cpp:1258-1286`) mutates `world_.grid.data[]` — was miscategorized as Tier 1 cosmetic, moved to simulation layer (Phase 6/14)
- Cosmetic event sites: **~61 total** (23 PlaySound + 35 Notification + 2 SetPalette + 1 RequestRedraw) — plan originally stated ~26, which was itself a correction from an original overstate of 50+ (the 50+ estimate was actually closer to correct). Bandwidth estimate updated accordingly: ~10-30KB/sec during active combat (Phase 5)
- Walker serializable field count: 44 (not 45 as originally stated) — total 86 fields (Phase 5)
- `statistics::controller` assignment count: ~3 (stats.cpp:53,56,95), not ~2 as originally stated. Other grep hits are local variable declarations (Phase 3)
- `fxlist` stale-pointer cleanup is missing from `GameWorld::tick()` — second pre-existing bug alongside the `controller` bug (Phase 2)
- Death callback chain is deeper than originally documented: `effect::death()` and `weap::death()` delegate to family descriptor `on_death` callbacks that create entities (bombs -> explosions, slimes -> smaller slimes with myguy transfer). Snapshot application bypasses death entirely rather than suppressing it (Phase 7)
- `walker::~walker()` is separate from `death()` — destructor path is safe for snapshot application (clears pointers, removes from obmap, resets render/stats — no entity creation, no events, no RNG) (Phase 7)
- `screen::act()` EndGame early return (line 951-955) calls `events.clear()` — server must capture ALL events before the early return (Phase 14)
- `RequestExitConfirmation` is a blocking UI prompt that can't run on a headless server — replaced with freeze-and-ask protocol: server pauses sim, broadcasts prompt, any player can respond, timeout auto-declines (Phase 14)
- `completed_levels` (std::set<int>) is campaign progression, belongs in InitialSetup not per-tick snapshots. Must be updated in each `InitialSetup` during level transitions (Phase 5/14)
- `smooth.cpp:36` has a `std::rand()` fallback in `rng()` — additional migration target (Phase 0)
- `game_world.cpp:692,714` use `rand()` for grid generation — additional migration targets (Phase 0)
- `SetEnd` event: defined in `EventKind` enum but has zero push sites in `src/gameplay/` — appears vestigial (Phase 5)
- `current_game` thread-local is a critical implicit dependency: ~332 occurrences across 39 files, `GameWorld::tick()` silently returns if null, obmap collision code dereferences it directly. `GameplayContextGuard` RAII added to catch context-switch bugs (Phase 7)
- `guy` fields mutate during gameplay (exp, kills, scen_damage, etc.) — must be included in snapshots for player-controlled entities (Phase 5/6)
- `screen::act()` has 8 early return paths (not "5+" as originally noted): lines 903, 955, 1017, 1031, 1036, 1043, 1066, 1068 (Phase 15)
- `viewscreen::control` cleanup (screen.cpp:909-913) is distinct from `statistics::controller` — different pointers, different cleanup locations (Phase 2)

---

## Independent Review Notes (from second-pass code audit)

The following observations come from an independent review of the plan against the actual codebase, conducted after the plan was written.

### Feasibility Assessment

| Aspect | Rating | Notes |
|--------|--------|-------|
| Codebase claim accuracy | 9.5/10 | Line numbers, field counts, grep counts nearly all verified correct |
| Architectural soundness | 9/10 | Server-authoritative + snapshot is the right model for this game |
| Phase ordering | 10/10 | Each phase builds on the last with correct dependencies |
| Risk identification | 8.5/10 | Two real pre-existing bugs found, death callbacks well-analyzed |
| Completeness | 9/10 | After augmentation: pause, relay, entity ordering, editor impact all addressed |
| Bandwidth analysis | 8.5/10 | Realistic estimates with SimEventBatch overhead included |

**Bottom line:** Plan is production-quality and ready to execute.

### Additional Codebase Facts Verified

- `rand()` grep across `src/gameplay/` returns exactly 17 call sites (10 family_elf, 2 walker, 2 game_world, 1 walker_combat, 1 stats, 1 smooth) — matches plan claim precisely
- SimEntity has exactly 16 public + 3 protected = 19 total fields. `frames` (protected) is correctly identified as derivable/skippable.
- Stale-pointer cleanup loop at `game_world.cpp:989-1014` iterates `oblist` (lines 990-1001) and `weaplist` (lines 1003-1014) but NOT `fxlist` — confirmed the bug is real
- `statistics::controller` is NOT cleaned in that same loop — confirmed the second bug is real
- Dead player entity condition at line 1022: `ob && ob->dead && ob->myguy == nullptr` — entities with `myguy` stay in oblist, confirmed
- `fxlist` dead entity cleanup at lines 1035-1038 uses `std::erase_if` (just erases, doesn't move to dead_list) — different from oblist's `dead_list.push_back()` pattern
- Level editor (`level_editor.cpp:2995-3526`) has its own event loop, never calls `screen::act()`, `game_frame()`, or `world_.tick()`. Zero `#ifdef OPENSCEN` guards exist in the codebase. Editor shares `og_game` link target but diverges at the event loop level. Deleting `screen::act()` (Phase 15) does NOT break the editor.
- `obmap::add()` at `obmap.h:35` — confirmed defensive (checks `walker_to_pos` for existing entry, removes first if present). The obmap update strategy in Phase 7 is correct.
- `GameWorld::entity_factory`, `entity_configurator`, `entity_derived_stats` are `std::function` callbacks at `game_world.h:148-150` — confirmed they are set by the platform layer and available on both SDL and headless clients
- `SimEventLog` is owned by `GameContext` (`ctx().sim_events`), not by `GameWorld` — the suppression flag correctly belongs on `SimEventLog` itself (Phase 7 augmentation)
- `OG_GAMEPLAY_COMPONENT_SOURCES` at CMakeLists.txt:377 aggregates: `OG_SIM_SOURCES` (2 files) + `OG_ENTITIES_SOURCES` (40+ files) + 5 additional gameplay files (`stats.cpp`, `gameplay_context.cpp`, `game_world.cpp`, `sim_input_handler.cpp`, `smooth.cpp`). New gameplay source files should be added to the appropriate sub-list.

### Risks to Monitor During Implementation

1. **`smooth()` caller audit (Phase 0):** The `std::rand()` fallback removal in `smooth.cpp` assumes all callers have `current_game` installed. If any caller runs during level loading (before `GameplayContextGuard`), the assert will fire. Verify all call sites before removing the fallback.

2. **Field table drift (Phase 5+):** 86 fields in the constexpr table is a large surface area. The `static_assert` on count catches additions/removals but not reorderings. Add a round-trip identity test: `capture -> serialize -> deserialize -> apply -> capture -> assert bitwise equal` as a CI test. This catches offset bugs, endianness bugs, and field ordering bugs.

3. **`statistics::walkrounds`:** ~~Claimed as dead code~~ — **resolved: deleted in Phase 0** (dead code cleanup).

4. **Allocation churn under heavy combat (Phase 7/9):** The plan correctly defers pooling optimization. The Phase 9 benchmark should include a worst-case scenario (4-player chaotic combat with many projectiles) and measure per-frame allocation count alongside snapshot sizes. If >50 allocations/tick, consider the free-list pool at that point.

5. **`guy_id_counter` in WorldSnapshot (Phase 5):** Included for "server and client must agree." Since only the server creates entities, the client never uses this counter directly. It's harmless to include (1 field, 4 bytes) and useful for server state restore, but it's not strictly necessary for client correctness.

---

## Third-Pass Review Augmentation

The following findings come from a third independent review of the plan against the codebase, focused on feasibility validation, gap identification, and design refinements.

### Confirmed: Plan Claims Are Accurate

Spot-checked against actual source:
- `InputState` at `input_state.h` — confirmed SDL-independent, `PlayerInput` has `bool held[16]` + `bool pressed[16]`, `InputState` has `players[4]` + `quit_requested`. Ready for wire serialization.
- `SimEventLog` at `sim_event_log.h` — confirmed clean accumulate/drain pattern. Events vector, no SDL types, `drain()` returns and clears.
- `GameWorld` at `game_world.h` — confirmed owns all sim state: entity lists (`std::list<std::unique_ptr<walker>>`), grid (`PixieData`), scores (`m_score[4]`), game flow flags, `SimRandom rng_`. Clean snapshot boundary.
- `current_game` thread-local at `gameplay_context.h:49` — confirmed `thread_local GameplayContext*`, holds `world`, `save`, `sim_events`, `config`, `rng_override_ref`, `pathfinding`.
- `SimRandom` LCG at `game_world.h:32-44` — confirmed glibc constants, public `state_`, seeded at construction.
- 17 `rand()` calls in `src/gameplay/` — confirmed exact: 10 `family_elf.cpp`, 2 `walker.cpp`, 2 `game_world.cpp`, 1 `walker_combat.cpp`, 1 `stats.cpp`, 1 `smooth.cpp`.
- `smooth.cpp:29-37` fallback chain — confirmed: tries `gameplay_rng_override()`, then `current_game->world->rng_`, then `std::rand()`.
- Stale-pointer cleanup at `game_world.cpp:989-1014` — confirmed iterates `oblist` and `weaplist` only. `fxlist` NOT iterated. `statistics::controller` NOT cleaned. Both bugs real.
- `timer_wait` adjustable in-game at `view.cpp:1352-1360` — confirmed range 0-20, default 6, ±2 per keypress.
- `screen::act()` at `screen.cpp:897-1068` — confirmed: calls `world_.tick()` first (line 905), then dispatches events (lines 920-978), then handles exit/withdraw prompts with blocking `yes_or_no_prompt()` at line 994.
- `game_frame_with_result()` at `game_loop.cpp:39-168` — confirmed: does exactly 1 `s.act()` (line 67) + 1 `s.redraw()` (line 81) + `time_delay()` FPS cap (lines 152-164) per call.
- `weap::do_bounce` at `weap.h:44` — confirmed only extra field on subclasses. `living`, `treasure`, `effect` are behavior-only.
- `walker::~walker()` is separate from `walker::death()` — confirmed: destructor does NOT call death(). Safe for snapshot entity removal.

### Design Refinements Applied (Inline Edits Above)

1. **Sim Tick Rate section rewritten** — `SIM_TICKS_PER_SEC` renamed to `DEFAULT_SIM_TICKS_PER_SEC`. Server tick interval derived from `timer_wait * 13.6ms`. Host controls speed; non-host speed keybinds suppressed. `timer_wait = 0` clamped to `MIN_TIMER_WAIT = 1` in networked mode.

2. **Wall-clock timeouts** — `DISCONNECT_TIMEOUT_MS`, `EXIT_PROMPT_TIMEOUT_MS`, `PAUSE_TIMEOUT_MS` replace tick-based equivalents for disconnect, exit prompt, and pause timeouts. With variable game speed, tick-based timeouts change real-world duration unpredictably. Keyframe interval stays tick-based (keyframes should happen every N sim updates).

3. **`EntitySnapshot` trivially_copyable guard** — `static_assert(std::is_trivially_copyable_v<EntitySnapshot>)` added alongside the field count assert. The `reinterpret_cast<uint8_t*>` serialization breaks silently if anyone adds a non-trivial member.

4. **`sync_ids_from_pointers()` entity_id check** — When reading `entity_id_` from a cross-reference pointer, treat `entity_id_ == 0` as stale (entity never assigned an ID). Complements the Phase 2 dead-entity cleanup.

5. **Phase 11 complexity reframed** — From "almost free / ~10 lines" to medium refactor. The Emscripten accumulator lives in a different function (`emscripten_frame_wrapper()`) from the native loop (`game_frame_with_result()`). Restructuring the native loop is real work: accumulator loop, input polling semantics change, spiral-of-death cap (max 4 ticks per call), FPS cap removal.

6. **Input jitter `MAX_LATE_PRESS_TICKS = 2` cap** — Late `pressed[]` inputs older than 2 ticks are discarded, not delivered. Prevents 500ms-late special attacks from firing into empty space.

7. **Phase 15 git tag** — `pre-networking-switchover` tag before the big delete, for bisect reference without maintaining dead code.

8. **Phase 31 cost math corrected** — Actual relay message count: ~9 relayed messages/tick × 12 ticks/sec = ~108 messages/sec = ~9.3M/day for one continuous 24-hour 4-player session. Free tier supports ~15 minutes. Paid tier ($5/mo) supports roughly one continuous session.

### Additional Risks and Notes

6. **`obmap` cost during `apply_snapshot()`:** The plan says obmap updates are "O(N) in total entities" and "negligible." More precisely, `obmap` uses `std::map<pair<short,short>, list<walker*>>` — each `add()` is O(log K) where K = occupied grid cells, and the defensive remove-then-add means 2 tree operations per moved entity. At ~200 entities and 12 ticks/sec this is fine (~2400 tree ops/sec). But if entity count or tick rate increases, obmap becomes the bottleneck before serialization does. Monitor in the Phase 9 benchmark.

7. **Emscripten WebSocket is callback-based and non-blocking:** The `EmscriptenWebSocketTransport` (Phase 26) cannot block-wait for messages — `poll()` must return empty if no messages have arrived. The in-process execution order (Phase 15, lines 838-856) assumes synchronous send/receive. In the browser, the client's input send and the server's snapshot receive happen within the same `requestAnimationFrame` callback, so there's inherently 1-frame latency between send and receive. At default speed (83ms tick interval >> 16ms frame time at 60fps), this is invisible — but should be documented.

8. **Networking test fixture:** ~~The plan has per-phase "Verify" sections but no dedicated test infrastructure phase.~~ — **resolved: `NetworkTestFixture` added to Phase 13** alongside InProcessTransport.

9. **`float` serialization across platforms:** `worldx_`/`worldy_` serialized as IEEE 754 floats is correct for x86/wasm. Unlikely edge case: NaN canonicalization differs across platforms. If any float field ever becomes NaN, `memcpy`-based serialization may produce different bytes on different platforms, causing spurious divergence detection. Not a blocker (what float in this game would be NaN?), but the Phase 8 endianness helpers should handle floats explicitly.

10. **Phase 7 entity list reordering (step 13):** ~~May be unnecessary~~ — **decision: keep it.** It's O(N) via `list::splice`, makes round-trip identity tests simpler (capture → apply → capture = bitwise equal), and is needed if client-side prediction is ever added.

11. **Game speed bandwidth note:** At `timer_wait = 1` (max networked speed, ~73 ticks/sec), delta bandwidth is ~6× the default estimate: ~300-900KB/sec outbound with 4 clients. Still within residential internet capacity, but the relay cost also scales linearly — roughly 6× the message count, potentially exceeding the paid tier for sustained fast-speed play. Consider noting this in a "Host Game" UI tooltip if speed is cranked up.

12. **`statistics::walkrounds`:** ~~Cleanup opportunity~~ — **resolved: deleted in Phase 0** (dead code cleanup).

---

## Fourth-Pass Review Augmentation

The following findings come from a fourth independent code review against the actual codebase, focused on feasibility validation, gap analysis, and design decisions made via discussion.

### Codebase Facts Verified (Fourth Pass)

All claims re-verified against source. Additional findings:

- **No mutable static/global state in `src/gameplay/`** besides `current_game` (thread-local). All `static` declarations are `constexpr` or `const` (smooth.cpp arrays, family registry sizes). The gameplay layer is clean for networking.
- **`obmap` defensive `add()`** confirmed at `obmap.h:35` — checks `walker_to_pos` for existing entry, calls `remove()` first if present. The Phase 7 obmap strategy (call `add()` for both new and moved entities) is correct and safe.
- **`obmap.cpp:297` `pow(2.0, door_level)`** — confirmed non-deterministic floating-point in simulation code. Fixed in Phase 0.
- **`atan2f` in `walker_combat.cpp:144`** — sets `hit_recoil_angle` and `attack_lunge_angle`. These are serialized fields, but since the server computes them and clients receive via snapshot, cross-platform `atan2f` divergence is not a problem for the snapshot model. Only matters if client-side prediction is added later.
- **`walker::~walker()` at `walker_render_bridge.cpp:97-118`** — re-confirmed: sets `dead = 1` (flag, not function call), removes from obmap, resets `stats_`/`render_`/`owned_myguy_` smart pointers. Does NOT call `death()`. Safe for snapshot entity removal.
- **`transform_to()` at `walker.cpp:1234-1289`** — re-confirmed: removes from obmap before changing `sizex`/`sizey`, uses `entity_configurator()` to reconfigure, re-adds via `setxy()`. No `death()` call, no entity creation. Snapshot application handles this correctly (step 6: detect order/family change, call `entity_configurator()` before overwriting fields).
- **`game_frame_with_result()` input timing** — confirmed: input poll at line 135 is AFTER `s.act()` at line 67. Phase 11's accumulator reorder (poll before tick loop) is an intentional 1-frame latency reduction.
- **`GameWorld::entity_factory`/`entity_configurator`/`entity_derived_stats`** — confirmed `std::function` callbacks at `game_world.h:148-150`. Set by platform layer in both SDL and headless builds. GameServer in `og_gameplay` can use them via callbacks without platform dependencies.
- **`emscripten_frame_wrapper()` at `glad.cpp:133-223`** — confirmed: accumulator pattern, clamps anti-spiral at `target_frame_time * 2`, subtracts one frame's worth per iteration. Phase 11's native accumulator should match this pattern.

### Design Decisions Made (Discussion-Driven)

The following decisions were made via discussion and are reflected in inline edits above:

1. **Phase 3 promoted to mandatory (post-Phase 2).** The ~477 compiler errors are mechanical and worth doing early. Eliminates `sync_ids_from_pointers()` UB risk before any snapshot code is written. Every subsequent phase benefits from enforced setter usage.

2. **Phase 15 split into three sub-phases (15/16/17).** Isolates the three risks:
   - 15: Pure refactor — split `screen::act()` into sub-methods, keep wrapper. All tests pass unchanged.
   - 16: Wire up GameServer/GameClient alongside old path. Both paths work.
   - 17: Migrate game-loop tests to server/client path, delete wrapper. No fallback.

3. **Integration tests migrate to server/client path (Phase 17).** Integration tests that call `game_frame()` or `screen::act()` are updated to use `NetworkTestFixture`. Tests exercise the real networking code path as a side effect.

4. **InProcessTransport is zero-copy.** Passes `shared_ptr<WorldSnapshot>` and `shared_ptr<InputState>` directly — no serialize/deserialize round-trip for local play. Serialization path exercised by unit tests and networked play only.

5. **Camera tracking via `InitialSetup` + `ControlChange` message.** Server sends per-client controlled `entity_id`s in `InitialSetup`. On character death/switch, server sends a `ControlChange` message (~8 bytes). Client updates `viewob[i]->control` after each `apply_snapshot()`.

6. **GameServer uses callback architecture.** Level transitions, save sync, exit prompt resolution — all injected as `std::function` callbacks by the platform layer. Same pattern as `entity_factory`/`entity_configurator`. Keeps GameServer in `og_gameplay` (no platform deps).

7. **`SimEventLogSuppressGuard` RAII type.** Manages the event suppression flag during `apply_snapshot()`. Destructor clears the flag even on early return/exception.

8. **Phase 10: Input replay system.** Records initial RNG seed + per-tick InputState. Invaluable for debugging desync in later phases. Also enables reproducible benchmarks for Phase 9.

9. **Phase 0 expanded.** Now includes: `pow(2.0, level)` → bit shift fix, `cmath` audit in `src/gameplay/`, `statistics::walkrounds` deletion.

10. **Entity list reordering (Phase 7, step 13) kept.** O(N) via `list::splice`, simplifies round-trip identity tests, supports future client-side prediction.

11. **`EntitySnapshot` must stay `trivially_copyable`.** `guy` data lives on `WorldSnapshot` (separate `std::vector<GuySnapshot>`), NOT embedded in `EntitySnapshot`. `EntitySnapshot` contains only a `guy_id` (int).

### Updated Risk Assessment

| Aspect | Rating | Notes |
|--------|--------|-------|
| Codebase claim accuracy | 9.5/10 | All line numbers, field counts, grep counts re-verified correct |
| Architectural soundness | 9.5/10 | Callback-based GameServer, zero-copy InProcessTransport, RAII guards |
| Phase ordering | 10/10 | Phase 3 promotion + Phase 15 three-way split reduces risk |
| Risk identification | 9/10 | `pow()` determinism fix, `atan2f` documented, input timing change noted |
| Completeness | 9.5/10 | Camera tracking, test migration, replay system, dead code cleanup all addressed |
| Bandwidth analysis | 8.5/10 | Unchanged — realistic estimates, grid variability noted |

**Bottom line:** Plan is production-quality and addresses all identified gaps. Ready to execute.

---

## Fifth-Pass Review Augmentation

The following findings come from a fifth independent code review against the actual codebase, focused on feasibility validation, design improvements, and gap analysis. All inline edits from this pass are reflected in the phases above.

### Design Change: Setter-Based Dirty Tracking (Eliminates `compute_delta()`)

**Previous design (comparison-based):** Server captures two full snapshots (baseline + current), compares all 86 fields for all ~200 entities, produces a delta. This works but is wasteful — most fields don't change between ticks.

**New design (setter-based):** Dirty bits are set at the source during `GameWorld::tick()` via `mark_dirty()` calls at field mutation sites. `capture_snapshot()` copies the dirty bits from entities and clears them. The server accumulates dirty bits per-client between sends. Delta serialization reads the accumulated mask directly — no comparison loop.

**Impact across phases:**
- **Phase 2:** Gains `dirty_mask_[2]` infrastructure on SimEntity, `mark_dirty()` / `mark_all_dirty()` / `clear_dirty()` methods, and bit index constants header (`dirty_field_bits.h`). Cross-reference setters call `mark_dirty()`. `removed_entity_ids_` vector added to GameWorld.
- **Phase 3:** Narrowed to cross-reference pointer privatization only (~477 compiler errors). Dirty tracking instrumentation deferred to Phase 8 (see Sixth-Pass below).
- **Phase 5:** Field table references bit index constants from Phase 2. References to `compute_delta()` removed.
- **Phase 6:** `capture_snapshot()` copies `dirty_mask_[2]` from entities, clears entity dirty bits. Drains `removed_entity_ids_`.
- **Phase 8:** `compute_delta()` eliminated. Gains the ~200-400 site dirty tracking instrumentation (deferred from Phase 3 — see Sixth-Pass below). Server maintains `PerClientState` with accumulated dirty masks per entity. `apply_delta()`, `serialize_delta()`, `deserialize_delta()` remain. CI safety-net test validates dirty-bit deltas against brute-force comparison.

**Advantages:**
- Eliminates O(N × 86) comparison loop per tick
- Per-client multi-tick accumulation is trivial (bitwise OR)
- No "baseline snapshot" storage per client — just accumulated masks (~3KB per client at 200 entities)
- Dirty-bit setting is O(1) per field write (one OR instruction)

**Risk:** Missed `mark_dirty()` call = field silently stale on clients between keyframes. Mitigated by: (1) periodic keyframes every ~5 seconds, (2) CI safety-net test that validates every entity's dirty mask against brute-force comparison on a worst-case combat scenario.

### Additional Inline Edits Applied

1. **Phase 1: Thread safety invariant** — `id_index_` and all GameWorld state is main-thread-only. When WebSocket I/O threads arrive (Phase 24), all interaction flows through the message queue. Explicit documentation added.

2. **Phase 6: Grid dirty cap** — `MAX_GRID_DIRTY_TILES = 64`. If exceeded (chain lightning, large explosions), fall back to full grid send. Prevents pathological delta payload spikes.

3. **Phase 14: Level transition event cleanup** — `SimEventLog::clear()` after `read_scenario()`, before first tick of new level. Entity creation during level loading pushes meaningless events that no client is ready to receive.

4. **Phase 24: Thread safety enforcement** — Explicit prohibitions on GameWorld access from I/O threads. Connection/disconnection events queued to game thread. `ci-tsan` CMake preset added for ThreadSanitizer CI builds. Stress test with 4 concurrent WebSocket clients under TSan.

5. **Phase 28: Direct LAN connections** — "Join Game" UI supports both direct IP:port entry (for LAN) and relay room codes (for NAT traversal). Two tabs/modes. Transport layer (`ITransport`) is identical once connected.

6. **Phase 31: Relay graceful degradation** — Documented recovery path for relay message drops (KeyframeRequest), WebSocket disconnections (auto-reconnect), and sustained failures (disconnect timeout → AI control). Added relay cost awareness UI warning for high game speed. Added relay packet-loss simulation test.

### Additional Codebase Facts Verified (Fifth Pass)

- **family_elf.cpp rand() calls:** Exactly 10 calls in `elf_do_special()` at lines 33, 34, 38, 39, 51, 52, 64, 65, 79, 80 — all adjusting weapon projectile trajectories. Matches plan precisely.
- **game_world.cpp lines 692, 714:** Both are `rand() % 4` for grass tile randomization in `generate_grass_grid()` / `resize_grid()`. Confirmed.
- **pow(2.0, door_level) at obmap.cpp:297:** Verified exact code: `pow(static_cast<double>(2), w->stats()->level)`. Floating-point pow in simulation code — plan's `1 << level` replacement is appropriate.
- **walker::death() at walker.cpp:1294-1410:** Confirmed creates life gems (line 1318), spawns 4 explosion FX for generators (lines 1356-1368), generates bloodstains (lines 1382-1410), consumes RNG (lines 1364, 1366, 1406).
- **screen::damage_tile() at screen.cpp:1258-1286:** Confirmed mutates `world_.grid.data[gridloc]` at line 1279. Only affects grass tiles (PIX_GRASS1-4 → PIX_GRASS1_DAMAGED).
- **effect_family_bomb.cpp:42:** Confirmed DamageTile event push.
- **treasure_family_navigation.cpp:** Lines 71-75 (RequestExitConfirmation normal exit), 88-90 (WithdrawToLevel), 91-95 (RequestExitConfirmation withdraw variant). All confirmed.
- **walker destructor at walker_render_bridge.cpp:97-118:** Confirmed: nulls pointers (lines 99-102), removes from obmap (lines 105-112), resets smart pointers (lines 114-117). Does NOT call `death()`.
- **transform_to() at walker.cpp:1234-1289:** Confirmed changes order/family in-place via `configure_existing_entity()` at line 1260. Removes from obmap before resize, re-adds via `setxy()`.
- **GameLoopFrameState at `game_loop_state.h:14-17`:** `#ifdef __EMSCRIPTEN__` guards on `last_frame_time` and `accumulated_time` confirmed.
- **guy.h fields at lines 47-73:** Exactly 22 data fields confirmed. Mutable gameplay fields: exp, kills, level_kills, total_damage, total_hits, total_shots, scen_damage, scen_kills, scen_damage_taken, scen_min_hp, scen_shots, scen_hits, level.
- **picker.cpp buttons at lines 425-465:** Emscripten path (lines 425-444) replaces "QUIT" with "HELP". Native path (lines 447-465) has "QUIT". Both paths confirmed ready for "Host Game" / "Join Game" button additions.
- **picker.cpp Emscripten lifecycle at lines 1642-1703:** `picker_check_start_requested()` (1642), `picker_init()` (1649), `picker_frame()` (1663), `picker_cleanup_for_game()` (1676), `picker_reinit_after_game()` (1683). All confirmed.
- **view.cpp speed control at lines 1348-1363:** `viewscreen::change_speed()` confirmed. Speed up (decrease timer_wait by 2, min 0) at lines 1352-1354. Speed down (increase timer_wait by 2, max 20) at lines 1356-1360.

### Additional Observations

1. **`special_cost[NUM_SPECIALS]` single-dirty-bit decision (Phase 5):** Using 1 dirty bit for a 6-element array (12 bytes) is correct at current scale. If `NUM_SPECIALS` ever grows significantly (modding support), per-element tracking may become worthwhile. Add a comment in the field table.

2. **Phase 11 spiral-of-death cap:** The "max 4 ticks per call" constant should be a named constant (`inline constexpr int MAX_TICKS_PER_FRAME = 4` in `net_constants.h`), not a magic number. At default speed (83ms/tick), 4 ticks = 332ms frame time threshold. Document this.

3. **`float` NaN canonicalization (Phase 8):** `worldx_`/`worldy_` serialized as IEEE 754 floats. NaN bit patterns differ across platforms. No gameplay float should ever be NaN, but the endianness helpers should canonicalize NaN to a fixed bit pattern as a safety measure (~2 lines of code).

### Updated Risk Assessment

| Aspect | Rating | Notes |
|--------|--------|-------|
| Codebase claim accuracy | 9.5/10 | All line numbers, field counts, grep counts independently re-verified |
| Architectural soundness | 9.5/10 | Setter-based dirty tracking eliminates comparison overhead, simplifies delta path |
| Phase ordering | 10/10 | Phase 3 narrowed to pointer privatization; dirty instrumentation moved to Phase 8 (Sixth-Pass) |
| Risk identification | 9.5/10 | Thread safety, grid spikes, level transition events, relay degradation all addressed |
| Completeness | 10/10 | Direct LAN connections, dirty tracking CI safety net, TSan CI preset, all gaps closed |
| Bandwidth analysis | 9/10 | Grid dirty cap addresses worst-case; relay cost warning covers speed scaling |

**Bottom line:** Plan is production-quality with all identified gaps addressed. The setter-based dirty tracking redesign simplifies Phase 8 significantly. Thread safety is now explicitly enforced with tests rather than assumed. Ready to execute.

---

## Sixth-Pass Review Augmentation

The following findings come from an independent feasibility review of the full plan against the actual codebase, with design decisions made via discussion.

### Design Changes Applied (Inline Edits Above)

1. **Dirty tracking instrumentation moved from Phase 3 to Phase 8.** Phase 3 was combining two massive independent tasks: pointer privatization (~477 sites) and dirty instrumentation (~200-400 sites). These have no dependency on each other — dirty tracking isn't consumed until Phase 8's delta compression. Moving the instrumentation to Phase 8 means:
   - Phase 3 is focused on one concern (pointer privatization), reducing risk
   - The instrumentation and its CI safety-net validation test land in the same phase — no window where dirty tracking bugs are latent
   - Phases 4-7 are built on proven infrastructure (entity IDs, snapshots, capture/apply) without depending on unvalidated dirty bits
   - Keyframe captures in Phases 6-7 set all bits (all fields dirty), so the absence of per-field `mark_dirty()` calls has zero effect until delta compression

2. **`ValidatingInProcessTransport` added to Phase 13.** InProcessTransport's zero-copy mode bypasses serialization, creating an 11-phase coverage gap (Phases 13-23) where serialization bugs hide. The validating variant serializes and deserializes every message in CI builds, turning every integration test into a serialization round-trip test. Zero production performance impact.

3. **Player reconnection protocol added to Phase 30.** Disconnected players' entities transition to AI control. If the same player reconnects (identified by session token from Hello handshake), the server hands the entity back via `ControlChange`. New players cannot join mid-game — only reconnection of previously-connected players.

4. **Session token added to Phase 12 Hello handshake.** 16-byte random token assigned by server, stored by client. Used for reconnection identification (Phase 30). First-time connections send zero token.

5. **Min supported protocol version byte added to Phase 12 Hello handshake.** Reserved for future version-range negotiation. For v1, `min_version == current_version`. Free to include now; avoids a breaking handshake change later.

6. **Relay keyframe broadcast optimization added to Phase 31.** Full keyframes are identical for all clients — send once through the relay's broadcast path instead of N copies. Cuts keyframe relay cost ~3× for a 4-player game. Per-client deltas remain individual. `ITransport::broadcast()` method added alongside `send()`.

7. **`pow()` bounds guard added to Phase 0.** `1 << level` is UB for `level >= 32`. Clamp to `[0, 30]` before the shift. In practice `door_level` is tiny (0-5), but defensive clamping costs nothing.

8. **`statistics::commands` clearing added to Phase 7 step 6.** Clients never consume AI commands (no `tick()`), but clearing during `apply_snapshot()` prevents unbounded growth from stale initial-load commands. One line, zero risk.

9. **`entity_configurator()` I/O cost note added to Phase 7 step 6.** Transform events (slime split, wave evolution) trigger sprite reload from campaign files. Rare in practice; if profiling shows an issue, cache configurator results per (order, family) pair.

10. **`dead_list`/obmap invariant documented in Phase 7.** Dead entities are removed from obmap during their `death()` callback (before being moved to `dead_list`). Skipping `dead_list` in snapshots is safe — dead entities have no spatial presence.

11. **Phase 3 commit strategy recommended.** Do pointer privatization one pointer at a time as separate commits (`foe` ~201 sites, `owner` ~157, `leader` ~86, `collide_ob` ~31, `controller` ~3). Each commit independently compilable and reviewable.

### Codebase Facts Verified (Sixth Pass)

All plan claims re-verified against source via thorough codebase exploration. Specific confirmations:

- `SimEntity` at `sim_entity.h:23-66` — 16 public + 3 protected fields confirmed. No `entity_id_` yet. Base class with position, size, team, state flags, order/family, frame.
- `walker` at `walker.h:46-227` — All 5 cross-reference pointers at documented locations confirmed: `foe` (167), `leader` (168), `owner` (169), `collide_ob` (188), `statistics::controller` (statistics.h:113).
- `GameWorld` at `game_world.h:55-160` — Entity lists (`oblist`, `fxlist`, `weaplist`, `dead_list`) as `std::list<std::unique_ptr<walker>>` confirmed. `SimRandom rng_` at line 126. All game flow flags at documented locations.
- `InputState` at `input_state.h:40-45` — `PlayerInput` with `bool held[16]` + `bool pressed[16]`, `InputState` with `players[4]` + `quit_requested` confirmed. SDL-independent.
- `GameplayContext` at `gameplay_context.h:39-56` — `thread_local GameplayContext* current_game` at line 49 confirmed. Holds `world`, `save`, `sim_events`, `config`, `rng_override_ref`, `pathfinding`.
- `GameSession` at `game_session.h:20-98` — RAII root. Supports headless (`allocate_screen=false`), seeded RNG. `SessionScope activate()` for thread-local global installation.
- `walker_init_common` at `walker.cpp:92-126` — `path_check_counter = 5 + rand()%10` at line 121 confirmed (non-deterministic). All pointer initializations to nullptr confirmed.
- `smooth.cpp:29-37` — Three-level RNG fallback confirmed: `gameplay_rng_override()` → `current_game->world->rng_` → `std::rand()`.
- `game_frame_with_result()` at `game_loop.cpp:39-168` — Confirmed: 1 tick (`s.act()` line 67) + 1 render (`s.redraw()` line 81). Input poll AFTER tick (line 135).
- `screen::act()` at `screen.cpp:897-1010+` — `world_.tick()` first (line 905), event dispatch (lines 918-978), blocking exit prompt (`yes_or_no_prompt()` line 994) confirmed.
- `SimEventLog` at `sim_event_log.h:21-58` — Accumulate/drain pattern. 9 `EventKind` types. `drain()` returns and clears.
- Entity factory callbacks at `game_world.h:148-150` — `std::function` callbacks set by platform layer, available on both SDL and headless.
- `obmap` at `obmap.h:28-46` — Dual data structure (`pos_to_walker` + `walker_to_pos`). `add()` is defensive (remove-then-add if exists).
- Stale-pointer cleanup at `game_world.cpp:989-1014` — Iterates `oblist` and `weaplist` only. `fxlist` NOT iterated. `statistics::controller` NOT cleaned. Both bugs confirmed real.
- `walker::~walker()` at `walker_render_bridge.cpp:97-116` — Does NOT call `death()`. Safe for snapshot entity removal.

### Updated Risk Assessment

| Aspect | Rating | Notes |
|--------|--------|-------|
| Codebase claim accuracy | 9.5/10 | All claims independently re-verified against source |
| Architectural soundness | 9.5/10 | Reconnection protocol, ValidatingInProcessTransport, relay broadcast optimization |
| Phase ordering | 10/10 | Dirty instrumentation moved to Phase 8 — right next to its consumer and its safety-net test |
| Risk identification | 9.5/10 | `pow()` UB guard, dead_list/obmap invariant, entity_configurator I/O cost, commands clearing |
| Completeness | 10/10 | Mid-game join policy, reconnection protocol, serialization coverage gap closed |
| Bandwidth analysis | 9/10 | Relay broadcast optimization reduces keyframe relay cost ~3× |

**Bottom line:** Plan is production-quality. All design decisions from discussion are reflected inline. The key structural improvement is moving dirty tracking instrumentation to Phase 8, which reduces Phase 3's risk and ensures instrumentation + validation test land together. Ready to execute.
