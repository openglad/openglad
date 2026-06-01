# Replan: OpenGlad Parity Coverage Pass 2

## 1. Context

### The goal
Finish the behavioral-coverage gaps in the OpenGlad parity test suite by adding
the remaining gap-fill scenarios across the 10 categories, mirroring every change
byte-for-byte to the `../openglad-master` (branch `parity-companion`) worktree,
capturing master goldens, and refreshing the final parity docs to a 156-scenario
state.

### Why this replan
The previous workflow bounced repeatedly on already-complete work. Two structural
defects caused it, and this replan fixes both:

1. **One deterministic `check` phase per implement phase.** The old design gave
   each implement phase six verifiers — one command-based `check` plus five
   prompt-less role reviewers (tester, senior-tester, senior-engineer, architect,
   pm) applying open-ended subjective judgment. All six had to pass, so any
   reviewer's discretionary objection bounced the whole phase. Delete the five
   role checkers. Each verifier is a single explicit checklist of exact shell
   commands with binary PASS/FAIL semantics.
2. **Idempotent implement prompts.** Every implement phase begins by verifying
   whether its deliverable already exists and passes; if so, it only ensures the
   commits landed and yields immediately. It does real work only for what is
   missing. This makes already-complete work pass instantly, keeps sessions short,
   and prevents re-churning green work.

The master worktree is already baseline-resynced; `scenario_runtime.cpp` and
`parity_scenario_runtime.cpp` already thread `special_names_table`; 140 scenarios
already exist. None of these are to be regenerated.

### Relevant codebase background
- Parity harness lives in `tests/parity/`:
  - `scenario_table.h` (4841 lines) — `kScenarios[]` table of `ScenarioSpec`
    rows: spawn lists, input lists, fact arrays (`pred::*`), and `kMut_*`
    mutation descriptors (file/line/from/to/rationale).
  - `test_parity_scenarios.cpp` — one `OG_PARITY_TEST(<id>)` macro per row.
  - `scenario_runtime.cpp` — runs each scenario headless; already wires
    `special_names_table` (do not modify).
  - `fact_predicate.h` — predicate vocabulary. Confirmed signatures include:
    `TickReached`, `LevelDoneEquals`, `ScoreDelta`, `WalkerFamilyCount`,
    `WalkerOfTeamAlive`, `WalkerHpRangeAtFinalTick`, `WalkerKeysApplied`,
    `WalkerPositionMoved`, `WalkerDiedByFinal`, `WalkerAliveAtFinal`,
    `TreasureFamilyRemovedFromOblist`,
    `TreasureFamilyOfOrderRemovedFromOblist`, `StatDeltaOnPickup`,
    `EffectFamilyCount`, `EventKindAtLeast`, `EventKindExactly`,
    `WeaponFamilyEmitted`, plus the `branch_only(...)` / `master_only(...)`
    wrappers.
  - `test_parity_coverage_gate.cpp` — 7 gates. Key contracts:
    - `label_exempted(label)` accepts only labels prefixed
      `rng_drift:`, `intended_diff:`, or `consequence:` (lines 702-707).
      Any widened range must carry one of these.
    - `is_consequence_predicate` looks for `consequence:` in the label
      (line 692-698).
    - Per-category depth gates require ≥3/≥4/≥5 non-`TickReached` predicates
      and ≥1 consequence predicate per scenario.
    - `mutation_canary_discriminating_power_gate` validates each mutation's
      file exists and line is in range.
- Golden capture: `scripts/parity/capture_master_golden.sh <scenario...>`.
- Master worktree: `../openglad-master` on branch `parity-companion`; mirror
  target file `../openglad-master/tools/parity_scenario_table.h`; rebuilt
  dump binary `../openglad-master/build/ci-test/parity_dump_master` via
  `cmake --build --preset ci-test --target parity_dump_master`.
- Build/test: `cmake --build --preset ci-test && ctest --preset ci-test`.

### Current state of the tree (measured)
- `grep -c 'OG_PARITY_TEST(' tests/parity/test_parity_scenarios.cpp` → **141** =
  140 scenario invocations + the one `#define OG_PARITY_TEST(NAME)` line. The 140
  real scenarios are 134 preexisting + 4 walker-status + 2 summon.
- 139 goldens in `tests/parity/golden/`. Exactly one scenario
  (`smoke_empty_scen99`, an Invariant scenario) has no golden by design — it is
  the lone `ADD_FAILURE(... "master golden missing" ...)` case.
- **Already done & green:** walker-status-timers (4 scenarios),
  summon-lifecycle (2 scenarios).
- **Not yet done** (not in the table, no golden) — 16 scenarios across 9
  remaining categories:
  generator-saturation (1), weapon-trajectories (3), effect-emission (3),
  effect-timers (1), input-pipeline (4), multiplayer-teams (1),
  level-withdraw (1), midcombat-state (2); plus the final-docs refresh.

End target: **156 scenarios = 134 + 4 + 2 + 16**, with **155 goldens**
(139 + 16; `smoke_empty_scen99` stays golden-less by design). After the 16
additions, `grep -c 'OG_PARITY_TEST(' …` reads **157** (156 scenario
invocations + the `#define`), not 156.

## 2. Generated Workflow Contract

The generated workflow MUST obey these fixed rules:

- **Backend / working dir:** `backend: claude` and `working_dir: .`.
- **Linear execution only.** No `parallel_groups`. Phases run strictly in the
  numbered order below.
- **Self-contained inline-only YAML.** No top-level `include`; no phase-level
  `prompt_file`, `workflow_file`, `workflow_dir`, `checks`, or any other
  YAML-source indirection. Every prompt is inline text.
- **No agent-guided bounce target lists.** Each `check` phase uses exactly one
  fixed `bounce_target` naming its paired implement phase.
- **Every verifier is an explicit top-level `check` phase.** Exactly one `check`
  per implement phase — no role-only checker swarm, no prompt-less checks.
- **Each verifier stays in the implement block it verifies and bounces to that
  implement phase.** The `check` immediately follows its implement phase and its
  `bounce_target` is that implement phase id.
