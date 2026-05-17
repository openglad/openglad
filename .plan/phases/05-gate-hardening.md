# Phase 05 — Behavioural-gate hardening + lint cross-check

**Phase Name**: Lock down the gate so every treasure-order family id is bound by at least one `TreasureFamilyOfOrderRemovedFromOblist` predicate (with `kOrderTreasure` qualifier); make lint reject treasure-row predicates that omit the qualifier; full suite progresses (FAIL count not increased from Phase 4, SKIP count not increased, only deferred cohorts may still be red).

**Implement Phase ID**: `05-gate-hardening`

## Preexisting Inputs

- `tests/parity/fact_predicate.{h,cpp}` (Phase 4 committed)
- `tests/parity/scenario_table.h` (Phase 4 committed)
- `tests/parity/golden/*.json` (Phase 4 committed)
- `tests/parity/test_parity_coverage_gate.cpp` (Phase 4 committed with `any_predicate_binds` cross-kind helper)
- `scripts/parity/lint_scenario_facts.py`
- `.plan/parity-harness-design.md`

## New Outputs

- Updated `.plan/parity-harness-design.md`: append section "Phase 05 — TreasureFamilyOfOrderRemovedFromOblist contract" documenting (a) the new FactKind, (b) per-Order `family_symbol_by_order` table, (c) gate's cross-kind walk, (d) schema-v1 producer-side behaviour change.
- Cleanup pass on gate code if Phase 4's `any_treasure_binding` helper was inlined incompletely. The implement prompt instructs the agent to grep for `any_predicate_binds(.*TreasureFamilyRemovedFromOblist` in the gate file and verify the new kind is walked at every relevant call-site.
- `tests/parity/test_parity_coverage_gate.cpp` gains a **new `TEST()` case** `behavioural_coverage_gate_treasure_kinds_required` proving the gate REQUIRES the new kind for treasure ids 0 and 8: asserts `any_predicate_binds(FactKind::TreasureFamilyOfOrderRemovedFromOblist, 0)` AND `any_predicate_binds(FactKind::TreasureFamilyOfOrderRemovedFromOblist, 8)` both return true. PASSED count after Phase 5 is Phase-4 PASSED + 1.
- Branch commit: `parity-finish-3: phase 05 — gate hardening; treasure ids 0 and 8 bound by new FactKind`.
- Companion commit only if a treasure-row predicate still uses the legacy factory; if so, standard mirror commit pair runs.

## File Changes

- `tests/parity/test_parity_coverage_gate.cpp` (regression guard).
- `.plan/parity-harness-design.md` (append section).
- (Conditional) `tests/parity/scenario_table.h` if any treasure row uses legacy factory; mirror to companion per Phase 2 recipe.
- (Conditional) recapture if `scenario_table.h` moved; verifier 05c repeats the 04c-style cmp.

## Implementation Details

- This phase is small; most work landed in Phase 4. Phase 5 exists separately so regression guards are committed AFTER Phase 4 stabilises.
- Agent does NOT introduce any `arg3`-on-`TreasureFamilyRemovedFromOblist` semantics; legacy factory remains exactly as in `tests/parity/fact_predicate.h:166-169` (`arg0=family, arg1..arg4=0`).
- Lint relies on existing four rules. No new lint rule.

## Verification Phases

### `05a-check-gate-source-uses-new-factkind`
- **Type**: `check`
- **Bounce target**: `05-gate-hardening`
- **Purpose**: confirm the gate source walks BOTH treasure FactKinds and the 17-entry enum from Phase 4 is preserved.
- **Commands**:
  ```
  test "$(grep -cE 'TreasureFamilyOfOrderRemovedFromOblist' tests/parity/test_parity_coverage_gate.cpp)" -ge 2
  cmake --build --preset ci-test --target og_test_parity
  python3 -c "
  from pathlib import Path
  import re
  t = Path('tests/parity/fact_predicate.h').read_text()
  start = t.index('enum class FactKind')
  block = t[start:t.index('};', start)]
  names = re.findall(r'^\s+([A-Z][A-Za-z0-9_]*),', block, re.MULTILINE)
  assert len(names) == 17, names
  assert 'TreasureFamilyRemovedFromOblist'        in names
  assert 'TreasureFamilyOfOrderRemovedFromOblist' in names
  "
  ```

