# Parity Divergence Report — Phase 06

This report records the first end-to-end branch-vs-master parity run for
`wip/networking`. It consumes the master goldens captured by
`scripts/parity/capture_master_golden.sh` (driving the Phase 05 master
companion `../openglad-master/build/ci-test/parity_dump_master` on branch
`parity-companion`, commit `ce70d23286f1e8034284e7c718ec658065f525e5`)
and the branch-side parity test binary built from
`tests/parity/test_parity_scenarios.cpp`.

Run inputs (recorded for reproducibility):

- Branch HEAD: `373965f4 parity: phase 05 — master companion docs and capture scripts`
- Master baseline (companion parent): `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`
- Branch parity binary: `build/ci-test/og_test_parity`
- Branch run log: `.plan/logs/parity-run-initial.log` (gitignored)
- Capture log (master-side): printed to stdout by
  `scripts/parity/capture_master_golden.sh`; not retained on disk by design.
- Schema: v1 per `.plan/parity-harness-design.md`.

## Capture summary

| Statistic                                  | Value |
|--------------------------------------------|------:|
| Scenarios attempted (`--list`)             |    15 |
| Scenarios captured to `tests/parity/golden/` |  15 |
| Schema validation failures                 |     0 |
| Master-side capture failures               |     0 |

Every master-comparable scenario in `tests/parity/scenario_table.h`
(`kMasterComparableScenarioCount == 15`) produced a schema-v1-valid dump.
The branch-internal `snapshot_dirty_bits_scen9301` has no master golden
by design.

Master-companion stdout note repeated 15 times during capture:
`parity_dump_master: early stop at tick 1 (level_done set)`. This is the
documented Phase 05 behaviour on an empty world (see
`.plan/master-companion.md` `## Caveats / known scope limits` item 3);
`GameWorld::tick()` sets `level_done = 2` when no foes are found, which
both runners treat as an early stop. Both sides reach `tick == 1` and
dump from the same state, so byte-equality is preserved.

## Per-scenario results

Run captured at `.plan/logs/parity-run-initial.log`. All 16 GoogleTest
entries (15 master-comparable + 1 branch-internal) passed.

| scenario_id                       | branch result | diff highlights                       | classification |
|-----------------------------------|---------------|---------------------------------------|----------------|
| `ai_idle_wander_scen9301`         | pass          | byte-equal to golden                  | pass           |
| `combat_attack_scen99`            | pass          | byte-equal to golden                  | pass           |
| `special_archmage_scen123`        | pass          | byte-equal to golden                  | pass           |
| `special_cleric_scen124`          | pass          | byte-equal to golden                  | pass           |
| `special_mage_scen126`            | pass          | byte-equal to golden                  | pass           |
| `special_thief_scen789`           | pass          | byte-equal to golden                  | pass           |
| `effect_bomb_lifetime_scen99`     | pass          | byte-equal to golden                  | pass           |
| `effect_chain_scen9410`           | pass          | byte-equal to golden                  | pass           |
| `summon_druid_pet_scen950`        | pass          | byte-equal to golden                  | pass           |
| `scoring_after_combat_scen99`     | pass          | byte-equal to golden                  | pass           |
| `save_roundtrip_scen99`           | pass          | byte-equal to golden                  | pass           |
| `exit_trigger_scen9302`           | pass          | byte-equal to golden                  | pass           |
| `tick_cadence_scen9301`           | pass          | byte-equal to golden                  | pass           |
| `rng_seed_stable_scen99`          | pass          | byte-equal to golden                  | pass           |
| `scripted_input_scen9301`         | pass          | byte-equal to golden                  | pass           |
| `snapshot_dirty_bits_scen9301`    | pass          | branch-internal: dumper deterministic | pass           |

Empirical canonical-JSON shape captured on both sides (representative
example, all 15 goldens differ only in `rng_state`):

```json
{"effects":[],"events":[],"rng_state":"0x00000001","schema_version":"v1","score_per_team":[0,0,0,0],"tick":1,"walkers":[]}
```

The 15 goldens carry the seed-specific `rng_state` strings
`0x00000001`, `0x00000007`, `0x00000042`, `0x00000123`, `0x0000BEEF`,
`0x0000CAFE`, `0x0000F00D`, `0x00000010`, exactly as configured in
`scenario_table.h`.

## Classified divergences

No diffs were observed. Every scenario hit `MATCH` (in the
`scripts/parity/diff_dumps.py` sense) and the branch-side GoogleTest
`EXPECT_EQ(expected, actual)` succeeded for every golden.

| field          | branch | master | classification | suspected commit | notes |
|----------------|--------|--------|----------------|------------------|-------|
| _(none observed)_ | — | — | — | — | nothing to classify |

There are therefore no `intended_diff` rows and no commit-SHA
justifications to cite. Phase 04's strict rule for `intended_diff`
(must cite a branch commit explicitly authorising the behaviour
change) is vacuously satisfied.

### Scope caveat — what this run can and cannot prove

Both runners currently exercise the **Phase 04 skeleton**: scenario
files referenced by `ScenarioSpec::scenario_file` are not yet loaded
into `og::sim::GameWorld`, and scripted input events are not yet
written into `world.input_state_`. This is documented in
`tests/parity/parity_runner.cpp:7-19` (branch) and
`.plan/master-companion.md` `## Caveats / known scope limits` item 1
(companion). The current goldens therefore prove:

