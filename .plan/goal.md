We are RE-PLANNING a workflow that has gotten stuck.

The previously-running workflow has been bouncing on phase `impl-summon-lifecycle` (25 bounces) without ever passing its checker. This means the current plan is not working. This is replan cycle #1.

Your job is to diagnose WHY the loop is stuck and produce a complete REPLACEMENT workflow that will succeed. The replacement workflow REPLACES the current one — any phases from the original that should still run must appear in your output. Phase ids may differ from the original.

## Common causes of a stuck phase

1. The checker has an impossible contract (asks for two mutually exclusive things).
2. The phase prompt is missing a key constraint that the checker enforces, so the implementer keeps producing work the checker rejects.
3. The phase is too large — split it into smaller phases each with their own checker.
4. The wrong starting assumption — the upstream/setup phase produced state that prevents this phase from ever passing, and the right move is to redo earlier phases differently.
5. The checker is over-strict on something orthogonal to the goal and should be loosened.

## The current (stuck) workflow YAML

````yaml
name: parity-coverage-pass-2
backend: claude
working_dir: .
max_bounces: 999
vars:
  GOAL: Fill behavioral-coverage gaps in the OpenGlad parity test suite by adding
    22 new scenarios across 10 categories (walker status timers, summon lifecycle,
    generator saturation, weapon trajectories, effect emission and timers, input pipeline
    edge cases, multi-team coordination, level withdraw, mid-combat state). Mirror
    every change byte-for-byte to the master parity-companion worktree, capture master
    goldens, and refresh the final parity docs.
phases:
- id: impl-walker-status-timers
  type: implement
  prompt: "You are adding 4 walker status-timer scenarios to the OpenGlad parity\n\
    test suite: `enemy_freeze_mage_scen99`, `invisibility_thief_scen99`,\n`speed_potion_movement_scen99`,\
    \ `invulnerable_potion_scen99`.\n\n## Preexisting Inputs\n\nConsume these existing\
    \ artifacts as-is \u2014 do **not** refetch, recollect,\nrediscover, or regenerate\
    \ them from scratch:\n\n- `tests/parity/scenario_table.h` (reuses `kInputsSpecialSlot2`\
    \ at lines\n  1359-1364 and `kInputsSpecialSlot3` at lines 1366-1373; 134 existing\n\
    \  scenarios are untouched)\n- `tests/parity/scenario_runtime.cpp` (already wires\
    \ `special_names_table`\n  at lines 169-189 \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\
    \ (134 existing\n  `OG_PARITY_TEST(...)` macros)\n- `tests/parity/fact_predicate.h`\
    \ (predicate vocabulary already sufficient)\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (7 depth/quality gates \u2014\n  **not** modified)\n- `src/gameplay/living.cpp`\
    \ \u2014 `invisibility_left` read at line 81;\n  per-tick decrement at lines 156-157\n\
    - `src/gameplay/families/family_thief.cpp:92-94` (CLOAK writes\n  `invisibility_left`)\n\
    - `src/gameplay/families/family_mage.cpp:195-218` (FREEZE TIME writes\n  `world.enemy_freeze\
    \ += 20 + 11 * level()` at line 198;\n  `special_cost = 500` at line 283)\n- `src/gameplay/game_world.cpp:1381-1413`\
    \ (consumes `world.enemy_freeze`)\n- `src/gameplay/families/treasure_family_consumables.cpp:63-86`\n\
    \  (speed_potion + invulnerable_potion on_eat)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h` (already\n  baseline-resynced)\n\
    - `../openglad-master/tools/parity_scenario_runtime.cpp` (already carries\n  the\
    \ matching `special_names_table` wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\
    \ (rebuilt in this\n  phase)\n- `../openglad-master/` worktree on branch `parity-companion`\n\
    \n## New Outputs\n\n- 4 new scenarios appended to `kScenarios[]`:\n  - `enemy_freeze_mage_scen99`\
    \ (mage slot 3 FREEZE TIME; writes\n    `world.enemy_freeze`)\n  - `invisibility_thief_scen99`\
    \ (thief slot 2 CLOAK)\n  - `speed_potion_movement_scen99` (treasure pickup)\n\
    \  - `invulnerable_potion_scen99` (treasure pickup)\n- 4 `OG_PARITY_TEST(...)`\
    \ macros appended to\n  `tests/parity/test_parity_scenarios.cpp`.\n- 4 fully-populated\
    \ `kMut_*` mutations with literal `from`/`to`\n  byte-matching the cited source\
    \ lines.\n- 4 new golden JSON files in `tests/parity/golden/`.\n- Updated `../openglad-master/tools/parity_scenario_table.h`\
    \ (byte-mirror\n  of branch table).\n- Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\
    \n## File Changes\n\n- `tests/parity/scenario_table.h` \u2014 append 4 spawn lists,\
    \ declare 2 new\n  inline input lists for the potion pickups (reuse `kInputsSpecialSlot2/3`\n\
    \  for the specials), append 4 fact arrays, 4 mutations, 4 `ScenarioSpec`\n  rows.\n\
    - `tests/parity/test_parity_scenarios.cpp` \u2014 append 4\n  `OG_PARITY_TEST(...)`\
    \ macros.\n- 4 new goldens captured via `scripts/parity/capture_master_golden.sh`.\n\
    - `../openglad-master/tools/parity_scenario_table.h` \u2014 byte-mirrored via\n\
    \  `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.\n\
    \n## Scenario Specs\n\n**`enemy_freeze_mage_scen99`** (mage slot 3 FREEZE TIME;\
    \ writes\n`world.enemy_freeze`)\n- Spawns: `{FAMILY_MAGE,0,kOrderLiving,120,120,0,0,12,600}`\
    \ and\n  `{FAMILY_ARCHER,1,kOrderLiving,200,120,0,0,5,0}`. Level 12 because\n\
    \  freeze duration `20 + 11 * 12 = 152 ticks` strictly exceeds the\n  150-tick\
    \ budget. Magicpoints 600 covers slot 3's `special_cost = 500`.\n- Inputs: reuse\
    \ `kInputsSpecialSlot3`.\n- Tick budget 150.\n- Facts:\n  - `pred::TickReached(150)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1)`\n  - `pred::WalkerFamilyCount(FAMILY_ARCHER,\
    \ 1, 1)`\n  - `pred::WalkerPositionMoved(FAMILY_ARCHER, 200, 120, \"consequence:\
    \ archer pinned at spawn for the full 150-tick window because freeze duration\
    \ 20+11*12=152 > tick budget 150; both floors equal spawn coords\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 3)`\n- Mutation: file `src/gameplay/families/family_mage.cpp`, line 198.\n \
    \ - from: `                current_game->world->enemy_freeze += 20 + 11 * self->stats()->level();`\n\
    \  - to:   `                current_game->world->enemy_freeze += 0;`\n  - rationale:\
    \ zeroing `world.enemy_freeze` increment (preserving 16-space\n    indentation)\
    \ lets enemies act normally; archer steps west; xpos drops\n    below 200; predicate\
    \ fails on x floor.\n\n**`invisibility_thief_scen99`** (thief slot 2 CLOAK)\n\
    - Spawns: `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,4,300}`,\n  `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`.\n\
    - Inputs: reuse `kInputsSpecialSlot2`.\n- Tick budget 150.\n- Facts:\n  - `pred::TickReached(150)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF,\
    \ 9000, 13000, \"intended_diff: invisible thief survives soldier swings; HP varies\
    \ with engagement RNG\")`\n  - `pred::WalkerPositionMoved(FAMILY_SOLDIER, 199,\
    \ 119)`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 2)`\n- Mutation: file `src/gameplay/families/family_thief.cpp`,\
    \ line 93.\n  - from: `            self->set_invisibility_left(static_cast<short>(self->invisibility_left()\
    \ + 20 + static_cast<std::int32_t>(current_game->world->rng_.next(20)) * self->stats()->level()));`\n\
    \  - to:   `            self->set_invisibility_left(0);`\n  - rationale: forcing\
    \ `invisibility_left` to 0 lets the soldier engage;\n    HP drops below 9000;\
    \ predicate fails on the low floor.\n\n**`speed_potion_movement_scen99`** (treasure\
    \ pickup)\n- Spawns: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0}`,\n  `{FAMILY_SPEED_POTION,0,kOrderTreasure,128,120,0,0}`.\n\
    - Inputs (new inline list): `{1,0,K_RIGHT},{200,0,K_NONE}`.\n- Tick budget 250.\n\
    - Facts:\n  - `pred::TickReached(250)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n  - `pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_SPEED_POTION,\
    \ kOrderTreasure)`\n  - `pred::WalkerPositionMoved(FAMILY_SOLDIER, 280, 120, \"\
    intended_diff: speed bonus extends xpos travel beyond unbuffed baseline of 220\"\
    )`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 2)`\n- Mutation: file `src/gameplay/families/treasure_family_consumables.cpp`,\n\
    \  line 82.\n  - from: `    eater->set_speed_bonus_left(eater->speed_bonus_left()\
    \ + 50 * self->stats()->level());`\n  - to:   `    eater->set_speed_bonus_left(0);`\n\
    \  - rationale: clears speed bonus; soldier walks at base speed; xpos at\n   \
    \ tick 250 stays \u2264 220; predicate fails on x floor.\n\n**`invulnerable_potion_scen99`**\
    \ (treasure pickup)\n- Spawns: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0}`,\n\
    \  `{FAMILY_INVULNERABLE_POTION,0,kOrderTreasure,128,120,0,0}`,\n  `{FAMILY_ARCHER,1,kOrderLiving,260,120,0,0}`.\n\
    - Inputs (new inline list): `{1,0,K_RIGHT},{200,0,K_NONE}`.\n- Tick budget 250.\n\
    - Facts:\n  - `pred::TickReached(250)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n  - `pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_INVULNERABLE_POTION,\
    \ kOrderTreasure)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 13000,\
    \ 30000, \"intended_diff: invulnerable_left blocks archer damage until timer expires;\
    \ HP held at or near max\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 3)`\n\
    - Mutation: file `src/gameplay/families/treasure_family_consumables.cpp`,\n  line\
    \ 67.\n  - from: `        eater->set_invulnerable_left(static_cast<short>(eater->invulnerable_left()\
    \ + (150 * self->stats()->level())));`\n  - to:   `        eater->set_invulnerable_left(0);`\n\
    \  - rationale: clears invulnerable timer; archer arrows now deal damage;\n  \
    \  HP drops below 13000; predicate fails on lower bound.\n\n## Mirror + Capture\n\
    \n1. `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`\n\
    2. In the master worktree:\n   `cmake --build --preset ci-test --target parity_dump_master`.\n\
    3. From the branch worktree:\n   `scripts/parity/capture_master_golden.sh enemy_freeze_mage_scen99\
    \ invisibility_thief_scen99 speed_potion_movement_scen99 invulnerable_potion_scen99`.\n\
    4. Run `cmake --build --preset ci-test && ctest --preset ci-test`. Wrap\n   any\
    \ predicate that fails master evaluation in `pred::branch_only(...)`\n   with\
    \ an `intended_diff:` or `rng_drift:` label; conversely wrap any\n   predicate\
    \ that fails branch but holds against the captured master in\n   `pred::master_only(...)`.\n\
    \n## Success Criteria\n\n- All 4 new scenarios appear in `kScenarios[]` and each\
    \ has a paired\n  `OG_PARITY_TEST(...)` macro.\n- All 4 new goldens exist on disk\
    \ and are committed to the branch.\n- `tests/parity/scenario_table.h` is byte-identical\
    \ to\n  `../openglad-master/tools/parity_scenario_table.h`.\n- `cmake --build\
    \ --preset ci-test && ctest --preset ci-test` reports 0\n  failures.\n- Every\
    \ new mutation carries literal `file`, `line`, `from`, `to`,\n  `rationale` fields\
    \ matching the cited source byte-for-byte (including\n  tabs vs spaces).\n- Every\
    \ widened range carries an `intended_diff:`, `rng_drift:`, or\n  `consequence:`\
    \ label.\n\n## Git Commit Requirement\n\nYou **must** commit work to git in **both**\
    \ worktrees before yielding:\n\n- Master worktree (after `cp`-mirroring the branch\
    \ table):\n  `git -C ../openglad-master add tools/parity_scenario_table.h && git\
    \ -C ../openglad-master commit -m \"parity-companion: mirror scenario_table.h\
    \ walker-status-timers\"`\n- Branch worktree:\n  `git add tests/parity/scenario_table.h\
    \ tests/parity/test_parity_scenarios.cpp tests/parity/golden/enemy_freeze_mage_scen99.json\
    \ tests/parity/golden/invisibility_thief_scen99.json tests/parity/golden/speed_potion_movement_scen99.json\
    \ tests/parity/golden/invulnerable_potion_scen99.json && git commit -m \"parity-cov:\
    \ walker status timer scenarios\"`\n\nNo `--no-verify` or `--amend` unless flagged\
    \ in the commit message as\nrecovery from a pre-existing failure. Do not yield\
    \ until both commits\nland.\n"