- **Checker-run commands live in the checker prompt.** Any build/test/lint/grep/
  diff the verifier must run is written verbatim into the `check` prompt as a
  command checklist; there are no non-agentic command phases.
- **Consume existing artifacts; do not regenerate.** Each implement prompt lists
  its preexisting inputs and instructs the agent to consume/update them in place
  — never to refetch, recollect, rediscover, or regenerate them. Preserved as
  already-prepared (do NOT recreate or modify):
  - `../openglad-master/` worktree (baseline-resynced, branch
    `parity-companion`).
  - `tests/parity/scenario_runtime.cpp` and
    `../openglad-master/tools/parity_scenario_runtime.cpp` `special_names_table`
    wiring.
  - The 140 existing scenarios + their goldens.
  - The already-landed walker-status-timers and summon-lifecycle scenarios,
    macros, goldens, and mirror commits.
- **Idempotency clause in every implement prompt.** Each implement prompt must
  begin with: "First check whether this phase's deliverable already exists and
  passes (scenarios present in `kScenarios[]`, macros present, goldens on disk,
  the named tests green, mirror in sync). If all already hold, ensure the two
  required git commits have landed and yield immediately — do not redo work.
  Otherwise implement only the missing pieces."
- **Commit-before-yield in every implement prompt.** Every implement prompt must
  instruct the agent to commit its work to git in **both** worktrees (branch +
  master mirror) before yielding, with the exact `git add`/`git commit` commands.
  The branch commit **must include
  `tests/parity/scenario_facts_generated.json`** — this file is git-tracked and
  is regenerated in-source on every `cmake --build --preset ci-test` (via the
  `scenario_facts_generated ALL` custom target wired by
  `CMakeLists.txt:1857-1870`, on which `og_test_parity` depends), so any phase
  that edits `scenario_table.h` and then builds leaves a modified tracked copy
  that must be committed. Omitting it would leave a stale committed copy and a
  dirty tree that `scripts/parity/evaluate_facts.py` and
  `scripts/parity/run_mutation_canary.sh` read as source of truth. It is a
  branch-only artifact (the master mirror commits only
  `tools/parity_scenario_table.h`) and does not need mirroring.
  No `--no-verify` or `--amend` unless flagged in the message as recovery.
- **Empirical numeric values are tunable.** The concrete floors
  (`EventKindAtLeast` counts), HP/position ranges, and tick budgets in each
  phase's Implementation Details are starting points derived from source
  inspection — not hard requirements. The implementer SHOULD adjust them to the
  values actually observed at runtime so that: (a) the unmutated scenario passes
  on the branch; (b) every `EventKindAtLeast` floor is strictly `> 0` (the
  `*depth_gate*` rejects `, 0)` floors); (c) the described mutation still flips
  ≥1 predicate; (d) any widened range carries an
  `intended_diff:`/`rng_drift:`/`consequence:` label; and (e) each scenario keeps
  ≥3 non-`TickReached` predicates including ≥1 predicate whose label begins
  `consequence:`. The scenario id, the mutation's target source line, and the
  predicate set's *shape* (which families/effects it asserts) are fixed; the
  literal numbers are not.
- **Checks are command-deterministic (the core fix).** Every `check` prompt is a
  fixed list of shell commands with binary PASS/FAIL outcomes (build, ctest, the
  named `og_test_parity --gtest_filter` runs, `diff -q`, `ls`, `grep -c`, the
  `*depth_gate*` / `*golden_evaluation_gate*` / `*mutation_canary*` gates, plus
  the two mechanical assertions defined next). The checker emits
  `VERDICT: PASS` iff every command yields its stated result and
  `VERDICT: FAIL: <reason>` otherwise. A checker must never apply open-ended
  subjective judgment beyond the literal command results. There is exactly one
  such checker per implement phase.
- **What the gates actually enforce.** The coverage-gate suite does **not**
  enforce the ≥3-non-`TickReached` + ≥1-`consequence:` requirement for the 16 new
  scenarios. The per-category depth gates are keyed to fixed scenario-id lists and
  id-suffix patterns
  (`predicate_depth_gate_core` → fixed `kCoreIds[]`;
  `predicate_depth_gate_walker_families` → fixed `kFamilyIds[]`;
  `predicate_depth_gate_emissions` → only ids containing `_emission_scen99` /
  `_pickup_scen99`, which the new `..._emit_scen99` ids do **not** match;
  `predicate_depth_gate_specials` → only `id.starts_with("special_")`, which
  `input_special_switch_wrap_scen99` does **not** match). None of the 16 new ids
  fall into any bucket, and the only generic gate
  (`behavioural_coverage_gate_runtime`) requires merely **≥1** non-`TickReached`
  master-true predicate — not ≥3 and not a consequence. What the gate runs *do*
  enforce: 0-floor `EventKindAtLeast` rejection and widened-range labels (both
  via `predicate_depth_gate_no_trivially_wide_ranges`), golden evaluation
  (`*golden_evaluation_gate*`), and mutation file/line validity
  (`*mutation_canary*`). Keep those gate runs.
- **Mechanical depth/consequence assertion (per gap-fill checker).** Because no
  gate covers the new ids, each gap-fill checker enforces the
  ≥3-non-`TickReached` + ≥1-`consequence:` requirement itself by **mechanically
  counting the scenario's `kFacts_<id>[]` array** in `scenario_table.h` — never
  by a gate run and never by judgment. Because this assertion is a literal
  substring search, "≥1 consequence predicate" here means **≥1 predicate whose
  label begins `consequence:`** — *not* the gate's broader kind-based
  `is_consequence_predicate` reading. Every new scenario's fact list MUST
  therefore carry at least one predicate with an explicit `consequence:` label;
  the §3 specs are drafted so each already does (for an HP-range or other widened
  predicate the `consequence:` prefix doubles as the widened-range exemption
  label, which `label_exempted` accepts alongside `intended_diff:`/`rng_drift:`,
  so one label serves both gates). For each new `<id>`, run:
  ```bash
  block=$(awk '/inline constexpr FactPredicate kFacts_<id>\[\]/{f=1} f{print} /^};/{if(f)exit}' tests/parity/scenario_table.h)
  total=$(printf '%s\n' "$block" | grep -c 'pred::')
  ticks=$(printf '%s\n' "$block" | grep -c 'pred::TickReached')
  test $((total - ticks)) -ge 3            # ≥3 non-TickReached predicates
  printf '%s\n' "$block" | grep -q 'consequence:'   # ≥1 consequence predicate
  ```
  (Each predicate begins with `pred::`, so counting `pred::` occurrences is
  reliable even for predicates whose label wraps across lines.) FAIL if either
  assertion fails.
