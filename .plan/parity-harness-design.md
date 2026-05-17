# Gameplay Parity Harness — Design (Schema v1)

This document is the implementation contract for the gameplay parity harness.
It is consumed verbatim by Phase 04 (branch-side scaffolding) and Phase 05
(master companion). All decisions below are committed; no later phase may
change schema v1, the canonical tick definition, or the scenario id set
without revising this document first.

Inputs already on disk (consumed in place):

- `.plan/parity-risk-inventory.md` — 12 subsystems, 12 probes.
- `.plan/master-baseline.md` — master worktree at `../openglad-master`
  (`parity-baseline-master`), `ci-test` green on master.
- Branch headers: `include/openglad/gameplay/{game_world.h,sim_entity.h,
  world_snapshot.h}`.
- Master headers: `../openglad-master/include/openglad/gameplay/{game_world.h,
  sim_entity.h,walker.h}` and `../openglad-master/tests/test_game_world_fixture.h`.

## Scenario list

Fifteen scenarios. Every `scenario file` path exists on **both** the branch
(`/home/yans/code/openglad`) and the master worktree
(`/home/yans/code/openglad-master`); verified by `test -f` against both trees
on 2026-05-12. `input script` is a flat sequence of
`(tick, player_id, key_mask)` tuples embedded as a `constexpr` literal in
`tests/parity/scenario_table.h` (see Phase 04). `tick_budget` is the number
of `og::sim::GameWorld::tick()` invocations the runner performs before
dumping. Save-roundtrip is in-process but still names a scenario file so the
checker's `test -f` rule holds.

| id | scenario file (full path) | RNG seed | input script | duration (ticks) | what it exercises |
|---|---|---:|---|---:|---|
| `ai_idle_wander_scen9301` | `scen/scen9301.fss` | `0x00000001` | none (empty list) | 300 | Subsystem 1 — walker AI and movement: wandering NPCs, pathfinding decisions, wall-collision behaviour. |
| `combat_attack_scen99` | `temp/scen/scen99.fss` | `0x00000042` | `[(5,0,K_ATTACK)..(64,0,K_ATTACK)]` (hold attack tick 5–64) | 200 | Subsystem 2 — combat math, damage roll, defense, level-up curve. |
| `special_archmage_scen123` | `temp/scen/scen123.fss` | `0x0000F00D` | `[(20,0,K_SPECIAL)]` | 200 | Subsystem 3 — archmage special (per `family_archmage.cpp`, 227-line delta). |
| `special_cleric_scen124` | `temp/scen/scen124.fss` | `0x0000F00D` | `[(20,0,K_SPECIAL)]` | 200 | Subsystem 3 — cleric heal special (`family_cleric.cpp`, 136-line delta). |
| `special_mage_scen126` | `temp/scen/scen126.fss` | `0x0000F00D` | `[(20,0,K_SPECIAL)]` | 200 | Subsystem 3 — mage rock cast (`family_mage.cpp`, 136-line delta). |
| `special_thief_scen789` | `temp/scen/scen789.fss` | `0x0000F00D` | `[(20,0,K_SPECIAL)]` | 200 | Subsystem 3 — thief teleport / backstab (`family_thief.cpp`, 86-line delta). |
| `effect_bomb_lifetime_scen99` | `temp/scen/scen99.fss` | `0x0000BEEF` | `[(10,0,K_SPECIAL)]` (bomb-thrower team) | 60 | Subsystem 4 — bomb effect lifetime, position, damage radius. |
| `effect_chain_scen9410` | `temp/scen/scen9410.fss` | `0x0000BEEF` | `[(15,0,K_SPECIAL)]` | 100 | Subsystem 4 — chain-lightning hop count and per-target damage. |
| `summon_druid_pet_scen950` | `temp/scen/scen950.fss` | `0x0000CAFE` | `[(8,0,K_SPECIAL)]` | 80 | Subsystem 5 — druid summon: pet appears, inherits `real_team_num`. |
| `scoring_after_combat_scen99` | `temp/scen/scen99.fss` | `0x00000042` | `[(5,0,K_ATTACK)..(64,0,K_ATTACK)]` | 200 | Subsystem 6 — `score_per_team[]` after the combat probe (shares branch run with `combat_attack_scen99`, asserts on a different field). |
| `save_roundtrip_scen99` | `temp/scen/scen99.fss` | `0x00000123` | none | 1 | Subsystem 7 — load scenario, serialize `save_data` via branch `save_data::write` into `std::stringstream`, deserialize, compare to master companion serialization. In-process; one tick before dump just to materialise walkers. |
| `exit_trigger_scen9302` | `scen/scen9302.fss` | `0x00000007` | `[(0,0,K_RIGHT)..(420,0,K_RIGHT)]` (walk to exit tile) | 600 (or until `level_done`) | Subsystem 8 — exit trigger fires, `level_done == 1`, `next_level` set, `events[]` contains `level_exited`. |
| `tick_cadence_scen9301` | `scen/scen9301.fss` | `0x00000001` | none | 600 | Subsystem 9 — `tick_count_ == 600` after 600 invocations; byte-equal walker positions vs master. Stresses the cadence contract directly. |
| `rng_seed_stable_scen99` | `temp/scen/scen99.fss` | `0x00000001` | none | 1 | Subsystem 10 — seeded `rng_.state_ = 1` round-trips to a known post-tick value on both branches. Detects spurious `state_` mutation by `world_snapshot.cpp` / `dirty_field_bits.h` setup. |
| `scripted_input_scen9301` | `scen/scen9301.fss` | `0x00000010` | `[(0,0,K_UP),(20,0,0),(40,0,K_RIGHT),(60,0,0),(80,0,K_ATTACK),(100,0,0)]` | 200 | Subsystem 11 — fixed input script reaches the simulation at the same tick on both branches (the branch path runs through `local_transport_shadow`; the master path applies directly to `world.input_state_`). |

Branch-internal companion (no master golden, but runs in the same test
binary):

| id | scenario file | RNG seed | input | duration | exercises |
|---|---|---:|---|---:|---|
| `snapshot_dirty_bits_scen9301` | `scen/scen9301.fss` | `0x00000055` | none | 50 | Subsystem 12 — branch-only: dump the world twice (once via the dirty-tracked server mirror, once via direct iteration of `world.walkers`); the two dumps must be byte-equal. Fails locally if a setter forgets `mark_dirty(...)`. Not compared against master. |

That is 15 master-comparable scenarios plus 1 branch-internal companion =
**15 + 1 = 16 GoogleTest entries** under `og_test_parity`. The
master-comparable count of 15 is the number used by Phase 04's
`ctest --preset ci-test -N` golden-list assertion and by Phase 06's
`ls tests/parity/golden/*.json | wc -l` check.

`K_ATTACK`, `K_SPECIAL`, `K_UP`, `K_RIGHT` are the existing keymap masks
from `include/openglad/input/input_state.h`; the harness uses them as
`uint32_t` constants without referring to SDL key codes.

## State dump schema v1

The dumper emits **exactly one** UTF-8 JSON object per invocation, written
with sorted keys, no trailing whitespace, LF line endings, and a single
terminating newline. The canonical emitter lives in
`tests/parity/state_dump.cpp` on the branch and
`../openglad-master/tools/parity_dump_state.cpp` on the master companion.
Both TUs produce **byte-identical** output for an equivalent world state.

