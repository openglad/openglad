# Phase 8: Snapshot Serialization + Delta Compression

> **See also:** [Phase 5 (WorldSnapshot)](phase-05-world-snapshot.md) | [Phase 2 (dirty tracking)](phase-02-cross-reference-ids.md) | [Context](../common/context.md) | [Verification Strategy](../common/verification-strategy.md)

Convert WorldSnapshot to/from byte stream for transport, including both full keyframe and delta formats. This phase also completes the dirty tracking instrumentation deferred from Phase 3.

## Dirty Tracking Instrumentation (prerequisite for delta compression)

Before delta compression can work, **all ~200-400 remaining field mutation sites** across `src/gameplay/` must call `mark_dirty()`. The infrastructure (`dirty_mask_[2]`, `mark_dirty()`, bit constants) has existed since Phase 2, and the 5 cross-reference setters already call `mark_dirty()`. This step instruments everything else:

- Position fields (`xpos`, `ypos`, `worldx_`, `worldy_`): already go through `setxy()` / `set_world_pos()` — add `mark_dirty` there (~5-10 call sites)
- High-churn fields (`hitpoints`, `action`, `frame`, `curdir`, `busy`, `cycle`): add `mark_dirty` at mutation sites in combat code, `act()` methods, animation updates (~100-200 sites)
- Rarely-changing fields (`team_num`, `family`, `order`, `armor`, `level`): add `mark_dirty` at the few mutation sites (~20-30 sites)
- **Total: ~200-400 sites.** All mechanical — no design decisions, just adding a one-liner next to existing assignments.

Fields do NOT need to be made private for dirty tracking. The `mark_dirty()` call is added alongside the existing direct assignment. Other fields remain public with `mark_dirty()` as a convention enforced by the CI safety-net test (below in this phase).

The CI safety-net test (see below) is the **hard gate** for this instrumentation — it must pass before any networking code uses dirty-based deltas. Placing both the instrumentation and the test in the same phase ensures no window where bugs are latent.

## Manual Binary Serialization

Serialization is driven by the `constexpr` field descriptor table from Phase 5 (`snapshot_fields.h`). No external serialization library is needed — the field table makes generic ser/deser straightforward:

```cpp
// Generic field serialization -- ~50 lines total
void serialize_fields(std::vector<uint8_t>& buf, const EntitySnapshot& snap,
                      const uint64_t dirty_mask[2]) {
    for (const auto& field : SNAP_FIELDS) {
        if (!(dirty_mask[field.bit_index / 64] & (1ULL << (field.bit_index % 64))))
            continue;
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&snap) + field.snap_offset;
        buf.insert(buf.end(), src, src + field.size);
    }
}

void deserialize_fields(const uint8_t*& cursor, EntitySnapshot& snap,
                         const uint64_t dirty_mask[2]) {
    for (const auto& field : SNAP_FIELDS) {
        if (!(dirty_mask[field.bit_index / 64] & (1ULL << (field.bit_index % 64))))
            continue;
        uint8_t* dst = reinterpret_cast<uint8_t*>(&snap) + field.snap_offset;
        std::memcpy(dst, cursor, field.size);
        cursor += field.size;
    }
}
```

**Endianness:** The game targets x86/x64 (native) and WebAssembly (Emscripten) — all little-endian. Write serialization using explicit `htole32`/`le32toh`-style helpers (or inline equivalents keyed on `field.size` — 2/4/8 byte swaps) from the start. On little-endian architectures these compile to no-ops via compiler intrinsics, so there's zero runtime cost, but the code is correct-by-construction if the game ever targets ARM big-endian or runs a dedicated server on exotic hardware. This costs ~10 extra lines in the serialization helpers and avoids a subtle portability landmine.

**Advantages over msgpack:**
- Zero dependencies — no vendored library
- ~200 lines total for full ser/deser (field table does the heavy lifting)
- Maximally compact wire format (no type tags, no length prefixes per field — bit indices in dirty_mask implicitly encode field identity and type)
- Trivially debuggable (hex dump maps directly to field table offsets)
- Endianness is explicit, not hidden behind a library

