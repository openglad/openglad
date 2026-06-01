# Phase 02 — Mirror resync and companion rebuild

## Phase Name
Make `../openglad-master/tools/parity_scenario_table.h` byte-equal with branch; rebuild master companion.

## Implement Phase ID
`02-mirror-resync`

## Preexisting Inputs
- `tests/parity/scenario_table.h` (branch, current).
- `tests/parity/state_dump.{h,cpp}` (branch).
- `../openglad-master/tools/parity_scenario_table.h` (possibly stale).
- `../openglad-master/tools/parity_dump_state.{cpp,h}`.
- `../openglad-master/build/` directory.
- `.plan/parity-present-state.md` (from phase 01).
- `.plan/parity-coverage-manifest.md` (from phase 01).
- `.plan/master-companion.md`.
- `scripts/parity/capture_master_golden.sh`.
- `scripts/parity/validate_schema.py`.

## New Outputs
- `../openglad-master/tools/parity_scenario_table.h` byte-equal to branch.
- `../openglad-master/build/ci-test/parity_dump_master` rebuilt.
- `.plan/master-companion.md` updated with the new pin (post-commit SHA of `../openglad-master`).
- `.plan/parity-coverage-manifest.md` `master_companion_sha:` updated to the new SHA.

## File Changes
- `cp -f tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.
- `cmake --build ../openglad-master/build/ci-test --target parity_dump_master`.
- `.plan/master-companion.md` (SHA pin updated).
- `.plan/parity-coverage-manifest.md` (`master_companion_sha:` updated).

## Implementation Details
1. Confirm no uncommitted branch state (`git status --porcelain` clean modulo permitted ignores).
2. `cp -f tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.
3. Build the companion binary: `cmake --build ../openglad-master/build/ci-test --target parity_dump_master` (rerun the CMake configure step on `../openglad-master/` if necessary so the target is known).
4. Companion-side commit (in `../openglad-master`):
   - `git -C ../openglad-master add tools/parity_scenario_table.h`.
   - `git -C ../openglad-master commit -m "parity-companion: phase 02 — mirror scenario_table.h to <branch-sha>"` where `<branch-sha>` is the branch HEAD just prior to this phase's branch commit.
5. Capture the new companion SHA: `SHA=$(git -C ../openglad-master rev-parse HEAD)`.
6. Edit `.plan/master-companion.md` and `.plan/parity-coverage-manifest.md` to use the new SHA.
7. Branch-side commit (see Git Commit Requirement).

## Verification Phases

### `02a-check-mirror-sha-equal`
- Type: `check`
- Bounce target: `02-mirror-resync`
- Purpose: Branch and companion `parity_scenario_table.h` are byte-identical.
- Commands:
  - `sha1sum tests/parity/scenario_table.h | awk '{print $1}'` must equal `sha1sum ../openglad-master/tools/parity_scenario_table.h | awk '{print $1}'`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

### `02b-check-companion-binary-fresh`
- Type: `check`
- Bounce target: `02-mirror-resync`
- Purpose: Companion binary exists, is executable, and emits schema-valid JSON for a known small scenario.
- Commands:
  - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
  - `../openglad-master/build/ci-test/parity_dump_master smoke_empty_scen99 > /tmp/p02-dump.json`.
  - `python3 scripts/parity/validate_schema.py /tmp/p02-dump.json` exits 0.

### `02c-check-sha-pin-doc-consistent`
- Type: `check`
- Bounce target: `02-mirror-resync`
- Purpose: Documentation pins match the live companion SHA, and the companion commit lists the mirror file.
- Commands:
  - `SHA=$(git -C ../openglad-master rev-parse HEAD)`.
  - `grep -F "$SHA" .plan/master-companion.md` exits 0.
  - `grep -F "master_companion_sha: $SHA" .plan/parity-coverage-manifest.md` exits 0.
  - `git -C ../openglad-master log -1 --name-status` contains `tools/parity_scenario_table.h`.

## Success Criteria
- All three check phases (`02a`, `02b`, `02c`) pass.
- Branch and companion scenario_table.h byte-equal.
- Companion binary built and schema-valid.
- SHA pin consistent across `.plan/master-companion.md` and `.plan/parity-coverage-manifest.md`.

## Git Commit Requirement
The implementer MUST commit on BOTH worktrees before yielding.

Companion (in `../openglad-master`):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 02 — mirror scenario_table.h to <branch-sha>"
```

Branch (in `/home/yans/code/openglad`):
```
git add .plan/master-companion.md .plan/parity-coverage-manifest.md
git commit -m "parity-cov: phase 02 — resync companion mirror"
```
