# Phase 06 — Residual Coverage Scenarios

## Phase Name
Close all remaining coverage gaps (effects, weapons, treasures,
generators, save round-trip, exit-trigger, event kinds).

## Implement Phase ID
`06-residual-coverage-scenarios`

## Preexisting Inputs
- All outputs of Phases 04 and 05 (walker-family and special-ability
  scenarios + goldens committed).
- `tests/parity/coverage_targets.h` (declares every required-coverage
  array including `kRequiredEffectFamilies[]`,
  `kRequiredWeaponFamilies[]`, `kRequiredTreasureFamilies[]`,
  `kRequiredEventKinds[]`).
- `src/gameplay/effect_family_registry.cpp` (effect family registration).
- `src/resources/save_data.cpp` (for on-disk save coverage).
- `tests/parity/scenario_runtime.{h,cpp}` (already supports
  `Order::WEAPON`, `Order::TREASURE`, and the `SpawnSpec::default_weapon`
  / `SpawnSpec::current_weapon` fields introduced in Phase 02).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `../openglad-master/build/ci-test/parity_dump_master` (rebuilt against
  the SHA in `.plan/parity-coverage-manifest.md` frontmatter).

## New Outputs
- Effect scenarios (~13): `effect_expand`, `effect_ghost_scare`,
  `effect_bomb`, `effect_explosion`, `effect_flash`,
  `effect_magic_shield`, `effect_knife_back`, `effect_boomerang`,
  `effect_cloud`, `effect_marker`, `effect_chain`, `effect_door_open`,
  `effect_hit`. Only the residue not already covered by Phases 04-05.
- Weapon scenarios (~20): one per weapon family
  `FAMILY_KNIFE..FAMILY_BOULDER`. Each scenario's `spawns[]` sets
  `SpawnSpec::default_weapon` and `SpawnSpec::current_weapon` to the
  target weapon family id; the carrier family is chosen so its attack
  path fires the weapon's emit-projectile logic (e.g. `FAMILY_ARCHER`
  for ranged, `FAMILY_SOLDIER` for melee).
- Treasure scenarios (13): every treasure family
  `FAMILY_STAIN(0)..FAMILY_SPEED_POTION(12)` spawned via `spawns[]` and
  collected by a walker driven onto it. Pickup verified by:
  (a) the treasure walker id absent from `dump.walkers[]` at the final
  tick, and
  (b) post-pickup change in collector stats (HP/MP) or
  `dump.score_per_team` (for gold). Coverage gate keys on the
  absent-treasure observation plus the `exercises` bit, not an
  EventKind string.
- Generator scenarios (4): tent / tower / bones / treehouse. Spawn
  generator, tick long enough for a child walker, verify the child in
  `walkers[]`.
- `save_roundtrip_disk_99`: loads scen99, runs 20 ticks, calls
  `level.save()` to write a real `.glad` archive into the PhysFS
  write-dir, opens it, deserialises into a fresh `LevelRuntimeData`,
  dumps, asserts byte-equal to in-memory dump.
- `exit_trigger_real_9302`: scripts the player onto the exit tile of
  scen9302; captures the `level_exited` event with the correct
  `next_level`.
- `rng_reseed_after_load_99`: validates the seed-after-load contract
  from Phase 02. Loads with seed A, re-seeds to B post-load, ticks;
  compared to loading with seed B and not re-seeding, post-tick dumps
  must differ.
- Event-emission scenarios for `EventKind` values not naturally
  emitted by family/special scenarios:
  - `play_sound` (any combat), `notification` (cleric heal or yell),
    `set_palette` (`FAMILY_GHOST` scare), `request_redraw` (any tick),
    `end_game`/`set_end`/`request_exit_confirmation`/`withdraw_to_level`
    (covered by `exit_trigger_real_9302`), `score_change` (any kill in
    a scoring-active arena).
- Mirror entries in `../openglad-master/tools/parity_scenario_table.h`,
  committed on `parity-companion`.
- Goldens for every new scenario captured **in this phase** via the
  rebuilt master companion, schema-validated, committed. Canonical —
  Phase 07 only re-captures into a throwaway directory.
- `scripts/parity/audit_event_coverage.py` — loads every golden,
  unions `events[*].kind`, asserts the union equals the full set in
  `kRequiredEventKinds[]`. Exits non-zero on missing kinds.
- `.plan/parity-coverage-manifest.md` final state — all rows filled.

