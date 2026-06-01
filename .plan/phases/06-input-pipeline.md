# Phase 06 — Input pipeline edge cases

## Phase Name
Input pipeline scenarios

## Implement Phase ID
`impl-input-pipeline`

## Preexisting Inputs
- `tests/parity/scenario_table.h`, `tests/parity/scenario_runtime.cpp` (the
  **already-wired** `special_names_table` — **do not modify**),
  `tests/parity/test_parity_scenarios.cpp`, `tests/parity/fact_predicate.h`,
  `tests/parity/test_parity_coverage_gate.cpp`.
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree + mirror + `parity_dump_master`; the
  **already-wired** `special_names_table` in
  `../openglad-master/tools/parity_scenario_runtime.cpp` (**do not modify**).
- `src/interface/input/input_state.cpp:3-29` (`PlayerInput::move_x/move_y` diagonal decode).
- `src/gameplay/sim_input_handler.cpp:168-195` (SwitchChar / `sim_cycle_next_character`),
  `:201-219` (SwitchSpecial wrap), `:326-351` (Fire press vs hold).
- `include/openglad/gameplay/input_action.h` (`InputAction::SwitchChar = 10`).

## New Outputs
4 `ScenarioSpec` rows, 4 macros, 4 `kMut_*`, 4 goldens, mirror update, rebuilt dump:
- `input_diagonal_movement_scen99`
- `input_hold_fire_search_scen99`
- `input_switch_char_scen99`
- `input_special_switch_wrap_scen99`

## File Changes
Append table rows + macros (with new inline input arrays); 4 new goldens; mirror
the table; rebuild master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes
(all 4 scenarios in `kScenarios[]`, macros present, goldens on disk, named tests
green, mirror in sync). If all hold, ensure the two commits landed and yield
immediately — do not redo work. Otherwise implement only the missing pieces.

- **`input_diagonal_movement_scen99`**: `{FAMILY_SOLDIER,0,kOrderLiving,160,160,0,0}`;
  new inline inputs `{1,0,K_DOWN_RIGHT},{40,0,K_NONE}`; tick 80. Facts:
  `TickReached(80)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `WalkerPositionMoved(FAMILY_SOLDIER, 175, 175, "consequence: ...")`,
  `EventKindAtLeast(1, 1, "rng_drift: ...")`, `WalkerOfTeamAlive(0,1,1)`.
  Mutation `input_state.cpp:26`:
  `        held[static_cast<int>(InputKey::DownRight)])` → `        false)`.
- **`input_hold_fire_search_scen99`**: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0}`,
  `{FAMILY_ARCHER,1,kOrderLiving,220,200,0,0}`; inputs `{5,0,K_FIRE},{200,0,K_NONE}`;
  tick 250. Facts: `TickReached(250)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `WeaponFamilyEmitted(FAMILY_KNIFE, "consequence: held-fire keeps the soldier firing knives across the run; mutation disables held-fire so far fewer knives are emitted")`,
  `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 9000, "rng_drift: ...")`, `EventKindAtLeast(1,6)`.
  Mutation `sim_input_handler.cpp:350`:
  `        if (pi.is_held(InputAction::Fire))` → `        if (false)`.
- **`input_switch_char_scen99`**: both walkers team 255 —
  `{FAMILY_SOLDIER,255,kOrderLiving,120,120,0,0,3,200}`,
  `{FAMILY_ARCHER,255,kOrderLiving,100,140,0,0,3,200}`; **`ScenarioSpec.player_team = 255`**;
  inputs `{1,0,K_RIGHT},{20,0,K_NONE},{30,0,K_SWITCH},{31,0,K_NONE},{40,0,K_RIGHT},{120,0,K_NONE}`;
  tick 150. Facts: `TickReached(150)`, `WalkerFamilyCount(FAMILY_SOLDIER,1,1)`,
  `WalkerFamilyCount(FAMILY_ARCHER,1,1)`,
  `WalkerPositionMoved(FAMILY_ARCHER, 110, 140, "consequence: ...")`, `EventKindAtLeast(1,1)`.
  Mutation `sim_input_handler.cpp:188`:
  `        control = sim_cycle_next_character(level.oblist, oldcontrol, reverse, filter);` → `        control = oldcontrol;`.
- **`input_special_switch_wrap_scen99`**: `{FAMILY_MAGE,0,kOrderLiving,120,120,0,0,13,600}`,
  `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`; inputs = 12 alternating
  `K_SPECIAL_SWITCH`/`K_NONE` across ticks 5..28 then `{30,0,K_SPECIAL},{31,0,K_NONE}`
  (full list per the original spec — lands on slot 3 FREEZE TIME); tick 150.
  Facts: `TickReached(150)`, `WalkerFamilyCount(FAMILY_MAGE,1,1)`,
  `WalkerPositionMoved(FAMILY_SOLDIER, 200, 120, "consequence: ...")`,
  `EventKindAtLeast(1,2)`,
  `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 15000, "rng_drift: ...")`.
  Mutation `sim_input_handler.cpp:204`:
  `        control->set_current_special(control->current_special() + 1);` → `        control->set_current_special(1);`.

Empirical numerics (`EventKindAtLeast` floors, HP/position ranges, tick budgets)
are tunable starting points — adjust to runtime so each unmutated scenario passes,
every floor is `> 0`, each mutation flips ≥1 predicate, every widened range carries
a label, and each scenario keeps ≥3 non-`TickReached` predicates incl. ≥1
`consequence:`-labelled one.

Follow the standard per-implement sequence (append → `cp`-mirror → rebuild master
dump → `capture_master_golden.sh input_diagonal_movement_scen99 input_hold_fire_search_scen99 input_switch_char_scen99 input_special_switch_wrap_scen99`
→ build+ctest 0 failures; wrap branch/master-divergent predicates in
`branch_only`/`master_only`).

## Verification Phases
- **`check-input-pipeline`** — type `check`, `bounce_target: impl-input-pipeline`.
  Purpose: deterministically confirm the 4 scenarios are present, green, mirrored,
  committed, depth/consequence-compliant, and that the runtime wiring is intact.
  Emit `VERDICT: PASS`/`VERDICT: FAIL: <reason>`. Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.input_diagonal_movement_scen99:Parity.input_hold_fire_search_scen99:Parity.input_switch_char_scen99:Parity.input_special_switch_wrap_scen99'` → pass.
  - `ls tests/parity/golden/{input_diagonal_movement,input_hold_fire_search,input_switch_char,input_special_switch_wrap}_scen99.json` → all exist.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `grep -n 'special_names_table' tests/parity/scenario_runtime.cpp` → ≥1 (wiring intact).
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - **Mechanical depth/consequence count** for each of the 4 new ids — run the
    `awk`/`grep` snippet on each `kFacts_<id>[]` block: require `≥3` non-`TickReached`
    predicates and `grep -q 'consequence:'`. FAIL if any fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- All 4 scenarios pass; 4 goldens exist; mirror byte-identical.
- `special_names_table` wiring intact in the branch runtime file.
- All coverage/golden/mutation gates pass.
- Each scenario has ≥3 non-`TickReached` predicates incl. ≥1 `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h input-pipeline"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/input_diagonal_movement_scen99.json tests/parity/golden/input_hold_fire_search_scen99.json tests/parity/golden/input_switch_char_scen99.json tests/parity/golden/input_special_switch_wrap_scen99.json && git commit -m "parity-cov: input-pipeline scenarios"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**.
After committing, `git status --porcelain tests/parity/` must be empty.
No `--no-verify`/`--amend` unless flagged as recovery.
