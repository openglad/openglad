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

### Snapshot Infrastructure (Phases 4–10)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 4](phases/phase-04-input-serialization.md) | InputState Serialization | Bitpacked input ser/deser for client→server transport |
| [Phase 5](phases/phase-05-world-snapshot.md) | WorldSnapshot Data Structure | 86-field EntitySnapshot, WorldSnapshot struct, constexpr field table, event tiers |
| [Phase 6](phases/phase-06-snapshot-capture.md) | Snapshot Capture | `capture_snapshot()`: live GameWorld → WorldSnapshot, grid dirty tracking, DamageTile migration |
| [Phase 7](phases/phase-07-snapshot-application.md) | Snapshot Application | `apply_snapshot()`: WorldSnapshot → GameWorld, entity creation/removal, cross-ref resolution |
| [Phase 8](phases/phase-08-serialization-delta.md) | Serialization + Delta Compression | Binary serialization, dirty tracking instrumentation, per-client delta accumulation, CI safety-net |
| [Phase 9](phases/phase-09-snapshot-benchmark.md) | Snapshot Size Benchmark | Measure actual snapshot/delta sizes on real game data |
| [Phase 10](phases/phase-10-input-replay.md) | Input Replay System | Record/playback for debugging desync |

### Game Loop Restructuring (Phases 11–18)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 11](phases/phase-11-decouple-tick-render.md) | Decouple Sim Tick from Render Frame | Accumulator-based tick loop on all platforms |
| [Phase 12](phases/phase-12-transport-interface.md) | ITransport Interface + Protocol Framing | Abstract transport, message framing, Hello handshake, message types |
| [Phase 13](phases/phase-13-inprocess-transport.md) | In-Process Transport | Zero-copy local transport, ValidatingInProcessTransport, NetworkTestFixture |
| [Phase 14](phases/phase-14-server-client.md) | GameServer and GameClient | Server-authoritative core, input jitter policy, exit/pause protocols, divergence detection |
| [Phase 15](phases/phase-15-split-screen-act.md) | Split `screen::act()` | Pure refactor into `tick_world()`, `dispatch_cosmetic_events()`, `dispatch_game_flow_events()` |
| [Phase 16](phases/phase-16-wire-server-client.md) | Wire Up GameServer/GameClient | In-process server/client wiring alongside old path |
| [Phase 17](phases/phase-17-migrate-tests.md) | Migrate Tests and Delete Wrapper | All tests on server/client path, delete `screen::act()` wrapper |
| [Phase 18](phases/phase-18-visual-interpolation.md) | Client-Side Visual Interpolation | Position lerp at render framerate |

### Lobby System (Phases 19–22)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 19](phases/phase-19-lobby-data-model.md) | Lobby Data Model | LobbyPlayer, LobbyState, LobbyMessage types |
| [Phase 20](phases/phase-20-lobby-server.md) | Lobby Server Logic | Join/leave, team assignment, ready-up, start transition |
| [Phase 21](phases/phase-21-lobby-client-ui.md) | Lobby Client UI | Picker refactored as lobby client |
| [Phase 22](phases/phase-22-lobby-game-transition.md) | Lobby → Game Transition | Wire lobby completion to GameServer/GameClient creation |

### Network Transport (Phases 23–28)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 23](phases/phase-23-vendor-ixwebsocket.md) | Vendor IXWebSocket | Third-party WebSocket library |
| [Phase 24](phases/phase-24-websocket-server.md) | WebSocket Server Transport | IXWebSocket-based server transport with thread safety |
| [Phase 25](phases/phase-25-websocket-client-native.md) | WebSocket Client Transport (Native) | IXWebSocket-based client transport |
| [Phase 26](phases/phase-26-websocket-client-emscripten.md) | WebSocket Client Transport (Emscripten) | Browser WebSocket transport |
| [Phase 27](phases/phase-27-headless-server.md) | Headless Server Binary | SDL-free dedicated server |
| [Phase 28](phases/phase-28-join-game-ui.md) | Client "Join Game" UI | Host/Join buttons, direct connect + relay |

### Polish & Relay (Phases 29–31)

| Phase | Title | Description |
|-------|-------|-------------|
| [Phase 29](phases/phase-29-host-migration.md) | Host Migration (Lobby Only) | First-client-is-host with lobby-only migration |
| [Phase 30](phases/phase-30-network-robustness.md) | Network Robustness | Reconnection, AI takeover, error handling, heartbeat |
| [Phase 31](phases/phase-31-relay-matchmaking.md) | Relay Server + Matchmaking | Cloudflare Workers relay with room codes |
