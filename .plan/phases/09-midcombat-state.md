# Phase 09 — Mid-combat state

## Phase Name
Mid-combat walker-state scenarios

## Implement Phase ID
`impl-midcombat-state`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp` (wiring,
  **do not modify**), `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/fact_predicate.h`, `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`.
- `src/gameplay/walker_combat.cpp:178-206` (central combat-damage HP write at line 189).
- `src/gameplay/families/treasure_family_consumables.cpp` (drumstick heal at
  line 25, magic_potion).
- `src/resources/save_data.cpp` (field defs; the harness inspects the live
  oblist, not `save_game`).

## New Outputs
2 `ScenarioSpec` rows, 2 macros, 2 `kMut_*`, 2 goldens, mirror update, rebuilt dump:
- `midcombat_partial_hp_scen99`
- `consumable_inventory_state_scen99`

## File Changes
Append table rows + macros; 2 new goldens; mirror the table; rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes
(both scenarios in `kScenarios[]`, macros present, goldens on disk, named tests
green, mirror in sync). If all hold, ensure the two commits landed and yield
immediately — do not redo work. Otherwise implement only the missing pieces.

- **`midcombat_partial_hp_scen99`**: `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,3,0}`,
  2 archers team 1 at `(60,120)`, `(180,120)`; inputs `{5,0,K_FIRE},{40,0,K_NONE}`;
  tick 80. Facts: `TickReached(80)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4000, 11000, "consequence: the two archers deal mid-combat damage that lands the soldier in the mid-HP band; mutation zeros the central combat-damage write so HP stays above the upper bound (7000-cent span, label_exempted)")`,
  `WalkerOfTeamAlive(0,1,1)`, `EventKindAtLeast(1,5)`.
  Mutation `walker_combat.cpp:189` (4-space):
  `    target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);` → `... - 0);`.
- **`consumable_inventory_state_scen99`**: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0,3,0}`,
  `{FAMILY_DRUMSTICK,0,kOrderTreasure,128,120,0,0}`,
  `{FAMILY_MAGIC_POTION,0,kOrderTreasure,160,120,0,0}`; inputs
  `{1,0,K_RIGHT},{40,0,K_NONE}`; tick 150. Facts: `TickReached(150)`,
  `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_DRUMSTICK, kOrderTreasure)`,
  `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_MAGIC_POTION, kOrderTreasure)`,
  `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 9000, 13000, "consequence: eating the drumstick boosts the soldier's HP into the upper band; mutation no-ops the heal so HP drops below the lower bound (label_exempted)")`.
  Mutation `treasure_family_consumables.cpp:25` (4-space):
  `    eater->stats()->set_hitpoints(eater->stats()->hitpoints() + amount);` → `... + 0);`.

Empirical numerics (`EventKindAtLeast` floors, HP ranges, tick budgets) are tunable
starting points — adjust to runtime so each unmutated scenario passes, every floor
is `> 0`, each mutation flips ≥1 predicate, every widened range carries a label,
and each scenario keeps ≥3 non-`TickReached` predicates incl. ≥1 `consequence:`-labelled
one. The 7000-cent HP range carries `intended_diff:`/`consequence:` (label_exempted).

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild master
dump → `capture_master_golden.sh midcombat_partial_hp_scen99 consumable_inventory_state_scen99`
→ build+ctest 0 failures; wrap branch/master-divergent predicates in
`branch_only`/`master_only`).

## Verification Phases
- **`check-midcombat-state`** — type `check`, `bounce_target: impl-midcombat-state`.
  Purpose: deterministically confirm the 2 scenarios are present, green, mirrored,
  committed, and depth/consequence-compliant. Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`.
  Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.midcombat_partial_hp_scen99:Parity.consumable_inventory_state_scen99'` → pass.
  - `ls tests/parity/golden/{midcombat_partial_hp,consumable_inventory_state}_scen99.json` → all exist.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*:Parity.predicate_depth_gate_no_trivially_wide_ranges'` → pass
    (the 7000-cent HP range carries an `intended_diff:`/`consequence:` label).
  - **Mechanical depth/consequence count** for each of the 2 new ids — run the
    `awk`/`grep` snippet on each `kFacts_<id>[]` block: require `≥3` non-`TickReached`
    predicates and `grep -q 'consequence:'`. FAIL if any fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- Both scenarios pass; 2 goldens exist; mirror byte-identical.
- The `predicate_depth_gate_no_trivially_wide_ranges` gate and all other gates pass.
- Each scenario has ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h midcombat-state"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/midcombat_partial_hp_scen99.json tests/parity/golden/consumable_inventory_state_scen99.json && git commit -m "parity-cov: midcombat-state scenarios"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
