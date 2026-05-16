# Phase 03 — Tighten widened predicates and add the "no-widening" lint

**Phase Name**: Strengthen predicate surface; lint refuses unjustified
range widening.

**Implement Phase ID**: `03-tighten-predicates`

## Preexisting Inputs

- `.plan/parity-honest-audit.md` (widened-predicate inventory drives the work list)
- `.plan/parity-recapture-diff.md` (if a golden was replaced, predicate must match new golden)
- `tests/parity/scenario_table.h`
- `tests/parity/fact_predicate.{h,cpp}`
- `scripts/parity/lint_scenario_facts.py`

## New Outputs

- Updated `tests/parity/scenario_table.h`:
  - Every `WalkerFamilyCount(family, mn, mx)` with `mn != mx` is either:
    (a) narrowed to `(mx, mx)` or `(mn, mn)` if recapture confirms
        master value is stable;
    (b) replaced by `EffectFamilyCount`, `WalkerDiedByFinal`, or
        `WeaponFamilyEmitted` capturing the actual behavioural diff
        with exact count; or
    (c) accompanied by inline
        `// intended_diff: <reason>; cited commit <sha>`
        recognised by the new lint rule, with a corresponding entry in
        `.plan/parity-honest-audit.md` "Reclassified rows" section.
  - Same treatment for `WalkerOfTeamAlive(team, mn, mx)` widened ranges.
  - `WalkerHpRangeAtFinalTick` ranges wider than 200 cents either narrow
    to ≤200 OR cite `// rng_drift: <reason>` linked to a new
    `intended_diff` row.
- Updated `scripts/parity/lint_scenario_facts.py` with
  `unjustified_widening` rule. Parser walks `kFacts_<id>[]`, identifies
  widened predicates, requires per-predicate justification. Reuses the
  existing C++ table parser.
- Updated `.plan/parity-honest-audit.md` — append "Reclassified rows"
  subsection listing every narrowed/widened-with-citation/deleted row:

  ```markdown
  ## Reclassified rows

  | scenario_id | predicate | before | after | citation |
  |---|---|---|---|---|
  | family_mage_scen99 | WalkerFamilyCount(FAMILY_MAGE, ...) | (0, 3) | (0, 0) | <sha-or-reason> |
  ```

## File Changes

- Modify `tests/parity/scenario_table.h` (predicate tightenings).
- Modify `scripts/parity/lint_scenario_facts.py` (new rule).
- Modify `.plan/parity-honest-audit.md` (append section).
- Branch commit: `parity-finish-2: phase 03 — tighten predicates and add no-unjustified-widening lint`.

## Implementation Details

- Re-run `build/ci-test/og_test_parity` after each tightening; revert to
  the `intended_diff` citation path with an audit doc entry if a row
  regresses.
- Lint rule grammar: an `intended_diff` citation is an inline C++
  comment matching `// intended_diff: .{20,}; commit [0-9a-f]{7,40}`
  placed immediately after the predicate in `kFacts_<id>[]`. An
  `rng_drift` citation has the same shape with the leading keyword
  `rng_drift`.

## Verification Phases

- **`03a-check-lint-passes`** (`check`, `bounce_target: 03-tighten-predicates`):
  Purpose: confirm the lint rule is wired and trips on tampered input.
  Commands:
  - `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h`
    exits 0 with no diagnostics.
  - Construct a tampered copy:
    `sed -E 's/WalkerFamilyCount\(([^,]+),\s*([0-9]+),\s*\2/WalkerFamilyCount(\1, \2, 99/' tests/parity/scenario_table.h > /tmp/tampered.h`.
  - `LINT_SCENARIO_TABLE=/tmp/tampered.h python3 scripts/parity/lint_scenario_facts.py`
    must exit non-zero with `unjustified_widening` diagnostic present.

- **`03b-check-tests-still-green`** (`check`, `bounce_target: 03-tighten-predicates`):
  Purpose: ensure tightening did not regress any test.
  Commands:
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity` — every `Parity.*` case still passes.

- **`03c-check-widening-justified`** (`check`, `bounce_target: 03-tighten-predicates`):
  Purpose: assert every remaining widened predicate has a paired
  justification.
  Commands:
  - `python3 - <<'PY'` scans `tests/parity/scenario_table.h` for every
    widened predicate; asserts either (i) the line is followed (within
    3 lines) by an inline comment matching
    `// .*(branch|master|widen|intended_diff|parity-fix|rng_drift)`
    OR (ii) the row's `discriminating_mutation` rationale references
    the same FactKind. Failure prints offending
    `(scenario_id, line, predicate)` and exits non-zero.
  - The new lint rule is re-invoked
    (`python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h`)
    to demonstrate it is wired and currently passes.

## Success Criteria

- Lint exits 0 on the canonical table, non-zero on a synthetically
  widened copy.
- Every widened predicate carries an inline justification or has been
  narrowed.
- All `Parity.*` tests pass.
- `.plan/parity-honest-audit.md` "Reclassified rows" table enumerates
  every change made in this phase.

## Git Commit Requirement

The implementer MUST `git add` the modified `scenario_table.h`,
`lint_scenario_facts.py`, and `parity-honest-audit.md` and
`git commit` with message
`parity-finish-2: phase 03 — tighten predicates and add no-unjustified-widening lint`
**before yielding**. The next check phase asserts HEAD contains those
file changes.
