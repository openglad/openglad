# Phase 31: Network Robustness

> **See also:** [Phase 15 (GameServer/GameClient)](phase-15-server-client.md) | [Phase 13 (Hello handshake)](phase-13-transport-interface.md) | [Context](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Polish for real-world conditions.

**Changes:**
- Connection timeout / reconnection with exponential backoff (IXWebSocket has built-in auto-reconnect support)
- Graceful disconnect (dropped player -> AI control via existing NPC AI in `walker::act()`)
- Error handling for malformed messages (validate protocol version, bounds-check lengths)
- Heartbeat messages (already in `NetMessageType` enum from Phase 13)
- Note: basic visual interpolation is handled in Phase 19. Phase 31 may add more advanced interpolation (cubic, extrapolation) if needed.
- **Reconnection burst budget:** If multiple clients disconnect and reconnect simultaneously (e.g., network blip), the server sends N full keyframes in one tick. With 4 clients, that's ~32-64KB burst (4 x 8-16KB compressed keyframe). Still well within residential internet burst capacity, but the server should stagger keyframe sends across the next few ticks if >2 clients request keyframes simultaneously, to avoid a single-tick bandwidth spike.

## Reconnection Protocol

When a player disconnects mid-game, the server:
1. Starts a `DISCONNECT_TIMEOUT_MS` (10 second) grace period. During this window, the player's last `held[]` input is repeated (movement continues) but `pressed[]` is cleared (no one-shot actions).
2. After the grace period expires without reconnection, the player's entity transitions to AI control (`act_type = ACT_RANDOM`). Other players see the entity start making autonomous decisions.
3. The server retains the player's **session token** (assigned during `Hello` handshake — see Phase 13), global player index, and controlled entity ID in a `DisconnectedPlayer` struct.

When a client connects and sends a `Hello` with a non-zero session token:
1. Server checks the token against `DisconnectedPlayer` entries.
2. **Match found:** Server accepts the reconnection. Sends `InitialSetup` with the player's original slot assignment + a full keyframe. Sends `ControlChange` to reassign the AI-controlled entity back to the player. The entity's `act_type` reverts to player-controlled. Other players see seamless transition from AI to human.
3. **No match:** Server rejects with "game in progress — no new players." The connection is closed.
4. If the entity died while AI-controlled, the reconnecting player resumes in dead state (same as if they'd been present for the death). They'll respawn on the next level.

**No mid-game joins for new players.** Only reconnection of previously-connected players is supported. This avoids the complexity of mid-game team assignment, viewscreen allocation, and entity ownership negotiation.

**`DisconnectedPlayer` cleanup:** Entries expire after `PAUSE_TIMEOUT_MS` (60 seconds) or when the game transitions to a new level. After expiry, the session token is no longer valid and the slot is permanently AI-controlled for the remainder of the current level.
