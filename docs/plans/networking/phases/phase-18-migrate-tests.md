# Phase 18: Migrate Tests and Delete Compatibility Wrapper

> **See also:** [Phase 17 (Wire Up)](phase-17-wire-server-client.md) | [Phase 14 (NetworkTestFixture)](phase-14-inprocess-transport.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Migrate integration tests that use the game sim loop from `game_frame()` / `screen::act()` to the GameServer/GameClient path, then delete the `screen::act()` wrapper.

**Test migration strategy:**
All integration tests that currently call `game_frame()` or `screen::act()` are migrated to use the `NetworkTestFixture` (built in Phase 14). The fixture creates a GameServer + GameClient with InProcessTransport, which exercises the real networking code path. This means every migrated integration test validates the server/client architecture as a side effect.

**Changes:**
- Migrate all integration tests in `ALL_INTEGRATION_TEST_SOURCES` that call `game_frame()` or `screen::act()` to use `NetworkTestFixture`. New test files must be added to `ALL_INTEGRATION_TEST_SOURCES` and assigned to an `og_add_test_group()` in `CMakeLists.txt`.
- Delete `screen::act()` compatibility wrapper
- Delete any dead code paths that only existed to support the wrapper
- **No feature flag, no dead code, no commented-out reference.** The server/client + InProcessTransport path is the only path. If a bug surfaces, it gets fixed — not worked around by reverting.

**Verify:** All ~1787 tests pass using the server/client path. No references to the old `screen::act()` remain (except in the level editor, which never called it). Manual playtesting confirms identical behavior.
