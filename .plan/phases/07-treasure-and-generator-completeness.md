# Phase 07 — Treasure and generator family completeness

## Phase Name
Every treasure family bound by a removal predicate; every generator family covered by a spawn-emit row.

## Implement Phase ID
`07-treasure-and-generator-completeness`

## Preexisting Inputs
- `tests/parity/coverage_targets.h::kRequiredTreasureFamilies` (13), `::kRequiredGeneratorFamilies` (4).
- Treasure rows from phase 03 (now using `TreasureFamilyOfOrderRemovedFromOblist`).
- Existing `generator_*_emission_scen99` rows (4).
- `tests/parity/test_parity_coverage_gate.cpp` (`behavioural_coverage_gate_treasures`).
- `tests/parity/scenario_table.h`.
- `tests/parity/scenario_facts_generated.json`.
- `tests/parity/golden/*.json`.
- `../openglad-master/build/ci-test/parity_dump_master`.
- `scripts/parity/capture_master_golden.sh`.
- `scripts/parity/check_coverage_manifest.py`.
- `.plan/parity-coverage-manifest.md`.

## New Outputs
- For every treasure family in `kRequiredTreasureFamilies` that does not yet have a `Treasure*RemovedFromOblist` (legacy OR new kind) predicate, add the predicate to its row (or a new dedicated row if no sensible existing row is available). For STAIN (`init_ignore=true`), add `WalkerFamilyCount(FAMILY_STAIN, 1, 1)` (under per-Order resolution) as the RNG-insensitive pin alongside the new `TreasureFamilyOfOrderRemovedFromOblist` row.
- For every generator family, ensure a row spawns one and runs long enough that it emits its enemy or treasure via `WalkerFamilyCount(<spawned-child>, min, max)` AND `EventKindAtLeast("play_sound", N)`.
- Extend `behavioural_coverage_gate_treasures` so its required-bound set is `{ legacy_TreasureFamilyRemovedFromOblist_args | per_Order_TreasureFamilyOfOrderRemovedFromOblist_args_with_kOrderTreasure }`. Gate fails if any required treasure family is unbound.
- Mirror updated.
- New/replacement goldens for affected rows.
- `.plan/parity-coverage-manifest.md` updated for treasure + generator sections.

## File Changes
- `tests/parity/scenario_table.h`.
- `tests/parity/test_parity_coverage_gate.cpp`.
- `tests/parity/scenario_facts_generated.json` (regenerate).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `tests/parity/golden/treasure_*.json`, `tests/parity/golden/generator_*.json` (capture).
- `.plan/parity-coverage-manifest.md`.

## Implementation Details
- Enumerate each treasure family id and resolve which existing row binds it; for unbound ids, add a binding predicate to the most natural existing row (e.g. `treasure_<name>_pickup_scen99`) or create a small new row.
- Generator emission rows: tick budget must be large enough that the generator's emission roll (RNG-dependent in master) fires at least once. Use `EventKindAtLeast` rather than `WalkerFamilyCount` exact if emit count varies.

## Verification Phases

### `07a-check-every-treasure-and-generator-bound`
- Type: `check`
- Bounce target: `07-treasure-and-generator-completeness`
- Purpose: Every required treasure and generator family is bound by an appropriate predicate.
- Commands:
  - In-line `python3 -c '<...>'` against `tests/parity/scenario_facts_generated.json`: loop `kRequiredTreasureFamilies` — for each treasure id, assert at least one row has a predicate of one of the two `Treasure*RemovedFromOblist` kinds with `arg0 == <id>`. Loop `kRequiredGeneratorFamilies` — for each generator family, assert a `WalkerFamilyCount(<that family>, ...)` predicate on some row.

### `07b-check-behavioural-gates-green`
- Type: `check`
- Bounce target: `07-treasure-and-generator-completeness`
- Purpose: Behavioural gates, treasure, and generator tests all pass.
- Commands:
  - `build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage_gate*:Parity.treasure_*:Parity.generator_*' 2>&1 | tee /tmp/p07b.out`. `grep -cE '^\[  FAILED  \]' /tmp/p07b.out` equals `0`. `grep -cE '^\[  SKIPPED \]' /tmp/p07b.out` equals `0`.

### `07c-check-no-suite-regression`
- Type: `check`
- Bounce target: `07-treasure-and-generator-completeness`
- Purpose: Full suite green; mirror still byte-equal.
- Commands:
  - `build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p07c.out`. `grep -cE '^\[  FAILED  \]' /tmp/p07c.out` equals `0`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

## Success Criteria
- All three check phases (`07a`, `07b`, `07c`) pass.
- Every required treasure id is bound by a `Treasure*RemovedFromOblist` predicate.
- Every required generator family has a `WalkerFamilyCount` predicate covering its spawned child.
- Behavioural gates + treasure + generator rows all green, no skips.
- Mirror byte-equal.

## Git Commit Requirement
Commit BOTH worktrees before yielding.

Companion (in `../openglad-master`):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 07 — mirror treasure and generator rows"
```

Branch:
```
git add tests/parity/scenario_table.h \
        tests/parity/test_parity_coverage_gate.cpp \
        tests/parity/scenario_facts_generated.json \
        tests/parity/golden/ \
        .plan/parity-coverage-manifest.md
git commit -m "parity-cov: phase 07 — treasure and generator completeness"
```
