# Phase 29: Host Migration (Lobby Only)

> **See also:** [Phase 20 (Lobby Server)](phase-20-lobby-server.md) | [Context (Known UX Limitations)](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Formalize first-client-is-host with migration on disconnect **in the lobby**.

**Changes:**
- `src/gameplay/lobby_server.cpp` — host assignment, migration on disconnect
- `lobby_state.h` — `host_player_id` field
- Picker UI conditionally shows host-only controls (scenario selection, force start)

**During gameplay:** If the host disconnects, all clients disconnect. There is no mid-game server migration. The dedicated headless server (Phase 27) is the recommended approach for sessions that need resilience.
