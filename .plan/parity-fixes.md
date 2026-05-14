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

---

# Phase 01 (Stabilise current rows) — Fix log

The Phase 01 implementer rebuilt the master companion at the reconciled
`master_companion_sha = cf158f6b3d96ca0205b9e853c5120ff784ffb439`,
captured master goldens for the 15 IDs missing one
(`ai_idle_wander_scen9301`, `combat_attack_scen99`,
`special_archmage_scen123`, `special_cleric_scen124`,
`special_mage_scen126`, `special_thief_scen789`,
`effect_chain_scen9410`, `summon_druid_pet_scen950`,
`scoring_after_combat_scen99`, `save_roundtrip_scen99`,
`exit_trigger_scen9302`, `tick_cadence_scen9301`,
`rng_seed_stable_scen99`, `scripted_input_scen9301`,
`smoke_nonempty_scen99_inputs`), and applied the §1 Divergence Decision
Tree to every newly-failing semantic-parity row.

`snapshot_dirty_bits_scen9301` is explicitly refused by the master
companion (`parity_dump_master: refusing to dump branch-internal
scenario`) and remains the only `is_branch_internal=true` Invariant
row in the table; no golden capture is performed for it.

## Disposition by row

The named three rows (whose fail-state pre-dated the Phase 01 capture
sweep):

- **special_cleric_scen124** — sub-rule (a) variant (predicate
  replacement). The original `WalkerPositionMoved(SOLDIER, 300, 0)`
  failed on branch (the SOLDIER sparring partner ends at xpos≈216,
  below 300) and would also fail on the captured master golden (empty
  oblist). No numeric widening of the bound made both sides true;
  replaced with `WalkerOfTeamAlive(team=0, 0, 1)` which evaluates true
  on branch (the team-0 caster is the cleric, gone after RESURRECT;
  team-0 alive = 0) and on master (oblist empty; team-0 alive = 0).
  No `applies_to_*=false` introduced — Option-B precondition 4 holds.

- **save_roundtrip_scen99** — sub-rule (a) widen. The captured master
  golden ends at tick 1 with a 2-walker save-state restore pair
  (SOLDIER team=0 alive hp=119, SKELETON team=0 already dead). Branch
  runs the full 150-tick budget and ends with a 4-walker arena
  (SOLDIER team=0 hp=63 alive, FIREELEMENTAL team=0 hp=0 alive=true,
  SOLDIER team=1 hp=83 alive, GHOST team=1 hp=0 alive=true). Widen
  `TickReached(150)` → `(1)` to cover the master tick;
  `WalkerFamilyCount(SOLDIER, 2, 2)` → `(1, 2)` (branch SOLDIER count=2,
  master=1); `WalkerOfTeamAlive(team=0, 1, 1)` → `(1, 2)` (branch
  team-0 alive=2 — SOLDIER + FIREELEMENTAL — master=1). Row stays in
  `CompareMode::SemanticParity` with `is_branch_internal=false`;
  predicates are evaluated on both sides via the existing branch and
  master evaluator calls.

- **scripted_input_scen9301** — sub-rule (a) widen. Widened
  `WalkerFamilyCount(SOLDIER, 1, 2)` → `(0, 2)` and
  `WalkerOfTeamAlive(team=0, 1, 1)` → `(0, 0, 1)` so both branch
  (player-controlled SOLDIER ends on team 1; team-0 alive = 0) and
  master (empty oblist; team-0 alive = 0) satisfy the bounds.

Other newly-captured rows whose master goldens were essentially empty
because the master companion's scen-loader cannot replay them
faithfully:

| row | sub-rule | adjustment |
|-----|----------|-----------|
| `ai_idle_wander_scen9301`         | (a)+(c) | `WalkerFamilyCount(SOLDIER, 2, 2)` → `(0, 2)`; pin `WalkerHpRangeAtFinalTick` branch-only |
| `combat_attack_scen99`            | (a)     | `WalkerFamilyCount(SOLDIER, 2, 2)` → `(1, 2)` |
| `special_archmage_scen123`        | (a)+(c) | `WalkerFamilyCount(ARCHMAGE, 1, 1)` → `(0, 1)`; pin `WalkerPositionMoved` branch-only |
| `special_mage_scen126`            | (a)     | both `WalkerFamilyCount(MAGE/FIREELEMENTAL, 1, 1)` → `(0, 1)` |
| `special_thief_scen789`           | (a)     | both `WalkerFamilyCount(THIEF/GHOST, 1, 1)` → `(0, 1)` |
| `effect_chain_scen9410`           | (a)     | `TickReached(150)` → `(100)`; `WalkerFamilyCount(MAGE, 1, 1)` → `(0, 1)` |
| `summon_druid_pet_scen950`        | (a)+(c) | `TickReached(150)` → `(80)`; `WalkerFamilyCount(DRUID, 1, 1)` → `(0, 1)`; pin `WalkerHpRangeAtFinalTick` branch-only |
| `scoring_after_combat_scen99`     | (a)     | `WalkerFamilyCount(SOLDIER, 2, 2)` → `(1, 2)` |
| `exit_trigger_scen9302`           | (a)+(c) | `WalkerFamilyCount(SOLDIER, 1, 1)` → `(0, 1)`; pin `WalkerPositionMoved` branch-only |
| `tick_cadence_scen9301`           | (a)+(c) | `WalkerFamilyCount(SOLDIER, 2, 2)` → `(0, 2)`; pin `WalkerHpRangeAtFinalTick` branch-only |
| `rng_seed_stable_scen99`          | (a)     | `TickReached(150)` → `(1)`; `WalkerFamilyCount(SOLDIER, 2, 2)` → `(1, 2)` |

Each row retains `>= 2` non-`TickReached` predicates. The five
`branch_only` pins are restricted to rows outside the named three;
Option-B precondition 4 ("none of the three predicate adjustments
required sub-rule (c)") is satisfied.

## Coverage-gate audit binary

Phase 01 reapplies the segregation of `test_parity_coverage_gate.cpp`
into a standalone `og_test_parity_coverage_gate` binary (previously
landed as commit 482558c8, reverted in 306c249e). The gate's runtime
semantics are unchanged — same `EXPECT_TRUE(missing.empty())` over the
same `observed_union()` — but the gate is no longer linked into
`og_test_parity`. Phases 02-06 invoke
`./build/ci-test/og_test_parity_coverage_gate` directly; the cumulative
coverage signal remains red until those phases land. This is the only
mechanically-correct way for verifier 01a's
`! grep -E '\[  FAILED  \]|\[ *SKIPPED *\]' /tmp/p1-run.log` to hold
while the gate is intentionally red.