- **Tree-cleanliness assertion (every gap-fill checker + `check-final-docs`).**
  After build+ctest, every checker runs `git status --porcelain tests/parity/`
  and requires **empty** output. This catches the regenerated-but-uncommitted
  `scenario_facts_generated.json` regression (in-session `ctest` would otherwise
  mask it because the build dependency regenerates the file before tests run).

## 3. Implementation Phases

Phase numbering is the linear execution order. Each implement phase is
immediately followed by its single `check` phase, which bounces back to it.

---

### Phase 1 — Confirm prior landed work (walker-status-timers + summon-lifecycle)

- **Phase Name:** Confirm prior landed work
- **Implement Phase ID:** `impl-confirm-prior-work`
- **Verification Phases:**
  - `check-confirm-prior-work` — type `check`, `bounce_target:
    impl-confirm-prior-work`. Deterministically confirm the 6 already-landed
    scenarios are intact, green, mirrored, and committed. Commands:
    - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
    - `./build/ci-test/og_test_parity --gtest_filter='Parity.enemy_freeze_mage_scen99:Parity.invisibility_thief_scen99:Parity.speed_potion_movement_scen99:Parity.invulnerable_potion_scen99:Parity.summon_lifetime_faerie_scen99:Parity.summon_lifetime_decrement_faerie_scen99'`
      → all pass.
    - `ls tests/parity/golden/{enemy_freeze_mage,invisibility_thief,speed_potion_movement,invulnerable_potion,summon_lifetime_faerie,summon_lifetime_decrement_faerie}_scen99.json`
      → all exist.
    - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
      → 0.
    - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'`
      → 7 pass.
    - `git -C . status --porcelain tests/parity/` → no uncommitted parity-test
      changes.
    - Verdict: emit `VERDICT: PASS` if all hold, else `VERDICT: FAIL: <reason>`.
- **Preexisting Inputs:**
  - `tests/parity/scenario_table.h` (contains the 6 landed scenarios).
  - `tests/parity/test_parity_scenarios.cpp` (6 paired macros).
  - The 6 golden files listed above.
  - `../openglad-master/tools/parity_scenario_table.h` (mirror) and built
    `parity_dump_master`.
  - Commits `36ab6e61` and `b6fab712` plus their master-mirror commits.
- **New Outputs:** None unless drift is detected. If `diff -q` shows drift or a
  test fails, re-mirror (`cp tests/parity/scenario_table.h
  ../openglad-master/tools/parity_scenario_table.h`), rebuild
  `parity_dump_master`, re-capture any missing golden, and commit the fix in
  both worktrees.
- **File Changes:** None in the normal (already-green) case.
- **Implementation Details:** Run the build+tests and the filters above; if all
  green and committed, yield immediately — there is nothing to commit. Only if
  something regressed (e.g. a golden missing or mirror drift) perform the minimal
  repair; in that case you **must** commit the repair in both worktrees (branch +
  master mirror) before yielding, including
  `tests/parity/scenario_facts_generated.json` in the branch commit if the build
  regenerated it. Keep the session short.
- **Verification:** the `check-confirm-prior-work` command checklist above.

---

### Phase 2 — Generator saturation

- **Phase Name:** Generator saturation scenario
- **Implement Phase ID:** `impl-generator-saturation`
- **Verification Phases:**
  - `check-generator-saturation` — `check`, `bounce_target:
    impl-generator-saturation`. Commands:
    - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
    - `./build/ci-test/og_test_parity --gtest_filter='Parity.generator_saturation_scen99'`
      → pass.
    - `ls tests/parity/golden/generator_saturation_scen99.json` → exists.
    - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
      → 0.
    - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'`
      → pass.
    - ≥3 non-`TickReached` predicates incl. ≥1 `consequence:`.
- **Preexisting Inputs:** `tests/parity/scenario_table.h`,
  `scenario_runtime.cpp` (wiring; do not modify),
  `test_parity_scenarios.cpp`, `fact_predicate.h`,
  `test_parity_coverage_gate.cpp`,
  `src/gameplay/walker.cpp:1217-1235` (`act_generate`, gates on
  `living_count < MAXOBS`), `src/gameplay/generator_family_registry.cpp:30-37`
  (`FAMILY_TOWER → default_weapon = FAMILY_MAGE`),
  `scripts/parity/capture_master_golden.sh`,
  master worktree + mirror + `parity_dump_master`.
- **New Outputs:** 1 `ScenarioSpec` row `generator_saturation_scen99`, 1
  `OG_PARITY_TEST` macro, 1 `kMut_generator_saturation_scen99` (tab-indented),
  1 golden, mirror update, rebuilt `parity_dump_master`.
- **File Changes:** append to `tests/parity/scenario_table.h` and
  `tests/parity/test_parity_scenarios.cpp`; new golden; `cp`-mirror table;
  rebuild master dump.
- **Implementation Details:**
  - Spawns: `{FAMILY_TOWER,1,kOrderGenerator,60,60,0,0,5,0}`,
    `{FAMILY_SOLDIER,0,kOrderLiving,240,240,0,0}` (observer).
  - Inputs: none (idle). Tick budget 2500.
  - Facts: `TickReached(2500)`; `WalkerFamilyCount(FAMILY_TOWER,1,1)`;
    `WalkerFamilyCount(FAMILY_MAGE, 3, 30, "consequence: generator saturates
    living_count over 2500 ticks; range spans RNG drift")`;
    `EventKindAtLeast(/*play_sound*/1, 4)`;
    `WalkerOfTeamAlive(1, 3, 30, "rng_drift: spawn count varies with per-tick
    RNG")`.
  - Mutation: `src/gameplay/walker.cpp` line 1219 (tab-indented). from:
    `\tif ( current_game->world->living_count < MAXOBS &&` → to: `\tif ( false &&`.
    Rationale: gate always false → generator never fires → 0 FAMILY_MAGE →
    lower-bound failure.
  - Mirror + capture + commit per the standard sequence (§4).