## Full Keyframe Serialization

**Changes:**
- Add `serialize_snapshot()` / `deserialize_snapshot()` to `src/gameplay/world_snapshot.cpp`
- Outer envelope: protocol version byte + snapshot format version byte + binary payload
- Apply zlib compression on top of the binary payload (vendored at `third_party/physfs/zlib123/`, CMake target `og_ext_zlib`) — expect 60-80% compression ratio
- `serialize_snapshot()` returns `std::vector<uint8_t>` (compressed)
- `deserialize_snapshot()` takes `const uint8_t*, size_t` (decompresses internally)
- Protocol version byte at offset 0 of every serialized message (input, snapshot, lobby) — baked in from the start, not retrofitted later
- **Snapshot format version:** A separate snapshot format version byte inside the snapshot payload (distinct from the protocol version in the message header). Snapshot layout will evolve independently of the wire protocol (adding fields, changing layouts). This lets you handle "same protocol, different snapshot format" gracefully during development. **Version mismatch handling:** if a client receives a snapshot with an unrecognized format version, it disconnects with a descriptive error ("server snapshot format v3, client supports v2 -- please update"). No attempt at backward-compatible decoding.
- **Keyframe format:** All entity fields included, `dirty_mask = all bits set`.

## Delta Compression (Setter-Based Dirty Tracking)

Full keyframes (~40KB) every tick waste bandwidth when most entities don't change between ticks. Delta compression sends only changed fields per entity. Unlike comparison-based delta computation (which diffs two full snapshots), this design uses **setter-based dirty tracking**: dirty bits are set at the source during `GameWorld::tick()` (via `mark_dirty()` calls instrumented in this phase's prerequisite step above) and read during snapshot capture (Phase 6). **There is no `compute_delta()` function.** The delta is built directly from the dirty bits that gameplay code already set.

**Server-side per-client state:**

```cpp
struct PerClientState {
    uint32_t last_sent_tick = 0;
    // Per-entity accumulated dirty masks since last sent snapshot.
    // Each tick: OR this tick's entity dirty bits into the map.
    // On send: use accumulated mask to select fields, then clear.
    std::unordered_map<uint32_t, uint64_t[2]> accumulated_dirty;
    // Entity IDs that appeared since last sent snapshot.
    std::vector<uint32_t> new_entity_ids;
    // Entity IDs that disappeared since last sent snapshot.
    std::vector<uint32_t> removed_entity_ids;
};
```

**Per-tick server flow:**

1. `world_.tick()` — gameplay code sets dirty bits via `mark_dirty()` calls
2. `capture_snapshot()` — copies each entity's `dirty_mask_[2]` into the snapshot, clears entity dirty bits, drains `removed_entity_ids_` from GameWorld
3. **Accumulate per client:** For each entity in the snapshot, OR the entity's dirty mask into `accumulated_dirty[entity_id]` for every client. For new entities (in snapshot but not in any client's `accumulated_dirty`), add to `new_entity_ids`. For removed entities (from the drained list), add to `removed_entity_ids`.
4. **Build delta for each client:** Use that client's `accumulated_dirty` as the per-entity dirty mask. New entities get all-bits-set. Removed entities get the zero-mask sentinel.
5. **Serialize and send delta** to each client.
6. **Clear client state:** After sending, clear `accumulated_dirty`, `new_entity_ids`, `removed_entity_ids` for that client.

This means a client that's 3 ticks behind (missed 2 deltas) automatically gets the union of all dirty bits for those 3 ticks in its next delta — no special "catch-up" logic needed.

**Wire format:**
```
[0]    uint8_t  protocol_version
[1]    uint8_t  message_type (DeltaSnapshotMessage or SnapshotMessage for keyframe)
[2..3] uint16_t payload_length
[4..7] uint32_t server_tick (current tick number)
[8..]  zlib-compressed delta payload
```

