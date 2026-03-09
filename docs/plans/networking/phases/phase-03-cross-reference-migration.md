# Phase 3: Compiler-Driven Cross-Reference Migration

> **See also:** [Phase 2](phase-02-cross-reference-ids.md) | [Verification Strategy](../common/verification-strategy.md)

Make all 5 cross-reference pointer fields private, then fix every compiler error to use the setters from Phase 2. This enforces that all future code uses the dual pointer+ID setters, eliminating the need for `sync_ids_from_pointers()`.

**This phase executes immediately after Phase 2.** While `sync_ids_from_pointers()` provides a safety net, it reads raw pointers every tick during `capture_snapshot()` (Phase 6) — a latent UB risk if any stale pointer path was missed by the Phase 2 bug fixes. Completing the migration now eliminates the entire class of stale-pointer-during-snapshot bugs before any snapshot code is written. The ~477 compiler errors are mechanical and touch gameplay code that will be modified in later phases anyway — getting it done early means every subsequent phase benefits from enforced setter usage.

After this phase, `sync_ids_from_pointers()` can be removed or converted to a debug-only assertion.

**Scope (grep counts, including test files):**
- `->foe = ...`: ~201 grep hits (gameplay + test code)
- `->leader = ...`: ~86 grep hits
- `->owner = ...`: ~157 grep hits
- `->collide_ob = ...`: ~31 grep hits
- `.controller = ...`: ~3 grep hits (stats.cpp:53,56,95 — 3 other grep hits in family files are local variable declarations, not field assignments)
- **Estimated total: ~477+ compiler errors to fix** across `src/gameplay/`, `src/interface/`, `include/openglad/gameplay/`, and `tests/`.

**Dirty tracking instrumentation is deferred to [Phase 8](phase-08-serialization-delta.md).** The infrastructure (`dirty_mask_[2]`, `mark_dirty()`, `mark_all_dirty()`, `clear_dirty()`, bit constants in `dirty_field_bits.h`) exists from Phase 2, and the cross-reference setters already call `mark_dirty()`. But the large-scale instrumentation of all ~200-400 remaining field mutation sites is deferred to Phase 8 where delta compression actually needs it. Until then, keyframe captures set all bits (all fields dirty), so the absence of per-field `mark_dirty()` calls has no effect. This keeps Phase 3 focused on a single concern (pointer privatization) and ensures the instrumentation and its CI safety-net test land in the same phase.

**Recommended commit strategy:** Do the privatization one pointer at a time as separate commits (`foe` first at ~201 sites, then `owner` at ~157, then `leader` at ~86, then `collide_ob` at ~31, then `controller` at ~3). Each commit is independently compilable and reviewable.

**Verify:** No raw pointer assignments remain for the 5 cross-reference fields. `sync_ids_from_pointers()` removed or converted to debug-only assertion. All existing tests pass.