- id: check-walker-status-timers
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the 4 new walker\nstatus-timer\
    \ scenarios compile, pass their depth/quality gates, exist\nwith goldens, and\
    \ the master mirror is in sync.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n\
    - `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/golden/enemy_freeze_mage_scen99.json`\n\
    - `tests/parity/golden/invisibility_thief_scen99.json`\n- `tests/parity/golden/speed_potion_movement_scen99.json`\n\
    - `tests/parity/golden/invulnerable_potion_scen99.json`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    \n## What to Verify\n\nRun each of these commands and confirm the expected result:\n\
    \n```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\ngrep -c '\"enemy_freeze_mage_scen99\"\\|\"invisibility_thief_scen99\"\
    \\|\"speed_potion_movement_scen99\"\\|\"invulnerable_potion_scen99\"' tests/parity/scenario_table.h\n\
    ```\nExpected \u2265 4.\n\n```bash\ngrep -c 'OG_PARITY_TEST(enemy_freeze_mage_scen99)\\\
    |OG_PARITY_TEST(invisibility_thief_scen99)\\|OG_PARITY_TEST(speed_potion_movement_scen99)\\\
    |OG_PARITY_TEST(invulnerable_potion_scen99)' tests/parity/test_parity_scenarios.cpp\n\
    ```\nExpected = 4.\n\n```bash\nls tests/parity/golden/enemy_freeze_mage_scen99.json\
    \ tests/parity/golden/invisibility_thief_scen99.json tests/parity/golden/speed_potion_movement_scen99.json\
    \ tests/parity/golden/invulnerable_potion_scen99.json\n```\nAll 4 must exist.\n\
    \n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.enemy_freeze_mage_scen99:Parity.invisibility_thief_scen99:Parity.speed_potion_movement_scen99:Parity.invulnerable_potion_scen99'\n\
    ```\nMust pass.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nEach new scenario must carry \u2265 3 non-`TickReached` predicates\
    \ including\n\u2265 1 consequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-walker-status-timers
- id: impl-walker-status-timers~check-2
  type: check
  role: tester
  bounce_target: impl-walker-status-timers
- id: impl-walker-status-timers~check-3
  type: check
  role: senior-tester
  bounce_target: impl-walker-status-timers
- id: impl-walker-status-timers~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-walker-status-timers
- id: impl-walker-status-timers~check-5
  type: check
  role: architect
  bounce_target: impl-walker-status-timers
- id: impl-walker-status-timers~check-6
  type: check
  role: pm
  bounce_target: impl-walker-status-timers
- id: impl-summon-lifecycle
  type: implement
  prompt: "You are adding 2 summon-lifecycle scenarios to the OpenGlad parity test\n\
    suite: `summon_lifetime_faerie_scen99` and\n`summon_lifetime_decrement_faerie_scen99`.\
    \ Both exercise Druid slot 2\nSUMMON FAERIE.\n\n## Preexisting Inputs\n\nConsume\
    \ these existing artifacts as-is \u2014 do **not** refetch, recollect,\nrediscover,\
    \ or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h` (reuses\
    \ `kInputsSpecialSlot2` at lines\n  1359-1364; 134 existing scenarios + the 4\
    \ from the walker-status-timer\n  phase are untouched)\n- `tests/parity/scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/fact_predicate.h`\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (depth/quality gates;\n  **not** modified)\n- `src/gameplay/living.cpp:87-111`\
    \ (owner-death cascade at lines 87 and\n  98; per-tick lifetime decrement at lines\
    \ 104-109)\n- `src/gameplay/families/family_druid.cpp:62-79` (SUMMON FAERIE slot\
    \ 2;\n  lifetime = `50 + level*40` at line 71; `special_cost = 80` at line 167)\n\
    - `src/gameplay/game_world.cpp:1078-1103` (range-gated AI targeting \u2014\n \
    \ off-map enemy ensures faerie is never reached)\n- `src/gameplay/game_world.cpp:1546-1561`\
    \ (remove-dead pass that reaps\n  expired faerie)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already carries\n  matching wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 2 new scenarios appended to `kScenarios[]`:\n  - `summon_lifetime_faerie_scen99`\
    \ (spawn-time initialisation path)\n  - `summon_lifetime_decrement_faerie_scen99`\
    \ (per-tick decrement path)\n- 2 `OG_PARITY_TEST(...)` macros appended.\n- 2 fully-populated\
    \ `kMut_*` mutations with literal `from`/`to`.\n- 2 new golden JSON files.\n-\
    \ Updated `../openglad-master/tools/parity_scenario_table.h` (byte-mirror).\n\
    - Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario\
    \ Specs\n\nDruid slot 2 SUMMON FAERIE deterministically creates a `FAMILY_FAERIE`\n\
    walker with `lifetime = 50 + level * 40` (`family_druid.cpp:71`).\n\n**`summon_lifetime_faerie_scen99`**\
    \ (spawn-time initialisation path)\n- Spawns: `{FAMILY_DRUID,0,kOrderLiving,120,120,0,0,4,300}`,\n\
    \  `{FAMILY_SOLDIER,1,kOrderLiving,2000,2000,0,0}` (off-map far enough\n  that\
    \ the AI cannot reach the faerie before the 210-tick lifetime\n  expires).\n-\
    \ Inputs: reuse `kInputsSpecialSlot2`.\n- Tick budget 650 (\u2248 3\xD7 the faerie's\
    \ natural lifetime).\n- Facts:\n  - `pred::TickReached(650)`\n  - `pred::WalkerFamilyCount(FAMILY_DRUID,\
    \ 1, 1)`\n  - `pred::WalkerFamilyCount(FAMILY_FAERIE, 0, 0, \"consequence: faerie\
    \ summoned around tick 30 then expired by tick 650 as lifetime ran out\")`\n \
    \ - `pred::EventKindAtLeast(/*play_sound*/1, 3)`\n  - `pred::WalkerOfTeamAlive(0,\
    \ 1, 2)`\n- Mutation: file `src/gameplay/families/family_druid.cpp`, line 71.\n\
    \  - from: `            alive->set_lifetime(50 + self->stats()->level() * 40);`\n\
    \  - to:   `            alive->set_lifetime(99999);`\n  - rationale: with an effectively-infinite\
    \ lifetime the faerie is still\n    alive at tick 650; predicate `WalkerFamilyCount(FAMILY_FAERIE,\
    \ 0, 0)`\n    fails because count is 1.\n\n**`summon_lifetime_decrement_faerie_scen99`**\
    \ (per-tick decrement path)\n- Spawns: `{FAMILY_DRUID,0,kOrderLiving,120,120,0,0,4,300}`\
    \ (magicpoints\n  300 covers SUMMON FAERIE's `special_cost = 80`),\n  `{FAMILY_SOLDIER,1,kOrderLiving,2000,2000,0,0}`\
    \ (off-map so the druid\n  is never engaged; owner-death cascades never fire,\
    \ isolating the\n  per-tick decrement).\n- Inputs: reuse `kInputsSpecialSlot2`.\n\
    - Tick budget 650.\n- Facts:\n  - `pred::TickReached(650)`\n  - `pred::WalkerFamilyCount(FAMILY_DRUID,\
    \ 1, 1)`\n  - `pred::WalkerFamilyCount(FAMILY_FAERIE, 0, 0, \"consequence: faerie's\
    \ 210-tick lifetime is decremented once per tick at living.cpp:104-105 until it\
    \ reaches 0 around tick ~240; lines 106-109 then fire set_dead+death+return\"\
    )`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 3)`\n  - `pred::WalkerOfTeamAlive(0,\
    \ 1, 2)`\n- Mutation: file `src/gameplay/living.cpp`, line 104. `living.cpp` uses\n\
    \  tabs \u2014 preserve the leading tab.\n  - from: `\t\tconst auto remaining_lifetime\
    \ = lifetime() - 1;`\n  - to:   `\t\tconst auto remaining_lifetime = lifetime();`\n\
    \  - rationale: removing `- 1` makes `remaining_lifetime == lifetime()`\n    every\
    \ tick; `if (remaining_lifetime < 1)` at line 106 is permanently\n    false; lifetime-expiry\
    \ kill at 108-109 never fires; with druid kept\n    alive, owner-death cascades\
    \ at 87/98 also never fire. Live: faerie\n    expires by ~tick 240. Mutated: faerie\
    \ still alive at tick 650, count\n    is 1 \u2014 predicate fails. Exercises the\
    \ *decrement* path rather than\n    *initialisation*.\n\n## Mirror + Capture\n\
    \n1. `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`\n\
    2. In the master worktree:\n   `cmake --build --preset ci-test --target parity_dump_master`.\n\
    3. From the branch worktree:\n   `scripts/parity/capture_master_golden.sh summon_lifetime_faerie_scen99\
    \ summon_lifetime_decrement_faerie_scen99`.\n4. Run `cmake --build --preset ci-test\
    \ && ctest --preset ci-test`. Wrap\n   diverging predicates with `pred::branch_only(...)`\
    \ /\n   `pred::master_only(...)` and an `intended_diff:` or `rng_drift:`\n   label.\n\
    \n## Success Criteria\n\n- Both new scenarios are present in `kScenarios[]` with\
    \ matching\n  `OG_PARITY_TEST(...)` macros.\n- Both new goldens exist and are\
    \ committed.\n- `tests/parity/scenario_table.h` is byte-identical to\n  `../openglad-master/tools/parity_scenario_table.h`.\n\
    - `cmake --build --preset ci-test && ctest --preset ci-test` reports 0\n  failures.\n\
    - Each mutation's `from`/`to` matches the cited source line byte-for-byte\n  (`living.cpp`\
    \ uses tabs).\n\n## Git Commit Requirement\n\nYou **must** commit in **both**\
    \ worktrees before yielding:\n\n- `git -C ../openglad-master add tools/parity_scenario_table.h\
    \ && git -C ../openglad-master commit -m \"parity-companion: mirror scenario_table.h\
    \ summon-lifecycle\"`\n- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp\
    \ tests/parity/golden/summon_lifetime_faerie_scen99.json tests/parity/golden/summon_lifetime_decrement_faerie_scen99.json\
    \ && git commit -m \"parity-cov: summon lifecycle scenarios\"`\n\nNo `--no-verify`\
    \ or `--amend` unless flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-summon-lifecycle
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm both summon-lifecycle\n\
    scenarios compile, pass gates, exist with goldens, and the mirror is in\nsync.\n\
    \n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/golden/summon_lifetime_faerie_scen99.json`\n- `tests/parity/golden/summon_lifetime_decrement_faerie_scen99.json`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n\n## What to Verify\n\n\
    ```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.summon_lifetime_faerie_scen99:Parity.summon_lifetime_decrement_faerie_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/summon_lifetime_faerie_scen99.json\
    \ tests/parity/golden/summon_lifetime_decrement_faerie_scen99.json\n```\nMust\
    \ succeed.\n\n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nBoth scenarios must have \u2265 3 non-`TickReached` predicates\
    \ including \u2265 1\nconsequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-summon-lifecycle
- id: impl-summon-lifecycle~check-2
  type: check
  role: tester
  bounce_target: impl-summon-lifecycle
- id: impl-summon-lifecycle~check-3
  type: check
  role: senior-tester
  bounce_target: impl-summon-lifecycle
- id: impl-summon-lifecycle~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-summon-lifecycle
- id: impl-summon-lifecycle~check-5
  type: check
  role: architect
  bounce_target: impl-summon-lifecycle
- id: impl-summon-lifecycle~check-6
  type: check
  role: pm
  bounce_target: impl-summon-lifecycle
- id: impl-generator-saturation
  type: implement
  prompt: "You are adding 1 generator-saturation scenario\n(`generator_saturation_scen99`)\
    \ to the OpenGlad parity test suite. It\nexercises a `FAMILY_TOWER` generator\
    \ saturating to MAXOBS over 2500\nticks.\n\n## Preexisting Inputs\n\nConsume these\
    \ existing artifacts as-is \u2014 do **not** refetch, recollect,\nrediscover,\
    \ or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h` (134 existing\
    \ + earlier-phase additions\n  are untouched)\n- `tests/parity/scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/fact_predicate.h`\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (depth/quality gates;\n  **not** modified)\n- `src/gameplay/walker.cpp:1217-1235`\
    \ (`act_generate` \u2014 gates on\n  `living_count < MAXOBS`)\n- `src/gameplay/generator_family_registry.cpp:30-37`\
    \ (`FAMILY_TOWER` \u2192\n  `default_weapon = FAMILY_MAGE`)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already carries\n  matching wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 1 new scenario appended to `kScenarios[]`:\n  `generator_saturation_scen99`.\n\
    - 1 `OG_PARITY_TEST(generator_saturation_scen99)` macro appended.\n- 1 fully-populated\
    \ `kMut_generator_saturation_scen99` with literal\n  `from`/`to` (`walker.cpp`\
    \ uses tabs).\n- 1 new golden JSON file.\n- Updated `../openglad-master/tools/parity_scenario_table.h`\
    \ (byte-mirror).\n- Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\
    \n## Scenario Spec\n\n**`generator_saturation_scen99`**\n- Spawns: `{FAMILY_TOWER,1,kOrderGenerator,60,60,0,0,5,0}`,\n\
    \  `{FAMILY_SOLDIER,0,kOrderLiving,240,240,0,0}` (observer, far enough not\n \
    \ to interfere).\n- Inputs: none (idle scenario).\n- Tick budget 2500.\n- Facts:\n\
    \  - `pred::TickReached(2500)`\n  - `pred::WalkerFamilyCount(FAMILY_TOWER, 1,\
    \ 1)`\n  - `pred::WalkerFamilyCount(FAMILY_MAGE, 3, 30, \"consequence: generator\
    \ saturates living_count over 2500 ticks; range spans RNG drift\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 4)`\n  - `pred::WalkerOfTeamAlive(1, 3, 30, \"rng_drift: spawn count varies\
    \ with per-tick RNG\")`\n- Mutation: file `src/gameplay/walker.cpp`, line 1219.\
    \ `walker.cpp` uses\n  tab indentation \u2014 preserve the leading tab.\n  - from:\
    \ `\tif ( current_game->world->living_count < MAXOBS &&`\n  - to:   `\tif ( false\
    \ &&`\n  - rationale: replacing the `living_count < MAXOBS` half of the\n    `act_generate`\
    \ gate with `false` makes the conjunction always false;\n    the generator never\
    \ fires; zero FAMILY_MAGE spawn;\n    `WalkerFamilyCount(FAMILY_MAGE, 3, 30)`\
    \ fails on lower bound.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`\n2. In the master worktree:\n\
    \   `cmake --build --preset ci-test --target parity_dump_master`.\n3. From the\
    \ branch worktree:\n   `scripts/parity/capture_master_golden.sh generator_saturation_scen99`.\n\
    4. Run `cmake --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging\
    \ predicates with `pred::branch_only(...)` /\n   `pred::master_only(...)` and\
    \ an `intended_diff:` or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- `generator_saturation_scen99`\
    \ appears in `kScenarios[]` with matching\n  `OG_PARITY_TEST(...)` macro.\n- Golden\
    \ file exists and is committed.\n- `tests/parity/scenario_table.h` is byte-identical\
    \ to the master mirror.\n- `cmake --build --preset ci-test && ctest --preset ci-test`\
    \ reports 0\n  failures.\n- Mutation `from`/`to` preserves tab indentation matching\n\
    \  `src/gameplay/walker.cpp:1219`.\n\n## Git Commit Requirement\n\nYou **must**\
    \ commit in **both** worktrees before yielding:\n\n- `git -C ../openglad-master\
    \ add tools/parity_scenario_table.h && git -C ../openglad-master commit -m \"\
    parity-companion: mirror scenario_table.h generator-saturation\"`\n- `git add\
    \ tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/golden/generator_saturation_scen99.json\
    \ && git commit -m \"parity-cov: generator saturation scenario\"`\n\nNo `--no-verify`\
    \ or `--amend` unless flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-generator-saturation
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm\n`generator_saturation_scen99`\
    \ compiles, passes gates, exists with its\ngolden, and the mirror is in sync.\n\
    \n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/golden/generator_saturation_scen99.json`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    \n## What to Verify\n\n```bash\ncmake --build --preset ci-test && ctest --preset\
    \ ci-test\n```\n0 failures expected.\n\n```bash\n./build/ci-test/og_test_parity\
    \ --gtest_filter='Parity.generator_saturation_scen99'\n```\nMust pass.\n\n```bash\n\
    ls tests/parity/golden/generator_saturation_scen99.json\n```\nMust succeed.\n\n\
    ```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nThe new scenario must have \u2265 3 non-`TickReached` predicates\
    \ including\n\u2265 1 consequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-generator-saturation
