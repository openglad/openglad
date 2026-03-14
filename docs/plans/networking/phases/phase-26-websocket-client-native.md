# Phase 26: WebSocket Client Transport (Native)

> **See also:** [Phase 13 (ITransport)](phase-13-transport-interface.md) | [Phase 24 (IXWebSocket)](phase-24-vendor-ixwebsocket.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

`WebSocketClientTransport : ITransport` using IXWebSocket.

**Changes:**
- Same files as Phase 25 (client class alongside server class)
- Uses `ix::WebSocket` — `setUrl()`, `setOnMessageCallback()`, `start()`
- Same thread-safe queue pattern as server transport
- Handles reconnection via IXWebSocket's built-in auto-reconnect (configurable backoff)