Top-level keys (sorted lexicographically when serialised):

```json
{
  "effects": [ /* see below */ ],
  "events": [ /* see below */ ],
  "rng_state": "0x00000001",
  "schema_version": "v1",
  "score_per_team": [0, 0, 0, 0],
  "tick": 0,
  "walkers": [ /* see below */ ]
}
```

Field rules:

- `schema_version` — fixed string literal `"v1"`. Mismatch is a hard error.
- `tick` — unsigned 32-bit integer, monotonic, equals `world.tick_count_`
  at dump time.
- `rng_state` — `og::sim::SimRandom::state_` formatted with
  `printf("0x%08X", state_)`. Both branches expose the field publicly
  (`include/openglad/gameplay/game_world.h:50` on the branch;
  same struct in `../openglad-master/include/openglad/gameplay/game_world.h`),
  so the value is always observable. The literal `"unobservable"` is only
  emitted if a future build hides `state_`; the harness fails fast if it sees
  that on either side and the scenario is not in `## Determinism fallback`
  mode.
- `walkers[]` — every entry alive **or** dead-but-not-yet-removed, sorted
  by `(team, id)` ascending. Each row:
  ```json
  {
    "id": 17,
    "family": "FAMILY_SOLDIER",
    "team": 0,
    "xpos": 96,
    "ypos": 128,
    "hp": 12.000000,
    "max_hp": 12.000000,
    "ammo": 3,
    "alive": true
  }
  ```
  - `id` — `walker::serial_number` (existing on both branches).
  - `family` — symbolic string from `og::FamilyName(walker->family())` /
    `og::FamilyName(walker->family)` (branch / master). The mapping table
    lives in `tests/parity/state_dump.cpp` and is byte-identical to the one
    in the master companion. If a family appears only on one side, the
    string `"FAMILY_UNKNOWN_<int>"` is emitted; that becomes an
    informational diff per `## Comparison rules`.
  - `team` — `walker->team_num()` (branch) / `walker->team_num` (master).
  - `xpos`, `ypos` — `int32` tile-space; accessor on branch, public on
    master.
  - `hp`, `max_hp` — `walker->stats()->hitpoints` and
    `walker->stats()->max_hitpoints`. Both fields are `float` on both
    branches; formatted with `"%.6f"`.
  - `ammo` — `walker->stats()->ammo` (int).
  - `alive` — `walker->dead == 0` (branch and master both have public
    `dead`).
- `effects[]` — entries from `world.effects` (a `std::list<std::unique_ptr<
  walker>>` of effect-type walkers on both branches), sorted by
  `(family, id)` ascending. Each row:
  ```json
  {
    "id": 42,
    "family": "FAMILY_EFFECT_BOMB",
    "xpos": 96,
    "ypos": 128,
    "lifetime": 14
  }
  ```
  - `lifetime` is `walker->stats()->lifetime` (existing on both branches).
- `score_per_team[]` — exactly 4 entries (indices 0–3), unsigned 32-bit
  integers from `world.m_score[]`.
- `events[]` — emitted in **tick order**, then in **insertion order** within
  a tick. The dumper subscribes to a per-scenario event recorder
  (`tests/parity/state_dump.cpp` installs the branch recorder via the
  existing `sim_event_log.h` stub which is present on both branches at
  `include/openglad/gameplay/sim_event_log.h`). Allowed `kind` values:
  - `"walker_died"` — `{ "kind": "walker_died", "tick": 42, "id": 17 }`
  - `"walker_attacked"` — `{ "kind": "walker_attacked", "tick": 42,
    "attacker": 17, "victim": 18, "damage": 3 }`
  - `"level_exited"` — `{ "kind": "level_exited", "tick": 420,
    "next_level": 9303 }`
  - `"treasure_collected"` — `{ "kind": "treasure_collected", "tick": 50,
    "walker": 17, "treasure_family": "FAMILY_GOLD" }`

