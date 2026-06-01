# Phase 07 — Multi-team coordination

## Phase Name
Multi-team is_friendly scenario

## Implement Phase ID
`impl-multiplayer-teams`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp:42-85`
  (`apply_post_load_spawns` — no-myguy branch; **do not modify**),
  `tests/parity/test_parity_scenarios.cpp`, `tests/parity/fact_predicate.h`,
  `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`.
- `src/gameplay/walker.cpp:1675-1742` (`is_friendly`; no-myguy branch 1711-1716;
  load-bearing line 1723).

## New Outputs
1 `ScenarioSpec` row `multiplayer_two_teams_scen99`, 1 macro,
1 `kMut_multiplayer_two_teams_scen99` (tab-indented), 1 golden, mirror update,
rebuilt dump.

## File Changes
Append table row + macro; 1 new golden; mirror the table; rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes (scenario
in `kScenarios[]`, macro present, golden on disk, named test green, mirror in
sync). If all hold, ensure the two commits landed and yield immediately — do not
redo work. Otherwise implement only the missing pieces.

- Spawns:
  - `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,3,200}` (team 0)
  - `{FAMILY_THIEF,2,kOrderLiving,140,140,0,0,3,200}` (team 2)
  - `{FAMILY_ARCHER,1,kOrderLiving,200,200,0,0}` (team 1)
- Inputs: `{5,0,K_RIGHT},{30,0,K_NONE},{35,0,K_FIRE},{200,0,K_NONE}`; tick budget 200.
- Facts: `TickReached(200)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `WalkerFamilyCount(FAMILY_THIEF,1,1)`,
  `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 9000, "consequence: team-2 thief attacks the team-1 archer because their team_num differs (walker.cpp:1723), driving archer HP down; mutation makes every pair mutually friendly so the archer is barely touched and HP stays high (label_exempted)")`,
  `EventKindAtLeast(1,4)`.
- Mutation `walker.cpp:1723` (two leading tabs):
  `\t\treturn headus->team_num() == headtarget->team_num();` → `\t\treturn 1;`.

Empirical numerics (`EventKindAtLeast` floor, HP range, tick budget) are tunable
starting points — adjust to runtime so the unmutated scenario passes, every floor
is `> 0`, the mutation flips ≥1 predicate, every widened range carries a label,
and the scenario keeps ≥3 non-`TickReached` predicates incl. ≥1 `consequence:`-labelled one.

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild master
dump → `capture_master_golden.sh multiplayer_two_teams_scen99` → build+ctest 0
failures; wrap branch/master-divergent predicates in `branch_only`/`master_only`).

## Verification Phases
- **`check-multiplayer-teams`** — type `check`, `bounce_target: impl-multiplayer-teams`.
  Purpose: deterministically confirm the scenario is present, green, mirrored,
  committed, and depth/consequence-compliant. Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`.
  Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.multiplayer_two_teams_scen99'` → pass.
  - `ls tests/parity/golden/multiplayer_two_teams_scen99.json` → exists.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - **Mechanical depth/consequence count** for `multiplayer_two_teams_scen99` — run
    the `awk`/`grep` snippet on the `kFacts_<id>[]` block: require `≥3` non-`TickReached`
    predicates and `grep -q 'consequence:'`. FAIL if either fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- `multiplayer_two_teams_scen99` passes; golden exists; mirror byte-identical.
- All coverage/golden/mutation gates pass.
- ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h multiplayer-teams"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/multiplayer_two_teams_scen99.json && git commit -m "parity-cov: multiplayer-teams scenario"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
