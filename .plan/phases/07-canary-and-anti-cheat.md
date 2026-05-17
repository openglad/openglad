# Phase 07 — Mutation canary + anti-cheat self-test

**Phase Name**: Every row's `discriminating_mutation` flips ≥1 predicate; anti-cheat self-test demonstrates silent loopholes are caught; CI driver script lands.

**Implement Phase ID**: `07-canary-and-anti-cheat`

## Preexisting Inputs

- Phase 06 commit (zero skip / zero fail baseline)
- `tests/parity/scenario_table.h` (every row has a real `discriminating_mutation`)
- `scripts/parity/run_mutation_canary.sh`
- `scripts/parity/run_mutation_canary_runtime.py` (parses `.plan/parity-canary-exemptions.md` if present, via `load_exemptions()` at lines 74-91)
- `scripts/parity/_apply_mutation.py`
- `scripts/parity/lint_scenario_facts.py` (the `dead_predicate` rule already exists at lines 560-580)
- `scripts/parity/validate_schema.py`
- `scripts/parity/capture_master_golden.sh` (Phase 03 added `--all`, `--no-write`)
- `tests/parity/test_parity_coverage_gate.cpp` (`behavioural_coverage_gate_no_dead_predicates` already exists)

## New Outputs

- `.plan/parity-canary-exemptions.md` — bullet-list of rows the canary cannot flip. Format — three lines per exempted row:
  ```
  # ===========================================================
  # why: <single-line reason citing the specific runner source line the runner never invokes>
  # future_work: <single-line follow-up plan to retire the exemption>
  - <scenario_id>
  ```
  `# why:` and `# future_work:` mandatory and immediately precede the `- <id>` line. Pipe-delimited markdown table rows are NOT permitted. Listed ids may report `flipped=0` without failing the canary.
- `scripts/parity/anti_cheat_selftest.sh` — bash script implementing Bypasses A–D. Each bypass:
  1. `git worktree add /tmp/parity-bypass-<letter> HEAD`
  2. Apply `sed -i` mutation to specific file.
  3. Run the relevant guard command; assert non-zero exit.
  4. `git -C /tmp/parity-bypass-<letter> checkout -- .`
  5. (After all bypasses) `git worktree remove --force /tmp/parity-bypass-<letter>`
- `scripts/parity/ci_parity.sh` — single-shot driver chaining: `cmake --build` → `og_test_parity` → lint → `run_mutation_canary.sh --all` → `capture_master_golden.sh --all --out-dir /tmp/recap --no-write` + `cmp -s` loop → `anti_cheat_selftest.sh`. `set -e`; each step's exit code logged to stderr.
- Append `## After Phase 07 — canary green; anti-cheat live` to `.plan/parity-present-state.md` with literal canary stdout summary and self-test exit log.
- Branch commit: `parity-finish-3: phase 07 — mutation canary green; anti-cheat self-test + ci_parity.sh`.

## File Changes

- Create `.plan/parity-canary-exemptions.md`.
- Create `scripts/parity/anti_cheat_selftest.sh` (`chmod +x`).
- Create `scripts/parity/ci_parity.sh` (`chmod +x`).
- Append to `.plan/parity-present-state.md`.
- `git add` listed files; commit.
- **DO NOT edit `scripts/parity/lint_scenario_facts.py`.** The `dead_predicate` rule already exists (landed in `c6f473f5`).

## Implementation Details

- `anti_cheat_selftest.sh` is bash (no python) so it runs in CI without additional deps. Each bypass is wrapped in a function returning 0 on "guard correctly fired" and 1 on "guard silently accepted bypass".
- `ci_parity.sh` is a simple chain (`set -e`). Failure at any step aborts the bundle.
- `run_mutation_canary.sh` already supports `--all` and the runtime already reads `.plan/parity-canary-exemptions.md` when present. Phase 07 creates the file; no parser change required.

**Bypass A — widening lint:**
```
TARGET=$(grep -nE 'pred::WalkerFamilyCount\([A-Z_0-9, ]+\)' tests/parity/scenario_table.h \
             | grep -vE ', *99 *\)' | head -1)
test -n "$TARGET" || { echo "no narrow WalkerFamilyCount to widen"; exit 1; }
LINE=$(echo "$TARGET" | cut -d: -f1)
sed -i -E -e "${LINE}s/(pred::WalkerFamilyCount\([^,]+, *[0-9]+, *)[0-9]+\)/\1 99)/" \
  tests/parity/scenario_table.h
python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h   # MUST exit non-zero
```