- id: impl-generator-saturation~check-2
  type: check
  role: tester
  bounce_target: impl-generator-saturation
- id: impl-generator-saturation~check-3
  type: check
  role: senior-tester
  bounce_target: impl-generator-saturation
- id: impl-generator-saturation~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-generator-saturation
- id: impl-generator-saturation~check-5
  type: check
  role: architect
  bounce_target: impl-generator-saturation
- id: impl-generator-saturation~check-6
  type: check
  role: pm
  bounce_target: impl-generator-saturation
- id: impl-weapon-trajectories
  type: implement
  prompt: "You are adding 3 weapon-trajectory scenarios to the OpenGlad parity test\n\
    suite: `weapon_rock_slot2_emit_scen99`, `weapon_boomerang_return_scen99`,\n`weapon_exploding_boulder_scen99`.\n\
    \n## Preexisting Inputs\n\nConsume these existing artifacts as-is \u2014 do **not**\
    \ refetch, recollect,\nrediscover, or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h`\
    \ (reuses `kInputsSpecialSlot2`; 134\n  existing + earlier-phase additions untouched)\n\
    - `tests/parity/scenario_runtime.cpp` (already wires\n  `special_names_table`\
    \ \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/fact_predicate.h`\n\
    - `tests/parity/test_parity_coverage_gate.cpp` (widened-range gate at\n  893-933;\
    \ `label_exempted` prefixes at 702-707)\n- `src/gameplay/families/family_elf.cpp:62-74`\
    \ (slot 2 BOUNCING ROCKS \u2014\n  two-fire loop)\n- `src/gameplay/families/family_soldier.cpp:45-52`\
    \ (slot 2 BOOMERANG)\n- `src/gameplay/families/effect_family_shield.cpp:63-128`\
    \ (boomerang FX\n  path; 133-134 registers FAMILY_BOOMERANG)\n- `src/gameplay/families/family_barbarian.cpp:23-65`\
    \ (slot 2 EXPLODING\n  BOULDER; line 59 `set_skip_exit(5000)`)\n- `src/gameplay/families/weapon_family_projectiles.cpp:14-31`\n\
    \  (`projectile_explode_on_death`)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already carries\n  matching wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 3 new scenarios appended to `kScenarios[]`:\n  - `weapon_rock_slot2_emit_scen99`\
    \ (elf slot 2 BOUNCING ROCKS)\n  - `weapon_boomerang_return_scen99` (soldier slot\
    \ 2 BOOMERANG)\n  - `weapon_exploding_boulder_scen99` (barbarian slot 2 EXPLODING\n\
    \    BOULDER)\n- 3 `OG_PARITY_TEST(...)` macros appended.\n- 3 fully-populated\
    \ `kMut_*` mutations.\n- 3 new golden JSON files.\n- Updated `../openglad-master/tools/parity_scenario_table.h`.\n\
    - Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario\
    \ Specs\n\n**`weapon_rock_slot2_emit_scen99`** (elf slot 2 BOUNCING ROCKS)\n-\
    \ Spawns: `{FAMILY_ELF,0,kOrderLiving,120,120,0,0,4,300}`,\n  `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`.\n\
    - Inputs: reuse `kInputsSpecialSlot2` (no K_FIRE held).\n- Tick budget 30 (rocks\
    \ still alive in `world.weaplist` at tick 30).\n- Facts:\n  - `pred::TickReached(30)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_ELF, 1, 1)`\n  - `pred::WeaponFamilyEmitted(FAMILY_ROCK)`\n\
    \  - `pred::WalkerOfTeamAlive(0, 1, 1)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n- Mutation: file `src/gameplay/families/family_elf.cpp`, line 66.\n\
    \  - from: `                fireob = static_cast<weap*>(self->fire());`\n  - to:\
    \   `                return false;`\n  - rationale: replacing the first `self->fire()`\
    \ inside slot 2's `for`\n    loop with `return false;` (preserving 16-space indentation)\
    \ aborts\n    before any FAMILY_ROCK is added; live: 2 rocks emitted around tick\n\
    \    20-21; mutated: no rocks ever spawn \u2014 predicate fails. `play_sound`\n\
    \    floor is deliberately omitted because the enemy soldier's AI emits a\n  \
    \  variable number of sounds.\n\n**`weapon_boomerang_return_scen99`** (soldier\
    \ slot 2 BOOMERANG)\n- Spawns: `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,4,300}`,\n\
    \  `{FAMILY_ARCHER,1,kOrderLiving,200,200,0,0}`.\n- Inputs: reuse `kInputsSpecialSlot2`.\n\
    - Tick budget 80. Boomerang spawns at ~tick 20-21 with\n  `lifetime = 30 + 12\
    \ * level() = 78`, alive in `dump.effects[]` from\n  ~tick 21 to ~tick 99.\n-\
    \ Facts:\n  - `pred::TickReached(80)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n  - `pred::EffectFamilyCount(FAMILY_BOOMERANG, 1, 2, -1, /*window_marker*/0,\
    \ \"consequence: BOOMERANG slot 2 spawns one FAMILY_BOOMERANG FX walker; default\
    \ window_marker=0 counts every alive FAMILY_BOOMERANG effect in dump.effects[]\"\
    )`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 2)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER,\
    \ 8000, 14000, \"rng_drift: soldier HP at tick 80 varies with archer engagement\
    \ RNG (archer at (200,200) may land 0-2 arrows by tick 80); widened range (6000\
    \ cents) carries a gate-recognised label per predicate_depth_gate_no_trivially_wide_ranges\
    \ (test_parity_coverage_gate.cpp:907-910 + label_exempted at :702-707)\")`\n-\
    \ Mutation: file `src/gameplay/families/family_soldier.cpp`, line 46.\n  - from:\
    \ `            newob = summon_entity(self, Order::FX, FAMILY_BOOMERANG);`\n  -\
    \ to:   `            return false;`\n  - rationale: prevents FAMILY_BOOMERANG\
    \ from ever being added to the FX\n    list; in-window count drops to 0; predicate\
    \ fails on lower bound.\n\n**`weapon_exploding_boulder_scen99`** (barbarian slot\
    \ 2 EXPLODING BOULDER)\n- Spawns: `{FAMILY_BARBARIAN,0,kOrderLiving,120,120,0,0,5,300}`,\n\
    \  `{FAMILY_SOLDIER,1,kOrderLiving,160,120,0,0}`,\n  `{FAMILY_SOLDIER,1,kOrderLiving,200,160,0,0}`,\n\
    \  `{FAMILY_SOLDIER,1,kOrderLiving,260,200,0,0}`.\n- Inputs: reuse `kInputsSpecialSlot2`.\n\
    - Tick budget 60. Boulder spawns at ~tick 20-21, travels ~10-20 ticks,\n  impacts,\
    \ and `projectile_explode_on_death` inserts a FAMILY_EXPLOSION.\n- Facts:\n  -\
    \ `pred::TickReached(60)`\n  - `pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1)`\n\
    \  - `pred::WeaponFamilyEmitted(FAMILY_BOULDER)`\n  - `pred::EffectFamilyCount(FAMILY_EXPLOSION,\
    \ 1, 4, -1, /*window_marker*/0, \"consequence: EXPLODING BOULDER triggers projectile_explode_on_death\
    \ adding \u2265 1 FAMILY_EXPLOSION FX walker; default window_marker=0 counts every\
    \ alive FAMILY_EXPLOSION; upper bound covers chained explosions\")`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 0, 3, \"rng_drift: explosion may kill 0-3 enemy soldiers depending on radius/aim\"\
    )`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 3)`\n- Mutation: file `src/gameplay/families/family_barbarian.cpp`,\
    \ line 59.\n  - from: `        alive->set_skip_exit(5000);`\n  - to:   `     \
    \   alive->set_skip_exit(0);`\n  - rationale: setting `skip_exit` to 0 (preserving\
    \ 8-space indentation)\n    makes `projectile_explode_on_death` skip the FAMILY_EXPLOSION\
    \ spawn;\n    in-window FAMILY_EXPLOSION count drops to 0; predicate fails on\n\
    \    lower bound.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`\n2. In the master worktree:\n\
    \   `cmake --build --preset ci-test --target parity_dump_master`.\n3. From the\
    \ branch worktree:\n   `scripts/parity/capture_master_golden.sh weapon_rock_slot2_emit_scen99\
    \ weapon_boomerang_return_scen99 weapon_exploding_boulder_scen99`.\n4. Run `cmake\
    \ --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging predicates\
    \ with `pred::branch_only(...)` /\n   `pred::master_only(...)` and an `intended_diff:`\
    \ or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- All 3 new scenarios appear\
    \ in `kScenarios[]` with paired\n  `OG_PARITY_TEST(...)` macros.\n- All 3 new\
    \ goldens exist on disk.\n- `tests/parity/scenario_table.h` is byte-identical\
    \ to the master mirror.\n- `cmake --build --preset ci-test && ctest --preset ci-test`\
    \ reports 0\n  failures.\n- Every widened range (boomerang HP 6000-cent span)\
    \ carries an\n  `intended_diff:`, `rng_drift:`, or `consequence:` label-exempted\n\
    \  prefix.\n\n## Git Commit Requirement\n\nYou **must** commit in **both** worktrees\
    \ before yielding:\n\n- `git -C ../openglad-master add tools/parity_scenario_table.h\
    \ && git -C ../openglad-master commit -m \"parity-companion: mirror scenario_table.h\
    \ weapon-trajectories\"`\n- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp\
    \ tests/parity/golden/weapon_rock_slot2_emit_scen99.json tests/parity/golden/weapon_boomerang_return_scen99.json\
    \ tests/parity/golden/weapon_exploding_boulder_scen99.json && git commit -m \"\
    parity-cov: weapon trajectory scenarios\"`\n\nNo `--no-verify` or `--amend` unless\
    \ flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-weapon-trajectories
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the 3 weapon-trajectory\n\
    scenarios pass, including the widened-range gate exemption for the\nboomerang\
    \ HP range.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/golden/weapon_rock_slot2_emit_scen99.json`\n- `tests/parity/golden/weapon_boomerang_return_scen99.json`\n\
    - `tests/parity/golden/weapon_exploding_boulder_scen99.json`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    \n## What to Verify\n\n```bash\ncmake --build --preset ci-test && ctest --preset\
    \ ci-test\n```\n0 failures expected.\n\n```bash\n./build/ci-test/og_test_parity\
    \ --gtest_filter='Parity.weapon_rock_slot2_emit_scen99:Parity.weapon_boomerang_return_scen99:Parity.weapon_exploding_boulder_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/weapon_rock_slot2_emit_scen99.json\
    \ tests/parity/golden/weapon_boomerang_return_scen99.json tests/parity/golden/weapon_exploding_boulder_scen99.json\n\
    ```\nMust succeed.\n\n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.predicate_depth_gate_no_trivially_wide_ranges'\n\
    ```\nMust pass (boomerang's 6000-cent HP span must carry `rng_drift:` label).\n\
    \nEach scenario must have \u2265 3 non-`TickReached` predicates including \u2265\
    \ 1\nconsequence.\n\n## Verdict\n\nIf every check above succeeds, emit:\n`VERDICT:\
    \ PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-weapon-trajectories
- id: impl-weapon-trajectories~check-2
  type: check
  role: tester
  bounce_target: impl-weapon-trajectories
- id: impl-weapon-trajectories~check-3
  type: check
  role: senior-tester
  bounce_target: impl-weapon-trajectories
- id: impl-weapon-trajectories~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-weapon-trajectories
- id: impl-weapon-trajectories~check-5
  type: check
  role: architect
  bounce_target: impl-weapon-trajectories
