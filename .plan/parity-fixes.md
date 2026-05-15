# Parity Fixes — Phase 07

This document records the disposition of every `regression` row in
`.plan/parity-divergence-report.md` (Phase 06) plus any reclassifications
made during Phase 07 investigation. The Phase 06 report is the single
source of truth for what work this phase owed.

## Inputs consumed (not regenerated)

- `.plan/parity-divergence-report.md` — authoritative work-list.
- `tests/parity/*.cpp`, `tests/parity/*.h` — branch parity tests
  (Phase 04 skeleton).
- `tests/parity/golden/*.json` — 15 master-companion goldens captured
  in Phase 06 (master companion commit
  `ce70d23286f1e8034284e7c718ec658065f525e5` against master parent
  `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`).

## Regression inventory at the start of Phase 07

Parsing `.plan/parity-divergence-report.md`:

- `## Per-scenario results` table: 16 scenarios, all classified `pass`
  (15 master-comparable byte-equal to golden + 1 branch-internal
  invariant). Zero scenarios classified `regression`.
- `## Classified divergences` table: one row, body `_(none observed)_`.
  Zero `regression` rows; zero `intended_diff` rows.
- `## Regressions to fix in Phase 07` enumerates 5 numbered
  *harness-completeness* items (loader wiring, scripted-input routing,
  golden re-capture, dirty-bit invariant expansion, populated-world
  tick-cadence verification). Phase 06 explicitly states (lines
  133–138 and 191–197 of the report):

  > None of these items represent a known **regression in branch
  > gameplay versus master** today. They are the residue of the Phase
  > 04 / Phase 05 skeleton handover.

  These items therefore are **not** `regression` rows in the Phase 06
  schema; they are forward-looking harness work that, if landed, would
  potentially uncover gameplay regressions. They are tracked but not
  consumed as "fix every regression" work items.

## Verification of the clean state

Run from branch HEAD `e6e2fbc0 parity: phase 06 — fix divergence report
section header`:

- `cmake --build --preset ci-test` — succeeds (`Vendor header check: OK`).
- `ctest --preset ci-test -R '^og_(test|unit)_parity' --output-on-failure`
  — `og_test_parity` passes (16/16 GoogleTest cases inside the binary).
- Full `ctest --preset ci-test` — 37/37 tests pass; integration,
  unit, build, and emscripten labels all green. Total: 209 s.

The "fix → re-run → fix" iteration in the phase doc is therefore a
no-op pass: parity is already clean against the Phase 06 goldens.

## Per-fix log

No `regression`-classified scenarios existed in
`.plan/parity-divergence-report.md` (`## Classified divergences` body is
`_(none observed)_`; every row in `## Per-scenario results` is
classified `pass`). Therefore there are no per-fix entries to record
under Phase 07's "one entry per regression" rule.

The 5 harness-completeness items listed under the Phase 06 report's
`## Regressions to fix in Phase 07` section are deliberately *not*
recorded here as fixes, for the reason given by that report itself
(quoted above). They are tracked as future work; landing them is a
prerequisite for *uncovering* potential gameplay regressions, not a
fix for any currently-known regression. Phase 06 explicitly leaves
that work to a follow-up pass, with the rule that newly observed
divergence "will be appended to this report" before Phase 07 acts.

| scenario_id | root cause (file:line + commit) | fix description | files modified | parity result after fix |
|-------------|---------------------------------|-----------------|----------------|-------------------------|
| _(no regression rows in Phase 06 divergence report)_ | — | — | — | — |

## Tests added

No new branch-side tests were added in this phase. Every scenario
already exercises the Phase 04 skeleton end-to-end via
`tests/parity/test_parity_scenarios.cpp`, and every dump is asserted
byte-equal to its Phase 06 golden (or invariant-checked for the
branch-internal `snapshot_dirty_bits_scen9301`). No new fix needed a
lock-in test because no fix was applied.

If a future iteration lands harness-completeness items 1–3 from the
Phase 06 report and a real gameplay regression surfaces, the rule for
*that* iteration remains: add a focused branch unit test under
`tests/unit/` or `tests/` that locks the fix in place, and append the
test name here in a follow-up Phase 07 commit.

## Re-classification log

No items were moved from `regression` to `intended_diff` in this phase
because the Phase 06 report contains no `regression`-classified items
to move. Phase 06's strict-citation rule (every `intended_diff` row
must cite a branch commit explicitly authorising the behaviour change)
is therefore vacuously satisfied.

| scenario_id | previous classification | new classification | citing branch commit | justification |
|-------------|-------------------------|--------------------|----------------------|---------------|
| _(none)_ | — | — | — | — |

# Phase 01 side-gate log

Phase 01 used default sub-rule (a) widening for the three rows called out
as currently failing by the plan: `special_cleric_scen124`,
`save_roundtrip_scen99`, and `scripted_input_scen9301`. None of those
three rows required sub-rule (c).

The following branch-only predicates are the explicit sub-rule (c) uses in
Phase 01. In each case the reconciled master dump has no matching walker
structure for the structural predicate, so no numeric widening can make the
same predicate substantive on both sides.

| scenario_id | branch-only predicate | reason |
|-------------|-----------------------|--------|
| `ai_idle_wander_scen9301` | `WalkerHpRangeAtFinalTick(SOLDIER, 0, 11900)` | Master dump ends with `walkers=[]`; HP range has no subject on master. |
| `special_archmage_scen123` | `WalkerPositionMoved(ARCHMAGE, 300, 0)` | Master dump ends with `walkers=[]`; position movement has no subject on master. |
| `summon_druid_pet_scen950` | `WalkerHpRangeAtFinalTick(SOLDIER, 8000, 9000)` | Master dump ends with `walkers=[]`; HP range has no subject on master. |
| `exit_trigger_scen9302` | `WalkerPositionMoved(SOLDIER, 300, 0)` | Master dump ends with `walkers=[]`; position movement has no subject on master. |
| `tick_cadence_scen9301` | `WalkerHpRangeAtFinalTick(SOLDIER, 0, 11900)` | Master dump ends with `walkers=[]`; HP range has no subject on master. |
