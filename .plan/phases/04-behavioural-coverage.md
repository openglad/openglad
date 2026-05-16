# Phase 04 — Behavioural coverage scenarios (weapons, treasures, FX, generators, events, specials)

**Phase Name**: Replace blob-spawn coverage with per-entity behavioural
scenarios.

**Implement Phase ID**: `04-behavioural-coverage`

## Preexisting Inputs

- `.plan/parity-honest-audit.md` (coverage-gap inventory in §(f))
- `.plan/parity-coverage-manifest.md`
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_table.h`
- `tests/parity/parity_runner.cpp` (extended only if a new input pattern is required)
- `tests/parity/scenario_runtime.cpp` (per-spawn `stats_level`/`magicpoints` already supported)
- `tests/parity/fact_predicate.{h,cpp}` (no new predicate kinds; existing 16 suffice)
- `tests/parity/test_parity_coverage_gate.cpp`
- `tests/parity/test_parity_scenarios.cpp` (one new `OG_PARITY_TEST(id)` per new scenario)
- `tests/parity/state_dump.{h,cpp}` (schema-v1 unchanged — read-only)
- `tests/parity/golden/*.json` (existing 39 untouched; Phase 5 captures new ones)
- `../openglad-master/tools/parity_scenario_table.h` (mirror; byte-equal contract)
- `scripts/parity/lint_scenario_facts.py`

## New Outputs

Concrete new scenarios with binding-predicate facts:

### Treasure-pickup scenarios

One per treasure family except `FAMILY_EXIT` (already exercised) and
`FAMILY_STAIN` (passive blood splash):

- `treasure_gold_bar_pickup_scen99`, `treasure_silver_bar_pickup_scen99`,
  `treasure_drumstick_pickup_scen99`, `treasure_magic_potion_pickup_scen99`,
  `treasure_invis_potion_pickup_scen99`, `treasure_invulnerable_potion_pickup_scen99`,
  `treasure_flight_potion_pickup_scen99`, `treasure_teleporter_pickup_scen99`,
  `treasure_life_gem_pickup_scen99`, `treasure_key_pickup_scen99`,
  `treasure_speed_potion_pickup_scen99`,
  `treasure_stain_observation_scen99` (passive — soldier walks over STAIN, treasure stays in oblist).

Spawn pattern: lone soldier on team 0 at `(96, 120)`; treasure at
`(160, 120)` via `kOrderTreasure`. Script `K_RIGHT` for ticks 1..20.

Predicates per row (all required):
- `TickReached(150)`
- `WalkerPositionMoved(FAMILY_SOLDIER, ≥160, 120)`
- `TreasureFamilyRemovedFromOblist(FAMILY_<TREASURE>)` (except STAIN)
- `EventKindAtLeast(score_change, 1)` for value-bearing treasures
- `EventKindAtLeast(play_sound, 2)` for audible pickups
- For HP-bearing treasures: `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, mn, mx)` with master-pinned bounds.
- For stat-bearing treasures (magic, speed, invis, invulnerable, flight): primary predicate is **downstream emission** (post-pickup cast or attack succeeds only because pickup happened). `StatDeltaOnPickup` may be present but never the sole gating predicate (returns `indeterminate` outside hp/max_hp).
- For `FAMILY_KEY`: `WalkerKeysApplied(FAMILY_SOLDIER, mask)`.
- For `FAMILY_TELEPORTER`: `WalkerPositionMoved` with exact post-warp coordinate.
- A `discriminating_mutation` whose subject is the treasure's pickup hook in `src/gameplay/families/treasure_family_*.cpp`. Mutation neuters the pickup; canary asserts ≥1 predicate flip.

### Weapon-emission scenarios

One per weapon family not naturally emitted by existing arenas:

- `weapon_knife_emission_scen99`, `weapon_arrow_emission_scen99`,
  `weapon_fireball_emission_scen99`, `weapon_tree_emission_scen99`,
  `weapon_meteor_emission_scen99`, `weapon_sprinkle_emission_scen99`,
  `weapon_bone_emission_scen99`, `weapon_blood_emission_scen99`,
  `weapon_blob_emission_scen99`, `weapon_fire_arrow_emission_scen99`,
  `weapon_lightning_emission_scen99`, `weapon_glow_emission_scen99`,
  `weapon_wave_emission_scen99`, `weapon_wave2_emission_scen99`,
  `weapon_wave3_emission_scen99`, `weapon_circle_protection_emission_scen99`,
  `weapon_hammer_emission_scen99`, `weapon_door_emission_scen99`,
  `weapon_boulder_emission_scen99`. Add `weapon_rock_emission_scen99` if ROCK is not naturally emitted.

Spawn: wielder on team 0 at `(120, 120)`, target on team 1 at
`(180, 120)`. Use `set_default_weapon` / `set_current_weapon` in
`SpawnSpec` to force wielder onto target weapon family.

Predicates per row:
- `TickReached(150)`
- `WeaponFamilyEmitted(FAMILY_<WEAPON>)` (primary; searches `dump.weapons[]`).
- `EffectFamilyCount(FAMILY_HIT, ≥1, ≤8, source=FAMILY_<wielder>)`.
- Discriminating mutation in weapon family's `act()` that suppresses emission or zeros damage.

### Effect-family scenarios

- `effect_expand_emission_scen99`, `effect_ghost_scare_emission_scen99`,
  `effect_explosion_emission_scen99`, `effect_flash_emission_scen99`,
  `effect_magic_shield_emission_scen99`, `effect_knife_back_emission_scen99`,
  `effect_boomerang_emission_scen99`, `effect_cloud_emission_scen99`,
  `effect_marker_emission_scen99`, `effect_door_open_emission_scen99`,
  `effect_hit_emission_scen99`.

Predicates: `TickReached(<budget>)`,
`EffectFamilyCount(FAMILY_<EFFECT>, mn, mx, source=FAMILY_<source>)`
with `mn == mx` (exact count from master golden after Phase 5).

### Generator scenarios

- `generator_tent_emission_scen99`, `generator_tower_emission_scen99`,
  `generator_bones_emission_scen99`, `generator_treehouse_emission_scen99`.

Spawn: just the generator on team 1 at `(120, 120)`.
`tick_budget = 300`, `fresh_arena = true`.

Predicates:
- `TickReached(300)`
- `WalkerFamilyCount(FAMILY_<SPAWNED>, mn, mx)` with `mn ≥ 1` and `mx ≤ 6`.

### Event-kind scenarios

- `event_notification_emission_scen99` — trigger death-message path (e.g. MAGE DIED).
- `event_set_palette_emission_scen99` — palette change on level start or palette-changing cast; fallback via level-transition palette-set in `glad.cpp` triggered by EXIT treasure pickup.
- `event_request_redraw_emission_scen99` — score change triggers HUD redraw; assert `EventKindAtLeast(request_redraw, 1)`.
- `event_end_game_emission_scen99` — last-player-dies path; spawn one player, three enemies, no input; assert `EndGame` at game-end tick.
- `event_set_end_emission_scen99` — `level_done == 1` plus `EventKindExactly(set_end, 1)` via exit-trigger arena.

### Per-family special-cast scenarios

For each `kRequiredSpecials (family, idx)` pair not already isolated by
a per-slot arena: `special_<family>_<idx>_scen99`. Each scenario:
- `stats_level` ≥ `(idx - 1) * 3 + 1` (cycle gate at `sim_input_handler.cpp:218`).
- `magicpoints` ≥ `special_cost(idx)` (firing gate at `living.cpp:532-533`).
- Inputs: `K_SPECIAL_SWITCH` × `(idx - 1)`, then `K_SPECIAL` once.
- Exercises bit: exactly `Special_<family>_<idx>`.
- Predicates: at least one of `WeaponFamilyEmitted`, `EffectFamilyCount`,
  `WalkerFamilyCount(<summoned>, 1, n)`, `EventKindExactly`,
  `WalkerPositionMoved` (teleport/blink), `WalkerHpRangeAtFinalTick`
  (heal/drain).

### Gate & manifest updates

- Updated `tests/parity/test_parity_coverage_gate.cpp` with new gate cases:
  `Parity.behavioural_coverage_gate_weapons`,
  `Parity.behavioural_coverage_gate_treasures`,
  `Parity.behavioural_coverage_gate_effects`,
  `Parity.behavioural_coverage_gate_generators`,
  `Parity.behavioural_coverage_gate_event_kinds`,
  `Parity.behavioural_coverage_gate` (umbrella). Bodies scan
  `kScenarios[].expected_facts` and assert each required family / kind
  appears as `arg0` of at least one matching predicate.
- Updated `tests/parity/scenario_table.h` registering every new row and
  its `kFacts_*` / `kMut_*` constants.
- Mirror update of `../openglad-master/tools/parity_scenario_table.h`
  (byte-for-byte) and rebuild of `parity_dump_master`.
- Updated `.plan/parity-coverage-manifest.md` — flip every `(none yet)`
  cell to the new scenario id; add a "behavioural predicate" column.
- Updated `.plan/parity-harness-design.md` — append "Phase 04 redo:
  behavioural coverage" section documenting new gate cases.
- `kFamilySpawns_golem_with_nonliving_targets` may stay; `04b` enforces
  no required family loses coverage if blob is removed.

## File Changes

- Modify `tests/parity/scenario_table.h`.
- Modify `tests/parity/test_parity_scenarios.cpp` (one `OG_PARITY_TEST` per new id).
- Modify `tests/parity/test_parity_coverage_gate.cpp`.
- Modify `tests/parity/parity_runner.cpp` and `tests/parity/scenario_runtime.cpp` ONLY if a new input pattern is required.
- **Do NOT** modify `tests/parity/state_dump.{h,cpp}` (schema-v1 freeze).
- Mirror to `../openglad-master/tools/parity_scenario_table.h` and recompile `parity_dump_master`.
- Modify `.plan/parity-coverage-manifest.md` and `.plan/parity-harness-design.md`.
- Branch commit: `parity-finish-2: phase 04 — behavioural coverage scenarios`.
- Master commit: `parity-companion: phase 04 — mirror scenario_table.h (<branch sha or short>)`.

## Implementation Details

- Per-slot special scenarios reuse the cycle/fire pattern from
  `kInputsFamilySpecialCoverage` but constrain the cycle to the target slot.
- Generator scenarios use `tick_budget = 300` because TENT/TOWER/etc.
  emit at ~150-tick intervals.
- Behavioural gate is `TEST(Parity, behavioural_coverage_gate_*)` in
  `test_parity_coverage_gate.cpp`; body scans
  `kScenarios[].expected_facts` arrays at runtime and asserts each
  required family/kind appears as `arg0` of at least one matching
  predicate.
- No changes to `fact_predicate.cpp` needed.

## Verification Phases

- **`04a-check-behavioural-gate`** (`check`, `bounce_target: 04-behavioural-coverage`):
  Purpose: confirm new behavioural gates exist and pass.
  Commands:
  - `cmake --build --preset ci-test --target og_test_parity`.
  - `build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage_gate*'`
    — each named gate passes:
    `Parity.behavioural_coverage_gate_weapons`,
    `Parity.behavioural_coverage_gate_treasures`,
    `Parity.behavioural_coverage_gate_effects`,
    `Parity.behavioural_coverage_gate_generators`,
    `Parity.behavioural_coverage_gate_event_kinds`,
    `Parity.behavioural_coverage_gate`.

- **`04b-check-no-blob-scenario-needed`** (`check`, `bounce_target: 04-behavioural-coverage`):
  Purpose: ensure required families remain covered after the synthetic
  blob spawn is name-grepped out.
  Commands:
  - `python3 - <<'PY'` reads `tests/parity/scenario_table.h`, removes
    references to `kFamilySpawns_golem_with_nonliving_targets`
    (textually), and asserts every required family is still covered by
    another scenario's `expected_facts[]`. Failure exits non-zero with
    the missing family listed.

- **`04c-check-gtests-pass`** (`check`, `bounce_target: 04-behavioural-coverage`):
  Purpose: full parity binary green and structural gates intact.
  Commands:
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity --gtest_brief=1` — no failures.
  - `build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'`
    — all structural gates pass.
  - `sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
    — branch and companion mirror SHA-1s match.
  - `git -C ../openglad-master log -1 --name-status` lists the mirror update.

## Success Criteria

- Every `kRequiredWeaponFamilies`, `kRequiredTreasureFamilies`,
  `kRequiredEffectFamilies`, `kRequiredEventKinds`, generator family,
  and `kRequiredSpecials` pair appears as `arg0` of at least one
  behavioural predicate in some scenario's `expected_facts[]`.
- All six new behavioural gates pass.
- Existing structural gates still pass.
- Branch and companion table SHA-1s remain equal after the mirror.

## Git Commit Requirement

Two-worktree phase. The implementer MUST:

- `git add` modified branch files and `git commit` with
  `parity-finish-2: phase 04 — behavioural coverage scenarios`
  before yielding.
- `git -C ../openglad-master add tools/parity_scenario_table.h` and
  `git -C ../openglad-master commit -m "parity-companion: phase 04 — mirror scenario_table.h (<branch sha or short>)"`
  before yielding.

Check phases verify both HEADs via `git log -1 --name-status` and
`git -C ../openglad-master log -1 --name-status`.
