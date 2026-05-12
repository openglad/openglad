# Phase 08 — CI Wiring and Honest Sign-off

## Phase Name
Lock the gate, replace the fraudulent sign-off.

## Implement Phase ID
`08-ci-and-honest-signoff`

## Preexisting Inputs
- All outputs of Phases 01-07 (audit doc, real runner/companion,
  coverage manifest + gate, walker / special / residual scenarios with
  goldens, divergence ledger, canary-validated harness).
- `.github/workflows/test.yml`.
- `.plan/parity-coverage-manifest.md`.
- `.plan/parity-redo-audit.md`.
- `.plan/parity-fixes.md`.
- `.plan/parity-signoff-fraudulent.md` (kept for historical reference;
  never cited as authoritative).

## New Outputs
- `.plan/parity-signoff.md` — rewritten from scratch. Required sections:
  1. Tear-down summary citing `.plan/parity-redo-audit.md`.
  2. Real coverage table citing `.plan/parity-coverage-manifest.md`,
     with observed-vs-required counts per category and a non-zero
     golden-file count with `ls -l` evidence.
  3. Divergence ledger citing `.plan/parity-fixes.md`.
  4. Reproduction commands a third party can replay.
  5. Canary perturbation evidence (the Phase 07c sed + ctest sequence).
  6. Master companion commit SHA and branch HEAD SHA the goldens were
     captured against.
  7. Literal excerpt of
     `ctest --preset ci-test -R '^og_test_parity'` showing the full
     pass count.
  None of "vacuously satisfied", "indirectly covered", or "not currently
  registered" may appear.

- `.github/workflows/test.yml` extended with two CI steps that run
  *before* the final ctest pass so a missing target fails CI fast:
  1. "Parity coverage manifest check" —
     `python3 scripts/parity/check_coverage_manifest.py` (pre-build).
  2. "Parity coverage gate" —
     `./build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*' --gtest_color=no`
     (immediately after build). The coverage-gate cases live inside the
     existing `og_test_parity` binary; no separate executable.
  The existing parity-test step still runs the full `og_test_parity`
  group via `ctest --preset ci-test -R '^og_test_parity'`. All steps
  use `if: always()` so a single failure still surfaces the others.

- Final amendment to `.plan/parity-harness-design.md` — add "Phase 08
  redo" section pointing at the rewritten sign-off and explicitly
  superseding any contradictory claim in older sections.

## File Changes
- New: `.plan/parity-signoff.md`.
- Modified: `.github/workflows/test.yml`.
- Modified: `.plan/parity-harness-design.md`.

## Implementation Details
- The agent rewriting the sign-off must run every command it cites
  before writing; verifier `08b` compares cited counts against
  re-execution and fails on stale numbers.
- CI step ordering: manifest check (cheap) before build; coverage gate
  immediately after build; full parity suite as part of the normal
  ctest pass. Each new step has `if: always()`.

## Verification Phases
- **Phase ID**: `08a-check-ci-yaml-runs-coverage`
  - **Type**: `check`
  - **Bounce Target**: `08-ci-and-honest-signoff`
  - **Purpose**: Confirm `.github/workflows/test.yml` contains both the
    manifest-check invocation and a coverage-gate invocation that
    selects the `Parity.coverage_gate*` cases inside `og_test_parity`.
  - **Commands**:
    ```
    grep -q 'check_coverage_manifest.py' .github/workflows/test.yml
    grep -qE "(gtest_filter=['\"]Parity.coverage_gate|ctest .* -R .Parity\\.coverage_gate)" \
        .github/workflows/test.yml
    grep -q "og_test_parity" .github/workflows/test.yml
    ```

- **Phase ID**: `08b-check-signoff-honest`
  - **Type**: `check`
  - **Bounce Target**: `08-ci-and-honest-signoff`
  - **Purpose**: Confirm the rewritten sign-off cites real evidence
    (non-zero golden count, full coverage table, master companion SHA,
    branch HEAD SHA) and contains none of the banned weasel phrases.
  - **Commands**:
    ```
    test -f .plan/parity-signoff.md
    ! grep -qiE 'vacuously|not currently registered|indirectly covered' \
        .plan/parity-signoff.md
    grep -q "branch HEAD: $(git rev-parse HEAD)" .plan/parity-signoff.md
    # Master companion SHA must match the manifest frontmatter.
    sha=$(awk '/master_companion_sha:/ {print $2; exit}' \
        .plan/parity-coverage-manifest.md)
    grep -q "$sha" .plan/parity-signoff.md
    # Re-execute the cited golden count.
    cited=$(grep -oE 'golden files?: *[0-9]+' .plan/parity-signoff.md \
        | head -1 | grep -oE '[0-9]+')
    actual=$(ls tests/parity/golden/*.json | wc -l)
    [ "$cited" = "$actual" ]
    ```

- **Phase ID**: `08c-check-end-to-end-rebuild`
  - **Type**: `check`
  - **Bounce Target**: `08-ci-and-honest-signoff`
  - **Purpose**: Confirm a clean rebuild from scratch passes the
    coverage manifest check, the build, the coverage gate, and the
    full ctest suite end-to-end.
  - **Commands**:
    ```
    rm -rf build/
    python3 scripts/parity/check_coverage_manifest.py
    cmake --preset ci-test
    cmake --build --preset ci-test
    ./build/ci-test/og_test_parity \
        --gtest_filter='Parity.coverage_gate*'
    ctest --preset ci-test --output-on-failure
    ```
    All commands exit 0.

## Success Criteria
- `.github/workflows/test.yml` has both the manifest-check and
  coverage-gate CI steps.
- `.plan/parity-signoff.md` exists, lacks every banned weasel phrase,
  and cites a branch HEAD SHA equal to current HEAD plus a master
  companion SHA matching the manifest frontmatter.
- Cited golden count matches `ls tests/parity/golden/*.json | wc -l`.
- Clean rebuild + full ctest passes end-to-end.

## Git Commit Requirement
The implementer must `git add .github/workflows/test.yml`,
`.plan/parity-signoff.md`, and `.plan/parity-harness-design.md`, then
`git commit -m "parity-redo: phase 08 — CI gate and honest sign-off"`
**before yielding**. The check phase asserts HEAD contains this
commit and that
`grep -q "branch HEAD: $(git rev-parse HEAD)" .plan/parity-signoff.md`
succeeds (i.e. the sign-off cites the very commit that contains it).
