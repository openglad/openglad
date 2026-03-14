# Phase 10: Snapshot Size Benchmark

> **See also:** [Phase 9 (Serialization)](phase-09-serialization-delta.md) | [Context (Bandwidth Budget)](docs/plans/networking/common/context.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Before committing to the serialization format, measure actual snapshot sizes on real game data. This validates the bandwidth budget and catches size estimate errors early.

**Changes:**
- Add a test or small utility that:
  1. Loads a level (`read_scenario()`) and runs 100 ticks of gameplay
  2. Calls `capture_snapshot()` (from Phase 6)
  3. Reports: entity count per list, raw `EntitySnapshot` size (bytes), total `WorldSnapshot` size, zlib-compressed size
  4. Runs 10 more ticks, captures again, computes a delta, reports delta size before/after zlib
- Can be a unit test or a standalone benchmark (e.g., `tests/unit/test_snapshot_size.cpp` — assign to an `og_add_unit_group()` in `CMakeLists.txt`)
- **Use a worst-case combat scenario** (4-player chaotic combat with many projectiles, explosions, and entity spawns) — not a quiet level. The bandwidth budget must hold under peak load.

**Expected results:**
- ~200 entities total across oblist/fxlist/weaplist
- ~200 bytes per packed entity (19 SimEntity + 44 walker + 22 stats fields, mix of floats/shorts/chars/ints)
- ~40KB per full snapshot uncompressed
- ~8-16KB after zlib (60-80% compression on structured data)
- ~2-5KB per delta uncompressed, ~1-3KB after zlib

If actual sizes differ significantly from these estimates, revisit the bandwidth budget and delta compression strategy before proceeding to transport phases.

**Verify:** Benchmark runs, numbers are reported. Update bandwidth budget section if needed.
