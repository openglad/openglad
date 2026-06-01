# Phase 02 — Generator saturation

## Phase Name
Generator saturation scenario

## Implement Phase ID
`impl-generator-saturation`

## Preexisting Inputs
- `tests/parity/scenario_table.h` (append target).
- `tests/parity/scenario_runtime.cpp` (wiring — **do not modify**).
- `tests/parity/test_parity_scenarios.cpp` (append target).
- `tests/parity/fact_predicate.h` (predicate vocabulary).
- `tests/parity/test_parity_coverage_gate.cpp` (gate contracts).
- `src/gameplay/walker.cpp:1217-1235` (`act_generate`, gates on `living_count < MAXOBS`).
- `src/gameplay/generator_family_registry.cpp:30-37` (`FAMILY_TOWER → default_weapon = FAMILY_MAGE`).
- `scripts/parity/capture_master_golden.sh`.
- `../openglad-master/` worktree (branch `parity-companion`), mirror file
  `../openglad-master/tools/parity_scenario_table.h`, and built
  `../openglad-master/build/ci-test/parity_dump_master`.

## New Outputs
- 1 `ScenarioSpec` row `generator_saturation_scen99`.
- 1 `OG_PARITY_TEST(generator_saturation_scen99)` macro.
- 1 `kMut_generator_saturation_scen99` mutation descriptor (tab-indented).
- 1 golden `tests/parity/golden/generator_saturation_scen99.json`.
- Mirror update + rebuilt `parity_dump_master`.

## File Changes
Append to `tests/parity/scenario_table.h` and `tests/parity/test_parity_scenarios.cpp`;
new golden; `cp`-mirror the table; rebuild the master dump.

## Implementation Details
First check whether this phase's deliverable already exists and passes (scenario
present in `kScenarios[]`, macro present, golden on disk, the named test green,
mirror in sync). If all already hold, ensure the two required git commits have
landed and yield immediately — do not redo work. Otherwise implement only the
missing pieces.

- Spawns: `{FAMILY_TOWER,1,kOrderGenerator,60,60,0,0,5,0}`,
  `{FAMILY_SOLDIER,0,kOrderLiving,240,240,0,0}` (observer).
- Inputs: none (idle). Tick budget 2500.
- Facts: `TickReached(2500)`; `WalkerFamilyCount(FAMILY_TOWER,1,1)`;
  `WalkerFamilyCount(FAMILY_MAGE, 3, 30, "consequence: generator saturates living_count over 2500 ticks; range spans RNG drift")`;
  `EventKindAtLeast(/*play_sound*/1, 4)`;
  `WalkerOfTeamAlive(1, 3, 30, "rng_drift: spawn count varies with per-tick RNG")`.
- Mutation: `src/gameplay/walker.cpp` line 1219 (tab-indented).
  from: `\tif ( current_game->world->living_count < MAXOBS &&` → to: `\tif ( false &&`.
  Rationale: gate always false → generator never fires → 0 FAMILY_MAGE →
  lower-bound failure.

Empirical numeric values (the `EventKindAtLeast` floor, the count ranges, the
tick budget) are tunable starting points: adjust to runtime-observed values so
the unmutated scenario passes on the branch, every `EventKindAtLeast` floor is
strictly `> 0`, the mutation still flips ≥1 predicate, any widened range carries
an `intended_diff:`/`rng_drift:`/`consequence:` label, and the scenario keeps
≥3 non-`TickReached` predicates including ≥1 whose label begins `consequence:`.

Follow the standard per-implement sequence: (1) append spawn/input/fact/mutation/
`ScenarioSpec` + macro (byte-match mutation `from`/`to` to the source line incl.
tabs vs spaces); (2) `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`;
(3) in master worktree `cmake --build --preset ci-test --target parity_dump_master`;
(4) from branch `scripts/parity/capture_master_golden.sh generator_saturation_scen99`;
(5) `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures
(wrap any branch/master-divergent predicate in `pred::branch_only(...)` /
`pred::master_only(...)` with an `intended_diff:`/`rng_drift:` label).

## Verification Phases
- **`check-generator-saturation`** — type `check`, `bounce_target: impl-generator-saturation`.
  Purpose: deterministically confirm the new scenario is present, green, mirrored,
  committed, and depth/consequence-compliant. Emit `VERDICT: PASS` iff every
  command yields its stated result, else `VERDICT: FAIL: <reason>`. Commands:
  - `cmake --build --preset ci-test && ctest --preset ci-test` → 0 failures.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.generator_saturation_scen99'` → pass.
  - `ls tests/parity/golden/generator_saturation_scen99.json` → exists.
  - `diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` → 0.
  - `./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:*mutation_canary*'` → pass.
  - **Mechanical depth/consequence count** for `generator_saturation_scen99`:
    ```bash
    block=$(awk '/inline constexpr FactPredicate kFacts_generator_saturation_scen99\[\]/{f=1} f{print} /^};/{if(f)exit}' tests/parity/scenario_table.h)
    total=$(printf '%s\n' "$block" | grep -c 'pred::')
    ticks=$(printf '%s\n' "$block" | grep -c 'pred::TickReached')
    test $((total - ticks)) -ge 3            # ≥3 non-TickReached predicates
    printf '%s\n' "$block" | grep -q 'consequence:' # ≥1 consequence predicate
    ```
    FAIL if either assertion fails.
  - **Tree-cleanliness:** `git status --porcelain tests/parity/` → empty.

## Success Criteria
- `generator_saturation_scen99` passes; golden exists; mirror byte-identical.
- All coverage/golden/mutation gates pass.
- ≥3 non-`TickReached` predicates incl. ≥1 with a `consequence:` label.
- `git status --porcelain tests/parity/` is empty.

## Git Commit Requirement
The implementer **must** commit work to git in **both** worktrees before yielding:
- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: mirror scenario_table.h generator"`
- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/scenario_facts_generated.json tests/parity/golden/generator_saturation_scen99.json && git commit -m "parity-cov: generator-saturation scenario"`

The branch commit **must include `tests/parity/scenario_facts_generated.json`**
(regenerated in-source by the build). After committing,
`git status --porcelain tests/parity/` must be empty. No `--no-verify`/`--amend`
unless flagged as recovery.
