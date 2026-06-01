# Phase 01 — Confirm prior landed work

## Phase Name
Confirm prior landed work (walker-status-timers + summon-lifecycle)

## Implement Phase ID
`impl-confirm-prior-work`

## Preexisting Inputs
- `tests/parity/scenario_table.h` (contains the 6 already-landed scenarios).
- `tests/parity/test_parity_scenarios.cpp` (6 paired `OG_PARITY_TEST` macros).
- The 6 golden files:
  - `tests/parity/golden/enemy_freeze_mage_scen99.json`
  - `tests/parity/golden/invisibility_thief_scen99.json`
  - `tests/parity/golden/speed_potion_movement_scen99.json`
  - `tests/parity/golden/invulnerable_potion_scen99.json`
  - `tests/parity/golden/summon_lifetime_faerie_scen99.json`
  - `tests/parity/golden/summon_lifetime_decrement_faerie_scen99.json`
- `../openglad-master/tools/parity_scenario_table.h` (mirror) and built
  `../openglad-master/build/ci-test/parity_dump_master`.
- Commits `36ab6e61` and `b6fab712` plus their master-mirror commits.

## New Outputs
None unless drift is detected. If `diff -q` shows mirror drift or a test fails,
the repair outputs are: a re-mirrored `../openglad-master/tools/parity_scenario_table.h`,
a rebuilt `parity_dump_master`, any re-captured missing golden, and the fix
commits in both worktrees.

## File Changes
None in the normal (already-green) case. Only on detected regression: re-mirror
the table (`cp`), rebuild the master dump, re-capture any missing golden.

## Implementation Details
First check whether this phase's deliverable already exists and passes
(the 6 scenarios present in `kScenarios[]`, macros present, goldens on disk,
the named tests green, mirror in sync). If all already hold, ensure the required
commits have landed and yield immediately — do not redo work.

Run the build + tests and the named filters below. If all green and committed,
yield immediately — there is nothing to commit. Only if something regressed
(e.g. a golden missing or mirror drift) perform the minimal repair:
- re-mirror: `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
- rebuild: in `../openglad-master`, `cmake --build --preset ci-test --target parity_dump_master`
- re-capture any missing golden via `scripts/parity/capture_master_golden.sh <scenario...>`

Keep the session short.

## Verification Phases
- **`check-confirm-prior-work`** — type `check`, `bounce_target: impl-confirm-prior-work`.
  Purpose: deterministically confirm the 6 already-landed scenarios are intact,
  green, mirrored, and committed. Emit `VERDICT: PASS` iff every command yields
  its stated result, else `VERDICT: FAIL: <reason>`. Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.enemy_freeze_mage_scen99:Parity.invisibility_thief_scen99:Parity.speed_potion_movement_scen99:Parity.invulnerable_potion_scen99:Parity.summon_lifetime_faerie_scen99:Parity.summon_lifetime_decrement_faerie_scen99'`
    → all pass.
  - `ls tests/parity/golden/{enemy_freeze_mage,invisibility_thief,speed_potion_movement,invulnerable_potion,summon_lifetime_faerie,summon_lifetime_decrement_faerie}_scen99.json`
    → all exist.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
    → 0 (byte-identical).
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'`
    → 7 pass.
  - `git -C . status --porcelain tests/parity/` → empty (no uncommitted
    parity-test changes).

## Success Criteria
- All six named scenarios pass.
- All six goldens exist on disk.
- Mirror table is byte-identical (`diff -q` → 0).
- All 7 coverage/golden/mutation gates pass.
- `git status --porcelain tests/parity/` is empty.
- If any repair was needed, it is committed in both worktrees.

## Git Commit Requirement
In the already-green case there is nothing to commit; yield immediately.
If — and only if — a regression was repaired, the implementer **must** commit the
repair in **both** worktrees (branch + master mirror) before yielding, including
`tests/parity/scenario_facts_generated.json` in the branch commit if the build
regenerated it. No `--no-verify` or `--amend` unless flagged in the message as
recovery.
