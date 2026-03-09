# Phase 23: Vendor IXWebSocket

> **See also:** [Context (Module Placement)](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

**Changes:**
- Add source to `third_party/ixwebsocket/`
- CMake: `add_subdirectory(third_party/ixwebsocket)` with `-DUSE_TLS=OFF -DUSE_ZLIB=OFF -DBUILD_DEMO=OFF`
- New CMake target: `og_ext_ixwebsocket` (or use IXWebSocket's native target name and alias)
- Exclude from Emscripten builds (Emscripten has built-in WebSocket API)
- Update `third_party/VENDORED_LIBS.md`

**Why IXWebSocket over libwebsockets:**
- **~13K LOC** (vs ~66K+ for lws core) — same scale as existing vendored libs (libzip, libyaml)
- **BSD-3-Clause** license (GPL v2 compatible)
- **Both server and client** in one library (`ix::WebSocketServer`, `ix::WebSocket`)
- **Zero dependencies** with TLS/zlib disabled (just pthreads)
- **Clean CMake** integration (~324-line CMakeLists vs lws's 172 `option()` directives)
- **Modern C++ API**: `sendBinary(data)` / `setOnMessageCallback()` — vs lws's verbose C callback switch statements
- **Threading model**: IXWebSocket creates **two threads per WebSocket connection** (one read, one heartbeat/ping) plus an acceptor thread on the server. For a 4-player game server: ~9-10 threads total (4 connections x 2 threads + 1 acceptor + main game loop). For a client: ~3 threads (1 connection x 2 + main). This is negligible. The game loop drains a thread-safe message queue each tick.
