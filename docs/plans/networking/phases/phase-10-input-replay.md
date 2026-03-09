# Phase 10: Input Replay System

> **See also:** [Phase 0 (deterministic RNG)](phase-00-migrate-rand.md) | [Phase 4 (InputState serialization)](phase-04-input-serialization.md) | [Verification Strategy](../common/verification-strategy.md)

With deterministic RNG (Phase 0), InputState serialization (Phase 4), and snapshot infrastructure (Phases 5-8), the codebase has everything needed for an input replay system. This is invaluable for debugging desync issues during later networking phases — record a game on the server, replay it offline to reproduce divergence.

**Changes:**
- New files: `include/openglad/gameplay/replay.h`, `src/gameplay/replay.cpp`
- Add to `OG_SIM_SOURCES` in CMakeLists.txt

**`ReplayRecorder`:**
- On game start: record initial RNG seed (`SimRandom::state_`), level ID, player count, `timer_wait`
- Each tick: append the `InputState` (serialized via Phase 4 bitpacking — ~17 bytes per tick)
- On game end: write to file (`.ogr` — OpenGlad Replay)
- File format: header (version, seed, level_id, player_count, timer_wait) + sequence of serialized InputState frames
- At 12 ticks/sec, a 10-minute game = ~7200 ticks × 17 bytes = ~120KB uncompressed. Trivially small.

**`ReplayPlayer`:**
- Load replay file, seed RNG, load level
- Each tick: deserialize the next `InputState` from the replay, feed to `GameWorld::tick()` as if it were live input
- Verify: if a `WorldSnapshot` was recorded at intervals (optional — every N ticks), compare against current state to detect divergence. Log the first differing field + tick number.

**Integration with Phase 9 benchmark:**
The replay system can also be used to create reproducible benchmark scenarios: record a chaotic 4-player combat session, replay it to measure snapshot sizes under identical conditions across code changes.

**Verify:** Record a short game (load level, tick 100 times with scripted input), save replay file, play it back, verify final world state matches. Test with multiple player counts. Verify replay produces identical RNG state at each tick.
