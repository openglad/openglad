# Phase 2: Add Cross-Reference ID Fields

> **See also:** [Context & Key Decisions](../common/context.md) | [Phase 1](phase-01-entity-unique-ids.md) | [Verification Strategy](../common/verification-strategy.md)

Walker has raw pointer cross-references that can't be serialized. Add parallel ID fields and setter helpers alongside the existing public pointers. This phase intentionally does **not** make the pointers private — that's a large mechanical refactor (~477+ compiler errors) that is deferred to a later cleanup to avoid blocking the networking critical path.

**All 5 cross-reference pointers:**
1. `walker* foe` (`walker.h:167`) — target enemy
2. `walker* leader` (`walker.h:168`) — commanding entity (summons, followers)
3. `walker* owner` (`walker.h:169`) — owning entity (for weapons)
4. `walker* collide_ob` (`walker.h:188`) — collision event target
5. `walker* controller` in `statistics` (`statistics.h:113`) — controlling entity for AI

**Changes:**
- Add `uint32_t foe_id = 0, leader_id = 0, owner_id = 0, collide_ob_id = 0` to walker (`include/openglad/gameplay/walker.h`)
- Add `uint32_t controller_id = 0` to statistics (`include/openglad/gameplay/statistics.h`)
- Add `set_foe(walker*)`, `set_leader(walker*)`, `set_owner(walker*)`, `set_collide_ob(walker*)` helpers that set both pointer and ID **and call `mark_dirty()` for the corresponding field bit** (see dirty tracking below)
- Add `set_controller(walker*)` helper on statistics **(also calls `mark_dirty()`)**
- Add `sync_ids_from_pointers()` method on walker that reads all 5 pointers and populates the corresponding `_id` fields from each pointer's `entity_id_`. Called as a safety net during `capture_snapshot()` (Phase 6) to guarantee consistency even for callers that haven't been migrated to setters yet.
- `walker_init_common()` at `src/gameplay/walker.cpp:92-126` already initializes all pointers to nullptr — update to also zero IDs
- Update stale-pointer cleanup in `GameWorld::tick()` (`src/gameplay/game_world.cpp:989-1014`) to also clear ID fields

**Dirty tracking infrastructure (foundation for setter-based delta compression — see [Phase 8](phase-08-serialization-delta.md)):**

The delta compression design uses **setter-based dirty tracking** rather than snapshot comparison. Dirty bits are set at the source (field mutation sites in gameplay code) and read during snapshot capture. This eliminates the expensive `compute_delta()` comparison step entirely.

- Add `uint64_t dirty_mask_[2] = {}` to `SimEntity` (`include/openglad/gameplay/sim_entity.h`). 128 bits covers all 86 serializable fields with headroom. Lives on the base class so walker, statistics, and subclass setters can all access it.
- Add inline `void mark_dirty(uint8_t bit) { dirty_mask_[bit / 64] |= (1ULL << (bit % 64)); }`
- Add inline `void mark_all_dirty() { dirty_mask_[0] = ~0ULL; dirty_mask_[1] = ~0ULL; }`
- Add inline `void clear_dirty() { dirty_mask_[0] = 0; dirty_mask_[1] = 0; }`
- Define **bit index constants** in a new header `include/openglad/gameplay/dirty_field_bits.h`:

```cpp
// include/openglad/gameplay/dirty_field_bits.h
// Bit indices for dirty_mask_[2]. Shared between entity setters and the
// constexpr field table (Phase 5). Adding a field = add a constant here
// + add an entry to SNAP_FIELDS[] + update the static_assert.

#pragma once
#include <cstdint>

namespace og::dirty {

// SimEntity fields (0-18)
inline constexpr uint8_t BIT_ENTITY_ID = 0;
inline constexpr uint8_t BIT_XPOS = 1;
inline constexpr uint8_t BIT_YPOS = 2;
// ... etc for all 86 fields ...
inline constexpr uint8_t BIT_DO_BOUNCE = 85;

inline constexpr uint8_t FIELD_COUNT = 86;

} // namespace og::dirty
```

- Call `mark_all_dirty()` in `GameWorld::add_to_list()` after assigning `entity_id_` — new entities are fully dirty.
- Cross-reference setters (`set_foe`, etc.) call `mark_dirty(BIT_FOE_ID)` etc.
- **Entity removal tracking:** Add `std::vector<uint32_t> removed_entity_ids_` to GameWorld. Populated in entity removal paths (stale-pointer cleanup, entity death). Drained by `capture_snapshot()` (Phase 6). This tracks which entities disappeared since the last capture, complementing the per-entity dirty bits which only track field changes on living entities.
- **Bug fix #1:** `GameWorld::tick()` currently cleans up stale pointers for `foe`, `leader`, `owner`, and `collide_ob` — but NOT `statistics::controller`. Add `controller` cleanup here too (clear both pointer and ID when the referenced entity is dead). This is a pre-existing latent bug. (Note: the comment at `game_world.cpp:1017-1018` about "viewscreen control pointer cleanup" refers to `viewscreen::control`, which is a *different* pointer cleaned in `screen::act()` lines 909-913 — NOT `statistics::controller`.)
- **Bug fix #2:** `GameWorld::tick()` stale-pointer cleanup iterates `oblist` (lines 990-1001) and `weaplist` (lines 1003-1014) but **NOT `fxlist`**. FX entities (explosions, chain lightning, etc.) can hold `foe`, `leader`, and `owner` pointers — these go stale when referenced entities die. Add `fxlist` to the stale-pointer cleanup loop. This is a second pre-existing latent bug.

**IMPORTANT: Bug fixes #1 and #2 are hard prerequisites for [Phase 6](phase-06-snapshot-capture.md).** `sync_ids_from_pointers()` reads all 5 cross-reference pointers. If any pointer is stale (dangling — points to freed memory), reading `entity_id_` from it is undefined behavior. These bug fixes must land before any code calls `sync_ids_from_pointers()`.

**Verify:** Unit test — `set_foe(other)` sets both pointer and ID. Clear foe clears both. `sync_ids_from_pointers()` populates IDs from raw pointers. Verify `statistics::controller` cleanup works. Verify `fxlist` stale-pointer cleanup works. All existing tests pass (no callers need to change — pointers remain public, setters are opt-in).
