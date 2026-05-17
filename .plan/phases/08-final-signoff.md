# Phase 08 — Final honest sign-off

**Phase Name**: Author the sign-off; full bundle green end-to-end; optional CI workflow update.

**Implement Phase ID**: `08-final-signoff`

## Preexisting Inputs

- Every Phase 1–7 commit on tree
- `.plan/parity-present-state.md` (all phase sections present)
- `.plan/parity-coverage-manifest.md` (Phase 02 updated)
- `.plan/parity-honest-audit.md`
- `.plan/parity-canary-exemptions.md` (Phase 07)
- `scripts/parity/ci_parity.sh`
- `scripts/parity/anti_cheat_selftest.sh`

## New Outputs

- `.plan/parity-signoff-honest.md` with required sections:
  - `## Final test surface`
  - `## Coverage outcome`
  - `## Mutation canary outcome`
  - `## Anti-cheat outcome`
  - `## Classified divergences`
  - `## Companion SHA`
  - `## Open risks`

  One-line summary at top:
  *"Parity overall: GREEN. Every required entity family, special ability, attack type, treasure, FX, generator, and event kind is exercised by at least one scenario whose `expected_facts[]` predicate constrains its behaviour; the mutation canary flips ≥1 predicate per non-exempt row (exemption count ≤ 2, both documented); the anti-cheat self-test confirms widening, golden-tamper, behavioural-gate-bypass, and dead-predicate attacks all fail-fast; every golden recapture matches its committed file byte-for-byte under companion SHA <sha>."*

  Doc lists every Phase 1–7 commit SHA range (`git log --grep='parity-finish-3' --oneline`) and the literal companion HEAD SHA.
- Optional update to `.github/workflows/test.yml` adding a job running `scripts/parity/ci_parity.sh` and `scripts/parity/anti_cheat_selftest.sh`. If the file does not exist, document the CI invocation under `## How to run in CI` in the signoff instead.
- Branch commit: `parity-finish-3: phase 08 — honest signoff; bundle green`.

## File Changes

- Create `.plan/parity-signoff-honest.md`.
- Optionally edit `.github/workflows/test.yml`.
- `git add` listed files; commit.

## Implementation Details

Agent runs `scripts/parity/ci_parity.sh` once and pastes literal stdout into signoff under `## Final test surface`. Signoff is data, not narrative — every claim cites a specific command output. No fresh code edits.

## Verification Phases

### `08a-check-signoff-content`
- **Type**: `check`
- **Bounce target**: `08-final-signoff`
- **Purpose**: confirm the signoff exists with the required headers, lists the Phase 1–7 commit SHA range, and cites the current companion HEAD.
- **Commands**:
  ```
  test -f .plan/parity-signoff-honest.md
  test "$(grep -c '^## ' .plan/parity-signoff-honest.md)" -ge 7
  grep -F '## Final test surface'        .plan/parity-signoff-honest.md
  grep -F '## Coverage outcome'          .plan/parity-signoff-honest.md
  grep -F '## Mutation canary outcome'   .plan/parity-signoff-honest.md
  grep -F '## Anti-cheat outcome'        .plan/parity-signoff-honest.md
  grep -F '## Classified divergences'    .plan/parity-signoff-honest.md
  grep -F '## Companion SHA'             .plan/parity-signoff-honest.md
  grep -F '## Open risks'                .plan/parity-signoff-honest.md
  COMPANION_SHA=$(git -C ../openglad-master rev-parse HEAD)
  grep -F "$COMPANION_SHA" .plan/parity-signoff-honest.md
  PHASES_COUNT=$(git log --grep='parity-finish-3' --oneline | wc -l)
  test "$PHASES_COUNT" -ge 8
  ```

### `08b-check-full-bundle-green`
- **Type**: `check`
- **Bounce target**: `08-final-signoff`
- **Purpose**: confirm the full repo test suite is green, parity bundle is green, and `og_test_parity` is zero-FAIL zero-SKIP with PASSED ≥ 130.
- **Commands**:
  ```
  cmake --build --preset ci-test
  ctest --preset ci-test --output-on-failure
  scripts/parity/ci_parity.sh
  scripts/parity/anti_cheat_selftest.sh
  build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p08.out
  FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p08.out)
  SKIPPED_LINES=$(grep -cE '^\[  SKIPPED \] Parity\.' /tmp/p08.out)
  PASSED=$(grep -oE '^\[  PASSED  \] [0-9]+ tests?\.' /tmp/p08.out | awk '{print $3}' | head -1)
  test "$FAILED"        -eq 0
  test "$SKIPPED_LINES" -eq 0
  test "$PASSED" -ge 130
  ```

### `08c-check-ci-yaml-wired-if-present`
- **Type**: `check`
- **Bounce target**: `08-final-signoff`
- **Purpose**: if `.github/workflows/test.yml` exists, confirm it invokes `ci_parity.sh` / `anti_cheat_selftest.sh`; otherwise confirm the signoff documents the CI invocation.
- **Commands**:
  ```
  if test -f .github/workflows/test.yml; then
    grep -E 'ci_parity\.sh|anti_cheat_selftest\.sh' .github/workflows/test.yml
  else
    grep -F '## How to run in CI' .plan/parity-signoff-honest.md
    grep -F 'scripts/parity/ci_parity.sh' .plan/parity-signoff-honest.md
  fi
  ```

## Success Criteria

- `.plan/parity-signoff-honest.md` exists with at least seven `## ` section headers covering the required topics; lists the companion HEAD and the Phase 1–7 commit range.
- Full repo test suite (`ctest --preset ci-test`) passes; parity bundle (`ci_parity.sh`) passes; anti-cheat self-test passes.
- `og_test_parity` zero-FAIL zero-SKIP with PASSED ≥ 130.
- Either `.github/workflows/test.yml` invokes the new scripts OR the signoff documents the CI invocation under `## How to run in CI`.

## Git Commit Requirement

The implementer **must** commit the signoff doc (and any CI workflow edit) before yielding:

```
git add .plan/parity-signoff-honest.md
# optionally: git add .github/workflows/test.yml
git commit -m "parity-finish-3: phase 08 — honest signoff; bundle green"
```
