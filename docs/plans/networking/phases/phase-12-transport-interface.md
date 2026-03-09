# Phase 12: ITransport Interface + Protocol Framing

> **See also:** [Context & Key Decisions](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

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

## Connection Handshake (`Hello` message)

On connect, client and server exchange `Hello` messages containing:
- Protocol version + snapshot format version (mismatch -> disconnect with descriptive error)
- **Min supported protocol version** (1 byte) — reserved for future version-range negotiation. For v1, `min_version == current_version`. In the future, if protocol v3 is backward-compatible with v2, the server can advertise `min=2, current=3` and clients supporting v2-v3 can negotiate. This byte is free to include now and avoids a breaking handshake change later.
- **Session token** (16 bytes, random) — assigned by the server during the initial `Hello` response. Stored by the client for reconnection purposes. On reconnect, the client includes its session token in the `Hello` message; the server matches it against disconnected player slots (see [Phase 30](phase-30-network-robustness.md) reconnection protocol). First-time connections send a zero token.
- **Campaign content hash** — a CRC32 or similar hash of the campaign's level file listing + file sizes. This catches campaign file mismatches at connection time rather than as mysterious missing entities during `read_scenario()`. If the hash doesn't match, disconnect with "campaign version mismatch — ensure all players have the same campaign files."

The `Hello` exchange happens before any other messages.

## `ClientReady` Message

Sent client->server after the client has processed `InitialSetup` and the first full keyframe (or after loading a new level during level transitions). The server MUST NOT send deltas to a client until it receives `ClientReady` — otherwise the server sends deltas against a baseline the client hasn't received yet. During the wait, the server queues or drops deltas for that client (full keyframe will be sent on `ClientReady`).

## `KeyframeRequest` Message

Sent client->server when the client detects a gap in the `server_tick` sequence (missed a delta and its local state may be stale). The server responds with a full keyframe on the next tick and resets that client's `PerClientState`. This is faster than waiting for the next periodic keyframe (~5 seconds).

## `ExitPromptBroadcast` / `ExitPromptResponse` Messages

- **`ExitPromptBroadcast`** (server->client): Sent when the server enters `pending_exit_prompt` state (see [Phase 14](phase-14-server-client.md)). Contains the prompt text, destination level, and whether it's a withdraw prompt. All clients display the prompt.
- **`ExitPromptResponse`** (client->server): Sent by any client in response to `ExitPromptBroadcast`. Contains accept/decline. First response from any player resolves the prompt.

**Verify:** Compiles. Mockable in tests.