- id: impl-weapon-trajectories~check-6
  type: check
  role: pm
  bounce_target: impl-weapon-trajectories
- id: impl-effect-emission
  type: implement
  prompt: "You are adding 3 multi-target / multi-spawn effect-emission scenarios:\n\
    `effect_heartburst_multitarget_scen99`, `effect_poison_cloud_emit_scen99`,\n`effect_protection_emit_scen99`.\n\
    \n## Preexisting Inputs\n\nConsume these existing artifacts as-is \u2014 do **not**\
    \ refetch, recollect,\nrediscover, or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h`\
    \ (reuses `kInputsSpecialSlot2`,\n  `kInputsSpecialSlot4`; 134 existing + earlier-phase\
    \ additions\n  untouched)\n- `tests/parity/scenario_runtime.cpp` (already wires\n\
    \  `special_names_table` \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/fact_predicate.h`\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (depth/quality gates;\n  **not** modified)\n- `src/gameplay/families/family_archmage.cpp:209-251`\
    \ (HEARTBURST\n  unshifted; per-foe FAMILY_EXPLOSION loop at 237-250)\n- `src/gameplay/families/family_thief.cpp:165-178`\
    \ (POISON CLOUD slot 4;\n  line 169 spawns FAMILY_CLOUD)\n- `src/gameplay/families/family_druid.cpp:86-149`\
    \ (PROTECTION; gated on\n  `howmany > 1` friendlies in range 60; line 116 spawns\n\
    \  FAMILY_CIRCLE_PROTECTION)\n- `src/gameplay/families/weapon_family_animate.cpp:32-43,126`\n\
    \  (CIRCLE_PROTECTION is BIT_IMMORTAL|BIT_NO_COLLIDE|BIT_PHANTOM \u2014 no\n \
    \ damage-absorption path; scenario is scoped to **emission**)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already carries\n  matching wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 3 new scenarios appended to `kScenarios[]`:\n  - `effect_heartburst_multitarget_scen99`\
    \ (archmage slot 2 unshifted\n    HEARTBURST)\n  - `effect_poison_cloud_emit_scen99`\
    \ (thief slot 4 POISON CLOUD)\n  - `effect_protection_emit_scen99` (druid slot\
    \ 4 PROTECTION)\n- 3 `OG_PARITY_TEST(...)` macros appended.\n- 3 fully-populated\
    \ `kMut_*` mutations.\n- 3 new golden JSON files.\n- Updated `../openglad-master/tools/parity_scenario_table.h`.\n\
    - Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario\
    \ Specs\n\n**`effect_heartburst_multitarget_scen99`** (archmage slot 2 HEARTBURST,\n\
    unshifted)\n- Spawns: `{FAMILY_ARCHMAGE,0,kOrderLiving,120,120,0,0,4,300}`, 4\
    \ \xD7\n  `{FAMILY_SOLDIER,1,kOrderLiving,X,120,0,0}` with X \u2208 {160,190,220,250}.\n\
    - Inputs: reuse `kInputsSpecialSlot2`.\n- Tick budget 30.\n- Facts:\n  - `pred::TickReached(30)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1)`\n  - `pred::EffectFamilyCount(FAMILY_EXPLOSION,\
    \ 4, 12, -1, /*window_marker*/0, \"consequence: unshifted HEARTBURST loop at family_archmage.cpp:237-250\
    \ spawns one FAMILY_EXPLOSION per foe in range; 4 enemies \u2192 \u2265 4 in-window\
    \ emissions; default window_marker=0 counts every alive FAMILY_EXPLOSION\")`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_SOLDIER, 0, 4, \"consequence: HEARTBURST\
    \ damage may kill 0-4 soldiers depending on damage roll\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 4)`\n- Mutation: file `src/gameplay/families/family_archmage.cpp`, line 239.\n\
    \  - from: `                        newob = summon_entity(self, Order::FX, FAMILY_EXPLOSION);`\n\
    \  - to:   `                        return false;`\n  - rationale: returning false\
    \ on the first loop iteration aborts the\n    per-foe spawn loop; in-window count\
    \ drops to 0; predicate fails on\n    lower bound.\n\n**`effect_poison_cloud_emit_scen99`**\
    \ (thief slot 4)\n- Spawns: `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,10,300}`,\n\
    \  `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`.\n- Inputs: reuse `kInputsSpecialSlot4`.\n\
    - Tick budget 80 (cloud lifetime `40 + 3 * 10 = 70`).\n- Facts:\n  - `pred::TickReached(80)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1)`\n  - `pred::EffectFamilyCount(FAMILY_CLOUD,\
    \ 1, 5, -1, /*window_marker*/0, \"consequence: POISON CLOUD slot 4 emits one FAMILY_CLOUD\
    \ effect walker (lifetime 40+3*10=70 at level 10); default window_marker=0 counts\
    \ every alive FAMILY_CLOUD\")`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER,\
    \ 0, 15000, \"rng_drift: cloud may or may not hit soldier depending on drift\"\
    )`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 3)`\n- Mutation: file `src/gameplay/families/family_thief.cpp`,\
    \ line 169.\n  - from: `            newob = summon_entity(self, Order::FX, FAMILY_CLOUD);`\n\
    \  - to:   `            return false;`\n  - rationale: prevents FAMILY_CLOUD spawn;\
    \ in-window count drops to 0;\n    predicate fails on lower bound.\n\n**`effect_protection_emit_scen99`**\
    \ (druid slot 4 PROTECTION emit)\n- Spawns: `{FAMILY_DRUID,0,kOrderLiving,120,120,0,0,10,300}`,\n\
    \  `{FAMILY_SOLDIER,0,kOrderLiving,150,120,0,0}` (**second team-0 friendly\n \
    \ within range 60 \u2014 required by `family_druid.cpp:95` `howmany > 1`**),\n\
    \  `{FAMILY_ARCHER,1,kOrderLiving,60,120,0,0}`,\n  `{FAMILY_ARCHER,1,kOrderLiving,220,120,0,0}`.\n\
    - Inputs: reuse `kInputsSpecialSlot4`.\n- Tick budget 150.\n- Facts:\n  - `pred::TickReached(150)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1)`\n  - `pred::WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1)`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 4)`\n- Mutation: file `src/gameplay/families/family_druid.cpp`, line 116.\n\
    \  - from: `                                alive = summon_entity(newob, Order::Weapon,\
    \ FAMILY_CIRCLE_PROTECTION);`\n  - to:   `                                return\
    \ false;`\n  - rationale: bypasses FAMILY_CIRCLE_PROTECTION creation; emit event\n\
    \    never fires; `WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION)` flips to\n \
    \   false.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`\n\
    2. In the master worktree:\n   `cmake --build --preset ci-test --target parity_dump_master`.\n\
    3. From the branch worktree:\n   `scripts/parity/capture_master_golden.sh effect_heartburst_multitarget_scen99\
    \ effect_poison_cloud_emit_scen99 effect_protection_emit_scen99`.\n4. Run `cmake\
    \ --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging predicates\
    \ with `pred::branch_only(...)` /\n   `pred::master_only(...)` and an `intended_diff:`\
    \ or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- All 3 new scenarios appear\
    \ in `kScenarios[]` with paired\n  `OG_PARITY_TEST(...)` macros.\n- All 3 new\
    \ goldens exist on disk.\n- `tests/parity/scenario_table.h` is byte-identical\
    \ to the master mirror.\n- `cmake --build --preset ci-test && ctest --preset ci-test`\
    \ reports 0\n  failures.\n- Cross-family consequences (FAMILY_EXPLOSION, FAMILY_SOLDIER\
    \ count)\n  carry `consequence:` labels.\n\n## Git Commit Requirement\n\nYou **must**\
    \ commit in **both** worktrees before yielding:\n\n- `git -C ../openglad-master\
    \ add tools/parity_scenario_table.h && git -C ../openglad-master commit -m \"\
    parity-companion: mirror scenario_table.h effect-emission\"`\n- `git add tests/parity/scenario_table.h\
    \ tests/parity/test_parity_scenarios.cpp tests/parity/golden/effect_heartburst_multitarget_scen99.json\
    \ tests/parity/golden/effect_poison_cloud_emit_scen99.json tests/parity/golden/effect_protection_emit_scen99.json\
    \ && git commit -m \"parity-cov: effect emission breadth scenarios\"`\n\nNo `--no-verify`\
    \ or `--amend` unless flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-effect-emission
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the 3 effect-emission\n\
    scenarios pass and the master mirror is in sync.\n\n## Preexisting Inputs\n\n\
    - `tests/parity/scenario_table.h`\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/golden/effect_heartburst_multitarget_scen99.json`\n- `tests/parity/golden/effect_poison_cloud_emit_scen99.json`\n\
    - `tests/parity/golden/effect_protection_emit_scen99.json`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    \n## What to Verify\n\n```bash\ncmake --build --preset ci-test && ctest --preset\
    \ ci-test\n```\n0 failures expected.\n\n```bash\n./build/ci-test/og_test_parity\
    \ --gtest_filter='Parity.effect_heartburst_multitarget_scen99:Parity.effect_poison_cloud_emit_scen99:Parity.effect_protection_emit_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/effect_heartburst_multitarget_scen99.json\
    \ tests/parity/golden/effect_poison_cloud_emit_scen99.json tests/parity/golden/effect_protection_emit_scen99.json\n\
    ```\nMust succeed.\n\n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nEach scenario must have \u2265 3 non-`TickReached` predicates\
    \ including \u2265 1\nconsequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-effect-emission
- id: impl-effect-emission~check-2
  type: check
  role: tester
  bounce_target: impl-effect-emission
- id: impl-effect-emission~check-3
  type: check
  role: senior-tester
  bounce_target: impl-effect-emission
- id: impl-effect-emission~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-effect-emission
- id: impl-effect-emission~check-5
  type: check
  role: architect
  bounce_target: impl-effect-emission
- id: impl-effect-emission~check-6
  type: check
  role: pm
  bounce_target: impl-effect-emission
- id: impl-effect-timers
  type: implement
  prompt: "You are adding 1 effect-timer scenario (`effect_bomb_timer_scen99`) to\n\
    the OpenGlad parity test suite \u2014 thief slot 1 DROP BOMB \u2192 FAMILY_BOMB\
    \ FX\nwalker.\n\n## Preexisting Inputs\n\nConsume these existing artifacts as-is\
    \ \u2014 do **not** refetch, recollect,\nrediscover, or regenerate them from scratch:\n\
    \n- `tests/parity/scenario_table.h` (reuses `kInputsSpecialSlot1`; 134\n  existing\
    \ + earlier-phase additions untouched)\n- `tests/parity/scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/fact_predicate.h`\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (depth/quality gates;\n  **not** modified)\n- `src/gameplay/families/effect_family_bomb.cpp:17-29`\
    \ (bomb on_death\n  adds FAMILY_EXPLOSION FX walker; on_act is `nullptr` at line\
    \ 95)\n- `src/gameplay/families/family_thief.cpp:61-91` (DROP BOMB slot 1; line\n\
    \  69 spawns FAMILY_BOMB)\n- `scripts/parity/capture_master_golden.sh`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    - `../openglad-master/tools/parity_scenario_runtime.cpp` (already carries\n  matching\
    \ wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 1 new scenario appended to `kScenarios[]`:\n  `effect_bomb_timer_scen99`.\n\
    - 1 `OG_PARITY_TEST(effect_bomb_timer_scen99)` macro appended.\n- 1 fully-populated\
    \ `kMut_effect_bomb_timer_scen99` mutation.\n- 1 new golden JSON file.\n- Updated\
    \ `../openglad-master/tools/parity_scenario_table.h`.\n- Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\
    \n## Scenario Spec\n\n**`effect_bomb_timer_scen99`** (thief slot 1 DROP BOMB)\n\
    - Spawns: `{FAMILY_THIEF,0,kOrderLiving,120,120,0,0,5,300}`,\n  `{FAMILY_SOLDIER,1,kOrderLiving,400,400,0,0}`\
    \ (far enough that the\n  bomb's blast does not delete it).\n- Inputs: reuse `kInputsSpecialSlot1`\
    \ (`scenario_table.h:1354-1357`).\n- Tick budget 30.\n- Facts:\n  - `pred::TickReached(30)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1)`\n  - `pred::EffectFamilyCount(FAMILY_BOMB,\
    \ 1, 2, -1, /*window_marker*/0, \"consequence: DROP BOMB slot 1 emits one FAMILY_BOMB\
    \ effect walker (FAMILY_BOMB's on_act is nullptr per effect_family_bomb.cpp:95,\
    \ so remaining lifetime stays at the initial 0 \u2014 but default window_marker=0\
    \ already counts every alive bomb regardless)\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 2)`\n  - `pred::WalkerOfTeamAlive(0, 1, 1)`\n- Mutation: file `src/gameplay/families/family_thief.cpp`,\
    \ line 69.\n  - from: `            newob = current_game->world->add_ob(Order::FX,\
    \ FAMILY_BOMB, 1);`\n  - to:   `            return false;`\n  - rationale: prevents\
    \ FAMILY_BOMB spawn; in-window count drops to 0;\n    predicate fails on lower\
    \ bound.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`\n\
    2. In the master worktree:\n   `cmake --build --preset ci-test --target parity_dump_master`.\n\
    3. From the branch worktree:\n   `scripts/parity/capture_master_golden.sh effect_bomb_timer_scen99`.\n\
    4. Run `cmake --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging\
    \ predicates with `pred::branch_only(...)` /\n   `pred::master_only(...)` and\
    \ an `intended_diff:` or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- `effect_bomb_timer_scen99`\
    \ appears in `kScenarios[]` with matching\n  `OG_PARITY_TEST(...)` macro.\n- Golden\
    \ file exists and is committed.\n- `tests/parity/scenario_table.h` is byte-identical\
    \ to the master mirror.\n- `cmake --build --preset ci-test && ctest --preset ci-test`\
    \ reports 0\n  failures.\n\n## Git Commit Requirement\n\nYou **must** commit in\
    \ **both** worktrees before yielding:\n\n- `git -C ../openglad-master add tools/parity_scenario_table.h\
    \ && git -C ../openglad-master commit -m \"parity-companion: mirror scenario_table.h\
    \ effect-timers\"`\n- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp\
    \ tests/parity/golden/effect_bomb_timer_scen99.json && git commit -m \"parity-cov:\
    \ bomb timer scenario\"`\n\nNo `--no-verify` or `--amend` unless flagged as recovery.\
    \ Do not yield\nuntil both commits land.\n"
