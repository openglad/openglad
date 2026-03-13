# Phase 26: WebSocket Client Transport (Emscripten)

> **See also:** [Phase 12 (ITransport)](phase-12-transport-interface.md) | [Context (Module Placement)](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

`EmscriptenWebSocketTransport : ITransport` using `<emscripten/websocket.h>`.

**Changes:**
- Create `src/platform/emscripten/` directory (does not exist yet)
- New files: `src/platform/emscripten/net_transport_emscripten_ws.h`, `src/platform/emscripten/net_transport_emscripten_ws.cpp`
- Uses `emscripten_websocket_new()`, `emscripten_websocket_send_binary()`, onmessage callback
- Emscripten build already has Asyncify support (`-sASYNCIFY` in `CMakeLists.txt:1652-1668`) for non-blocking socket ops
- Must conditionally include this source in the Emscripten build path. Currently the Emscripten build (`CMakeLists.txt:1647`) builds `og_game_web` from `GAME_SOURCES_NO_MAIN` — add emscripten transport sources to this list under an `if(EMSCRIPTEN)` guard.
