# Phase 03 — Weapon trajectories

## Phase Name
Weapon trajectory scenarios

## Implement Phase ID
`impl-weapon-trajectories`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp` (wiring,
  **do not modify**), `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/fact_predicate.h`, `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`.
- `src/gameplay/families/family_elf.cpp:62-74` (slot 2 BOUNCING ROCKS).
- `src/gameplay/families/family_soldier.cpp:45-52` (slot 2 BOOMERANG).
- `src/gameplay/families/effect_family_shield.cpp:63-134` (boomerang FX).
- `src/gameplay/families/family_barbarian.cpp:23-65` (slot 2 EXPLODING BOULDER,
  line 59 `set_skip_exit(5000)`).
- `src/gameplay/families/weapon_family_projectiles.cpp:14-31` (`projectile_explode_on_death`).
- Reuses `kInputsSpecialSlot2` (existing input constant — reuse by name).
- Coverage-gate widened-range exemption at lines 702-707 / 893-933.

## New Outputs
3 `ScenarioSpec` rows, 3 macros, 3 `kMut_*`, 3 goldens, mirror update, rebuilt dump:
- `weapon_rock_slot2_emit_scen99`
- `weapon_boomerang_return_scen99`
- `weapon_exploding_boulder_scen99`

## File Changes
Append table rows + macros; 3 new goldens; mirror the table; rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes
(all 3 scenarios in `kScenarios[]`, macros present, goldens on disk, named tests
green, mirror in sync). If all hold, ensure the two commits landed and yield
immediately — do not redo work. Otherwise implement only the missing pieces.

- **`weapon_rock_slot2_emit_scen99`**: spawns
  `{FAMILY_ELF,0,kOrderLiving,120,120,0,0,4,300}`,
  `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`; reuse `kInputsSpecialSlot2`;
  tick 30. Facts: `TickReached(30)`, `WalkerFamilyCount(FAMILY_ELF,1,1)`,
  `WeaponFamilyEmitted(FAMILY_ROCK, "consequence: elf slot 2 BOUNCING ROCKS emits FAMILY_ROCK projectiles; mutation aborts the first fire() so no rock ever spawns")`,
  `WalkerOfTeamAlive(0,1,1)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`.
  Mutation `family_elf.cpp:66` (16-space): `fireob = static_cast<weap*>(self->fire());` → `return false;`.
- **`weapon_boomerang_return_scen99`**: spawns
  `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,4,300}`,
  `{FAMILY_ARCHER,1,kOrderLiving,200,200,0,0}`; reuse `kInputsSpecialSlot2`;
  tick 80. Facts: `TickReached(80)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `EffectFamilyCount(FAMILY_BOOMERANG, 1, 2, -1, 0, "consequence: ...")`,
  `EventKindAtLeast(1,2)`,
  `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 14000, "rng_drift: ... (6000-cent span, gate-recognised label)")`.
  Mutation `family_soldier.cpp:46` **12-space** (byte-match — the source is
  12-space-indented, not 16):
  `            newob = summon_entity(self, Order::FX, FAMILY_BOOMERANG);` → `            return false;`.
- **`weapon_exploding_boulder_scen99`**: spawns
  `{FAMILY_BARBARIAN,0,kOrderLiving,120,120,0,0,5,300}` + 3 soldiers at
  `(160,120)`, `(200,160)`, `(260,200)` team 1; reuse `kInputsSpecialSlot2`;
  tick 60. Facts: `TickReached(60)`, `WalkerFamilyCount(FAMILY_BARBARIAN,1,1)`,
  `WeaponFamilyEmitted(FAMILY_BOULDER)`,
  `EffectFamilyCount(FAMILY_EXPLOSION, 1, 4, -1, 0, "consequence: ...")`,
  `WalkerFamilyCount(FAMILY_SOLDIER, 0, 3, "rng_drift: ...")`, `EventKindAtLeast(1,3)`.
  Mutation `family_barbarian.cpp:59` (8-space): `alive->set_skip_exit(5000);` → `alive->set_skip_exit(0);`.

Empirical numerics (`EventKindAtLeast` floors, HP/count ranges, tick budgets) are
tunable starting points — adjust to runtime so each unmutated scenario passes,
every floor is `> 0`, each mutation flips ≥1 predicate, every widened range carries
a label, and each scenario keeps ≥3 non-`TickReached` predicates incl. ≥1
`consequence:`-labelled one. The boomerang 6000-cent HP span MUST carry an
`rng_drift:` label.

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild
master dump → `capture_master_golden.sh weapon_rock_slot2_emit_scen99 weapon_boomerang_return_scen99 weapon_exploding_boulder_scen99`
→ build+ctest 0 failures; wrap branch/master-divergent predicates in
`branch_only`/`master_only`).

## Verification Phases
- **`check-weapon-trajectories`** — type `check`, `bounce_target: impl-weapon-trajectories`.
  Purpose: deterministically confirm the 3 scenarios are present, green, mirrored,
  committed, and depth/consequence-compliant. Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`.
  Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.weapon_rock_slot2_emit_scen99:Parity.weapon_boomerang_return_scen99:Parity.weapon_exploding_boulder_scen99'` → pass.
  - `ls tests/parity/golden/{weapon_rock_slot2_emit,weapon_boomerang_return,weapon_exploding_boulder}_scen99.json` → all exist.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.predicate_depth_gate_no_trivially_wide_ranges'` → pass
    (boomerang 6000-cent HP span must carry an `rng_drift:` label).
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - **Mechanical depth/consequence count** for each of the 3 new ids — run the
    `awk`/`grep` snippet (see §2 of the plan) on each `kFacts_<id>[]` block:
    require `≥3` non-`TickReached` predicates and `grep -q 'consequence:'`. FAIL if any fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- All 3 scenarios pass; 3 goldens exist; mirror byte-identical.
- The `predicate_depth_gate_no_trivially_wide_ranges` gate and all other gates pass.
- Each scenario has ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h weapon-trajectories"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/weapon_rock_slot2_emit_scen99.json tests/parity/golden/weapon_boomerang_return_scen99.json tests/parity/golden/weapon_exploding_boulder_scen99.json && git commit -m "parity-cov: weapon-trajectory scenarios"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
