# Phase 08 — Final honest sign-off

**Phase Name**: Write the final sign-off; close the loop on the user's goal.

**Implement Phase ID**: `08-final-signoff`

## Preexisting Inputs

- `.plan/parity-honest-audit.md`
- `.plan/parity-recapture-diff.md`
- `.plan/parity-second-divergence-report.md`
- `.plan/parity-second-fixes.md`
- `.plan/parity-canary-exemptions.md`
- `.plan/parity-coverage-manifest.md`
- `tests/parity/golden/*.json`
- `scripts/parity/ci_parity.sh`

## New Outputs

- `.plan/parity-signoff-honest.md` — final sign-off document. Includes a
  one-line statement:

  > *"Parity overall: GREEN. Every required family, event kind, weapon,
  > treasure, FX, and special is exercised by at least one scenario
  > whose `expected_facts[]` predicate constrains its behaviour; the
  > mutation canary flips ≥1 predicate per non-exempt row; the
  > recapture verifier confirms every golden was produced by companion
  > SHA <pinned>."*

  All other claims cite specific verifier outputs from Phase 7's
  `ci_parity.sh`. Sign-off lists every Phase 1 → Phase 7 commit SHA
  range. Required sections (each header present and body non-empty):
  - `## Final test surface` — test cases and pass/fail
  - `## Coverage outcome` — every required family / event / special
    backed by a behavioural predicate
  - `## Mutation canary outcome` — flip count per row
  - `## Classified divergences` — final per-row classification
  - `## Anti-cheating locks` — names of every check catching future
    regression
  - `## Open risks` — partial coverage carry-overs (e.g. on-disk save
    round-trip if still untested)

## File Changes

- Create `.plan/parity-signoff-honest.md`.
- Branch commit: `parity-finish-2: phase 08 — honest signoff`.

## Implementation Details

Agent runs the full CI bundle once more and writes the document from
actual output. Sign-off lists each test name as it appears in
`og_test_parity --gtest_list_tests` and the exact pass counts. SHA
ranges are pulled from `git log` between the Phase-1 commit and HEAD.

```bash
scripts/parity/ci_parity.sh
cmake --build --preset ci-test && ctest --preset ci-test --output-on-failure
build/ci-test/og_test_parity --gtest_list_tests > /tmp/final_tests.txt
build/ci-test/og_test_parity --gtest_brief=1     > /tmp/final_run.txt
git log --oneline origin/master..HEAD            > /tmp/final_commits.txt
```

## Verification Phases

- **`08a-check-signoff-content`** (`check`, `bounce_target: 08-final-signoff`):
  Purpose: ensure the sign-off doc exists, has all required sections,
  and was committed.
  Commands:
  - `test -f .plan/parity-signoff-honest.md`.
  - For each header
    (`## Final test surface`, `## Coverage outcome`,
     `## Mutation canary outcome`, `## Classified divergences`,
     `## Anti-cheating locks`, `## Open risks`):
    `grep -q "^<header>$" .plan/parity-signoff-honest.md` and the
    section body (next non-blank line through next `## `) is non-empty.
  - Sign-off lists every Phase 1 → Phase 7 commit SHA range
    (verifier extracts SHAs and matches against
    `git log origin/master..HEAD --pretty=%H`).
  - `git log -1 --name-status | grep -q parity-signoff-honest.md`.

- **`08b-check-full-suite-green`** (`check`, `bounce_target: 08-final-signoff`):
  Purpose: terminal smoke-test of the whole bundle.
  Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test --output-on-failure`
    exits 0.
  - `scripts/parity/ci_parity.sh` exits 0.

## Success Criteria

- `.plan/parity-signoff-honest.md` exists with all six required section
  headers and non-empty bodies; the one-line green statement is present
  and cites the pinned companion SHA.
- Full ctest exits 0; `ci_parity.sh` exits 0.
- HEAD commit lists the new sign-off doc.

## Git Commit Requirement

The implementer MUST `git add .plan/parity-signoff-honest.md` and
`git commit` with message `parity-finish-2: phase 08 — honest signoff`
**before yielding**. The next check phase asserts HEAD contains the
sign-off via `git log -1 --name-status`.
