# Phase 14: In-Process Transport

> **See also:** [Phase 13 (ITransport)](phase-13-transport-interface.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Direct function call transport for local play — `send()` on one side enqueues to the other's receive buffer.

**Changes:**
- New files: `include/openglad/gameplay/net_transport_inprocess.h`, `src/gameplay/net_transport_inprocess.cpp`
- `InProcessTransport` creates linked pairs (server-side + client-side)
- **Zero-copy: bypasses serialization entirely.** For local play, `InProcessTransport` passes `shared_ptr<WorldSnapshot>` (and `shared_ptr<InputState>`) directly between server and client — no serialize/deserialize round-trip. This keeps local play's per-frame overhead near-zero compared to pre-networking. The `ITransport` interface provides both a typed API (`send_snapshot(shared_ptr<WorldSnapshot>)`, `send_input(shared_ptr<InputState>)`) and a raw bytes API (`send(peer_id, data, len)`). `InProcessTransport` implements the typed API; `WebSocketServerTransport`/`WebSocketClientTransport` implement the raw bytes API (which goes through serialization). `GameServer` and `GameClient` use whichever API the transport supports. The serialization path is exercised by unit tests and networked play.

## `ValidatingInProcessTransport` (CI mode)

The zero-copy `InProcessTransport` bypasses serialization entirely, which means the serialization path (Phase 9) is only exercised by unit tests until WebSocket transport arrives in Phase 25 — an 11-phase coverage gap. Add a `ValidatingInProcessTransport` variant that **serializes and deserializes every message** (snapshot, delta, input, events) through the Phase 9 binary serialization path, then passes the deserialized copy to the recipient. This turns every integration test into a serialization round-trip test.

- Enabled via a constructor flag or CMake option (e.g., `-DVALIDATE_SERIALIZATION=ON` in CI builds, off by default for local dev performance)
- Uses the same `ITransport` interface — `GameServer`/`GameClient` don't know they're being validated
- If any field fails round-trip (e.g., endianness bug, missed field in serialization table, non-trivially-copyable type sneaks in), the test fails immediately with the field name and diff
- **Zero production impact:** only active in CI builds. Local play always uses zero-copy `InProcessTransport`.

## `NetworkTestFixture`

Build a reusable `NetworkTestFixture` class alongside `InProcessTransport` — this is the primary way to validate everything from Phase 14 onward:
- Creates a `GameServer` + N `GameClient`s with `InProcessTransport` (or `ValidatingInProcessTransport` in CI)
- Loads a level on the server
- Steps N ticks (server ticks, clients receive snapshots)
- Verifies client world state matches server (entity counts, positions, field values)
- Parameterized: player count, level, tick count, input sequence

This fixture is used by all subsequent phases' integration tests. Without it, "all existing tests pass" doesn't validate any networking code.

**Verify:** Unit test — linked pair send/receive. Multi-client broadcast. Message ordering preserved. `NetworkTestFixture` runs a basic scenario (load level, tick 10 times, verify client matches server). CI builds verify serialization round-trip on every message via `ValidatingInProcessTransport`.
