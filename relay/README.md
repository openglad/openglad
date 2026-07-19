# OpenGlad Relay (Cloudflare Worker + Durable Objects)

Deployable rendezvous/relay for OpenGlad networked multiplayer. Browsers
cannot listen for connections, so the web build (and optionally native
clients) meet in a named room on this relay, which forwards opaque binary
game frames between peers.

The wire protocol is defined by the C++ transports — 
`src/platform/emscripten/net_transport_relay_ws.cpp` and
`src/platform/sdl/net_transport_relay_ws.cpp` — and mirrored by the test stub
`tests/e2e/relay_stub.js`. This worker implements the same contract.

## HTTP API

All endpoints send permissive CORS (`Access-Control-Allow-Origin: *`), because
the game is served from a different origin.

| Endpoint | Description |
|----------|-------------|
| `POST /api/create?campaign=<hash>&campaign_name=<name>&host=<host name>` | Create a room. Returns `{"room_code":"GLAD-XXXX","code":"GLAD-XXXX","owner_token":"<32 hex>"}` (the C++ parser accepts `code` or `room_code`). |
| `GET /api/rooms[?campaign=<hash>]` | JSON array of joinable rooms: `code`, `campaign_hash`, `campaign_name`, `host_name`, `player_count`, `created_at` (ms). Only rooms with a connected peer are listed. |
| `WS /api/room/<CODE>[?owner_token=...]` | Join a room. Unknown rooms refuse the upgrade with HTTP 404 (the browser never fires the WS `open` event); malformed codes get 400; full rooms 409. |

Room codes are `GLAD-` plus 4 characters from an alphabet without the
confusable characters `0/O` and `1/I/L`; lookups are case-insensitive.

## WebSocket protocol

Control messages (TEXT, JSON, relay -> client; `host` is `0` while no host is
connected):

- to a new socket: `{"type":"joined","peer_id":N,"host":H}` then
  `{"type":"peer_list","peers":[...],"host":H}` (the list includes the new
  peer; clients filter themselves out)
- to everyone else: `{"type":"peer_joined","peer_id":N,"is_host":B}`
- on disconnect: `{"type":"peer_left","peer_id":N}`

Binary frames (opaque game payloads):

- client -> relay: `[0x01][target peer u32 LE][body]` (send to one peer)
- client -> relay: `[0x03][body]` (broadcast to everyone else)
- relay -> client: `[0x02][sender peer u32 LE][body]`

Peer ids count up from 1. The room owner (correct `owner_token`) always gets
peer id 1 and is the host, but clients read the `host` field rather than
assuming that.

## Room lifecycle

- Created via `POST /api/create`; joinable immediately (the owner's
  create -> connect gap is covered by the empty-room grace window).
- Owner reconnects supersede the previous owner socket (closed with 1012);
  other peers see no churn.
- Owner disconnect with guests present: guests receive
  `{"type":"peer_left","peer_id":1}` and are then closed (1001). The game
  server runs inside the host's client, so the room dies with it. The C++
  transport resets its host peer id on that `peer_left` and reports the
  closed link as `TransportLinkState::Lost`.
- Owner disconnect while alone: the room stays reconnectable for the
  empty-room grace window, then a Durable Object alarm deletes it.
- Alarms also enforce: rooms whose owner never connects (5 min), an absolute
  room age cap (12 h), and a registry heartbeat while occupied.

## Limits

| Limit | Value | Enforcement |
|-------|-------|-------------|
| Peers per room | 8 (one slot reserved for the owner) | HTTP 409 on join |
| Max inbound frame | 128 KiB (`kMaxInboundFrameBytes`); broadcasts 4 bytes less so the re-framed forward stays within the receivers' limit | WS close 1009 |
| Max inbound TEXT message | 4 KiB (game clients never send TEXT) | WS close 1009 |
| Per-connection messages | 2000 msgs or 8 MiB per second | WS close 1008 |
| Room creates per IP | 10 per minute | HTTP 429 |
| Empty-room TTL | 120 s (`EMPTY_ROOM_TTL_MS` var) | DO alarm |

## Architecture

- Stateless Worker routes -> one `GameRoom` Durable Object per room
  (`idFromName(code)`), using the WebSocket Hibernation API
  (`acceptWebSocket`/`serializeAttachment` + `webSocketMessage`/
  `webSocketClose`), so idle rooms hold no running isolate and cost nothing
  between messages. On wake the peer map is rebuilt from socket attachments.
- A single `RoomRegistry` Durable Object (strongly consistent, no KV
  namespace to provision) backs `GET /api/rooms` and the per-IP create rate
  limit. Rooms upsert their entry on every membership change plus a 5-minute
  heartbeat while occupied; entries not refreshed within 15 minutes are
  pruned on read, so a room DO that vanished without deregistering self-heals
  out of the listing. Rate counters are in-memory (a light limit; atomic
  because a DO is single-threaded).
- Both DO classes are SQLite-backed (`new_sqlite_classes`), which works on
  the free plan.

## Deploy

```bash
cd relay
npm ci
CLOUDFLARE_API_TOKEN=... npx wrangler deploy   # add CLOUDFLARE_ACCOUNT_ID=... if the token spans accounts
```

No custom domain; the worker serves on
`https://openglad-relay.<account-subdomain>.workers.dev`. Production also
mounts it SAME-ORIGIN at `https://openglad.pages.dev/relay` via the Pages
project's `web/_worker.js` router + a `RELAY` service binding to this Worker
(set on the Pages project; Durable Objects must live here, not in Pages).
The game's shipped default is the same-origin mount. Point the game elsewhere
via `OPENGLAD_RELAY_BASE_URL` (native/headless) or the
`kDefaultRelayBaseUrl` constants in the two `picker_lobby_network_client.cpp`
copies, or `window.__opengladRelayBaseUrlForTests` in browser tests.

## Tests

```bash
npm run typecheck   # tsc --noEmit
npm test            # vitest (@cloudflare/vitest-pool-workers, runs in workerd)
npm run smoke       # node smoke.mjs: spawns `wrangler dev --local` and drives
                    # the full protocol over real HTTP/WebSockets, including
                    # the empty-room expiry alarm (TTL overridden to 4 s via
                    # --var EMPTY_ROOM_TTL_MS:4000)
node smoke.mjs --url https://openglad.pages.dev/relay --skip-expiry   # or the workers.dev URL
                    # against a deployed relay (expiry check skipped: the
                    # deployed TTL is 120 s; verify it by creating a room,
                    # waiting >2 min, and confirming the WS upgrade 404s)
```

The unit suite (`npm test`) covers the same lifecycle deterministically,
driving the alarm handler directly (`runDurableObjectAlarm`) after aging
`empty_since` / `created_at`, so expiry logic is testable without waiting.
