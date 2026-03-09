# Phase 31: Relay Server + Matchmaking (Cloudflare Workers)

> **See also:** [Phase 28 (Join Game UI)](phase-28-join-game-ui.md) | [Phase 24 (WebSocket Server)](phase-24-websocket-server.md) | [Context (Bandwidth Budget)](docs/plans/networking/common/context.md) | [Execution Order](docs/plans/networking/common/execution-order.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Players behind NAT can't host without port forwarding. A lightweight relay server sidesteps this entirely — both host and joiner connect outbound to the relay, which forwards traffic between them. Deployed on Cloudflare Workers for global edge presence, zero infrastructure management, and free/cheap tier.

## Architecture

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

## Cloudflare Workers Implementation

**Why Workers + Durable Objects:**
- **Durable Objects** provide per-room state with WebSocket support — each game room is a single Durable Object instance that holds open WebSocket connections to all players in that room
- **Edge deployment** — relay runs at the nearest Cloudflare PoP to each player, minimizing relay-added latency
- **Zero server management** — no VPS, no Docker, no uptime monitoring
- **Cost:** Free tier includes 100K requests/day + 100K Durable Object requests/day. Actual message count for a 4-player game at default speed (12 ticks/sec): per tick the host sends 3 snapshot messages (to each non-host peer, relayed) + 3 event batch messages + each of 3 non-host clients sends 1 input message (relayed to host) = ~9 relayed messages/tick x 12 ticks/sec = ~108 messages/sec = **~9.3M messages/day for one continuous 24-hour session**. The free tier supports roughly **15 minutes** of a 4-player game. The paid tier ($5/mo, 10M requests/day) supports approximately one continuous session, or several shorter play sessions per day. For multiple concurrent games, expect $5-15/mo depending on usage. This is still very cheap compared to running a VPS.
- **WebSocket support** is native in Workers Durable Objects — `state.acceptWebSocket()` and `webSocketMessage()` / `webSocketClose()` handlers

## Relay Protocol (Room Management Layer)

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

## Durable Object: GameRoom

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

## Worker Router

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

## Room Codes

- Format: `GLAD-XXXX` where XXXX is 4 alphanumeric characters (case-insensitive), giving ~1.7M unique codes
- Codes are ephemeral — stored in Workers KV with a 1-hour TTL
- Displayed in the Host Game UI; joiners type the code (no IP addresses, no port numbers)
- If a code collision occurs (unlikely), the create endpoint retries with a new code

## Room Listing / Browser

- `GET /api/rooms` returns a JSON array of active rooms with: code, player count, campaign name, host name, created timestamp
- The "Join Game" UI (Phase 28) shows a room browser alongside the manual code entry
- Rooms with `campaign_hash` filtering: client can filter to rooms matching its local campaign hash, avoiding "campaign version mismatch" errors at connect time
- Rooms auto-expire from the KV index after 1 hour. The Durable Object `alarm()` cleans up empty rooms after 30 seconds of no connections.

## Client-Side Transport: `RelayWebSocketTransport`

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

## Host and Join Flows

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

## Latency Impact

The relay adds one extra network hop per message. With Cloudflare's edge network:
- **Same-city players:** +1-3ms (relay at local PoP)
- **Same-country players:** +5-15ms (relay at nearest PoP)
- **Cross-continent players:** +10-30ms (relay at nearest PoP, still shorter than direct route in many cases due to Cloudflare's backbone)

At 12 ticks/sec (83ms/tick), even +30ms relay overhead keeps total input lag at ~180-280ms — tolerable for the game's pacing.

## Deployment

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

## Security Considerations

- **No authentication** in v1 — anyone with the room code can join. Room codes are short-lived (1 hour TTL) and random enough to prevent guessing.
- **Rate limiting:** Cloudflare Workers has built-in rate limiting. Apply per-IP limits on room creation (e.g., 10 rooms/hour) and WebSocket message rate (e.g., 100 messages/sec per connection — well above the ~24 messages/sec game peak).
- **Message size limit:** Reject relay messages >64KB (largest legitimate message is a full keyframe at ~16KB compressed). Prevents abuse.
- **Room capacity:** Limit to 4 connections per room (matching the game's 4-player max). Reject additional joins.
- **Future:** Add optional room passwords. Add player accounts with a simple token-based auth (e.g., JWT from a separate auth endpoint).

## Graceful Degradation Under Relay Failures

If the Cloudflare relay drops messages mid-game (rate limit exceeded, transient Worker error, edge PoP failover):
- **Missed deltas:** Client detects gap (missing `server_tick` sequence) and sends `KeyframeRequest` (Phase 12). Server responds with full keyframe on next tick. Game hiccups for one keyframe interval (~5 seconds) but self-corrects.
- **Missed input:** Server's input jitter policy (Phase 14) repeats `held[]` state and buffers late `pressed[]` events. The game continues without the missing player's one-shot actions for the dropped ticks.
- **WebSocket disconnection:** IXWebSocket's built-in auto-reconnect (Phase 25) reconnects with exponential backoff. On reconnect, client sends `ClientReady`, receives a fresh keyframe, and resumes.
- **Sustained relay failure (>10 seconds):** `DISCONNECT_TIMEOUT_MS` fires, player transitions to AI control (Phase 30). If the relay recovers, the player can rejoin via the same room code.

This recovery path requires no special relay-aware code — the existing `KeyframeRequest`, input jitter, auto-reconnect, and disconnect timeout mechanisms handle it. The relay is a dumb pipe and the game protocol is already designed for unreliable delivery of cosmetic events and reliable (TCP-ordered) delivery of game-flow events.

**Relay broadcast optimization for keyframes:** Per-client deltas differ (each client's `accumulated_dirty` diverges), so they must be sent individually through the relay. But **full keyframes are identical for all clients** — the server currently sends N copies of the same keyframe (one per client). When using a relay, use the relay's broadcast (`relay` message type) to send the keyframe once; the Durable Object fans it out to all peers. This cuts keyframe relay cost by ~3x (1 message instead of 3 for a 4-player game). Per-tick deltas remain per-client. Implementation: add an `ITransport::broadcast(data, len)` method alongside `send(peer_id, data, len)`. `WebSocketServerTransport` implements broadcast as N individual sends (direct connections are already point-to-point). `RelayWebSocketTransport` implements broadcast as a single relay message.

**Relay cost awareness UI:** If the host increases game speed (`timer_wait < 4`), relay message rate scales linearly. At `timer_wait = 1` (~73 ticks/sec), relay traffic is ~6x the default — potentially exceeding the paid tier budget for sustained play. Add a warning in the "Host Game" UI when speed is increased while using a relay connection: "High game speed increases relay usage."

**Verify:** Deploy to Cloudflare Workers. Create room, connect two clients (one native, one browser). Relay binary messages between them. Measure added latency. Verify room listing, room expiry, and host migration on disconnect. Verify the game plays correctly end-to-end through the relay — lobby, game start, level transitions, exit prompts. **Simulate relay message drops** (inject packet loss in test) and verify the game self-corrects via keyframe request within 5 seconds.
