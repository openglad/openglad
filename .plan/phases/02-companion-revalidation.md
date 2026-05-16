# Phase 02 — Master companion re-validation and SHA pinning

**Phase Name**: Rebuild master companion at a fresh SHA; pin in docs;
recapture-vs-existing diff across all 39 goldens.

**Implement Phase ID**: `02-companion-revalidation`

## Preexisting Inputs

- `.plan/parity-honest-audit.md` (Phase 1)
- `.plan/parity-coverage-manifest.md` (SHA to reconcile)
- `.plan/master-companion.md` (SHA reference to reconcile)
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json` (39 files; not deleted)
- `../openglad-master/tools/parity_scenario_table.h`
- `../openglad-master/tools/parity_dump_master.cpp`
- `../openglad-master/tools/parity_dump_state.{h,cpp}`
- `../openglad-master/tools/parity_bootstrap.{h,cpp}`
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`
- `scripts/parity/diff_dumps.py`

## New Outputs

- `.plan/parity-recapture-diff.md` — per-golden recapture result. Sections:
  - **Header**: pinned companion SHA (current HEAD of `parity-companion`),
    branch HEAD SHA, branch-side `tests/parity/scenario_table.h` SHA-1,
    companion-side `tools/parity_scenario_table.h` SHA-1. Two table
    SHA-1s MUST be equal.
  - **Per-golden recapture diff** table — one row per master-comparable
    scenario (38 rows). Columns:
    `scenario_id | bytes_before | bytes_after | result (byte-equal/diff) | notes`.
    `result == diff` rows include a one-line summary of which fields
    changed (RNG state, events count, walker count).
  - **Outcome summary**: count of byte-equal vs diff vs schema-invalid rows.
- Updated `.plan/parity-coverage-manifest.md` frontmatter
  `master_companion_sha:` → actual companion HEAD.
- Updated `.plan/master-companion.md` — refresh SHA tables and
  "Drift-detection SHA-1s" section to current values.
- If `tests/parity/scenario_table.h` SHA differs from companion mirror:
  copy branch → master, commit on master, re-run. This is the ONLY
  `tests/parity/` write Phase 2 may perform. Goldens are NOT modified
  in this phase (Phase 5 does the replacement); Phase 2 produces only
  the diff log.

## File Changes

- Modify `.plan/parity-coverage-manifest.md` (frontmatter only).
- Modify `.plan/master-companion.md` (SHA tables and "Drift-detection" rows).
- Create `.plan/parity-recapture-diff.md`.
- If needed (table SHA mismatch):
  `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
  and commit on `../openglad-master` (`parity-companion` branch).
- Branch commit: `parity-finish-2: phase 02 — companion revalidation; pinned SHA <hash>; <N> goldens diverge from recapture`.
- Master commit (if any): `parity-companion: phase 02 — mirror scenario_table.h SHA-1 from branch <hash>`.

## Implementation Details

```bash
cd /home/yans/code/openglad-master
git checkout parity-companion
cmake --preset ci-test
cmake --build --preset ci-test --target parity_dump_master
COMPANION_SHA=$(git rev-parse HEAD)
cd /home/yans/code/openglad
mkdir -p /tmp/recapture
for id in $(../openglad-master/build/ci-test/parity_dump_master --list); do
    ../openglad-master/build/ci-test/parity_dump_master \
        --scenario "$id" --out "/tmp/recapture/$id.json"
    python3 scripts/parity/validate_schema.py "/tmp/recapture/$id.json"
done
for f in tests/parity/golden/*.json; do
    id=$(basename "$f" .json)
    if [ -f "/tmp/recapture/$id.json" ]; then
        if cmp -s "$f" "/tmp/recapture/$id.json"; then
            echo "$id byte-equal"
        else
            echo "$id diff:"
            diff <(python3 -m json.tool "$f") <(python3 -m json.tool "/tmp/recapture/$id.json") | head
        fi
    fi
done
```

The two table SHA-1s on branch/companion sides MUST match before
recapture starts; if not, the agent first mirrors the branch table to
the companion and commits on master.

## Verification Phases

- **`02a-check-companion-build`** (`check`, `bounce_target: 02-companion-revalidation`):
  Purpose: verify companion build artefact exists and table SHA-1s match.
  Commands:
  - `cd ../openglad-master && cmake --build --preset ci-test --target parity_dump_master` exits 0.
  - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
  - `sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
    — both SHA-1s must match.

- **`02b-check-companion-list-matches`** (`check`, `bounce_target: 02-companion-revalidation`):
  Purpose: ensure companion exposes every non-internal scenario id.
  Commands:
  - `../openglad-master/build/ci-test/parity_dump_master --list > /tmp/cmaster_ids.txt`.
  - `python3 - <<'PY'` extracts every `kScenarios[].id` where
    `is_branch_internal == false` from `tests/parity/scenario_table.h`.
  - Diff the two sets; ordering free; sets must be equal.

- **`02c-check-recapture-diff-log`** (`check`, `bounce_target: 02-companion-revalidation`):
  Purpose: assert the diff log is non-trivial and the commit message
  embeds the pinned SHA.
  Commands:
  - `test -f .plan/parity-recapture-diff.md`.
  - `grep -q '^## Per-golden recapture diff' .plan/parity-recapture-diff.md`.
  - Row count equals `kMasterComparableScenarioCount` (currently 38).
  - `grep -c '^| ' .plan/parity-recapture-diff.md` ≥ 38.
  - `grep -E '^\| [a-z_0-9]+_scen[0-9]+ +\| (byte-equal|diff)' .plan/parity-recapture-diff.md | wc -l`
    matches the row count.
  - Phase 2 commit message contains the literal current companion SHA:
    `git log -1 --pretty=%B` matches
    `git -C ../openglad-master rev-parse HEAD`.
  - `grep -q "master_companion_sha: $(git -C ../openglad-master rev-parse HEAD)" .plan/parity-coverage-manifest.md`.

## Success Criteria

- `parity_dump_master` rebuilds cleanly at the current companion HEAD.
- Branch `scenario_table.h` and companion mirror SHA-1s match.
- Companion `--list` set equals the branch's non-internal scenario id set.
- `.plan/parity-recapture-diff.md` contains exactly
  `kMasterComparableScenarioCount` rows, each tagged byte-equal or diff,
  with an outcome summary.
- Manifest frontmatter `master_companion_sha:` equals
  `git -C ../openglad-master rev-parse HEAD`.
- Branch HEAD commit message embeds the pinned companion SHA.

## Git Commit Requirement

Two-worktree phase. The implementer MUST:

- `git add` the modified docs (`.plan/parity-coverage-manifest.md`,
  `.plan/master-companion.md`, `.plan/parity-recapture-diff.md`) and
  `git commit` on the branch with the
  `parity-finish-2: phase 02 — …` message before yielding.
- If the companion mirror was updated, also
  `git -C ../openglad-master add tools/parity_scenario_table.h`
  and `git -C ../openglad-master commit -m "parity-companion: phase 02 — mirror scenario_table.h SHA-1 from branch <hash>"`
  before yielding.

Check `02a` and `02c` verify both HEADs via `git log -1 --name-status`
and `git -C ../openglad-master log -1 --name-status`.