- id: check-effect-timers
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the bomb-timer scenario\n\
    passes and the master mirror is in sync.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n\
    - `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/golden/effect_bomb_timer_scen99.json`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n\n## What to Verify\n\n\
    ```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.effect_bomb_timer_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/effect_bomb_timer_scen99.json\n\
    ```\nMust succeed.\n\n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nThe new scenario must have \u2265 3 non-`TickReached` predicates\
    \ including\n\u2265 1 consequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-effect-timers
- id: impl-effect-timers~check-2
  type: check
  role: tester
  bounce_target: impl-effect-timers
- id: impl-effect-timers~check-3
  type: check
  role: senior-tester
  bounce_target: impl-effect-timers
- id: impl-effect-timers~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-effect-timers
- id: impl-effect-timers~check-5
  type: check
  role: architect
  bounce_target: impl-effect-timers
- id: impl-effect-timers~check-6
  type: check
  role: pm
  bounce_target: impl-effect-timers
- id: impl-input-pipeline
  type: implement
  prompt: "You are adding 4 input-pipeline edge-case scenarios:\n`input_diagonal_movement_scen99`,\
    \ `input_hold_fire_search_scen99`,\n`input_switch_char_scen99`, `input_special_switch_wrap_scen99`.\n\
    \n## Preexisting Inputs\n\nConsume these existing artifacts as-is \u2014 do **not**\
    \ refetch, recollect,\nrediscover, or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h`\
    \ (134 existing + earlier-phase additions\n  untouched)\n- `tests/parity/scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` at lines 169-189 \u2014 **required by\n\
    \  `input_special_switch_wrap_scen99`**; **do not modify**)\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` at lines 143-163 \u2014 **do not modify**)\n\
    - `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/fact_predicate.h`\n\
    - `tests/parity/test_parity_coverage_gate.cpp` (depth/quality gates;\n  **not**\
    \ modified)\n- `src/interface/input/input_state.cpp:3-29` (`PlayerInput::move_x/move_y`\n\
    \  \u2014 diagonal decode lives **here**, not in `sim_input_handler.cpp`)\n- `src/gameplay/sim_input_handler.cpp:168-195`\
    \ (SwitchChar branch via\n  `InputAction::SwitchChar`/K_SWITCH; cycles friendly\
    \ walkers via\n  `sim_cycle_next_character`)\n- `src/gameplay/sim_input_handler.cpp:201-219`\
    \ (SwitchSpecial branch;\n  wrap when slot is missing/out-of-bounds/unlocked)\n\
    - `src/gameplay/sim_input_handler.cpp:326-351` (Fire branch; press vs\n  hold\
    \ distinction)\n- `include/openglad/gameplay/input_action.h` (`InputAction::SwitchChar`\
    \ =\n  10; K_SWITCH is **character-switch**, not weapon-switch)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 4 new scenarios appended to `kScenarios[]`:\n  - `input_diagonal_movement_scen99`\
    \ (PlayerInput diagonal decode)\n  - `input_hold_fire_search_scen99` (Fire-held\
    \ vs Fire-pressed)\n  - `input_switch_char_scen99` (K_SWITCH transfers control\
    \ across\n    same-team walkers)\n  - `input_special_switch_wrap_scen99` (K_SPECIAL_SWITCH\
    \ wraps 1\u21925\u21921)\n- 4 `OG_PARITY_TEST(...)` macros appended.\n- 4 fully-populated\
    \ `kMut_*` mutations.\n- 4 new golden JSON files.\n- Updated `../openglad-master/tools/parity_scenario_table.h`.\n\
    - Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario\
    \ Specs\n\n**`input_diagonal_movement_scen99`**\n- Spawns: `{FAMILY_SOLDIER,0,kOrderLiving,160,160,0,0}`.\n\
    - Inputs (new inline list): `{1,0,K_DOWN_RIGHT},{40,0,K_NONE}`.\n- Tick budget\
    \ 80.\n- Use `K_DOWN_RIGHT` over `K_UP_RIGHT` because `WalkerPositionMoved` is\
    \ a\n  lower-bound predicate on both axes; upward motion decreases ypos.\n- Facts:\n\
    \  - `pred::TickReached(80)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER, 1,\
    \ 1)`\n  - `pred::WalkerPositionMoved(FAMILY_SOLDIER, 175, 175, \"consequence:\
    \ K_DOWN_RIGHT increases both xpos (160\u2192175+) and ypos (160\u2192175+); each\
    \ floor above spawn\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 1, \"rng_drift:\
    \ footstep count varies with tile boundaries\")`\n  - `pred::WalkerOfTeamAlive(0,\
    \ 1, 1)`\n- Mutation: file `src/interface/input/input_state.cpp`, line 26.\n \
    \ - from: `        held[static_cast<int>(InputKey::DownRight)])`\n  - to:   `\
    \        false)`\n  - rationale: removing `InputKey::DownRight` from `move_y()`'s\n\
    \    down-recognition disjunction makes K_DOWN_RIGHT register only on x;\n   \
    \ ypos stays at 160; predicate fails on the y floor.\n\n**`input_hold_fire_search_scen99`**\n\
    - Spawns: `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0}`,\n  `{FAMILY_ARCHER,1,kOrderLiving,220,200,0,0}`.\n\
    - Inputs (new inline list): `{5,0,K_FIRE},{200,0,K_NONE}`.\n- Tick budget 250.\n\
    - Facts:\n  - `pred::TickReached(250)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n  - `pred::WeaponFamilyEmitted(FAMILY_KNIFE)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER,\
    \ 0, 9000, \"rng_drift: archer HP drops as soldier approaches and fires\")`\n\
    \  - `pred::EventKindAtLeast(/*play_sound*/1, 6)`\n- Mutation: file `src/gameplay/sim_input_handler.cpp`,\
    \ line 350.\n  - from: `        if (pi.is_held(InputAction::Fire))`\n  - to: \
    \  `        if (false)`\n  - rationale: disabling held-fire leaves only press-fire\
    \ at line 329;\n    soldier fires only once at tick 5; far fewer knives reach\
    \ archer;\n    archer HP stays above 9000; predicate fails on upper bound.\n\n\
    **`input_switch_char_scen99`**\n- Spawns (both walkers with `team_num = 255` so\
    \ `apply_post_load_spawns`\n  writes `real_team_num = 255`):\n  - `{FAMILY_SOLDIER,255,kOrderLiving,120,120,0,0,3,200}`\n\
    \  - `{FAMILY_ARCHER,255,kOrderLiving,100,140,0,0,3,200}`\n- **`ScenarioSpec.player_team\
    \ = 255`** so `find_player_walker` selects\n  the soldier (first in spawn order)\
    \ as initial control.\n- Inputs (new inline list):\n  `{1,0,K_RIGHT},{20,0,K_NONE},{30,0,K_SWITCH},{31,0,K_NONE},{40,0,K_RIGHT},{120,0,K_NONE}`.\n\
    - Tick budget 150.\n- Facts:\n  - `pred::TickReached(150)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n  - `pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1)`\n  - `pred::WalkerPositionMoved(FAMILY_ARCHER,\
    \ 110, 140, \"consequence: K_SWITCH at tick 30 transfers control to archer under\
    \ team=255/player_team=255 spawn; K_RIGHT ticks 40-119 walk archer past xpos 110\
    \ from spawn xpos 100\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 1)`\n\
    - Mutation: file `src/gameplay/sim_input_handler.cpp`, line 188.\n  - from: `\
    \        control = sim_cycle_next_character(level.oblist, oldcontrol, reverse,\
    \ filter);`\n  - to:   `        control = oldcontrol;`\n  - rationale: control\
    \ stays on the soldier; archer never receives\n    `ACT_CONTROL` and stays at\
    \ xpos 100; predicate fails on x floor.\n\n**`input_special_switch_wrap_scen99`**\
    \ (depends on the already-wired\n`special_names_table`)\n- Spawns: `{FAMILY_MAGE,0,kOrderLiving,120,120,0,0,13,600}`\
    \ (level 13\n  enables all 5 mage slots; magicpoints 600 covers slot 3's\n  `special_cost\
    \ = 500`), `{FAMILY_SOLDIER,1,kOrderLiving,200,120,0,0}`.\n- Inputs (new inline\
    \ list): 12 alternating `K_SPECIAL_SWITCH`/`K_NONE`\n  presses across ticks 5..28,\
    \ then `K_SPECIAL` at tick 30:\n  `{5,0,K_SPECIAL_SWITCH},{6,0,K_NONE},{7,0,K_SPECIAL_SWITCH},{8,0,K_NONE},{9,0,K_SPECIAL_SWITCH},{10,0,K_NONE},{11,0,K_SPECIAL_SWITCH},{12,0,K_NONE},{13,0,K_SPECIAL_SWITCH},{14,0,K_NONE},{15,0,K_SPECIAL_SWITCH},{16,0,K_NONE},{17,0,K_SPECIAL_SWITCH},{18,0,K_NONE},{19,0,K_SPECIAL_SWITCH},{20,0,K_NONE},{21,0,K_SPECIAL_SWITCH},{22,0,K_NONE},{23,0,K_SPECIAL_SWITCH},{24,0,K_NONE},{25,0,K_SPECIAL_SWITCH},{26,0,K_NONE},{27,0,K_SPECIAL_SWITCH},{28,0,K_NONE},{30,0,K_SPECIAL},{31,0,K_NONE}`.\n\
    - Tick budget 150.\n- With the harness wiring in place: 12 presses cycle\n  `1\u2192\
    2\u21923\u21924\u21925\u21921\u21922\u21923\u21924\u21925\u21921\u21922\u2192\
    3`, landing on slot 3 (FREEZE TIME).\n  K_SPECIAL at tick 30 fires FREEZE TIME.\n\
    - Facts:\n  - `pred::TickReached(150)`\n  - `pred::WalkerFamilyCount(FAMILY_MAGE,\
    \ 1, 1)`\n  - `pred::WalkerPositionMoved(FAMILY_SOLDIER, 200, 120, \"consequence:\
    \ FREEZE TIME pins soldier at spawn; both floors equal spawn coords\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 2)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 15000, \"rng_drift:\
    \ which special fires depends on wraparound landing slot\")`\n- Mutation: file\
    \ `src/gameplay/sim_input_handler.cpp`, line 204.\n  - from: `        control->set_current_special(control->current_special()\
    \ + 1);`\n  - to:   `        control->set_current_special(1);`\n  - rationale:\
    \ clamping to slot 1 (TELEPORT) regardless of press count;\n    wraparound never\
    \ reaches slot 3 (FREEZE TIME); `world.enemy_freeze`\n    never written; soldier\
    \ acts normally and walks away from spawn;\n    predicate fails on whichever axis\
    \ the soldier leaves.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`\n2. In the master worktree:\n\
    \   `cmake --build --preset ci-test --target parity_dump_master`.\n3. From the\
    \ branch worktree:\n   `scripts/parity/capture_master_golden.sh input_diagonal_movement_scen99\
    \ input_hold_fire_search_scen99 input_switch_char_scen99 input_special_switch_wrap_scen99`.\n\
    4. Run `cmake --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging\
    \ predicates with `pred::branch_only(...)` /\n   `pred::master_only(...)` and\
    \ an `intended_diff:` or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- All\
    \ 4 new scenarios appear in `kScenarios[]` with paired\n  `OG_PARITY_TEST(...)`\
    \ macros.\n- All 4 new goldens exist on disk.\n- `tests/parity/scenario_table.h`\
    \ is byte-identical to the master mirror.\n- `cmake --build --preset ci-test &&\
    \ ctest --preset ci-test` reports 0\n  failures.\n- `tests/parity/scenario_runtime.cpp`\
    \ still threads\n  `special_names_table` (untouched by this phase).\n\n## Git\
    \ Commit Requirement\n\nYou **must** commit in **both** worktrees before yielding:\n\
    \n- `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master\
    \ commit -m \"parity-companion: mirror scenario_table.h input-pipeline\"`\n- `git\
    \ add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp tests/parity/golden/input_diagonal_movement_scen99.json\
    \ tests/parity/golden/input_hold_fire_search_scen99.json tests/parity/golden/input_switch_char_scen99.json\
    \ tests/parity/golden/input_special_switch_wrap_scen99.json && git commit -m \"\
    parity-cov: input pipeline edge-case scenarios\"`\n\nNo `--no-verify` or `--amend`\
    \ unless flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-input-pipeline
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the 4 input-pipeline\n\
    scenarios pass, the master mirror is in sync, and the harness\n`special_names_table`\
    \ wiring is intact.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n\
    - `tests/parity/scenario_runtime.cpp`\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/golden/input_diagonal_movement_scen99.json`\n- `tests/parity/golden/input_hold_fire_search_scen99.json`\n\
    - `tests/parity/golden/input_switch_char_scen99.json`\n- `tests/parity/golden/input_special_switch_wrap_scen99.json`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n\n## What to Verify\n\n\
    ```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.input_diagonal_movement_scen99:Parity.input_hold_fire_search_scen99:Parity.input_switch_char_scen99:Parity.input_special_switch_wrap_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/input_diagonal_movement_scen99.json\
    \ tests/parity/golden/input_hold_fire_search_scen99.json tests/parity/golden/input_switch_char_scen99.json\
    \ tests/parity/golden/input_special_switch_wrap_scen99.json\n```\nMust succeed.\n\
    \n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\ngrep -n 'special_names_table' tests/parity/scenario_runtime.cpp\n\
    ```\nMust return \u2265 1 match.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nEach scenario must have \u2265 3 non-`TickReached` predicates\
    \ including \u2265 1\nconsequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-input-pipeline
