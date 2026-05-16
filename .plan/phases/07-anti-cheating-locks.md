# Phase 07 — Anti-cheating checks and CI wiring

**Phase Name**: Lock the harness against future widening / blob-cover /
silent recapture.

**Implement Phase ID**: `07-anti-cheating-locks`

## Preexisting Inputs

- `tests/parity/test_parity_coverage_gate.cpp`
- `scripts/parity/lint_scenario_facts.py`
- `scripts/parity/run_mutation_canary.sh`
- `scripts/parity/validate_schema.py`
- `scripts/parity/capture_master_golden.sh`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `.github/workflows/*.yml` (if present)

## New Outputs

- `scripts/parity/ci_parity.sh` — single-shot driver (executable):
  ```bash
  cmake --build --preset ci-test --target og_test_parity
  build/ci-test/og_test_parity
  python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h
  scripts/parity/run_mutation_canary.sh --all
  scripts/parity/capture_master_golden.sh --dry-run-compare-only
  ```
- New mode `--dry-run-compare-only` in `capture_master_golden.sh` —
  recapture every golden into `/tmp/recapture/` and assert byte-equal vs
  committed; exit 1 on any diff.
- Updated `.github/workflows/test.yml` (if present) — add a
  `parity-strict` job running `scripts/parity/ci_parity.sh`. If no CI
  YAML exists, the verifier accepts `ci_parity.sh` as the integration
  surface and the invocation is documented in
  `.plan/parity-second-divergence-report.md` "How to run in CI".

## File Changes

- Create `scripts/parity/ci_parity.sh` (executable).
- Modify `scripts/parity/capture_master_golden.sh` (new flag).
- Modify `.github/workflows/test.yml` (CI job) — if file exists.
- Document the invocation in `.plan/parity-second-divergence-report.md` if
  no CI YAML is present.
- Branch commit: `parity-finish-2: phase 07 — anti-cheating gate + CI wiring`.

## Implementation Details

- Bypass-3 relies on `validate_schema.py` returning non-zero on
  malformed JSON (it does).
- Bypass-1 relies on Phase 3 lint rule (parse → diagnose → non-zero exit).
- Bypass-2 relies on Phase 4 behavioural gate.

## Verification Phases

- **`07a-check-ci-runs-everything`** (`check`, `bounce_target: 07-anti-cheating-locks`):
  Purpose: confirm the full parity bundle is invoked via either CI YAML
  or `ci_parity.sh`.
  Commands:
  - `test -x scripts/parity/ci_parity.sh`.
  - `scripts/parity/ci_parity.sh` exits 0.
  - If `.github/workflows/test.yml` exists,
    `grep -E '(og_test_parity|lint_scenario_facts\.py|run_mutation_canary\.sh|behavioural_coverage_gate|ci_parity\.sh)' .github/workflows/test.yml`
    matches every invocation listed; missing invocation fails.
  - Otherwise, the verifier accepts `ci_parity.sh` and asserts the
    invocation is documented in
    `.plan/parity-second-divergence-report.md` "How to run in CI".

- **`07b-check-no-bypass-known-tricks`** (`check`, `bounce_target: 07-anti-cheating-locks`):
  Purpose: prove each anti-cheating lock fires on a concrete tampering.
  Commands (each runs in a throwaway worktree;
  `git -C /tmp/parity-bypass checkout -- .` restores between bypasses):
  - `git worktree add /tmp/parity-bypass HEAD`.
  - **Bypass 1 (widening lint)**:
    `cd /tmp/parity-bypass && sed -i -E 's/WalkerFamilyCount\(FAMILY_SOLDIER,\s*[0-9]+,\s*[0-9]+\)/WalkerFamilyCount(FAMILY_SOLDIER, 0, 99)/' tests/parity/scenario_table.h`
    on the first occurrence. Then
    `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h`
    MUST exit non-zero with `unjustified_widening` on stderr.
  - Restore: `git -C /tmp/parity-bypass checkout -- .`.
  - **Bypass 2 (behavioural coverage)**: `sed -i` the
    `WeaponFamilyEmitted(FAMILY_KNIFE, ...)` predicate out of
    `tests/parity/scenario_table.h`, then build and run
    `build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage_gate_weapons'`.
    Test MUST FAIL naming `FAMILY_KNIFE`.
  - Restore.
  - **Bypass 3 (golden tampering)**: `printf 'X' > tests/parity/golden/<first id>.json`. Then
    `python3 scripts/parity/validate_schema.py tests/parity/golden/<id>.json`
    MUST exit non-zero, AND
    `build/ci-test/og_test_parity --gtest_filter='Parity.<id>'` MUST
    FAIL.
  - All three guards must trigger; if any bypass passes silently the
    verifier exits non-zero.
  - Cleanup: `git worktree remove --force /tmp/parity-bypass`.

## Success Criteria

- `scripts/parity/ci_parity.sh` exists, is executable, and exits 0.
- `capture_master_golden.sh --dry-run-compare-only` is a working mode.
- CI YAML invokes the parity bundle (or `ci_parity.sh` is documented as
  the integration surface).
- Each of the three concrete bypasses is caught by the appropriate
  guard.

## Git Commit Requirement

The implementer MUST `git add` `scripts/parity/ci_parity.sh`,
`scripts/parity/capture_master_golden.sh`, the CI YAML (if modified),
and any documentation update, then `git commit` with message
`parity-finish-2: phase 07 — anti-cheating gate + CI wiring`
**before yielding**. The next check phases verify HEAD lists those file
changes via `git log -1 --name-status`.
