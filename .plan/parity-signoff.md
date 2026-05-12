# Parity Sign-off — Phase 08

This is the final sign-off for the gameplay parity framework that
proves `wip/networking` has not regressed gameplay vs `origin/master`
(`16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`). It consumes prior
phase outputs verbatim and does not re-derive any of them:

- `.plan/parity-risk-inventory.md` (Phase 01) — 12 at-risk subsystems.
- `.plan/parity-harness-design.md` (Phase 03) — schema v1, scenario
  list, coverage matrix, partial-coverage caveats.
- `.plan/parity-divergence-report.md` (Phase 06) — divergence report
  for the run that produced the 15 committed goldens.
- `.plan/parity-fixes.md` (Phase 07) — Phase 07 disposition log.

Authoritative pointers used by this document (do not re-audit):

- Master companion commit on `parity-companion`:
  `ce70d23286f1e8034284e7c718ec658065f525e5`.
- Master parent on `origin/master`:
  `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`.
- Branch parity test binary: `og_test_parity` (15 + 1 = 16 GoogleTest
  entries), registered in `CMakeLists.txt:1807`.
- Committed goldens: `tests/parity/golden/*.json`, 15 files, one per
  master-comparable scenario.

## Final status

Parity overall: **GREEN**. Every scenario in
`.plan/parity-divergence-report.md` `## Per-scenario results` is
classified `pass`. The Phase 07 audit
(`.plan/parity-fixes.md` `## Verification of the clean state`)
re-confirmed this against branch HEAD at the time of the audit and
recorded the same `pass` outcome.

Per-scenario status (mirrors the Phase 06 table; no re-derivation):

| scenario_id                       | result | mode                         |
|-----------------------------------|--------|------------------------------|
| `ai_idle_wander_scen9301`         | pass   | byte-equal to golden         |
| `combat_attack_scen99`            | pass   | byte-equal to golden         |
| `special_archmage_scen123`        | pass   | byte-equal to golden         |
| `special_cleric_scen124`          | pass   | byte-equal to golden         |
| `special_mage_scen126`            | pass   | byte-equal to golden         |
| `special_thief_scen789`           | pass   | byte-equal to golden         |
| `effect_bomb_lifetime_scen99`     | pass   | byte-equal to golden         |
| `effect_chain_scen9410`           | pass   | byte-equal to golden         |
| `summon_druid_pet_scen950`        | pass   | byte-equal to golden         |
| `scoring_after_combat_scen99`     | pass   | byte-equal to golden         |
| `save_roundtrip_scen99`           | pass   | byte-equal to golden         |
| `exit_trigger_scen9302`           | pass   | byte-equal to golden         |
| `tick_cadence_scen9301`           | pass   | byte-equal to golden         |
| `rng_seed_stable_scen99`          | pass   | byte-equal to golden         |
| `scripted_input_scen9301`         | pass   | byte-equal to golden         |
| `snapshot_dirty_bits_scen9301`    | pass   | branch-internal invariant    |

Full-suite verification on a clean build at the start of this phase
(commit base `334461df parity-fix(phase07): record clean parity state
in .plan/parity-fixes.md`):

- `rm -rf build/ci-test && cmake --preset ci-test && cmake --build --preset ci-test`
  — exit code 0.
- `ctest --preset ci-test --output-on-failure` — exit code 0; 37/37
  tests passed (24 integration including `og_test_parity` at index
  24, 7 unit, 5 script/server entries, 1 emscripten build test;
  total wall time 210.38 s). Parity groups are subsumed by this
  invocation per the CMake registration in `CMakeLists.txt:1807`.

CI integration: `.github/workflows/test.yml` runs the same
`ctest --preset ci-test` plus an explicit
`ctest ... -R '^og_(test|unit)_parity'` step so a parity failure shows
up as its own CI summary line. The workflow does NOT re-capture
goldens; goldens are committed under `tests/parity/golden/` and are
read by `og_test_parity` at test time.

## Subsystem coverage

The coverage matrix below is the canonical one from
`.plan/parity-harness-design.md` `## Coverage matrix`, lifted in full
so this sign-off is self-contained. Every numbered subsystem in
`.plan/parity-risk-inventory.md` `## Subsystems at risk` (1–12)
appears here. Subsystems with a known coverage gap are documented in
`## Open risks` below, repeating the Phase 03
`Subsystems with partial parity coverage` table verbatim.

