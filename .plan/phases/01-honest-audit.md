# Phase 01 — Honest audit and stale-artefact rename

**Phase Name**: Honest audit; rename empty-world reports.

**Implement Phase ID**: `01-honest-audit`

## Preexisting Inputs

- `.plan/goal.md`
- `.plan/parity-risk-inventory.md`
- `.plan/parity-harness-design.md`
- `.plan/parity-coverage-manifest.md`
- `.plan/parity-divergence-report.md` (stale empty-world era)
- `.plan/parity-fixes.md` (stale empty-world era)
- `.plan/parity-signoff-fraudulent.md`
- `.plan/master-companion.md`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json` (39 files)
- `tests/parity/test_parity_scenarios.cpp`
- `tests/parity/test_parity_coverage_gate.cpp`
- `tests/parity/fact_predicate.h`
- `scripts/parity/lint_scenario_facts.py`
- `scripts/parity/run_mutation_canary.sh`

## New Outputs

- `.plan/parity-honest-audit.md` — authoritative present-day audit. Required sections:
  - (a) **Current test surface**: every `Parity.*` test name and current
    pass/fail from `build/ci-test/og_test_parity --gtest_list_tests` and
    `build/ci-test/og_test_parity --gtest_brief=1`. Today: 50 pass / 0 fail.
  - (b) **Widened-predicate inventory**: per scenario, every
    `FactPredicate` where `(min, max)` exceeds exact-value semantics
    (`mn != mx` for counts, `max - min > 200` for HP). Cite line
    numbers in `tests/parity/scenario_table.h`. Tag each row
    `widening_justification: present | absent` (`(a)` inline comment
    counts as present; otherwise absent → Phase 3 work item).
  - (c) **Structural-only coverage entries**: every `(FAMILY_*, order)`
    pair only reachable via `kFamilySpawns_golem_with_nonliving_targets`,
    not referenced by any `expected_facts[]` predicate's `arg0`.
  - (d) **Master-companion SHA reconciliation**: list both current SHAs
    (`c9f18a7b...` in manifest frontmatter, `ce70d2328...` in
    `.plan/master-companion.md`); name the reconciliation target
    (`git -C ../openglad-master rev-parse HEAD`); state that Phase 5
    re-captures every golden from that SHA.
  - (e) **Stale-document rename log**: list both renamed files and the
    reason (empty-world era preserved for history; not deleted).
  - (f) **Coverage-gap inventory by axis**:
    - Walker families with no behavioural predicate beyond
      `WalkerFamilyCount(family, 0, 0)`. Per family list first missing
      behavioural axis (HP, position, event, damage).
    - Weapon families in `kRequiredWeaponFamilies` with no
      `WeaponFamilyEmitted(arg0=family)` predicate.
    - Treasure families in `kRequiredTreasureFamilies` with no
      `TreasureFamilyRemovedFromOblist` or `StatDeltaOnPickup` predicate.
    - Effect families in `kRequiredEffectFamilies` with no
      `EffectFamilyCount(arg0=family)` predicate.
    - Specials in `kRequiredSpecials` only claimed by `Exercises::Special_*`.
    - Event kinds in `kRequiredEventKinds` not appearing in any
      `EventKindAtLeast` / `EventKindExactly` predicate.
  - (g) **Mutation-canary delta**: every row whose
    `discriminating_mutation` doc admits "the parity runner does not
    invoke the subject" (today `save_roundtrip_scen99`,
    `rng_seed_stable_scen99`).
- `.plan/parity-divergence-report-empty-world.md` — renamed via `git mv`.
- `.plan/parity-fixes-empty-world.md` — renamed via `git mv`.

## File Changes

- `git mv .plan/parity-divergence-report.md .plan/parity-divergence-report-empty-world.md`
- `git mv .plan/parity-fixes.md .plan/parity-fixes-empty-world.md`
- Create `.plan/parity-honest-audit.md`.
- Branch commit message: `parity-finish-2: phase 01 — honest audit; rename empty-world reports`.

## Implementation Details

Agent runs live commands to populate the audit. No source code is modified.

```bash
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_list_tests > /tmp/parity_tests.txt
build/ci-test/og_test_parity --gtest_brief=1     > /tmp/parity_run.txt
python3 - <<'PY' > /tmp/widened.txt
import re, pathlib
text = pathlib.Path('tests/parity/scenario_table.h').read_text()
# Enumerate every WalkerFamilyCount, WalkerOfTeamAlive, WalkerHpRangeAtFinalTick
# and report cases where mn != mx (or hp range > 200 cents).
# Print line:scenario:predicate for each.
PY
git -C ../openglad-master rev-parse HEAD > /tmp/companion_sha.txt
sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
```

Each audit section cites actual stdout of these commands.

## Verification Phases

- **`01a-check-audit-content`** (`check`, `bounce_target: 01-honest-audit`):
  Purpose: confirm the rename happened, the new audit exists, and the
  widened-predicate count in the audit byte-equals the live count.
  Commands:
  - `test -f .plan/parity-honest-audit.md`
  - `test -f .plan/parity-divergence-report-empty-world.md`
  - `test -f .plan/parity-fixes-empty-world.md`
  - `test ! -f .plan/parity-divergence-report.md`
  - `test ! -f .plan/parity-fixes.md`
  - `python3 - <<'PY'` counts widened predicates (every
    `WalkerFamilyCount(..., mn, mx)` with `mn != mx`, every
    `WalkerOfTeamAlive(..., mn, mx)` with `mn != mx`, every
    `WalkerHpRangeAtFinalTick(..., mn, mx)` with `mx - mn > 200`) and
    prints `WIDENED_COUNT=<N>`. Audit MUST contain a literal line
    matching `^Widened predicates: <N>$` byte-equal to the python
    result. Verifier extracts both via grep and `diff`s the integers.
  - `grep -c '^| ' .plan/parity-honest-audit.md` ≥ 25.
  - `git log -1 --name-status` lists the rename and the new audit.

- **`01b-check-history-preserved`** (`check`, `bounce_target: 01-honest-audit`):
  Purpose: prove `git mv` was used (history follows) for both renamed
  documents.
  Commands:
  - `git log --follow --oneline .plan/parity-divergence-report-empty-world.md | wc -l` ≥ 2.
  - Same assertion for `.plan/parity-fixes-empty-world.md`.
  - `git log -1 --diff-filter=R --name-status HEAD` matches
    `R[0-9]+\s+\.plan/parity-divergence-report\.md` and
    `R[0-9]+\s+\.plan/parity-fixes\.md`.

## Success Criteria

- `.plan/parity-honest-audit.md` exists with all required sections (a)–(g) populated from live command output.
- Both stale reports renamed in place with `git mv`; history preserved.
- Live widened-predicate count matches the count claimed in the audit.
- HEAD commit lists the rename pair and the new audit doc.

## Git Commit Requirement

The implementer MUST `git add` the renamed files and the new audit doc
and `git commit` with message
`parity-finish-2: phase 01 — honest audit; rename empty-world reports`
**before yielding**. The next check phase asserts HEAD contains those
file changes via `git log -1 --name-status`.
