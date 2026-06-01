# Phase 08 — Level withdraw

## Phase Name
Level withdraw scenario

## Implement Phase ID
`impl-level-withdraw`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp` (wiring,
  **do not modify**), `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/fact_predicate.h`, `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`.
- The existing `scripted_input_scen9301` row and its constants
  `kFamilySpawns_soldier_with_exit_withdraw` (lines 372-376) and
  `kInputsScripted9301` (lines 263-270) — **reuse by name; do not duplicate**.
- `src/gameplay/game_world.cpp:1357,1391-1499` (withdraw handling; `level_done`
  default 2 at 1357).
- `src/gameplay/families/treasure_family_navigation.cpp:36-98` (`exit_on_eat`;
  line 86 sets `withdraw_requested`; 88-90 emit WithdrawToLevel).

## New Outputs
1 `ScenarioSpec` row `level_withdraw_scen99`, 1 macro, 1 new fact array
`kFacts_level_withdraw_scen99`, 1 `kMut_*`, 1 golden, mirror update, rebuilt dump.
**No new spawn/input constants** (reuse by name).

## File Changes
Append table row + macro + new fact array; 1 new golden; mirror the table;
rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes (scenario
in `kScenarios[]`, macro present, golden on disk, named test green, mirror in
sync). If all hold, ensure the two commits landed and yield immediately — do not
redo work. Otherwise implement only the missing pieces.

- Spawn list: **reuse `kFamilySpawns_soldier_with_exit_withdraw` by name** (do
  not duplicate the constant).
- Inputs: **reuse `kInputsScripted9301` by name**. Tick budget 200.
- Facts (`kFacts_level_withdraw_scen99`): `TickReached(200)`,
  `WalkerFamilyCount(FAMILY_SOLDIER, 2, 2)`,
  `LevelDoneEquals(2, "consequence: withdraw path returns level_done=2 ...")`,
  `EventKindAtLeast(/*withdraw_to_level*/8, 1)`, `EventKindAtLeast(1, 2)`.
- Mutation `treasure_family_navigation.cpp:86` (8-space):
  `        world.withdraw_requested = true;` → `        world.withdraw_requested = false;`.
  Rationale: with the flag false, the early-break at `game_world.cpp:1393` never
  fires, the loop completes and sets `level_done = 0` at 1408 →
  `LevelDoneEquals(2)` fails.

Empirical numerics (`EventKindAtLeast` floors, tick budget) are tunable starting
points — adjust to runtime so the unmutated scenario passes, every floor is `> 0`,
the mutation flips ≥1 predicate, every widened range carries a label, and the
scenario keeps ≥3 non-`TickReached` predicates incl. ≥1 `consequence:`-labelled one.
Here `LevelDoneEquals(2, "consequence: ...")` serves as the consequence predicate.

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild master
dump → `capture_master_golden.sh level_withdraw_scen99` → build+ctest 0 failures;
wrap branch/master-divergent predicates in `branch_only`/`master_only`).

## Verification Phases
- **`check-level-withdraw`** — type `check`, `bounce_target: impl-level-withdraw`.
  Purpose: deterministically confirm the scenario is present, green, mirrored,
  committed, reuses the shared constants by name, and is depth/consequence-compliant.
  Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`. Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.level_withdraw_scen99'` → pass.
  - `ls tests/parity/golden/level_withdraw_scen99.json` → exists.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - `grep -c 'kFamilySpawns_soldier_with_exit_withdraw' tests/parity/scenario_table.h` → ≥2 (reused by name, not duplicated).
  - `grep -c 'kInputsScripted9301' tests/parity/scenario_table.h` → ≥2 (reused by name).
  - **Mechanical depth/consequence count** for `level_withdraw_scen99` — run the
    `awk`/`grep` snippet on the `kFacts_<id>[]` block: require `≥3` non-`TickReached`
    predicates and `grep -q 'consequence:'` (the `LevelDoneEquals(2)` consequence). FAIL if either fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- `level_withdraw_scen99` passes; golden exists; mirror byte-identical.
- Shared spawn/input constants reused by name (each `grep -c` ≥2), not duplicated.
- All coverage/golden/mutation gates pass.
- ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h level-withdraw"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/level_withdraw_scen99.json && git commit -m "parity-cov: level-withdraw scenario"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
