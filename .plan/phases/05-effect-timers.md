# Phase 05 — Effect timers (bomb)

## Phase Name
Effect timer scenario

## Implement Phase ID
`impl-effect-timers`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp` (wiring,
  **do not modify**), `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/fact_predicate.h`, `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`.
- `src/gameplay/families/effect_family_bomb.cpp:17-29,95` (on_death adds
  FAMILY_EXPLOSION; on_act nullptr).
- `src/gameplay/families/family_thief.cpp:61-91` (DROP BOMB slot 1, line 69
  spawns FAMILY_BOMB).
- Reuses `kInputsSpecialSlot1` (existing constant — reuse by name).

## New Outputs
1 `ScenarioSpec` row `effect_bomb_timer_scen99`, 1 macro, 1 `kMut_*`, 1 golden,
mirror update, rebuilt dump.

## File Changes
Append table row + macro; 1 new golden; mirror the table; rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes (scenario
in `kScenarios[]`, macro present, golden on disk, named test green, mirror in
sync). If all hold, ensure the two commits landed and yield immediately — do not
redo work. Otherwise implement only the missing pieces.

- **`effect_bomb_timer_scen99`**: spawns
  `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,5,300}`,
  `{FAMILY_SOLDIER,1,kOrderLiving,400,400,0,0}`; reuse `kInputsSpecialSlot1`;
  tick 30. Facts: `TickReached(30)`, `WalkerFamilyCount(FAMILY_THIEF,1,1)`,
  `EffectFamilyCount(FAMILY_BOMB, 1, 2, -1, 0, "consequence: ...")`,
  `EventKindAtLeast(1,2)`, `WalkerOfTeamAlive(0,1,1)`.
  Mutation `family_thief.cpp:69`:
  `newob = current_game->world->add_ob(Order::FX, FAMILY_BOMB, 1);` → `return false;`.

Empirical numerics (`EventKindAtLeast` floor, effect-count range, tick budget)
are tunable starting points — adjust to runtime so the unmutated scenario passes,
every floor is `> 0`, the mutation flips ≥1 predicate, every widened range carries
a label, and the scenario keeps ≥3 non-`TickReached` predicates incl. ≥1
`consequence:`-labelled one.

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild master
dump → `capture_master_golden.sh effect_bomb_timer_scen99` → build+ctest 0
failures; wrap branch/master-divergent predicates in `branch_only`/`master_only`).

## Verification Phases
- **`check-effect-timers`** — type `check`, `bounce_target: impl-effect-timers`.
  Purpose: deterministically confirm the scenario is present, green, mirrored,
  committed, and depth/consequence-compliant. Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`.
  Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.effect_bomb_timer_scen99'` → pass.
  - `ls tests/parity/golden/effect_bomb_timer_scen99.json` → exists.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - **Mechanical depth/consequence count** for `effect_bomb_timer_scen99` — run the
    `awk`/`grep` snippet on the `kFacts_<id>[]` block: require `≥3` non-`TickReached`
    predicates and `grep -q 'consequence:'`. FAIL if either fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- `effect_bomb_timer_scen99` passes; golden exists; mirror byte-identical.
- All coverage/golden/mutation gates pass.
- ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h effect-timers"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/effect_bomb_timer_scen99.json && git commit -m "parity-cov: effect-timer scenario"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
