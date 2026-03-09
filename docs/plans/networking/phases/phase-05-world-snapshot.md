# Phase 5: WorldSnapshot Data Structure

> **See also:** [Context & Key Decisions](../common/context.md) | [Phase 2 (dirty tracking)](phase-02-cross-reference-ids.md) | [Verification Strategy](../common/verification-strategy.md)

Define the snapshot structs that represent a complete world state at a point in time. This phase requires explicitly deciding what to serialize and what to skip.

**Changes:**
- New header: `include/openglad/gameplay/world_snapshot.h`

## `EntitySnapshot` — Fields to Include

**From `SimEntity` base (19 fields: 18 original serializable + entity_id_ from Phase 1):**
- `entity_id_` (added in Phase 1), `xpos`, `ypos`, `sizex`, `sizey`
- `team_num`, `real_team_num`, `user`
- `dead`, `death_called`, `invulnerable_left`, `invisibility_left`, `flight_left`, `bonus_rounds`
- `order`, `family`, `frame`
- `worldx_`, `worldy_` (authoritative float position — serialized as IEEE 754 `float`, 4 bytes each, preserving full precision since the source type is `float`)

> **Note on field count:** SimEntity has 19 total fields (16 public + 3 protected: `worldx_`, `worldy_`, `frames`). `frames` (protected, `sim_entity.h:65`) is skipped (derived from order/family). That leaves 18 original serializable + `entity_id_` = 19.

**From `walker` (serialize these 44 fields, including 4 cross-ref IDs from Phase 2):**
- Movement: `lastx`, `lasty`, `stepsize`, `normal_stepsize`, `curdir`, `enddir` (6)
- Combat: `damage`, `fire_frequency`, `busy`, `current_weapon`, `default_weapon`, `attack_lunge`, `attack_lunge_angle`, `hit_recoil`, `hit_recoil_angle`, `last_hitpoints` (10)
- State: `action`, `act_type`, `old_act_type`, `ani_type`, `cycle`, `drawcycle`, `current_special`, `ignore`, `in_act`, `shifter_down`, `yo_delay`, `skip_exit`, `outline`, `hurt_flash` (14)
- Timers: `lifetime`, `speed_bonus`, `speed_bonus_left`, `charm_left`, `weapons_left` (5)
- Identity: `keys`, `view_all`, `lineofsight` (3)
- AI: `path_check_counter` — controls AI re-pathfinding frequency, affects simulation timing (1)
- Healing: `regen_delay_` (protected) — post-damage healing cooldown in ticks, directly affects HP regen via `compute_hp_regen()` in `combat_math.h` (1)
- Cross-reference IDs: `foe_id`, `leader_id`, `owner_id`, `collide_ob_id` (4)
- `do_bounce` — lives on `weap` subclass (`weap.h:44`), not on `walker` base. Capture requires `dynamic_cast<weap*>` or checking `order == Order::Weapon`. Set to 0 for non-weapon entities. (Only `weap` has extra data fields among the 4 subclasses — `living`, `treasure`, and `effect` are behavior-only with no additional state.) (1)