1. Schema v1 emitters on both sides agree byte-for-byte for an
   equivalent empty `GameWorld`.
2. RNG seeding (`world.rng_.state_ = spec.rng_seed`) round-trips to
   the configured value on both sides.
3. The tick-cadence contract (one `world.tick()` call = one tick;
   `tick_count_ == 1` after the first call which trips `level_done`)
   matches.
4. `score_per_team[]` and the `effects` / `walkers` / `events`
   containers are empty on both sides under the same trigger.

The current goldens do **not** yet prove parity for the populated
scenarios that the design table promises: walker AI movement, combat
damage rolls, family specials, summon inheritance, scoring after
combat, exit-trigger firing, save-blob round-trip. Wiring the level
loader and scripted-input writer is on the critical path for Phase 07
(`07-fix-regressions` already lists "complete the runner / companion
beyond the empty-world skeleton" as the gating work — see
`.plan/phases/07-fix-regressions.md`).

Because the strict rule for Phase 06 is *"when in doubt, classify as
`regression`"*, the empty-world skeleton is not used as cover for any
suspected divergence: the report would simply re-list any newly
populated scenario whose populated dumps fail byte-equal once Phase 07
wires the loader. There is no current evidence of any specific
regression in the subsystems listed in `.plan/parity-risk-inventory.md`;
the skeleton run has not yet exercised them.

## Regressions to fix in Phase 7

No master-vs-branch byte-level regressions were observed in this run.
Phase 07 still has gating work — Phase 06 leaves the following items as
Phase 07 inputs. These are **harness-completeness** items, not
gameplay regressions; they are listed here so Phase 07 has an
ordered punch list and so the divergence report's `## Regressions`
section is not silently empty.

1. **Wire the scenario-file loader into `og::parity::run_scenario`.**
   File: `tests/parity/parity_runner.cpp:23-50`.
   Root cause: Phase 04 skeleton constructs an empty `GameWorld` and
   never calls into `gloader.cpp` / `PhysFS` for `spec.scenario_file`.
   Severity: P1 — blocks every populated-world parity assertion in
   the design table.
   Mirror task on the master companion:
   `../openglad-master/tools/parity_dump_master.cpp` (same skeleton; see
   `.plan/master-companion.md` `## Caveats / known scope limits` item
   1). Both runners must be updated in the same Phase 07 commit so
   goldens stay in lockstep.

2. **Route scripted `InputEvent`s into `world.input_state_`.**
   File: `tests/parity/parity_runner.cpp:9-19`.
   Root cause: `apply_inputs_at_tick` is a no-op placeholder; the
   scripted input table is read but never written into the world.
   Severity: P1 — `combat_attack_scen99`, `special_*`,
   `effect_*`, `summon_druid_pet_scen950`, `exit_trigger_scen9302`,
   and `scripted_input_scen9301` all require this to test what they
   were named for. Must also be mirrored on the master companion
   (the master path applies directly to `world.input_state_`; the
   branch path additionally exercises the `local_transport_shadow`
   variant for `scripted_input_scen9301` per the design's
   `## Tick-cadence parity contract` item 3).

3. **Re-capture goldens after items 1 and 2 land.** Re-run
   `scripts/parity/capture_master_golden.sh` from the branch repo
   against an updated companion, validate each dump with
   `scripts/parity/validate_schema.py`, and replace the 15 goldens
   in this commit. Severity: P1 (mechanical; depends on 1 and 2).

4. **Expand `snapshot_dirty_bits_scen9301` invariant body.** File:
   `tests/parity/test_parity_scenarios.cpp:35-49`. The current body
   asserts the dumper is deterministic (`canonical_serialize(dump)` is
   stable across repeated runs). Once the scenario actually populates
   walkers, the invariant must compare a dump taken via the
   dirty-tracked server mirror (`world_snapshot.cpp` + `dirty_field_bits.h`)
   against a dump taken via direct iteration of `world.walkers`. That
   path is documented in `include/openglad/gameplay/world_snapshot.h`
   and exists only on the branch side. Severity: P2 — only catches
   branch-only dirty-bit regressions; does not affect master parity.

5. **Treat the master companion's empty-world `level_done == 2`
   early-stop as a deliberate match condition.** Currently both sides
   stop at tick 1 on an empty world; Phase 07 must verify that once
   walkers are loaded, both sides stop at the **same** tick (per the
   design `## Tick-cadence parity contract` item 2). Severity: P2 —
   passive: the dumper already captures `tick`; Phase 07 just has to
   make sure populated-world runs do not accidentally trip the same
   empty-world short-circuit.

None of these items represent a known **regression in branch gameplay
versus master** today. They are the residue of the Phase 04 / Phase 05
skeleton handover, scoped here so Phase 07's `07-fix-regressions`
phase has a concrete, file-anchored work list. If Phase 07's
populated-world re-run uncovers actual gameplay divergence, those
findings will be appended to this report (per Phase 06's rule that
false-positive `regression` labels are recoverable).