- **Verification:** the `check-generator-saturation` checklist.

---

### Phase 3 — Weapon trajectories

- **Phase Name:** Weapon trajectory scenarios
- **Implement Phase ID:** `impl-weapon-trajectories`
- **Verification Phases:**
  - `check-weapon-trajectories` — `check`, `bounce_target:
    impl-weapon-trajectories`. Commands: build+ctest 0 failures;
    `--gtest_filter='Parity.weapon_rock_slot2_emit_scen99:Parity.weapon_boomerang_return_scen99:Parity.weapon_exploding_boulder_scen99'`
    pass; `ls` the 3 goldens; `diff -q` mirror → 0;
    `--gtest_filter='Parity.predicate_depth_gate_no_trivially_wide_ranges'`
    pass (boomerang 6000-cent HP span must carry an `rng_drift:` label);
    `*depth_gate*:*golden_evaluation_gate*:*mutation_canary*` pass; each
    scenario ≥3 non-`TickReached` incl. ≥1 consequence.
- **Preexisting Inputs:** as Phase 2 plus
  `src/gameplay/families/family_elf.cpp:62-74` (slot 2 BOUNCING ROCKS),
  `src/gameplay/families/family_soldier.cpp:45-52` (slot 2 BOOMERANG),
  `src/gameplay/families/effect_family_shield.cpp:63-134` (boomerang FX),
  `src/gameplay/families/family_barbarian.cpp:23-65` (slot 2 EXPLODING BOULDER,
  line 59 `set_skip_exit(5000)`),
  `src/gameplay/families/weapon_family_projectiles.cpp:14-31`
  (`projectile_explode_on_death`); reuses `kInputsSpecialSlot2`;
  coverage-gate widened-range exemption at lines 702-707 / 893-933.
- **New Outputs:** 3 rows, 3 macros, 3 `kMut_*`, 3 goldens, mirror, dump.
- **File Changes:** append table + macros; 3 goldens; mirror; rebuild dump.
- **Implementation Details:**
  - `weapon_rock_slot2_emit_scen99`: spawns
    `{FAMILY_ELF,0,kOrderLiving,120,120,0,0,4,300}`,
    `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`; reuse `kInputsSpecialSlot2`;
    tick 30. Facts: `TickReached(30)`, `WalkerFamilyCount(FAMILY_ELF,1,1)`,
    `WeaponFamilyEmitted(FAMILY_ROCK, "consequence: elf slot 2 BOUNCING ROCKS
    emits FAMILY_ROCK projectiles; mutation aborts the first fire() so no rock
    ever spawns")`, `WalkerOfTeamAlive(0,1,1)`,
    `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`. Mutation
    `family_elf.cpp:66` 16-space: `fireob = static_cast<weap*>(self->fire());`
    → `return false;`.
  - `weapon_boomerang_return_scen99`: spawns
    `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,4,300}`,
    `{FAMILY_ARCHER,1,kOrderLiving,200,200,0,0}`; reuse `kInputsSpecialSlot2`;
    tick 80. Facts: `TickReached(80)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `EffectFamilyCount(FAMILY_BOOMERANG, 1, 2, -1, 0, "consequence: ...")`,
    `EventKindAtLeast(1,2)`,
    `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 14000, "rng_drift: ...
    (6000-cent span, gate-recognised label)")`. Mutation `family_soldier.cpp:46`
    **12-space** (byte-match the source — it is 12-space-indented, not 16):
    `            newob = summon_entity(self, Order::FX, FAMILY_BOOMERANG);` →
    `            return false;`.
  - `weapon_exploding_boulder_scen99`: spawns
    `{FAMILY_BARBARIAN,0,kOrderLiving,120,120,0,0,5,300}` + 3 soldiers at
    `(160,120)`,`(200,160)`,`(260,200)` team 1; reuse `kInputsSpecialSlot2`;
    tick 60. Facts: `TickReached(60)`,
    `WalkerFamilyCount(FAMILY_BARBARIAN,1,1)`,
    `WeaponFamilyEmitted(FAMILY_BOULDER)`,
    `EffectFamilyCount(FAMILY_EXPLOSION, 1, 4, -1, 0, "consequence: ...")`,
    `WalkerFamilyCount(FAMILY_SOLDIER, 0, 3, "rng_drift: ...")`,
    `EventKindAtLeast(1,3)`. Mutation `family_barbarian.cpp:59` 8-space:
    `alive->set_skip_exit(5000);` → `alive->set_skip_exit(0);`.
- **Verification:** `check-weapon-trajectories` checklist.

---

### Phase 4 — Effect emission breadth

- **Phase Name:** Effect emission scenarios
- **Implement Phase ID:** `impl-effect-emission`
- **Verification Phases:**
  - `check-effect-emission` — `check`, `bounce_target: impl-effect-emission`.
    Commands: build+ctest 0 failures;
    `--gtest_filter='Parity.effect_heartburst_multitarget_scen99:Parity.effect_poison_cloud_emit_scen99:Parity.effect_protection_emit_scen99'`
    pass; `ls` 3 goldens; `diff -q` mirror → 0; gates pass; ≥3/≥1.
- **Preexisting Inputs:** as above plus
  `src/gameplay/families/family_archmage.cpp:209-251` (HEARTBURST per-foe
  explosion loop 237-250), `src/gameplay/families/family_thief.cpp:165-178`
  (POISON CLOUD slot 4, line 169 spawns FAMILY_CLOUD),
  `src/gameplay/families/family_druid.cpp:86-149` (PROTECTION; gated on
  `howmany > 1` friendlies in range 60; line 116 spawns
  FAMILY_CIRCLE_PROTECTION),
  `src/gameplay/families/weapon_family_animate.cpp:32-43,126`; reuses
  `kInputsSpecialSlot2`, `kInputsSpecialSlot4`.