Note: `baseline_tick` from the comparison-based design is replaced by `server_tick`. Since dirty bits are accumulated per-client, there's no "baseline snapshot" to reference — the server tracks what each client has seen via the accumulated mask. The client doesn't need to confirm which baseline it holds; the server knows.

**Delta payload per entity:**
```
uint32_t entity_id
uint64_t dirty_mask[2]  (both zero = removed entity sentinel)
[raw field bytes for each set bit, in bit-index order per SNAP_FIELDS table]
```

**Changes:**
- Add `PerClientState` struct to `include/openglad/gameplay/game_server.h`
- Add `apply_delta()`, `serialize_delta()`, `deserialize_delta()` to `src/gameplay/world_snapshot.cpp`
- **No `compute_delta()` function.** Delta masks come directly from accumulated dirty bits.
- Field-to-bit mapping: uses the same bit index constants from `dirty_field_bits.h` (Phase 2) and `SNAP_FIELDS` constexpr table (Phase 5). 19 SimEntity fields + 44 walker fields + 22 stats fields + 1 weap field = **86 fields** -> `uint64_t dirty_mask[2]` (128 bits). First 64 fields in `dirty_mask[0]`, remaining 22 in `dirty_mask[1]`. When `dirty_mask[1]` is all-zero for a given delta (common case — most entities don't touch stats fields), it can be omitted on the wire with a flag bit.

**Expected savings:**
- Typical tick: ~20 entities change out of ~200, ~5-10 fields change per entity
- Delta size before zlib: ~2-5KB (vs ~40KB full)
- After zlib: ~1-3KB
- At `DEFAULT_SIM_TICKS_PER_SEC` ticks/sec, 4 clients: ~50-150KB/sec outbound (vs ~600-800KB/sec with full snapshots + zlib)

**zlib bypass for tiny deltas:** zlib adds ~11 bytes of header overhead. For very small deltas (<64 bytes uncompressed — e.g., only 1-2 entities changed a single field), compression may increase size. Add a flag bit in the message header: if set, payload is uncompressed. `serialize_delta()` compresses, checks if compressed size >= uncompressed size, and sends whichever is smaller.

## CI Safety-Net Test (Critical)

The dirty-tracking design has an inherent correctness risk: if a `mark_dirty()` call is missed at a mutation site, that field silently stops updating on clients. Periodic keyframes mask this (every `KEYFRAME_INTERVAL_TICKS` ~5 seconds), but the field is stale between keyframes.

Add a **comparison-based validation test** that runs alongside the dirty-bit path in CI builds:

```cpp
// tests/test_dirty_tracking_safety.cpp
// For each tick in a combat scenario:
// 1. Capture snapshot using dirty bits (the real path)
// 2. Capture a second "reference" snapshot by brute-force comparing all fields
//    against the previous tick's full snapshot
// 3. Assert the dirty-bit snapshot's mask is a SUPERSET of the reference mask
//    (extra dirty bits are OK — conservative. Missing bits = bug.)
```

This test runs a worst-case 4-player combat scenario (many entity spawns, deaths, projectiles, explosions, AI actions, speed changes) for 200+ ticks and validates every single entity's dirty mask against the brute-force reference. If any `mark_dirty()` call was missed during the instrumentation pass (this phase's prerequisite step), this test catches it.

**This test is a hard gate for Phase 8 completion.** It must pass before any networking code uses dirty-based deltas.

**Verify:** Unit test — round-trip full snapshots through bytes. Build delta from accumulated dirty masks, apply delta to client baseline, assert result matches server state. Full pipeline test: capture -> serialize -> deserialize -> apply -> capture -> assert match. Test with: no changes (empty delta), all changes (equivalent to keyframe), entity spawn, entity removal, multi-tick accumulation (client misses 3 ticks, next delta has union of all dirty bits). Verify round-trip through serialization. Verify compression ratio is within expected range (cross-check with Phase 9 benchmark). CI safety-net test passes on worst-case combat scenario.
