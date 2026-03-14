# Phase 15: GameServer and GameClient

> **See also:** [Context & Key Decisions](docs/plans/networking/common/context.md) | [Phase 13 (ITransport)](phase-13-transport-interface.md) | [Phase 14 (InProcessTransport)](phase-14-inprocess-transport.md) | [Phase 5 (WorldSnapshot)](phase-05-world-snapshot.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

The core server-authoritative objects. This is the convergence point where everything comes together.

## GameServer

- Owns `GameWorld` and a `GameSession` (for RNG, context)
- On startup: sends `InitialSetup` message to each client with `guy` character data for all players + level metadata (constant GameWorld fields: `id`, `title`, `difficulty`, `pixmaxx`, `pixmaxy`, etc.) + `completed_levels` set
- Each tick: collect `InputState` from clients via transport -> feed to viewscreen input processing -> `world_.tick()` -> `capture_snapshot()` (copies dirty bits, clears them) -> accumulate dirty masks per client -> `serialize_delta()` using per-client accumulated masks -> broadcast to all clients. Send full keyframe every `KEYFRAME_INTERVAL_TICKS`.
- Sends `SimEventBatch` message alongside each delta/keyframe (separate message, same tick)
- Maintains per-client baseline snapshot for delta computation
- **Tracks `current_palette_id`:** When a `SetPalette` event is emitted by simulation code, the server updates `current_palette_id` in the WorldSnapshot. Clients that miss the event will converge to the correct palette on the next keyframe.

**Important: `screen::act()` actual order.** The current `screen::act()` calls `world_.tick()` **first** (line 905), **then** dispatches events from SimEventLog (lines 920-978). The server refactoring preserves this order: tick the world, capture the snapshot, drain events, then broadcast.

## Input Jitter Policy

The server does NOT wait for all client inputs before ticking. If a client's input hasn't arrived by tick time:
- **`held[]` array (movement, long-held keys):** Repeat the client's last known `held[]` state. Movement direction, held fire button, etc. persist naturally between ticks, so repeating is correct behavior in most cases.
- **`pressed[]` array (one-shot actions):** Do NOT repeat. Clear all `pressed[]` bits for the missing tick. If the late input arrives before the NEXT tick, deliver the `pressed[]` events on that next tick instead (late delivery). This prevents missed button presses (special attacks, yells, weapon switches) at the cost of 1-tick additional latency for those actions during jitter.
- **Late delivery cap:** `MAX_LATE_PRESS_TICKS = 2`. If a `pressed[]` input arrives more than 2 ticks late, discard it rather than delivering. At default speed (12 ticks/sec), 2 ticks = ~166ms — still a reasonable window for a button press. Delivering a 6-tick-late special attack (after a 500ms network spike) would cause gameplay weirdness (attacking thin air because the target moved away).
- The server tracks a `last_received_input_tick` per client. If no input arrives within `DISCONNECT_TIMEOUT_MS` wall-clock time, the client is considered disconnected and transitions to AI control ([Phase 31](phase-31-network-robustness.md)).
- **Speed-change input filtering:** Only the host client's speed-change inputs (`timer_wait` adjustments) are accepted. Non-host clients' speed-change inputs are silently dropped.

## Exit Prompt — Freeze-and-Ask Protocol

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

## Networked Pause — Freeze-and-Broadcast Protocol

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

## EndGame Early Return

`screen::act()` currently returns immediately from `EndGame` events (`screen.cpp:951-955`), clearing remaining events. In the server/client split, the server captures ALL events for a tick before sending them — it doesn't short-circuit. The client processes `EndGame` last (or the `GameFlowEventBatch` is ordered with EndGame at the end) to ensure no events are lost.

## Divergence Detection (Development Diagnostic)

Add a `snapshot_hash` field (CRC32) to each `WorldSnapshot`. The server computes it from the full snapshot data. Clients compute their own hash after `apply_snapshot()` and periodically send a `SnapshotHashCheck` message (e.g., every `KEYFRAME_INTERVAL_TICKS`) containing their local hash + tick number. The server compares; on mismatch, it force-sends a keyframe and logs a diagnostic with the tick number and entity counts. This is invaluable during development for catching desync bugs (e.g., missed fields, stale pointers, RNG drift). The hash check can be compiled out or disabled in release builds — it's ~4 bytes per keyframe interval, negligible overhead.

## GameClient

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

## Level Transitions

When the server completes a level (detected via `next_level` flag in the snapshot), it sends a new `InitialSetup` message with the next level's metadata + guy data + updated `completed_levels` (reusing the existing `InitialSetup` message type — no new message needed). The client receives this, runs `read_scenario()` to load the new level from local campaign files, sends `ClientReady`, and the server resumes sending snapshots for the new level. Between the old level's last snapshot and the new level's first snapshot, the client shows a loading/transition screen.

**Level transition event cleanup:** After `read_scenario()` loads the new level on the server, call `SimEventLog::clear()` before the first tick. Entity creation during level loading may push events (sounds, notifications) that reference the new level's entities — but no client has loaded the new level yet, so these events are meaningless and would cause "unknown entity" warnings on clients. Clear the log so the first tick starts clean. Also clear per-client `PerClientState::accumulated_dirty` (Phase 9) — the new level's entities have no relationship to the old level's dirty state.

**Level transition event gap:** Between a level's completion and the client sending `ClientReady` for the new level, the server continues ticking the new level. The server does NOT send deltas/events to a client until it receives `ClientReady` (see Phase 13). This means the client misses the first few ticks of the new level (~3-6 ticks during loading). Tier 1 cosmetic events during this gap are lost (a few startup sounds) — acceptable. Tier 2 game-flow events should not fire in the first few ticks of a fresh level. The first message after `ClientReady` is always a full keyframe, so the client catches up to current state immediately.

## Player-to-Viewscreen Mapping and Camera Tracking

In local play, viewscreens map 1:1 to local players. With networking, a remote client controls (say) player index 2 but renders only viewscreen 0 locally. The client needs a `local_player_indices[]` mapping that says "my local viewscreen 0 follows the entity controlled by global player index N." This mapping is established during lobby/game start.

The server sends a per-client array of **controlled `entity_id`s** in the `InitialSetup` message (alongside `guy` data and level metadata). Each entry maps a global player index to the `entity_id` that player's viewscreen should follow. The client stores this mapping and after each `apply_snapshot()`, sets `viewob[local_view]->control` to the entity with the corresponding `entity_id` (looked up via `id_index_`).

When a controlled entity dies or the player switches characters (via `sim_find_next_control()`), the server sends a **`ControlChange` message** (new message type — add to `NetMessageType` enum in Phase 13) containing the player index and new controlled `entity_id`. The client updates its mapping. This is a small, infrequent message (~8 bytes: player index + entity_id).

## GameServer Callback Architecture

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
