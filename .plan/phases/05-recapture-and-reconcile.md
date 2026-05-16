# Phase 05 — Recapture and reconcile every golden

**Phase Name**: Run the pinned companion against every scenario (old +
new); commit the canonical golden set.

**Implement Phase ID**: `05-recapture-and-reconcile`

## Preexisting Inputs

- `.plan/parity-recapture-diff.md` (identifies divergent goldens)
- `tests/parity/scenario_table.h` (Phase 4-extended)
- `../openglad-master/tools/parity_scenario_table.h` (mirror; SHA-equal)
- `../openglad-master/build/ci-test/parity_dump_master`
- `tests/parity/golden/*.json` (existing 39 files)
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`
- `scripts/parity/diff_dumps.py`

## New Outputs

- Refreshed `tests/parity/golden/*.json` — every scenario in `kScenarios`
  with `is_branch_internal == false` has a canonical golden at
  `tests/parity/golden/<id>.json`. Phase-4 new scenarios added;
  Phase-2 divergent goldens replaced in place; byte-equal goldens
  unchanged.
- `.plan/parity-second-divergence-report.md` — per-golden recapture &
  predicate-evaluation report (replaces empty-world report as current
  authority). Required sections:
  - **Header**: pinned companion SHA, branch HEAD SHA, total golden
    count, golden-replace count.
  - **Per-golden replacement log**: one row per replaced golden,
    columns `scenario_id | bytes_before | bytes_after | reason (recapture-diff / new-scenario)`.
  - **Predicate-evaluation table**: per scenario, each `expected_facts[]`
    entry and its branch / master evaluation on the new golden. Rows
    where one side passes and the other fails are `regression`
    candidates for Phase 6.
  - **Classified divergences**: every dump field where semantic
    evaluation differs. One of:
    - `regression` (Phase 6 fixes; cite suspect branch commit range via
      `git log origin/master..HEAD -- src/...`).
    - `intended_diff` (cited branch commit explicitly authorising).
    - `rng_drift` (RNG draws differ but every predicate still holds).
- Updated `.plan/parity-coverage-manifest.md` "Phase X sign-off snapshot"
  section reflecting full coverage outcome.

## File Changes

- `tests/parity/golden/*.json` (replace/add as needed).
- Create `.plan/parity-second-divergence-report.md`.
- Modify `.plan/parity-coverage-manifest.md` (sign-off snapshot).
- Branch commit: `parity-finish-2: phase 05 — recapture goldens against companion <sha>; <N> replaced, <M> added`.

## Implementation Details

```bash
COMPANION_SHA=$(git -C ../openglad-master rev-parse HEAD)
diff <(sha1sum tests/parity/scenario_table.h | awk '{print $1}') \
     <(sha1sum ../openglad-master/tools/parity_scenario_table.h | awk '{print $1}')
mkdir -p /tmp/golden_capture
for id in $(../openglad-master/build/ci-test/parity_dump_master --list); do
    ../openglad-master/build/ci-test/parity_dump_master \
        --scenario "$id" --out "/tmp/golden_capture/$id.json"
    python3 scripts/parity/validate_schema.py "/tmp/golden_capture/$id.json"
done
for f in /tmp/golden_capture/*.json; do
    id=$(basename "$f" .json)
    target="tests/parity/golden/$id.json"
    if [ ! -f "$target" ] || ! cmp -s "$f" "$target"; then
        cp "$f" "$target"
    fi
done
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity
```

Predicates are NOT widened to accommodate a fresh golden. A predicate
failing on a fresh golden is a Phase 3 work item; the verifier failure
bounces the implementer to redo predicate work as needed.

## Verification Phases

- **`05a-check-golden-count`** (`check`, `bounce_target: 05-recapture-and-reconcile`):
  Purpose: assert every non-internal scenario has a schema-valid golden.
  Commands:
  - `python3 - <<'PY'` parses `tests/parity/scenario_table.h` to count
    `is_branch_internal == false` scenarios; assert
    `ls tests/parity/golden/*.json | wc -l` equals that count.
  - For each `f` in `tests/parity/golden/*.json`,
    `python3 scripts/parity/validate_schema.py "$f"` exits 0.

- **`05b-check-recapture-fresh`** (`check`, `bounce_target: 05-recapture-and-reconcile`):
  Purpose: prove on-disk goldens equal a freshly-captured set.
  Commands:
  - Re-run `parity_dump_master` for every scenario into `/tmp/recheck/`.
  - `cmp -s` each `/tmp/recheck/<id>.json` against
    `tests/parity/golden/<id>.json`. Every golden byte-equal. Any diff
    fails this check.

- **`05c-check-tests-green-against-new-goldens`** (`check`, `bounce_target: 05-recapture-and-reconcile`):
  Purpose: tightened predicates from Phase 3 still hold on the new goldens.
  Commands:
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity` — every case passes. A failure means
    a Phase-3 widening retreat is required; verifier exits non-zero.

## Success Criteria

- `tests/parity/golden/*.json` count equals the master-comparable
  scenario count.
- Every golden validates against `validate_schema.py` and is byte-equal
  to a fresh recapture.
- All `Parity.*` tests pass against the new goldens.
- `.plan/parity-second-divergence-report.md` classifies every divergence
  as `regression`, `intended_diff`, or `rng_drift`.

## Git Commit Requirement

The implementer MUST `git add` the new/replaced goldens, the new
divergence report, and the manifest update, then `git commit` with
message `parity-finish-2: phase 05 — recapture goldens against companion <sha>; <N> replaced, <M> added`
**before yielding**. The next check phases assert HEAD contains those
file changes via `git log -1 --name-status`.
