# Phase 25: WebSocket Client Transport (Native)

> **See also:** [Phase 12 (ITransport)](phase-12-transport-interface.md) | [Phase 23 (IXWebSocket)](phase-23-vendor-ixwebsocket.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

`WebSocketClientTransport : ITransport` using IXWebSocket.

**Changes:**
- Same files as Phase 24 (client class alongside server class)
- Uses `ix::WebSocket` — `setUrl()`, `setOnMessageCallback()`, `start()`
- Same thread-safe queue pattern as server transport
- Handles reconnection via IXWebSocket's built-in auto-reconnect (configurable backoff)
