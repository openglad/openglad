# Master Companion — `parity_dump_master`

This document records the Phase 05 master-side companion that produces
schema-v1 parity dumps from `origin/master`. The companion lives in a sibling
git worktree (`../openglad-master`) on a local-only branch
(`parity-companion`); it is **never** pushed to `origin/master`.

## Companion location

- Worktree:      `/home/yans/code/openglad-master`
- Branch:        `parity-companion` (local-only, built on top of
  `parity-baseline-master` per `.plan/master-baseline.md`)
- Commit SHA:    `08f8ae4644956d1fbb30d8a2d6638dce277d1391`
  (pinned by parity-finish-3 phase 02 on 2026-05-16; matches
  `.plan/parity-coverage-manifest.md` frontmatter `master_companion_sha:`
  and embeds branch HEAD `e9ac53955119255672326610fa852e6f215519fc`)
- Parent (master baseline): `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`
  (= `origin/master` HEAD on 2026-05-12)
- Binary:        `../openglad-master/build/ci-test/parity_dump_master`

## Scenarios covered

The companion emits goldens for the 38 **master-comparable** scenarios from
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
smoke_nonempty_scen99
smoke_nonempty_scen99_inputs
family_soldier_scen99
family_elf_scen99
family_archer_scen99
family_mage_scen99
family_skeleton_scen99
family_cleric_scen99
family_fireelemental_scen99
family_faerie_scen99
family_slime_scen99
family_small_slime_scen99
family_medium_slime_scen99
family_thief_scen99
family_ghost_scen99
family_druid_scen99
family_orc_scen99
family_big_orc_scen99
family_barbarian_scen99
family_archmage_scen99
family_golem_scen99
family_giant_skeleton_scen99
family_tower1_scen99
```

Run `parity_dump_master --list` for the canonical, machine-readable list
(one id per line).

## Source files on the companion branch

| File                                           | Role |
|------------------------------------------------|------|
| `tools/parity_dump_master.cpp`                 | Entry point: parses `--scenario <id> --out <path>`, finds the spec, drives the tick loop, writes canonical JSON. |
| `tools/parity_dump_state.{h,cpp}`              | Schema-v1 emitter. Mirrors `tests/parity/state_dump.{h,cpp}` on the branch byte-for-byte for the fields master populates; differs in how it reads fields (master public members vs branch accessors). |
| `tools/parity_scenario_table.h`                | **Byte-for-byte** copy of `tests/parity/scenario_table.h`. Synchronisation contract is enforced via the SHA-1 below. |
| `tools/fact_predicate.h`                       | Verbatim copy of `tests/parity/fact_predicate.h`. Pulled in transitively through the mirrored `parity_scenario_table.h`. Master never reads through any of the declared symbols. |
| `tools/state_dump.h`                           | Single-line forwarder (`#include "parity_dump_state.h"`). Resolves the `#include "state_dump.h"` chain inside the mirrored `fact_predicate.h` to master's existing `StateDump` definition. |
| `tools/parity_bootstrap.{h,cpp}`               | Phase 02-redo addition. Initialises the headless data layer (PhysFS / palette / level loader hooks) so `parity_dump_master` can load `.fss` scenarios. |
| `tools/parity_scenario_runtime.{h,cpp}`        | Phase 02-redo addition. Routes scripted `InputEvent`s into the same `sim_process_player_input` pipeline the SDL loop uses; applies post-load spawn specs. |
| `tools/parity_dump_master_stubs.cpp`           | Minimal SDL-free stubs for the few symbols `og_gameplay` would otherwise pull from `og_interface` / `og_platform_sdl` (`walker::~walker`, `set_frame`, `find_follow_leader`, `og::runtime::active_session_reset_time`). |
| `src/resources/pixie_data.cpp` (vendored in)   | Provides `PixieData::valid()`, referenced by `walker_specials.cpp`. Compiled directly into the binary so we don't drag in `og_resources`. |
| `CMakeLists.txt` (modified)                    | Registers the `parity_dump_master` target at the bottom of the file, guarded on `NOT EMSCRIPTEN AND TARGET og_gameplay`. |

## Drift-detection SHA-1s

The branch's `tests/parity/scenario_table.h` and the companion's
`tools/parity_scenario_table.h` **must** remain byte-identical. Any change to
one requires the same edit to the other and a rebuilt master golden set.

| File on companion (`../openglad-master/`)      | SHA-1 (sha1sum)                                |
|------------------------------------------------|------------------------------------------------|
| `tools/parity_scenario_table.h`                | `1f95d0afa823e7caccf1973c16f28212a675f033`     |
| `tools/parity_dump_master.cpp`                 | `fd1f6b9604326eae096c4ffe733eaf03efa1c3ee`     |
| `tools/parity_dump_state.h`                    | `a9a944e8e6bb63ac9724bdd44f4a161bddc806ab`     |
| `tools/parity_dump_state.cpp`                  | `09c0a0c86e99d67ddd48843e6066e4d113967263`     |
| `tools/parity_dump_master_stubs.cpp`           | `9c69eca8181a48832cebd9672b87d503bd9b1453`     |
| `tools/fact_predicate.h`                       | `662486bce8643e93c984a01aa0a8661feccf47b6`     |
| `tools/state_dump.h`                           | `11272f58969024c65bdc966f47de683944d74da8`     |
| `tools/parity_bootstrap.cpp`                   | `eb27e81a927d85d57cf663ebbd6c1e867c0e8f72`     |
| `tools/parity_bootstrap.h`                     | `eaf10ffd2850866cfeb7bea42a712e9498719480`     |
| `tools/parity_scenario_runtime.cpp`            | `7aae94859c2a4e6955f207aa9e57d16053afaa18`     |
| `tools/parity_scenario_runtime.h`              | `3c1ece3d0f7b371826837d37907710a53fd8d0c5`     |

The companion-side `parity_scenario_table.h` SHA must equal the branch
`tests/parity/scenario_table.h` SHA verbatim — `1f95d0afa823e7caccf1973c16f28212a675f033`
matches on both sides today. SHA-1s above were captured during
parity-finish-3 phase 02 on companion HEAD
`08f8ae4644956d1fbb30d8a2d6638dce277d1391` (mirroring branch HEAD
`e9ac53955119255672326610fa852e6f215519fc`).

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
