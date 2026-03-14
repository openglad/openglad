# Context & Key Decisions

OpenGlad is a top-down action RPG with 4-player split-screen. The goal is to add networked multiplayer in incremental phases, starting with an in-process server/client architecture and culminating in remote play over WebSockets.

The codebase is well-positioned: `InputState` is SDL-independent (4 players x 16 bools x 2 arrays), `SimEventLog` decouples simulation from platform side effects, and `GameSession` already supports concurrent sessions (de-singletonized in PR #105). `GameWorld` has a deterministic `SimRandom` LCG RNG, but ~17 call sites in gameplay code still use C stdlib `rand()` — Phase 0 fixes this before anything else.

## Key Decisions

- **Server-authoritative** model (server runs sim, broadcasts state)
- **No client-side prediction** initially — clients display the latest server snapshot as-is. At 12 ticks/sec (83ms/tick) + network RTT, remote players will experience ~150-250ms input lag. This is acceptable for LAN and tolerable for internet play in a top-down action game, but noticeable for twitch gameplay. Client-side prediction (speculatively apply own input, rollback on server correction) is a natural follow-up optimization after the base architecture is proven — the snapshot capture/apply infrastructure built here directly supports rollback.
- **Full state snapshots** with **delta compression** (only changed fields per entity) + zlib on top
- **Decouple sim tick from render frame** rate
- **WebSockets** for transport (works in browsers too)
- **IXWebSocket** (vendored, no TLS) for native; Emscripten built-in API for web
- **First-client-is-host** for headless servers
- **Protocol version byte** in every message header from day one
- **Manual binary serialization** driven by the `constexpr` field descriptor table (Phase 5). No external serialization library — the field table makes generic ser/deser trivial (~200 lines), avoids vendoring ~15K LOC of msgpack headers, and keeps endianness handling explicit and debuggable.
- **Campaign file parity** — all clients must have the same campaign data files locally. The server does not transmit level data; clients call `read_scenario()` from local files. A campaign content hash is exchanged during the `Hello` handshake (Phase 13) to detect mismatches early. Server-sent level data is a future extension if needed (e.g., for web clients connecting to custom campaigns).

## Sim Tick Rate & Game Speed

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

## Bandwidth Budget

At default speed (`timer_wait = 6`, ~12 ticks/sec) and ~40KB per full snapshot (~200 entities x ~200 bytes packed), that's ~480KB/sec per client uncompressed. Delta compression (only sending changed entity fields via per-field dirty bitmask) cuts this dramatically — between ticks, most entities don't change position or state. Typical delta: ~2-5KB. zlib on top of deltas (which still have structural similarity) yields ~1-3KB per tick. At 12 ticks/sec with 4 clients: ~50-150KB/sec outbound — very comfortable for residential internet. At max networked speed (`timer_wait = 1`, ~73 ticks/sec), bandwidth scales linearly to ~300-900KB/sec outbound — still within residential internet capacity but approaching the comfort zone.

Full keyframe snapshots are sent periodically (every `KEYFRAME_INTERVAL_TICKS` / ~5 seconds) or on client join for baseline sync. zlib is already vendored (at `third_party/physfs/zlib123/`, CMake target `og_ext_zlib`).

**SimEventBatch overhead:** The above budget only covers snapshot/delta data. `SimEventBatch` messages (sounds, notifications, palette changes) add ~10-30KB/sec during active combat (~61 cosmetic event call sites across gameplay code — 23 PlaySound + 35 Notification + 2 SetPalette + 1 RequestRedraw — firing at varying rates x ~20-50 bytes each x 12 ticks/sec). Total outbound with events: ~60-180KB/sec — still comfortable for residential internet.

## Module Placement

- Snapshot/entity ID code: `og_gameplay` (`include/openglad/gameplay/`, `src/gameplay/`)
- Transport interface + in-process transport: `og_gameplay` (no platform deps)
- WebSocket transports: new `OG_NETWORK_SOURCES` CMake list in `src/platform/sdl/` — platform-specific, depends on IXWebSocket. (Note: `OG_PLATFORM_SOURCES` is only 4 files; networking deserves its own list.)
- Emscripten transport: new `src/platform/emscripten/` directory (does not exist yet, must be created)
- Headless server binary: new `src/server/` directory, new CMake target (can follow the `openglad_text` headless target at `CMakeLists.txt:938` as a proven SDL-free precedent)
- All new source files must be added to the appropriate `OG_*_SOURCES` list in `CMakeLists.txt`

**CMake naming note:** The individual source lists (`OG_SIM_SOURCES`, `OG_ENTITIES_SOURCES`, etc.) are aggregated into `OG_GAMEPLAY_COMPONENT_SOURCES` (CMakeLists.txt:377) which builds the `og_gameplay` target. When adding new files, add them to the appropriate sub-list (e.g., `OG_SIM_SOURCES`), which will automatically flow into `og_gameplay` via the aggregation.

## Known UX Limitations

**AI behavioral jitter:** Clients don't run `world.tick()` — they apply server snapshots. The `statistics::commands` list (AI command queue) is deliberately not serialized (see Phase 5). This means AI entities may appear to twitch or change direction abruptly when a new snapshot overwrites their state mid-walk-command. This is inherent to the server-authoritative model without client-side prediction. Phase 19 interpolation smooths positional movement but not behavioral discontinuities (e.g., sudden direction changes). This is acceptable for a top-down action game at 12 ticks/sec, but should be monitored during playtesting.

**Host disconnect during gameplay:** If the host (who is also the game server) disconnects mid-game, all clients disconnect. There is no mid-game host migration or server promotion. The dedicated headless server (Phase 28) is the recommended path for sessions that need resilience. Host migration in the lobby (Phase 30) only applies pre-game.

**SetPalette desync risk:** `SetPalette` is classified as Tier 1 (cosmetic, best-effort delivery). The mage's freeze spell changes the entire screen palette via `SetPalette`. If a client misses this event, the palette stays wrong until the next keyframe corrects it (see `current_palette_id` in Phase 5 WorldSnapshot), which happens every ~5 seconds. This is acceptable given the rarity of the event and the self-correcting nature of keyframe sync.

**Exit prompt freezes all players:** When any player walks onto a level exit, the server pauses simulation and broadcasts the exit prompt to all clients. Any player can accept or decline. The game freezes for everyone during this prompt (see Phase 15). This matches the original single-screen behavior where the exit dialog froze the game. Timeout after `EXIT_PROMPT_TIMEOUT_MS` (15 seconds) auto-declines.

**Networked pause:** Any player can pause in networked mode, using the same freeze-broadcast pattern as exit prompts. Server stops ticking, broadcasts pause state, all clients freeze. Timeout after `PAUSE_TIMEOUT_MS` (60 seconds) auto-unpauses to prevent a disconnected player from freezing the game forever. See Phase 15 for protocol details.

**Grid size variability:** Grid dimensions (`PixieData.w * PixieData.h`) vary per level. The bandwidth budget estimates ~2400 bytes for a 60×40 map, but larger levels (up to ~228×228 = ~52KB) are possible. Full grid resends in keyframes remain cheap even at worst case — ~52KB every 5 seconds is ~10KB/sec, well within budget. Per-tick grid deltas are always tiny (a few changed tiles from explosions).

**Game speed in networked play:** Only the host can adjust game speed (`timer_wait`). Non-host clients' speed-change keybinds are suppressed. The server includes `timer_wait` in every WorldSnapshot. When the host changes speed, clients see the new `timer_wait` in the next snapshot and adjust their interpolation timing accordingly. `timer_wait = 0` (uncapped speed) is clamped to `MIN_TIMER_WAIT = 1` in networked mode to prevent bandwidth explosion (~73 ticks/sec at `timer_wait = 1` is already aggressive). The bandwidth budget scales linearly with tick rate — at `timer_wait = 1` (73 ticks/sec), delta bandwidth is ~6x the default estimate (~300-900KB/sec outbound with 4 clients). This is still within residential internet capacity but should be noted in the host UI if speed is cranked up.

**No mid-game joins:** New players cannot join a game already in progress. All players must be present in the lobby before game start. This simplifies the protocol significantly — no need for mid-game team assignment, viewscreen allocation, or entity ownership negotiation. Disconnected players can reconnect (see below); new players cannot.

**Player reconnection:** When a player disconnects mid-game, their entity transitions to AI control (Phase 31). The server retains the player's session token (assigned during the `Hello` handshake) and slot assignment. If the same player reconnects before the game ends, the server re-identifies them via the session token, sends a fresh `InitialSetup` + keyframe, and issues a `ControlChange` message to hand the AI-controlled entity back to the player. Other players see the entity seamlessly transition from AI back to human control. If the session token doesn't match any disconnected slot, the connection is rejected with "game in progress — no new players."