## File Changes
- Modified: `tests/parity/scenario_table.h`.
- Modified: `../openglad-master/tools/parity_scenario_table.h`.
- New: numerous `tests/parity/golden/*.json` files (effect_*, weapon_*,
  treasure_*, generator_*, event_*, save_roundtrip_*, exit_trigger_*,
  rng_reseed_*).
- New: `scripts/parity/audit_event_coverage.py`.
- Modified: `tests/parity/test_parity_scenarios.cpp` (macro
  invocations).
- Modified: `.plan/parity-coverage-manifest.md`.
- Modified: `.plan/parity-harness-design.md` (final scenario count
  updated; coverage matrix replaced with pointer to the manifest).

## Implementation Details
- `apply_post_load_spawns` already supports `Order::WEAPON` and
  `Order::TREASURE` plus the new weapon-override fields from Phase 02;
  no new spec field is introduced here.
- For weapon coverage, `apply_post_load_spawns` calls
  `walker->set_default_weapon(spawn.default_weapon)` and
  `walker->set_current_weapon(spawn.current_weapon)` (dirty-field
  setters at `include/openglad/gameplay/walker.h:170-171`) only for
  non-zero fields. There is no `walker::weapon_type` member.
- Save round-trip on disk uses the PhysFS write-dir under
  `${CMAKE_BINARY_DIR}/parity-write/` configured by Phase 02's
  bootstrap.
- The `exit_trigger_real_9302` script is generated by reading
  scen9302's map metadata to compute the exit tile position.

## Verification Phases
- **Phase ID**: `06a-check-residual-coverage`
  - **Type**: `check`
  - **Bounce Target**: `06-residual-coverage-scenarios`
  - **Purpose**: Confirm every coverage-gate case (walker families,
    effect families, weapon families, treasure families, generator
    families, specials, event kinds) reports zero uncovered targets.
  - **Commands**:
    ```
    cmake --build --preset ci-test --target og_test_parity
    ./build/ci-test/og_test_parity \
        --gtest_filter='Parity.coverage_gate*'    # exits 0, zero gaps
    ```

- **Phase ID**: `06b-check-event-kind-coverage`
  - **Type**: `check`
  - **Bounce Target**: `06-residual-coverage-scenarios`
  - **Purpose**: Re-derive event-kind coverage by directly grepping
    every golden via the new audit script.
  - **Commands**:
    ```
    python3 scripts/parity/audit_event_coverage.py \
        tests/parity/golden/             # exits 0
    ```

- **Phase ID**: `06c-check-byte-equal-vs-master`
  - **Type**: `check`
  - **Bounce Target**: `06-residual-coverage-scenarios`
  - **Purpose**: Per-scenario `diff -q` between newly committed
    branch goldens and freshly captured master dumps for the residual
    categories.
  - **Commands**:
    ```
    (cd ../openglad-master && cmake --build --preset ci-test \
        --target parity_dump_master)
    for id in $(ls tests/parity/golden/{effect_*,weapon_*,treasure_*,generator_*,event_*,save_roundtrip_*,exit_trigger_*,rng_reseed_*}.json 2>/dev/null \
                  | xargs -n1 basename | sed 's/\.json$//'); do
      ../openglad-master/build/ci-test/parity_dump_master \
          --scenario "$id" --out "/tmp/${id}.json"
      diff -q "tests/parity/golden/${id}.json" "/tmp/${id}.json"
    done
    ./build/ci-test/og_test_parity \
        --gtest_filter='Parity.effect_*:Parity.weapon_*:Parity.treasure_*:Parity.generator_*:Parity.event_*:Parity.save_roundtrip_*:Parity.exit_trigger_*:Parity.rng_reseed_*'
    ```
    All `diff -q` invocations and the test run must exit 0.

## Success Criteria
- All new parity tests pass.
- `Parity.coverage_gate*` reports zero uncovered targets across every
  category.
- `scripts/parity/audit_event_coverage.py` exits 0.
- `.plan/parity-coverage-manifest.md` has every row filled with a
  `covering_scenario_id`.

## Git Commit Requirement
Two commits **before yielding**:
1. Branch-side: `git add` scenario table, test registrations, audit
   script, manifest updates, and every new golden file; `git commit -m
   "parity-redo: phase 06 — residual effect/weapon/treasure/generator/
   event coverage"`.
2. Master-side: `git -C ../openglad-master add
   tools/parity_scenario_table.h` and
   `git -C ../openglad-master commit -m "parity-companion: phase 06 —
   residual coverage mirror"`.
Both worktrees must have clean status at verification time.
