# Phase 1: Add Entity Unique IDs

> **See also:** [Context & Key Decisions](../common/context.md) | [Verification Strategy](../common/verification-strategy.md)

Entities are currently identified by pointer only. The only existing ID system is `guy::id` (character-level, for duplicate detection in `SaveData::team_list`), which is unrelated. Everything downstream (serialization, snapshots, cross-references) needs stable entity-level IDs.

**Changes:**
- Add `uint32_t entity_id_ = 0` to `SimEntity` (`include/openglad/gameplay/sim_entity.h:23`)
- Add `uint32_t next_entity_id_ = 1` counter + `assign_entity_id()` to `GameWorld` (`include/openglad/gameplay/game_world.h`)
- Call `assign_entity_id()` inside `GameWorld::add_to_list()` (`src/gameplay/game_world.cpp:108-128` — this is a **private** method, but we're modifying GameWorld internals so access is fine. The public API is `add_ob()`/`add_fx_ob()`/`add_weap_ob()` which delegate to it.)
- Add `walker* find_by_id(uint32_t)` method on GameWorld (linear scan is fine for ~200 entities)
- Add persistent `std::unordered_map<uint32_t, walker*> id_index_` to GameWorld. Update it in `add_to_list()` (insert) and on entity removal (erase). `find_by_id()` uses this index for O(1) lookup. This is critical for Phase 7 (`apply_snapshot()`) where cross-reference resolution would otherwise be O(N^2).

**Thread safety invariant:** `id_index_` (and `GameWorld` in general) is **single-thread-access only**. All mutations and reads happen on the game loop thread. When WebSocket I/O threads arrive (Phase 24), all incoming messages are queued and drained via `poll()` on the game loop thread. The send path must also not touch GameWorld state from I/O callbacks (e.g., don't call `capture_snapshot()` from an `onClientConnected` callback — queue a "send keyframe" request for the game loop to process). This invariant applies to all GameWorld state introduced in subsequent phases.

**Verify:** Unit test (`tests/unit/`) — add entities, assert unique non-zero IDs. `find_by_id()` returns correct entity. `id_index_` stays consistent after add/remove. All existing tests pass.