- id: impl-input-pipeline~check-2
  type: check
  role: tester
  bounce_target: impl-input-pipeline
- id: impl-input-pipeline~check-3
  type: check
  role: senior-tester
  bounce_target: impl-input-pipeline
- id: impl-input-pipeline~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-input-pipeline
- id: impl-input-pipeline~check-5
  type: check
  role: architect
  bounce_target: impl-input-pipeline
- id: impl-input-pipeline~check-6
  type: check
  role: pm
  bounce_target: impl-input-pipeline
- id: impl-multiplayer-teams
  type: implement
  prompt: "You are adding 1 multi-team coordination scenario\n(`multiplayer_two_teams_scen99`)\
    \ \u2014 player team 0 + NPC team 1 + NPC team\n2 exercising NPC-vs-NPC team comparison.\n\
    \n## Preexisting Inputs\n\nConsume these existing artifacts as-is \u2014 do **not**\
    \ refetch, recollect,\nrediscover, or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h`\
    \ (134 existing + earlier-phase additions\n  untouched)\n- `tests/parity/scenario_runtime.cpp:42-85`\
    \ (`apply_post_load_spawns`\n  constructs each walker without a `myguy` savestate\
    \ \u2014 every harness\n  walker rides the no-myguy branch in `walker::is_friendly`;\
    \ **do not\n  modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/fact_predicate.h`\n\
    - `tests/parity/test_parity_coverage_gate.cpp` (depth/quality gates;\n  **not**\
    \ modified)\n- `src/gameplay/walker.cpp:1675-1742` (`is_friendly`; the no-myguy\
    \ branch\n  at lines 1711-1716 fires for **every** pair in this scenario; line\n\
    \  1723 `return headus->team_num() == headtarget->team_num();` is the\n  load-bearing\
    \ line)\n- `scripts/parity/capture_master_golden.sh`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    - `../openglad-master/tools/parity_scenario_runtime.cpp` (already carries\n  matching\
    \ wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 1 new scenario appended to `kScenarios[]`:\n  `multiplayer_two_teams_scen99`.\n\
    - 1 `OG_PARITY_TEST(multiplayer_two_teams_scen99)` macro appended.\n- 1 fully-populated\
    \ `kMut_multiplayer_two_teams_scen99` mutation\n  (`walker.cpp` uses tabs).\n\
    - 1 new golden JSON file.\n- Updated `../openglad-master/tools/parity_scenario_table.h`.\n\
    - Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario\
    \ Spec\n\n**`multiplayer_two_teams_scen99`**\n- Spawns:\n  - `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,3,200}`\
    \ (player team 0)\n  - `{FAMILY_THIEF,2,kOrderLiving,140,140,0,0,3,200}` (NPC\
    \ team 2)\n  - `{FAMILY_ARCHER,1,kOrderLiving,200,200,0,0}` (NPC team 1)\n- Inputs\
    \ (new inline list):\n  `{5,0,K_RIGHT},{30,0,K_NONE},{35,0,K_FIRE},{200,0,K_NONE}`.\n\
    - Tick budget 200.\n- Facts:\n  - `pred::TickReached(200)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 1, 1)`\n  - `pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER,\
    \ 0, 9000, \"rng_drift: archer HP depends on combat order \u2014 live: team-2\
    \ thief attacks team-1 archer because their team_num differs (line 1723); mutated:\
    \ thief and archer are mutual friends, archer is only attacked by the team-0 soldier\
    \ from a distance\")`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 4)`\n- Mutation:\
    \ file `src/gameplay/walker.cpp`, line 1723. `walker.cpp` uses\n  tabs \u2014\
    \ preserve the two leading tabs.\n  - from: `\t\treturn headus->team_num() ==\
    \ headtarget->team_num();`\n  - to:   `\t\treturn 1;`\n  - rationale: hardwiring\
    \ the no-myguy branch's return to `1` makes\n    every pair mutually friendly;\
    \ thief and archer never attack;\n    archer HP stays high; predicate fails on\
    \ upper bound.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`\n2. In the master worktree:\n\
    \   `cmake --build --preset ci-test --target parity_dump_master`.\n3. From the\
    \ branch worktree:\n   `scripts/parity/capture_master_golden.sh multiplayer_two_teams_scen99`.\n\
    4. Run `cmake --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging\
    \ predicates with `pred::branch_only(...)` /\n   `pred::master_only(...)` and\
    \ an `intended_diff:` or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- `multiplayer_two_teams_scen99`\
    \ appears in `kScenarios[]` with matching\n  `OG_PARITY_TEST(...)` macro.\n- Golden\
    \ file exists and is committed.\n- `tests/parity/scenario_table.h` is byte-identical\
    \ to the master mirror.\n- `cmake --build --preset ci-test && ctest --preset ci-test`\
    \ reports 0\n  failures.\n- Mutation `from`/`to` preserves tab indentation of\n\
    \  `src/gameplay/walker.cpp:1723`.\n\n## Git Commit Requirement\n\nYou **must**\
    \ commit in **both** worktrees before yielding:\n\n- `git -C ../openglad-master\
    \ add tools/parity_scenario_table.h && git -C ../openglad-master commit -m \"\
    parity-companion: mirror scenario_table.h multiplayer-teams\"`\n- `git add tests/parity/scenario_table.h\
    \ tests/parity/test_parity_scenarios.cpp tests/parity/golden/multiplayer_two_teams_scen99.json\
    \ && git commit -m \"parity-cov: multi-team is_friendly scenario\"`\n\nNo `--no-verify`\
    \ or `--amend` unless flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-multiplayer-teams
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the multi-team scenario\n\
    passes, exercises NPC-vs-NPC friendship via the no-myguy branch, and the\nmaster\
    \ mirror is in sync.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n\
    - `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/golden/multiplayer_two_teams_scen99.json`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n\n## What to Verify\n\n\
    ```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.multiplayer_two_teams_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/multiplayer_two_teams_scen99.json\n\
    ```\nMust succeed.\n\n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\nThe new scenario must have \u2265 3 non-`TickReached` predicates\
    \ including\n\u2265 1 consequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-multiplayer-teams
- id: impl-multiplayer-teams~check-2
  type: check
  role: tester
  bounce_target: impl-multiplayer-teams
- id: impl-multiplayer-teams~check-3
  type: check
  role: senior-tester
  bounce_target: impl-multiplayer-teams
- id: impl-multiplayer-teams~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-multiplayer-teams
- id: impl-multiplayer-teams~check-5
  type: check
  role: architect
  bounce_target: impl-multiplayer-teams
- id: impl-multiplayer-teams~check-6
  type: check
  role: pm
  bounce_target: impl-multiplayer-teams
- id: impl-level-withdraw
  type: implement
  prompt: "You are adding 1 level-transition (withdraw) scenario\n(`level_withdraw_scen99`)\
    \ that reuses the spawn+input of the canonical\n`scripted_input_scen9301` row\
    \ paired with a withdraw-specific mutation\nand a `LevelDoneEquals(2)` consequence.\n\
    \n## Preexisting Inputs\n\nConsume these existing artifacts as-is \u2014 do **not**\
    \ refetch, recollect,\nrediscover, or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h`\
    \ (134 existing + earlier-phase additions\n  untouched). **Reuses by name:**\n\
    \  - `kFamilySpawns_soldier_with_exit_withdraw` (lines 372-376)\n  - `kInputsScripted9301`\
    \ (lines 263-270)\n  - The existing `scripted_input_scen9301` row at lines 3629-3636\
    \ is the\n    **canonical working withdraw scenario** \u2014 its spawn+input lists\
    \ are\n    reused as-is rather than paraphrased.\n- `tests/parity/scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/fact_predicate.h`\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (depth/quality gates;\n  **not** modified)\n- `src/gameplay/game_world.cpp:1357,1391-1499`\
    \ (withdraw handling;\n  `level_done = 2` is the pre-loop default at line 1357;\n\
    \  `withdraw_requested` checks at 1393/1438/1460/1480 break/return early,\n  preserving\
    \ the default by skipping the `level_done = 0` writes at\n  1408/1432/1455)\n\
    - `src/gameplay/families/treasure_family_navigation.cpp:36-98`\n  (`exit_on_eat`;\
    \ writes `world.withdraw_requested = true` at line 86;\n  emits `WithdrawToLevel`\
    \ event at 88-90)\n- `scripts/parity/capture_master_golden.sh`\n- `../openglad-master/tools/parity_scenario_table.h`\n\
    - `../openglad-master/tools/parity_scenario_runtime.cpp` (already carries\n  matching\
    \ wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 1 new scenario appended to `kScenarios[]`: `level_withdraw_scen99`.\n- 1 `OG_PARITY_TEST(level_withdraw_scen99)`\
    \ macro appended.\n- 1 new fact array declared inline (`kFacts_level_withdraw_scen99`)\
    \ so\n  9301's predicates are not run twice.\n- 1 fully-populated `kMut_level_withdraw_scen99`\
    \ mutation.\n- 1 new golden JSON file.\n- Updated `../openglad-master/tools/parity_scenario_table.h`.\n\
    - Rebuilt `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario\
    \ Spec\n\n**`level_withdraw_scen99`**\n- Spawn list: **reuse `kFamilySpawns_soldier_with_exit_withdraw`**\
    \ by\n  name (no new spawn-list constant declared).\n- Inputs: **reuse `kInputsScripted9301`**\
    \ by name.\n- Tick budget 200 (`scripted_input_scen9301` itself runs to 150;\n\
    \  extended to 200 so the early-return after `withdraw_requested` settles\n  before\
    \ predicates evaluate).\n- Differentiation from `scripted_input_scen9301`: the\
    \ existing 9301 row\n  uses mutation `kMut_walker_ai_wander` (generic AI). This\
    \ new row pairs\n  the same spawn + input precedent with a *withdraw-specific*\
    \ mutation\n  and a `LevelDoneEquals(2)` consequence. The new row declares its\
    \ own\n  fact array so 9301's predicates are not run twice.\n- Facts (new `kFacts_level_withdraw_scen99`\
    \ array):\n  - `pred::TickReached(200)`\n  - `pred::WalkerFamilyCount(FAMILY_SOLDIER,\
    \ 2, 2)` (structural; matches\n    9301's two soldiers)\n  - `pred::LevelDoneEquals(2,\
    \ \"consequence: withdraw path returns level_done=2 (default set at game_world.cpp:1357,\
    \ preserved when withdraw_requested break at 1393 short-circuits writes at 1408/1432/1455)\"\
    )`\n  - `pred::EventKindAtLeast(/*withdraw_to_level*/8, 1)`\n  - `pred::EventKindAtLeast(/*play_sound*/1,\
    \ 2)`\n- Mutation: file `src/gameplay/families/treasure_family_navigation.cpp`,\n\
    \  line 86.\n  - from: `        world.withdraw_requested = true;`\n  - to:   `\
    \        world.withdraw_requested = false;`\n  - rationale: with `withdraw_requested`\
    \ left false the early-break at\n    `game_world.cpp:1393` and the early-returns\
    \ at 1438/1460/1480 never\n    fire; the oblist for-loop completes, visits the\
    \ surviving team-1\n    soldier, and sets `level_done = 0` at line 1408; `LevelDoneEquals(2)`\n\
    \    fails. (The `WithdrawToLevel` event at navigation:88-90 is emitted\n    unconditionally\
    \ after the flag-write, so it is not what flips;\n    `LevelDoneEquals(2)` is\
    \ the decisive predicate.)\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`\n2. In the master worktree:\n\
    \   `cmake --build --preset ci-test --target parity_dump_master`.\n3. From the\
    \ branch worktree:\n   `scripts/parity/capture_master_golden.sh level_withdraw_scen99`.\n\
    4. Run `cmake --build --preset ci-test && ctest --preset ci-test`. Wrap\n   diverging\
    \ predicates with `pred::branch_only(...)` /\n   `pred::master_only(...)` and\
    \ an `intended_diff:` or `rng_drift:`\n   label.\n\n## Success Criteria\n\n- `level_withdraw_scen99`\
    \ appears in `kScenarios[]` with matching\n  `OG_PARITY_TEST(...)` macro.\n- Existing\
    \ `kFamilySpawns_soldier_with_exit_withdraw` and\n  `kInputsScripted9301` constants\
    \ are reused by name (no duplicate\n  declarations).\n- Golden file exists and\
    \ is committed.\n- `tests/parity/scenario_table.h` is byte-identical to the master\
    \ mirror.\n- `cmake --build --preset ci-test && ctest --preset ci-test` reports\
    \ 0\n  failures.\n\n## Git Commit Requirement\n\nYou **must** commit in **both**\
    \ worktrees before yielding:\n\n- `git -C ../openglad-master add tools/parity_scenario_table.h\
    \ && git -C ../openglad-master commit -m \"parity-companion: mirror scenario_table.h\
    \ level-withdraw\"`\n- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp\
    \ tests/parity/golden/level_withdraw_scen99.json && git commit -m \"parity-cov:\
    \ level withdraw scenario\"`\n\nNo `--no-verify` or `--amend` unless flagged as\
    \ recovery. Do not yield\nuntil both commits land.\n"