| Phase 01 subsystem | covering parity test(s) |
|---|---|
| 1. Walker AI and movement | `ai_idle_wander_scen9301`, `tick_cadence_scen9301`, `scripted_input_scen9301` |
| 2. Combat math and damage | `combat_attack_scen99` |
| 3. Special abilities per family | `special_archmage_scen123`, `special_cleric_scen124`, `special_mage_scen126`, `special_thief_scen789` (4 of 15 families directly; remaining 11 indirectly via walker tick paths — see `## Open risks`) |
| 4. Effect lifecycle | `effect_bomb_lifetime_scen99`, `effect_chain_scen9410` (door-open and ghost-scare effects covered indirectly only — see `## Open risks`) |
| 5. Summon and pet behavior | `summon_druid_pet_scen950` |
| 6. Scoring and team statistics | `scoring_after_combat_scen99` |
| 7. Save format read / write | `save_roundtrip_scen99` (in-process `stringstream` round-trip; on-disk `.glad` archive not covered — see `## Open risks`) |
| 8. Scenario load and exit-trigger firing | `exit_trigger_scen9302` |
| 9. Tick cadence (sim-vs-render decoupling) | `tick_cadence_scen9301` (primary); every other scenario verifies cadence indirectly via the `tick == tick_budget` rule in `.plan/parity-harness-design.md` `## Comparison rules` |
| 10. RNG seeding and determinism | `rng_seed_stable_scen99` (primary); every byte-equal scenario relies on `rng_state` matching exactly |
| 11. Per-frame transport shadow (single-player path) | `scripted_input_scen9301` (variant routed through `local_transport_shadow` on the branch side per Phase 03 `## Tick-cadence parity contract` item 3) |
| 12. World snapshot + dirty-field bookkeeping | `snapshot_dirty_bits_scen9301` (branch-internal; no master golden) |

All twelve Phase 01 subsystems have at least one parity probe; none
appear under "framework does not cover at all." Three subsystems have
documented gaps that fall to manual QA; those are enumerated in
`## Open risks`.

Out-of-scope items from `.plan/parity-risk-inventory.md` `## Out of
scope` (WebSocket transports, lobby UI, multiplex internals,
Emscripten-only paths, replay subsystem, FPS overlay, stale build
artifacts, prior-task `.plan/` residue) are intentionally not
exercised by the parity framework; they are not gameplay-parity
concerns and remain the responsibility of their own tests and manual
smoke.

## How to re-run

Run the full test suite (parity tests are subsumed):

```
cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test
```

Run only the parity groups (faster local feedback; same command CI
uses for its parity-specific step):

```
cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test -R '^og_(test|unit)_parity'
```

The regex matches `og_test_parity` today and is forward-compatible
with an optional `og_unit_parity` (not currently registered; see
`.plan/parity-harness-design.md` `## Test groups to add`).

### Capturing fresh goldens

Goldens live in `tests/parity/golden/*.json` and are read by
`og_test_parity` at runtime. They are NOT re-captured by CI. Fresh
capture is a developer step and is needed when, and only when, one of
the following is true:

1. `origin/master` advanced and the branch wants to rebase onto the
   new master commit (the goldens are pinned to the master parent
   `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd` recorded in Phase 06
   and Phase 07).
2. The shared scenario table
   (`tests/parity/scenario_table.h` and the byte-identical
   `../openglad-master/tools/parity_scenario_table.h`) was changed —
   adding, removing, or re-tuning a scenario — and the master
   companion needs to re-emit.
3. The canonical JSON emitter changed on either side (this is a
   schema-v1 break and must be planned per
   `.plan/parity-harness-design.md` `## State dump schema v1`).

Capture procedure:

```
# In the master worktree at ../openglad-master, on branch parity-companion:
cd ../openglad-master
git checkout parity-companion
cmake --preset ci-test
cmake --build --preset ci-test --target parity_dump_master

# Back in the branch repo:
cd -
scripts/parity/capture_master_golden.sh
```

The script
(`scripts/parity/capture_master_golden.sh`) drives
`../openglad-master/build/ci-test/parity_dump_master` once per
master-comparable scenario, writes each canonical JSON to
`tests/parity/golden/<id>.json`, and validates each dump against
schema v1 via `scripts/parity/validate_schema.py`. After re-capture,
re-run the parity tests and commit the updated goldens.

Do NOT re-capture goldens to "make the tests pass" — if a re-capture
silently fixes a previously failing scenario, the diff that capture
hid is exactly the regression the framework is supposed to catch.
Re-capture is a deliberate rebase / schema-bump action.

## Known intended diffs

None. `.plan/parity-divergence-report.md` `## Classified divergences`
records zero `intended_diff` rows (table body `_(none observed)_`),
and Phase 07's `## Re-classification log` records no items moved from
`regression` to `intended_diff` (table body `_(none)_`).

The Phase 06 strict rule for `intended_diff` (every row must cite a
branch commit explicitly authorising the behaviour change) is
vacuously satisfied, and there is therefore no commit-SHA list to
copy here. If a future re-capture surfaces a divergence that
maintainers accept as intentional, that row must be added to this
section with the citing commit before it is allowed to merge.