**From `statistics` (serialize these 22 fields):**
- `hitpoints`, `max_hitpoints`, `magicpoints`, `max_magicpoints` (4)
- `max_heal_delay`, `current_heal_delay`, `max_magic_delay`, `current_magic_delay` (4)
- `magic_per_round`, `heal_per_round`, `armor`, `level` (4)
- `bit_flags`, `delete_me`, `frozen_delay`, `weapon_cost` (4)
- `special_cost[NUM_SPECIALS]` (6-element array — `NUM_SPECIALS = 6` at `statistics.h:30` — uses **1 dirty bit** for the whole array; if any element changed, all 12 bytes are sent. The array is small enough that per-element tracking isn't worth the complexity.)
- `old_order`, `old_family`, `last_distance`, `current_distance` (4)
- `controller_id` (1)

**Total serializable fields:** 19 SimEntity + 44 walker + 22 stats + 1 weap = **86 fields**.

## Fields to Explicitly SKIP (not serialized)

- `SimEntity::frames` (protected, `sim_entity.h:65`) — total frame count for sprite sheet. Derivable from `(order, family)` via `entity_configurator` callback, same as animation data. Constant for a given entity type.
- `walker::ani` — pointer to static animation table, reconstruct from `(order, family, ani_type)`
- `walker::path_to_foe` — pathfinding state, entities re-path on restore (controlled by `path_check_counter` which IS serialized)
- `walker::damage_numbers` — ephemeral render-only UI data (floating damage text)
- `walker::render_` — render component, client-side only
- `walker::myself_` — self-pointer, trivially reconstructed
- `walker::owned_myguy_` — character data ownership, handled separately (see myguy section below)
- `walker::myguy` — non-owning pointer, reconstructed from `guy_id` (see myguy section below)
- `walker::foe`, `walker::leader`, `walker::owner`, `walker::collide_ob` — raw pointers, reconstructed from `_id` fields via `find_by_id()` in `apply_snapshot()`
- `statistics::controller` — raw pointer, reconstructed from `controller_id` via `find_by_id()`
- `statistics::commands` — AI command queue. On snapshot restore, AI re-evaluates next tick. Serializing queued commands adds complexity for minimal benefit since commands are generated fresh each tick based on world state. **Known side effect:** discarding in-flight commands causes AI behavioral jitter (see "Known UX Limitations" in [Context](../common/context.md)).
- `statistics::name` — NPC name string, reconstruct from family/order on entity creation
- `statistics::walkrounds` — **deleted in Phase 0** (dead code cleanup). No longer exists.

## `myguy` Handling

Each walker can reference a `guy*` (player character data from `SaveData::team_list`). For networking:
- Player character `guy` data is sent at game start as part of initial setup (name, family, stats, exp, level — 22 data fields from `include/openglad/gameplay/guy.h:28-74`)
- **Mid-game `guy` mutations:** `guy` fields change during gameplay (exp, kills, scen_damage, scen_kills, scen_damage_taken, scen_min_hp, scen_shots, scen_hits — see `guy.h:48-68`). These mutations happen in `walker_combat.cpp` and `stats.cpp`. Include changed `guy` fields in the snapshot for player-controlled entities. This adds ~30 bytes per player character per snapshot (up to 4 players = ~120 bytes total — negligible).
- Clients store received `guy` data in a local lookup table (e.g., `std::unordered_map<int, guy>` keyed by `guy::id`) for pointer reconstruction
- `EntitySnapshot` stores only a `guy_id` (int, matching `guy::id` from `SaveData`) to link walker to character. **The actual `guy` data (name, stats, etc.) lives in a separate `std::vector<GuySnapshot>` on `WorldSnapshot`, NOT embedded in `EntitySnapshot`.** This is critical: `EntitySnapshot` must remain `trivially_copyable` for `memcpy`-based serialization (see `static_assert` below). Embedding `guy::name` (`std::string`) in `EntitySnapshot` would break this invariant.
- Clients reconstruct the `myguy` pointer from their local guy lookup table after `apply_snapshot()`, and update the local `guy` fields from the snapshot data

## `WorldSnapshot` — World-Level State

- `tick_count` (from `GameWorld::tick_count_`)
- `rng_state` (from `GameWorld::rng_` — LCG seed value, `rng_.state_` is public)
- `level_tick_count_` (private, `game_world.h:157` — needs public accessor, same treatment as `regen_delay_`; see Phase 6)
- Game flow flags: `level_done`, `game_ended`, `end`, `retry`, `next_level`, `ending`
- `enemy_freeze` timer
- `timer_wait` (sim speed)
- `living_count`
- `control_hp` — HP of control point objective, changes during combat
- `withdraw_requested`, `withdraw_level` — player withdrawal state, affects game flow
- `guy_id_counter` — entity ID allocator, server and client must agree for consistency
- Per-player scores: `m_score[4]` from `GameWorld` (NOT from SaveData — during gameplay, `GameWorld::m_score[4]` at `game_world.h:142` is the authoritative source. SaveData scores are only the persistent store, synced via `sync_save_data_from_world()` between levels.)
- Per-player `guy` snapshot data (for entities with `myguy != nullptr` — see myguy handling above)
- Vectors of `EntitySnapshot`: one vector each for oblist, fxlist, weaplist (skip dead_list — dead entities don't affect simulation). Note: source data is `std::list<std::unique_ptr<walker>>` (linked lists), but snapshot uses vectors for compact serialization.
- **Dead player entities in oblist:** Dead entities with `myguy != nullptr` (player characters) are NOT moved to `dead_list` during `GameWorld::tick()` (see `game_world.cpp:1019-1027` — only entities with `myguy == nullptr` are moved). These dead player entities remain in `oblist` for respawn/scoring purposes and MUST be included in the oblist snapshot vector.
- `current_palette_id` (uint8_t) — 0 = normal palette, 1 = blue/freeze palette. Included in world-level state so that clients always converge to the correct palette on every keyframe, eliminating the SetPalette desync risk entirely. 1 byte per snapshot — negligible cost for guaranteed correctness.
- `pending_exit_prompt` — server-side exit prompt state (see Phase 14). Serialized so clients know whether the sim is paused for a prompt.
- `paused` (bool) — whether the game is paused by a player. `pause_player_index` (uint8_t) — which player paused. Clients display "PAUSED by [name]" overlay when set. See Phase 14 pause protocol.

**Note:** Level metadata (`id`, `title`, `type`, `par_value`, `time_bonus_limit`, `difficulty`, `pixmaxx`, `pixmaxy`, `mysmoother`) and team config (`my_team`, `allied_mode`, `current_scenario`, `completed_levels`) are constant during a level. These are sent once at level start via `InitialSetup`, not included in per-tick snapshots. `completed_levels` (`std::set<int>` at `game_world.h:147`) is campaign progression state — the server updates it between levels and includes the updated set in each `InitialSetup` message.

**Grid data:** The tile grid (`GameWorld::grid`, a `PixieData` with `w`, `h`, and `data[]` of size w*h) is sent as part of `InitialSetup` at level start. However, tile damage during gameplay mutates `grid.data[]` (see DamageTile note above). After DamageTile is moved into the simulation layer, the grid becomes authoritative server state. Include a **grid dirty flag + changed tile list** in every tick's delta/keyframe. The grid is small (~2400 bytes for a 60x40 map), so even a full grid resend in keyframes is cheap. Per-tick grid deltas typically contain only a few changed tiles (from explosions), adding negligible bytes.

## SimEventLog Events

**SimEventLog events** are sent as a **separate `SimEventBatch` message** alongside each snapshot/delta, NOT embedded inside the snapshot struct. Events are one-shot side effects (play sound, show notification) while snapshots are idempotent state — mixing them creates problems:
- If a delta is lost/skipped, embedded events would be lost permanently
- Delta compression doesn't apply to events (they're not "fields that changed")
- Keyframe semantics become ambiguous (which events to include?)

The `SimEventBatch` message contains a sequence number and the events drained from `SimEventLog` for that tick. Clients track the last-seen sequence number to detect gaps. On keyframe resync, the client resets its event sequence counter.

### Event Reliability — Two-Tier Model

SimEventLog events fall into two categories with different reliability requirements:

**Tier 1 — Cosmetic events (best-effort delivery):**
- `PlaySound` — sound effects
- `Notification` — floating text, HUD messages
- `SetPalette` — palette changes (freeze effect). With `current_palette_id` in the snapshot, a missed event self-corrects on the next keyframe (~5 seconds).
- `RequestRedraw` — force screen refresh
- Missing a cosmetic event is tolerable (a skipped sound, a missed floating number). These are sent in the `SimEventBatch` message alongside each delta/keyframe. Clients track sequence numbers; gaps are logged but not fatal.

**`DamageTile` — moved to simulation layer (not an event):**
`screen::damage_tile()` at `screen.cpp:1258-1286` **mutates `world_.grid.data[]`** (e.g., `PIX_GRASS1` -> `PIX_GRASS1_DAMAGED`). This is authoritative world state, not a cosmetic effect. If a client misses this event, its grid diverges from the server's. Fix: move tile damage logic into `GameWorld::tick()` (or a helper called from gameplay code) so the grid mutation happens server-side as part of the simulation. The `DamageTile` event is then removed from `EventKind` — clients receive the updated grid state via snapshots. See [Phase 6](phase-06-snapshot-capture.md) for grid snapshot details.

**Tier 2 — Game-flow events (reliable delivery):**
- `EndGame` — triggers `sync_save_data_from_world()` + `endgame()` UI sequence (`screen.cpp:951-955`)
- `SetEnd` — defined in `EventKind` enum but **has zero push sites in gameplay code**; appears to be vestigial. Kept in enum for forward compatibility but not expected to fire. If needed in the future, add push sites at that time.
- `RequestExitConfirmation` — triggers server-side sim pause + broadcast prompt (see Phase 14)
- `WithdrawToLevel` — triggers level withdrawal transition (`screen.cpp:966-968`)
- `ScoreChange` — triggers score UI refresh (`screen.cpp:970-973`; score data itself is already in snapshot via `m_score[4]`)

Game-flow events carry **side effects beyond state** — they trigger UI sequences, blocking prompts, and save data syncs that aren't captured by snapshot flags alone. These are sent in a separate **`GameFlowEventBatch`** message with reliable, ordered delivery (TCP/WebSocket guarantees this; for in-process transport it's automatic). The client dispatches game-flow events through a dedicated `screen::dispatch_game_flow_events()` method (see [Phase 15](phase-15-split-screen-act.md) for the `screen::act()` split).

