# Phase 4 — Bound networking work per tick

## Phase Name
`bounded-network-drain`

## Implement Phase ID
`phase_4_bounded_network_drain`

## Preexisting Inputs
- `.plan/plan.md` — full implementation plan.
- `src/platform/sdl/local_transport_shadow.cpp:1053-1119` (`local_transport_shadow_finish_tick`) and `:182-204` (`poll_local_transport_client`).
- `include/openglad/gameplay/game_client.h:39-40` (`poll_messages` / `poll_messages(float)`).
- `include/openglad/gameplay/game_server.h:131` (`poll_incoming_messages`).
- `include/openglad/gameplay/net_constants.h` (`KEYFRAME_INTERVAL_TICKS` line 14, `MAX_GRID_DIRTY_TILES` line 21).
- `tests/unit/test_net_transport*.cpp` and `tests/test_network_fixture.h` — extend, do not replace.
- Existing `og_test_picker_network` group at `CMakeLists.txt:1667` — already links the shadow runtime.
- `include/openglad/core/runtime_trace.h` — `emit_runtime_trace` API for the overflow trace.

## New Outputs
- In `include/openglad/gameplay/net_constants.h`:
    ```cpp
    inline constexpr int MAX_INBOUND_MESSAGES_PER_TICK = 64;
    inline constexpr int MAX_OUTBOUND_MESSAGES_PER_TICK = 32;
    inline constexpr std::uint32_t MAX_NETWORK_BUDGET_US_PER_TICK = 2000; // 2 ms (advisory, traced not enforced)
    ```
- New overloads:
    - `void GameClient::poll_messages(float current_render_alpha, int max_messages);`
    - `void GameServer::poll_incoming_messages(int max_messages);`
    - `int GameServer::messages_drained_last_call() const;` (so the shadow can know whether the cap fired).
- New tests `tests/test_local_transport_shadow_budget.cpp` (in the existing `og_test_picker_network` group at `CMakeLists.txt:1667`):
    - Push 1000 fake snapshot messages; assert exactly `MAX_INBOUND_MESSAGES_PER_TICK` are drained per call and the un-drained queue persists.
    - Run repeated ticks until the queue drains; assert no tick processes more than the cap.
    - Assert exactly one `shadow_inbound_overflow` trace on the first tick of the storm and at least one across the storm.

## File Changes
- `include/openglad/gameplay/game_client.h`, `src/gameplay/game_client.cpp`: add the bounded overload; existing unbounded versions delegate with `INT_MAX`.
- `include/openglad/gameplay/game_server.h`, `src/gameplay/game_server.cpp`: same shape, plus `messages_drained_last_call()`.
- `src/platform/sdl/local_transport_shadow.cpp`:
    - `poll_local_transport_client()` gains an `int budget` parameter and forwards.
    - `local_transport_shadow_finish_tick()` calls `poll_local_transport_client(*session.myscreen_, client, MAX_INBOUND_MESSAGES_PER_TICK)`. If the server reports it drained the full budget, emit `runtime_trace` `shadow_inbound_overflow` so tests can assert the cap fired.
- New `tests/test_local_transport_shadow_budget.cpp` added to the `og_test_picker_network` group in `CMakeLists.txt`.

## Implementation Details
- Budget values (64 / 32 / 2 ms) are tunable but live in the header so they are visible in code review and jitter runs.
- When the cap is hit, the un-drained queue stays in the transport and is consumed next tick. Snapshots are idempotent under sequence numbers (already true), so deferred application is safe.
- The 2-ms time budget is advisory only in this phase (recorded in trace but not enforced by an additional clock check inside the loop). Hard time-budget enforcement is deferred — the message-count cap is the primary jitter safety net.

## Verification Phases
- **Phase ID:** `phase_4_check`
    - **Type:** `check`
    - **bounce_target:** `phase_4_bounded_network_drain`
    - **Purpose:** prove that snapshot/keyframe storms cannot stretch a frame because both inbound drain and outbound publish are capped per tick.
    - **Commands inline:**
        - `cmake --build --preset ci-test`
        - `ctest --preset ci-test --output-on-failure -R 'NetTransport|GameClient|GameServer|LocalTransportShadow'`

## Success Criteria
- New `tests/test_local_transport_shadow_budget.cpp` cases pass.
- Existing `og_test_picker_network` and the inprocess transport tests pass unchanged.
- `runtime_trace` records `shadow_inbound_overflow` at least once in the new storm test.

## Git Commit Requirement
Before yielding control, the implementer MUST stage and commit all changes with `git add -A && git commit -m "bounded-network-drain: <change>"`. Do not yield with a dirty working tree.
