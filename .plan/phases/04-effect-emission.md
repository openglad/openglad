# Phase 04 — Effect emission breadth

## Phase Name
Effect emission scenarios

## Implement Phase ID
`impl-effect-emission`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp` (wiring,
  **do not modify**), `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/fact_predicate.h`, `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`.
- `src/gameplay/families/family_archmage.cpp:209-251` (HEARTBURST per-foe
  explosion loop 237-250).
- `src/gameplay/families/family_thief.cpp:165-178` (POISON CLOUD slot 4,
  line 169 spawns FAMILY_CLOUD).
- `src/gameplay/families/family_druid.cpp:86-149` (PROTECTION; gated on
  `howmany > 1` friendlies in range 60; line 116 spawns FAMILY_CIRCLE_PROTECTION).
- `src/gameplay/families/weapon_family_animate.cpp:32-43,126`.
- Reuses `kInputsSpecialSlot2`, `kInputsSpecialSlot4` (existing constants — reuse by name).

## New Outputs
3 `ScenarioSpec` rows, 3 macros, 3 `kMut_*`, 3 goldens, mirror update, rebuilt dump:
- `effect_heartburst_multitarget_scen99`
- `effect_poison_cloud_emit_scen99`
- `effect_protection_emit_scen99`

## File Changes
Append table rows + macros; 3 new goldens; mirror the table; rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes
(all 3 scenarios in `kScenarios[]`, macros present, goldens on disk, named tests
green, mirror in sync). If all hold, ensure the two commits landed and yield
immediately — do not redo work. Otherwise implement only the missing pieces.

- **`effect_heartburst_multitarget_scen99`**: archmage
  `{FAMILY_ARCHMAGE,0,kOrderLiving,120,120,0,0,4,300}` + 4 soldiers team 1 at
  x∈{160,190,220,250}, y=120; reuse `kInputsSpecialSlot2`; tick 30. Facts:
  `TickReached(30)`, `WalkerFamilyCount(FAMILY_ARCHMAGE,1,1)`,
  `EffectFamilyCount(FAMILY_EXPLOSION, 4, 12, -1, 0, "consequence: ...")`,
  `WalkerFamilyCount(FAMILY_SOLDIER, 0, 4, "consequence: ...")`, `EventKindAtLeast(1,4)`.
  Mutation `family_archmage.cpp:239` (24-space):
  `newob = summon_entity(self, Order::FX, FAMILY_EXPLOSION);` → `return false;`.
- **`effect_poison_cloud_emit_scen99`**: thief
  `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,10,300}`,
  `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`; reuse `kInputsSpecialSlot4`;
  tick 80. Facts: `TickReached(80)`, `WalkerFamilyCount(FAMILY_THIEF,1,1)`,
  `EffectFamilyCount(FAMILY_CLOUD, 1, 5, -1, 0, "consequence: ...")`,
  `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 15000, "rng_drift: ...")`, `EventKindAtLeast(1,3)`.
  Mutation `family_thief.cpp:169`:
  `newob = summon_entity(self, Order::FX, FAMILY_CLOUD);` → `return false;`.
- **`effect_protection_emit_scen99`**: druid
  `{FAMILY_DRUID,0,kOrderLiving,120,120,0,0,10,300}`, **second team-0 friendly**
  `{FAMILY_SOLDIER,0,kOrderLiving,150,120,0,0}` (required for `howmany > 1`),
  2 archers team 1 at `(60,120)`, `(220,120)`; reuse `kInputsSpecialSlot4`;
  tick 150. Facts: `TickReached(150)`, `WalkerFamilyCount(FAMILY_DRUID,1,1)`,
  `WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION, "consequence: druid slot 4 PROTECTION emits a FAMILY_CIRCLE_PROTECTION weapon when ≥1 friendly is in range; mutation bypasses creation so the emit never fires")`,
  `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`, `EventKindAtLeast(1,4)`.
  Mutation `family_druid.cpp:116` (32-space):
  `alive = summon_entity(newob, Order::Weapon, FAMILY_CIRCLE_PROTECTION);` → `return false;`.

Empirical numerics (`EventKindAtLeast` floors, effect-count/HP ranges, tick budgets)
are tunable starting points — adjust to runtime so each unmutated scenario passes,
every floor is `> 0`, each mutation flips ≥1 predicate, every widened range carries
a label, and each scenario keeps ≥3 non-`TickReached` predicates incl. ≥1
`consequence:`-labelled one.

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild master
dump → `capture_master_golden.sh effect_heartburst_multitarget_scen99 effect_poison_cloud_emit_scen99 effect_protection_emit_scen99`
→ build+ctest 0 failures; wrap branch/master-divergent predicates in
`branch_only`/`master_only`).

## Verification Phases
- **`check-effect-emission`** — type `check`, `bounce_target: impl-effect-emission`.
  Purpose: deterministically confirm the 3 scenarios are present, green, mirrored,
  committed, and depth/consequence-compliant. Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`.
  Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.effect_heartburst_multitarget_scen99:Parity.effect_poison_cloud_emit_scen99:Parity.effect_protection_emit_scen99'` → pass.
  - `ls tests/parity/golden/{effect_heartburst_multitarget,effect_poison_cloud_emit,effect_protection_emit}_scen99.json` → all exist.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - **Mechanical depth/consequence count** for each of the 3 new ids — run the
    `awk`/`grep` snippet on each `kFacts_<id>[]` block: require `≥3` non-`TickReached`
    predicates and `grep -q 'consequence:'`. FAIL if any fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- All 3 scenarios pass; 3 goldens exist; mirror byte-identical.
- All coverage/golden/mutation gates pass.
- Each scenario has ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h effect-emission"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/effect_heartburst_multitarget_scen99.json tests/parity/golden/effect_poison_cloud_emit_scen99.json tests/parity/golden/effect_protection_emit_scen99.json && git commit -m "parity-cov: effect-emission scenarios"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
