# Phase 17: Migrate Tests and Delete Compatibility Wrapper

> **See also:** [Phase 16 (Wire Up)](phase-16-wire-server-client.md) | [Phase 13 (NetworkTestFixture)](phase-13-inprocess-transport.md) | [Verification Strategy](../common/verification-strategy.md)

Migrate all ~145 integration tests from `game_frame()` → `screen::act()` to the GameServer/GameClient path, then delete the `screen::act()` wrapper.

**Test migration strategy:**
All integration tests that currently call `game_frame()` are migrated to use the `NetworkTestFixture` (built in Phase 13). The fixture creates a GameServer + GameClient with InProcessTransport, which exercises the real networking code path. This means every integration test validates the server/client architecture as a side effect.

**Changes:**
- Migrate all integration tests in `TEST_SOURCES` that call `game_frame()` to use `NetworkTestFixture`
- Delete `screen::act()` compatibility wrapper
- Delete any dead code paths that only existed to support the wrapper
- **No feature flag, no dead code, no commented-out reference.** The server/client + InProcessTransport path is the only path. If a bug surfaces, it gets fixed — not worked around by reverting.

**Verify:** All ~894 tests pass using the server/client path. No references to the old `screen::act()` remain (except in the level editor, which never called it). Manual playtesting confirms identical behavior.
