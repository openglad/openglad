# Phase 03 — Mass golden capture

**Phase Name**: Capture a fresh master golden for every master-comparable scenario in `kScenarios`; commit replacements and additions.

**Implement Phase ID**: `03-mass-golden-capture`

## Preexisting Inputs

- `tests/parity/scenario_table.h` (Phase 1 committed, Phase 2 mirrored)
- `../openglad-master/tools/parity_scenario_table.h` (mirror, SHA-equal)
- `../openglad-master/build/ci-test/parity_dump_master`
- `tests/parity/golden/*.json` (39 files)
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`
- `.plan/parity-present-state.md` (Phase 1)

## New Outputs

- Extended `scripts/parity/capture_master_golden.sh` with:
  - `--all` — iterate every id from `parity_dump_master --list`, capture into `tests/parity/golden/<id>.json`. Each golden passes `validate_schema.py` before being written; failure aborts the sweep with a non-zero exit naming the offending id.
  - `--no-write --out-dir <dir>` — capture to `<dir>` instead of `tests/parity/golden/`, no in-tree mutation.
- ~91 new / replaced JSON files under `tests/parity/golden/`.
- Append-only update to `.plan/parity-present-state.md` adding `## After Phase 03 — mass golden capture` section with PASS / SKIP / FAIL integers and `### Newly-FAILing (deferred to Phase 4/5/6)` subsection listing each newly-failing id.
- Branch commit: `parity-finish-3: phase 03 — recapture every golden against companion <sha>; <N> new, <M> replaced`.

## File Changes

- Edit `scripts/parity/capture_master_golden.sh` (add `--all` and `--no-write` modes).
- Run `scripts/parity/capture_master_golden.sh --all` to write goldens to `tests/parity/golden/`.
- `git add scripts/parity/capture_master_golden.sh tests/parity/golden/*.json .plan/parity-present-state.md`
- `git commit -m "parity-finish-3: phase 03 — recapture every golden against companion <sha>; <N> new, <M> replaced"`

## Implementation Details

- The script today calls `parity_dump_master --scenario <id> --out <path>`. The `--all` extension is a thin shell loop over `parity_dump_master --list`.
- Validator is mandatory per-id: malformed JSON dumps must NOT be committed. The script `exit 1`s on first validation failure.
- Output ordering is `sort -u` over `--list`.
- The script writes to a temp file then `mv`s into place so re-runs are byte-stable (verified by 03b).
- The recapture **does not** modify `kScenarios`.

## Verification Phases

### `03a-check-no-skips-on-master-side`
- **Type**: `check`
- **Bounce target**: `03-mass-golden-capture`
- **Purpose**: confirm every scenario the companion knows has a committed golden, and every committed golden is schema-v1 valid.
- **Commands**:
  ```
  ls tests/parity/golden/*.json | wc -l
  COMPANION_LIST_COUNT=$(../openglad-master/build/ci-test/parity_dump_master --list | wc -l)
  test "$(ls tests/parity/golden/*.json | wc -l)" -ge "$COMPANION_LIST_COUNT"
  while IFS= read -r id; do
    test -f tests/parity/golden/$id.json || { echo "missing: $id"; exit 1; }
  done < <(../openglad-master/build/ci-test/parity_dump_master --list)
  for f in tests/parity/golden/*.json; do
    python3 scripts/parity/validate_schema.py "$f" || { echo "invalid: $f"; exit 1; }
  done
  ```

### `03b-check-recapture-is-stable`
- **Type**: `check`
- **Bounce target**: `03-mass-golden-capture`
- **Purpose**: confirm a fresh recapture is byte-identical to the committed goldens (dumper determinism).
- **Commands**:
  ```
  rm -rf /tmp/recapture && mkdir -p /tmp/recapture
  scripts/parity/capture_master_golden.sh --all --out-dir /tmp/recapture --no-write
  for f in tests/parity/golden/*.json; do
    id=$(basename "$f" .json)
    cmp -s "$f" "/tmp/recapture/$id.json" || { echo "DIFF: $id"; exit 1; }
  done
  ```

### `03c-check-parity-tests-master-side-ok`
- **Type**: `check`
- **Bounce target**: `03-mass-golden-capture`
- **Purpose**: confirm SKIP count strictly drops, FAIL growth is bounded to the deferred cohort, and the present-state doc records the new counts.
- **Commands**:
  ```
  cmake --build --preset ci-test --target og_test_parity
  build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/parity-phase03.out

  PRE_SKIP=$(grep -oE '^Skipped: [0-9]+$' .plan/parity-present-state.md | awk '{print $2}' | head -1)
  POST_SKIP=$(grep -oE '^\[  SKIPPED \] [0-9]+ tests?\.' /tmp/parity-phase03.out | awk '{print $3}' | head -1)
  test -n "$POST_SKIP" || POST_SKIP=0
  test "$POST_SKIP" -lt "$PRE_SKIP"

  OLD_FAILS=$(awk '/^## Failing tests/,/^## /' .plan/parity-present-state.md \
              | grep -oE 'Parity\.[a-z_0-9]+' | sort -u)
  NEW_FAILS=$(grep -oE '^\[  FAILED  \] Parity\.[a-z_0-9]+' /tmp/parity-phase03.out \
              | awk '{print $NF}' | sort -u)
  ALLOWED='^Parity\.(weapon|effect|generator|event|special)_[a-z_0-9]+_emission_scen99$|^Parity\.special_[a-z_0-9]+_scen99$|^Parity\.coverage_catchall_scen99$'
  REGRESSED=$(comm -23 <(printf '%s\n' "$NEW_FAILS") <(printf '%s\n' "$OLD_FAILS") \
              | grep -vE "$ALLOWED" || true)
  test -z "$REGRESSED" || { echo "Unauthorised new FAILs:"; echo "$REGRESSED"; exit 1; }
  NON_DEFERRED_FAILS=$(printf '%s\n' "$NEW_FAILS" | grep -vE "$ALLOWED" || true)
  BASELINE_NON_DEFERRED=$(printf '%s\n' "$OLD_FAILS" | grep -vE "$ALLOWED" || true)
  test "$(printf '%s' "$NON_DEFERRED_FAILS" | sort -u)" \
     = "$(printf '%s' "$BASELINE_NON_DEFERRED" | sort -u)"

  grep -F '## After Phase 03 — mass golden capture' .plan/parity-present-state.md
  grep -F '### Newly-FAILing (deferred to Phase 4/5/6)' .plan/parity-present-state.md
  ```

## Success Criteria

- Every id in `parity_dump_master --list` has a committed golden under `tests/parity/golden/<id>.json`.
- A fresh recapture is byte-identical to every committed golden.
- `og_test_parity` SKIP count is strictly lower than Phase 1 baseline; ideally 0.
- Any new FAILs are in the deferred cohort regex (`weapon|effect|generator|event|special` emissions, special `<family>_<idx>`, or `coverage_catchall_scen99`); no non-deferred regressions.
- `.plan/parity-present-state.md` gains the `## After Phase 03` section with PASS / SKIP / FAIL plus the deferred FAIL list.

## Git Commit Requirement

The implementer **must** commit the script changes, all new/replaced goldens, and the doc append before yielding:

```
git add scripts/parity/capture_master_golden.sh tests/parity/golden/*.json .plan/parity-present-state.md
git commit -m "parity-finish-3: phase 03 — recapture every golden against companion <sha>; <N> new, <M> replaced"
```

Use a directory-wide `git add tests/parity/golden/*.json` so stale replacements cannot drift out of the commit.
