# Phase 3 — Build Verify

## Phase Name
`build-verify`

## Implement Phase ID
`03-build-verify`

## Preexisting Inputs
- Rebased branch from Phase 2 with the test-migration commit on top (`ahead=329, behind=0`).
- Existing CMake build tree at the repo root (untracked `CMakeCache.txt`, `Makefile`, `*.a`, `openglad_*` binaries) — reused for verification builds; do not delete.
- `.plan/plan.md` §1 substitution table and operating rule for any extra accessor migrations that surface only at compile time.
- Module headers `include/openglad/gameplay/walker.h`, `include/openglad/gameplay/statistics.h`, `include/openglad/gameplay/sim_entity.h` — referenced for the operating rule on unfamiliar fields.
- CMake presets file (`CMakePresets.json` / `cmake/presets`) — used only via `cmake --preset ci-test`.

## New Outputs
- Successful build artifact set under `build/ci-test/` including `build/ci-test/openglad`.
- Zero or more follow-up commits with subject `rebase: fixup build after master rebase (phase 3)` if additional accessor migration was missed in Phase 2's whole-file pass and surfaces only at compile time. Each round of fixups is its own commit (do not amend).

## File Changes
- Most likely `tests/test_walker_specials.cpp` for missed accessor migrations. Other files possible if compile errors surface there. Apply additional accessor migrations consistent with the §1 operating rule; do not introduce unrelated edits.

## Implementation Details
1. Run `cmake --preset ci-test` then `cmake --build --preset ci-test`.
2. If compile errors mention `tests/test_walker_specials.cpp` and reference a member as non-existent or private, apply the corresponding accessor substitution. Common edge cases the verbatim substitution may miss: postfix-style usage (`w->busy++`), compound assignment (`w->magicpoints -= cost`), member-access chains via reference instead of pointer, fields not enumerated in the §1 table that master happens to use. For an unfamiliar field, follow the operating rule: `grep -n '<name>' include/openglad/gameplay/walker.h include/openglad/gameplay/statistics.h include/openglad/gameplay/sim_entity.h`.
3. If errors point elsewhere, treat them as in-scope: they reflect the rebase result and must be fixed.
4. After fixes: `git add <fixed files>` and `git commit -m "rebase: fixup build after master rebase (phase 3)"`. Each round of fixups is its own commit (do not amend).
5. Re-run `cmake --build --preset ci-test` until green. Bound by progress, not commit count: if two consecutive build attempts produce the same set of errors (no progress), yield with the build still red.
6. Before yielding, confirm `git status --porcelain | grep -v '^?? '` is empty.

## Verification Phases
- **`03a-check-build`** — type `check`, `bounce_target: 03-build-verify`. Purpose: assert the project compiles cleanly with the CI preset, the `openglad` binary is built, no `error:` / `fatal error` strings appear in build output, and the working tree has no uncommitted tracked changes. Runs:
  ```bash
  set -e
  cmake --preset ci-test
  cmake --build --preset ci-test 2>&1 | tee /tmp/og-build.log
  if grep -E 'error:|fatal error' /tmp/og-build.log >/dev/null; then
    echo "FAIL: compile errors in build log"; exit 1
  fi
  [ -x build/ci-test/openglad ] || { echo "FAIL: build/ci-test/openglad missing"; exit 1; }
  dirty=$(git status --porcelain | grep -v '^?? ' || true)
  [ -z "$dirty" ] || { echo "FAIL: uncommitted changes after Phase 3:"; echo "$dirty"; exit 1; }
  echo "OK"
  ```

## Success Criteria
- `cmake --build --preset ci-test` exits 0.
- No `error:` or `fatal error` strings in the build log.
- `build/ci-test/openglad` exists and is executable.
- Working tree has no uncommitted tracked changes.

## Git Commit Requirement
The implementer MUST commit any build-fixup work as its own commit (subject `rebase: fixup build after master rebase (phase 3)`) before yielding. Do not amend earlier commits. After yielding, the working tree must have no uncommitted tracked changes.