All game-flow-critical **state** (level completion, player death, game end) MUST also be derivable from `WorldSnapshot` flags (`level_done`, `game_ended`, `dead`, etc.) as a consistency guarantee. The events trigger the UI transitions; the snapshot flags are the source of truth. If a client reconnects and missed events, the snapshot flags let it recover to the correct state (though it may miss the transition animation).

### Tier 1 (Cosmetic) Push Sites — ~61 Call Sites

- **PlaySound** (`emit_sound`) — 23 sites: `effect_family_bomb.cpp:21`, `effect_family_chain.cpp:45`, `family_archmage.cpp:203,244`, `family_cleric.cpp:123,181,235`, `family_druid.cpp:144`, `family_mage.cpp:153,252`, `family_orc.cpp:66`, `family_soldier.cpp:40,98`, `treasure_family_consumables.cpp:30`, `treasure_family_valuables.cpp:43,55,88`, `weapon_family_projectiles.cpp:23`, `walker.cpp:401,453,1367`, `walker_combat.cpp:367,369`
- **Notification** (`emit_notification` / `push_notification`) — 35 sites: `family_archmage.cpp:160,176,192,194,285,459`, `family_cleric.cpp:122,137,166,178,220,232`, `family_druid.cpp:143`, `family_mage.cpp:106,123,141,143,206`, `family_orc.cpp:90`, `family_soldier.cpp:100`, `family_thief.cpp:110,160`, `treasure_family_consumables.cpp:39`, `treasure_family_valuables.cpp:87`, `game_world.cpp:871,910`, `obmap.cpp:314`, `stats.cpp:531`, `walker_combat.cpp:329,334,352`, `walker_specials.cpp:104`, `weap.cpp:89,125,133`
- **SetPalette** — 2 sites: `family_mage.cpp:198`, `game_world.cpp:878`
- **RequestRedraw** — 1 site: `family_mage.cpp:207`

