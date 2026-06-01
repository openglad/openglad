# Phase 11 — Anti-cheat self-test and final sign-off

## Phase Name
Codify and self-test every bypass; final sign-off doc; full repo test suite green.

## Implement Phase ID
`11-anti-cheat-and-final-signoff`

## Preexisting Inputs
- `scripts/parity/lint_scenario_facts.py` (existing 4 + new 1 = 5 rules).
- `scripts/parity/run_mutation_canary.sh` + `run_mutation_canary_runtime.py` (phase 10).
- `scripts/parity/check_coverage_manifest.py` (with subcommands from phases 01 and to-be-added in this phase).
- `scripts/parity/capture_master_golden.sh`.
- `og_test_parity` (phase 09/10 green).
- `tests/parity/test_parity_scenarios.cpp`.
- `tests/parity/scenario_table.h` (final shape).
- `.plan/parity-coverage-manifest.md`.
- `.plan/parity-canary-exemptions.md`.
- `.github/workflows/ci.yml` (only if present in repo).

## New Outputs
- `scripts/parity/anti_cheat_selftest.sh` — for each known bypass, spins up a throwaway worktree, applies a realistic mutation, runs the corresponding guard, and asserts non-zero exit. Bypasses to cover:
  - **A**: Skip a treasure row via `compare_mode = ByteEqual` to dodge predicate evaluation → guard: `scripts/parity/check_coverage_manifest.py --require-semantic-parity-for-required-rows`.
  - **B**: Remove all RNG-insensitive predicates from a row → guard: `lint_scenario_facts.py::requires_rng_insensitive_predicate`.
  - **C**: Replace `EffectFamilyCount(FAMILY_X, source=Y, ...)` with unqualified version → `lint_scenario_facts.py::effect_count_unqualified`.
  - **D**: Add a dead predicate that is never evaluated → `lint_scenario_facts.py::dead_predicate`.
  - **E**: Widen `WalkerFamilyCount(FAMILY_X, 1, 1)` to `(0, 999)` without `rng_drift` comment → `lint_scenario_facts.py::unjustified_widening`.
  - **F**: Delete a golden to make a row skip → flip `test_parity_scenarios.cpp:108` so that the SemanticParity-missing-golden branch (literal message `"master golden missing for "`) becomes `ADD_FAILURE` instead of `GTEST_SKIP`. The other two `GTEST_SKIP` sites (`:118` ByteEqual missing-golden with message `"golden not yet captured for "`, and `:144` the `OG_PARITY_TEST(NAME)` macro's missing-scenario SKIP with message `"scenario \"" #NAME "\" is not present in kScenarios; "`) are intentionally left intact. Verifier asserts `grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `0` and `grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `1`.
  - **G**: Stage a branch-side `scenario_table.h` change without mirroring → guard: new pre-commit-friendly script `scripts/parity/check_mirror_sha.sh` that `cmp`s the two files and exits non-zero.
  - **H**: Add a row but never register `OG_PARITY_TEST(id)` → guard: new `scripts/parity/check_test_registration.py` greps for every row id in `test_parity_scenarios.cpp`.
- `scripts/parity/ci_parity.sh` orchestrator that runs: build, full `og_test_parity`, `lint_scenario_facts.py`, `check_coverage_manifest.py`, `check_mirror_sha.sh`, `check_test_registration.py`, `run_mutation_canary.sh --all`, `anti_cheat_selftest.sh`. Exits non-zero if any fails.
- `.plan/parity-signoff-honest.md` summarising final state with literal lines:
  - `Total rows: <N>`
  - `Rows green: <N>`
  - `Rows with ≥1 RNG-insensitive predicate: <N>`
  - `Coverage manifest: <X>/<X>`
  - `Anti-cheat bypasses caught: 8/8`
  - `Canary non-exempt rows green: <N>/<N>`
- CI hookup: if `.github/workflows/ci.yml` exists at implement time, add a job entry that invokes `scripts/parity/ci_parity.sh`; if absent, this output is skipped (orchestrator is still landed).
- `tests/parity/test_parity_scenarios.cpp` — Bypass F implementation: flip the SemanticParity missing-golden branch at `:108` from `GTEST_SKIP() << "master golden missing for "` to `ADD_FAILURE() << "master golden missing for "`.
- `scripts/parity/check_coverage_manifest.py` — add `--require-semantic-parity-for-required-rows` subcommand if not already present (Bypass A guard).

## File Changes
- `scripts/parity/anti_cheat_selftest.sh` (new).
- `scripts/parity/ci_parity.sh` (new).
- `scripts/parity/check_mirror_sha.sh` (new).
- `scripts/parity/check_test_registration.py` (new).
- `scripts/parity/check_coverage_manifest.py` (add `--require-semantic-parity-for-required-rows`).
- `tests/parity/test_parity_scenarios.cpp` (flip missing-golden path from SKIP to ADD_FAILURE when `compare_mode == SemanticParity`).
- `.github/workflows/ci.yml` (only if present).
- `.plan/parity-signoff-honest.md` (new).

## Implementation Details
- `anti_cheat_selftest.sh` orchestrates all 8 bypasses with a uniform pattern:
  ```
  for each bypass:
      worktree=/tmp/parity-bypass-<letter>
      git worktree add "$worktree" HEAD
      apply mutation in "$worktree"
      run guard against "$worktree"
      assert guard exit ≠ 0
      git worktree remove --force "$worktree"
      echo "Bypass <letter>: guard tripped"
  ```
- `ci_parity.sh` is `set -euo pipefail` and runs each step sequentially; first failure exits non-zero with the failed step name.
- `check_mirror_sha.sh` is a 3-line wrapper around `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.
- `check_test_registration.py` parses `tests/parity/scenario_table.h` for every row id and asserts `OG_PARITY_TEST(<id>)` exists in `tests/parity/test_parity_scenarios.cpp`.

## Verification Phases

### `11a-check-anti-cheat-selftest-passes`
- Type: `check`
- Bounce target: `11-anti-cheat-and-final-signoff`
- Purpose: All 8 bypasses are caught by their corresponding guard; all throwaway worktrees cleaned up.
- Commands:
  - `scripts/parity/anti_cheat_selftest.sh 2>&1 | tee /tmp/p11a.out`. Exit 0.
  - `grep -F 'Bypass A: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass B: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass C: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass D: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass E: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass F: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass G: guard tripped' /tmp/p11a.out` exits 0.
  - `grep -F 'Bypass H: guard tripped' /tmp/p11a.out` exits 0.
  - `ls /tmp | grep -E '^parity-bypass-' | wc -l` equals `0`.
  - `grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `0`.
  - `grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `1`.

### `11b-check-ci-orchestrator-and-fullsuite-green`
- Type: `check`
- Bounce target: `11-anti-cheat-and-final-signoff`
- Purpose: CI orchestrator passes; full repo test suite passes; CI YAML hookup is correct (conditional on file existing).
- Commands:
  - `scripts/parity/ci_parity.sh` exits 0.
  - `cmake --build --preset ci-test && ctest --preset ci-test` exits 0.
  - CI-file conditional (total): `if [ -f .github/workflows/ci.yml ]; then grep -F 'scripts/parity/ci_parity.sh' .github/workflows/ci.yml && grep -F 'parity-ci' .github/workflows/ci.yml; else true; fi` exits 0.

### `11c-check-signoff-doc-and-manifest`
- Type: `check`
- Bounce target: `11-anti-cheat-and-final-signoff`
- Purpose: Sign-off doc lists literal totals matching live state; mirror byte-equal.
- Commands:
  - `test -f .plan/parity-signoff-honest.md`.
  - Re-derive every integer (rows, rows green, rows with ≥1 RNG-insensitive predicate, coverage targets covered/total, canary non-exempt rows green/total) and `grep -F` each literal line into the doc:
    - `Total rows: <N>`
    - `Rows green: <N>`
    - `Rows with ≥1 RNG-insensitive predicate: <N>`
    - `Coverage manifest: <X>/<X>`
    - `Anti-cheat bypasses caught: 8/8`
    - `Canary non-exempt rows green: <N>/<N>`
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

## Success Criteria
- All three check phases (`11a`, `11b`, `11c`) pass.
- `anti_cheat_selftest.sh` reports all 8 bypasses tripped; no leftover worktrees.
- Bypass F flip applied: `GTEST_SKIP() << "master golden missing` count is `0`, `ADD_FAILURE() << "master golden missing` count is `1`.
- `ci_parity.sh` exits 0.
- Full repo test suite green (`ctest --preset ci-test`).
- `.plan/parity-signoff-honest.md` exists with every literal tally line re-derived from live state.
- Mirror byte-equal.

## Git Commit Requirement
Commit BOTH worktrees before yielding (companion commit is almost certainly empty this phase; only commit if there is a real change).

Companion (in `../openglad-master`, only if a real change exists):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 11 — anti-cheat and final signoff"
```

Branch:
```
git add scripts/parity/anti_cheat_selftest.sh \
        scripts/parity/ci_parity.sh \
        scripts/parity/check_mirror_sha.sh \
        scripts/parity/check_test_registration.py \
        scripts/parity/check_coverage_manifest.py \
        tests/parity/test_parity_scenarios.cpp \
        .plan/parity-signoff-honest.md
# Add .github/workflows/ci.yml only if it exists and was modified.
[ -f .github/workflows/ci.yml ] && git add .github/workflows/ci.yml || true
git commit -m "parity-cov: phase 11 — anti-cheat self-test and final signoff"
```
