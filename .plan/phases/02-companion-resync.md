# Phase 02 — Re-sync the master companion mirror

**Phase Name**: Bring `../openglad-master/tools/parity_scenario_table.h` byte-equal to branch; rebuild `parity_dump_master`; reconcile SHAs.

**Implement Phase ID**: `02-companion-resync`

## Preexisting Inputs

- `.plan/parity-present-state.md` (Phase 1)
- `.plan/parity-coverage-manifest.md` (out-of-date `master_companion_sha`)
- `.plan/master-companion.md` (out-of-date SHA tables)
- `tests/parity/scenario_table.h` (committed at end of Phase 1)
- `../openglad-master/tools/parity_scenario_table.h` (stale mirror)
- `../openglad-master/build/ci-test/parity_dump_master` (stale binary)
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`

## New Outputs

- Updated `../openglad-master/tools/parity_scenario_table.h` (byte-equal to branch).
- Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.
- Updated `.plan/parity-coverage-manifest.md` frontmatter `master_companion_sha:` line.
- Updated `.plan/master-companion.md` body `## Drift-detection SHA-1s` section.
- Branch commit: `parity-finish-3: phase 02 — resync companion mirror; pinned <sha>`.
- Companion commit: `parity-companion: phase 02 — mirror scenario_table.h SHA <branch-sha>`.

## File Changes

- `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
- `git -C ../openglad-master add tools/parity_scenario_table.h`
- `git -C ../openglad-master commit -m "parity-companion: phase 02 — mirror scenario_table.h SHA <branch-sha>"`
- `cd /home/yans/code/openglad-master && cmake --build --preset ci-test --target parity_dump_master`
- Edit `.plan/parity-coverage-manifest.md` `master_companion_sha:` line.
- Edit `.plan/master-companion.md` `## Drift-detection SHA-1s` section.
- `git add .plan/parity-coverage-manifest.md .plan/master-companion.md`
- `git commit -m "parity-finish-3: phase 02 — resync companion mirror; pinned <sha>"`

## Implementation Details

The branch file at this point includes the WIP treasure-row rewrite committed in Phase 1. Both `.plan/` docs are updated to the post-rebuild companion HEAD.

**Master-side commit sequence (literal):**

```
BRANCH_SHA=$(git rev-parse HEAD)
cp tests/parity/scenario_table.h \
   ../openglad-master/tools/parity_scenario_table.h
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m \
  "parity-companion: phase 02 — mirror scenario_table.h SHA ${BRANCH_SHA}"
cd /home/yans/code/openglad-master && \
  cmake --build --preset ci-test --target parity_dump_master && \
  cd /home/yans/code/openglad
```

The companion commit message literally embeds the branch HEAD SHA captured before the `cp`. Verifier 02c re-derives the SHA and grep-matches the companion's `git log -1 --pretty=%B`.

## Verification Phases

### `02a-check-mirror-sha-equal`
- **Type**: `check`
- **Bounce target**: `02-companion-resync`
- **Purpose**: confirm branch and companion `scenario_table.h` are byte-identical.
- **Commands**:
  ```
  sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h \
    | awk '{print $1}' | sort -u | wc -l                                # expect 1
  diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h    # exit 0
  ```

### `02b-check-companion-binary-fresh`
- **Type**: `check`
- **Bounce target**: `02-companion-resync`
- **Purpose**: confirm the companion dumper rebuilt successfully and lists the same scenarios as the branch table.
- **Commands**:
  ```
  cd /home/yans/code/openglad-master && cmake --build --preset ci-test --target parity_dump_master
  test -x ../openglad-master/build/ci-test/parity_dump_master
  BRANCH_COUNT=$(python3 -c "
  from pathlib import Path
  from scripts.parity.lint_scenario_facts import _load_table, parse_scenarios
  print(len(parse_scenarios(_load_table(Path('tests/parity/scenario_table.h')))))
  ")
  COMPANION_COUNT=$(../openglad-master/build/ci-test/parity_dump_master --list | wc -l)
  test "$BRANCH_COUNT" = "$COMPANION_COUNT"
  ```

### `02c-check-doc-sha-reconciled`
- **Type**: `check`
- **Bounce target**: `02-companion-resync`
- **Purpose**: confirm `.plan/parity-coverage-manifest.md` and `.plan/master-companion.md` cite the current companion HEAD, and both worktrees have a commit on top.
- **Commands**:
  ```
  grep '^master_companion_sha: ' .plan/parity-coverage-manifest.md | wc -l           # expect 1
  test "$(grep '^master_companion_sha: ' .plan/parity-coverage-manifest.md | awk '{print $2}')" \
     = "$(git -C ../openglad-master rev-parse HEAD)"
  grep -c "$(git -C ../openglad-master rev-parse HEAD)" .plan/master-companion.md     # >= 1
  git log -1 --name-status | grep -F .plan/parity-coverage-manifest.md
  git log -1 --name-status | grep -F .plan/master-companion.md
  git -C ../openglad-master log -1 --name-status | grep -F tools/parity_scenario_table.h
  git -C ../openglad-master log -1 --pretty=%B | grep -F "$(git rev-parse HEAD)"
  ```

## Success Criteria

- `sha1sum` shows one unique digest across branch and companion `scenario_table.h`.
- Companion `parity_dump_master --list` line count equals the branch scenario count parsed via the lint script.
- `.plan/parity-coverage-manifest.md` and `.plan/master-companion.md` both name the current companion HEAD.
- Branch HEAD touches both doc files; companion HEAD touches the mirror.

## Git Commit Requirement

The implementer **must** create both commits before yielding:
1. Companion commit `parity-companion: phase 02 — mirror scenario_table.h SHA <branch-sha>` on `../openglad-master`.
2. Branch commit `parity-finish-3: phase 02 — resync companion mirror; pinned <sha>` on the current worktree.

The companion commit must literally embed the branch-side HEAD SHA captured before the `cp`. The companion binary must be rebuilt after the companion commit lands.