### Tier 2 (Game-Flow) Push Sites — 6 Call Sites

- **EndGame** (1): `walker.cpp:1335`
- **ScoreChange** (2): `walker_combat.cpp:83`, `treasure_family_valuables.cpp:32`
- **RequestExitConfirmation** (2): `treasure_family_navigation.cpp:71,91`
- **WithdrawToLevel** (1): `treasure_family_navigation.cpp:88`
- **DamageTile** (1): `effect_family_bomb.cpp:42` — migrated to simulation layer in Phase 6, removed from events
- **SetEnd** (0): zero push sites (vestigial)

## Delta Compression Support

**For delta compression ([Phase 8](phase-08-serialization-delta.md)):** `EntitySnapshot` includes a `uint64_t dirty_mask[2]` bitmask (128 bits). With 19 SimEntity fields + 44 walker fields + 22 stats fields + 1 weap field = **86 serializable fields**, a single `uint64_t` (64 bits) is insufficient. Two `uint64_t` fields provide 128 bits — comfortable headroom for the current 86 fields plus future additions. When sending deltas, only fields whose corresponding bit is set are included in the wire format. Full keyframes set all bits. This bitmask is part of the struct definition from the start, even though delta serialization is implemented in Phase 8.

## Constexpr Field Table

86 fields with manual bit indices, duplicated across `EntitySnapshot` struct definition, `capture_snapshot()`, `apply_snapshot()`, and serialize/deserialize — that's 4 places per field (`compute_delta()` is eliminated by setter-based dirty tracking — see Phase 8). Missing one = silent desync bug. Use a `constexpr` field descriptor table to define fields once and reference everywhere:

