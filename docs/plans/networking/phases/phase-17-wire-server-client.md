# Phase 17: Wire Up GameServer/GameClient Alongside Old Path

> **See also:** [Phase 15 (GameServer/GameClient)](phase-15-server-client.md) | [Phase 16 (screen::act() split)](phase-16-split-screen-act.md) | [Phase 14 (InProcessTransport)](phase-14-inprocess-transport.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Create the GameServer/GameClient in-process wiring, running alongside the existing `screen::act()` path. Both paths work during this phase.

**Changes:**
- `glad_init()` / `glad_main()` in `src/platform/sdl/glad_gameplay.cpp:35-114` create GameServer + GameClient(s) based on `save_data.numplayers`
- GameServer calls `screen::tick_world()` + `capture_snapshot()` + accumulates dirty masks per client + `serialize_delta()` each tick
- GameClient receives snapshots via InProcessTransport + calls `apply_snapshot()` + `dispatch_cosmetic_events()` + `dispatch_game_flow_events()`
- `GameSession` (`include/openglad/platform/game_session.h`) owns server/client objects
- `game_frame_with_result()` routes through the server/client path
- **Git tag for bisect:** Before this phase, create a `git tag pre-networking-switchover` on the commit immediately before. This provides a reference point for bisecting behavioral differences without maintaining dead code. The tag is cheaper than a feature flag and doesn't rot.

## In-Process Single-Thread Execution Order

When server and client run on the same thread via `InProcessTransport` (local play), the exact per-frame execution order must be:

```
1. Client: capture local InputState via ctx().poll_input()
2. Client: InProcessTransport::send_input() (zero-copy: passes shared_ptr<InputState>)
3. Server: GameplayContextGuard install server context
4. Server: InProcessTransport::poll() → dequeue client input
5. Server: world_.tick() with collected input
6. Server: capture_snapshot() (zero-copy: produces shared_ptr<WorldSnapshot>)
7. Server: drain SimEventLog → build SimEventBatch + GameFlowEventBatch
8. Server: InProcessTransport::send_snapshot() (zero-copy: passes shared_ptr<WorldSnapshot> + event batches)
9. Server: ~GameplayContextGuard (restore previous context)
10. Client: GameplayContextGuard install client context
11. Client: InProcessTransport::poll() → dequeue snapshot + events
12. Client: apply_snapshot() on mirror world (no deserialize step — snapshot already in memory)
13. Client: dispatch_cosmetic_events() + dispatch_game_flow_events()
14. Client: ~GameplayContextGuard (restore previous context)
15. Render (using client's mirror world state + interpolation)
```

Steps 3-9 (server) and 10-14 (client) each have their own `GameplayContextGuard` scope. The debug assert in `GameplayContextGuard` catches context-switch bugs immediately if any step runs with the wrong `current_game` installed. Getting this order wrong produces mysterious desync — spell it out explicitly in the implementation.

- Emscripten state machine in `src/platform/sdl/glad.cpp` gets equivalent routing (the `GameState::Playing` branch at lines ~173-205)
- `screen::ready_for_battle()` (`src/interface/screen.cpp:693-720`) continues to set up viewscreens as before — the client just feeds them from snapshots instead of direct simulation

**Level editor (`openscen`) compatibility:** The editor has its own main loop (`level_editor()` at `level_editor.cpp:2995`) that never calls `screen::act()`, `game_frame()`, or `world_.tick()`. It only reads `world().grid` and `world().end` for editor purposes. Removing `screen::act()` does not break the editor build or runtime. Verified: zero `OPENSCEN` preprocessor guards exist in the codebase — the editor and game share the same source but diverge at the event loop level.

**Verify:** Game plays identically to before but internally uses server/client. Old `screen::act()` wrapper still exists but is no longer called from the game loop. Performance acceptable (InProcessTransport zero-copy adds negligible overhead). Manual playtesting with 1-4 players in split-screen.
