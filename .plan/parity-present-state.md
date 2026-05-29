# Parity Test Suite — Present State

Final metrics for the parity hardening effort, refreshed after **Coverage
Pass 2** (gap-fill) on branch `wip/networking`. Coverage Pass 2 added 22
SemanticParity scenarios on top of the 134 captured at the end of Phase 11,
bringing the suite to **156 scenarios**.

## Scenario Inventory

- **Total scenarios in `kScenarios[]`:** 156
- **`CompareMode::SemanticParity`:** 154
- **`CompareMode::Invariant`:** 2 (`snapshot_dirty_bits_scen9301`,
  `smoke_empty_scen99`)
- **`CompareMode::ByteEqual`:** 0
- **Golden JSON files in `tests/parity/golden/`:** 155
  (every scenario except `smoke_empty_scen99`, which is Invariant and
  carries no master golden by design)
- **Missing master golden among SemanticParity scenarios:** 0

## GoogleTest Counts (CTest)

- **CTest entries (`ctest --preset ci-test`):** 37 — **all passing**.
- **Total GoogleTest cases across integration binaries:** 1883.
- **Total GoogleTest cases across unit binaries:** 530.
- **Per-scenario parity cases in `og_test_parity`:** 182 (156 scenarios +
  26 additional fixtures including the 7 depth/quality gate tests and the
  behavioural/coverage and dumper-determinism checks). All 182 pass.

Final run:
```
100% tests passed, 0 tests failed out of 37
```

## Trivially-True Predicate Floor

- `EventKindAtLeast(*, 0)` occurrences in `scenario_table.h`: **0**
  (verified by `grep -c 'EventKindAtLeast.*,\s*0)' tests/parity/scenario_table.h`).

Every `EventKindAtLeast` predicate has a floor strictly greater than zero,
so a regression that silenced the corresponding event would fail the gate
rather than pass vacuously.

## Predicate Inventory

- Total `FactPredicate` arrays bound to scenarios: 154 (one per
  SemanticParity scenario).
- Total predicates across all arrays: 738.
- Average predicate depth: **4.79** non-empty predicates per scenario.

### Per-Category Predicate Depth

| Category          | Scenarios | Predicates | Avg Depth |
|-------------------|----------:|-----------:|----------:|
| special           |        42 |        176 |      4.19 |
| weapon            |        23 |        116 |      5.04 |
| family            |        21 |        126 |      6.00 |
| effect            |        19 |         94 |      4.95 |
| treasure          |        13 |         52 |      4.00 |
| generator         |         5 |         21 |      4.20 |
| event             |         5 |         20 |      4.00 |
| input             |         4 |         20 |      5.00 |
| status_timer      |         4 |         20 |      5.00 |
| summon            |         3 |         15 |      5.00 |
| level_transition  |         2 |         11 |      5.50 |
| midcombat         |         2 |         10 |      5.00 |
| smoke             |         2 |         12 |      6.00 |
| ai                |         1 |          5 |      5.00 |
| combat            |         1 |          5 |      5.00 |
| multiplayer       |         1 |          5 |      5.00 |
| scoring           |         1 |          5 |      5.00 |
| save              |         1 |          5 |      5.00 |
| tick              |         1 |          5 |      5.00 |
| rng               |         1 |          5 |      5.00 |
| scripted          |         1 |          6 |      6.00 |
| coverage          |         1 |          4 |      4.00 |
| **Total**         |   **154** |    **738** |  **4.79** |

Every SemanticParity scenario carries at least 4 predicates (3
non-`TickReached`), satisfying the depth gate enforced by
`test_parity_coverage_gate.cpp`.

## Structural vs Behavioral Coverage

- **Structural** coverage (walker counts, family presence, position,
  alive-status): every scenario has at least one structural predicate.
- **Behavioral** consequence coverage: every weapon emission, effect
  emission, special ability, treasure pickup, generator, status/timer,
  input, multiplayer, level-transition, and midcombat scenario carries at
  least one consequence predicate (`WalkerHpRangeAtFinalTick`,
  `WalkerDiedByFinal`, `WalkerPositionMoved`, `WalkerAliveAtFinal`,
  `WeaponFamilyEmitted`, `EffectFamilyCount`, `ScoreDelta`,
  `TreasurePickup`, `TreasureFamilyRemovedFromOblist`,
  `TreasureFamilyOfOrderRemovedFromOblist`, `LevelDoneEquals`, or a
  cross-family `WalkerFamilyCount` distinct from the wielder).
- **Final tick gating:** 21 walker-family scenarios + each special and
  weapon scenario assert observable state at the final tick via either
  HP-range or death predicates.
- The 7 depth/quality gates in `test_parity_coverage_gate.cpp` enforce that
  this coverage cannot regress without a failing test.

## Widened Predicates and Justification Labels

Predicates whose ranges are widened above a single value carry an
explanatory `label` so the gate cannot be silently relaxed. Two label
prefixes are used and accepted by the depth gate:

| Justification prefix | Occurrences |
|----------------------|------------:|
| `intended_diff:`     |          57 |
| `rng_drift:`         |          46 |
| **Total labelled**   |     **103** |

Every widened range in `scenario_table.h` carries one of these prefixes
followed by a `commit <sha>` reference identifying the divergence;
see `tests/parity/test_parity_coverage_gate.cpp` for the enforcing
gate (`predicate_depth_gate_no_trivially_wide_ranges`) that fails the build
if a widened range lacks a justification.

## Master-Golden Missing Policy

`tests/parity/test_parity_scenarios.cpp:108` has been converted from
`GTEST_SKIP` to `ADD_FAILURE`:

```cpp
ADD_FAILURE() << "master golden missing for " << spec.id
              << " (expected at " << path.string()
              << ") — Phase 04+ recapture will populate";
```

A missing master golden for a SemanticParity scenario is now a hard test
failure rather than a silent skip. The `ByteEqual` and
missing-scenario-in-`kScenarios` branches retain `GTEST_SKIP` by design.

Verification:

```
$ grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp
0
$ grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp
1
```

## Final Verification Gates

| Gate                                                                   | Value |
|------------------------------------------------------------------------|------:|
| `ctest --preset ci-test` failed tests                                  |     0 |
| Total scenarios in `kScenarios[]`                                      |   156 |
| `grep -c 'GTEST_SKIP() << "master golden missing'`                     |     0 |
| `grep -c 'ADD_FAILURE() << "master golden missing'`                    |     1 |
| `grep -c 'EventKindAtLeast.*,\s*0)' tests/parity/scenario_table.h`     |     0 |
| SemanticParity scenarios with missing master golden                    |     0 |
| Goldens in `tests/parity/golden/`                                      |   155 |
| Mirror `../openglad-master/tools/parity_scenario_table.h` drift        |     0 |