```cpp
// include/openglad/gameplay/snapshot_fields.h

#include <cstddef>
#include <cstdint>

struct FieldDesc {
    uint8_t bit_index;       // fixed position in dirty_mask (0-127)
    uint8_t size;            // sizeof(field_type)
    uint16_t snap_offset;    // offsetof(EntitySnapshot, field_name)
};

// --- All 86 fields, defined once ---
inline constexpr FieldDesc SNAP_FIELDS[] = {
    // SimEntity fields (0-18)
    {0,  sizeof(uint32_t), offsetof(EntitySnapshot, entity_id)},
    {1,  sizeof(int16_t),  offsetof(EntitySnapshot, xpos)},
    {2,  sizeof(int16_t),  offsetof(EntitySnapshot, ypos)},
    // ... etc for all 86 fields
};

inline constexpr size_t SNAP_FIELD_COUNT = std::size(SNAP_FIELDS);
static_assert(SNAP_FIELD_COUNT == 86, "Field count drift -- update snapshot_fields.h");
static_assert(std::is_trivially_copyable_v<EntitySnapshot>,
              "EntitySnapshot must be trivially copyable for memcpy-based serialization");
```

**`trivially_copyable` guard:** The `reinterpret_cast<uint8_t*>` serialization (Phase 8) requires `EntitySnapshot` to be trivially copyable. If anyone adds a `std::string`, `std::vector`, or other non-trivial member, the serialization silently breaks (reads garbage or crashes). The `static_assert` catches this at compile time. `EntitySnapshot` should contain only scalar types and fixed-size arrays.

The `bit_index` values in `SNAP_FIELDS` reference the same constants defined in `dirty_field_bits.h` (Phase 2). This ensures the field table and the `mark_dirty()` calls use identical bit assignments.

This table drives generic loops for capture, apply, and serialization. (`compute_delta()` is eliminated — see Phase 8.) Adding a field means adding one entry to `SNAP_FIELDS`, one constant to `dirty_field_bits.h`, instrumenting mutation sites with `mark_dirty()`, and updating both static asserts. Fields with special handling (e.g., `do_bounce` needing a `weap*` downcast, `regen_delay_` needing an accessor) are excluded from the generic table and handled manually alongside it.

The same table also drives manual binary serialization (Phase 8) — a generic `serialize_fields(buf, snap, dirty_mask)` loop writes each set field as raw bytes in field-table order. No external serialization library needed.

Compared to X-macros (the original plan), `constexpr` field descriptors are:
- Debuggable (real data in a real array, visible in debuggers)
- IDE-friendly (no preprocessor expansion needed for autocomplete/go-to-definition)
- Type-safe (no macro text substitution surprises)
- Slightly more verbose per field, but the table is write-once

**Verify:** Compiles. Can construct and populate manually. Static assert on field count to catch accidental additions.
