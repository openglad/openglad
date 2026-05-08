# Phase 4 — Test Verify

## Phase Name
`test-verify`

## Implement Phase ID
`04-test-verify`

## Preexisting Inputs
- Built binaries from Phase 3 under `build/ci-test/`.
- Rebased branch with all previous fixups committed.
- The 42 new `TEST_F(WalkerSpecials, ...)` cases brought in by master (40 from second-region append + 2 from first-region insertion).
- `.plan/plan.md` §1 substitution table for any further accessor adjustment.
- `.plan/findings.md` — appended in this phase only if pre-existing branch test failures unrelated to the rebase are encountered.

## New Outputs
- A passing `ctest --preset ci-test` run reporting `100% tests passed`.
- Zero or more follow-up commits with subject `rebase: fixup tests after master rebase (phase 4)` if a test fails because the accessor migration in Phase 2 left a behavior gap.

## File Changes
- Only if test failures point to `tests/test_walker_specials.cpp` lines requiring further accessor adjustment, or to the resolved `effect_family_ghost_scare.cpp` not actually returning `false` in all branches. Adjustments stay in those two files.
- `.plan/findings.md` — append a `## Known pre-existing test failures` section only if pre-existing branch test failures unrelated to the rebase are encountered. Do not mask failures by editing tests or skipping them.

## Implementation Details
1. Run `ctest --preset ci-test --output-on-failure`.
2. For each `og_unit_*` or `og_test_*` failure, identify whether the failure is caused by:
   - (a) a missed accessor substitution in the rebase resolution,
   - (b) a divergence between the branch's accessor semantics and master's expected field semantics (e.g. `set_busy(0)` vs `busy = 0` differ in side-effects), or
   - (c) a pre-existing branch test failure unrelated to the rebase.
3. For (a): apply the missing substitution.
4. For (b): adjust the master-supplied test setup to call the appropriate setter sequence to reproduce master's intended precondition. Document the change in the commit message.
5. For (c): leave the failure in place but record it in `.plan/findings.md` under `## Known pre-existing test failures` and surface it to the operator. Do not mask failures by editing tests or skipping them. The verifier will still fail and bounce; the operator decides whether to override.
6. Commit any fixes (`git add <files>` + `git commit -m "rebase: fixup tests after master rebase (phase 4)"`) before yielding. Confirm `git status --porcelain | grep -v '^?? '` is empty.
7. Bound by progress, not commit count: if two consecutive ctest runs produce the same set of failing test cases (no progress), yield with tests still red.

## Verification Phases
- **`04a-check-tests`** — type `check`, `bounce_target: 04-test-verify`. Purpose: assert the full ctest preset passes 100% and the working tree has no uncommitted tracked changes. Runs:
  ```bash
  set -e
  ctest --preset ci-test --output-on-failure 2>&1 | tee /tmp/og-ctest.log
  grep -qE '100% tests passed' /tmp/og-ctest.log || { echo "FAIL: not 100% tests passed"; tail -40 /tmp/og-ctest.log; exit 1; }
  dirty=$(git status --porcelain | grep -v '^?? ' || true)
  [ -z "$dirty" ] || { echo "FAIL: uncommitted changes after Phase 4:"; echo "$dirty"; exit 1; }
  echo "OK"
  ```

## Success Criteria
- `ctest --preset ci-test --output-on-failure` exits 0 and the log contains `100% tests passed`.
- Working tree has no uncommitted tracked changes.
- Any pre-existing failures unrelated to the rebase are documented in `.plan/findings.md` (verifier will still bounce in this case; operator override required).

## Git Commit Requirement
The implementer MUST commit any test-fixup work as its own commit (subject `rebase: fixup tests after master rebase (phase 4)`) before yielding. Do not amend earlier commits. After yielding, the working tree must have no uncommitted tracked changes.
