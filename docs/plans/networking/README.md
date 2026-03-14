# OpenGlad Networking Implementation Plan

Networked multiplayer for OpenGlad, implemented in incremental phases. Server-authoritative model with full state snapshots, delta compression, and WebSocket transport.

## Common Reference

| Document | Description |
|----------|-------------|
| [Context & Key Decisions](common/context.md) | Architecture decisions, sim tick rate, bandwidth budget, module placement, known UX limitations |
| [Milestones](common/milestones.md) | Key milestone summary across all phases |
| [Execution Order](common/execution-order.md) | Phase sequencing and parallelism rules |
| [Verification Strategy](common/verification-strategy.md) | Per-phase verification command |
| [Review Notes](common/review-notes.md) | Codebase verification, independent review findings, and design augmentations from multiple review passes |

## Phases

### Foundation (Phases 0–3)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 0](phases/phase-00-migrate-rand.md) | Migrate `rand()` to `SimRandom` | Deterministic simulation: migrate 17 call sites, fix `pow()`, delete dead code |
| [Phase 1](phases/phase-01-entity-unique-ids.md) | Add Entity Unique IDs | Stable `entity_id_` on SimEntity, ID index on GameWorld |
| [Phase 2](phases/phase-02-cross-reference-ids.md) | Add Cross-Reference ID Fields | Parallel ID fields + setters for 5 cross-ref pointers, dirty tracking infrastructure, bug fixes |
| [Phase 3](phases/phase-03-cross-reference-migration.md) | Compiler-Driven Cross-Reference Migration | Privatize all 5 cross-ref pointers, fix ~477 compiler errors |

### Snapshot Infrastructure (Phases 4–11)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 4](phases/phase-04-input-serialization.md) | InputState Serialization | Bitpacked input ser/deser for client→server transport |
| [Phase 5](phases/phase-05-world-snapshot.md) | WorldSnapshot Data Structure | 86-field EntitySnapshot, WorldSnapshot struct, constexpr field table, event tiers |
| [Phase 6](phases/phase-06-snapshot-capture.md) | Snapshot Capture | `capture_snapshot()`: live GameWorld → WorldSnapshot, grid dirty tracking, DamageTile migration |
| [Phase 7](phases/phase-07-snapshot-application.md) | Snapshot Application | `apply_snapshot()`: WorldSnapshot → GameWorld, entity creation/removal, cross-ref resolution |
| [Phase 8](phases/phase-08-setter-dirty-tracking.md) | Dirty Tracking via Setter Refactor | Make 86 serializable fields private, add getter/setter pairs with `mark_dirty()`, CI safety-net test |
| [Phase 9](phases/phase-09-serialization-delta.md) | Serialization + Delta Compression | Binary serialization, per-client delta accumulation, wire format |
| [Phase 10](phases/phase-10-snapshot-benchmark.md) | Snapshot Size Benchmark | Measure actual snapshot/delta sizes on real game data |
| [Phase 11](phases/phase-11-input-replay.md) | Input Replay System | Record/playback for debugging desync |

### Game Loop Restructuring (Phases 12–19)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 12](phases/phase-12-decouple-tick-render.md) | Decouple Sim Tick from Render Frame | Accumulator-based tick loop on all platforms |
| [Phase 13](phases/phase-13-transport-interface.md) | ITransport Interface + Protocol Framing | Abstract transport, message framing, Hello handshake, message types |
| [Phase 14](phases/phase-14-inprocess-transport.md) | In-Process Transport | Zero-copy local transport, ValidatingInProcessTransport, NetworkTestFixture |
| [Phase 15](phases/phase-15-server-client.md) | GameServer and GameClient | Server-authoritative core, input jitter policy, exit/pause protocols, divergence detection |
| [Phase 16](phases/phase-16-split-screen-act.md) | Split `screen::act()` | Pure refactor into `tick_world()`, `dispatch_cosmetic_events()`, `dispatch_game_flow_events()` |
| [Phase 17](phases/phase-17-wire-server-client.md) | Wire Up GameServer/GameClient | In-process server/client wiring alongside old path |
| [Phase 18](phases/phase-18-migrate-tests.md) | Migrate Tests and Delete Wrapper | All tests on server/client path, delete `screen::act()` wrapper |
| [Phase 19](phases/phase-19-visual-interpolation.md) | Client-Side Visual Interpolation | Position lerp at render framerate |

### Lobby System (Phases 20–23)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 20](phases/phase-20-lobby-data-model.md) | Lobby Data Model | LobbyPlayer, LobbyState, LobbyMessage types |
| [Phase 21](phases/phase-21-lobby-server.md) | Lobby Server Logic | Join/leave, team assignment, ready-up, start transition |
| [Phase 22](phases/phase-22-lobby-client-ui.md) | Lobby Client UI | Picker refactored as lobby client |
| [Phase 23](phases/phase-23-lobby-game-transition.md) | Lobby → Game Transition | Wire lobby completion to GameServer/GameClient creation |

### Network Transport (Phases 24–29)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 24](phases/phase-24-vendor-ixwebsocket.md) | Vendor IXWebSocket | Third-party WebSocket library |
| [Phase 25](phases/phase-25-websocket-server.md) | WebSocket Server Transport | IXWebSocket-based server transport with thread safety |
| [Phase 26](phases/phase-26-websocket-client-native.md) | WebSocket Client Transport (Native) | IXWebSocket-based client transport |
| [Phase 27](phases/phase-27-websocket-client-emscripten.md) | WebSocket Client Transport (Emscripten) | Browser WebSocket transport |
| [Phase 28](phases/phase-28-headless-server.md) | Headless Server Binary | SDL-free dedicated server |
| [Phase 29](phases/phase-29-join-game-ui.md) | Client "Join Game" UI | Host/Join buttons, direct connect + relay |

### Polish & Relay (Phases 30–32)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 30](phases/phase-30-host-migration.md) | Host Migration (Lobby Only) | First-client-is-host with lobby-only migration |
| [Phase 31](phases/phase-31-network-robustness.md) | Network Robustness | Reconnection, AI takeover, error handling, heartbeat |
| [Phase 32](phases/phase-32-relay-matchmaking.md) | Relay Server + Matchmaking | Cloudflare Workers relay with room codes |
