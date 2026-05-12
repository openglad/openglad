# Master Companion — `parity_dump_master`

This document records the Phase 05 master-side companion that produces
schema-v1 parity dumps from `origin/master`. The companion lives in a sibling
git worktree (`../openglad-master`) on a local-only branch
(`parity-companion`); it is **never** pushed to `origin/master`.

## Companion location

- Worktree:      `/home/yans/code/openglad-master`
- Branch:        `parity-companion` (local-only, built on top of
  `parity-baseline-master` per `.plan/master-baseline.md`)
- Commit SHA:    `ce70d23286f1e8034284e7c718ec658065f525e5`
- Parent (master baseline): `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`
  (= `origin/master` HEAD on 2026-05-12)
- Binary:        `../openglad-master/build/ci-test/parity_dump_master`

## Scenarios covered

The companion emits goldens for the 15 **master-comparable** scenarios from
`tests/parity/scenario_table.h`. The branch-internal `snapshot_dirty_bits_scen9301`
scenario has no master golden by design (no dirty-bits infrastructure on
master); the companion refuses to dump it.

```
ai_idle_wander_scen9301
combat_attack_scen99
special_archmage_scen123
special_cleric_scen124
special_mage_scen126
special_thief_scen789
effect_bomb_lifetime_scen99
effect_chain_scen9410
summon_druid_pet_scen950
scoring_after_combat_scen99
save_roundtrip_scen99
exit_trigger_scen9302
tick_cadence_scen9301
rng_seed_stable_scen99
scripted_input_scen9301
```

Run `parity_dump_master --list` for the canonical, machine-readable list
(one id per line).

## Source files on the companion branch

| File                                           | Role |
|------------------------------------------------|------|
| `tools/parity_dump_master.cpp`                 | Entry point: parses `--scenario <id> --out <path>`, finds the spec, drives the tick loop, writes canonical JSON. |
| `tools/parity_dump_state.{h,cpp}`              | Schema-v1 emitter. Mirrors `tests/parity/state_dump.{h,cpp}` on the branch byte-for-byte; differs only in how it reads fields (master public members vs branch accessors). |
| `tools/parity_scenario_table.h`                | **Byte-for-byte** copy of `tests/parity/scenario_table.h`. Synchronisation contract is enforced via the SHA-1 below. |
| `tools/parity_dump_master_stubs.cpp`           | Minimal SDL-free stubs for the few symbols `og_gameplay` would otherwise pull from `og_interface` / `og_platform_sdl` (`walker::~walker`, `set_frame`, `find_follow_leader`, `og::runtime::active_session_reset_time`). |
| `src/resources/pixie_data.cpp` (vendored in)   | Provides `PixieData::valid()`, referenced by `walker_specials.cpp`. Compiled directly into the binary so we don't drag in `og_resources`. |
| `CMakeLists.txt` (modified)                    | Registers the `parity_dump_master` target at the bottom of the file, guarded on `NOT EMSCRIPTEN AND TARGET og_gameplay`. |

## Drift-detection SHA-1s

The branch's `tests/parity/scenario_table.h` and the companion's
`tools/parity_scenario_table.h` **must** remain byte-identical. Any change to
one requires the same edit to the other and a rebuilt master golden set.

| File on companion (`../openglad-master/`)      | SHA-1 (sha1sum)                                |
|------------------------------------------------|------------------------------------------------|
| `tools/parity_scenario_table.h`                | `4f8698d68ee446620752a96cbc980b71564b1f50`     |
| `tools/parity_dump_master.cpp`                 | `d4d6d8797aa512b87d85b9daebe3d05bd1f816b8`     |
| `tools/parity_dump_state.h`                    | `9fc7c48d94215fa9ec8265c06531c074f8b8844a`     |
| `tools/parity_dump_state.cpp`                  | `a484eb61bf195b71a63c6380dc3eccc0002f73f8`     |
| `tools/parity_dump_master_stubs.cpp`           | `0f07908c3cfdcc9b8a247c6119d83a922380452e`     |

The companion-side `parity_scenario_table.h` SHA must equal the branch
`tests/parity/scenario_table.h` SHA verbatim — `4f8698d6...` matches on both
sides today.

## RNG-seeding mechanism (literal)

Both sides seed deterministic state with the **same** source line, immediately
after constructing the world:

