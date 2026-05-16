# Phase 06 — Mutation canary expansion and regression classification

**Phase Name**: Re-run the canary across every scenario; classify and
fix gameplay regressions.

**Implement Phase ID**: `06-canary-and-regressions`

## Preexisting Inputs

- `.plan/parity-second-divergence-report.md` (regressions to resolve)
- `.plan/parity-honest-audit.md`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `tests/parity/parity_runner.cpp`, `tests/parity/scenario_runtime.cpp`
- `scripts/parity/run_mutation_canary.sh`, `scripts/parity/_apply_mutation.py`

## New Outputs

- `.plan/parity-second-fixes.md` — one row per Phase-5-classified
  regression. Columns:
  `scenario_id | root cause (file:line + suspected commit) | fix description | files modified | parity result after fix | lock-in test`.
  Each fix commit has prefix `parity-fix:`. Each lock-in lands a focused
  unit test under `tests/unit/parity_fixes/test_<scenario>_<short>.cpp`.
- `.plan/parity-canary-exemptions.md` — explicit list of rows the canary
  cannot exercise mechanically. Each row has `Why:` (citing specific
  source lines) and `Future work:` lines. Verifier asserts both.
- Updated `.plan/parity-second-divergence-report.md` — every
  `regression` row back-references its `parity-second-fixes.md` row.
- Optional new unit tests under `tests/unit/parity_fixes/`.

## File Changes

- Source-code fixes for every Phase-5 regression. Each fix is a discrete
  `parity-fix:` commit (one per fix).
- New unit tests under `tests/unit/parity_fixes/` (one per fix).
- Create `.plan/parity-second-fixes.md`, `.plan/parity-canary-exemptions.md`.
- Modify `.plan/parity-second-divergence-report.md` (back-references).
- Final branch commit: `parity-finish-2: phase 06 — mutation canary green; regressions classified`.

## Implementation Details

- The canary `--all` already iterates every row. The agent confirms each
  `discriminating_mutation` reaches the runner (re-read
  `.plan/parity-coverage-manifest.md` "Known limitations"). Exempt rows
  keep their mutations but are whitelisted in the canary script via a
  parsed `parity-canary-exemptions.md`.
- For each Phase-5 `regression` row:
  1. `git log origin/master..HEAD -- <suspected files>` lists candidates.
  2. Reproduce in focused unit test: spawn involved walkers, trigger the
     path, assert master-side value (from golden) vs branch-side.
  3. Either land a `parity-fix:` commit restoring branch behaviour to
     master, OR reclassify as `intended_diff` with commit SHA cited in
     `.plan/parity-second-divergence-report.md`.
  4. Re-run canary on the touched row; confirm flip still works.

## Verification Phases

- **`06a-check-canary-every-row`** (`check`, `bounce_target: 06-canary-and-regressions`):
  Purpose: every non-exempt scenario flips ≥1 predicate; exempt list
  bounded and enumerated.
  Commands:
  - `scripts/parity/run_mutation_canary.sh --all` exits 0.
  - Canary stdout lists ≥1 flip for every non-exempt scenario.
  - Verifier parses `.plan/parity-canary-exemptions.md`, enforces
    exemption list size ≤ published list, asserts every other row
    flipped.

- **`06b-check-regressions-resolved`** (`check`, `bounce_target: 06-canary-and-regressions`):
  Purpose: every regression has a paired fix row and the whole ctest
  suite is green.
  Commands:
  - `python3 - <<'PY'` parses `.plan/parity-second-divergence-report.md`
    "Classified divergences" section, extracts every `regression` row,
    and asserts a corresponding row exists in
    `.plan/parity-second-fixes.md` keyed by `scenario_id`. For each,
    `parity result after fix` starts with `green` and `lock-in test` is
    non-empty. Zero regressions → header-only fixes file is accepted.
  - `cmake --build --preset ci-test && ctest --preset ci-test --output-on-failure`
    exits 0 — no fix may break an unrelated test.

- **`06c-check-no-residual-regression`** (`check`, `bounce_target: 06-canary-and-regressions`):
  Purpose: predicate evaluation agrees branch ↔ master across every
  scenario.
  Commands:
  - Re-evaluate every scenario's `expected_facts[]` on the current
    branch dump and master golden. Both sides satisfy every predicate.
    Check fails if any predicate evaluates differently (unclassified
    diff).

## Success Criteria

- Canary flips ≥1 predicate on every non-exempt row; exemption list
  bounded with `Why:` / `Future work:` per entry.
- Every Phase-5 `regression` is paired with a `parity-fix:` commit and
  a lock-in unit test, OR reclassified as `intended_diff` / `rng_drift`
  with citation.
- Full ctest green.
- No residual diff between branch and master predicate evaluation.

## Git Commit Requirement

The implementer MUST land one `parity-fix:` commit per regression fix
(separate commits) AND a final commit
`parity-finish-2: phase 06 — mutation canary green; regressions classified`
for the new docs and exemption list, **before yielding**. The next
check phases assert HEAD reaches those commits via
`git log origin/master..HEAD --oneline` and `git log -1 --name-status`.