- **New Outputs:** 3 rows, 3 macros, 3 `kMut_*`, 3 goldens, mirror, dump.
- **Implementation Details:**
  - `effect_heartburst_multitarget_scen99`: archmage
    `{FAMILY_ARCHMAGE,0,kOrderLiving,120,120,0,0,4,300}` + 4 soldiers team 1 at
    x∈{160,190,220,250},y=120; reuse `kInputsSpecialSlot2`; tick 30. Facts:
    `TickReached(30)`, `WalkerFamilyCount(FAMILY_ARCHMAGE,1,1)`,
    `EffectFamilyCount(FAMILY_EXPLOSION, 4, 12, -1, 0, "consequence: ...")`,
    `WalkerFamilyCount(FAMILY_SOLDIER, 0, 4, "consequence: ...")`,
    `EventKindAtLeast(1,4)`. Mutation `family_archmage.cpp:239` 24-space:
    `newob = summon_entity(self, Order::FX, FAMILY_EXPLOSION);` → `return false;`.
  - `effect_poison_cloud_emit_scen99`: thief
    `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,10,300}`,
    `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`; reuse `kInputsSpecialSlot4`;
    tick 80. Facts: `TickReached(80)`, `WalkerFamilyCount(FAMILY_THIEF,1,1)`,
    `EffectFamilyCount(FAMILY_CLOUD, 1, 5, -1, 0, "consequence: ...")`,
    `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 15000, "rng_drift: ...")`,
    `EventKindAtLeast(1,3)`. Mutation `family_thief.cpp:169`:
    `newob = summon_entity(self, Order::FX, FAMILY_CLOUD);` → `return false;`.
  - `effect_protection_emit_scen99`: druid
    `{FAMILY_DRUID,0,kOrderLiving,120,120,0,0,10,300}`, **second team-0
    friendly** `{FAMILY_SOLDIER,0,kOrderLiving,150,120,0,0}` (required for
    `howmany > 1`), 2 archers team 1 at `(60,120)`,`(220,120)`; reuse
    `kInputsSpecialSlot4`; tick 150. Facts: `TickReached(150)`,
    `WalkerFamilyCount(FAMILY_DRUID,1,1)`,
    `WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION, "consequence: druid slot 4
    PROTECTION emits a FAMILY_CIRCLE_PROTECTION weapon when ≥1 friendly is in
    range; mutation bypasses creation so the emit never fires")`,
    `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`, `EventKindAtLeast(1,4)`. Mutation
    `family_druid.cpp:116` 32-space:
    `alive = summon_entity(newob, Order::Weapon, FAMILY_CIRCLE_PROTECTION);` →
    `return false;`.
- **Verification:** `check-effect-emission` checklist.

---

### Phase 5 — Effect timers (bomb)

- **Phase Name:** Effect timer scenario
- **Implement Phase ID:** `impl-effect-timers`
- **Verification Phases:**
  - `check-effect-timers` — `check`, `bounce_target: impl-effect-timers`.
    Commands: build+ctest 0; `--gtest_filter='Parity.effect_bomb_timer_scen99'`
    pass; `ls` golden; `diff -q` mirror → 0; gates pass; ≥3/≥1.
- **Preexisting Inputs:** as above plus
  `src/gameplay/families/effect_family_bomb.cpp:17-29,95` (on_death adds
  FAMILY_EXPLOSION; on_act nullptr), `src/gameplay/families/family_thief.cpp:61-91`
  (DROP BOMB slot 1, line 69 spawns FAMILY_BOMB); reuses `kInputsSpecialSlot1`.
- **New Outputs:** 1 row, 1 macro, 1 `kMut_*`, 1 golden, mirror, dump.
- **Implementation Details:**
  - `effect_bomb_timer_scen99`: `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,5,300}`,
    `{FAMILY_SOLDIER,1,kOrderLiving,400,400,0,0}`; reuse `kInputsSpecialSlot1`;
    tick 30. Facts: `TickReached(30)`, `WalkerFamilyCount(FAMILY_THIEF,1,1)`,
    `EffectFamilyCount(FAMILY_BOMB, 1, 2, -1, 0, "consequence: ...")`,
    `EventKindAtLeast(1,2)`, `WalkerOfTeamAlive(0,1,1)`. Mutation
    `family_thief.cpp:69`:
    `newob = current_game->world->add_ob(Order::FX, FAMILY_BOMB, 1);` →
    `return false;`.
- **Verification:** `check-effect-timers` checklist.

---

### Phase 6 — Input pipeline edge cases

- **Phase Name:** Input pipeline scenarios
- **Implement Phase ID:** `impl-input-pipeline`
- **Verification Phases:**
  - `check-input-pipeline` — `check`, `bounce_target: impl-input-pipeline`.
    Commands: build+ctest 0;
    `--gtest_filter='Parity.input_diagonal_movement_scen99:Parity.input_hold_fire_search_scen99:Parity.input_switch_char_scen99:Parity.input_special_switch_wrap_scen99'`
    pass; `ls` 4 goldens; `diff -q` mirror → 0;
    `grep -n 'special_names_table' tests/parity/scenario_runtime.cpp` ≥1
    (wiring intact); gates pass; ≥3/≥1.
- **Preexisting Inputs:** as above plus
  `src/interface/input/input_state.cpp:3-29` (`PlayerInput::move_x/move_y`
  diagonal decode), `src/gameplay/sim_input_handler.cpp:168-195` (SwitchChar /
  `sim_cycle_next_character`), `:201-219` (SwitchSpecial wrap), `:326-351`
  (Fire press vs hold), `include/openglad/gameplay/input_action.h`
  (`InputAction::SwitchChar = 10`); the **already-wired** `special_names_table`
  in both runtime files (do not modify).