- id: check-level-withdraw
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm the withdraw scenario\n\
    passes, reuses the canonical 9301 spawn/input constants by name, and the\nmaster\
    \ mirror is in sync.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n\
    - `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/golden/level_withdraw_scen99.json`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n\n## What to Verify\n\n\
    ```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.level_withdraw_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/level_withdraw_scen99.json\n\
    ```\nMust succeed.\n\n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*'\n\
    ```\nMust pass.\n\n```bash\ngrep -c 'kFamilySpawns_soldier_with_exit_withdraw'\
    \ tests/parity/scenario_table.h\n```\nExpected \u2265 2 (declaration + 2 reuses:\
    \ 9301 and the new row).\n\n```bash\ngrep -c 'kInputsScripted9301' tests/parity/scenario_table.h\n\
    ```\nExpected \u2265 2 (declaration + 2 reuses).\n\nThe new scenario must have\
    \ \u2265 3 non-`TickReached` predicates including\n\u2265 1 consequence (`LevelDoneEquals(2)`).\n\
    \n## Verdict\n\nIf every check above succeeds, emit:\n`VERDICT: PASS`\n\nOtherwise\
    \ emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-level-withdraw
- id: impl-level-withdraw~check-2
  type: check
  role: tester
  bounce_target: impl-level-withdraw
- id: impl-level-withdraw~check-3
  type: check
  role: senior-tester
  bounce_target: impl-level-withdraw
- id: impl-level-withdraw~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-level-withdraw
- id: impl-level-withdraw~check-5
  type: check
  role: architect
  bounce_target: impl-level-withdraw
- id: impl-level-withdraw~check-6
  type: check
  role: pm
  bounce_target: impl-level-withdraw
- id: impl-midcombat-state
  type: implement
  prompt: "You are adding 2 mid-combat walker-state scenarios:\n`midcombat_partial_hp_scen99`\
    \ and `consumable_inventory_state_scen99`.\n\n## Preexisting Inputs\n\nConsume\
    \ these existing artifacts as-is \u2014 do **not** refetch, recollect,\nrediscover,\
    \ or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h` (134 existing\
    \ + earlier-phase additions\n  untouched)\n- `tests/parity/scenario_runtime.cpp`\
    \ (already wires\n  `special_names_table` \u2014 **do not modify**)\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/fact_predicate.h`\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (depth/quality gates;\n  `label_exempted` prefixes at 702-707; **not** modified)\n\
    - `src/resources/save_data.cpp` (defines walker fields the dump observes;\n  the\
    \ parity harness does **not** invoke `save_game()`, it inspects the\n  live oblist)\n\
    - `src/gameplay/families/treasure_family_consumables.cpp` (drumstick at\n  line\
    \ 25; magic_potion on_eat)\n- `src/gameplay/walker_combat.cpp:178-206` (`walker::do_combat_damage`,\n\
    \  the single central combat-damage HP write at line 189; arrow/knife\n  both\
    \ route through `walker::attack` \u2192 `do_combat_damage`)\n- `scripts/parity/capture_master_golden.sh`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already carries\n  matching wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `../openglad-master/` worktree on branch `parity-companion`\n\n## New Outputs\n\
    \n- 2 new scenarios appended to `kScenarios[]`:\n  - `midcombat_partial_hp_scen99`\
    \ (mid-combat HP after archer fire)\n  - `consumable_inventory_state_scen99` (drumstick\
    \ + magic_potion\n    pickups, walker HP boost path)\n- 2 `OG_PARITY_TEST(...)`\
    \ macros appended.\n- 2 fully-populated `kMut_*` mutations.\n- 2 new golden JSON\
    \ files.\n- Updated `../openglad-master/tools/parity_scenario_table.h`.\n- Rebuilt\
    \ `../openglad-master/build/ci-test/parity_dump_master`.\n\n## Scenario Specs\n\
    \n**`midcombat_partial_hp_scen99`**\n- Spawns:\n  - `{FAMILY_SOLDIER,0,kOrderLiving,120,120,0,0,3,0}`\n\
    \  - `{FAMILY_ARCHER,1,kOrderLiving,60,120,0,0}`\n  - `{FAMILY_ARCHER,1,kOrderLiving,180,120,0,0}`\n\
    - Inputs (new inline list): `{5,0,K_FIRE},{40,0,K_NONE}`.\n- Tick budget 80 (short\
    \ so HP lands in mid-band rather than 0).\n- Facts:\n  - `pred::TickReached(80)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER,\
    \ 4000, 11000, \"intended_diff: mid-combat HP varies between branch and master\
    \ combat ordering; widened range (7000 cents) carries label per label_exempted\
    \ at test_parity_coverage_gate.cpp:702-707\")`\n  - `pred::WalkerOfTeamAlive(0,\
    \ 1, 1)`\n  - `pred::EventKindAtLeast(/*play_sound*/1, 5)`\n- Mutation: file `src/gameplay/walker_combat.cpp`,\
    \ line 189.\n  - from: `    target->stats()->set_hitpoints(target->stats()->hitpoints()\
    \ - tempdamage);`\n  - to:   `    target->stats()->set_hitpoints(target->stats()->hitpoints()\
    \ - 0);`\n  - rationale: zeroing combat damage at the single central HP-write\n\
    \    site; archers cannot reduce soldier HP; final HP exceeds 11000;\n    predicate\
    \ fails on upper bound.\n\n**`consumable_inventory_state_scen99`**\n- Spawns:\n\
    \  - `{FAMILY_SOLDIER,0,kOrderLiving,96,120,0,0,3,0}`\n  - `{FAMILY_DRUMSTICK,0,kOrderTreasure,128,120,0,0}`\n\
    \  - `{FAMILY_MAGIC_POTION,0,kOrderTreasure,160,120,0,0}`\n- Inputs (new inline\
    \ list): `{1,0,K_RIGHT},{40,0,K_NONE}`.\n- Tick budget 150.\n- Facts:\n  - `pred::TickReached(150)`\n\
    \  - `pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1)`\n  - `pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_DRUMSTICK,\
    \ kOrderTreasure)`\n  - `pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_MAGIC_POTION,\
    \ kOrderTreasure)`\n  - `pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 9000,\
    \ 13000, \"intended_diff: drumstick HP boost varies with rng_.next(10*level) roll\"\
    )`\n- Mutation: file `src/gameplay/families/treasure_family_consumables.cpp`,\n\
    \  line 25.\n  - from: `    eater->stats()->set_hitpoints(eater->stats()->hitpoints()\
    \ + amount);`\n  - to:   `    eater->stats()->set_hitpoints(eater->stats()->hitpoints()\
    \ + 0);`\n  - rationale: replaces drumstick heal with a no-op (preserving 4-space\n\
    \    indentation); soldier still eats the drumstick (so the\n    TreasureFamilyOfOrderRemovedFromOblist\
    \ predicate still passes) but\n    HP at tick 150 drops below 9000; predicate\
    \ fails on lower bound.\n\n## Mirror + Capture\n\n1. `cp tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`\n2. In the master worktree:\n\
    \   `cmake --build --preset ci-test --target parity_dump_master`.\n3. From the\
    \ branch worktree:\n   `scripts/parity/capture_master_golden.sh midcombat_partial_hp_scen99\
    \ consumable_inventory_state_scen99`.\n4. Run `cmake --build --preset ci-test\
    \ && ctest --preset ci-test`. Wrap\n   diverging predicates with `pred::branch_only(...)`\
    \ /\n   `pred::master_only(...)` and an `intended_diff:` or `rng_drift:`\n   label.\n\
    \n## Success Criteria\n\n- Both new scenarios appear in `kScenarios[]` with paired\n\
    \  `OG_PARITY_TEST(...)` macros.\n- Both new goldens exist on disk and are committed.\n\
    - `tests/parity/scenario_table.h` is byte-identical to the master mirror.\n- `cmake\
    \ --build --preset ci-test && ctest --preset ci-test` reports 0\n  failures.\n\
    - The 7000-cent HP range on `midcombat_partial_hp_scen99` carries an\n  `intended_diff:`\
    \ label.\n\n## Git Commit Requirement\n\nYou **must** commit in **both** worktrees\
    \ before yielding:\n\n- `git -C ../openglad-master add tools/parity_scenario_table.h\
    \ && git -C ../openglad-master commit -m \"parity-companion: mirror scenario_table.h\
    \ midcombat-state\"`\n- `git add tests/parity/scenario_table.h tests/parity/test_parity_scenarios.cpp\
    \ tests/parity/golden/midcombat_partial_hp_scen99.json tests/parity/golden/consumable_inventory_state_scen99.json\
    \ && git commit -m \"parity-cov: mid-combat walker-state scenarios\"`\n\nNo `--no-verify`\
    \ or `--amend` unless flagged as recovery. Do not yield\nuntil both commits land.\n"
- id: check-midcombat-state
  type: check
  prompt: "You are a **parity-scenario reviewer**. Confirm both mid-combat scenarios\n\
    pass, the widened HP range on `midcombat_partial_hp_scen99` carries the\nrequired\
    \ `intended_diff:` label, and the master mirror is in sync.\n\n## Preexisting\
    \ Inputs\n\n- `tests/parity/scenario_table.h`\n- `tests/parity/test_parity_scenarios.cpp`\n\
    - `tests/parity/golden/midcombat_partial_hp_scen99.json`\n- `tests/parity/golden/consumable_inventory_state_scen99.json`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n\n## What to Verify\n\n\
    ```bash\ncmake --build --preset ci-test && ctest --preset ci-test\n```\n0 failures\
    \ expected.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='Parity.midcombat_partial_hp_scen99:Parity.consumable_inventory_state_scen99'\n\
    ```\nMust pass.\n\n```bash\nls tests/parity/golden/midcombat_partial_hp_scen99.json\
    \ tests/parity/golden/consumable_inventory_state_scen99.json\n```\nMust succeed.\n\
    \n```bash\ndiff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\n./build/ci-test/og_test_parity --gtest_filter='*depth_gate*:*golden_evaluation_gate*:Parity.predicate_depth_gate_no_trivially_wide_ranges'\n\
    ```\nMust pass.\n\nEach scenario must have \u2265 3 non-`TickReached` predicates\
    \ including \u2265 1\nconsequence.\n\n## Verdict\n\nIf every check above succeeds,\
    \ emit:\n`VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-midcombat-state
- id: impl-midcombat-state~check-2
  type: check
  role: tester
  bounce_target: impl-midcombat-state
- id: impl-midcombat-state~check-3
  type: check
  role: senior-tester
  bounce_target: impl-midcombat-state
- id: impl-midcombat-state~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-midcombat-state
- id: impl-midcombat-state~check-5
  type: check
  role: architect
  bounce_target: impl-midcombat-state
- id: impl-midcombat-state~check-6
  type: check
  role: pm
  bounce_target: impl-midcombat-state
- id: impl-final-docs
  type: implement
  prompt: "You are the **final-docs engineer** for the OpenGlad parity coverage\n\
    pass 2. Refresh `.plan/parity-present-state.md` and\n`.plan/parity-coverage-manifest.md`\
    \ to reflect the 156-scenario state\n(134 preexisting + 22 added across the 10\
    \ earlier phases). No source\ncode changes.\n\n## Preexisting Inputs\n\nConsume\
    \ these existing artifacts as-is \u2014 do **not** refetch, recollect,\nrediscover,\
    \ or regenerate them from scratch:\n\n- `tests/parity/scenario_table.h` (156 scenarios\
    \ after Phases 1-10: 134\n  preexisting + 22 added)\n- `tests/parity/test_parity_scenarios.cpp`\
    \ (156 `OG_PARITY_TEST(...)`\n  macro uses after Phases 1-10; each macro pairs\
    \ one-for-one with a\n  `kScenarios[]` row)\n- `tests/parity/scenario_runtime.cpp`\
    \ (carries the already-wired\n  `special_names_table` \u2014 **do not modify**)\n\
    - `tests/parity/golden/*.json` (155 goldens after Phases 1-10)\n- `tests/parity/test_parity_coverage_gate.cpp`\
    \ (7 depth/quality gates;\n  **not** modified)\n- `../openglad-master/tools/parity_scenario_table.h`\
    \ (byte-mirrored each\n  phase)\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ (already carries\n  the harness wiring \u2014 **do not modify**)\n- `../openglad-master/build/ci-test/parity_dump_master`\n\
    - `.plan/parity-present-state.md` (current 134-scenario doc; **update in\n  place**)\n\
    - `.plan/parity-coverage-manifest.md` (current 134-scenario doc;\n  **update in\
    \ place**)\n- `../openglad-master/` worktree on branch `parity-companion`\n\n\
    ## New Outputs\n\n- Updated `.plan/parity-present-state.md` reflecting 156 scenarios\
    \ (154\n  SemanticParity + 2 Invariant) and 155 goldens.\n- Updated `.plan/parity-coverage-manifest.md`\
    \ with a `## Gap-Fill\n  Scenarios Added in Coverage Pass 2` section listing the\
    \ 22 new\n  scenarios grouped by the 10 phase categories; updated Final Behavioral\n\
    \  Coverage Status Per Entity table.\n\n## Implementation Details\n\n1. Re-confirm\
    \ the master mirror is still in sync:\n   `diff -q tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h`.\n   If not, re-mirror\n \
    \  (`cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`)\n\
    \   and rebuild `parity_dump_master` in `../openglad-master`\n   (`cmake --build\
    \ --preset ci-test --target parity_dump_master`) before\n   continuing.\n2. Re-run\
    \ `cmake --build --preset ci-test && ctest --preset ci-test` and\n   ensure 0\
    \ failures.\n3. Update `.plan/parity-present-state.md`:\n   - Inventory: **156\
    \ total**, **154 SemanticParity**, **2 Invariant**.\n   - Golden file count: **155**.\n\
    \   - Per-category predicate-depth table: add rows for the new categories.\n \
    \    After the gap-fill the per-category counts are\n     `status_timer: 4, summon:\
    \ 3, generator: 5, weapon: 23, effect: 19,\n     input: 4, multiplayer: 1, level_transition:\
    \ 2, midcombat: 2`.\n     Recompute average predicate depth across 154 SemanticParity\
    \ rows.\n   - Trivially-true predicate floor section: must remain 0.\n   - Master-Golden\
    \ Missing Policy section: leave the `1` ADD_FAILURE\n     count, leave the `0`\
    \ GTEST_SKIP count.\n4. Update `.plan/parity-coverage-manifest.md`:\n   - Append\
    \ a `## Gap-Fill Scenarios Added in Coverage Pass 2` section\n     listing the\
    \ 22 new scenarios grouped by phase category.\n   - Update the Final Behavioral\
    \ Coverage Status Per Entity table.\n5. Commit the final docs.\n\n## Success Criteria\n\
    \n- 156 rows in `kScenarios[]`; 154 SemanticParity + 2 Invariant.\n- 156 `OG_PARITY_TEST(...)`\
    \ macros in\n  `tests/parity/test_parity_scenarios.cpp`, one per `kScenarios[]`\
    \ row.\n- 155 golden JSON files in `tests/parity/golden/`.\n- 7 depth/quality\
    \ gates pass (gate code is **not** modified).\n- `../openglad-master/tools/parity_scenario_table.h`\
    \ byte-mirrors the\n  branch table.\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\
    \ still carries\n  the prior `special_names` wiring.\n- `../openglad-master/build/ci-test/parity_dump_master`\
    \ is freshly built.\n- `tests/parity/scenario_runtime.cpp` still threads\n  `special_names_table`\
    \ (not modified).\n- `.plan/parity-present-state.md` and `.plan/parity-coverage-manifest.md`\n\
    \  document the 156-scenario state with the new \"Gap-Fill\" sections.\n\n## Git\
    \ Commit Requirement\n\nYou **must** commit the final-docs sweep before yielding.\
    \ If the master\nmirror needed a re-sync at step 1, also commit that mirror update\
    \ in the\nmaster worktree:\n\n- (Conditional)\n  `git -C ../openglad-master add\
    \ tools/parity_scenario_table.h && git -C ../openglad-master commit -m \"parity-companion:\
    \ final mirror resync\"` \u2014\n  only if `diff -q` at step 1 reported drift.\n\
    - Branch commit:\n  `git add tests/parity/scenario_table.h .plan/parity-present-state.md\
    \ .plan/parity-coverage-manifest.md && git commit -m \"parity-cov-2: final docs\
    \ refresh for 156-scenario suite\"`\n\nNo `--no-verify` or `--amend` unless flagged\
    \ as recovery. Do not yield\nuntil the branch commit lands (and the master commit\
    \ if required).\n"
