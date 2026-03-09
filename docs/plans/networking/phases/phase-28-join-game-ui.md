# Phase 28: Client "Join Game" UI

> **See also:** [Phase 24 (WebSocket Server)](phase-24-websocket-server.md) | [Phase 25 (WebSocket Client)](phase-25-websocket-client-native.md) | [Phase 31 (Relay)](phase-31-relay-matchmaking.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

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