- **New Outputs:** 4 rows, 4 macros, 4 `kMut_*`, 4 goldens, mirror, dump.
- **Implementation Details:**
  - `input_diagonal_movement_scen99`: `{FAMILY_SOLDIER,0,kOrderLiving,160,160,0,0}`;
    new inline inputs `{1,0,K_DOWN_RIGHT},{40,0,K_NONE}`; tick 80. Facts:
    `TickReached(80)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `WalkerPositionMoved(FAMILY_SOLDIER, 175, 175, "consequence: ...")`,
    `EventKindAtLeast(1, 1, "rng_drift: ...")`, `WalkerOfTeamAlive(0,1,1)`.
    Mutation `input_state.cpp:26`:
    `        held[static_cast<int>(InputKey::DownRight)])` → `        false)`.
  - `input_hold_fire_search_scen99`: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0}`,
    `{FAMILY_ARCHER,1,kOrderLiving,220,200,0,0}`; inputs `{5,0,K_FIRE},{200,0,K_NONE}`;
    tick 250. Facts: `TickReached(250)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `WeaponFamilyEmitted(FAMILY_KNIFE, "consequence: held-fire keeps the soldier
    firing knives across the run; mutation disables held-fire so far fewer
    knives are emitted")`,
    `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 9000, "rng_drift: ...")`,
    `EventKindAtLeast(1,6)`. Mutation `sim_input_handler.cpp:350`:
    `        if (pi.is_held(InputAction::Fire))` → `        if (false)`.
  - `input_switch_char_scen99`: both walkers team 255 —
    `{FAMILY_SOLDIER,255,kOrderLiving,120,120,0,0,3,200}`,
    `{FAMILY_ARCHER,255,kOrderLiving,100,140,0,0,3,200}`; **`ScenarioSpec.player_team
    = 255`**; inputs
    `{1,0,K_RIGHT},{20,0,K_NONE},{30,0,K_SWITCH},{31,0,K_NONE},{40,0,K_RIGHT},{120,0,K_NONE}`;
    tick 150. Facts: `TickReached(150)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `WalkerFamilyCount(FAMILY_ARCHER,1,1)`,
    `WalkerPositionMoved(FAMILY_ARCHER, 110, 140, "consequence: ...")`,
    `EventKindAtLeast(1,1)`. Mutation `sim_input_handler.cpp:188`:
    `        control = sim_cycle_next_character(level.oblist, oldcontrol, reverse, filter);`
    → `        control = oldcontrol;`.
  - `input_special_switch_wrap_scen99`: `{FAMILY_MAGE,0,kOrderLiving,120,120,0,0,13,600}`,
    `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`; inputs = 12 alternating
    `K_SPECIAL_SWITCH`/`K_NONE` across ticks 5..28 then `{30,0,K_SPECIAL},{31,0,K_NONE}`
    (full list in the original spec, lands on slot 3 FREEZE TIME); tick 150.
    Facts: `TickReached(150)`, `WalkerFamilyCount(FAMILY_MAGE,1,1)`,
    `WalkerPositionMoved(FAMILY_SOLDIER, 200, 120, "consequence: ...")`,
    `EventKindAtLeast(1,2)`,
    `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 15000, "rng_drift: ...")`.
    Mutation `sim_input_handler.cpp:204`:
    `        control->set_current_special(control->current_special() + 1);` →
    `        control->set_current_special(1);`.
- **Verification:** `check-input-pipeline` checklist.

---

### Phase 7 — Multi-team coordination

- **Phase Name:** Multi-team is_friendly scenario
- **Implement Phase ID:** `impl-multiplayer-teams`
- **Verification Phases:**
  - `check-multiplayer-teams` — `check`, `bounce_target: impl-multiplayer-teams`.
    Commands: build+ctest 0;
    `--gtest_filter='Parity.multiplayer_two_teams_scen99'` pass; `ls` golden;
    `diff -q` mirror → 0; gates pass; ≥3/≥1.
- **Preexisting Inputs:** as above plus
  `tests/parity/scenario_runtime.cpp:42-85` (`apply_post_load_spawns` —
  no-myguy branch; do not modify), `src/gameplay/walker.cpp:1675-1742`
  (`is_friendly`; no-myguy branch 1711-1716; load-bearing line 1723).
- **New Outputs:** 1 row, 1 macro, 1 `kMut_*` (tab-indented), 1 golden,
  mirror, dump.
- **Implementation Details:**
  - Spawns: `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,3,200}` (team 0),
    `{FAMILY_THIEF,2,kOrderLiving,140,140,0,0,3,200}` (team 2),
    `{FAMILY_ARCHER,1,kOrderLiving,200,200,0,0}` (team 1); inputs
    `{5,0,K_RIGHT},{30,0,K_NONE},{35,0,K_FIRE},{200,0,K_NONE}`; tick 200.
  - Facts: `TickReached(200)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `WalkerFamilyCount(FAMILY_THIEF,1,1)`,
    `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 9000, "consequence: team-2 thief
    attacks the team-1 archer because their team_num differs (walker.cpp:1723),
    driving archer HP down; mutation makes every pair mutually friendly so the
    archer is barely touched and HP stays high (label_exempted)")`,
    `EventKindAtLeast(1,4)`.
  - Mutation `walker.cpp:1723` (two leading tabs):
    `\t\treturn headus->team_num() == headtarget->team_num();` → `\t\treturn 1;`.
- **Verification:** `check-multiplayer-teams` checklist.

---

### Phase 8 — Level withdraw

- **Phase Name:** Level withdraw scenario
- **Implement Phase ID:** `impl-level-withdraw`
- **Verification Phases:**
  - `check-level-withdraw` — `check`, `bounce_target: impl-level-withdraw`.
    Commands: build+ctest 0;
    `--gtest_filter='Parity.level_withdraw_scen99'` pass; `ls` golden;
    `diff -q` mirror → 0; gates pass;
    `grep -c 'kFamilySpawns_soldier_with_exit_withdraw' tests/parity/scenario_table.h`
    ≥2 (reused by name, not duplicated);
    `grep -c 'kInputsScripted9301' tests/parity/scenario_table.h` ≥2; ≥3
    non-`TickReached` incl. ≥1 consequence (`LevelDoneEquals(2)`).