- id: check-final-docs
  type: check
  prompt: "You are the **final-sweep reviewer**. Confirm the full 156-scenario suite\n\
    is green, totals match expected counts, every gate passes, harness wiring\nis\
    \ intact, and the docs reflect the new state.\n\n## Preexisting Inputs\n\n- `tests/parity/scenario_table.h`\n\
    - `tests/parity/test_parity_scenarios.cpp`\n- `tests/parity/scenario_runtime.cpp`\n\
    - `../openglad-master/tools/parity_scenario_table.h`\n- `../openglad-master/tools/parity_scenario_runtime.cpp`\n\
    - `../openglad-master/build/ci-test/parity_dump_master`\n- `.plan/parity-present-state.md`\n\
    - `.plan/parity-coverage-manifest.md`\n\n## What to Verify\n\n```bash\ncmake --build\
    \ --preset ci-test && ctest --preset ci-test\n```\n0 failures expected.\n\n```bash\n\
    ./build/ci-test/og_test_parity --gtest_filter='*'\n```\n0 failures expected across\
    \ all 156 scenarios + existing fixtures.\n\n```bash\ndiff -q tests/parity/scenario_table.h\
    \ ../openglad-master/tools/parity_scenario_table.h\n```\nMust return 0.\n\n```bash\n\
    test -x ../openglad-master/build/ci-test/parity_dump_master\n```\nMust succeed.\n\
    \n```bash\nls tests/parity/golden/*.json | wc -l\n```\nMust equal **155**.\n\n\
    All 22 new golden files must exist (verify with explicit `ls`):\n```bash\nls \\\
    \n  tests/parity/golden/enemy_freeze_mage_scen99.json \\\n  tests/parity/golden/invisibility_thief_scen99.json\
    \ \\\n  tests/parity/golden/speed_potion_movement_scen99.json \\\n  tests/parity/golden/invulnerable_potion_scen99.json\
    \ \\\n  tests/parity/golden/summon_lifetime_faerie_scen99.json \\\n  tests/parity/golden/summon_lifetime_decrement_faerie_scen99.json\
    \ \\\n  tests/parity/golden/generator_saturation_scen99.json \\\n  tests/parity/golden/weapon_rock_slot2_emit_scen99.json\
    \ \\\n  tests/parity/golden/weapon_boomerang_return_scen99.json \\\n  tests/parity/golden/weapon_exploding_boulder_scen99.json\
    \ \\\n  tests/parity/golden/effect_heartburst_multitarget_scen99.json \\\n  tests/parity/golden/effect_poison_cloud_emit_scen99.json\
    \ \\\n  tests/parity/golden/effect_protection_emit_scen99.json \\\n  tests/parity/golden/effect_bomb_timer_scen99.json\
    \ \\\n  tests/parity/golden/input_diagonal_movement_scen99.json \\\n  tests/parity/golden/input_hold_fire_search_scen99.json\
    \ \\\n  tests/parity/golden/input_switch_char_scen99.json \\\n  tests/parity/golden/input_special_switch_wrap_scen99.json\
    \ \\\n  tests/parity/golden/multiplayer_two_teams_scen99.json \\\n  tests/parity/golden/level_withdraw_scen99.json\
    \ \\\n  tests/parity/golden/midcombat_partial_hp_scen99.json \\\n  tests/parity/golden/consumable_inventory_state_scen99.json\n\
    ```\nAll must exist.\n\n```bash\ngrep -c 'EventKindAtLeast.*,\\s*0)' tests/parity/scenario_table.h\n\
    ```\nMust return 0.\n\n```bash\ngrep -c 'GTEST_SKIP() << \"master golden missing'\
    \ tests/parity/test_parity_scenarios.cpp\n```\nMust return 0.\n\n```bash\ngrep\
    \ -c 'ADD_FAILURE() << \"master golden missing' tests/parity/test_parity_scenarios.cpp\n\
    ```\nMust return 1.\n\n```bash\ngrep -c 'special_names_table' tests/parity/scenario_runtime.cpp\
    \ ../openglad-master/tools/parity_scenario_runtime.cpp\n```\nMust return \u2265\
    \ 2.\n\n```bash\ngrep -c '156 scenarios' .plan/parity-present-state.md\n```\n\
    Must return \u2265 1.\n\n```bash\ngrep -c 'Gap-Fill Scenarios' .plan/parity-coverage-manifest.md\n\
    ```\nMust return \u2265 1.\n\n## Verdict\n\nIf every check above succeeds, emit:\n\
    `VERDICT: PASS`\n\nOtherwise emit:\n`VERDICT: FAIL: <reason>`\n"
  bounce_target: impl-final-docs
- id: impl-final-docs~check-2
  type: check
  role: tester
  bounce_target: impl-final-docs
- id: impl-final-docs~check-3
  type: check
  role: senior-tester
  bounce_target: impl-final-docs
- id: impl-final-docs~check-4
  type: check
  role: senior-engineer
  bounce_target: impl-final-docs
- id: impl-final-docs~check-5
  type: check
  role: architect
  bounce_target: impl-final-docs
- id: impl-final-docs~check-6
  type: check
  role: pm
  bounce_target: impl-final-docs

````

## What has been tried (per-phase summary)

- `impl-walker-status-timers`: status=completed, attempts=5. Last failure: Checker agent crashed (exit 1). I'll verify this implementation thoroughly. Let me start by examining the changes. The implementer deviated from the literal spec (redesigned scenarios in commits a4ae4e3c/ca1af3e8). That's allowed if it serves the goal and passes. Let me verify the key success criteria. First, the mutation source byte-matches and golden files. All 4 mutation `from` lines match source byte-for-byte. Now the mirror requirement and master worktree. Mirror byte-identical, master worktree committed. No `branch_only`/`master_only` masking wrappers present — predicates kept real with 
- `check-walker-status-timers`: status=completed, attempts=5. Last failure: 
- `impl-walker-status-timers~check-2`: status=completed, attempts=5. Last failure: 
- `impl-walker-status-timers~check-3`: status=completed, attempts=3. Last failure: 
- `impl-walker-status-timers~check-4`: status=completed, attempts=2. Last failure: 
- `impl-walker-status-timers~check-5`: status=completed, attempts=1. Last failure: 
- `impl-walker-status-timers~check-6`: status=completed, attempts=1. Last failure: 
- `impl-summon-lifecycle`: status=running, attempts=25. Last failure: Implementation agent crashed (exit 1). API Error: Unable to connect to API (ConnectionRefused) API Error: Unable to connect to API (ConnectionRefused)

## Last implementer output (truncated)

````
API Error: Unable to connect to API (ConnectionRefused)
API Error: Unable to connect to API (ConnectionRefused)
````

## Last checker output (truncated)

````
/verifies mutations, and confirm the facts-generated regen is consistent:
The mutation canary gate (which passed in the full suite) validates file existence and line range; I've already manually confirmed all 4 `from` strings match source byte-for-byte. Let me do final validation on the goldens and confirm the facts-generated regen is committed/consistent:
All success criteria verified. Here's my assessment:

## Verification Summary

| Criterion | Status |
|---|---|
| 4 scenarios in `kScenarios[]` + paired `OG_PARITY_TEST` macros | ✅ All present |
| 4 goldens on disk, tracked, valid JSON | ✅ Verified |
| `scenario_table.h` byte-identical to master mirror | ✅ `diff` reports IDENTICAL |
| `cmake --build && ctest` → 0 failures | ✅ 37/37 tests passed; 4 new parity tests OK |
| Mutations match source byte-for-byte (incl. indentation) | ✅ All 4 `from` strings verified via `cat -A` against source lines 198/93/82/67 |
| Mutation canary discriminating-power gate | ✅ Passes (file exists, line in range) |
| Widened ranges carry `intended_diff`/`rng_drift`/`consequence` labels | ✅ Present on all widened predicates |
| Git commits in both worktrees, clean trees | ✅ Branch (3 commits) + master (3 mirror commits), both clean |

**Notes on deviations (not failures):** The implementer redesigned the scenario inputs/tick-budgets/facts relative to the literal task text (commits `a4ae4e3c` "redesign two timer scenarios for genuine mutation discrimination" and `ca1af3e8` "label widened HP ranges"). E.g. speed_potion uses tick budget 60 with a new `kInputsPotionWalk20` list and added a FAMILY_ORC, the freeze-archer position floor was relaxed to 190 with an `rng_drift` label, and HP ranges were widened with justification labels. These deviations were deliberate quality improvements to make mutations genuinely discriminate, the mutations still target the exact cited source lines, and all structural success criteria are met. The `scenario_facts_generated.json` regen is tracked and consistent (full suite passes any sync gate).

No placeholders/TODOs; rationale fields are populated and substantive; mirror commits land in the master worktree as required.

VERDICT: PASS
All success criteria verified. Here's my assessment:

## Verification Summary

| Criterion | Status |
|---|---|
| 4 scenarios in `kScenarios[]` + paired `OG_PARITY_TEST` macros | ✅ All present |
| 4 goldens on disk, tracked, valid JSON | ✅ Verified |
| `scenario_table.h` byte-identical to master mirror | ✅ `diff` reports IDENTICAL |
| `cmake --build && ctest` → 0 failures | ✅ 37/37 tests passed; 4 new parity tests OK |
| Mutations match source byte-for-byte (incl. indentation) | ✅ All 4 `from` strings verified via `cat -A` against source lines 198/93/82/67 |
| Mutation canary discriminating-power gate | ✅ Passes (file exists, line in range) |
| Widened ranges carry `intended_diff`/`rng_drift`/`consequence` labels | ✅ Present on all widened predicates |
| Git commits in both worktrees, clean trees | ✅ Branch (3 commits) + master (3 mirror commits), both clean |

**Notes on deviations (not failures):** The implementer redesigned the scenario inputs/tick-budgets/facts relative to the literal task text (commits `a4ae4e3c` "redesign two timer scenarios for genuine mutation discrimination" and `ca1af3e8` "label widened HP ranges"). E.g. speed_potion uses tick budget 60 with a new `kInputsPotionWalk20` list and added a FAMILY_ORC, the freeze-archer position floor was relaxed to 190 with an `rng_drift` label, and HP ranges were widened with justification labels. These deviations were deliberate quality improvements to make mutations genuinely discriminate, the mutations still target the exact cited source lines, and all structural success criteria are met. The `scenario_facts_generated.json` regen is tracked and consistent (full suite passes any sync gate).

No placeholders/TODOs; rationale fields are populated and substantive; mirror commits land in the master worktree as required.

VERDICT: PASS
````

## Constraints

- Reuse the same `backend` and `working_dir` as the current workflow unless changing them is part of the fix.
- Each phase must be a discrete, verifiable step with an agentic `check` phase as its verifier.
- If a checker should run tests, lint, build, or other commands, put those commands in the checker instructions.
- Order phases from setup/scaffolding to implementation to polish.
- Keep prompts specific and actionable.