```cpp
world().rng_.state_ = scenario.rng_seed;
```

On master that resolves to `og::sim::SimRandom::state_` declared public at
`include/openglad/gameplay/game_world.h:43`. On the branch it resolves to the
same field in `include/openglad/gameplay/game_world.h` (also public). The
parity contract relies on these two TUs producing the same post-tick
`rng_state` for the same seed.

The companion implementation pattern (see `tools/parity_dump_master.cpp`):

```cpp
GameWorld world(spec.rng_seed);
world.rng_.state_ = spec.rng_seed;   // canonical seeding statement
GameplayContext ctx{ .world = &world, .sim_events = &events };
current_game = &ctx;
for (std::uint32_t t = 0; t < spec.tick_budget; ++t) world.tick();
```

The branch runner in `tests/parity/parity_runner.cpp` runs the same loop
against the same number of `world.tick()` invocations.

## How to rebuild

```bash
cd /home/yans/code/openglad-master
git checkout parity-companion
cmake --build --preset ci-test --target parity_dump_master
test -x build/ci-test/parity_dump_master && echo OK
```

If `parity-companion` does not yet exist locally (fresh clone):

```bash
git -C /home/yans/code/openglad fetch origin
git worktree add -b parity-baseline-master \
    /home/yans/code/openglad-master origin/master           # one-time
git -C /home/yans/code/openglad-master checkout \
    -b parity-companion parity-baseline-master              # one-time
# then apply the companion commit (cherry-pick or re-author from the diff
# captured in this document's SHA table)
cmake --preset ci-test
cmake --build --preset ci-test --target parity_dump_master
```

## How to capture goldens (Phase 06)

From the branch repo:

```bash
scripts/parity/capture_master_golden.sh          # all 15 scenarios
scripts/parity/capture_master_golden.sh <id>...  # one or more by id
```

The script invokes `parity_dump_master` once per scenario, writes the dump
to `tests/parity/golden/<id>.json`, and pipes the output through
`scripts/parity/validate_schema.py`. Capture fails fast on any schema
violation.

## Smoke test recorded during Phase 05

Three scenarios were captured and validated end-to-end at the close of
Phase 05:

| Scenario                          | Validator result |
|-----------------------------------|------------------|
| `ai_idle_wander_scen9301`         | OK               |
| `combat_attack_scen99`            | OK               |
| `rng_seed_stable_scen99`          | OK               |

Each dump terminated with one trailing newline, sorted top-level keys
(`effects`, `events`, `rng_state`, `schema_version`, `score_per_team`,
`tick`, `walkers`), and `schema_version == "v1"`. The captured smoke
files were deleted; Phase 06 is the phase that commits the canonical
golden set.

## Caveats / known scope limits

1. Phase 05 mirrors the **Phase 04 skeleton** on the branch side: neither
   the branch runner nor the master companion currently loads the scenario
   file referenced by `ScenarioSpec::scenario_file`. Both exercise an empty
   world for the requested number of ticks. This is intentional — Phase 06
   wires both sides to the loader simultaneously, so the captured goldens
   stay in lockstep.
2. Master's `GameWorld::tick()` early-returns when `current_game->sim_events`
   is null; the companion installs a transient `GameplayContext` for the
   duration of the run and restores the previous `current_game` before
   writing the dump.
3. On an empty world, `GameWorld::tick()` sets `level_done = 2` (no foes
   found), which the runner treats as an early stop. This matches the
   branch's behaviour for the same input and is captured in the dump's
   `tick` field (always `1` for tick_budgets > 0 on an empty world).
4. `EventKind::DamageTile = 14` exists on master but not on the branch
   enum. The companion's `event_kind_symbol()` deliberately does not handle
   it: any DamageTile event would serialise as `"kind_14"`, matching the
   string the branch dumper would emit for an unknown enum value. The
   branch-side dumper does the same fall-through, so the output is
   byte-identical for that path.

## Non-goals (deferred to later phases)

- Loading `.fss` scenario files in either runner — Phase 06.
- Walking the scripted `InputEvent` table into `world.input_state_` —
  Phase 06.
- Wiring the master companion through CTest (it is a manual / scripted
  tool, not a test entry) — out of scope; Phase 06 is the consumer.
- Promoting any flagged HP / max_hp divergence to `intended_diff` —
  Phase 06 strict classification.