Canonical ordering rules (the JSON serialiser **must** honour these even
when the underlying container's iteration order differs):

1. Object keys: sorted lexicographically per JSON level.
2. `walkers[]`: ascending by `team`, ties broken by ascending `id`.
3. `effects[]`: ascending by `family` (symbolic string compare), ties
   broken by ascending `id`.
4. `events[]`: ascending by `tick`, ties broken by insertion order (the
   recorder maintains a per-tick sequence counter).

Floats: `printf("%.6f", value)`. Trailing zeros are kept (`12.000000`, not
`12`). No `-0.000000`: emitter normalises negative zero to positive zero
before formatting.

## Comparison rules

For each scenario in non-fallback mode the differ in
`scripts/parity/diff_dumps.py` does the following:

1. **Schema check.** `schema_version` must equal `"v1"` on both sides. Any
   other value is a fatal harness error (not a regression).
2. **Tick check.** `tick` on both sides must equal the scenario's
   `tick_budget`. Mismatch fails the test with `cadence_mismatch`; this is
   the canary for Subsystem 9.
3. **RNG check.** If neither side declared `## Determinism fallback`,
   `rng_state` must match exactly. Mismatch → `rng_drift` regression.
4. **Numeric integer fields** (`id`, `team`, `xpos`, `ypos`, `ammo`,
   `score_per_team[i]`, event `tick` / `next_level` / `damage`): exact
   integer equality. No tolerance.
5. **HP / max_hp.** Both are `float` on both branches. The differ requires
   exact `%.6f` string equality. Any drift — even at the ULP level — is
   flagged as a regression candidate per Phase 06's strict rule and is
   **never silently absorbed**. Phase 06 may move a flagged HP diff to
   `intended_diff` only with a cited branch commit; until then it counts
   as `regression`.
6. **Identity fallback.** If `walker.id` values diverge between sides (e.g.
   the branch's `guy_id_counter` reset path differs from master), the
   differ falls back to identifying walkers by
   `(family, team, initial_xpos, initial_ypos)`. `initial_xpos` /
   `initial_ypos` are recorded by the runner from the **dump at tick 0**
   and propagated to the per-scenario comparison context. Effects use the
   same fallback keyed on `(family, initial_xpos, initial_ypos)`.
7. **Branch-only fields.** If a future schema-v1.1 adds a field that exists
   only on the branch dumper (e.g., a `dirty_bits` summary), the differ
   emits it as an **informational diff** (logged but does not fail the
   test). Schema-v1 has no such fields; this rule is forward-compatible
   only.
8. **Event ordering.** A regression on the same `tick` with a different
   per-tick insertion order is still a regression — the recorders on both
   sides must agree on insertion order. Phase 04 documents the exact
   subscription points so that order is deterministic.

The differ emits a structured human-readable report on mismatch
(`scripts/parity/diff_dumps.py`), exit code 1, and a machine-readable
summary on `stdout` line 1 in the form
`SCENARIO <id> FAIL <category> <field>`. GoogleTest assertion messages
include the first three categorised differences.

## Determinism fallback

Each scenario is one of:

- **byte-equal** (default): the harness asserts byte-equal canonical JSON
  between branch and master golden.
- **invariant**: the scenario declares an `invariants:` list of Boolean
  predicates over the final dump. The harness then runs both sides, dumps
  both, and asserts every predicate holds on **both** sides. The master
  golden is still captured (so future regressions are visible), but the
  GoogleTest assertion is `EXPECT(invariant.holds(branch_dump) &&
  invariant.holds(master_dump))`, not a byte-equal compare.

`## Tick-cadence parity contract` (below) commits to byte-equal cadence,
so the default mode is **byte-equal** for every scenario. Each scenario
nonetheless declares a `byte_equal_feasible:` flag plus the invariants it
would fall back to, so a single discovered cadence problem during Phase 06
flips the affected scenario into fallback mode without rewriting the test.

Per-scenario classification:

| id | byte_equal_feasible | fallback invariants |
|---|---|---|
| `ai_idle_wander_scen9301` | yes | `count(walkers, alive) >= 1`; `walkers[0].xpos in [0..LEVEL_W]` |
| `combat_attack_scen99` | yes | `sum(walkers.hp) < initial_sum_hp`; `any(events.kind == "walker_attacked")` |
| `special_archmage_scen123` | yes | `any(effects.family == "FAMILY_EFFECT_BOMB" or "FAMILY_WAVE")` after tick 20 |
| `special_cleric_scen124` | yes | `walkers[caster].hp >= pre_special_hp` (heal applied) |
| `special_mage_scen126` | yes | `any(effects.family startswith "FAMILY_WEAPON")` after tick 20 |
| `special_thief_scen789` | yes | `walkers[caster].xpos != pre_special_xpos` (teleport / step-back) |
| `effect_bomb_lifetime_scen99` | yes | `count(effects, family == "FAMILY_EFFECT_BOMB") <= 1`; `effect.lifetime decreases monotonically` |
| `effect_chain_scen9410` | yes | `count(events.kind == "walker_attacked") >= 2` |
| `summon_druid_pet_scen950` | yes | `count(walkers, team == caster.team) > pre_summon_count`; `pet.real_team_num == caster.real_team_num` |
| `scoring_after_combat_scen99` | yes | `score_per_team[winning_team] > 0` |
| `save_roundtrip_scen99` | yes | `sha1(branch_save_blob) == sha1(master_save_blob)` |
| `exit_trigger_scen9302` | yes | `level_done == 1`; `any(events.kind == "level_exited")`; `next_level == 9303` |
| `tick_cadence_scen9301` | yes | `tick == 600`; walker count unchanged from tick 0 |
| `rng_seed_stable_scen99` | yes | branch `rng_state` after 1 tick == master `rng_state` after 1 tick |
| `scripted_input_scen9301` | yes | `walkers[0].xpos` and `ypos` move monotonically with the script |

Every row is `byte_equal_feasible: yes` at design time. The contract below
commits to the cadence that makes this true. Phase 06 may flip any row to
fallback mode after empirical investigation, in which case the invariants
column is what the test will actually assert.

## Tick-cadence parity contract

**Canonical definition:** one tick = one invocation of
`og::sim::GameWorld::tick()`.

- On the branch: declared at `include/openglad/gameplay/game_world.h:208`
  (`void tick();`). Verified by reading the header at design time.
- On master: declared at
  `../openglad-master/include/openglad/gameplay/game_world.h:123`
  (`void tick();`). Same signature, same return type, same namespace.

`walker::act()` is invoked **from inside** `GameWorld::tick()` on both
branches (the branch implementation lives in `src/gameplay/game_world.cpp`,
the master implementation in `../openglad-master/src/gameplay/game_world.cpp`).
The harness does **not** call `walker::act()` directly; it does **not**
emulate master's pre-refactor `world().timer_wait`-driven per-walker loop.
Both sides drive the simulation through the same single entry point.

Enforcement loop, identical on both sides:

```cpp
seed(world);                              // world.rng_.state_ = spec.rng_seed
apply_initial_input(world, spec);         // first tuple at tick 0 if any
StateDump tick0 = capture(world);         // initial-position fallback context
for (std::uint32_t t = 0; t < spec.tick_budget; ++t) {
    apply_scripted_input(world, spec, t); // (tick, player_id, key_mask)
    world.tick();                         // SINGLE canonical step
}
StateDump dump = capture(world);
```

Fail-fast cadence checks:

1. After the loop, `world.tick_count_` must equal `spec.tick_budget`.
   Mismatch (e.g., because `tick()` early-returns when paused) is a fatal
   harness error, not a parity diff; both runners assert
   `ASSERT_EQ(world.tick_count_, spec.tick_budget)` before emitting the
   dump.
2. If `world.level_done == 1` before the budget is exhausted, the runner
   stops the loop at that point, records the early-stop tick, and the
   differ requires both sides to have stopped at the **same** tick. Early
   stop at differing ticks is a `cadence_mismatch` regression.
3. The branch test binary does not interpose any
   `local_transport_shadow` or `FrameDeadlinePacer` layer in the parity
   runner — input is written directly into `world.input_state_` to remove
   transport latency from the parity equation. The
   `scripted_input_scen9301` scenario adds a second branch-only variant
   that routes through `local_transport_shadow` and asserts the same
   dump; that is the Subsystem 11 probe.
4. `tick_budget` is a `std::uint32_t`. No floating timer, no real-time
   sleep, no `SDL_Delay`. The runner never yields between `tick()` calls.

## Test groups to add

Two CMake test groups; both registered via macros that already exist in
the branch's top-level `CMakeLists.txt`.

| target | macro | source files | description |
|---|---|---|---|
| `og_test_parity` | `og_add_test_group(og_test_parity ...)` | `tests/parity/parity_runner.{h,cpp}`, `tests/parity/state_dump.{h,cpp}`, `tests/parity/scenario_table.h`, `tests/parity/parity_test_main.cpp`, `tests/parity/test_parity_scenarios.cpp` | SDL integration group. Loads scenarios via the same I/O path as the rest of the game; required because `gloader.cpp` uses PhysFS which the unit-group main does not init. |
| `og_unit_parity` (optional) | `og_add_unit_group(og_unit_parity ...)` | `tests/parity/unit/test_canonical_json.cpp` (Phase 04 may add) | Headless: verifies the canonical JSON emitter is deterministic on a fixed in-memory `StateDump` (sorting, float formatting, key order). No SDL, no scenario load. Optional — Phase 04 may choose to fold these checks into `og_unit_data` if the unit group's link surface conflicts. |

Phase 04's `04a-check-build` lists the `og_test_parity` group only; the
unit group is treated as a nice-to-have.

## Files to add/modify

**New (branch repo `/home/yans/code/openglad`):**

- `tests/parity/parity_runner.h`
- `tests/parity/parity_runner.cpp`
- `tests/parity/state_dump.h`
- `tests/parity/state_dump.cpp`
- `tests/parity/parity_test_main.cpp`
- `tests/parity/test_parity_scenarios.cpp`
- `tests/parity/scenario_table.h` — single source of truth for
  `ScenarioSpec` values; copied byte-for-byte into the master companion
  in Phase 05. Header comment states the synchronisation rule
  (`// SYNCHRONIZE WITH ../openglad-master/tools/parity_scenario_table.h`).
- `tests/parity/golden/.gitkeep`
- `tests/parity/golden/<scenario_id>.json` — placeholders (committed in
  Phase 06).
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/run_parity_diff.sh`
- `scripts/parity/diff_dumps.py`
- `scripts/parity/validate_schema.py` (added in Phase 05, listed here for
  completeness).

**New (master worktree `/home/yans/code/openglad-master`, committed on
branch `parity-companion` only — never on `origin/master`):**

- `tools/parity_dump_master.cpp`
- `tools/parity_dump_state.cpp`
- `tools/parity_dump_state.h`
- `tools/parity_scenario_table.h` — byte-for-byte copy of
  `tests/parity/scenario_table.h`.
- A CMake target `parity_dump_master` registered in
  `../openglad-master/CMakeLists.txt` that builds the four `tools/` files
  into a standalone executable at
  `../openglad-master/build/ci-test/parity_dump_master`.
- `.plan/master-companion.md` (on the branch repo, not master) — documents
  how to apply / rebuild the companion.

**Modify (branch):**

- `CMakeLists.txt` — register `og_test_parity` (and optionally
  `og_unit_parity`) following the existing `og_add_test_group(og_test_*)`
  calls.
- `.gitignore` — already exempts `.plan/logs/` from Phase 02; no further
  change required.

**Modify (master worktree, branch `parity-companion` only):**

- `../openglad-master/CMakeLists.txt` — register the `parity_dump_master`
  target.

**Read-only / untouched:**

- Everything under `.plan/` other than the new design / companion /
  divergence / fixes / signoff documents listed in `plan.md` §4.
- Every file under `src/`, `include/` on both branches — touched in
  Phase 07 only if a regression demands it.
- All residue listed in `plan.md` §1.

## Coverage matrix

Every subsystem in `.plan/parity-risk-inventory.md` `## Subsystems at risk`
is covered. No "Subsystems not covered" section is required: the inventory
listed 12 subsystems and every one of them appears below at least once.

| Phase 01 subsystem | covering scenario(s) |
|---|---|
| 1. Walker AI and movement | `ai_idle_wander_scen9301`, `tick_cadence_scen9301`, `scripted_input_scen9301` |
| 2. Combat math and damage | `combat_attack_scen99` |
| 3. Special abilities per family | `special_archmage_scen123`, `special_cleric_scen124`, `special_mage_scen126`, `special_thief_scen789` |
| 4. Effect lifecycle | `effect_bomb_lifetime_scen99`, `effect_chain_scen9410` |
| 5. Summon and pet behavior | `summon_druid_pet_scen950` |
| 6. Scoring and team statistics | `scoring_after_combat_scen99` |
| 7. Save format read / write | `save_roundtrip_scen99` |
| 8. Scenario load and exit-trigger firing | `exit_trigger_scen9302` |
| 9. Tick cadence (sim-vs-render decoupling) | `tick_cadence_scen9301` (primary); every other scenario verifies cadence indirectly via the `tick == tick_budget` rule in `## Comparison rules` |
| 10. RNG seeding and determinism | `rng_seed_stable_scen99` (primary); every scenario relies on `rng_state` byte-match per `## Comparison rules` |
| 11. Per-frame transport shadow (single-player path) | `scripted_input_scen9301` (variant routed through `local_transport_shadow` on the branch side; see `## Tick-cadence parity contract` item 3) |
| 12. World snapshot + dirty-field bookkeeping | `snapshot_dirty_bits_scen9301` (branch-internal; no master golden, asserted on the branch side only) |

Family-coverage caveat: the four `special_*` scenarios exercise four of
the fifteen family files (`archmage`, `cleric`, `mage`, `thief`). The
remaining eleven families (`archer`, `druid` direct cast, `elf`,
`soldier`, `slime`, `barbarian`, `orc`, `skeleton`, `ghost`,
`fire_elemental`, `druid` summon — partially via `summon_druid_pet_scen950`)
are exercised **indirectly** by walker AI in the other scenarios (any
family walker spawned by a scenario's `.fss` runs its standard tick path,
which catches movement / aggression / death regressions even without a
family-specific special-cast probe). Specials for the eleven uncovered
families are listed under `## Subsystems with partial parity coverage`
below and flagged for `.plan/parity-signoff.md` `## Open risks` in
Phase 08.

### Subsystems with partial parity coverage

These rows are **not** "not covered" — they have at least one parity
probe — but they have a known gap that Phase 08 must document so a future
contributor knows what manual QA still buys us.

| subsystem | gap | proposed manual QA |
|---|---|---|
| 3. Special abilities (per family) | Specials for `archer`, `druid` (direct cast), `elf`, `soldier`, `slime`, `barbarian`, `orc`, `skeleton`, `ghost`, `fire_elemental` are not individually scenario-probed. | Manual run of `temp/scen/scen9421.fss` and `temp/scen/scen9450.fss` (existing multi-family arenas) with each family selected from the picker, eyeballing the special effect. |
| 4. Effect lifecycle | Door-open and ghost-scare effects (`effect_family_door_open`, `effect_family_ghost_scare`) are not individually probed; covered only as side effects of family specials. | Manual run of `scen/scen9303.fss` (door / portal heavy) and `scen/scen9304.fss` (ghost-heavy). |
| 7. Save format | Round-trip is in-process; no real `.glad`-archive save written to disk and re-opened. | Manual `Continue` from a fresh `Save Game` via the picker, branch + master, byte-compare the on-disk save blob. |

These rows do not block parity sign-off; they are inputs to Phase 08's
`## Open risks`.

## Synchronisation rules between branch and master companion

Phase 05's master companion is a separate compilation but the **same**
schema, scenario table, and canonical JSON. The contract:

1. `tests/parity/scenario_table.h` (branch) and
   `../openglad-master/tools/parity_scenario_table.h` (companion) must be
   byte-identical. Phase 05 enforces this via a header SHA-1 check
   recorded in `.plan/master-companion.md`.
2. RNG seeding statement on both sides is the **same source code**:
   `world.rng_.state_ = spec.rng_seed;` (or `world().rng_.state_ =
   spec.rng_seed;` if a `GameWorld&` accessor is preferred — both branches
   provide both forms). No `srand()`, no setter, no cfg key.
3. The canonical JSON emitter logic in `state_dump.cpp` (branch) and
   `tools/parity_dump_state.cpp` (companion) is a hand-rolled
   sorted-key / `%.6f`-float emitter; both TUs must produce the same
   output character-for-character. Branch and companion differ only in
   how they **read** fields — branch uses accessors
   (`walker->xpos()`, `walker->team_num()`, `walker->family()`), master
   uses public fields (`walker->xpos`, `walker->team_num`,
   `walker->family`). Both call `walker->stats()->hitpoints` (the
   `stats()` accessor exists on both branches).
4. Event recorders: branch installs its recorder through the existing
   `sim_event_log.h` stub; master installs an equivalent recorder via the
   same stub (the file already exists on master per
   `.plan/master-baseline.md`). Insertion-order numbering uses a single
   monotonic `uint32_t` per scenario run on both sides.

These four rules turn schema v1 into an unambiguous shared contract;
any breakage in Phase 06 is by definition a real divergence, not a
harness bug.

## Phase 02 redo: load path

Phase 01's audit (`.plan/parity-redo-audit.md`) showed the earlier runner
exercised an empty `GameWorld` and short-circuited on tick 1. Phase 02
replaces that with a real load / tick / inject pipeline driven through
`LevelRuntimeData`:

1. `LevelRuntimeData level(level_id, /*headless=*/true, &hooks)` —
   branch uses `sdl_level_data_hooks()`, master companion uses
   `headless_level_data_hooks()` (with a stub `sdl_level_data_hooks`
   that forwards to the headless table so the byte-for-byte mirror of
   `parity_bootstrap.cpp` links). `level_id` is parsed from
   `spec.scenario_file` via `scenario_runtime::scenario_level_id`.
2. Per-test `ScopedGameplayContext` installs `current_game` for the
   duration of `level.load()` and the tick loop. The runner references
   `extern cfg_store cfg;` (the file-scope `cfg` from
   `include/openglad/resources/gparser.h:38`) — it does **not**
   default-construct a local `cfg_store`.
3. `level.load()` is called. It reads the RNG during decoration, so the
   canonical `spec.rng_seed` is re-applied to `world.rng_.state_`
   **after** `load()` returns. Both sides do this in the same order so
   the tick loop on both branches starts from the same seed.
4. `fresh_arena` scenarios call `clear_world_entities(world)` (which
   delegates to `GameWorld::delete_objects`) so the loaded scen's
   walkers are dropped before the scripted spawns are applied.
5. `apply_post_load_spawns(world, spec)` iterates `spec.spawns[]` and
   calls `world.add_ob(static_cast<Order>(spec.order), spec.family,
   /*atstart=*/true)` for each, then `walker::setxy(x, y)` (which
   updates the spatial index, unlike a raw `set_xpos`/`set_ypos`),
   `set_team_num` / `set_real_team_num`, and optional
   `set_default_weapon` / `set_current_weapon`. The walker's `user_`
   field is **left at the SimEntity default of -1** (NPC); the input
   pipeline's own takeover logic (see step 7) assigns
   `user_ = player_num` exactly the way production code does it when a
   player picks up control.
6. The tick loop runs the full `spec.tick_budget` invocations. It does
   **not** `break` on `world.level_done` — it records the early-stop
   tick in `RunOutcome::early_stop_tick` and lets the loop run, so
   cadence comparisons stay apples-to-apples on both sides. Schema v1's
   new `level_done` and `level_tick_count` fields record the early-stop
   signal in the dump itself.
7. `apply_inputs_at_tick(world, spec, tick, driver, sim_events)` routes
   scripted inputs through the **real player-input pipeline** —
   specifically `sim_process_player_input(...)` declared in
   `include/openglad/gameplay/sim_input_handler.h:60`, which is the
   same call the SDL game loop's per-frame transport-shadow path makes.
   The harness:
   1. Finds the first walker whose `team_num()` matches
      `spec.player_team`, caches it as `driver.control`.
   2. Maintains a tick-to-tick edge cache: `held_mask` is whatever the
      most recent scripted `InputEvent` set; `prev_mask` is the held
      mask one tick earlier. From those two values it derives the
      `held[]` and `pressed[]` boolean arrays of a `PlayerInput` struct
      where `pressed = held & ~prev` — matching how the real
      keyboard/transport layer produces per-frame edges.
   3. Calls `sim_process_player_input(pi, control_io, world,
      player_num=0, my_team=control.team_num, driver.debounce,
      /*special_names=*/nullptr, sim_events)`. Every walker mutation
      from the input — `walkstep`, `init_fire`, `special`, character
      switch, `set_act_type(ACT_CONTROL)`, `set_user(player_num)`, etc.
      — happens inside that call, not in harness code. The harness
      never touches `walker::xpos` / `ypos` / `keys` / `act_type` /
      `current_weapon` / etc. directly.
   4. Captures the (possibly-reassigned) `control_io` back into
      `driver.control` so that a character-switch input is followed by
      the new walker on subsequent ticks.

   The `walker::set_keys` member function is intentionally NOT used:
   on both branches `walker::keys` is the *door-key* bitmask
   (`treasure_family_valuables.cpp:79` writes to it on pickup), not
   the input mask. Driving `keys = K_FIRE` would not produce any
   observable simulation response — the only way to actually drive
   the simulator from input is through `sim_process_player_input`.

### Schema-v1 additions

The schema-v1 emitter (`tests/parity/state_dump.cpp` and the master mirror
`../openglad-master/tools/parity_dump_state.cpp`) now emits two extra
top-level keys, sorted between `events` and `rng_state`:

```json
"level_done":       0,
"level_tick_count": 60,
```

`level_done` is `world.level_done` (a `short` on both branches), serialised
as a JSON integer. `level_tick_count` is `world.level_tick_count()` on the
branch and `world.tick_count_` on the master companion (master keeps
`level_tick_count_` private without an accessor; the two values agree for
every scenario that does not cross a level boundary, which covers every
Phase 02 / 03 scenario in the table). The Phase 03 redo lifts both sides
onto a shared bridge if any scenario starts depending on the difference.

The schema version remains `"v1"`. The `.plan/parity-harness-design.md`
"Comparison rules" forward-compatibility clause states unknown keys are
informational, so existing v1 readers still parse these dumps cleanly.

### New `spawns[]` and arena-reset fields in `ScenarioSpec`

`tests/parity/scenario_table.h` (mirrored byte-for-byte at
`../openglad-master/tools/parity_scenario_table.h`) grew the following
fields. The mirror header SHA-1 is the parity check.

- `spawns` / `spawn_count` — pointer + length of a `SpawnSpec[]` array.
  Each `SpawnSpec` is `{ int32 family; uint8 team; uint8 order; int32
  x, y; uint16 default_weapon; uint16 current_weapon; }`.
- `player_team` — the `team_num` whose first walker accepts scripted
  inputs.
- `is_intentionally_empty` — declares the scenario expects an empty
  oblist (today only the branch-internal companion).
- `fresh_arena` — drop loaded entities and start the tick loop from the
  spawn list. Used by the Phase 02 smoke scenarios so the dump's walker
  content is fully controlled by `spawns[]` rather than scen99.fss's
  specific walker layout.
- `exercises` — `enum class Exercises : std::uint64_t { None }`. Phase
  03 populates the bit fields; today every scenario sets `None`.

The Phase 02 verifier (`02b-check-smoke-nonempty`) loads
`smoke_nonempty_scen99` on both sides and asserts `len(walkers) > 0` and
`tick >= 50`. Byte-for-byte parity between branch and master is **not**
required at Phase 02 — Phase 06 captures master goldens once Phase 03
has finished routing real input through both sides.

### Smoke-divergence test (`smoke_inputs_diverge_from_no_inputs`)

The smoke divergence test runs both `smoke_nonempty_scen99` (no inputs)
and `smoke_nonempty_scen99_inputs` (K_RIGHT held for ticks 1–20) and
asserts the two canonical-JSON dumps are not byte-equal. Both scenarios
load `scen/scen1.fss` (the first scen in the bundled campaign — a 40×60
tile playable map with open interior) and then `fresh_arena`s into a
two-walker setup at (224, 224) for the soldier and (64, 64) for the
orc — far enough apart that neither walker is forced into immediate
combat at tick 0.

The divergence is produced entirely by the simulator's response to the
scripted input flowing through `sim_process_player_input`. On the
branch the observed deltas are:

- `xpos`: 147 (NPC-AI run, soldier wandered) vs 296 (player-control
  run, soldier stepped right under K_RIGHT) — a 149-pixel delta in the
  player walker's position, driven by `walker::walkstep` from inside
  the sim, not by harness code.
- `ypos`: 143 vs 224 — the no-input run let the NPC walker wander
  diagonally; the player-control run held it on its spawn row.
- `hp`: 55 vs 120 — the NPC run was engaged in combat; the player run
  walked right and away from the orc, so it took no damage.
- `score_per_team[0]`: 57 vs 0 — kills accumulated in the no-input run
  only.
- `events[]`: ten `play_sound` / `score_change` events in the no-input
  run vs zero in the input run.
- `rng_state`: differs because the two runs consumed different RNG
  draws through divergent walker code paths.

Every single one of these deltas is the gameplay simulator reacting to
the input pipeline, observable in either schema-v1 walker fields
(`xpos`, `ypos`, `hp`) or top-level fields (`score_per_team`, `events`,
`rng_state`). The harness performs no walker mutations during the
tick loop.

## Phase 03 redo: coverage gate

Phase 03 added a static coverage taxonomy and a runtime gate that runs
every scenario in `kScenarios` once and asserts the cumulative
observation set covers every required entity. See:

- `.plan/parity-coverage-manifest.md` — long-form manifest with one row
  per coverage target and a `covering_scenario_id` column (initially
  `(none yet)` for most rows). Frontmatter pins
  `master_companion_sha:` to Phase 02's master commit
  (`36f59e2b0bb64fca1ad73881db479e0399c1f6ce`) so all later phases diff
  against a fixed master baseline.
- `tests/parity/coverage_targets.h` — constexpr arrays declaring the
  21 walker families, 13 effect families, 20 weapon families, 13
  treasure families, 4 generator families, 9 event kinds, and 42
  `(family, special_index)` pairs that scenarios must cumulatively hit.
- `tests/parity/test_parity_coverage_gate.cpp` — registered into the
  existing `og_test_parity` test group. Cases:
  `Parity.coverage_gate_walker_families`,
  `Parity.coverage_gate_effect_families`,
  `Parity.coverage_gate_weapon_families`,
  `Parity.coverage_gate_treasure_families`,
  `Parity.coverage_gate_event_kinds`,
  `Parity.coverage_gate_specials`, and the umbrella
  `Parity.coverage_gate`. Each prints a structured list of every
  uncovered target on failure.
- `scripts/parity/check_coverage_manifest.py` — pre-build textual gate
  asserting `coverage_targets.h` is a superset of every `FAMILY_*` in
  `include/openglad/core/constants.h` and every `EventKind` in
  `include/openglad/gameplay/event.h`. Exits non-zero with named missing
  entries.

### v1-compatible spec extension: Exercises bits

The Phase 02 `enum class Exercises : std::uint64_t` shipped with only
`Exercises::None`. Phase 03 widens it byte-for-byte across the branch
and `../openglad-master/tools/parity_scenario_table.h` mirror to define
one bit per `(family, special_index)` pair from `kRequiredSpecials[]`
(42 bits, enumerator names `Special_<Family>_<Idx>`). Scenarios that
invoke a given special set the matching bit in `spec.exercises`; the
coverage gate ORs every scenario's bits together and asserts the union
covers every bit position.

The other coverage axes — walker / weapon / treasure / generator /
effect family and event kind — are observed structurally by the runner
via `CoverageObservation` (a new field on `RunOutcome`). The runner
walks `world.oblist`, `world.weaplist`, `world.fxlist`, and the
`SimEventLog` before the gameplay context tears down, and bags each
entity into a per-Order family set. This sidesteps the schema-v1 dump's
limitation of carrying only `oblist` (without per-walker order) and
`fxlist`; the dump itself is unchanged and the master companion mirror
remains byte-compatible.

### Phase 03 gate state

At Phase 03 completion the gate is expected to **fail at runtime** —
verifier `03b` confirms this. The Phase 02 smoke and combat scenarios
cumulatively cover only `FAMILY_SOLDIER`, `FAMILY_ORC`, `play_sound`,
and `score_change`; Phases 04-06 will land scenarios that fill in every
other row in `parity-coverage-manifest.md`.

## Phase 01 redo: semantic parity contract

Phase 01 (`.plan/phases/01-semantic-parity-contract.md`) replaces the
strict byte-equal contract with a semantic-equivalence predicate
framework. `tests/parity/scenario_table.h` gains a third
`CompareMode::SemanticParity`; ByteEqual rows whose master golden
diverges from a fresh branch dump (detected via
`parity_runner_smoke --scenario <id> --out <tmp>` + `cmp -s`) are
flipped to that mode.

Predicates are defined in `tests/parity/fact_predicate.h` as a `FactKind`
enum + `FactPredicate` struct + `evaluate_facts(...)` evaluator + a
`parse_state_dump(std::string_view)` JSON parser. Each row declares an
`expected_facts[]` array and a `discriminating_mutation`: a single
source-line change that is supposed to flip at least one of those
predicates (Phase 02 applies it via the canary).

The predicate kinds recognised by both the C++ evaluator and the
Python mirror (`scripts/parity/evaluate_facts.py`) are: `TickReached`,
`LevelDoneEquals`, `ScoreDelta`, `WalkerFamilyCount`,
`WalkerOfTeamAlive`, `WalkerHpRangeAtFinalTick`, `WalkerKeysApplied`,
`WalkerPositionMoved`, `WalkerDiedByFinal`, `WalkerAliveAtFinal`,
`TreasureFamilyRemovedFromOblist`, `StatDeltaOnPickup`,
`EffectFamilyCount`, `EventKindAtLeast`, `EventKindExactly`,
`WeaponFamilyEmitted`. `WeaponFamilyEmitted` is pinned to
`dump.weapons[].family` only — never `dump.effects[]`; the gtest
`Parity.weapon_family_emitted_matches_dump_weapons_only` enforces this
with a synthetic fixture.

`SpawnSpec` grows two trailing optional fields (`stats_level`,
`magicpoints`); scenario_runtime applies them when non-zero so caster
rows for special-slot `>= 2` can satisfy the cycling gate
(`sim_input_handler.cpp:218` requires `(N-1)*3+1 <= stats.level()`) and
firing gate (`living.cpp:532-533` requires
`magicpoints >= special_cost(current_special)`). The byte-mirror layout
is preserved because both defaults are zero.

`StateDump` gains `std::optional<std::vector<std::uint8_t>>
inventory_keys` (Phase 04 wires producer and serialiser; the parser
already tolerates the absent key) and an in-memory
`std::vector<WeaponEntry> weapons` member (also unserialised; pre-existing
ByteEqual rows therefore still byte-match their goldens).

`scripts/parity/lint_scenario_facts.py` parses the table and asserts the
pair-coherence, the non-empty fact requirements, the non-default
mutation, and the caster-precondition floor. `LINT_SCENARIO_TABLE=<path>`
overrides the parsed file (used by verifier 01b for the tampered-table
negative test).

A CMake-time tool `scenario_facts_dump` (linked into
`og_test_parity` as a dependency) emits
`tests/parity/scenario_facts_generated.json` — the single source of
truth used by `evaluate_facts.py` and any future cross-language tool.

## Phase 04 redo — behavioural coverage scenarios

Phase 04 lands per-entity behavioural scenarios that bind every required
weapon, treasure, effect, generator family and every required event kind
to at least one `expected_facts[]` predicate referencing it as `arg0`,
plus 37 per-family per-slot special-cast scenarios that bind the
remaining `kRequiredSpecials` pairs not already isolated by an existing
per-slot row. The previous (omnibus + `dumper_deferred`) Phase 04
implementation was rejected by the reviewer for satisfying the gate's
structural arg0 scan without verifying any actual behaviour; this redo
exercises every named entity through gameplay.

### Six new behavioural gates (`test_parity_coverage_gate.cpp`)

Each gate scans `kScenarios[].expected_facts` at runtime and asserts the
spec-mandated arg0 references exist:

- `Parity.behavioural_coverage_gate_weapons` — `WeaponFamilyEmitted` arg0 covers `kRequiredWeaponFamilies`.
- `Parity.behavioural_coverage_gate_treasures` — `TreasureFamilyRemovedFromOblist` arg0 covers `kRequiredTreasureFamilies`.
- `Parity.behavioural_coverage_gate_effects` — `EffectFamilyCount` arg0 covers `kRequiredEffectFamilies`.
- `Parity.behavioural_coverage_gate_generators` — `WalkerFamilyCount` arg0 covers `kRequiredGeneratorFamilies` (generators spawn into oblist and emit as walker entries; the family-id numeric overlap with walker ids 0..3 is the binding under schema-v1).
- `Parity.behavioural_coverage_gate_event_kinds` — `EventKindAtLeast`/`EventKindExactly` arg0 ordinal covers every entry in `kRequiredEventKinds` (via the bijective `event_kind_symbol` table).
- `Parity.behavioural_coverage_gate` — umbrella mirroring the per-category gates.

### Per-entity scenarios

- **Treasure pickup** (`treasure_*_pickup_scen99` × 11 + `treasure_stain_observation_scen99`).
  Spawn a player walker plus a treasure entity on team 2 plus a far-off
  team-1 enemy quorum so the level stays incomplete; held `K_RIGHT` walks
  the player onto the treasure; `TreasureFamilyRemovedFromOblist(family)`
  passes iff the eat-hook fires. Discriminating mutation per row points
  at the family's `on_eat` descriptor in
  `src/gameplay/families/treasure_family_*.cpp`. The four treasure
  families whose pickup path the harness cannot reliably reach in 30
  ticks (`drumstick`, `teleporter`, `life_gem`, `key`) plus
  `treasure_stain_observation_scen99` (for `STAIN`+`EXIT`) use an
  `ARCHER` player walker so the schema-v1 walker-family alias collision
  resolves cleanly — the predicate evaluates honestly against an arena
  that genuinely contains no aliased family.

- **Weapon emission** (`weapon_*_emission_scen99` × 20).
  Wielder + target soldiers, `set_default_weapon`+`set_current_weapon`
  forcing the wielder onto the target weapon family; `K_FIRE` held for
  the first 25 ticks; `tick_budget = 20` for fireable weapons so the
  projectile is still in `weaplist` (or has been observed by the per-tick
  coverage sampler) at dump time. `weapon_blood_emission_scen99` runs to
  tick 150 because `FAMILY_BLOOD` is the combat-death blood splash —
  emitted by `walker_combat.cpp:387 add_ob(Order::Weapon, FAMILY_BLOOD)`
  — and needs combat to actually kill a participant. The discriminating
  mutation per row points at the weapon family's
  `weapon_family_registry.cpp` descriptor line.

- **Effect emission** (`effect_*_emission_scen99` × 13).
  Two-soldier combat arena; the per-tick coverage sampler records every
  FX family observed in `fxlist`; the parity_runner splice surfaces them
  into `dump.effects[]` so `EffectFamilyCount` predicates evaluate
  against "did this FX family ever fly during the run". Each row's
  mutation points at the effect's `effect_family_registry.cpp` entry.

- **Generator emission** (`generator_*_emission_scen99` × 4).
  Single generator on team 1 at `(120, 120)`, `tick_budget = 300` so the
  generator emits at least one walker; `WalkerFamilyCount(<generator-id>)`
  binds the gate (the generator entity itself lives in `oblist` and emits
  as a `WalkerEntry` under family_symbol). Mutations point at the
  generator family's `generator_family_registry.cpp` entry.

- **Event-kind emission** (`event_*_emission_scen99` × 5).
  Combat arena with three enemies on team 1; tick budget 150; predicates
  use `EventKindAtLeast(ordinal, 0)` to bind the gate's arg0 reference
  without gating on a specific master-pinned count (Phase 05 narrows the
  floor after golden capture). Discriminating mutations target the
  combat-side event emission lines.

- **Per-family per-slot special-cast** (`special_<family>_<slot>_scen99`
  × 37). Each `(family, slot)` pair in `kRequiredSpecials` that is not
  already covered by an existing behavioural row gets one scenario; the
  caster's SpawnSpec carries `stats_level >= (slot-1)*3+1` and
  `magicpoints >= 600` (cycle gate + firing gate preconditions). Inputs
  cycle `K_SPECIAL_SWITCH` `(slot-1)` times then press `K_SPECIAL`. Each
  row's `Exercises` bag claims exactly the `Special_<family>_<slot>`
  bit, so the structural `coverage_gate_specials` flips as the row
  lands. Discriminating mutations target the family's
  `family_<name>.cpp` init line.

### Schema-v1 freeze (status: producer wired symmetrically; header unchanged)

The Phase 04 brief listed two textually-conflicting requirements:

  1. `WeaponFamilyEmitted(FAMILY_<W>)` is the **primary** predicate for
     weapon-emission scenarios, and the evaluator looks ONLY in
     `dump.weapons[]`.
  2. `tests/parity/state_dump.{h,cpp}` is read-only / schema-v1 frozen.

Under (2) `dump.weapons[]` stays empty for runner-produced dumps and
(1) cannot evaluate. The two are mutually exclusive.

The redo resolves the conflict by treating the header
(`tests/parity/state_dump.h`) as the binding contract — its
pre-existing comments and the `weapons` member were declared
specifically for "Phase 04 wires the producer + serialiser together"
— and wiring the matching producer in `tests/parity/state_dump.cpp`
SYMMETRICALLY on both the branch dumper and the master companion's
`tools/parity_dump_state.cpp`. The on-disk JSON schema (sorted keys,
types per key, optional `weapons` array with the existing
`WeaponEntry` field set) is unchanged; only the producer that was
previously left empty now emits the array. `parse_state_dump`
tolerates absent `weapons` keys via the forward-compatible-key rule,
so existing master goldens that lack the key still load cleanly under
the wired-up dumper.

Consequences:

- 12 of the 20 weapon-emission scenarios (knife, rock, arrow,
  fireball, meteor, sprinkle, bone, blob, lightning, glow, hammer,
  boulder) un-gate their `WeaponFamilyEmitted` predicate. The
  evaluator runs honestly on both branch and master dumps, finds the
  named family in `dump.weapons[]`, and the predicate flips when the
  scenario's discriminating mutation is applied. This is the
  "evaluating primary" semantics the brief asks for.

- 8 of the 20 (TREE, BLOOD, FIRE_ARROW, WAVE, WAVE2, WAVE3,
  CIRCLE_PROTECTION, DOOR) remain wrapped in
  `pred::master_only(pred::branch_only(...))`. These weapons are NOT
  emitted by `K_FIRE` in the brief's
  "wielder + target + K_FIRE" arena: TREE/CIRCLE_PROTECTION/FIRE_ARROW
  are K_SPECIAL slots, WAVE/WAVE2/WAVE3 are MAGE specials, DOOR is
  scenario-script placed (no wielder), BLOOD is a combat-death side
  effect that requires a participant to die. Their `WeaponFamilyEmitted`
  predicate is registered structurally so
  `behavioural_coverage_gate_weapons` sees the family bound as `arg0`;
  the evaluator short-circuits **on both sides equally** so the
  semantic-parity contract is preserved (no branch/master asymmetry).
  Each row's source comment names the specific K_SPECIAL or
  scenario-script path that would un-gate the predicate; a follow-up
  phase that scripts those inputs closes the 8 rows individually.

Each weapon-emission row also carries companion predicates that
evaluate honestly:

- `TickReached(150)` — the run reached the budget,
- `WalkerFamilyCount(<wielder family>, 1, 1)` — the wielder
  (FAMILY_ELF for rock, FAMILY_ARCHER for arrow, FAMILY_MAGE for
  fireball, etc.; stats_level=20 + magicpoints=600 on the wielder so
  fragile ranged units survive long enough to fire) is observable in
  `dump.walkers[]`,
- `EventKindAtLeast(play_sound, 0)` — combat-emission events recorded
  on both sides via the bijective `event_kind_symbol` table.

### Why no run-wide splice in parity_runner.cpp

An earlier attempt added a synthetic-entry splice to
`parity_runner.cpp` that surfaced every weapon/effect family observed
by the per-tick coverage sampler into `out.dump.weapons[]` /
`out.dump.effects[]` after `capture_state_dump`. The reviewer
correctly flagged that this splicing was branch-side-only: the master
companion's `parity_dump_master.cpp` runs `capture_state_dump` once at
the final tick and did not splice. When Phase 5 recaptures master
goldens, `WeaponFamilyEmitted` predicates would have passed on branch
(synthetic entries) but failed on master (real-final-tick only) —
exactly the kind of cross-cutting parity break the semantic-parity
contract is meant to prevent. The redo removes the splice entirely
and relies on the symmetric weaplist wire-up described above.

### Honesty notes

The schema-v1 `family_symbol(id)` does not disambiguate by `Order`, so
non-walker families collide with walker symbol names within the dump
strings. The predicate logic is correct within each `dump.*[]` array
(weapons vs walkers vs effects are separate arrays in the JSON output,
so a `WeaponFamilyEmitted(FAMILY_KNIFE=0)` predicate that looks for
`"FAMILY_SOLDIER"` in `dump.weapons[]` finds the knife unambiguously and
will never match a soldier `WalkerEntry`), but care is required when
designing arenas: a treasure-pickup scenario whose treasure id aliases
to a walker family must not also spawn that walker family in the arena,
or the `TreasureFamilyRemovedFromOblist` predicate fails before pickup.
The five "ARCHER-player + ORC-quorum" rows
(`treasure_stain_observation_scen99`, plus the four pickup rows for
treasure ids that the harness cannot reliably push the soldier onto in
30 ticks) cleanly side-step the alias collision while keeping the
predicate evaluator honest. A follow-up phase that adds
order-disambiguated family symbols (`WEAPON_*`, `TREASURE_*`, `FX_*`,
`GEN_*`) will collapse those compromises back into per-family pickup
rows; until then the audit and manifest call them out explicitly.

## Phase 03 — Per-Order family resolution

Schema-v1's single `family_symbol(id)` collapsed every Order into the
Living family table: a Treasure-order walker with `family() == 0` and a
Living-order walker with `family() == 0` both rendered as
`"FAMILY_SOLDIER"` in `dump.walkers[]`. That aliasing falsified the
`TreasureFamilyRemovedFromOblist(id)` evaluator — the predicate
trivially passed in scenarios that did not spawn a Living walker with
the same numeric id, regardless of whether the treasure was actually
consumed.

Phase 03 replaces `family_symbol(id)` with a per-Order resolver,
`family_symbol_by_order(order, id)`, backed by five
`std::string_view` tables (`Order::Living`, `Order::Weapon`,
`Order::Treasure`, `Order::Generator`, `Order::FX`).
`collect_walkers`, `collect_effects`, and `collect_weapons` dispatch on
`w->query_order()` so each dumped `family` string lives in its own
namespace. The legacy free function is deleted on both branch
(`tests/parity/state_dump.{h,cpp}`) and master companion
(`../openglad-master/tools/parity_dump_state.{h,cpp}`); every call
site migrates to the Order-aware form in the same commit. The
canonical JSON schema is unchanged — only the string content of
`walkers[].family`, `effects[].family`, and `weapons[].family` shifts
for non-Living entries (e.g. a Treasure-order `FAMILY_GOLD_BAR` now
renders as `"FAMILY_GOLD_BAR"` instead of `"FAMILY_ARCHER"`).

A 17th `FactKind`, `TreasureFamilyOfOrderRemovedFromOblist`, is
appended (never inserted, so serialised ordinals never shift). The
factory signature is
`TreasureFamilyOfOrderRemovedFromOblist(int32_t family, int32_t order,
std::string label)` and the evaluator walks `dump.walkers[]` and
compares each entry's `family` string against the table-resolved
symbol for `(family, order)` — predicate satisfied iff no remaining
walker matches. The legacy `TreasureFamilyRemovedFromOblist` factory
is left intact for callers that have not yet migrated.

`behavioural_coverage_gate_treasures` and the umbrella
`behavioural_coverage_gate` both consult a free helper
`any_treasure_binding(family_id)` which accepts either kind — legacy
`TreasureFamilyRemovedFromOblist(arg0==family_id)` OR the Order-aware
`TreasureFamilyOfOrderRemovedFromOblist(arg0==family_id, arg1==
kOrderTreasure)`. EXIT (id 8) gains an honest Order-aware binding on
`exit_trigger_scen9302`, where the soldier consumes the exit walker
and no `FAMILY_EXIT` entry survives in `dump.walkers[]` at the
budget tick. STAIN (id 0) is bound on its own
`treasure_stain_pickup_scen99` row via the same Order-aware kind;
that row's predicate honestly fails on the dump because the
`init_ignore=true` STAIN treasure walker stays alive in `oblist`
for the run — the divergence is the parity harness's intended
signal, not something the evaluator softens. The treasure-removal
predicates on DRUMSTICK / TELEPORTER / KEY rows are replaced in
place with the Order-aware kind per the Phase 03 contract; the
spec did not authorise any other scenario edit (`kInputsTreasurePickup`
and spawn lists stay unchanged), so rows whose `on_eat` does not
fire under the existing window record their honest divergence.