### `05b-check-behavioural-gates-green`
- **Type**: `check`
- **Bounce target**: `05-gate-hardening`
- **Purpose**: confirm the behavioural gates pass and every `kFacts_treasure_*[]` predicate uses the new kind with `kOrderTreasure`.
- **Commands**:
  ```
  build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage_gate*' --gtest_brief=1 \
    2>&1 | tee /tmp/p05-gate.out
  test "$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p05-gate.out)" -eq 0
  test "$(grep -cE '^\[  SKIPPED \] Parity\.' /tmp/p05-gate.out)" -eq 0

  python3 -c "
  from pathlib import Path
  from scripts.parity.lint_scenario_facts import _load_table, parse_predicate_calls
  text = _load_table(Path('tests/parity/scenario_table.h'))
  calls = parse_predicate_calls(text)
  bad = []
  for arr, preds in calls.items():
      if not arr.startswith('kFacts_treasure_'): continue
      for i, p in enumerate(preds):
          if p['kind'] != 'TreasureFamilyOfOrderRemovedFromOblist': continue
          args = p['args']
          if len(args) < 2 or 'kOrderTreasure' not in args[1]:
              bad.append((arr, i, args))
  assert not bad, bad
  "

  grep -nE 'pred::TreasureFamilyRemovedFromOblist\s*\(' tests/parity/scenario_table.h \
    | grep -F kFacts_treasure_ \
    | { read -r line; test -z "$line" || { echo "legacy factory in treasure row: $line"; exit 1; }; }
  ```

### `05c-check-full-suite-progresses`
- **Type**: `check`
- **Bounce target**: `05-gate-hardening`
- **Purpose**: confirm full-suite FAIL/SKIP counts do not regress from Phase 4, every failing id is in the deferred-cohort regex, and lint passes.
- **Commands**:
  ```
  build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p05.out
  FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p05.out)
  SKIPPED=$(grep -oE '^\[  SKIPPED \] [0-9]+ tests?\.' /tmp/p05.out \
            | awk '{print $3}' | head -1); SKIPPED=${SKIPPED:-0}

  P4_FAIL=$(awk '/^## After Phase 04/,0' .plan/parity-present-state.md \
            | grep -oE '^Failing tests: [0-9]+' | awk '{print $3}' | head -1)
  P4_SKIP=$(awk '/^## After Phase 04/,0' .plan/parity-present-state.md \
            | grep -oE '^Skipped: [0-9]+' | awk '{print $2}' | head -1)
  test "$FAILED"  -le "$P4_FAIL"
  test "$SKIPPED" -le "$P4_SKIP"

  ALLOWED='^Parity\.(weapon|effect|generator|event|special)_[a-z_0-9]+_emission_scen99$|^Parity\.special_[a-z_0-9]+_scen99$|^Parity\.coverage_catchall_scen99$'
  REGRESSED=$(grep -oE '^\[  FAILED  \] Parity\.[a-z_0-9]+' /tmp/p05.out \
              | awk '{print $NF}' | sort -u | grep -vE "$ALLOWED" || true)
  test -z "$REGRESSED" || { echo "non-deferred fail: $REGRESSED"; exit 1; }

  python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h
  ```

## Success Criteria

- Behavioural gates pass zero-SKIP zero-FAIL.
- Gate source walks both treasure FactKinds (≥ 2 mentions of `TreasureFamilyOfOrderRemovedFromOblist`).
- Every `kFacts_treasure_*[]` predicate uses the new kind with `kOrderTreasure`; legacy factory is absent in treasure rows.
- Full-suite FAIL ≤ Phase 4 FAIL, SKIP ≤ Phase 4 SKIP, every failing id in deferred-cohort regex.
- Lint exits 0.

## Git Commit Requirement

The implementer **must** commit the gate-hardening regression guard and design-doc append before yielding:

```
git add tests/parity/test_parity_coverage_gate.cpp .plan/parity-harness-design.md
git commit -m "parity-finish-3: phase 05 — gate hardening; treasure ids 0 and 8 bound by new FactKind"
```

If a treasure row still uses the legacy factory, also update `tests/parity/scenario_table.h` and mirror to `../openglad-master/tools/parity_scenario_table.h` via the Phase 2 commit recipe before yielding.