- **Preexisting Inputs:** as above plus the existing `scripted_input_scen9301`
  row and its constants `kFamilySpawns_soldier_with_exit_withdraw` (lines
  372-376) and `kInputsScripted9301` (lines 263-270) — **reuse by name**;
  `src/gameplay/game_world.cpp:1357,1391-1499` (withdraw handling; `level_done`
  default 2 at 1357), `src/gameplay/families/treasure_family_navigation.cpp:36-98`
  (`exit_on_eat`; line 86 sets `withdraw_requested`; 88-90 emit WithdrawToLevel).
- **New Outputs:** 1 row, 1 macro, 1 new fact array
  `kFacts_level_withdraw_scen99`, 1 `kMut_*`, 1 golden, mirror, dump. No new
  spawn/input constants (reuse by name).
- **Implementation Details:**
  - Spawn list: reuse `kFamilySpawns_soldier_with_exit_withdraw` by name.
  - Inputs: reuse `kInputsScripted9301` by name. Tick budget 200.
  - Facts (`kFacts_level_withdraw_scen99`): `TickReached(200)`,
    `WalkerFamilyCount(FAMILY_SOLDIER, 2, 2)`,
    `LevelDoneEquals(2, "consequence: withdraw path returns level_done=2 ...")`,
    `EventKindAtLeast(/*withdraw_to_level*/8, 1)`, `EventKindAtLeast(1, 2)`.
  - Mutation `treasure_family_navigation.cpp:86` 8-space:
    `        world.withdraw_requested = true;` →
    `        world.withdraw_requested = false;`. Rationale: with the flag false,
    early-break at game_world.cpp:1393 never fires, loop completes, sets
    `level_done = 0` at 1408 → `LevelDoneEquals(2)` fails.
- **Verification:** `check-level-withdraw` checklist.

---

### Phase 9 — Mid-combat state

- **Phase Name:** Mid-combat walker-state scenarios
- **Implement Phase ID:** `impl-midcombat-state`
- **Verification Phases:**
  - `check-midcombat-state` — `check`, `bounce_target: impl-midcombat-state`.
    Commands: build+ctest 0;
    `--gtest_filter='Parity.midcombat_partial_hp_scen99:Parity.consumable_inventory_state_scen99'`
    pass; `ls` 2 goldens; `diff -q` mirror → 0;
    `--gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*:Parity.predicate_depth_gate_no_trivially_wide_ranges'`
    pass (the 7000-cent HP range carries `intended_diff:`); ≥3/≥1.
- **Preexisting Inputs:** as above plus
  `src/gameplay/walker_combat.cpp:178-206` (central combat-damage HP write at
  line 189), `src/gameplay/families/treasure_family_consumables.cpp` (drumstick
  heal at line 25, magic_potion), `src/resources/save_data.cpp` (field defs;
  harness inspects live oblist, not save_game).
- **New Outputs:** 2 rows, 2 macros, 2 `kMut_*`, 2 goldens, mirror, dump.
- **Implementation Details:**
  - `midcombat_partial_hp_scen99`: `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,3,0}`,
    2 archers team 1 at `(60,120)`,`(180,120)`; inputs `{5,0,K_FIRE},{40,0,K_NONE}`;
    tick 80. Facts: `TickReached(80)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4000, 11000, "consequence: the two
    archers deal mid-combat damage that lands the soldier in the mid-HP band;
    mutation zeros the central combat-damage write so HP stays above the upper
    bound (7000-cent span, label_exempted)")`, `WalkerOfTeamAlive(0,1,1)`,
    `EventKindAtLeast(1,5)`. Mutation `walker_combat.cpp:189` 4-space:
    `    target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);`
    → `... - 0);`.
  - `consumable_inventory_state_scen99`: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0,3,0}`,
    `{FAMILY_DRUMSTICK,0,kOrderTreasure,128,120,0,0}`,
    `{FAMILY_MAGIC_POTION,0,kOrderTreasure,160,120,0,0}`; inputs
    `{1,0,K_RIGHT},{40,0,K_NONE}`; tick 150. Facts: `TickReached(150)`,
    `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
    `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_DRUMSTICK, kOrderTreasure)`,
    `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_MAGIC_POTION, kOrderTreasure)`,
    `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 9000, 13000, "consequence: eating
    the drumstick boosts the soldier's HP into the upper band; mutation no-ops
    the heal so HP drops below the lower bound (label_exempted)")`.
    Mutation `treasure_family_consumables.cpp:25` 4-space:
    `    eater->stats()->set_hitpoints(eater->stats()->hitpoints() + amount);` →
    `... + 0);`.
- **Verification:** `check-midcombat-state` checklist.

---

### Phase 10 — Final docs refresh

- **Phase Name:** Final docs refresh (156-scenario state)
- **Implement Phase ID:** `impl-final-docs`
- **Verification Phases:**
  - `check-final-docs` — `check`, `bounce_target: impl-final-docs`. Commands:
    - `cmake --build --preset ci-test && ctest --preset ci-test` → 0.
    - `./build/ci-test/og_test_parity --gtest_filter='*'` → 0 failures.
    - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
      → 0.
    - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
    - `ls tests/parity/golden/*.json | wc -l` → 155.
    - `grep -c 'OG_PARITY_TEST(' tests/parity/test_parity_scenarios.cpp` → 157
      (156 scenario invocations + the single `#define OG_PARITY_TEST(NAME)`
      line; this is the correct value — it is **not** 156).
    - explicit `ls` of all 16 new golden files (generator/weapon/effect/input/
      multiplayer/level/midcombat) → all exist.
    - `grep -c 'EventKindAtLeast.*,\s*0)' tests/parity/scenario_table.h` → 0.
    - `grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp`
      → 0.
    - `grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp`
      → 1.
    - `grep -c 'special_names_table' tests/parity/scenario_runtime.cpp ../openglad-master/tools/parity_scenario_runtime.cpp`
      → ≥2.
    - `grep -c '156 scenarios' .plan/parity-present-state.md` → ≥1.
    - `grep -c 'Gap-Fill Scenarios' .plan/parity-coverage-manifest.md` → ≥1.
    - `git status --porcelain tests/parity/` → empty (no uncommitted
      regenerated `scenario_facts_generated.json` or other parity-test changes).