## Open risks

The parity framework does not exercise every code path that
`.plan/parity-risk-inventory.md` lists as at risk. The gaps below are
lifted verbatim from `.plan/parity-harness-design.md`
`### Subsystems with partial parity coverage`, which Phase 03
explicitly flagged for this section.

| subsystem | gap | proposed manual-QA step |
|---|---|---|
| 3. Special abilities (per family) | Specials for `archer`, `druid` (direct cast), `elf`, `soldier`, `slime`, `barbarian`, `orc`, `skeleton`, `ghost`, `fire_elemental` are not individually scenario-probed (only `archmage`, `cleric`, `mage`, `thief` are). The other eleven families are exercised indirectly by walker AI in other scenarios, which catches movement / aggression / death drift but not a family-specific special-cast regression. | Manual run of `temp/scen/scen9421.fss` and `temp/scen/scen9450.fss` (existing multi-family arenas) with each family selected from the picker; eyeball that each special's effect (projectile, heal, summon, teleport, shockwave) fires at the right moment and with the right visual / damage outcome on both branches. |
| 4. Effect lifecycle | Door-open (`effect_family_door_open`) and ghost-scare (`effect_family_ghost_scare`) effects are not individually probed; they are only covered as side effects of family specials, which the bomb / chain probes do not trigger. | Manual run of `scen/scen9303.fss` (door / portal heavy) and `scen/scen9304.fss` (ghost-heavy) on both branches; observe that doors open / close on the same trigger tile and that ghost-scare visuals fire on the same enemy contact. |
| 7. Save format | Round-trip is in-process via `std::stringstream`; no real on-disk `.glad`-archive save is written via `zip_api.cpp` and re-opened. The PhysFS / libzip persistence layer is therefore not parity-tested. | Manual `Save Game` from the picker, then `Continue` from the same slot on both branch and master; byte-compare the on-disk save blob. If the binary content matches, persistence parity is held. |

Additional structural risks the framework cannot fully exercise:

- **WebSocket / networked-lobby behaviour.** Listed under Phase 01
  `## Out of scope`; pure-addition code with no master analog. Manual
  QA owner: networked-lobby smoke (start a server, join a client,
  pick teams, verify a fixed scenario completes with the same outcome
  reported to both clients).
- **Emscripten build.** Web parity is not exercised by `ci-test`; the
  `web-emscripten` preset has its own CI lane. Any regression in
  web-only paths will not be caught by this parity harness.
- **Render-side timing / FPS overlay.** Render parity is out of scope
  by design (sim-only). Cosmetic regressions in frame pacing are
  visible only through the standalone FPS overlay docs in
  `.plan/show-fps-*.md`.

If any of the items in this section turn out, in practice, to be
load-bearing parity blockers, the right response is to author a
focused parity scenario for them (extending `scenario_table.h` on
both sides under the Phase 03 synchronisation rules) and re-capture
goldens. This sign-off intentionally does not pretend they are
already covered.

## Manual canary check

Use this procedure to verify the parity framework is actually
sensitive to a real gameplay change. It perturbs a single gameplay
constant, re-runs one parity scenario, and observes a readable diff.

1. **Pick a scenario and a constant.** A safe pair is the
   `rng_seed_stable_scen99` scenario and the seed for that scenario
   in `tests/parity/scenario_table.h`. Look up its `rng_seed`
   (`0x00000001` per Phase 03 `## Scenario list`).

2. **Perturb on the branch side only.** Edit
   `tests/parity/scenario_table.h` and change the entry for
   `rng_seed_stable_scen99` from `0x00000001` to, say, `0x00000002`.
   Do NOT change the master companion's
   `tools/parity_scenario_table.h` (the byte-for-byte synchronisation
   rule from Phase 03 is what we are deliberately violating for this
   canary).

3. **Re-run only that scenario.**

   ```
   cmake --build --preset ci-test --target og_test_parity
   ctest --preset ci-test -R og_test_parity --output-on-failure --gtest_filter='Parity.rng_seed_stable_scen99'
   ```

4. **Observe the diff.** Expected: the test fails with a
   `rng_drift`-category mismatch. The GoogleTest output should
   surface the first few categorised differences (per the comparison
   rules in `.plan/parity-harness-design.md` `## Comparison rules`
   item 8), and the machine-readable summary line
   `SCENARIO rng_seed_stable_scen99 FAIL rng_drift rng_state` should
   appear from `scripts/parity/diff_dumps.py` if invoked directly.

5. **Revert.** `git checkout -- tests/parity/scenario_table.h`. Re-run
   the same `ctest` command; the scenario should turn green again.

If the canary in step 4 silently passes, the parity framework is no
longer wired up correctly — investigate before trusting any green
parity result. The framework is only meaningful if it can see the
perturbation we just introduced.