**Bypass B — behavioural-gate removal:** `sed -i` to remove one `WeaponFamilyEmitted(FAMILY_KNIFE)` line; rebuild and run `og_test_parity --gtest_filter='Parity.behavioural_coverage_gate_weapons'`. Must report `FAILED` naming FAMILY_KNIFE.

**Bypass C — golden tamper:** `printf 'XX' > tests/parity/golden/family_soldier_scen99.json`; then `python3 scripts/parity/validate_schema.py tests/parity/golden/family_soldier_scen99.json` must exit non-zero. AND `og_test_parity --gtest_filter='Parity.family_soldier_scen99'` must report FAILED.

**Bypass D — dead-predicate trick:** edit one predicate to wrap as `pred::branch_only(pred::master_only(...))`. Run `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h` AND `og_test_parity --gtest_filter='Parity.behavioural_coverage_gate_no_dead_predicates'`. Both must fail with the existing `dead_predicate` rule.

Each bypass undone with `git -C /tmp/parity-bypass-NN/ checkout -- .` before next; worktree removed at end with `git worktree remove --force`.

## Verification Phases

### `07a-check-canary-every-row-flips`
- **Type**: `check`
- **Bounce target**: `07-canary-and-anti-cheat`
- **Purpose**: confirm every non-exempt row's mutation flips ≥1 predicate, the exemption file count matches the canary's `flipped=0` count, and the exemption count is upper-bounded.
- **Commands**:
  ```
  scripts/parity/run_mutation_canary.sh --all 2>&1 | tee /tmp/p07-canary.out
  test "$(tail -1 /tmp/p07-canary.out; echo $?)" || true
  scripts/parity/run_mutation_canary.sh --all   # rerun for exit-code assert
  EXEMPT_DOC=$(python3 -c "
  import sys
  sys.path.insert(0, 'scripts/parity')
  from run_mutation_canary_runtime import load_exemptions
  print(len(load_exemptions()))
  ")
  EXEMPT_CANARY=$(grep -cE 'flipped=0' /tmp/p07-canary.out)
  test "$EXEMPT_DOC" = "$EXEMPT_CANARY"
  test "$EXEMPT_DOC" -le 2 || { echo "exempt count too high: $EXEMPT_DOC"; exit 1; }
  awk '/^# why:/ {seen_why=1} /^# future_work:/ {seen_fw=1} \
       /^- / { if (!seen_why || !seen_fw) { print "missing why/future_work above: " $0; exit 1 } \
               seen_why=0; seen_fw=0 }' .plan/parity-canary-exemptions.md
  ```

### `07b-check-anti-cheat-selftest-traps-tampering`
- **Type**: `check`
- **Bounce target**: `07-canary-and-anti-cheat`
- **Purpose**: confirm all four bypass attempts trigger their respective guards.
- **Commands**:
  ```
  test -x scripts/parity/anti_cheat_selftest.sh
  scripts/parity/anti_cheat_selftest.sh
  ```

### `07c-check-ci-script-runs-full-bundle`
- **Type**: `check`
- **Bounce target**: `07-canary-and-anti-cheat`
- **Purpose**: confirm `scripts/parity/ci_parity.sh` exists, is executable, and runs the full chain green.
- **Commands**:
  ```
  test -x scripts/parity/ci_parity.sh
  scripts/parity/ci_parity.sh
  ```

## Success Criteria

- `run_mutation_canary.sh --all` exits 0; every non-exempt scenario reports `flipped>=1`.
- Exemption doc lists at most 2 rows; every entry has `# why:` and `# future_work:` lines immediately above the `- <id>` bullet.
- `anti_cheat_selftest.sh` exits 0 (all four bypasses caught).
- `ci_parity.sh` exits 0 (full bundle green).
- HEAD contains the new doc and scripts.

## Git Commit Requirement

The implementer **must** commit the new doc, both new scripts, and the present-state append before yielding:

```
chmod +x scripts/parity/anti_cheat_selftest.sh scripts/parity/ci_parity.sh
git add .plan/parity-canary-exemptions.md \
        scripts/parity/anti_cheat_selftest.sh \
        scripts/parity/ci_parity.sh \
        .plan/parity-present-state.md
git commit -m "parity-finish-3: phase 07 — mutation canary green; anti-cheat self-test + ci_parity.sh"
```

Do not modify `scripts/parity/lint_scenario_facts.py`.
