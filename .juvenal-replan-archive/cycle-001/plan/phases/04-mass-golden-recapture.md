# Phase 04 — Mass master golden recapture

## Phase Name
Capture every missing master golden; refresh existing ones under the resynced companion.

## Implement Phase ID
`04-mass-golden-recapture`

## Preexisting Inputs
- `scripts/parity/capture_master_golden.sh` (extended in this phase).
- `tests/parity/scenario_table.h` (final treasure-fixed version from phase 03).
- `../openglad-master/build/ci-test/parity_dump_master` (rebuilt in phase 03 with mirror patch).
- `tests/parity/golden/*.json` (39+ existing).
- `scripts/parity/validate_schema.py`.
- `scripts/parity/check_coverage_manifest.py` (with `--emit-scenario-list` added in phase 01).

## New Outputs
- `scripts/parity/capture_master_golden.sh` accepts:
  - `--all` (iterate every `SemanticParity` row in `kScenarios` whose `is_branch_internal == false`).
  - `--out-dir <path>` (default `tests/parity/golden`).
  - `--no-write --diff` (emit JSON to a tmp dir and `diff -ru tests/parity/golden tmpdir` instead of overwriting; used by verifier).
- Up to 93 new + replacement golden files under `tests/parity/golden/` so every `SemanticParity` master-comparable row has a fresh golden.
- `.plan/parity-recapture-diff.md` listing every file added/replaced (one line per change, format `<+|M> <sha1-prefix> <id>.json`).
- `tests/parity/scenario_facts_generated.json` regenerated.

## File Changes
- `scripts/parity/capture_master_golden.sh` (extend with `--all`, `--out-dir`, `--no-write --diff`).
- `tests/parity/golden/*.json` (mass replace/add).
- `.plan/parity-recapture-diff.md` (new).
- `tests/parity/scenario_facts_generated.json` (regenerate).

## Implementation Details
1. Implement `--all` by reading `kScenarios` from `tests/parity/scenario_table.h` via the companion binary's own enumeration (pass each `id` to `parity_dump_master` and write `<out-dir>/<id>.json` if `compare_mode == SemanticParity && is_branch_internal == false`).
2. Reuse the existing `parity_dump_master` invocation pattern in `capture_master_golden.sh`. Do not rewrite the companion binary.
3. Run `scripts/parity/capture_master_golden.sh --all`.
4. Regenerate `tests/parity/scenario_facts_generated.json` via the existing tool.
5. After bulk capture, run `git status tests/parity/golden/ | wc -l` and write the count plus the per-file change list into `.plan/parity-recapture-diff.md`.

## Verification Phases

### `04a-check-zero-skipped-and-pass-grows`
- Type: `check`
- Bounce target: `04-mass-golden-recapture`
- Purpose: No more skipped parity tests; total passed equals prior P + F + S (no rows silently dropped).
- Commands:
  - `cmake --build --preset ci-test --target og_test_parity && build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p04a.out`.
  - `grep -cE '^\[  SKIPPED \] Parity\.' /tmp/p04a.out` equals `0`.
  - Read `Passed: P`, `Skipped: S`, `Failing: F` from `.plan/parity-present-state.md` and assert live `[  PASSED  ] N` equals `P + F + S`. With today's baseline (P=56, S=81, F=13) the assertion is `PASSED == 150`. A silently disagreeing predicate set would short the count and trip this check.

### `04b-check-every-master-comparable-row-has-golden`
- Type: `check`
- Bounce target: `04-mass-golden-recapture`
- Purpose: Every `SemanticParity && !is_branch_internal` row has a schema-valid golden on disk.
- Commands:
  - `python3 scripts/parity/check_coverage_manifest.py --emit-scenario-list > /tmp/p04b.tsv`.
  - In-line Python or shell loop: split each non-empty line of `/tmp/p04b.tsv` on a single ASCII tab (`\t`, 0x09), assert exactly three fields. For every row with `compare_mode == SemanticParity && is_branch_internal == false`:
    - `test -s tests/parity/golden/<id>.json` (exists and non-empty).
    - `python3 scripts/parity/validate_schema.py tests/parity/golden/<id>.json` exits 0.

### `04c-check-recapture-doc-and-mirror-untouched`
- Type: `check`
- Bounce target: `04-mass-golden-recapture`
- Purpose: Recapture diff doc lists every file actually changed; mirror untouched this phase.
- Commands:
  - `test -f .plan/parity-recapture-diff.md`.
  - `LINES=$(grep -cE '^[+M] [0-9a-f]{8} ' .plan/parity-recapture-diff.md)`; assert `LINES >= 1`.
  - Assert `LINES` equals `git diff --name-only HEAD~1 HEAD -- tests/parity/golden/ | wc -l`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0 (mirror unchanged this phase).

## Success Criteria
- All three check phases (`04a`, `04b`, `04c`) pass.
- `og_test_parity` reports `[  SKIPPED ] 0` for `Parity.*` rows.
- Every `SemanticParity` non-branch-internal row has a schema-valid golden.
- Recapture diff file enumerates each golden change with a sha1 prefix.

## Git Commit Requirement
Branch-only commit before yielding:
```
git add scripts/parity/capture_master_golden.sh \
        tests/parity/golden/ \
        tests/parity/scenario_facts_generated.json \
        .plan/parity-recapture-diff.md
git commit -m "parity-cov: phase 04 — mass golden recapture (N goldens)"
```
No companion-side commit this phase.
