# Phase 01 — Baseline and per-target gap inventory

## Phase Name
Baseline test counts and per-target coverage gap inventory.

## Implement Phase ID
`01-baseline-and-inventory`

## Preexisting Inputs
- `.plan/goal.md`
- `.plan/parity-coverage-manifest.md`
- `.plan/parity-honest-audit.md`
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json` (39 files)
- `../openglad-master/` worktree at HEAD `de702ef0...`

## New Outputs
- `.plan/parity-present-state.md` (≤200 lines) containing:
  - **Test count snapshot**: lines `Passed: P`, `Skipped: S`, `Failing: F` derived from `og_test_parity --gtest_brief=1`. `--gtest_brief=1` does not emit a `[  FAILED  ] N tests.` summary line; failure count is `grep -cE "^\[  FAILED  \] Parity\." /tmp/p01.out`.
  - **Failing tests**: one bullet per `[  FAILED  ] Parity.<id>` line.
  - **Skipped tests**: one bullet per `master golden missing for <id>` line.
  - **Master companion SHA pinned this phase**: one line `Master companion SHA: <40-hex>` equal to `git -C ../openglad-master rev-parse HEAD`.
  - **Mirror SHA delta**: literal `sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` output. If unequal, state "BRANCH ≠ COMPANION — phase 02 resyncs."
  - **Per-target coverage gap inventory.** Seven tables, one per category, each with columns `target | observed_in_any_row | covering_scenario_id | golden_present`:
    - 21 walker families
    - 20 weapon families
    - 13 treasure families
    - 4 generator families
    - 13 effect families
    - 42 (family, special_slot) pairs, each emitted as literal token `FAMILY_<name>:slot<N>`. `kRequiredSpecials` is `std::pair<std::int32_t, std::uint8_t>` in `tests/parity/coverage_targets.h`; `.first` is reverse-mapped via the bare `family_symbol` table in `state_dump.cpp` (Phase 03 hasn't run yet), `.second` is rendered as its decimal value.
    - 9 event kinds
  - **Broken-state authorisation**: quote bounds from plan §1 verbatim.
- `.plan/parity-coverage-manifest.md` updated in place: `master_companion_sha` frontmatter equals the live companion SHA; `(none yet)` cells reflect current `kScenarios` observations.

## File Changes
- Write `.plan/parity-present-state.md`.
- Edit `.plan/parity-coverage-manifest.md` (`master_companion_sha:`, `covering_scenario_id` cells).
- Edit `scripts/parity/check_coverage_manifest.py` to add `argparse` plus `--emit-gap-table` and `--emit-scenario-list` subcommands.

## Implementation Details
1. Run `cmake --build --preset ci-test --target og_test_parity && build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p01.out`.
2. Extract counts:
   - `PASSED=$(grep -oE '^\[  PASSED  \] [0-9]+' /tmp/p01.out | head -1 | awk '{print $3}')`
   - `SKIPPED=$(grep -oE '^\[  SKIPPED \] [0-9]+' /tmp/p01.out | head -1 | awk '{print $3}')`
   - `FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p01.out)`
3. Extend `scripts/parity/check_coverage_manifest.py` with an `argparse` front-end (script today has none — arguments are silently ignored) adding two subcommands:
   - `--emit-gap-table`: parses `tests/parity/coverage_targets.h` and `tests/parity/scenario_table.h`, writes the seven gap tables to stdout in pipe-table form; consumed as `/tmp/gap.md`.
   - `--emit-scenario-list`: parses `tests/parity/scenario_table.h`, writes one line per `kScenarios` row formatted as three tab-separated fields `<id>\t<compare_mode>\t<is_branch_internal>`:
     - `<id>`: C++ identifier-style scenario id literal (e.g. `smoke_empty_scen99`).
     - `<compare_mode>`: one of `ByteEqual`, `Invariant`, `SemanticParity` (enum identifier verbatim, no `CompareMode::` prefix).
     - `<is_branch_internal>`: lowercase `true`/`false`.
     - Lines in row order; single trailing newline terminates the last line.
     - Example output for the first two present-day rows:
       ```
       smoke_empty_scen99	SemanticParity	false
       rng_seed_stable_scen99	Invariant	false
       ```
     - Consumed by Phase 04 verifier 04b, which splits on a single ASCII tab (`\t`, 0x09) and asserts exactly three fields per non-empty line.
   - Both subcommands are pure stdout, exit 0 on success and ≠0 on parse failure. Default behaviour (no flags) remains identical.
4. Run `python3 scripts/parity/check_coverage_manifest.py --emit-gap-table > /tmp/gap.md` and embed the seven tables verbatim in `.plan/parity-present-state.md`.
5. For each target row, `golden_present` is `yes` iff `tests/parity/golden/<covering_scenario_id>.json` exists.

## Verification Phases

### `01a-check-tree-clean-and-counts`
- Type: `check`
- Bounce target: `01-baseline-and-inventory`
- Purpose: Confirm clean working tree, present-state doc exists, recorded counts re-derive from a fresh test run.
- Commands:
  - `git status --porcelain | grep -v '^?? .plan/.juvenal-state.json$' | grep -v '^?? scripts/parity/__pycache__' | wc -l` must equal `0`.
  - `test -f .plan/parity-present-state.md`.
  - Re-run `cmake --build --preset ci-test --target og_test_parity && build/ci-test/og_test_parity --gtest_brief=1`, re-derive `PASSED/SKIPPED/FAILED`, `grep -F` each integer in `.plan/parity-present-state.md` under "Test count snapshot".
  - `git log -1 --name-status` lists `.plan/parity-present-state.md` and `.plan/parity-coverage-manifest.md`.

### `01b-check-companion-sha-pinned`
- Type: `check`
- Bounce target: `01-baseline-and-inventory`
- Purpose: Companion SHA is pinned consistently across present-state and manifest.
- Commands:
  - `SHA=$(git -C ../openglad-master rev-parse HEAD)`.
  - `grep -F "Master companion SHA: $SHA" .plan/parity-present-state.md` exits 0.
  - `grep -F "master_companion_sha: $SHA" .plan/parity-coverage-manifest.md` exits 0.

### `01c-check-gap-inventory-shape`
- Type: `check`
- Bounce target: `01-baseline-and-inventory`
- Purpose: Every walker/weapon/treasure/generator/effect family, special slot pair, and event kind appears literally in the gap inventory.
- Commands:
  - Loop over each header constant via `grep -oE '"FAMILY_[A-Z0-9_]+"' tests/parity/coverage_targets.h` and `grep -F` each one in `.plan/parity-present-state.md`.
  - For the 9 event kinds in `kRequiredEventKinds`, `grep -F` each.
  - For the 42 specials in `kRequiredSpecials` (`std::pair<std::int32_t, std::uint8_t>`, members `.first`/`.second`), emit the canonical token `FAMILY_<name>:slot<N>` and `grep -F` each in `.plan/parity-present-state.md`.

## Success Criteria
- All three check phases (`01a`, `01b`, `01c`) pass.
- Working tree is clean except for permitted ignores.
- Live test counts match the recorded snapshot.
- Companion SHA is identical in both `.plan/parity-present-state.md` and `.plan/parity-coverage-manifest.md`.
- Gap inventory contains every required family, special-slot pair, and event kind literally.

## Git Commit Requirement
The implementer MUST commit before yielding:
```
git add .plan/parity-present-state.md \
        .plan/parity-coverage-manifest.md \
        scripts/parity/check_coverage_manifest.py
git commit -m "parity-cov: phase 01 — baseline and per-target gap inventory"
```
No companion-side commit this phase.