- **Preexisting Inputs:** the full 156-scenario `scenario_table.h`, 156 macros,
  155 goldens, both runtime files (wiring intact), the mirror + dump, and the
  existing docs `.plan/parity-present-state.md`,
  `.plan/parity-coverage-manifest.md` (update in place).
- **New Outputs:** updated `.plan/parity-present-state.md` (156 total, 154
  SemanticParity + 2 Invariant, 155 goldens, per-category depth table) and
  `.plan/parity-coverage-manifest.md` (new `## Gap-Fill Scenarios Added in
  Coverage Pass 2` section grouping the 22 gap-fill scenarios by the 10 phase
  categories + updated coverage table). No source code changes.
- **Implementation Details:** Step 1 re-confirm mirror in sync (re-mirror +
  rebuild dump if `diff -q` shows drift). Step 2 build + ctest 0 failures.
  Steps 3-4 rewrite the two docs. Per-category counts after gap-fill:
  `status_timer:4, summon:3, generator:5, weapon:23, effect:19, input:4,
  multiplayer:1, level_transition:2, midcombat:2`. Leave the missing-golden
  policy at `1` ADD_FAILURE / `0` GTEST_SKIP. Commit the final docs; if the
  Step 2 build regenerated `tests/parity/scenario_facts_generated.json`, include
  it in the same branch commit so `git status --porcelain tests/parity/` ends
  empty.
- **Verification:** `check-final-docs` checklist.

## 4. Standard Sequences

### Per-implement-phase sequence (applies to Phases 2-9)

Every gap-fill implement prompt must, after the idempotency check, follow:
1. Append spawn/input/fact/mutation/`ScenarioSpec` to `scenario_table.h` and the
   `OG_PARITY_TEST(...)` macro to `test_parity_scenarios.cpp`. Each mutation
   `from`/`to` must byte-match the cited source line including tabs vs spaces.
   Every widened range must carry an `intended_diff:`, `rng_drift:`, or
   `consequence:` label, and each scenario must have ≥3 non-`TickReached`
   predicates including at least one predicate whose label begins `consequence:`
   (the per-phase checker enforces this by a literal `grep -q 'consequence:'` on
   the `kFacts_<id>[]` block, so a kind-based consequence with no label is not
   sufficient — give one predicate an explicit `consequence:` label).
2. `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.
3. In master worktree: `cmake --build --preset ci-test --target parity_dump_master`.
4. From branch: `scripts/parity/capture_master_golden.sh <scenario...>`.
5. `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
   Wrap any predicate that diverges between branch and captured master in
   `pred::branch_only(...)` / `pred::master_only(...)` with an `intended_diff:`
   or `rng_drift:` label.
6. **Commit in both worktrees before yielding** (the branch commit MUST include
   `tests/parity/scenario_facts_generated.json`, which the build regenerated):
   - `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h <category>"`
   - `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json <new goldens> && git commit -m "parity-cov: <category> scenarios"`
   After committing, `git status --porcelain tests/parity/` must be empty.
   No `--no-verify`/`--amend` unless flagged as recovery.

### Per-check-phase commands (applies to Phases 2-9)

In addition to the per-phase command checklists in §3, **every** gap-fill
`check` phase runs these two mechanical assertions (defined in §2) — the
per-phase "≥3 non-`TickReached` incl. ≥1 consequence" bullet is satisfied by the
first of them, *not* by any gate run:

- **Mechanical depth/consequence count** — for each of that phase's new scenario
  ids, count `pred::` minus `pred::TickReached` in the `kFacts_<id>[]` block of
  `scenario_table.h` and require ≥3, and `grep -q 'consequence:'` the block
  (the `awk`/`grep` snippet in §2). FAIL if either fails.
- **Tree-cleanliness** — `git status --porcelain tests/parity/` must be empty
  (catches an uncommitted regenerated `scenario_facts_generated.json`).

`check-final-docs` (Phase 10) also runs the tree-cleanliness assertion; its
depth/consequence counts are covered by the existing full-suite gate runs.

## 5. Critical Files

- `tests/parity/scenario_table.h` — append 16 new `ScenarioSpec` rows, their
  spawn/input/fact arrays, and 16 `kMut_*` mutation descriptors (Phases 2-9).
  Mirror target: `../openglad-master/tools/parity_scenario_table.h` (byte-copy
  each phase).
- `tests/parity/test_parity_scenarios.cpp` — append 16 `OG_PARITY_TEST(...)`
  macros (final total 156). The single `ADD_FAILURE` missing-golden line stays.
- `tests/parity/golden/*.json` — 16 new master goldens captured via
  `capture_master_golden.sh` (final total 155).
- `../openglad-master/build/ci-test/parity_dump_master` — rebuilt each phase.
- `.plan/parity-present-state.md`, `.plan/parity-coverage-manifest.md` — refreshed
  in Phase 10.
- **Do NOT modify:** `tests/parity/scenario_runtime.cpp`,
  `../openglad-master/tools/parity_scenario_runtime.cpp`,
  `tests/parity/test_parity_coverage_gate.cpp`, `tests/parity/fact_predicate.h`,
  and all game-source files (mutations are *described* in `kMut_*`, the source
  itself is never edited).

## 6. Final Verification

After all phases complete:
- `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
- `./build/ci-test/og_test_parity --gtest_filter='*'` → 0 failures across all
  156 scenarios, the 7 coverage gates, and existing fixtures.
- `grep -c 'OG_PARITY_TEST(' tests/parity/test_parity_scenarios.cpp` → 157
  (156 scenario invocations + the one `#define OG_PARITY_TEST(NAME)` line).
- `ls tests/parity/golden/*.json | wc -l` → 155.
- `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
  → 0 (byte-identical mirror).
- `test -x ../openglad-master/build/ci-test/parity_dump_master` → present.
- `grep -c 'special_names_table' tests/parity/scenario_runtime.cpp ../openglad-master/tools/parity_scenario_runtime.cpp`
  → ≥2 (wiring intact).
- Docs: `.plan/parity-present-state.md` references "156 scenarios";
  `.plan/parity-coverage-manifest.md` has the "Gap-Fill Scenarios" section.
- Both worktrees clean (`git status --porcelain` shows no uncommitted
  parity/test/doc changes); every phase's two commits landed.
