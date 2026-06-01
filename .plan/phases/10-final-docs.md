# Phase 10 — Final docs refresh

## Phase Name
Final docs refresh (156-scenario state)

## Implement Phase ID
`impl-final-docs`

## Preexisting Inputs
- The full 156-scenario `tests/parity/scenario_table.h`, 156 macros in
  `tests/parity/test_parity_scenarios.cpp`, 155 goldens in `tests/parity/golden/`.
- Both runtime files (wiring intact; **do not modify**):
  `tests/parity/scenario_runtime.cpp`,
  `../openglad-master/tools/parity_scenario_runtime.cpp`.
- The mirror `../openglad-master/tools/parity_scenario_table.h` and built
  `../openglad-master/build/ci-test/parity_dump_master`.
- The existing docs `.plan/parity-present-state.md` and
  `.plan/parity-coverage-manifest.md` (**update in place**).

## New Outputs
- Updated `.plan/parity-present-state.md` (156 total, 154 SemanticParity + 2
  Invariant, 155 goldens, per-category depth table).
- Updated `.plan/parity-coverage-manifest.md` (new
  `## Gap-Fill Scenarios Added in Coverage Pass 2` section grouping the gap-fill
  scenarios by the 10 phase categories + updated coverage table).
- **No source code changes.**

## File Changes
Edit `.plan/parity-present-state.md` and `.plan/parity-coverage-manifest.md` in
place. Commit the final docs (plus a regenerated
`tests/parity/scenario_facts_generated.json` if the Step-2 build produced one).

## Implementation Details
First check whether this phase's deliverable already exists and passes (docs
already reference "156 scenarios" / "Gap-Fill Scenarios", suite green, mirror in
sync). If all hold, ensure commits landed and yield immediately — do not redo
work. Otherwise update only what is missing.

- **Step 1** — re-confirm mirror in sync; re-mirror (`cp`) + rebuild master dump
  if `diff -q` shows drift.
- **Step 2** — `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
- **Steps 3-4** — rewrite the two docs. Per-category counts after gap-fill:
  `status_timer:4, summon:3, generator:5, weapon:23, effect:19, input:4,
  multiplayer:1, level_transition:2, midcombat:2`. Leave the missing-golden
  policy at `1` ADD_FAILURE / `0` GTEST_SKIP.

If the Step-2 build regenerated `tests/parity/scenario_facts_generated.json`,
include it in the same branch commit so `git status --porcelain tests/parity/`
ends empty.

## Verification Phases
- **`check-final-docs`** — type `check`, `bounce_target: impl-final-docs`.
  Purpose: deterministically confirm the full 156-scenario state, mirror, goldens,
  docs, and tree-cleanliness. Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`. Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='*'` → 0 failures.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
  - `ls tests/parity/golden/*.json | wc -l` → 155.
  - `grep -c 'OG_PARITY_TEST(' tests/parity/test_parity_scenarios.cpp` → 157
    (156 scenario invocations + the single `#define OG_PARITY_TEST(NAME)` line;
    this is the correct value — it is **not** 156).
  - explicit `ls` of all 16 new golden files (generator/weapon/effect/input/
    multiplayer/level/midcombat) → all exist.
  - `grep -c 'EventKindAtLeast.*,\s*0)' tests/parity/scenario_table.h` → 0.
  - `grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp` → 0.
  - `grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp` → 1.
  - `grep -c 'special_names_table' tests/parity/scenario_runtime.cpp ../openglad-master/tools/parity_scenario_runtime.cpp` → ≥2.
  - `grep -c '156 scenarios' .plan/parity-present-state.md` → ≥1.
  - `grep -c 'Gap-Fill Scenarios' .plan/parity-coverage-manifest.md` → ≥1.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty (no
    uncommitted regenerated `scenario_facts_generated.json` or other parity-test changes).

## Success Criteria
- Full suite green (0 failures); all 156 scenarios + 7 gates pass.
- 155 goldens; `OG_PARITY_TEST(` count is exactly 157; mirror byte-identical.
- `parity_dump_master` present; runtime wiring intact.
- Docs reference "156 scenarios" and the "Gap-Fill Scenarios" section.
- Missing-golden policy: 1 ADD_FAILURE / 0 GTEST_SKIP.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit the final docs to git before yielding. If the
Step-2 build regenerated `tests/parity/scenario_facts_generated.json`, include it
in the same branch commit so `git status --porcelain tests/parity/` ends empty:
- `git add .plan/parity-present-state.md .plan/parity-coverage-manifest.md tests/parity/scenario_facts_generated.json && git commit -m "parity-cov: final docs refresh (156-scenario state)"`

If Step 1 had to re-mirror, also commit the master worktree:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: re-mirror scenario_table.h"`

No `--no-verify`/`--amend` unless flagged as recovery.
