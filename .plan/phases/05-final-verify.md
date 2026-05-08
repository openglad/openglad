# Phase 5 — Final Verify

## Phase Name
`final-verify`

## Implement Phase ID
`05-final-verify`

## Preexisting Inputs
- Rebased + green branch from Phases 2–4 (`ahead >= 329, behind=0`, build/tests green at end of Phase 4).
- `.plan/findings.md` containing the `## Pre-rebase snapshot` section from Phase 1 (and possibly a `## Known pre-existing test failures` section from Phase 4).
- Existing CMake build tree at the repo root for the sanity re-run.
- `.plan/plan.md` §1 substitution table and operating rule (used only if the sanity re-run regresses).
- `origin/master` ref (re-fetched in this phase).

## New Outputs
- A `## Final rebase summary` section appended to `.plan/findings.md` recording: post-rebase HEAD SHA, count of commits ahead of `origin/master`, count of fixup commits added in Phases 3–4, links to any test failures recorded as pre-existing.
- One commit with subject `rebase: record final findings` adding this section.
- If the verifier's build-or-test sanity re-run surfaces a regression, additional commits with subject `rebase: fixup after master rebase (phase 5)` applying the §1 substitution table to the offending file(s). Phase 5 must own its own remediation because the verifier bounces only to `05-final-verify` (cross-block bounces are forbidden by the workflow contract).

## File Changes
- `.plan/findings.md` — append-only addition of the `## Final rebase summary` section.
- If the sanity re-run regresses: the offending source/test file(s) under `src/` or `tests/`, with edits limited to applying the §1 substitution table consistent with Phases 3/4.

## Implementation Details
1. Run `cmake --build --preset ci-test` and `ctest --preset ci-test --output-on-failure` as a sanity check.
2. If either fails, treat it identically to Phase 3 / Phase 4: apply the substitution table, commit with subject `rebase: fixup after master rebase (phase 5)`, and re-run. Bound by progress, not commit count: if two consecutive build/ctest runs produce the same error or failure set, yield with the regression recorded in `.plan/findings.md` under `## Known pre-existing test failures`.
3. Append the `## Final rebase summary` section to `.plan/findings.md` with: post-rebase HEAD SHA, ahead count vs `origin/master`, the count of fixup commits added in Phases 3 and 4 (`git log --oneline origin/master..HEAD | grep -c 'rebase: fixup'`), and a list of any pre-existing test failures recorded earlier.
4. `git add .plan/findings.md && git commit -m "rebase: record final findings"`.
5. Confirm `git status --porcelain | grep -v '^?? '` is empty and `.juvenal-state.json` blob still matches HEAD blob before yielding.

## Verification Phases
- **`05a-check-final`** — type `check`, `bounce_target: 05-final-verify`. Purpose: end-to-end sanity that the rebase landed cleanly — branch is based on `origin/master`, `ahead >= 329, behind=0`, no rebase artifacts, build is green, full ctest passes 100%, no conflict markers anywhere, both `## Pre-rebase snapshot` and `## Final rebase summary` sections present in `.plan/findings.md`, `.juvenal-state.json` blocker still cleared, and working tree clean. Runs:
  ```bash
  set -e
  git fetch origin master
  base=$(git merge-base HEAD origin/master)
  om=$(git rev-parse origin/master)
  [ "$base" = "$om" ] || { echo "FAIL: not based on origin/master"; exit 1; }
  ahead=$(git rev-list --count origin/master..HEAD)
  behind=$(git rev-list --count HEAD..origin/master)
  [ "$behind" = "0" ] || { echo "FAIL: behind=$behind"; exit 1; }
  [ "$ahead" -ge 329 ] || { echo "FAIL: ahead=$ahead (expected >= 329)"; exit 1; }
  [ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] || { echo "FAIL: rebase artifacts present"; exit 1; }
  cmake --build --preset ci-test
  ctest --preset ci-test --output-on-failure 2>&1 | tee /tmp/og-ctest-final.log
  grep -qE '100% tests passed' /tmp/og-ctest-final.log || { echo "FAIL: ctest regressed"; exit 1; }
  if grep -RIn -e '^<<<<<<<' -e '^=======' -e '^>>>>>>>' src tests include 2>/dev/null; then
    echo "FAIL: conflict markers remain"; exit 1
  fi
  grep -q '^## Final rebase summary' .plan/findings.md || { echo "FAIL: final summary missing"; exit 1; }
  grep -q '^## Pre-rebase snapshot' .plan/findings.md || { echo "FAIL: pre-rebase snapshot missing"; exit 1; }
  wt_blob=$(git hash-object .juvenal-state.json)
  head_blob=$(git rev-parse HEAD:.juvenal-state.json)
  [ "$wt_blob" = "$head_blob" ] || { echo "FAIL: .juvenal-state.json blob drifted from HEAD"; exit 1; }
  dirty=$(git status --porcelain | grep -v '^?? ' || true)
  [ -z "$dirty" ] || { echo "FAIL: uncommitted changes:"; echo "$dirty"; exit 1; }
  echo "OK"
  ```

## Success Criteria
- `git merge-base HEAD origin/master == git rev-parse origin/master`.
- `ahead >= 329`, `behind=0`.
- No `.git/rebase-merge/` or `.git/rebase-apply/` directory present.
- `cmake --build --preset ci-test` exits 0 and `ctest --preset ci-test` reports `100% tests passed`.
- No conflict markers anywhere under `src/`, `tests/`, `include/`.
- `.plan/findings.md` contains both the `## Pre-rebase snapshot` and `## Final rebase summary` sections.
- `.juvenal-state.json` working-tree blob equals HEAD blob; `skip-worktree` still set.
- Working tree has no uncommitted tracked changes.

## Git Commit Requirement
The implementer MUST commit the final findings entry as its own commit (subject `rebase: record final findings`) before yielding, plus any sanity-rerun fixup commits (subject `rebase: fixup after master rebase (phase 5)`) on their own. Do not amend earlier commits. After yielding, the working tree must have no uncommitted tracked changes.
