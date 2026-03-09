# OpenGlad Networking Implementation Plan

## Context

OpenGlad is a top-down action RPG with 4-player split-screen. The goal is to add networked multiplayer in incremental phases, starting with an in-process server/client architecture and culminating in remote play over WebSockets.

The codebase is well-positioned: `InputState` is SDL-independent (4 players x 16 bools x 2 arrays), `SimEventLog` decouples simulation from platform side effects, and `GameSession` already supports concurrent sessions (de-singletonized in PR #105). `GameWorld` has a deterministic `SimRandom` LCG RNG, but ~17 call sites in gameplay code still use C stdlib `rand()` — Phase 0 fixes this before anything else.

### Key Decisions
- **Server-authoritative** model (server runs sim, broadcasts state)
- **No client-side prediction** initially — clients display the latest server snapshot as-is. At 12 ticks/sec (83ms/tick) + network RTT, remote players will experience ~150-250ms input lag. This is acceptable for LAN and tolerable for internet play in a top-down action game, but noticeable for twitch gameplay. Client-side prediction (speculatively apply own input, rollback on server correction) is a natural follow-up optimization after the base architecture is proven — the snapshot capture/apply infrastructure built here directly supports rollback.
- **Full state snapshots** with **delta compression** (only changed fields per entity) + zlib on top
- **Decouple sim tick from render frame** rate
- **WebSockets** for transport (works in browsers too)
- **IXWebSocket** (vendored, no TLS) for native; Emscripten built-in API for web
- **First-client-is-host** for headless servers
- **Protocol version byte** in every message header from day one
- **Manual binary serialization** driven by the `constexpr` field descriptor table (Phase 5). No external serialization library — the field table makes generic ser/deser trivial (~200 lines), avoids vendoring ~15K LOC of msgpack headers, and keeps endianness handling explicit and debuggable.
- **Campaign file parity** — all clients must have the same campaign data files locally. The server does not transmit level data; clients call `read_scenario()` from local files. A campaign content hash is exchanged during the `Hello` handshake (Phase 12) to detect mismatches early. Server-sent level data is a future extension if needed (e.g., for web clients connecting to custom campaigns).

### Sim Tick Rate & Game Speed

The current game's tick rate is governed by `GameWorld::timer_wait` (default 6) × 13.6ms per delay unit = **81.6ms per tick ≈ 12.25 ticks/sec**. Players can adjust speed in-game (`view.cpp:1352-1360`): `timer_wait` ranges from 0 (max speed, uncapped) to 20 (~272ms per tick, ~3.7 ticks/sec).

In networked play, the **host controls game speed** — the server's tick interval is derived from `timer_wait`. Non-host players cannot change speed (the speed adjustment keybinds are suppressed for non-host clients; server ignores speed-change inputs from non-host peers). The server broadcasts `timer_wait` in each WorldSnapshot so clients can adapt their interpolation timing.

```cpp
// include/openglad/gameplay/net_constants.h
inline constexpr int DEFAULT_SIM_TICKS_PER_SEC = 12;
inline constexpr int DEFAULT_SIM_TICK_MS = 1000 / DEFAULT_SIM_TICKS_PER_SEC; // 83ms
inline constexpr float TIMER_WAIT_TO_MS = 13.6f; // 1 timer_wait unit = 13.6ms
inline constexpr signed char DEFAULT_TIMER_WAIT = 6; // 6 * 13.6 = 81.6ms ≈ 83ms
inline constexpr signed char MIN_TIMER_WAIT = 1; // ~73 ticks/sec (don't allow 0 in networked mode — uncapped speed breaks bandwidth budget)
inline constexpr signed char MAX_TIMER_WAIT = 20; // ~3.7 ticks/sec

// Tick-based constants use DEFAULT_SIM_TICKS_PER_SEC for sizing.
// Actual real-world duration depends on current timer_wait.
inline constexpr int KEYFRAME_INTERVAL_TICKS = DEFAULT_SIM_TICKS_PER_SEC * 5; // ~60 ticks / 5 seconds at default speed

// --- Wall-clock timeouts (independent of game speed) ---
// These use real time, not tick counts, to prevent speed changes from
// affecting timeout behavior. A host at timer_wait=2 (fast) shouldn't
// disconnect players 3x faster than a host at timer_wait=6 (normal).
inline constexpr int DISCONNECT_TIMEOUT_MS = 10'000; // 10 seconds real time
inline constexpr int EXIT_PROMPT_TIMEOUT_MS = 15'000; // 15 seconds real time
inline constexpr int PAUSE_TIMEOUT_MS = 60'000; // 60 seconds real time
```

**Why wall-clock timeouts:** With variable `timer_wait`, tick-based timeouts change real-world duration as speed changes. A `DISCONNECT_TIMEOUT_TICKS = 120` is 10 seconds at default speed but only ~3.3 seconds at `timer_wait = 2`. Using wall-clock milliseconds for timeouts (disconnect, pause, exit prompt) ensures consistent real-world behavior regardless of game speed. `KEYFRAME_INTERVAL_TICKS` stays tick-based because keyframes should happen every N simulation updates, not every N seconds.

All tick-rate-dependent calculations throughout the plan reference these constants rather than hardcoded values. This makes it trivial to experiment with different default tick rates if 150-250ms input lag proves too noticeable during playtesting.

### Bandwidth Budget

At default speed (`timer_wait = 6`, ~12 ticks/sec) and ~40KB per full snapshot (~200 entities x ~200 bytes packed), that's ~480KB/sec per client uncompressed. Delta compression (only sending changed entity fields via per-field dirty bitmask) cuts this dramatically — between ticks, most entities don't change position or state. Typical delta: ~2-5KB. zlib on top of deltas (which still have structural similarity) yields ~1-3KB per tick. At 12 ticks/sec with 4 clients: ~50-150KB/sec outbound — very comfortable for residential internet. At max networked speed (`timer_wait = 1`, ~73 ticks/sec), bandwidth scales linearly to ~300-900KB/sec outbound — still within residential internet capacity but approaching the comfort zone.

Full keyframe snapshots are sent periodically (every `KEYFRAME_INTERVAL_TICKS` / ~5 seconds) or on client join for baseline sync. zlib is already vendored (at `third_party/physfs/zlib123/`, CMake target `og_ext_zlib`).

**SimEventBatch overhead:** The above budget only covers snapshot/delta data. `SimEventBatch` messages (sounds, notifications, palette changes) add ~10-30KB/sec during active combat (~61 cosmetic event call sites across gameplay code — 23 PlaySound + 35 Notification + 2 SetPalette + 1 RequestRedraw — firing at varying rates x ~20-50 bytes each x 12 ticks/sec). Total outbound with events: ~60-180KB/sec — still comfortable for residential internet.

### Module Placement

- Snapshot/entity ID code: `og_gameplay` (`include/openglad/gameplay/`, `src/gameplay/`)
- Transport interface + in-process transport: `og_gameplay` (no platform deps)
- WebSocket transports: new `OG_NETWORK_SOURCES` CMake list in `src/platform/sdl/` — platform-specific, depends on IXWebSocket. (Note: `OG_PLATFORM_SOURCES` is only 4 files; networking deserves its own list.)
- Emscripten transport: new `src/platform/emscripten/` directory (does not exist yet, must be created)
- Headless server binary: new `src/server/` directory, new CMake target (can follow the `openglad_text` headless target at `CMakeLists.txt:938` as a proven SDL-free precedent)
- All new source files must be added to the appropriate `OG_*_SOURCES` list in `CMakeLists.txt`

**CMake naming note:** The individual source lists (`OG_SIM_SOURCES`, `OG_ENTITIES_SOURCES`, etc.) are aggregated into `OG_GAMEPLAY_COMPONENT_SOURCES` (CMakeLists.txt:377) which builds the `og_gameplay` target. When adding new files, add them to the appropriate sub-list (e.g., `OG_SIM_SOURCES`), which will automatically flow into `og_gameplay` via the aggregation.

### Known UX Limitations

**AI behavioral jitter:** Clients don't run `world.tick()` — they apply server snapshots. The `statistics::commands` list (AI command queue) is deliberately not serialized (see Phase 5). This means AI entities may appear to twitch or change direction abruptly when a new snapshot overwrites their state mid-walk-command. This is inherent to the server-authoritative model without client-side prediction. Phase 18 interpolation smooths positional movement but not behavioral discontinuities (e.g., sudden direction changes). This is acceptable for a top-down action game at 12 ticks/sec, but should be monitored during playtesting.

**Host disconnect during gameplay:** If the host (who is also the game server) disconnects mid-game, all clients disconnect. There is no mid-game host migration or server promotion. The dedicated headless server (Phase 27) is the recommended path for sessions that need resilience. Host migration in the lobby (Phase 29) only applies pre-game.

**SetPalette desync risk:** `SetPalette` is classified as Tier 1 (cosmetic, best-effort delivery). The mage's freeze spell changes the entire screen palette via `SetPalette`. If a client misses this event, the palette stays wrong until the next keyframe corrects it (see `current_palette_id` in Phase 5 WorldSnapshot), which happens every ~5 seconds. This is acceptable given the rarity of the event and the self-correcting nature of keyframe sync.

**Exit prompt freezes all players:** When any player walks onto a level exit, the server pauses simulation and broadcasts the exit prompt to all clients. Any player can accept or decline. The game freezes for everyone during this prompt (see Phase 14). This matches the original single-screen behavior where the exit dialog froze the game. Timeout after `EXIT_PROMPT_TIMEOUT_MS` (15 seconds) auto-declines.

**Networked pause:** Any player can pause in networked mode, using the same freeze-broadcast pattern as exit prompts. Server stops ticking, broadcasts pause state, all clients freeze. Timeout after `PAUSE_TIMEOUT_MS` (60 seconds) auto-unpauses to prevent a disconnected player from freezing the game forever. See Phase 14 for protocol details.

**Grid size variability:** Grid dimensions (`PixieData.w * PixieData.h`) vary per level. The bandwidth budget estimates ~2400 bytes for a 60×40 map, but larger levels (up to ~228×228 = ~52KB) are possible. Full grid resends in keyframes remain cheap even at worst case — ~52KB every 5 seconds is ~10KB/sec, well within budget. Per-tick grid deltas are always tiny (a few changed tiles from explosions).

**Game speed in networked play:** Only the host can adjust game speed (`timer_wait`). Non-host clients' speed-change keybinds are suppressed. The server includes `timer_wait` in every WorldSnapshot. When the host changes speed, clients see the new `timer_wait` in the next snapshot and adjust their interpolation timing accordingly. `timer_wait = 0` (uncapped speed) is clamped to `MIN_TIMER_WAIT = 1` in networked mode to prevent bandwidth explosion (~73 ticks/sec at `timer_wait = 1` is already aggressive). The bandwidth budget scales linearly with tick rate — at `timer_wait = 1` (73 ticks/sec), delta bandwidth is ~6x the default estimate (~300-900KB/sec outbound with 4 clients). This is still within residential internet capacity but should be noted in the host UI if speed is cranked up.

**No mid-game joins:** New players cannot join a game already in progress. All players must be present in the lobby before game start. This simplifies the protocol significantly — no need for mid-game team assignment, viewscreen allocation, or entity ownership negotiation. Disconnected players can reconnect (see below); new players cannot.

**Player reconnection:** When a player disconnects mid-game, their entity transitions to AI control (Phase 30). The server retains the player's session token (assigned during the `Hello` handshake) and slot assignment. If the same player reconnects before the game ends, the server re-identifies them via the session token, sends a fresh `InitialSetup` + keyframe, and issues a `ControlChange` message to hand the AI-controlled entity back to the player. Other players see the entity seamlessly transition from AI back to human control. If the session token doesn't match any disconnected slot, the connection is rejected with "game in progress — no new players."

---

## Phase 0: Migrate `rand()` to `SimRandom`

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

---

## Phase 1: Add Entity Unique IDs

Entities are currently identified by pointer only. The only existing ID system is `guy::id` (character-level, for duplicate detection in `SaveData::team_list`), which is unrelated. Everything downstream (serialization, snapshots, cross-references) needs stable entity-level IDs.

**Changes:**
- Add `uint32_t entity_id_ = 0` to `SimEntity` (`include/openglad/gameplay/sim_entity.h:23`)
- Add `uint32_t next_entity_id_ = 1` counter + `assign_entity_id()` to `GameWorld` (`include/openglad/gameplay/game_world.h`)
- Call `assign_entity_id()` inside `GameWorld::add_to_list()` (`src/gameplay/game_world.cpp:108-128` — this is a **private** method, but we're modifying GameWorld internals so access is fine. The public API is `add_ob()`/`add_fx_ob()`/`add_weap_ob()` which delegate to it.)
- Add `walker* find_by_id(uint32_t)` method on GameWorld (linear scan is fine for ~200 entities)
- Add persistent `std::unordered_map<uint32_t, walker*> id_index_` to GameWorld. Update it in `add_to_list()` (insert) and on entity removal (erase). `find_by_id()` uses this index for O(1) lookup. This is critical for Phase 7 (`apply_snapshot()`) where cross-reference resolution would otherwise be O(N^2).

**Thread safety invariant:** `id_index_` (and `GameWorld` in general) is **single-thread-access only**. All mutations and reads happen on the game loop thread. When WebSocket I/O threads arrive (Phase 24), all incoming messages are queued and drained via `poll()` on the game loop thread. The send path must also not touch GameWorld state from I/O callbacks (e.g., don't call `capture_snapshot()` from an `onClientConnected` callback — queue a "send keyframe" request for the game loop to process). This invariant applies to all GameWorld state introduced in subsequent phases.

**Verify:** Unit test (`tests/unit/`) — add entities, assert unique non-zero IDs. `find_by_id()` returns correct entity. `id_index_` stays consistent after add/remove. All existing tests pass.

---

## Phase 2: Add Cross-Reference ID Fields

Walker has raw pointer cross-references that can't be serialized. Add parallel ID fields and setter helpers alongside the existing public pointers. This phase intentionally does **not** make the pointers private — that's a large mechanical refactor (~477+ compiler errors) that is deferred to a later cleanup to avoid blocking the networking critical path.

**All 5 cross-reference pointers:**
1. `walker* foe` (`walker.h:167`) — target enemy
2. `walker* leader` (`walker.h:168`) — commanding entity (summons, followers)
3. `walker* owner` (`walker.h:169`) — owning entity (for weapons)
4. `walker* collide_ob` (`walker.h:188`) — collision event target
5. `walker* controller` in `statistics` (`statistics.h:113`) — controlling entity for AI

**Changes:**
- Add `uint32_t foe_id = 0, leader_id = 0, owner_id = 0, collide_ob_id = 0` to walker (`include/openglad/gameplay/walker.h`)
- Add `uint32_t controller_id = 0` to statistics (`include/openglad/gameplay/statistics.h`)
- Add `set_foe(walker*)`, `set_leader(walker*)`, `set_owner(walker*)`, `set_collide_ob(walker*)` helpers that set both pointer and ID **and call `mark_dirty()` for the corresponding field bit** (see dirty tracking below)
- Add `set_controller(walker*)` helper on statistics **(also calls `mark_dirty()`)**
- Add `sync_ids_from_pointers()` method on walker that reads all 5 pointers and populates the corresponding `_id` fields from each pointer's `entity_id_`. Called as a safety net during `capture_snapshot()` (Phase 6) to guarantee consistency even for callers that haven't been migrated to setters yet.
- `walker_init_common()` at `src/gameplay/walker.cpp:92-126` already initializes all pointers to nullptr — update to also zero IDs
- Update stale-pointer cleanup in `GameWorld::tick()` (`src/gameplay/game_world.cpp:989-1014`) to also clear ID fields

**Dirty tracking infrastructure (foundation for setter-based delta compression — see Phase 8):**

The delta compression design uses **setter-based dirty tracking** rather than snapshot comparison. Dirty bits are set at the source (field mutation sites in gameplay code) and read during snapshot capture. This eliminates the expensive `compute_delta()` comparison step entirely.

- Add `uint64_t dirty_mask_[2] = {}` to `SimEntity` (`include/openglad/gameplay/sim_entity.h`). 128 bits covers all 86 serializable fields with headroom. Lives on the base class so walker, statistics, and subclass setters can all access it.
- Add inline `void mark_dirty(uint8_t bit) { dirty_mask_[bit / 64] |= (1ULL << (bit % 64)); }`
- Add inline `void mark_all_dirty() { dirty_mask_[0] = ~0ULL; dirty_mask_[1] = ~0ULL; }`
- Add inline `void clear_dirty() { dirty_mask_[0] = 0; dirty_mask_[1] = 0; }`
- Define **bit index constants** in a new header `include/openglad/gameplay/dirty_field_bits.h`:

```cpp
// include/openglad/gameplay/dirty_field_bits.h
// Bit indices for dirty_mask_[2]. Shared between entity setters and the
// constexpr field table (Phase 5). Adding a field = add a constant here
// + add an entry to SNAP_FIELDS[] + update the static_assert.

#pragma once
#include <cstdint>

namespace og::dirty {

// SimEntity fields (0-18)
inline constexpr uint8_t BIT_ENTITY_ID = 0;
inline constexpr uint8_t BIT_XPOS = 1;
inline constexpr uint8_t BIT_YPOS = 2;
// ... etc for all 86 fields ...
inline constexpr uint8_t BIT_DO_BOUNCE = 85;

inline constexpr uint8_t FIELD_COUNT = 86;

} // namespace og::dirty
```

- Call `mark_all_dirty()` in `GameWorld::add_to_list()` after assigning `entity_id_` — new entities are fully dirty.
- Cross-reference setters (`set_foe`, etc.) call `mark_dirty(BIT_FOE_ID)` etc.
- **Entity removal tracking:** Add `std::vector<uint32_t> removed_entity_ids_` to GameWorld. Populated in entity removal paths (stale-pointer cleanup, entity death). Drained by `capture_snapshot()` (Phase 6). This tracks which entities disappeared since the last capture, complementing the per-entity dirty bits which only track field changes on living entities.
- **Bug fix #1:** `GameWorld::tick()` currently cleans up stale pointers for `foe`, `leader`, `owner`, and `collide_ob` — but NOT `statistics::controller`. Add `controller` cleanup here too (clear both pointer and ID when the referenced entity is dead). This is a pre-existing latent bug. (Note: the comment at `game_world.cpp:1017-1018` about "viewscreen control pointer cleanup" refers to `viewscreen::control`, which is a *different* pointer cleaned in `screen::act()` lines 909-913 — NOT `statistics::controller`.)
- **Bug fix #2:** `GameWorld::tick()` stale-pointer cleanup iterates `oblist` (lines 990-1001) and `weaplist` (lines 1003-1014) but **NOT `fxlist`**. FX entities (explosions, chain lightning, etc.) can hold `foe`, `leader`, and `owner` pointers — these go stale when referenced entities die. Add `fxlist` to the stale-pointer cleanup loop. This is a second pre-existing latent bug.

**IMPORTANT: Bug fixes #1 and #2 are hard prerequisites for Phase 6.** `sync_ids_from_pointers()` reads all 5 cross-reference pointers. If any pointer is stale (dangling — points to freed memory), reading `entity_id_` from it is undefined behavior. These bug fixes must land before any code calls `sync_ids_from_pointers()`.

**Verify:** Unit test — `set_foe(other)` sets both pointer and ID. Clear foe clears both. `sync_ids_from_pointers()` populates IDs from raw pointers. Verify `statistics::controller` cleanup works. Verify `fxlist` stale-pointer cleanup works. All existing tests pass (no callers need to change — pointers remain public, setters are opt-in).

---

## Phase 3: Compiler-Driven Cross-Reference Migration

Make all 5 cross-reference pointer fields private, then fix every compiler error to use the setters from Phase 2. This enforces that all future code uses the dual pointer+ID setters, eliminating the need for `sync_ids_from_pointers()`.

**This phase executes immediately after Phase 2.** While `sync_ids_from_pointers()` provides a safety net, it reads raw pointers every tick during `capture_snapshot()` (Phase 6) — a latent UB risk if any stale pointer path was missed by the Phase 2 bug fixes. Completing the migration now eliminates the entire class of stale-pointer-during-snapshot bugs before any snapshot code is written. The ~477 compiler errors are mechanical and touch gameplay code that will be modified in later phases anyway — getting it done early means every subsequent phase benefits from enforced setter usage.

After this phase, `sync_ids_from_pointers()` can be removed or converted to a debug-only assertion.

**Scope (grep counts, including test files):**
- `->foe = ...`: ~201 grep hits (gameplay + test code)
- `->leader = ...`: ~86 grep hits
- `->owner = ...`: ~157 grep hits
- `->collide_ob = ...`: ~31 grep hits
- `.controller = ...`: ~3 grep hits (stats.cpp:53,56,95 — 3 other grep hits in family files are local variable declarations, not field assignments)
- **Estimated total: ~477+ compiler errors to fix** across `src/gameplay/`, `src/interface/`, `include/openglad/gameplay/`, and `tests/`.

**Dirty tracking instrumentation is deferred to Phase 8.** The infrastructure (`dirty_mask_[2]`, `mark_dirty()`, `mark_all_dirty()`, `clear_dirty()`, bit constants in `dirty_field_bits.h`) exists from Phase 2, and the cross-reference setters already call `mark_dirty()`. But the large-scale instrumentation of all ~200-400 remaining field mutation sites is deferred to Phase 8 where delta compression actually needs it. Until then, keyframe captures set all bits (all fields dirty), so the absence of per-field `mark_dirty()` calls has no effect. This keeps Phase 3 focused on a single concern (pointer privatization) and ensures the instrumentation and its CI safety-net test land in the same phase.

**Recommended commit strategy:** Do the privatization one pointer at a time as separate commits (`foe` first at ~201 sites, then `owner` at ~157, then `leader` at ~86, then `collide_ob` at ~31, then `controller` at ~3). Each commit is independently compilable and reviewable.

**Verify:** No raw pointer assignments remain for the 5 cross-reference fields. `sync_ids_from_pointers()` removed or converted to debug-only assertion. All existing tests pass.

---

## Phase 4: InputState Serialization

`InputState` is what clients send to the server every tick. Defined at `include/openglad/gameplay/input_state.h`.

Structure: `PlayerInput` has `bool held[16]` + `bool pressed[16]` per player. `InputState` has `PlayerInput players[4]` + `bool quit_requested`. Total semantic size: ~130 bytes, but packs to much less.

**Changes:**
- Add `serialize_input()` / `deserialize_input()` functions
- New file: `src/gameplay/input_state_net.cpp`, header: `include/openglad/gameplay/input_state_net.h`
- Pack bools as bitfields: 4 players x 32 bits (16 held + 16 pressed) = 16 bytes + 1 byte header (protocol version + quit flag)
- Add to `OG_SIM_SOURCES` in CMakeLists.txt

**Verify:** Unit test — round-trip serialization with various button combinations. Verify `move_x()` and `move_y()` helpers produce same results after round-trip.

---

## Phase 5: WorldSnapshot Data Structure

Define the snapshot structs that represent a complete world state at a point in time. This phase requires explicitly deciding what to serialize and what to skip.

**Changes:**
- New header: `include/openglad/gameplay/world_snapshot.h`

**`EntitySnapshot` struct — fields to include:**

From `SimEntity` base (19 fields: 18 original serializable + entity_id_ from Phase 1):
- `entity_id_` (added in Phase 1), `xpos`, `ypos`, `sizex`, `sizey`
- `team_num`, `real_team_num`, `user`
- `dead`, `death_called`, `invulnerable_left`, `invisibility_left`, `flight_left`, `bonus_rounds`
- `order`, `family`, `frame`
- `worldx_`, `worldy_` (authoritative float position — serialized as IEEE 754 `float`, 4 bytes each, preserving full precision since the source type is `float`)

> **Note on field count:** SimEntity has 19 total fields (16 public + 3 protected: `worldx_`, `worldy_`, `frames`). `frames` (protected, `sim_entity.h:65`) is skipped (derived from order/family). That leaves 18 original serializable + `entity_id_` = 19.

From `walker` (serialize these 44 fields, including 4 cross-ref IDs from Phase 2):
- Movement: `lastx`, `lasty`, `stepsize`, `normal_stepsize`, `curdir`, `enddir` (6)
- Combat: `damage`, `fire_frequency`, `busy`, `current_weapon`, `default_weapon`, `attack_lunge`, `attack_lunge_angle`, `hit_recoil`, `hit_recoil_angle`, `last_hitpoints` (10)
- State: `action`, `act_type`, `old_act_type`, `ani_type`, `cycle`, `drawcycle`, `current_special`, `ignore`, `in_act`, `shifter_down`, `yo_delay`, `skip_exit`, `outline`, `hurt_flash` (14)
- Timers: `lifetime`, `speed_bonus`, `speed_bonus_left`, `charm_left`, `weapons_left` (5)
- Identity: `keys`, `view_all`, `lineofsight` (3)
- AI: `path_check_counter` — controls AI re-pathfinding frequency, affects simulation timing (1)
- Healing: `regen_delay_` (protected) — post-damage healing cooldown in ticks, directly affects HP regen via `compute_hp_regen()` in `combat_math.h` (1)
- Cross-reference IDs: `foe_id`, `leader_id`, `owner_id`, `collide_ob_id` (4)
- `do_bounce` — lives on `weap` subclass (`weap.h:44`), not on `walker` base. Capture requires `dynamic_cast<weap*>` or checking `order == Order::Weapon`. Set to 0 for non-weapon entities. (Only `weap` has extra data fields among the 4 subclasses — `living`, `treasure`, and `effect` are behavior-only with no additional state.) (1)

From `statistics` (serialize these 22 fields):
- `hitpoints`, `max_hitpoints`, `magicpoints`, `max_magicpoints` (4)
- `max_heal_delay`, `current_heal_delay`, `max_magic_delay`, `current_magic_delay` (4)
- `magic_per_round`, `heal_per_round`, `armor`, `level` (4)
- `bit_flags`, `delete_me`, `frozen_delay`, `weapon_cost` (4)
- `special_cost[NUM_SPECIALS]` (6-element array — `NUM_SPECIALS = 6` at `statistics.h:30` — uses **1 dirty bit** for the whole array; if any element changed, all 12 bytes are sent. The array is small enough that per-element tracking isn't worth the complexity.)
- `old_order`, `old_family`, `last_distance`, `current_distance` (4)
- `controller_id` (1)

**Total serializable fields:** 19 SimEntity + 44 walker + 22 stats + 1 weap = **86 fields**.

**Fields to explicitly SKIP (not serialized):**
- `SimEntity::frames` (protected, `sim_entity.h:65`) — total frame count for sprite sheet. Derivable from `(order, family)` via `entity_configurator` callback, same as animation data. Constant for a given entity type.
- `walker::ani` — pointer to static animation table, reconstruct from `(order, family, ani_type)`
- `walker::path_to_foe` — pathfinding state, entities re-path on restore (controlled by `path_check_counter` which IS serialized)
- `walker::damage_numbers` — ephemeral render-only UI data (floating damage text)
- `walker::render_` — render component, client-side only
- `walker::myself_` — self-pointer, trivially reconstructed
- `walker::owned_myguy_` — character data ownership, handled separately (see myguy section below)
- `walker::myguy` — non-owning pointer, reconstructed from `guy_id` (see myguy section below)
- `walker::foe`, `walker::leader`, `walker::owner`, `walker::collide_ob` — raw pointers, reconstructed from `_id` fields via `find_by_id()` in `apply_snapshot()`
- `statistics::controller` — raw pointer, reconstructed from `controller_id` via `find_by_id()`
- `statistics::commands` — AI command queue. On snapshot restore, AI re-evaluates next tick. Serializing queued commands adds complexity for minimal benefit since commands are generated fresh each tick based on world state. **Known side effect:** discarding in-flight commands causes AI behavioral jitter (see "Known UX Limitations" in Context).
- `statistics::name` — NPC name string, reconstruct from family/order on entity creation
- `statistics::walkrounds` — **deleted in Phase 0** (dead code cleanup). No longer exists.

**`myguy` handling:**
Each walker can reference a `guy*` (player character data from `SaveData::team_list`). For networking:
- Player character `guy` data is sent at game start as part of initial setup (name, family, stats, exp, level — 22 data fields from `include/openglad/gameplay/guy.h:28-74`)
- **Mid-game `guy` mutations:** `guy` fields change during gameplay (exp, kills, scen_damage, scen_kills, scen_damage_taken, scen_min_hp, scen_shots, scen_hits — see `guy.h:48-68`). These mutations happen in `walker_combat.cpp` and `stats.cpp`. Include changed `guy` fields in the snapshot for player-controlled entities. This adds ~30 bytes per player character per snapshot (up to 4 players = ~120 bytes total — negligible).
- Clients store received `guy` data in a local lookup table (e.g., `std::unordered_map<int, guy>` keyed by `guy::id`) for pointer reconstruction
- `EntitySnapshot` stores only a `guy_id` (int, matching `guy::id` from `SaveData`) to link walker to character. **The actual `guy` data (name, stats, etc.) lives in a separate `std::vector<GuySnapshot>` on `WorldSnapshot`, NOT embedded in `EntitySnapshot`.** This is critical: `EntitySnapshot` must remain `trivially_copyable` for `memcpy`-based serialization (see `static_assert` in Phase 5). Embedding `guy::name` (`std::string`) in `EntitySnapshot` would break this invariant.
- Clients reconstruct the `myguy` pointer from their local guy lookup table after `apply_snapshot()`, and update the local `guy` fields from the snapshot data

**`WorldSnapshot` struct — world-level state:**
- `tick_count` (from `GameWorld::tick_count_`)
- `rng_state` (from `GameWorld::rng_` — LCG seed value, `rng_.state_` is public)
- `level_tick_count_` (private, `game_world.h:157` — needs public accessor, same treatment as `regen_delay_`; see Phase 6)
- Game flow flags: `level_done`, `game_ended`, `end`, `retry`, `next_level`, `ending`
- `enemy_freeze` timer
- `timer_wait` (sim speed)
- `living_count`
- `control_hp` — HP of control point objective, changes during combat
- `withdraw_requested`, `withdraw_level` — player withdrawal state, affects game flow
- `guy_id_counter` — entity ID allocator, server and client must agree for consistency
- Per-player scores: `m_score[4]` from `GameWorld` (NOT from SaveData — during gameplay, `GameWorld::m_score[4]` at `game_world.h:142` is the authoritative source. SaveData scores are only the persistent store, synced via `sync_save_data_from_world()` between levels.)
- Per-player `guy` snapshot data (for entities with `myguy != nullptr` — see myguy handling above)
- Vectors of `EntitySnapshot`: one vector each for oblist, fxlist, weaplist (skip dead_list — dead entities don't affect simulation). Note: source data is `std::list<std::unique_ptr<walker>>` (linked lists), but snapshot uses vectors for compact serialization.
- **Dead player entities in oblist:** Dead entities with `myguy != nullptr` (player characters) are NOT moved to `dead_list` during `GameWorld::tick()` (see `game_world.cpp:1019-1027` — only entities with `myguy == nullptr` are moved). These dead player entities remain in `oblist` for respawn/scoring purposes and MUST be included in the oblist snapshot vector.
- `current_palette_id` (uint8_t) — 0 = normal palette, 1 = blue/freeze palette. Included in world-level state so that clients always converge to the correct palette on every keyframe, eliminating the SetPalette desync risk entirely. 1 byte per snapshot — negligible cost for guaranteed correctness.
- `pending_exit_prompt` — server-side exit prompt state (see Phase 14). Serialized so clients know whether the sim is paused for a prompt.
- `paused` (bool) — whether the game is paused by a player. `pause_player_index` (uint8_t) — which player paused. Clients display "PAUSED by [name]" overlay when set. See Phase 14 pause protocol.

**Note:** Level metadata (`id`, `title`, `type`, `par_value`, `time_bonus_limit`, `difficulty`, `pixmaxx`, `pixmaxy`, `mysmoother`) and team config (`my_team`, `allied_mode`, `current_scenario`, `completed_levels`) are constant during a level. These are sent once at level start via `InitialSetup`, not included in per-tick snapshots. `completed_levels` (`std::set<int>` at `game_world.h:147`) is campaign progression state — the server updates it between levels and includes the updated set in each `InitialSetup` message.

**Grid data:** The tile grid (`GameWorld::grid`, a `PixieData` with `w`, `h`, and `data[]` of size w*h) is sent as part of `InitialSetup` at level start. However, tile damage during gameplay mutates `grid.data[]` (see DamageTile note above). After DamageTile is moved into the simulation layer, the grid becomes authoritative server state. Include a **grid dirty flag + changed tile list** in every tick's delta/keyframe. The grid is small (~2400 bytes for a 60x40 map), so even a full grid resend in keyframes is cheap. Per-tick grid deltas typically contain only a few changed tiles (from explosions), adding negligible bytes.

**SimEventLog events** are sent as a **separate `SimEventBatch` message** alongside each snapshot/delta, NOT embedded inside the snapshot struct. Events are one-shot side effects (play sound, show notification) while snapshots are idempotent state — mixing them creates problems:
- If a delta is lost/skipped, embedded events would be lost permanently
- Delta compression doesn't apply to events (they're not "fields that changed")
- Keyframe semantics become ambiguous (which events to include?)

The `SimEventBatch` message contains a sequence number and the events drained from `SimEventLog` for that tick. Clients track the last-seen sequence number to detect gaps. On keyframe resync, the client resets its event sequence counter.

**Event reliability — two-tier model:** SimEventLog events fall into two categories with different reliability requirements:

**Tier 1 — Cosmetic events (best-effort delivery):**
- `PlaySound` — sound effects
- `Notification` — floating text, HUD messages
- `SetPalette` — palette changes (freeze effect). With `current_palette_id` in the snapshot, a missed event self-corrects on the next keyframe (~5 seconds).
- `RequestRedraw` — force screen refresh
- Missing a cosmetic event is tolerable (a skipped sound, a missed floating number). These are sent in the `SimEventBatch` message alongside each delta/keyframe. Clients track sequence numbers; gaps are logged but not fatal.

**`DamageTile` — moved to simulation layer (not an event):**
`screen::damage_tile()` at `screen.cpp:1258-1286` **mutates `world_.grid.data[]`** (e.g., `PIX_GRASS1` -> `PIX_GRASS1_DAMAGED`). This is authoritative world state, not a cosmetic effect. If a client misses this event, its grid diverges from the server's. Fix: move tile damage logic into `GameWorld::tick()` (or a helper called from gameplay code) so the grid mutation happens server-side as part of the simulation. The `DamageTile` event is then removed from `EventKind` — clients receive the updated grid state via snapshots. See Phase 6 for grid snapshot details.

**Tier 2 — Game-flow events (reliable delivery):**
- `EndGame` — triggers `sync_save_data_from_world()` + `endgame()` UI sequence (`screen.cpp:951-955`)
- `SetEnd` — defined in `EventKind` enum but **has zero push sites in gameplay code**; appears to be vestigial. Kept in enum for forward compatibility but not expected to fire. If needed in the future, add push sites at that time.
- `RequestExitConfirmation` — triggers server-side sim pause + broadcast prompt (see Phase 14)
- `WithdrawToLevel` — triggers level withdrawal transition (`screen.cpp:966-968`)
- `ScoreChange` — triggers score UI refresh (`screen.cpp:970-973`; score data itself is already in snapshot via `m_score[4]`)

Game-flow events carry **side effects beyond state** — they trigger UI sequences, blocking prompts, and save data syncs that aren't captured by snapshot flags alone. These are sent in a separate **`GameFlowEventBatch`** message with reliable, ordered delivery (TCP/WebSocket guarantees this; for in-process transport it's automatic). The client dispatches game-flow events through a dedicated `screen::dispatch_game_flow_events()` method (see Phase 15 for the `screen::act()` split).

All game-flow-critical **state** (level completion, player death, game end) MUST also be derivable from `WorldSnapshot` flags (`level_done`, `game_ended`, `dead`, etc.) as a consistency guarantee. The events trigger the UI transitions; the snapshot flags are the source of truth. If a client reconnects and missed events, the snapshot flags let it recover to the correct state (though it may miss the transition animation).

**Phase 5 verification step:** audit all event push call sites in `src/gameplay/` and categorize each as Tier 1 or Tier 2.

Tier 1 (cosmetic) push sites — there are **~61 call sites** across gameplay code:
- **PlaySound** (`emit_sound`) — 23 sites: `effect_family_bomb.cpp:21`, `effect_family_chain.cpp:45`, `family_archmage.cpp:203,244`, `family_cleric.cpp:123,181,235`, `family_druid.cpp:144`, `family_mage.cpp:153,252`, `family_orc.cpp:66`, `family_soldier.cpp:40,98`, `treasure_family_consumables.cpp:30`, `treasure_family_valuables.cpp:43,55,88`, `weapon_family_projectiles.cpp:23`, `walker.cpp:401,453,1367`, `walker_combat.cpp:367,369`
- **Notification** (`emit_notification` / `push_notification`) — 35 sites: `family_archmage.cpp:160,176,192,194,285,459`, `family_cleric.cpp:122,137,166,178,220,232`, `family_druid.cpp:143`, `family_mage.cpp:106,123,141,143,206`, `family_orc.cpp:90`, `family_soldier.cpp:100`, `family_thief.cpp:110,160`, `treasure_family_consumables.cpp:39`, `treasure_family_valuables.cpp:87`, `game_world.cpp:871,910`, `obmap.cpp:314`, `stats.cpp:531`, `walker_combat.cpp:329,334,352`, `walker_specials.cpp:104`, `weap.cpp:89,125,133`
- **SetPalette** — 2 sites: `family_mage.cpp:198`, `game_world.cpp:878`
- **RequestRedraw** — 1 site: `family_mage.cpp:207`

Tier 2 (game-flow) push sites — 6 call sites total:
- **EndGame** (1): `walker.cpp:1335`
- **ScoreChange** (2): `walker_combat.cpp:83`, `treasure_family_valuables.cpp:32`
- **RequestExitConfirmation** (2): `treasure_family_navigation.cpp:71,91`
- **WithdrawToLevel** (1): `treasure_family_navigation.cpp:88`
- **DamageTile** (1): `effect_family_bomb.cpp:42` — migrated to simulation layer in Phase 6, removed from events
- **SetEnd** (0): zero push sites (vestigial)

**For delta compression (Phase 8):** `EntitySnapshot` includes a `uint64_t dirty_mask[2]` bitmask (128 bits). With 19 SimEntity fields + 44 walker fields + 22 stats fields + 1 weap field = **86 serializable fields**, a single `uint64_t` (64 bits) is insufficient. Two `uint64_t` fields provide 128 bits — comfortable headroom for the current 86 fields plus future additions. When sending deltas, only fields whose corresponding bit is set are included in the wire format. Full keyframes set all bits. This bitmask is part of the struct definition from the start, even though delta serialization is implemented in Phase 8.

**Constexpr field table:** 86 fields with manual bit indices, duplicated across `EntitySnapshot` struct definition, `capture_snapshot()`, `apply_snapshot()`, and serialize/deserialize — that's 4 places per field (`compute_delta()` is eliminated by setter-based dirty tracking — see Phase 8). Missing one = silent desync bug. Use a `constexpr` field descriptor table to define fields once and reference everywhere:

```cpp
// include/openglad/gameplay/snapshot_fields.h

#include <cstddef>
#include <cstdint>

struct FieldDesc {
    uint8_t bit_index;       // fixed position in dirty_mask (0-127)
    uint8_t size;            // sizeof(field_type)
    uint16_t snap_offset;    // offsetof(EntitySnapshot, field_name)
};

// --- All 86 fields, defined once ---
inline constexpr FieldDesc SNAP_FIELDS[] = {
    // SimEntity fields (0-18)
    {0,  sizeof(uint32_t), offsetof(EntitySnapshot, entity_id)},
    {1,  sizeof(int16_t),  offsetof(EntitySnapshot, xpos)},
    {2,  sizeof(int16_t),  offsetof(EntitySnapshot, ypos)},
    // ... etc for all 86 fields
};

inline constexpr size_t SNAP_FIELD_COUNT = std::size(SNAP_FIELDS);
static_assert(SNAP_FIELD_COUNT == 86, "Field count drift -- update snapshot_fields.h");
static_assert(std::is_trivially_copyable_v<EntitySnapshot>,
              "EntitySnapshot must be trivially copyable for memcpy-based serialization");
```

**`trivially_copyable` guard:** The `reinterpret_cast<uint8_t*>` serialization (Phase 8) requires `EntitySnapshot` to be trivially copyable. If anyone adds a `std::string`, `std::vector`, or other non-trivial member, the serialization silently breaks (reads garbage or crashes). The `static_assert` catches this at compile time. `EntitySnapshot` should contain only scalar types and fixed-size arrays.

The `bit_index` values in `SNAP_FIELDS` reference the same constants defined in `dirty_field_bits.h` (Phase 2). This ensures the field table and the `mark_dirty()` calls use identical bit assignments.

This table drives generic loops for capture, apply, and serialization. (`compute_delta()` is eliminated — see Phase 8.) Adding a field means adding one entry to `SNAP_FIELDS`, one constant to `dirty_field_bits.h`, instrumenting mutation sites with `mark_dirty()`, and updating both static asserts. Fields with special handling (e.g., `do_bounce` needing a `weap*` downcast, `regen_delay_` needing an accessor) are excluded from the generic table and handled manually alongside it.

The same table also drives manual binary serialization (Phase 8) — a generic `serialize_fields(buf, snap, dirty_mask)` loop writes each set field as raw bytes in field-table order. No external serialization library needed.

Compared to X-macros (the original plan), `constexpr` field descriptors are:
- Debuggable (real data in a real array, visible in debuggers)
- IDE-friendly (no preprocessor expansion needed for autocomplete/go-to-definition)
- Type-safe (no macro text substitution surprises)
- Slightly more verbose per field, but the table is write-once

**Verify:** Compiles. Can construct and populate manually. Static assert on field count to catch accidental additions.

---

## Phase 6: Snapshot Capture (World -> Snapshot)

Read a live GameWorld and produce a WorldSnapshot.

**Changes:**
- Implement `WorldSnapshot capture_snapshot(const GameWorld&)` in `src/gameplay/world_snapshot.cpp`
- Walks oblist/fxlist/weaplist (iterating `std::list<std::unique_ptr<walker>>`), copies 44 walker fields + 22 stats fields per entity
- Calls `sync_ids_from_pointers()` on each entity before capture to guarantee cross-reference IDs are consistent with raw pointers (safety net until Phase 3 migration is complete)
- **`sync_ids_from_pointers()` stale pointer guard:** When reading `entity_id_` from a cross-reference pointer, check that the result is non-zero. An `entity_id_` of 0 means the entity was never assigned an ID (e.g., a pointer to a partially-constructed or already-destroyed object). If `entity_id_ == 0`, treat the reference as stale and set the corresponding `_id` field to 0. This complements the Phase 2 stale-pointer cleanup (which catches `dead` entities) by also catching pointers to entities that were removed from lists without being marked dead (e.g., during level transitions or editor operations).
- For `do_bounce`: check `ob->query_order() == Order::Weapon`, then `static_cast<weap*>(ob)->do_bounce`. Safe because `query_order()` is a virtual that returns the compile-time order. Set to 0 for non-weapon entities.
- For `regen_delay_`: this is a protected field on walker (accessed directly from `living.cpp:139,142`, `walker.cpp:253`, `walker_combat.cpp:169`). Either add a public accessor `int32_t regen_delay() const`, or make `capture_snapshot` a friend. Public accessor is cleaner.
- For `level_tick_count_`: this is a private field on GameWorld (`game_world.h:157`). Add a public accessor `uint32_t level_tick_count() const` and setter `void set_level_tick_count(uint32_t)`.
- Captures all GameWorld state fields listed in Phase 5 (including `control_hp`, `withdraw_requested`, `withdraw_level`, `guy_id_counter`, `current_palette_id`, `pending_exit_prompt`)
- Captures `guy` snapshot data for player-controlled entities (entities where `myguy != nullptr`): copy the relevant `guy` fields (exp, kills, scen_damage, scen_kills, etc.) into the snapshot keyed by `guy::id`
- **Dirty mask capture:** For each entity, copy `dirty_mask_[2]` into the `EntitySnapshot::dirty_mask[2]`, then call `clear_dirty()` on the entity. This is the handoff: dirty bits flow from the live entity (set during `GameWorld::tick()` by `mark_dirty()` calls) into the snapshot (read during delta serialization in Phase 8). For keyframe captures, override to all-bits-set regardless of entity dirty state.
- **Removed entity list:** Drain `GameWorld::removed_entity_ids_` (populated by entity removal paths — Phase 2) into the snapshot. This list tells delta serialization which entities to mark as removed.
- **Grid tile snapshot:** Include grid dirty data in every tick's capture. Track a `grid_dirty_tiles_` vector (`std::vector<std::pair<short,short>>`) on GameWorld, populated by the new `GameWorld::damage_tile()` method. `capture_snapshot()` drains this vector into the snapshot's grid diff. On keyframe captures, also include the full grid data (`world.grid.data[]`, w*h bytes) as a baseline.
- **Grid dirty cap:** If `grid_dirty_tiles_.size()` exceeds `MAX_GRID_DIRTY_TILES` (64), discard the per-tile delta and flag the snapshot for a full grid send instead. Chain lightning or large explosions can damage 50+ tiles in one tick, spiking delta payload size. The cap ensures worst-case grid delta cost is bounded: either ≤64 tile coordinates (~256 bytes) or one full grid send (~2-5KB for typical maps, ~52KB worst case for 228×228). Add `inline constexpr size_t MAX_GRID_DIRTY_TILES = 64;` to `net_constants.h`.
- **DamageTile migration:** Move `screen::damage_tile()` logic (`screen.cpp:1258-1286`) into a `GameWorld::damage_tile(short xloc, short yloc)` method. The method mutates `grid.data[]` and appends the changed tile coordinates to `grid_dirty_tiles_`. The gameplay code that currently emits `EventKind::DamageTile` events (`effect_family_bomb.cpp:42`) should instead call `world.damage_tile()` directly. Remove `DamageTile` from `EventKind` entirely — it's no longer an event, it's a simulation mutation.
- Drains current `SimEventLog` events into a separate `SimEventBatch` (not into the snapshot struct)
- **`transform_to()` awareness:** `order` and `family` can change mid-game via `walker::transform_to()` (slimes splitting: `FAMILY_SLIME` -> `FAMILY_SMALL_SLIME`, wave projectiles evolving: `FAMILY_WAVE` -> `FAMILY_WAVE2`, etc.). `transform_to()` changes these fields in-place on the same C++ object — the subclass (`living`/`weap`/`treasure`/`effect`) does NOT change. In practice, `transform_to()` only changes family within the same Order. The snapshot captures whatever `order` and `family` the entity currently has. See Phase 7 for how `apply_snapshot()` handles this.
- Total per entity: ~200 bytes depending on packing

**Verify:** Integration test — load level, run ticks, capture snapshot, verify entity count and field values match live world. Verify all 5 cross-reference IDs are populated correctly. Verify `do_bounce` is captured for weapon entities and 0 for others. Verify grid dirty tiles are captured after an explosion damages tiles.

---

## Phase 7: Snapshot Application (Snapshot -> World)

Apply a WorldSnapshot to a GameWorld, replacing its state. This is what clients do when they receive a server snapshot.

### `GameplayContextGuard` RAII

The thread-local `GameplayContext* current_game` (`gameplay_context.h:49`) is dereferenced by ~332 occurrences across 39 gameplay files. `GameWorld::tick()` silently returns if `current_game == nullptr` (no error, no crash — just skips the tick). The `obmap` collision code dereferences `current_game->world->rng_` directly during spatial queries (`obmap.cpp:276`). Entity death callbacks (`walker::death()`) and factory callbacks (`entity_configurator`) also read `current_game`.

**Add a `GameplayContextGuard` RAII type** (~20 lines) that installs and uninstalls `current_game`:

```cpp
// include/openglad/gameplay/gameplay_context.h

class GameplayContextGuard {
public:
    explicit GameplayContextGuard(GameplayContext* ctx) : prev_(current_game) {
        assert(current_game == nullptr || current_game == ctx &&
               "Re-entrant GameplayContext installation — context switch bug");
        current_game = ctx;
    }
    ~GameplayContextGuard() { current_game = prev_; }
    GameplayContextGuard(const GameplayContextGuard&) = delete;
    GameplayContextGuard& operator=(const GameplayContextGuard&) = delete;
private:
    GameplayContext* prev_;
};
```

The debug assert catches context-switch bugs immediately (e.g., server context active when client code runs) rather than as mysterious desync hours later. When server and client run on the same thread (InProcessTransport), `current_game` points to whichever world most recently installed itself — if any code path forgets to swap contexts, this assert fires.

**Mandatory requirement:** `current_game` MUST point to the client's mirror world (with valid `world`, `sim_events`, and `config` pointers) before `apply_snapshot()` runs. Use `GameplayContextGuard` to install the context. If server and client run on separate threads, each thread must install its own `current_game` (it's `thread_local`).

### Bypass death path entirely during entity removal

Entity removal during `apply_snapshot()` must NOT trigger death callbacks. The death callback chain is deep and creates serious side effects:

1. `walker::death()` (`walker.cpp:1294-1379`): creates treasure entities (life gems at line 1318), spawns 4 explosion FX entities for generators (line 1356-1358), creates bloodstain entities (line 1390), emits sounds and `EndGame` events, consumes RNG (3 calls per generator explosion iteration: `rng_.next(sizex-8)`, `rng_.next(sizey-8)`, `rng_.next(3)`)
2. `effect::death()` (`effect.cpp:132-146`): delegates to family descriptor `on_death` callbacks — e.g., `bomb_on_death` (`effect_family_bomb.cpp:17-30`) creates explosion entities and emits sounds; `ghost_scare_on_death` has its own side effects
3. `weap::death()` (`weap.cpp:142-159`): delegates to weapon family descriptor `on_death` callbacks — e.g., `projectile_explode_on_death`, `rock_on_death`, `knife_on_death`, `door_on_death` — various entity creation
4. `living::death()` (inherits `walker::death()`): family-specific `on_death` callbacks — e.g., `slime_on_death` (`family_slime.cpp:30-48`) creates smaller slime entities AND transfers `myguy` ownership; `fire_elemental_on_death` creates fire effects

All of these would corrupt the state being applied by creating new entities, consuming RNG, and emitting events.

**Solution:** When removing entities not present in the snapshot, bypass the death path entirely. These entities aren't "dying" in the gameplay sense — they're being replaced by authoritative server state. Implementation:
- Remove from `obmap` via `obmap::remove()` (spatial index cleanup is correct and necessary)
- Remove from `id_index_`
- Erase from the entity list directly (destroying the `unique_ptr` calls `walker::~walker()`)
- `walker::~walker()` (at `src/interface/walker_render_bridge.cpp:97-116`) clears cross-reference pointers, removes from obmap (idempotent if already removed), and resets render/stats — all safe. It does NOT call `death()`.
- The key insight: `~walker()` and `death()` are separate paths. `death()` is called during gameplay when an entity's HP reaches zero. `~walker()` is called when the owning `unique_ptr` is destroyed. During snapshot application, we only trigger `~walker()` (via list erasure), never `death()`.

Additionally, suppress `SimEventLog::push()` during `apply_snapshot()` to prevent stale events from any incidental code paths. Add a `bool suppressed_ = false` flag **on `SimEventLog` itself** (not on `GameWorld`) — `SimEventLog` is where `push()` lives, so that's where the check belongs. Use an RAII guard to manage the flag:

```cpp
// include/openglad/gameplay/sim_event_log.h

class SimEventLogSuppressGuard {
public:
    explicit SimEventLogSuppressGuard(SimEventLog& log) : log_(log) {
        log_.set_suppressed(true);
    }
    ~SimEventLogSuppressGuard() { log_.set_suppressed(false); }
    SimEventLogSuppressGuard(const SimEventLogSuppressGuard&) = delete;
    SimEventLogSuppressGuard& operator=(const SimEventLogSuppressGuard&) = delete;
private:
    SimEventLog& log_;
};
```

`apply_snapshot()` creates a `SimEventLogSuppressGuard` at the top of the function. If `apply_snapshot()` returns early or throws, the destructor clears the flag — no risk of `suppressed_` getting stuck true and silently eating all future events.

**Changes:**
- Implement `void apply_snapshot(GameWorld&, const WorldSnapshot&)` in `src/gameplay/world_snapshot.cpp`

**Operation sequence (order matters):**
1. Set `applying_snapshot_ = true`
2. Install `GameplayContextGuard` for the target world's context
3. Restore GameWorld-level state fields (game flow flags, timers, scores, `control_hp`, `withdraw_requested`, `withdraw_level`, `guy_id_counter`, `current_palette_id`, `pending_exit_prompt`)
4. Update local `guy` lookup table from snapshot's `guy` data (exp, kills, etc.)
5. **Remove missing entities:** Walk existing entity lists, remove any entity whose `entity_id_` is not in the snapshot. Call `obmap::remove()`, erase from `id_index_`, then erase from list (which destroys the `unique_ptr` -> calls `~walker()`, NOT `death()`).
6. **Update existing entities:** For entities present in both world and snapshot (matched by `entity_id_`), overwrite all snapshot fields. Clear `stats_->commands` (AI command queue) — the client never runs `tick()` so commands are never consumed, but clearing prevents unbounded growth from stale initial-load commands. **Ensure both integer positions (`xpos`/`ypos`) and float positions (`worldx_`/`worldy_`) are written before updating the obmap** — the obmap indexes by `xpos`/`ypos`, so stale integer positions cause incorrect spatial queries. **If order/family changed** (e.g., slime transform), call `entity_configurator()` to update sprite/animation data before overwriting fields — same as `transform_to()` does internally. No subclass change is needed because `transform_to()` only changes family within the same Order in practice. **Note:** `entity_configurator()` loads sprite pixel data from campaign files — this is an I/O operation in the hot path. Since `transform_to()` events are rare (slime splits, wave evolution), the I/O cost is negligible in practice. If profiling shows otherwise, cache the configurator results per (order, family) pair.
7. **Create new entities:** For entity IDs in snapshot but not in world, use factory callbacks: `entity_factory()` -> `entity_configurator()` -> overwrite all snapshot fields. Add to `id_index_`. Insert into obmap via `obmap::add()`.
8. **Resolve cross-references:** Walk all entities, resolve `foe_id`, `leader_id`, `owner_id`, `collide_ob_id`, `controller_id` to pointers via `id_index_` (O(1) per lookup). **IMPORTANT: This step is deliberately deferred until ALL entities (existing + new) are in `id_index_`.** New entities created in step 7 may reference other new entities (e.g., a freshly spawned weapon's `owner_id` pointing to a freshly spawned living entity). If cross-references were resolved during creation (step 7), the target entity might not exist yet. Resolving all cross-references in a single pass after all entities are present guarantees correctness regardless of creation order. Future maintainers must not "optimize" by resolving cross-refs during entity creation.
9. **Update obmap for moved entities:** For entities that existed before and changed position, call `obmap::add()` (the defensive remove-then-add in `obmap::add()` handles this).
10. Reconstruct `ani` pointer from `(order, family, ani_type)` for new/updated entities
11. Reconstruct `myguy` pointer from `guy_id` via local guy lookup table
12. Apply grid dirty tiles from snapshot (overwrite `grid.data[]` entries)
13. **Reorder entity lists to match snapshot order.** Snapshot vectors preserve server list iteration order. After steps 5-7, the client's lists may have entities in a different order (existing entities stay in their original list positions, new entities are appended). Walk each list and reorder to match the snapshot vector's entity_id sequence. This preserves tick-order determinism for future lockstep/prediction work and makes divergence debugging easier. Implementation: build an `entity_id -> list::iterator` map, then `splice()` nodes into a fresh list in snapshot order. O(N) with no reallocation.
14. **Clear `dead_list`.** The client never runs `GameWorld::tick()`, so nothing ever moves entities to `dead_list` on the client side. Entities removed in step 5 are destroyed directly. Clear `dead_list` after each apply to prevent unbounded growth from any stale entries.
15. `SimEventLogSuppressGuard` destructor restores `suppressed_ = false` (RAII — no manual cleanup needed)

**Entity creation via factory callbacks:**
GameWorld creates entities through three callbacks set up by the platform layer (`game_world.h:148-150`):
- `entity_factory(Order, int32_t family)` — allocates the correct subtype (`living`/`weap`/`treasure`/`effect`)
- `entity_configurator(walker&, Order, int32_t)` — loads sprite data, sets `frames`
- `entity_derived_stats(walker*, Order, int32_t)` — computes derived stats from base stats

`apply_snapshot()` MUST use these callbacks to create new entities (not raw `new walker()`), because:
1. The correct subclass must be instantiated (`weap` for weapons, `living` for NPCs, etc.)
2. `entity_configurator` sets up sprite/animation data that the skip-list fields (`ani`, `frames`) depend on
3. `entity_derived_stats` initializes baseline stats that the snapshot then overwrites

Flow for a new entity in a snapshot: `entity_factory()` -> `entity_configurator()` -> overwrite all snapshot fields -> resolve cross-references. The factory callbacks must be available on the client's GameWorld (they are, since the client has a full GameSession with rendering support).

**Error handling for factory callbacks:** If `entity_factory()` returns nullptr (e.g., unknown order/family combination due to version mismatch), log an error and skip that entity rather than crashing. The client will be missing that entity until the next keyframe. This is a graceful degradation — the game continues with a visual glitch rather than a crash.

**Entity recycling — the common path:** Most entities persist across consecutive snapshots. Steps 5-7 above handle three cases: remove (entity gone), update (entity persists), create (entity new). The **update** path (step 6) is the common case and is cheap — just field overwrites on an existing allocation. Only genuinely new entity IDs (never seen in any previous snapshot) trigger factory creation. This naturally avoids the allocation churn concern for projectiles that live across multiple ticks.

**obmap spatial index update strategy:**
The obmap uses `std::map<pair<short,short>, list<walker*>>` + `std::unordered_map<walker*, list<pair<short,short>>>` — two-way mapping between grid cells and entities. `obmap::add()` is defensive: if the entity already exists in `walker_to_pos`, it calls `remove()` first (idempotent). This means it's safe to call `add()` for both new and moved entities without tracking position deltas.

Simplified strategy (preferred over manual diffing for robustness):
- **Removed entities:** `obmap::remove()` before erasing from list (step 5)
- **New entities:** `obmap::add()` after field overwrite (step 7)
- **Moved entities:** `obmap::add()` after field overwrite (step 6 — the defensive remove-then-add handles this)

This is O(N) in total entities. For ~200 entities at 12 ticks/sec, this is negligible.

**Entity allocation churn:**
Projectiles spawn and die constantly. At 12 ticks/sec with 4 players + NPCs firing, expect 20-50 entity creates/destroys per second. Modern allocators handle this volume fine. **Deferred optimization:** if profiling (Phase 9 benchmark or later playtesting) shows allocation is a bottleneck, add a simple free list of recycled walker allocations (per-subclass: `weap` pool for projectiles, `effect` pool for FX). The pool would be a `std::vector<std::unique_ptr<walker>>` on GameWorld — pop to reuse, push on "death" instead of deleting. Do NOT build this upfront.

**`dead_list` and obmap invariant:** Skipping `dead_list` in snapshots is safe because dead entities are removed from the obmap during their `death()` callback chain (before being moved to `dead_list` in `GameWorld::tick()`). Dead entities in `dead_list` have no spatial presence and don't affect collision queries. The snapshot captures only live entities from `oblist`/`fxlist`/`weaplist`. Dead player entities (`myguy != nullptr`) are the exception — they stay in `oblist` (not moved to `dead_list`), so they ARE captured.

**Verify:** Round-trip test — capture snapshot, clear world, apply snapshot, capture again, assert identical. Test across entity death/spawn events. Verify obmap is consistent after apply (spatial queries return correct results). Verify `do_bounce` round-trips correctly for weapon entities. Verify `regen_delay_` and `path_check_counter` round-trip correctly. Verify no death callbacks fire during entity removal (no new entities created, no events emitted, no RNG consumed). Verify grid dirty tiles round-trip correctly. Verify `transform_to` scenario: capture a snapshot where a slime has transformed to FAMILY_SMALL_SLIME, apply to a world that still has FAMILY_SLIME, verify family updated and sprite reconfigured.

---

## Phase 8: Snapshot Serialization + Delta Compression

Convert WorldSnapshot to/from byte stream for transport, including both full keyframe and delta formats. This phase also completes the dirty tracking instrumentation deferred from Phase 3.

### Dirty Tracking Instrumentation (prerequisite for delta compression)

Before delta compression can work, **all ~200-400 remaining field mutation sites** across `src/gameplay/` must call `mark_dirty()`. The infrastructure (`dirty_mask_[2]`, `mark_dirty()`, bit constants) has existed since Phase 2, and the 5 cross-reference setters already call `mark_dirty()`. This step instruments everything else:

- Position fields (`xpos`, `ypos`, `worldx_`, `worldy_`): already go through `setxy()` / `set_world_pos()` — add `mark_dirty` there (~5-10 call sites)
- High-churn fields (`hitpoints`, `action`, `frame`, `curdir`, `busy`, `cycle`): add `mark_dirty` at mutation sites in combat code, `act()` methods, animation updates (~100-200 sites)
- Rarely-changing fields (`team_num`, `family`, `order`, `armor`, `level`): add `mark_dirty` at the few mutation sites (~20-30 sites)
- **Total: ~200-400 sites.** All mechanical — no design decisions, just adding a one-liner next to existing assignments.

Fields do NOT need to be made private for dirty tracking. The `mark_dirty()` call is added alongside the existing direct assignment. Other fields remain public with `mark_dirty()` as a convention enforced by the CI safety-net test (below in this phase).

The CI safety-net test (see below) is the **hard gate** for this instrumentation — it must pass before any networking code uses dirty-based deltas. Placing both the instrumentation and the test in the same phase ensures no window where bugs are latent.

### Manual Binary Serialization

Serialization is driven by the `constexpr` field descriptor table from Phase 5 (`snapshot_fields.h`). No external serialization library is needed — the field table makes generic ser/deser straightforward:

```cpp
// Generic field serialization -- ~50 lines total
void serialize_fields(std::vector<uint8_t>& buf, const EntitySnapshot& snap,
                      const uint64_t dirty_mask[2]) {
    for (const auto& field : SNAP_FIELDS) {
        if (!(dirty_mask[field.bit_index / 64] & (1ULL << (field.bit_index % 64))))
            continue;
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&snap) + field.snap_offset;
        buf.insert(buf.end(), src, src + field.size);
    }
}

void deserialize_fields(const uint8_t*& cursor, EntitySnapshot& snap,
                         const uint64_t dirty_mask[2]) {
    for (const auto& field : SNAP_FIELDS) {
        if (!(dirty_mask[field.bit_index / 64] & (1ULL << (field.bit_index % 64))))
            continue;
        uint8_t* dst = reinterpret_cast<uint8_t*>(&snap) + field.snap_offset;
        std::memcpy(dst, cursor, field.size);
        cursor += field.size;
    }
}
```

**Endianness:** The game targets x86/x64 (native) and WebAssembly (Emscripten) — all little-endian. Write serialization using explicit `htole32`/`le32toh`-style helpers (or inline equivalents keyed on `field.size` — 2/4/8 byte swaps) from the start. On little-endian architectures these compile to no-ops via compiler intrinsics, so there's zero runtime cost, but the code is correct-by-construction if the game ever targets ARM big-endian or runs a dedicated server on exotic hardware. This costs ~10 extra lines in the serialization helpers and avoids a subtle portability landmine.

**Advantages over msgpack:**
- Zero dependencies — no vendored library
- ~200 lines total for full ser/deser (field table does the heavy lifting)
- Maximally compact wire format (no type tags, no length prefixes per field — bit indices in dirty_mask implicitly encode field identity and type)
- Trivially debuggable (hex dump maps directly to field table offsets)
- Endianness is explicit, not hidden behind a library

### Full Keyframe Serialization

**Changes:**
- Add `serialize_snapshot()` / `deserialize_snapshot()` to `src/gameplay/world_snapshot.cpp`
- Outer envelope: protocol version byte + snapshot format version byte + binary payload
- Apply zlib compression on top of the binary payload (vendored at `third_party/physfs/zlib123/`, CMake target `og_ext_zlib`) — expect 60-80% compression ratio
- `serialize_snapshot()` returns `std::vector<uint8_t>` (compressed)
- `deserialize_snapshot()` takes `const uint8_t*, size_t` (decompresses internally)
- Protocol version byte at offset 0 of every serialized message (input, snapshot, lobby) — baked in from the start, not retrofitted later
- **Snapshot format version:** A separate snapshot format version byte inside the snapshot payload (distinct from the protocol version in the message header). Snapshot layout will evolve independently of the wire protocol (adding fields, changing layouts). This lets you handle "same protocol, different snapshot format" gracefully during development. **Version mismatch handling:** if a client receives a snapshot with an unrecognized format version, it disconnects with a descriptive error ("server snapshot format v3, client supports v2 -- please update"). No attempt at backward-compatible decoding.
- **Keyframe format:** All entity fields included, `dirty_mask = all bits set`.

### Delta Compression (Setter-Based Dirty Tracking)

Full keyframes (~40KB) every tick waste bandwidth when most entities don't change between ticks. Delta compression sends only changed fields per entity. Unlike comparison-based delta computation (which diffs two full snapshots), this design uses **setter-based dirty tracking**: dirty bits are set at the source during `GameWorld::tick()` (via `mark_dirty()` calls instrumented in this phase's prerequisite step above) and read during snapshot capture (Phase 6). **There is no `compute_delta()` function.** The delta is built directly from the dirty bits that gameplay code already set.

**Server-side per-client state:**

The server maintains per-client accumulated dirty state:

```cpp
struct PerClientState {
    uint32_t last_sent_tick = 0;
    // Per-entity accumulated dirty masks since last sent snapshot.
    // Each tick: OR this tick's entity dirty bits into the map.
    // On send: use accumulated mask to select fields, then clear.
    std::unordered_map<uint32_t, uint64_t[2]> accumulated_dirty;
    // Entity IDs that appeared since last sent snapshot.
    std::vector<uint32_t> new_entity_ids;
    // Entity IDs that disappeared since last sent snapshot.
    std::vector<uint32_t> removed_entity_ids;
};
```

**Per-tick server flow:**

1. `world_.tick()` — gameplay code sets dirty bits via `mark_dirty()` calls
2. `capture_snapshot()` — copies each entity's `dirty_mask_[2]` into the snapshot, clears entity dirty bits, drains `removed_entity_ids_` from GameWorld
3. **Accumulate per client:** For each entity in the snapshot, OR the entity's dirty mask into `accumulated_dirty[entity_id]` for every client. For new entities (in snapshot but not in any client's `accumulated_dirty`), add to `new_entity_ids`. For removed entities (from the drained list), add to `removed_entity_ids`.
4. **Build delta for each client:** Use that client's `accumulated_dirty` as the per-entity dirty mask. New entities get all-bits-set. Removed entities get the zero-mask sentinel.
5. **Serialize and send delta** to each client.
6. **Clear client state:** After sending, clear `accumulated_dirty`, `new_entity_ids`, `removed_entity_ids` for that client.

This means a client that's 3 ticks behind (missed 2 deltas) automatically gets the union of all dirty bits for those 3 ticks in its next delta — no special "catch-up" logic needed.

**Wire format:**
```
[0]    uint8_t  protocol_version
[1]    uint8_t  message_type (DeltaSnapshotMessage or SnapshotMessage for keyframe)
[2..3] uint16_t payload_length
[4..7] uint32_t server_tick (current tick number)
[8..]  zlib-compressed delta payload
```

Note: `baseline_tick` from the comparison-based design is replaced by `server_tick`. Since dirty bits are accumulated per-client, there's no "baseline snapshot" to reference — the server tracks what each client has seen via the accumulated mask. The client doesn't need to confirm which baseline it holds; the server knows.

**Delta payload per entity:**
```
uint32_t entity_id
uint64_t dirty_mask[2]  (both zero = removed entity sentinel)
[raw field bytes for each set bit, in bit-index order per SNAP_FIELDS table]
```

**Changes:**
- Add `PerClientState` struct to `include/openglad/gameplay/game_server.h`
- Add `apply_delta()`, `serialize_delta()`, `deserialize_delta()` to `src/gameplay/world_snapshot.cpp`
- **No `compute_delta()` function.** Delta masks come directly from accumulated dirty bits.
- Field-to-bit mapping: uses the same bit index constants from `dirty_field_bits.h` (Phase 2) and `SNAP_FIELDS` constexpr table (Phase 5). 19 SimEntity fields + 44 walker fields + 22 stats fields + 1 weap field = **86 fields** -> `uint64_t dirty_mask[2]` (128 bits). First 64 fields in `dirty_mask[0]`, remaining 22 in `dirty_mask[1]`. When `dirty_mask[1]` is all-zero for a given delta (common case — most entities don't touch stats fields), it can be omitted on the wire with a flag bit.

**Expected savings:**
- Typical tick: ~20 entities change out of ~200, ~5-10 fields change per entity
- Delta size before zlib: ~2-5KB (vs ~40KB full)
- After zlib: ~1-3KB
- At `DEFAULT_SIM_TICKS_PER_SEC` ticks/sec, 4 clients: ~50-150KB/sec outbound (vs ~600-800KB/sec with full snapshots + zlib)

**zlib bypass for tiny deltas:** zlib adds ~11 bytes of header overhead. For very small deltas (<64 bytes uncompressed — e.g., only 1-2 entities changed a single field), compression may increase size. Add a flag bit in the message header: if set, payload is uncompressed. `serialize_delta()` compresses, checks if compressed size >= uncompressed size, and sends whichever is smaller.

**CI safety-net test (critical — catches missed `mark_dirty()` calls):**

The dirty-tracking design has an inherent correctness risk: if a `mark_dirty()` call is missed at a mutation site, that field silently stops updating on clients. Periodic keyframes mask this (every `KEYFRAME_INTERVAL_TICKS` ~5 seconds), but the field is stale between keyframes.

Add a **comparison-based validation test** that runs alongside the dirty-bit path in CI builds:

```cpp
// tests/test_dirty_tracking_safety.cpp
// For each tick in a combat scenario:
// 1. Capture snapshot using dirty bits (the real path)
// 2. Capture a second "reference" snapshot by brute-force comparing all fields
//    against the previous tick's full snapshot
// 3. Assert the dirty-bit snapshot's mask is a SUPERSET of the reference mask
//    (extra dirty bits are OK — conservative. Missing bits = bug.)
```

This test runs a worst-case 4-player combat scenario (many entity spawns, deaths, projectiles, explosions, AI actions, speed changes) for 200+ ticks and validates every single entity's dirty mask against the brute-force reference. If any `mark_dirty()` call was missed during the instrumentation pass (this phase's prerequisite step), this test catches it.

**This test is a hard gate for Phase 8 completion.** It must pass before any networking code uses dirty-based deltas.

**Verify:** Unit test — round-trip full snapshots through bytes. Build delta from accumulated dirty masks, apply delta to client baseline, assert result matches server state. Full pipeline test: capture -> serialize -> deserialize -> apply -> capture -> assert match. Test with: no changes (empty delta), all changes (equivalent to keyframe), entity spawn, entity removal, multi-tick accumulation (client misses 3 ticks, next delta has union of all dirty bits). Verify round-trip through serialization. Verify compression ratio is within expected range (cross-check with Phase 9 benchmark). CI safety-net test passes on worst-case combat scenario.

---

## Phase 9: Snapshot Size Benchmark

Before committing to the serialization format, measure actual snapshot sizes on real game data. This validates the bandwidth budget and catches size estimate errors early.

**Changes:**
- Add a test or small utility that:
  1. Loads a level (`read_scenario()`) and runs 100 ticks of gameplay
  2. Calls `capture_snapshot()` (from Phase 6)
  3. Reports: entity count per list, raw `EntitySnapshot` size (bytes), total `WorldSnapshot` size, zlib-compressed size
  4. Runs 10 more ticks, captures again, computes a delta, reports delta size before/after zlib
- Can be a unit test or a standalone benchmark (e.g., `tests/unit/test_snapshot_size.cpp`)
- **Use a worst-case combat scenario** (4-player chaotic combat with many projectiles, explosions, and entity spawns) — not a quiet level. The bandwidth budget must hold under peak load.

**Expected results:**
- ~200 entities total across oblist/fxlist/weaplist
- ~200 bytes per packed entity (19 SimEntity + 44 walker + 22 stats fields, mix of floats/shorts/chars/ints)
- ~40KB per full snapshot uncompressed
- ~8-16KB after zlib (60-80% compression on structured data)
- ~2-5KB per delta uncompressed, ~1-3KB after zlib

If actual sizes differ significantly from these estimates, revisit the bandwidth budget and delta compression strategy before proceeding to transport phases.

**Verify:** Benchmark runs, numbers are reported. Update bandwidth budget section if needed.

---

## Phase 10: Input Replay System

With deterministic RNG (Phase 0), InputState serialization (Phase 4), and snapshot infrastructure (Phases 5-8), the codebase has everything needed for an input replay system. This is invaluable for debugging desync issues during later networking phases — record a game on the server, replay it offline to reproduce divergence.

**Changes:**
- New files: `include/openglad/gameplay/replay.h`, `src/gameplay/replay.cpp`
- Add to `OG_SIM_SOURCES` in CMakeLists.txt

**`ReplayRecorder`:**
- On game start: record initial RNG seed (`SimRandom::state_`), level ID, player count, `timer_wait`
- Each tick: append the `InputState` (serialized via Phase 4 bitpacking — ~17 bytes per tick)
- On game end: write to file (`.ogr` — OpenGlad Replay)
- File format: header (version, seed, level_id, player_count, timer_wait) + sequence of serialized InputState frames
- At 12 ticks/sec, a 10-minute game = ~7200 ticks × 17 bytes = ~120KB uncompressed. Trivially small.

**`ReplayPlayer`:**
- Load replay file, seed RNG, load level
- Each tick: deserialize the next `InputState` from the replay, feed to `GameWorld::tick()` as if it were live input
- Verify: if a `WorldSnapshot` was recorded at intervals (optional — every N ticks), compare against current state to detect divergence. Log the first differing field + tick number.

**Integration with Phase 9 benchmark:**
The replay system can also be used to create reproducible benchmark scenarios: record a chaotic 4-player combat session, replay it to measure snapshot sizes under identical conditions across code changes.

**Verify:** Record a short game (load level, tick 100 times with scripted input), save replay file, play it back, verify final world state matches. Test with multiple player counts. Verify replay produces identical RNG state at each tick.

---

## Phase 11: Decouple Sim Tick from Render Frame

**Medium refactor.** The Emscripten build has a reference accumulator implementation, but it lives in a different function (`emscripten_frame_wrapper()` in `glad.cpp:133-223`) from the native game loop (`game_frame_with_result()` in `game_loop.cpp:39-168`). This phase restructures the native game loop — not just removing `#ifdef` guards.

Currently `game_frame_with_result()` does exactly 1 tick (`s.act()` at line 67) + 1 render (`s.redraw()` at line 81) per call on native, paced by `time_delay()` (`src/core/util.cpp:132-152`, converts delay ticks at 13.6ms each to microseconds, sleeps then spin-waits for precision). The Emscripten accumulator is a separate code path that can't be copy-pasted — it drives a state machine rather than a blocking loop.

**Complexity notes:**
- `game_frame_with_result()` currently returns `GameFrameResult` for a single frame. With an accumulator, it may execute 0-N ticks per call (0 if not enough time has accumulated, >1 if the frame took long). The return value semantics need to handle "done" detected during any of those ticks.
- Input polling (`ctx().poll_input()` at line 135, `s.process_input()` at line 138) currently happens once per call, AFTER the tick. With an accumulator, input should be polled once per call (before the tick loop), and each tick consumes the same input snapshot. This matches the networking model where input is sampled once and sent to the server.
- The `time_delay()` FPS cap (lines 152-164) becomes unnecessary when the accumulator manages timing. But `timer_wait` still determines the tick interval: `tick_interval_ms = timer_wait * 13.6f`.

**Changes:**
- Remove `#ifdef __EMSCRIPTEN__` guards from `last_frame_time`/`accumulated_time` fields in `GameLoopFrameState` (`include/openglad/interface/game_loop_state.h`) — make them available on all platforms
- Add `uint32_t fixed_tick_ms` to `GameLoopDeps` (`include/openglad/platform/game_loop.h:22-34`). Default to `DEFAULT_SIM_TICK_MS` from `net_constants.h`. When 0, derive from `timer_wait * TIMER_WAIT_TO_MS` (backward compat with current per-frame behavior).
- Restructure `game_frame_with_result()` in `src/platform/sdl/game_loop.cpp`:
  1. Measure elapsed time since last call (using `SDL_GetTicks()` or `steady_clock`)
  2. Add to accumulator
  3. Compute tick interval: `fixed_tick_ms > 0 ? fixed_tick_ms : timer_wait * TIMER_WAIT_TO_MS`
  4. While accumulator >= tick_interval: poll input (once before loop), call `s.act()`, subtract interval. Cap at 4 ticks per call to prevent spiral-of-death (if a frame takes 500ms, don't try to catch up with 6 ticks — clamp to 4 and drop the rest).
  5. Render once
  6. Remove old `time_delay()` FPS cap when accumulator is active
- Default `fixed_tick_ms=0` means "derive tick interval from timer_wait" (backward compat — existing behavior unchanged, just accumulator-driven instead of sleep-driven)

**Input timing reorder (intentional behavioral change):**
The current code polls input AFTER the tick: `s.act()` (line 67) → `ctx().poll_input()` (line 135) → `s.process_input()` (line 138). This means input from frame N is consumed in frame N+1's tick. The restructured accumulator loop polls input ONCE before the tick loop, so input from frame N is consumed in frame N's tick(s). This reduces input latency by one frame (~83ms at default speed) — **better** for networked play, and matches the networking model where input is sampled once and sent to the server before the tick. This is an intentional improvement, not a bug, but it's a subtle behavioral change that could affect speedrun timing or tight input sequences in tests.

**Verify:** Default behavior unchanged (game feels the same). With `fixed_tick_ms=83` (matching `DEFAULT_SIM_TICK_MS`), game runs at same speed. Adjust `timer_wait` up/down, verify game speed changes proportionally. All existing tests pass. If any test is sensitive to the 1-frame input latency reduction, update the test — the new timing is correct.

---

## Phase 12: ITransport Interface + Protocol Framing

Abstract transport for server/client communication.

**Changes:**
- New header: `include/openglad/gameplay/net_transport.h`
- `ITransport` interface: `send(peer_id, data, len)`, `poll() -> vector<ReceivedMessage>`, `accept_connections()`, `disconnect(peer_id)`, `connected_peers()`
- `ReceivedMessage` struct: peer_id + data buffer

**Message framing (all messages share this header):**
```
[0]    uint8_t  protocol_version (currently 1)
[1]    uint8_t  message_type (NetMessageType enum)
[2..3] uint16_t payload_length
[4..]  payload bytes
```

- `NetMessageType` enum: `Hello`, `InputMessage`, `SnapshotMessage`, `DeltaSnapshotMessage`, `SimEventBatchMessage`, `GameFlowEventBatchMessage`, `LobbyMessage`, `LobbyStateMessage`, `InitialSetup` (guy data + level metadata + controlled entity_ids per player), `ClientReady`, `KeyframeRequest`, `Heartbeat`, `ExitPromptBroadcast`, `ExitPromptResponse`, `PauseBroadcast`, `PauseResponse`, `ControlChange` (player switched/died — new controlled entity_id)

**Connection handshake (`Hello` message):**
On connect, client and server exchange `Hello` messages containing:
- Protocol version + snapshot format version (mismatch -> disconnect with descriptive error)
- **Min supported protocol version** (1 byte) — reserved for future version-range negotiation. For v1, `min_version == current_version`. In the future, if protocol v3 is backward-compatible with v2, the server can advertise `min=2, current=3` and clients supporting v2-v3 can negotiate. This byte is free to include now and avoids a breaking handshake change later.
- **Session token** (16 bytes, random) — assigned by the server during the initial `Hello` response. Stored by the client for reconnection purposes. On reconnect, the client includes its session token in the `Hello` message; the server matches it against disconnected player slots (see Phase 30 reconnection protocol). First-time connections send a zero token.
- **Campaign content hash** — a CRC32 or similar hash of the campaign's level file listing + file sizes. This catches campaign file mismatches at connection time rather than as mysterious missing entities during `read_scenario()`. If the hash doesn't match, disconnect with "campaign version mismatch — ensure all players have the same campaign files."

The `Hello` exchange happens before any other messages.

**`ClientReady` message:**
Sent client->server after the client has processed `InitialSetup` and the first full keyframe (or after loading a new level during level transitions). The server MUST NOT send deltas to a client until it receives `ClientReady` — otherwise the server sends deltas against a baseline the client hasn't received yet. During the wait, the server queues or drops deltas for that client (full keyframe will be sent on `ClientReady`).

**`KeyframeRequest` message:**
Sent client->server when the client detects a gap in the `server_tick` sequence (missed a delta and its local state may be stale). The server responds with a full keyframe on the next tick and resets that client's `PerClientState`. This is faster than waiting for the next periodic keyframe (~5 seconds).

**`ExitPromptBroadcast` message (server->client):**
Sent when the server enters `pending_exit_prompt` state (see Phase 14). Contains the prompt text, destination level, and whether it's a withdraw prompt. All clients display the prompt.

**`ExitPromptResponse` message (client->server):**
Sent by any client in response to `ExitPromptBroadcast`. Contains accept/decline. First response from any player resolves the prompt.

**Verify:** Compiles. Mockable in tests.

---

## Phase 13: In-Process Transport

Direct function call transport for local play — `send()` on one side enqueues to the other's receive buffer.

**Changes:**
- New files: `include/openglad/gameplay/net_transport_inprocess.h`, `src/gameplay/net_transport_inprocess.cpp`
- `InProcessTransport` creates linked pairs (server-side + client-side)
- **Zero-copy: bypasses serialization entirely.** For local play, `InProcessTransport` passes `shared_ptr<WorldSnapshot>` (and `shared_ptr<InputState>`) directly between server and client — no serialize/deserialize round-trip. This keeps local play's per-frame overhead near-zero compared to pre-networking. The `ITransport` interface provides both a typed API (`send_snapshot(shared_ptr<WorldSnapshot>)`, `send_input(shared_ptr<InputState>)`) and a raw bytes API (`send(peer_id, data, len)`). `InProcessTransport` implements the typed API; `WebSocketServerTransport`/`WebSocketClientTransport` implement the raw bytes API (which goes through serialization). `GameServer` and `GameClient` use whichever API the transport supports. The serialization path is exercised by unit tests and networked play.

**`ValidatingInProcessTransport` (CI mode):**
The zero-copy `InProcessTransport` bypasses serialization entirely, which means the serialization path (Phase 8) is only exercised by unit tests until WebSocket transport arrives in Phase 24 — an 11-phase coverage gap. Add a `ValidatingInProcessTransport` variant that **serializes and deserializes every message** (snapshot, delta, input, events) through the Phase 8 binary serialization path, then passes the deserialized copy to the recipient. This turns every integration test into a serialization round-trip test.

- Enabled via a constructor flag or CMake option (e.g., `-DVALIDATE_SERIALIZATION=ON` in CI builds, off by default for local dev performance)
- Uses the same `ITransport` interface — `GameServer`/`GameClient` don't know they're being validated
- If any field fails round-trip (e.g., endianness bug, missed field in serialization table, non-trivially-copyable type sneaks in), the test fails immediately with the field name and diff
- **Zero production impact:** only active in CI builds. Local play always uses zero-copy `InProcessTransport`.

**NetworkTestFixture:**
Build a reusable `NetworkTestFixture` class alongside `InProcessTransport` — this is the primary way to validate everything from Phase 13 onward:
- Creates a `GameServer` + N `GameClient`s with `InProcessTransport` (or `ValidatingInProcessTransport` in CI)
- Loads a level on the server
- Steps N ticks (server ticks, clients receive snapshots)
- Verifies client world state matches server (entity counts, positions, field values)
- Parameterized: player count, level, tick count, input sequence

This fixture is used by all subsequent phases' integration tests. Without it, "all existing tests pass" doesn't validate any networking code.

**Verify:** Unit test — linked pair send/receive. Multi-client broadcast. Message ordering preserved. `NetworkTestFixture` runs a basic scenario (load level, tick 10 times, verify client matches server). CI builds verify serialization round-trip on every message via `ValidatingInProcessTransport`.

---

## Phase 14: GameServer and GameClient

The core server-authoritative objects. This is the convergence point where everything comes together.

**GameServer:**
- Owns `GameWorld` and a `GameSession` (for RNG, context)
- On startup: sends `InitialSetup` message to each client with `guy` character data for all players + level metadata (constant GameWorld fields: `id`, `title`, `difficulty`, `pixmaxx`, `pixmaxy`, etc.) + `completed_levels` set
- Each tick: collect `InputState` from clients via transport -> feed to viewscreen input processing -> `world_.tick()` -> `capture_snapshot()` (copies dirty bits, clears them) -> accumulate dirty masks per client -> `serialize_delta()` using per-client accumulated masks -> broadcast to all clients. Send full keyframe every `KEYFRAME_INTERVAL_TICKS`.
- Sends `SimEventBatch` message alongside each delta/keyframe (separate message, same tick)
- Maintains per-client baseline snapshot for delta computation
- **Tracks `current_palette_id`:** When a `SetPalette` event is emitted by simulation code, the server updates `current_palette_id` in the WorldSnapshot. Clients that miss the event will converge to the correct palette on the next keyframe.

**Important: `screen::act()` actual order.** The current `screen::act()` calls `world_.tick()` **first** (line 905), **then** dispatches events from SimEventLog (lines 920-978). The server refactoring preserves this order: tick the world, capture the snapshot, drain events, then broadcast.

**Input jitter policy:**
The server does NOT wait for all client inputs before ticking. If a client's input hasn't arrived by tick time:
- **`held[]` array (movement, long-held keys):** Repeat the client's last known `held[]` state. Movement direction, held fire button, etc. persist naturally between ticks, so repeating is correct behavior in most cases.
- **`pressed[]` array (one-shot actions):** Do NOT repeat. Clear all `pressed[]` bits for the missing tick. If the late input arrives before the NEXT tick, deliver the `pressed[]` events on that next tick instead (late delivery). This prevents missed button presses (special attacks, yells, weapon switches) at the cost of 1-tick additional latency for those actions during jitter.
- **Late delivery cap:** `MAX_LATE_PRESS_TICKS = 2`. If a `pressed[]` input arrives more than 2 ticks late, discard it rather than delivering. At default speed (12 ticks/sec), 2 ticks = ~166ms — still a reasonable window for a button press. Delivering a 6-tick-late special attack (after a 500ms network spike) would cause gameplay weirdness (attacking thin air because the target moved away).
- The server tracks a `last_received_input_tick` per client. If no input arrives within `DISCONNECT_TIMEOUT_MS` wall-clock time, the client is considered disconnected and transitions to AI control (Phase 30).
- **Speed-change input filtering:** Only the host client's speed-change inputs (`timer_wait` adjustments) are accepted. Non-host clients' speed-change inputs are silently dropped.

**Exit prompt — freeze-and-ask protocol:**

The current `screen::act()` dispatches `RequestExitConfirmation` events by showing a **blocking UI prompt** (`screen.cpp:994` — `yes_or_no_prompt()`). The server cannot block on UI.

**Protocol:**
1. When simulation emits `RequestExitConfirmation` (player walks onto exit tile at `treasure_family_navigation.cpp:71,91`), the server enters `pending_exit_prompt` state.
2. **Server pauses simulation.** `world_.tick()` is not called while `pending_exit_prompt` is active. All entities freeze. Snapshots continue to be sent (clients need to know the game is paused), but the `pending_exit_prompt` flag is set in the WorldSnapshot.
3. Server broadcasts `ExitPromptBroadcast` to all clients with prompt text and destination level.
4. All clients display the prompt UI. **Any player** can accept or decline — this is a team decision, not per-player.
5. First `ExitPromptResponse` from any client resolves the prompt:
   - **Accept:** Server performs the level transition (withdraw or exit, same logic as `screen.cpp:996-1043`). Sends new `InitialSetup` for the next level.
   - **Decline:** Server clears `pending_exit_prompt`, resumes ticking. The triggering entity's `skip_exit` field prevents immediate re-trigger.
6. **Timeout:** If no response within `EXIT_PROMPT_TIMEOUT_MS` (15 seconds wall-clock), auto-decline and resume. Prevents a disconnected client from freezing the game forever.
7. If the player who triggered the exit dies or disconnects while the prompt is open, auto-decline.

**Networked pause — freeze-and-broadcast protocol:**

Pause in networked play uses the same freeze-broadcast pattern as exit prompts. Any player can pause.

**Protocol:**
1. Client sends `PauseBroadcast` request to server (any player, not just host).
2. Server enters `paused` state. `world_.tick()` is not called while paused. Snapshots continue to be sent (with a `paused` flag in WorldSnapshot) so clients know the game is frozen.
3. Server broadcasts `PauseBroadcast` to all clients. All clients display a "PAUSED by [player name]" overlay.
4. Any player sending `PauseResponse` (unpause) resumes the game. Server clears `paused` state, resumes ticking.
5. **Timeout:** If no unpause within `PAUSE_TIMEOUT_MS` (60 seconds wall-clock), auto-unpause. This prevents a disconnected player from freezing the game forever.
6. If the player who paused disconnects while paused, auto-unpause.
7. **Rate limit:** Minimum 5 seconds between pauses by the same player to prevent spam.

**WorldSnapshot addition:** Add `bool paused` and `uint8_t pause_player_index` to the WorldSnapshot struct (Phase 5). These are world-level state fields, same category as `pending_exit_prompt`.

**EndGame early return:**
`screen::act()` currently returns immediately from `EndGame` events (`screen.cpp:951-955`), clearing remaining events. In the server/client split, the server captures ALL events for a tick before sending them — it doesn't short-circuit. The client processes `EndGame` last (or the `GameFlowEventBatch` is ordered with EndGame at the end) to ensure no events are lost.

**Divergence detection (development diagnostic):**
Add a `snapshot_hash` field (CRC32) to each `WorldSnapshot`. The server computes it from the full snapshot data. Clients compute their own hash after `apply_snapshot()` and periodically send a `SnapshotHashCheck` message (e.g., every `KEYFRAME_INTERVAL_TICKS`) containing their local hash + tick number. The server compares; on mismatch, it force-sends a keyframe and logs a diagnostic with the tick number and entity counts. This is invaluable during development for catching desync bugs (e.g., missed fields, stale pointers, RNG drift). The hash check can be compiled out or disabled in release builds — it's ~4 bytes per keyframe interval, negligible overhead.

**GameClient:**
- Owns viewscreen(s) for local split-screen
- Stores received `guy` data in local lookup table (`std::unordered_map<int, guy>` keyed by `guy::id`) for `myguy` pointer reconstruction during `apply_snapshot()`
- Each tick: capture local `InputState` via `ctx().poll_input()` -> `serialize_input()` -> send to server
- Receives delta -> `deserialize_delta()` -> `apply_delta()` on baseline -> `apply_snapshot()` on local world mirror
- Receives full keyframe -> replaces baseline, `apply_snapshot()` on local world mirror
- If a delta's `server_tick` is not contiguous with the client's last-seen tick (gap in sequence — missed a delta), send `KeyframeRequest` to server and wait for a full keyframe before resuming delta application
- Receives `SimEventBatch` (Tier 1 cosmetic events) -> dispatches via `screen::dispatch_cosmetic_events()`. Tracks event sequence number; on gap detection, logs warning (missing a sound is tolerable).
- Receives `GameFlowEventBatch` (Tier 2 game-flow events) -> dispatches via `screen::dispatch_game_flow_events()`. These events trigger UI sequences (end-game, level exit prompts, withdrawal). Reliable delivery via TCP/WebSocket ordering guarantees.
- Receives `ExitPromptBroadcast` -> displays the exit prompt UI. Player response is sent as `ExitPromptResponse`.
- Sends `ClientReady` after processing `InitialSetup` + first keyframe, and again after each level load during level transitions.
- **Palette correction:** On each keyframe, client reads `current_palette_id` from the snapshot and sets the local palette accordingly, self-correcting any missed `SetPalette` events.
- **Snapshot hash check:** After applying a snapshot, compute CRC32 and periodically send to server for divergence detection (debug builds only).

**Level transitions:**
When the server completes a level (detected via `next_level` flag in the snapshot), it sends a new `InitialSetup` message with the next level's metadata + guy data + updated `completed_levels` (reusing the existing `InitialSetup` message type — no new message needed). The client receives this, runs `read_scenario()` to load the new level from local campaign files, sends `ClientReady`, and the server resumes sending snapshots for the new level. Between the old level's last snapshot and the new level's first snapshot, the client shows a loading/transition screen.

**Level transition event cleanup:** After `read_scenario()` loads the new level on the server, call `SimEventLog::clear()` before the first tick. Entity creation during level loading may push events (sounds, notifications) that reference the new level's entities — but no client has loaded the new level yet, so these events are meaningless and would cause "unknown entity" warnings on clients. Clear the log so the first tick starts clean. Also clear per-client `PerClientState::accumulated_dirty` (Phase 8) — the new level's entities have no relationship to the old level's dirty state.

**Level transition event gap:** Between a level's completion and the client sending `ClientReady` for the new level, the server continues ticking the new level. The server does NOT send deltas/events to a client until it receives `ClientReady` (see Phase 12). This means the client misses the first few ticks of the new level (~3-6 ticks during loading). Tier 1 cosmetic events during this gap are lost (a few startup sounds) — acceptable. Tier 2 game-flow events should not fire in the first few ticks of a fresh level. The first message after `ClientReady` is always a full keyframe, so the client catches up to current state immediately.

**Player-to-viewscreen mapping and camera tracking:**
In local play, viewscreens map 1:1 to local players. With networking, a remote client controls (say) player index 2 but renders only viewscreen 0 locally. The client needs a `local_player_indices[]` mapping that says "my local viewscreen 0 follows the entity controlled by global player index N." This mapping is established during lobby/game start.

The server sends a per-client array of **controlled `entity_id`s** in the `InitialSetup` message (alongside `guy` data and level metadata). Each entry maps a global player index to the `entity_id` that player's viewscreen should follow. The client stores this mapping and after each `apply_snapshot()`, sets `viewob[local_view]->control` to the entity with the corresponding `entity_id` (looked up via `id_index_`).

When a controlled entity dies or the player switches characters (via `sim_find_next_control()`), the server sends a **`ControlChange` message** (new message type — add to `NetMessageType` enum in Phase 12) containing the player index and new controlled `entity_id`. The client updates its mapping. This is a small, infrequent message (~8 bytes: player index + entity_id).

**GameServer callback architecture:**
GameServer lives in `og_gameplay` (no platform dependencies). Level transitions (`read_scenario`, save data sync), exit prompt resolution, and withdrawal logic currently live in `screen.cpp` (`og_interface` layer). The server accesses these via `std::function` callbacks injected by the platform layer — the same pattern as `entity_factory`/`entity_configurator`/`entity_derived_stats` on `GameWorld`:

```cpp
// GameServer callback interface (set by platform layer)
std::function<bool(int level_id)> on_level_transition;      // load next level
std::function<void()> on_save_sync;                          // sync_save_data_from_world()
std::function<bool(int destination)> on_exit_accepted;       // exit confirmation accepted
std::function<bool(int destination)> on_withdraw_accepted;   // withdrawal accepted
```

This keeps GameServer in the gameplay layer while delegating platform-specific operations upward.

**Changes:**
- New files: `include/openglad/gameplay/game_server.h`, `src/gameplay/game_server.cpp`
- New files: `include/openglad/gameplay/game_client.h`, `src/gameplay/game_client.cpp`
- Add sources to `OG_SIM_SOURCES` in CMakeLists.txt (gameplay-layer, no platform deps — flows into `OG_GAMEPLAY_COMPONENT_SOURCES` -> `og_gameplay` target)

**Verify:** Integration test — GameServer + GameClient with InProcessTransport. Load level, send input, server ticks, client receives delta/snapshot, world states match. Test with multiple clients (2-4). Test keyframe resync. Test exit prompt freeze-and-ask flow: trigger exit, verify sim pauses, send response, verify sim resumes. Test divergence detection: intentionally corrupt a client field, verify hash mismatch is detected.

---

## Phase 15: Split `screen::act()` Into Sub-Methods (Pure Refactor)

Extract the internals of `screen::act()` into three new methods, leaving `screen::act()` as a thin wrapper that calls them in sequence. **All ~894 existing tests pass unchanged** — this is a behavior-identical refactor.

**`screen::act()` refactoring:** The current `screen::act()` does: `world_.tick()` -> dispatch events (8 early return paths identified: lines 903, 955, 1017, 1031, 1036, 1043, 1066, 1068). Split into three methods:
  - `screen::tick_world()` — calls `world_.tick()`, drains SimEventLog into a `SimEventBatch` + `GameFlowEventBatch`. Returns the two batches. Used by GameServer in Phase 16.
  - `screen::dispatch_cosmetic_events(SimEventBatch&)` — dispatches Tier 1 events (sounds, notifications, palette, redraw). Used by GameClient in Phase 16.
  - `screen::dispatch_game_flow_events(GameFlowEventBatch&)` — dispatches Tier 2 events (EndGame, SetEnd, RequestExitConfirmation, WithdrawToLevel, ScoreChange). Used by GameClient in Phase 16. These trigger UI transitions like `endgame()`, exit confirmation prompts, and withdrawal sequences.

**Refactoring complexity notes:**
- `screen::act()` has **8 early return paths** (line 903: null context, 955: EndGame, 1017: withdraw load error, 1031: withdraw save error, 1036: withdraw success, 1043: exit success, 1066: world.end, 1068: normal exit). These all need to map to game-flow events or snapshot flags rather than immediate returns.
- The exit/withdraw confirmation dialog (lines 980-1048) is now handled by the freeze-and-ask protocol (Phase 14) — no blocking `yes_or_no_prompt` call on the server.
- `DamageTile` is already moved to simulation layer in Phase 6, so it's not in the event dispatch path.

**Compatibility wrapper (`screen::act()`):** After the split, `screen::act()` becomes:
```cpp
bool screen::act() {
    auto [cosmetic, game_flow] = tick_world();
    dispatch_cosmetic_events(cosmetic);
    dispatch_game_flow_events(game_flow);
    // ... viewscreen control cleanup, same as before ...
}
```

All existing tests and the game loop call `screen::act()` exactly as before. The sub-methods are new public API that Phase 16 will use.

**Verify:** All existing ~894 tests pass with zero changes. Game plays identically. The sub-methods are individually callable (tested via new unit tests).

---

## Phase 16: Wire Up GameServer/GameClient Alongside Old Path

Create the GameServer/GameClient in-process wiring, running alongside the existing `screen::act()` path. Both paths work during this phase.

**Changes:**
- `glad_init()` / `glad_main()` in `src/platform/sdl/glad_gameplay.cpp:35-114` create GameServer + GameClient(s) based on `save_data.numplayers`
- GameServer calls `screen::tick_world()` + `capture_snapshot()` + accumulates dirty masks per client + `serialize_delta()` each tick
- GameClient receives snapshots via InProcessTransport + calls `apply_snapshot()` + `dispatch_cosmetic_events()` + `dispatch_game_flow_events()`
- `GameSession` (`include/openglad/platform/game_session.h`) owns server/client objects
- `game_frame_with_result()` routes through the server/client path
- **Git tag for bisect:** Before this phase, create a `git tag pre-networking-switchover` on the commit immediately before. This provides a reference point for bisecting behavioral differences without maintaining dead code. The tag is cheaper than a feature flag and doesn't rot.

**In-process single-thread execution order:**

When server and client run on the same thread via `InProcessTransport` (local play), the exact per-frame execution order must be:

```
1. Client: capture local InputState via ctx().poll_input()
2. Client: InProcessTransport::send_input() (zero-copy: passes shared_ptr<InputState>)
3. Server: GameplayContextGuard install server context
4. Server: InProcessTransport::poll() → dequeue client input
5. Server: world_.tick() with collected input
6. Server: capture_snapshot() (zero-copy: produces shared_ptr<WorldSnapshot>)
7. Server: drain SimEventLog → build SimEventBatch + GameFlowEventBatch
8. Server: InProcessTransport::send_snapshot() (zero-copy: passes shared_ptr<WorldSnapshot> + event batches)
9. Server: ~GameplayContextGuard (restore previous context)
10. Client: GameplayContextGuard install client context
11. Client: InProcessTransport::poll() → dequeue snapshot + events
12. Client: apply_snapshot() on mirror world (no deserialize step — snapshot already in memory)
13. Client: dispatch_cosmetic_events() + dispatch_game_flow_events()
14. Client: ~GameplayContextGuard (restore previous context)
15. Render (using client's mirror world state + interpolation)
```

Steps 3-9 (server) and 10-14 (client) each have their own `GameplayContextGuard` scope. The debug assert in `GameplayContextGuard` catches context-switch bugs immediately if any step runs with the wrong `current_game` installed. Getting this order wrong produces mysterious desync — spell it out explicitly in the implementation.

- Emscripten state machine in `src/platform/sdl/glad.cpp` gets equivalent routing (the `GameState::Playing` branch at lines ~173-205)
- `screen::ready_for_battle()` (`src/interface/screen.cpp:693-720`) continues to set up viewscreens as before — the client just feeds them from snapshots instead of direct simulation

**Level editor (`openscen`) compatibility:** The editor has its own main loop (`level_editor()` at `level_editor.cpp:2995`) that never calls `screen::act()`, `game_frame()`, or `world_.tick()`. It only reads `world().grid` and `world().end` for editor purposes. Removing `screen::act()` does not break the editor build or runtime. Verified: zero `OPENSCEN` preprocessor guards exist in the codebase — the editor and game share the same source but diverge at the event loop level.

**Verify:** Game plays identically to before but internally uses server/client. Old `screen::act()` wrapper still exists but is no longer called from the game loop. Performance acceptable (InProcessTransport zero-copy adds negligible overhead). Manual playtesting with 1-4 players in split-screen.

---

## Phase 17: Migrate Tests and Delete Compatibility Wrapper

Migrate all ~145 integration tests from `game_frame()` → `screen::act()` to the GameServer/GameClient path, then delete the `screen::act()` wrapper.

**Test migration strategy:**
All integration tests that currently call `game_frame()` are migrated to use the `NetworkTestFixture` (built in Phase 13). The fixture creates a GameServer + GameClient with InProcessTransport, which exercises the real networking code path. This means every integration test validates the server/client architecture as a side effect.

**Changes:**
- Migrate all integration tests in `TEST_SOURCES` that call `game_frame()` to use `NetworkTestFixture`
- Delete `screen::act()` compatibility wrapper
- Delete any dead code paths that only existed to support the wrapper
- **No feature flag, no dead code, no commented-out reference.** The server/client + InProcessTransport path is the only path. If a bug surfaces, it gets fixed — not worked around by reverting.

**Verify:** All ~894 tests pass using the server/client path. No references to the old `screen::act()` remain (except in the level editor, which never called it). Manual playtesting confirms identical behavior.

---

## Phase 18: Client-Side Visual Interpolation

At default speed (~12 sim ticks/sec), entities update position every ~83ms. On a 60fps display, this produces visible teleporting between positions. Linear interpolation between the last two received snapshot positions makes movement smooth at render time.

**Changes:**
- Client stores the two most recent entity positions: `prev_pos` (from tick N-1) and `curr_pos` (from tick N)
- Each render frame, compute `alpha = time_since_last_tick / tick_interval` (0.0 to 1.0)
- Render entities at `lerp(prev_pos, curr_pos, alpha)` for `worldx_`/`worldy_` and `xpos`/`ypos`
- Interpolation applies ONLY to position. Other fields (frame, ani_type, action, curdir) snap to the latest value — interpolating discrete animation state causes glitches.
- When an entity spawns (first tick it appears), `prev_pos = curr_pos` (no interpolation, snap to position)
- When an entity dies, stop rendering immediately (don't interpolate toward a dead position)
- Implementation lives in the render path, not the simulation. The GameClient stores interpolation state per-entity alongside the WorldSnapshot.
- **Camera/viewport interpolation:** The viewscreen camera follows the player entity. The camera must track the *interpolated* position, not the snapped position — otherwise smooth entity movement is undermined by jerky camera. Apply the same lerp alpha to the camera's follow target.

**Verify:** Visual smoothness at 60fps with 12 tick/sec sim rate. Entities don't overshoot or rubber-band. Spawning entities appear at correct position (no lerp from origin). Dying entities disappear cleanly.

---

## Phase 19: Lobby Data Model

**Changes:**
- New header: `include/openglad/gameplay/lobby_state.h`
- `LobbyPlayer`: name, team, character slots (references to `guy` data), ready flag, is_host, player_index (global)
- `LobbyState`: player list, campaign/scenario selection, game settings (difficulty, allied_mode matching `SaveData::allied_mode`)
- `LobbyMessage` types: join, leave, ready, team_change, start_game, settings_change
- Serialization for lobby messages using same protocol framing from Phase 12

---

## Phase 20: Lobby Server Logic

**Changes:**
- New files: `include/openglad/gameplay/lobby_server.h`, `src/gameplay/lobby_server.cpp`
- Manages lobby state machine: join/leave, team assignment, ready-up, start transition
- First connected client = host (admin: pick scenario, force start)
- Broadcasts `LobbyState` on every change
- Populates equivalent of `SaveData` fields: `numplayers`, `allied_mode`, `team_list`, `current_campaign`, `scen_num`

---

## Phase 21: Lobby Client UI

Refactor picker to operate as a lobby client. The current picker flow is: `picker_main()` (`picker.cpp:366-377`) -> `picker_initialize_shared_menu_state()` -> `run_picker()` state machine -> team selection via `IPickerClient` -> sets `g_start_game_requested` flag -> state machine transitions to gameplay.

**Important architecture note:** The picker has two code paths:
- **Native:** `picker_main()` is a blocking call (traditional event loop)
- **Emscripten:** Non-blocking state machine driven by `picker_frame()` (returns bool for state transitions), with `picker_init()`, `picker_check_start_requested()`, `picker_cleanup_for_game()`, `picker_reinit_after_game()` lifecycle functions (all in `picker.cpp:1642-1703`). The Emscripten frame wrapper (`glad.cpp:158-170`) calls `picker_frame()` each browser frame and transitions to `GameState::Playing` when it returns true.

Both paths must be updated for lobby integration.

**Changes:**
- `src/interface/ui/picker.cpp` and related picker files (`picker_team_build.cpp`, `picker_common.cpp`)
- Picker reads from `LobbyState` for player list / team assignments instead of only local `SaveData`
- Team selection syncs to lobby server via messages (currently just writes `SaveData::numplayers` directly via `set_player_mode()` at `picker.cpp:1209`)
- Game start mechanism: currently sets `g_start_game_requested` flag (checked by `picker_check_start_requested()` at `picker.cpp:1642-1646`). Change this to send a lobby start message instead, with the lobby confirming before the flag is set.
- Single-player: lobby auto-populated, transparent to user — `SdlPickerClient` creates local lobby internally
- **Emscripten path:** `picker_frame()` polls for lobby state updates each frame. Lobby "game start" confirmation sets `g_start_game_requested`, triggering the `GameState::Picker -> GameState::Playing` transition via the existing state machine.
- **Native path:** lobby events are processed within the blocking `run_picker()` loop. Start request sends lobby message and waits for lobby confirmation before proceeding to `glad_main()`.

---

## Phase 22: Lobby -> Game Transition

Wire lobby completion to GameServer/GameClient creation.

**Changes:**
- `src/platform/sdl/glad_gameplay.cpp` — `glad_init()` accepts config from lobby (player assignments, team data, campaign/level selection)
- `src/platform/sdl/glad.cpp` — Emscripten state machine (`GameState::Picker` -> `GameState::Playing` transition at lines ~158-171) gets lobby-aware transition
- SaveData populated from lobby state before level load
- `screen::ready_for_battle(numviews)` called with numviews derived from lobby player count

---

## Phase 23: Vendor IXWebSocket

**Changes:**
- Add source to `third_party/ixwebsocket/`
- CMake: `add_subdirectory(third_party/ixwebsocket)` with `-DUSE_TLS=OFF -DUSE_ZLIB=OFF -DBUILD_DEMO=OFF`
- New CMake target: `og_ext_ixwebsocket` (or use IXWebSocket's native target name and alias)
- Exclude from Emscripten builds (Emscripten has built-in WebSocket API)
- Update `third_party/VENDORED_LIBS.md`

**Why IXWebSocket over libwebsockets:**
- **~13K LOC** (vs ~66K+ for lws core) — same scale as existing vendored libs (libzip, libyaml)
- **BSD-3-Clause** license (GPL v2 compatible)
- **Both server and client** in one library (`ix::WebSocketServer`, `ix::WebSocket`)
- **Zero dependencies** with TLS/zlib disabled (just pthreads)
- **Clean CMake** integration (~324-line CMakeLists vs lws's 172 `option()` directives)
- **Modern C++ API**: `sendBinary(data)` / `setOnMessageCallback()` — vs lws's verbose C callback switch statements
- **Threading model**: IXWebSocket creates **two threads per WebSocket connection** (one read, one heartbeat/ping) plus an acceptor thread on the server. For a 4-player game server: ~9-10 threads total (4 connections x 2 threads + 1 acceptor + main game loop). For a client: ~3 threads (1 connection x 2 + main). This is negligible. The game loop drains a thread-safe message queue each tick.

---

## Phase 24: WebSocket Server Transport

`WebSocketServerTransport : ITransport` using IXWebSocket.

**Changes:**
- New files in `src/platform/sdl/` (platform-specific, depends on IXWebSocket)
- Add to a new `OG_NETWORK_SOURCES` list in CMakeLists.txt (not `OG_PLATFORM_SOURCES` which only has 4 files: `native_input.cpp`, `sai2x.cpp`, `sound.cpp`, `video_sdl.cpp`). The new list is linked into the `og_platform_sdl` target.
- Uses `ix::WebSocketServer` — construct with port, set `onClientMessageCallback`, call `listenAndStart()`
- Incoming messages are pushed to a thread-safe queue (e.g., `std::mutex` + `std::deque<ReceivedMessage>`) by the I/O thread callback
- `poll()` drains the queue on the game loop thread — no locking during normal operation (try_lock + swap pattern)
- Same message framing as Phase 12 (protocol version + type + length + payload)

**Thread safety enforcement (critical — see Phase 1 invariant):**

IXWebSocket creates **two threads per connection** (read + heartbeat) plus an acceptor thread. These I/O threads must NEVER touch `GameWorld`, `id_index_`, entity lists, or any simulation state directly. All interaction flows through the thread-safe message queue:

- **Receive path (I/O → game thread):** I/O callback pushes `ReceivedMessage` to queue. Game thread drains via `poll()`. ✓ Already safe.
- **Send path (game thread → I/O):** `send(peer_id, data, len)` is called from the game thread. IXWebSocket's `sendBinary()` is thread-safe (internally queues to the write thread). ✓ Safe.
- **Connection events (I/O thread):** `onClientConnected` / `onClientDisconnected` callbacks fire on I/O threads. These must NOT trigger `capture_snapshot()`, `read_scenario()`, or any GameWorld access. Instead, push a connection/disconnection event to the message queue. The game loop processes it on the next `poll()` and takes any action (e.g., sending a keyframe to a new client) from the game thread.

**Explicit prohibitions (enforced by code review + documented in header):**
- No `GameWorld&` or `GameServer&` references captured in I/O callbacks
- No `capture_snapshot()`, `apply_snapshot()`, or entity access from I/O threads
- No `id_index_` access from I/O threads
- Connection/disconnection handling is always deferred to game thread via queue

**Verify:** Unit test — verify message ordering is preserved under concurrent send/receive. Stress test — 4 clients sending input at 12Hz while server broadcasts snapshots at 12Hz, verify no data races (run under ThreadSanitizer / `-fsanitize=thread`). **ThreadSanitizer CI build:** Add a `ci-tsan` CMake preset (similar to existing `ci-asan`) that builds with `-fsanitize=thread`. Run the NetworkTestFixture with WebSocket transports under TSan. This catches data races that code review misses.

---

## Phase 25: WebSocket Client Transport (Native)

`WebSocketClientTransport : ITransport` using IXWebSocket.

**Changes:**
- Same files as Phase 24 (client class alongside server class)
- Uses `ix::WebSocket` — `setUrl()`, `setOnMessageCallback()`, `start()`
- Same thread-safe queue pattern as server transport
- Handles reconnection via IXWebSocket's built-in auto-reconnect (configurable backoff)

---

## Phase 26: WebSocket Client Transport (Emscripten)

`EmscriptenWebSocketTransport : ITransport` using `<emscripten/websocket.h>`.

**Changes:**
- Create `src/platform/emscripten/` directory (does not exist yet)
- New files: `src/platform/emscripten/net_transport_emscripten_ws.h`, `src/platform/emscripten/net_transport_emscripten_ws.cpp`
- Uses `emscripten_websocket_new()`, `emscripten_websocket_send_binary()`, onmessage callback
- Emscripten build already has Asyncify support (`-sASYNCIFY` in `CMakeLists.txt:1527-1566`) for non-blocking socket ops
- Must conditionally include this source in the Emscripten build path. Currently the Emscripten build (`CMakeLists.txt:1528`) builds `og_game_web` from `GAME_SOURCES_NO_MAIN` — add emscripten transport sources to this list under an `if(EMSCRIPTEN)` guard.

---

## Phase 27: Headless Server Binary

New CMake target `openglad_server` — no SDL, no rendering. Follow the `openglad_text` target (`CMakeLists.txt:938`) as a proven SDL-free precedent.

**Changes:**
- New `src/server/server_main.cpp`
- Links: `og_gameplay`, `og_core`, `og_resources`, `og_ext_ixwebsocket` + ws transport
- Does NOT link: `og_interface`, `og_platform_sdl` (no SDL, no rendering, no UI)
- Note: `walker` already delegates rendering to an optional `render_` component (`std::unique_ptr<IRenderComponent>`), so headless walkers just have `render_ = nullptr`
- Runs LobbyServer -> GameServer loop
- Minimal GameContext setup without screen/SDL — needs `IRandom`, `SimEventLog`, but not `InputState` polling (server receives input over network)
- **Headless level loading:** The headless server needs `entity_factory`, `entity_configurator`, and `entity_derived_stats` callbacks to create entities during `read_scenario()` and `apply_snapshot()`. The `openglad_text` target already solves this with `walker_headless.cpp` and `platform_headless.cpp` (headless stubs for walker render components and platform hooks). The server binary reuses these same stubs — entities are created with `render_ = nullptr`, and `entity_configurator` provides sim-relevant data (frame counts) without loading sprite pixel data.
- Requires careful CMakeLists.txt changes: new executable target similar to `openglad`/`openscen`/`openglad_demo` but with different link set. Must verify no transitive SDL dependencies leak through gameplay modules.
- **CI guardrail:** Add a CI build step that compiles `openglad_server` without SDL2 available (e.g., `cmake -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON`) to guarantee no SDL leakage. This is the kind of thing that breaks silently when someone adds an innocent-looking `#include` to a gameplay header.

---

## Phase 28: Client "Join Game" UI

Add "Host Game" / "Join Game" to picker menu with support for both direct connections and relay.

**Changes:**
- Main menu buttons currently defined in `src/interface/ui/picker.cpp:427-467` (conditionally compiled for `__EMSCRIPTEN__` vs native — Emscripten replaces "quit" with "help") — add "Host Game" / "Join Game" buttons to both paths

**"Host Game" flow:**
- Creates `WebSocketServerTransport` listening on a configurable port (default 12345)
- Creates local `InProcessTransport` for the host's own client
- Displays the host's LAN IP + port for direct connections (e.g., "LAN: 192.168.1.5:12345")
- Optionally creates a relay room (Phase 31) and displays the room code alongside the LAN address

**"Join Game" flow — two connection modes:**
1. **Direct Connect:** Enter IP:port (e.g., `192.168.1.5:12345`). Connect via `WebSocketClientTransport` to `ws://<ip>:<port>`. Best for LAN play — zero relay latency, zero relay cost, works offline.
2. **Relay Room Code:** Enter room code (e.g., `GLAD-XKCD`). Connect via `RelayWebSocketTransport` (Phase 31) to `wss://relay.openglad.example/api/room/GLAD-XKCD`. Required for players behind NAT without port forwarding.

The UI shows both options (tab or toggle). The transport layer (`ITransport`) is identical once connected — GameServer/GameClient don't know or care whether the connection is direct or relayed.

- Lobby UI shows remote players alongside local team
- **Emscripten note:** Browser clients can only use WebSocket (no raw TCP). Direct connect works if the host is reachable (same network or port-forwarded). Relay works universally.

---

## Phase 29: Host Migration (Lobby Only)

Formalize first-client-is-host with migration on disconnect **in the lobby**.

**Changes:**
- `src/gameplay/lobby_server.cpp` — host assignment, migration on disconnect
- `lobby_state.h` — `host_player_id` field
- Picker UI conditionally shows host-only controls (scenario selection, force start)

**During gameplay:** If the host disconnects, all clients disconnect. There is no mid-game server migration. The dedicated headless server (Phase 27) is the recommended approach for sessions that need resilience.

---

## Phase 30: Network Robustness

Polish for real-world conditions.

**Changes:**
- Connection timeout / reconnection with exponential backoff (IXWebSocket has built-in auto-reconnect support)
- Graceful disconnect (dropped player -> AI control via existing NPC AI in `walker::act()`)
- Error handling for malformed messages (validate protocol version, bounds-check lengths)
- Heartbeat messages (already in `NetMessageType` enum from Phase 12)
- Note: basic visual interpolation is handled in Phase 18. Phase 30 may add more advanced interpolation (cubic, extrapolation) if needed.
- **Reconnection burst budget:** If multiple clients disconnect and reconnect simultaneously (e.g., network blip), the server sends N full keyframes in one tick. With 4 clients, that's ~32-64KB burst (4 × 8-16KB compressed keyframe). Still well within residential internet burst capacity, but the server should stagger keyframe sends across the next few ticks if >2 clients request keyframes simultaneously, to avoid a single-tick bandwidth spike.

**Reconnection protocol:**

When a player disconnects mid-game, the server:
1. Starts a `DISCONNECT_TIMEOUT_MS` (10 second) grace period. During this window, the player's last `held[]` input is repeated (movement continues) but `pressed[]` is cleared (no one-shot actions).
2. After the grace period expires without reconnection, the player's entity transitions to AI control (`act_type = ACT_RANDOM`). Other players see the entity start making autonomous decisions.
3. The server retains the player's **session token** (assigned during `Hello` handshake — see Phase 12), global player index, and controlled entity ID in a `DisconnectedPlayer` struct.

When a client connects and sends a `Hello` with a non-zero session token:
1. Server checks the token against `DisconnectedPlayer` entries.
2. **Match found:** Server accepts the reconnection. Sends `InitialSetup` with the player's original slot assignment + a full keyframe. Sends `ControlChange` to reassign the AI-controlled entity back to the player. The entity's `act_type` reverts to player-controlled. Other players see seamless transition from AI to human.
3. **No match:** Server rejects with "game in progress — no new players." The connection is closed.
4. If the entity died while AI-controlled, the reconnecting player resumes in dead state (same as if they'd been present for the death). They'll respawn on the next level.

**No mid-game joins for new players.** Only reconnection of previously-connected players is supported. This avoids the complexity of mid-game team assignment, viewscreen allocation, and entity ownership negotiation.

**`DisconnectedPlayer` cleanup:** Entries expire after `PAUSE_TIMEOUT_MS` (60 seconds) or when the game transitions to a new level. After expiry, the session token is no longer valid and the slot is permanently AI-controlled for the remainder of the current level.

---

## Phase 31: Relay Server + Matchmaking (Cloudflare Workers)

Players behind NAT can't host without port forwarding. A lightweight relay server sidesteps this entirely — both host and joiner connect outbound to the relay, which forwards traffic between them. Deployed on Cloudflare Workers for global edge presence, zero infrastructure management, and free/cheap tier.

### Architecture

```
Player A (host)                    Cloudflare Worker                    Player B (joiner)
─────────────────                  ─────────────────                    ──────────────────
WebSocket connect ──────────────→  Relay Worker       ←──────────────── WebSocket connect
  "create room ABCD"               ├─ Durable Object                    "join room ABCD"
                                    │  per game room
  game messages ──────────────────→ │ ────────────────────────────────→  game messages
  game messages ←────────────────── │ ←────────────────────────────────  game messages
```

**Key insight:** The relay does NOT understand game protocol contents. It is a dumb binary pipe between connected peers within a room. All game logic (snapshots, deltas, events, lobby messages) passes through opaquely. The relay only understands room management (create, join, leave, list).

### Cloudflare Workers Implementation

**Why Workers + Durable Objects:**
- **Durable Objects** provide per-room state with WebSocket support — each game room is a single Durable Object instance that holds open WebSocket connections to all players in that room
- **Edge deployment** — relay runs at the nearest Cloudflare PoP to each player, minimizing relay-added latency
- **Zero server management** — no VPS, no Docker, no uptime monitoring
- **Cost:** Free tier includes 100K requests/day + 100K Durable Object requests/day. Actual message count for a 4-player game at default speed (12 ticks/sec): per tick the host sends 3 snapshot messages (to each non-host peer, relayed) + 3 event batch messages + each of 3 non-host clients sends 1 input message (relayed to host) = ~9 relayed messages/tick × 12 ticks/sec = ~108 messages/sec = **~9.3M messages/day for one continuous 24-hour session**. The free tier supports roughly **15 minutes** of a 4-player game. The paid tier ($5/mo, 10M requests/day) supports approximately one continuous session, or several shorter play sessions per day. For multiple concurrent games, expect $5-15/mo depending on usage. This is still very cheap compared to running a VPS.
- **WebSocket support** is native in Workers Durable Objects — `state.acceptWebSocket()` and `webSocketMessage()` / `webSocketClose()` handlers

**Relay protocol (room management layer, separate from game protocol):**

```typescript
// Relay message types (JSON wrapper around binary game payloads)
type RelayMessage =
  | { type: "create_room"; campaign_hash: string }       // → { type: "room_created"; code: string }
  | { type: "join_room"; code: string }                  // → { type: "joined"; peer_id: number }
  | { type: "leave_room" }                               // → broadcast { type: "peer_left"; peer_id }
  | { type: "list_rooms" }                               // → { type: "room_list"; rooms: RoomInfo[] }
  | { type: "relay"; data: ArrayBuffer }                 // → forwarded to all other peers in room
  | { type: "relay_to"; peer_id: number; data: ArrayBuffer } // → forwarded to specific peer
```

The `relay` message is the hot path — it wraps the existing game protocol binary messages (snapshots, deltas, input, events) and forwards them to other peers in the room. The relay never inspects the `data` payload.

### Durable Object: GameRoom

```typescript
// src/game-room.ts (Cloudflare Worker Durable Object)

export class GameRoom implements DurableObject {
  private peers: Map<number, WebSocket> = new Map();
  private nextPeerId = 1;
  private roomCode: string;
  private campaignHash: string;
  private createdAt: number;
  private hostPeerId: number;

  async fetch(request: Request): Promise<Response> {
    // HTTP upgrade → WebSocket
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.state.acceptWebSocket(server);
    const peerId = this.nextPeerId++;
    server.serializeAttachment({ peerId });
    this.peers.set(peerId, server);
    if (this.peers.size === 1) this.hostPeerId = peerId;
    // Notify all peers
    this.broadcast({ type: "peer_joined", peer_id: peerId, is_host: peerId === this.hostPeerId });
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(ws: WebSocket, message: string | ArrayBuffer) {
    const { peerId } = ws.deserializeAttachment();
    if (message instanceof ArrayBuffer) {
      // Binary: game protocol relay (hot path)
      // Check first byte for relay_to vs broadcast
      this.relayBinary(peerId, message);
      return;
    }
    // JSON: room management
    const msg = JSON.parse(message);
    switch (msg.type) {
      case "leave_room":
        this.removePeer(peerId);
        break;
      case "list_peers":
        ws.send(JSON.stringify({
          type: "peer_list",
          peers: [...this.peers.keys()],
          host: this.hostPeerId
        }));
        break;
    }
  }

  async webSocketClose(ws: WebSocket) {
    const { peerId } = ws.deserializeAttachment();
    this.removePeer(peerId);
    // Host migration: promote next peer
    if (peerId === this.hostPeerId && this.peers.size > 0) {
      this.hostPeerId = this.peers.keys().next().value;
      this.broadcast({ type: "host_changed", new_host: this.hostPeerId });
    }
  }

  private relayBinary(fromPeer: number, data: ArrayBuffer) {
    // Forward to all other peers in the room
    for (const [id, ws] of this.peers) {
      if (id !== fromPeer) {
        ws.send(data);
      }
    }
  }

  private removePeer(peerId: number) {
    this.peers.delete(peerId);
    this.broadcast({ type: "peer_left", peer_id: peerId });
    // Auto-cleanup empty rooms after 30s
    if (this.peers.size === 0) {
      this.state.storage.deleteAlarm();
      this.state.storage.setAlarm(Date.now() + 30_000);
    }
  }

  private broadcast(msg: object) {
    const json = JSON.stringify(msg);
    for (const ws of this.peers.values()) ws.send(json);
  }

  async alarm() {
    // Cleanup: room has been empty for 30 seconds
    if (this.peers.size === 0) {
      // Durable Object will be evicted automatically
    }
  }
}
```

### Worker Router

```typescript
// src/index.ts (Cloudflare Worker entry point)

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/api/rooms") {
      // List active rooms (stored in KV or queried from DO)
      return handleListRooms(env);
    }

    if (url.pathname.startsWith("/api/room/")) {
      const code = url.pathname.split("/")[3];
      // Route to the Durable Object for this room
      const id = env.GAME_ROOM.idFromName(code);
      const room = env.GAME_ROOM.get(id);
      return room.fetch(request);
    }

    if (url.pathname === "/api/create") {
      // Generate a short room code, create Durable Object
      const code = generateRoomCode(); // e.g., "GLAD-XKCD"
      const id = env.GAME_ROOM.idFromName(code);
      const room = env.GAME_ROOM.get(id);
      // Store room metadata in KV for listing
      await env.ROOM_INDEX.put(code, JSON.stringify({
        code,
        created: Date.now(),
        campaign_hash: url.searchParams.get("campaign") || "",
      }), { expirationTtl: 3600 }); // Auto-expire after 1 hour
      return room.fetch(request);
    }

    return new Response("OpenGlad Relay", { status: 200 });
  }
};
```

### Room Codes

- Format: `GLAD-XXXX` where XXXX is 4 alphanumeric characters (case-insensitive), giving ~1.7M unique codes
- Codes are ephemeral — stored in Workers KV with a 1-hour TTL
- Displayed in the Host Game UI; joiners type the code (no IP addresses, no port numbers)
- If a code collision occurs (unlikely), the create endpoint retries with a new code

### Room Listing / Browser

- `GET /api/rooms` returns a JSON array of active rooms with: code, player count, campaign name, host name, created timestamp
- The "Join Game" UI (Phase 28) shows a room browser alongside the manual code entry
- Rooms with `campaign_hash` filtering: client can filter to rooms matching its local campaign hash, avoiding "campaign version mismatch" errors at connect time
- Rooms auto-expire from the KV index after 1 hour. The Durable Object `alarm()` cleans up empty rooms after 30 seconds of no connections.

### Client-Side Transport: `RelayWebSocketTransport`

A new `ITransport` implementation that wraps the relay WebSocket connection.

**Changes (native):**
- New files in `src/platform/sdl/`: `net_transport_relay_ws.h`, `net_transport_relay_ws.cpp`
- Uses IXWebSocket (same as Phase 24-25) to connect to `wss://relay.openglad.example/api/room/GLAD-XXXX`
- Binary messages are unwrapped from the relay envelope and delivered to the game protocol layer as if they came from a direct WebSocket connection
- The `peer_id` from the relay maps to the `peer_id` in `ITransport` — the game server/client code doesn't know it's going through a relay

**Changes (Emscripten):**
- Extends the `EmscriptenWebSocketTransport` from Phase 26 with the same relay URL scheme
- The browser's native WebSocket connects to the Cloudflare Worker URL
- Same binary relay protocol — the browser client is indistinguishable from a native client to the relay

**Host flow:**
1. Player clicks "Host Game" in picker
2. Client sends `POST /api/create?campaign=HASH` to relay
3. Relay creates room, returns room code + WebSocket upgrade
4. Client displays room code: "GLAD-XKCD — share this code with friends"
5. Client creates `GameServer` locally (same as Phase 14)
6. Client wraps the relay WebSocket in `RelayWebSocketTransport`
7. Other players connect by entering the room code

**Join flow:**
1. Player clicks "Join Game", enters room code "GLAD-XKCD"
2. Client connects to `wss://relay.openglad.example/api/room/GLAD-XKCD`
3. Relay assigns peer_id, notifies all peers
4. `Hello` handshake proceeds over the relay (protocol version + campaign hash check)
5. Lobby messages flow through relay transparently
6. Game starts — snapshots/deltas/input flow through relay

### Latency Impact

The relay adds one extra network hop per message. With Cloudflare's edge network:
- **Same-city players:** +1-3ms (relay at local PoP)
- **Same-country players:** +5-15ms (relay at nearest PoP)
- **Cross-continent players:** +10-30ms (relay at nearest PoP, still shorter than direct route in many cases due to Cloudflare's backbone)

At 12 ticks/sec (83ms/tick), even +30ms relay overhead keeps total input lag at ~180-280ms — tolerable for the game's pacing.

### Deployment

```bash
# Deploy relay worker
cd relay/
npx wrangler deploy

# wrangler.toml
name = "openglad-relay"
main = "src/index.ts"
compatibility_date = "2024-09-23"

[[durable_objects.bindings]]
name = "GAME_ROOM"
class_name = "GameRoom"

[[kv_namespaces]]
binding = "ROOM_INDEX"
id = "..."
```

**Repository structure:**
```
openglad/
├── relay/                    Cloudflare Workers relay server
│   ├── src/
│   │   ├── index.ts          Worker router
│   │   └── game-room.ts      Durable Object (per-room state)
│   ├── wrangler.toml         Deployment config
│   ├── package.json
│   └── tsconfig.json
```

The relay is a separate deployable, not part of the C++ build. It has its own `package.json` and deploys independently via `wrangler deploy`.

### Security Considerations

- **No authentication** in v1 — anyone with the room code can join. Room codes are short-lived (1 hour TTL) and random enough to prevent guessing.
- **Rate limiting:** Cloudflare Workers has built-in rate limiting. Apply per-IP limits on room creation (e.g., 10 rooms/hour) and WebSocket message rate (e.g., 100 messages/sec per connection — well above the ~24 messages/sec game peak).
- **Message size limit:** Reject relay messages >64KB (largest legitimate message is a full keyframe at ~16KB compressed). Prevents abuse.
- **Room capacity:** Limit to 4 connections per room (matching the game's 4-player max). Reject additional joins.
- **Future:** Add optional room passwords. Add player accounts with a simple token-based auth (e.g., JWT from a separate auth endpoint).

**Graceful degradation under relay rate limits / failures:**

If the Cloudflare relay drops messages mid-game (rate limit exceeded, transient Worker error, edge PoP failover):
- **Missed deltas:** Client detects gap (missing `server_tick` sequence) and sends `KeyframeRequest` (Phase 12). Server responds with full keyframe on next tick. Game hiccups for one keyframe interval (~5 seconds) but self-corrects.
- **Missed input:** Server's input jitter policy (Phase 14) repeats `held[]` state and buffers late `pressed[]` events. The game continues without the missing player's one-shot actions for the dropped ticks.
- **WebSocket disconnection:** IXWebSocket's built-in auto-reconnect (Phase 25) reconnects with exponential backoff. On reconnect, client sends `ClientReady`, receives a fresh keyframe, and resumes.
- **Sustained relay failure (>10 seconds):** `DISCONNECT_TIMEOUT_MS` fires, player transitions to AI control (Phase 30). If the relay recovers, the player can rejoin via the same room code.

This recovery path requires no special relay-aware code — the existing `KeyframeRequest`, input jitter, auto-reconnect, and disconnect timeout mechanisms handle it. The relay is a dumb pipe and the game protocol is already designed for unreliable delivery of cosmetic events and reliable (TCP-ordered) delivery of game-flow events.

**Relay broadcast optimization for keyframes:** Per-client deltas differ (each client's `accumulated_dirty` diverges), so they must be sent individually through the relay. But **full keyframes are identical for all clients** — the server currently sends N copies of the same keyframe (one per client). When using a relay, use the relay's broadcast (`relay` message type) to send the keyframe once; the Durable Object fans it out to all peers. This cuts keyframe relay cost by ~3× (1 message instead of 3 for a 4-player game). Per-tick deltas remain per-client. Implementation: add an `ITransport::broadcast(data, len)` method alongside `send(peer_id, data, len)`. `WebSocketServerTransport` implements broadcast as N individual sends (direct connections are already point-to-point). `RelayWebSocketTransport` implements broadcast as a single relay message.

**Relay cost awareness UI:** If the host increases game speed (`timer_wait < 4`), relay message rate scales linearly. At `timer_wait = 1` (~73 ticks/sec), relay traffic is ~6× the default — potentially exceeding the paid tier budget for sustained play. Add a warning in the "Host Game" UI when speed is increased while using a relay connection: "High game speed increases relay usage."

**Verify:** Deploy to Cloudflare Workers. Create room, connect two clients (one native, one browser). Relay binary messages between them. Measure added latency. Verify room listing, room expiry, and host migration on disconnect. Verify the game plays correctly end-to-end through the relay — lobby, game start, level transitions, exit prompts. **Simulate relay message drops** (inject packet loss in test) and verify the game self-corrects via keyframe request within 5 seconds.

---

## Key Milestones

- **Phase 0** — deterministic simulation: all gameplay `rand()` calls migrated to `SimRandom`, `pow()` replaced with bit shift, `walkrounds` dead code deleted.
- **Phase 3** — all cross-reference pointers are private with enforced setter usage. `sync_ids_from_pointers()` removed. (Dirty tracking instrumentation deferred to Phase 8.)
- **Phase 10** — input replay system: record a game, play it back deterministically. Invaluable for debugging later phases.
- **Phase 15** — `screen::act()` split into sub-methods. Pure refactor, all tests pass unchanged.
- **Phase 16** — game loop wired through GameServer/GameClient with InProcessTransport (zero-copy). Game plays identically to before.
- **Phase 17** — all ~145 integration tests migrated to server/client path. `screen::act()` wrapper deleted. No fallback.
- **Phase 18** — smooth visual interpolation at render framerate, eliminating 12fps teleporting.
- **Phase 22** — lobby system works, local multiplayer flows through lobby -> game -> lobby.
- **Phase 28** — two separate processes (or a browser + native) playing the same game over WebSockets.
- **Phase 31** — players connect via room codes through Cloudflare Workers relay. No port forwarding, no IP addresses. Browser and native clients play together through the relay transparently.

---

## Execution Order

**Each phase is completed and fully verified before the next begins. There is no parallelism — phases are executed strictly one at a time, in order.** Phase 31 (relay) is the sole exception: its TypeScript relay server code can be developed in parallel with Phases 23-30 since it's a standalone project with no C++ build dependencies — only the client-side `RelayWebSocketTransport` integration requires Phases 25/26 to be complete.

All phases are executed sequentially, one at a time, in order: Phase 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31.

---

## Verification Strategy

After each phase:
```bash
cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test
```

---

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
- `openglad_text` SDL-free precedent at CMakeLists.txt:938 (Phase 27)
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
- CMake: `OG_PLATFORM_SOURCES` has exactly 4 files, `OG_SIM_SOURCES` has 2 files, `og_ext_zlib` target exists at line 746, vendored lib pattern at lines 742-776, Emscripten `-sASYNCIFY` at lines 1527-1566 (Phases 11, 23, 26)
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
- Test infrastructure: 145 integration tests (TEST_SOURCES), 28 unit tests (OG_UNIT_TEST_SOURCES), 13 data tests, 71+ runtime tests
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
   - 17: Migrate tests to server/client path, delete wrapper. No fallback.

3. **Integration tests migrate to server/client path (Phase 17).** All ~145 integration tests that call `game_frame()` are updated to use `NetworkTestFixture`. Tests exercise the real networking code path as a side effect.

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
- **GameLoopFrameState at game_loop_state.h:14-17:** `#ifdef __EMSCRIPTEN__` guards on `last_frame_time` and `accumulated_time` confirmed.
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
