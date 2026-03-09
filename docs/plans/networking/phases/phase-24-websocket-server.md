# Phase 24: WebSocket Server Transport

> **See also:** [Phase 12 (ITransport)](phase-12-transport-interface.md) | [Phase 23 (IXWebSocket)](phase-23-vendor-ixwebsocket.md) | [Phase 1 (thread safety invariant)](phase-01-entity-unique-ids.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

`WebSocketServerTransport : ITransport` using IXWebSocket.

**Changes:**
- New files in `src/platform/sdl/` (platform-specific, depends on IXWebSocket)
- Add to a new `OG_NETWORK_SOURCES` list in CMakeLists.txt (not `OG_PLATFORM_SOURCES` which only has 4 files: `native_input.cpp`, `sai2x.cpp`, `sound.cpp`, `video_sdl.cpp`). The new list is linked into the `og_platform_sdl` target.
- Uses `ix::WebSocketServer` — construct with port, set `onClientMessageCallback`, call `listenAndStart()`
- Incoming messages are pushed to a thread-safe queue (e.g., `std::mutex` + `std::deque<ReceivedMessage>`) by the I/O thread callback
- `poll()` drains the queue on the game loop thread — no locking during normal operation (try_lock + swap pattern)
- Same message framing as Phase 12 (protocol version + type + length + payload)

## Thread Safety Enforcement (Critical)

IXWebSocket creates **two threads per connection** (read + heartbeat) plus an acceptor thread. These I/O threads must NEVER touch `GameWorld`, `id_index_`, entity lists, or any simulation state directly. All interaction flows through the thread-safe message queue:

- **Receive path (I/O -> game thread):** I/O callback pushes `ReceivedMessage` to queue. Game thread drains via `poll()`. Already safe.
- **Send path (game thread -> I/O):** `send(peer_id, data, len)` is called from the game thread. IXWebSocket's `sendBinary()` is thread-safe (internally queues to the write thread). Safe.
- **Connection events (I/O thread):** `onClientConnected` / `onClientDisconnected` callbacks fire on I/O threads. These must NOT trigger `capture_snapshot()`, `read_scenario()`, or any GameWorld access. Instead, push a connection/disconnection event to the message queue. The game loop processes it on the next `poll()` and takes any action (e.g., sending a keyframe to a new client) from the game thread.

**Explicit prohibitions (enforced by code review + documented in header):**
- No `GameWorld&` or `GameServer&` references captured in I/O callbacks
- No `capture_snapshot()`, `apply_snapshot()`, or entity access from I/O threads
- No `id_index_` access from I/O threads
- Connection/disconnection handling is always deferred to game thread via queue

**Verify:** Unit test — verify message ordering is preserved under concurrent send/receive. Stress test — 4 clients sending input at 12Hz while server broadcasts snapshots at 12Hz, verify no data races (run under ThreadSanitizer / `-fsanitize=thread`). **ThreadSanitizer CI build:** Add a `ci-tsan` CMake preset (similar to existing `ci-asan`) that builds with `-fsanitize=thread`. Run the NetworkTestFixture with WebSocket transports under TSan. This catches data races that code review misses.
