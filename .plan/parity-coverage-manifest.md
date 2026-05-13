---
phase: 04-walker-family-scenarios
schema: v1
master_companion_sha: 87c07781534ea07a350b22662bb1434fdba9bdb4
generated_from:
  - include/openglad/core/constants.h
  - include/openglad/gameplay/event.h
  - src/gameplay/families/family_*.cpp
  - tests/parity/coverage_targets.h
  - tests/parity/scenario_table.h
enforced_by:
  - scripts/parity/check_coverage_manifest.py
  - tests/parity/test_parity_coverage_gate.cpp
---

# Gameplay Parity Coverage Manifest

This document is the single source of truth for "what the parity harness
must exercise". It is the contract that ties three artefacts together:

1. **The C++ headers** declaring gameplay entities
   (`include/openglad/core/constants.h`, `include/openglad/gameplay/event.h`)
   and the family descriptors that bind `do_special` callbacks
   (`src/gameplay/families/family_*.cpp`).
2. **`tests/parity/coverage_targets.h`** — the constexpr arrays of required
   IDs and `Exercises` bit positions consumed by the runtime gate.
3. **`tests/parity/scenario_table.h`** and the scenarios that ultimately
   exercise each row below.

The `master_companion_sha` in the frontmatter pins the master worktree
(at `../openglad-master`) used as the parity baseline. Phases 04-07 diff
the cumulative branch dump against a master capture produced from that
exact commit; do not bump this SHA without rerunning master capture.

## How rows become covered

A row's `covering_scenario_id` says `(none yet)` until a scenario in
`tests/parity/scenario_table.h` causes the corresponding bit/family to
appear in the parity runner's `CoverageObservation` (for structurally
observable targets — walker, weapon, treasure, generator, effect family
and event kind) or sets the appropriate `Exercises::Special_*` bit
(for specials).

Phase 03 deliberately ships with the gate failing. Phases 04-06 then
backfill scenarios, replacing `(none yet)` with the scenario `id` that
first covers each row.

## Required walker families (21)

Observed via `CoverageObservation::walker_families` (Order::Living in
oblist).

| family | covering_scenario_id |
|---|---|
| `FAMILY_SOLDIER` | `smoke_nonempty_scen99` |
| `FAMILY_ELF` | `family_elf_scen99` |
| `FAMILY_ARCHER` | `family_archer_scen99` |
| `FAMILY_MAGE` | `family_mage_scen99` |
| `FAMILY_SKELETON` | `family_skeleton_scen99` |
| `FAMILY_CLERIC` | `family_cleric_scen99` |
| `FAMILY_FIREELEMENTAL` | `family_fireelemental_scen99` |
| `FAMILY_FAERIE` | `family_faerie_scen99` |
| `FAMILY_SLIME` | `family_slime_scen99` |
| `FAMILY_SMALL_SLIME` | `family_small_slime_scen99` |
| `FAMILY_MEDIUM_SLIME` | `family_medium_slime_scen99` |
| `FAMILY_THIEF` | `family_thief_scen99` |
| `FAMILY_GHOST` | `family_ghost_scen99` |
| `FAMILY_DRUID` | `family_druid_scen99` |
| `FAMILY_ORC` | `smoke_nonempty_scen99` |
| `FAMILY_BIG_ORC` | `family_big_orc_scen99` |
| `FAMILY_BARBARIAN` | `family_barbarian_scen99` |
| `FAMILY_ARCHMAGE` | `family_archmage_scen99` |
| `FAMILY_GOLEM` | `family_golem_scen99` |
| `FAMILY_GIANT_SKELETON` | `family_giant_skeleton_scen99` |
| `FAMILY_TOWER1` | `family_tower1_scen99` |

## Required weapon families (20)

Observed via `CoverageObservation::weapon_families` (weaplist).

| family | covering_scenario_id |
|---|---|
| `FAMILY_KNIFE` | `(none yet)` |
| `FAMILY_ROCK` | `(none yet)` |
| `FAMILY_ARROW` | `(none yet)` |
| `FAMILY_FIREBALL` | `(none yet)` |
| `FAMILY_TREE` | `(none yet)` |
| `FAMILY_METEOR` | `(none yet)` |
| `FAMILY_SPRINKLE` | `(none yet)` |
| `FAMILY_BONE` | `(none yet)` |
| `FAMILY_BLOOD` | `(none yet)` |
| `FAMILY_BLOB` | `(none yet)` |
| `FAMILY_FIRE_ARROW` | `(none yet)` |
| `FAMILY_LIGHTNING` | `(none yet)` |
| `FAMILY_GLOW` | `(none yet)` |
| `FAMILY_WAVE` | `(none yet)` |
| `FAMILY_WAVE2` | `(none yet)` |
| `FAMILY_WAVE3` | `(none yet)` |
| `FAMILY_CIRCLE_PROTECTION` | `(none yet)` |
| `FAMILY_HAMMER` | `(none yet)` |
| `FAMILY_DOOR` | `(none yet)` |
| `FAMILY_BOULDER` | `(none yet)` |

## Required treasure families (13)

Observed via `CoverageObservation::treasure_families` (Order::Treasure
in oblist).

| family | covering_scenario_id |
|---|---|
| `FAMILY_STAIN` | `(none yet)` |
| `FAMILY_DRUMSTICK` | `(none yet)` |
| `FAMILY_GOLD_BAR` | `(none yet)` |
| `FAMILY_SILVER_BAR` | `(none yet)` |
| `FAMILY_MAGIC_POTION` | `(none yet)` |
| `FAMILY_INVIS_POTION` | `(none yet)` |
| `FAMILY_INVULNERABLE_POTION` | `(none yet)` |
| `FAMILY_FLIGHT_POTION` | `(none yet)` |
| `FAMILY_EXIT` | `(none yet)` |
| `FAMILY_TELEPORTER` | `(none yet)` |
| `FAMILY_LIFE_GEM` | `(none yet)` |
| `FAMILY_KEY` | `(none yet)` |
| `FAMILY_SPEED_POTION` | `(none yet)` |

## Required generator families (4)

Observed via `CoverageObservation::generator_families` (Order::Generator
in oblist). Not asserted by an individual `coverage_gate_*` case yet —
the umbrella `coverage_gate` requires walker / effect / weapon /
treasure / event-kind / specials. Generators are listed here so Phase
04-06 scenarios are graded against them too, in preparation for adding
the generator gate when a generator-bearing scenario lands.

| family | covering_scenario_id |
|---|---|
| `FAMILY_TENT` | `(none yet)` |
| `FAMILY_TOWER` | `family_mage_scen99` |
| `FAMILY_BONES` | `(none yet)` |
| `FAMILY_TREEHOUSE` | `family_elf_scen99` |

## Required effect (FX) families (13)

Observed via `CoverageObservation::effect_families` (fxlist).

| family | covering_scenario_id |
|---|---|
| `FAMILY_EXPAND` | `(none yet)` |
| `FAMILY_GHOST_SCARE` | `(none yet)` |
| `FAMILY_BOMB` | `(none yet)` |
| `FAMILY_EXPLOSION` | `(none yet)` |
| `FAMILY_FLASH` | `(none yet)` |
| `FAMILY_MAGIC_SHIELD` | `(none yet)` |
| `FAMILY_KNIFE_BACK` | `(none yet)` |
| `FAMILY_BOOMERANG` | `(none yet)` |
| `FAMILY_CLOUD` | `(none yet)` |
| `FAMILY_MARKER` | `(none yet)` |
| `FAMILY_CHAIN` | `(none yet)` |
| `FAMILY_DOOR_OPEN` | `(none yet)` |
| `FAMILY_HIT` | `(none yet)` |

## Required event kinds (9)

Observed via `CoverageObservation::event_kinds` (canonical
lowercase strings from `state_dump.cpp::event_kind_symbol`).

| event_kind | covering_scenario_id |
|---|---|
| `play_sound` | `smoke_nonempty_scen99` |
| `notification` | `(none yet)` |
| `set_palette` | `(none yet)` |
| `request_redraw` | `(none yet)` |
| `end_game` | `(none yet)` |
| `set_end` | `(none yet)` |
| `request_exit_confirmation` | `(none yet)` |
| `withdraw_to_level` | `(none yet)` |
| `score_change` | `(none yet)` |

## Required specials (42)

Observed via `Exercises::Special_*` bits set in scenario `exercises`
fields. The bit position is fixed by the order of `kRequiredSpecials[]`
in `tests/parity/coverage_targets.h` and matches the enumerator order
in `Exercises` in `tests/parity/scenario_table.h`. Reorderings here MUST
mirror byte-for-byte to `../openglad-master/tools/parity_scenario_table.h`.

| bit | (family, special_idx) | special name | Exercises enumerator | covering_scenario_id |
|---:|---|---|---|---|
|  0 | `(FAMILY_SOLDIER, 1)` | CHARGE | `Special_Soldier_1` | `(none yet)` |
|  1 | `(FAMILY_SOLDIER, 2)` | BOOMERANG | `Special_Soldier_2` | `(none yet)` |
|  2 | `(FAMILY_SOLDIER, 3)` | WHIRLWIND | `Special_Soldier_3` | `(none yet)` |
|  3 | `(FAMILY_SOLDIER, 4)` | DISARM | `Special_Soldier_4` | `(none yet)` |
|  4 | `(FAMILY_ELF, 1)` | ROCKS | `Special_Elf_1` | `(none yet)` |
|  5 | `(FAMILY_ELF, 2)` | BOUNCING ROCKS | `Special_Elf_2` | `(none yet)` |
|  6 | `(FAMILY_ELF, 3)` | LOTS OF ROCKS | `Special_Elf_3` | `(none yet)` |
|  7 | `(FAMILY_ELF, 4)` | MEGA ROCKS | `Special_Elf_4` | `(none yet)` |
|  8 | `(FAMILY_ARCHER, 1)` | FIRE ARROWS | `Special_Archer_1` | `(none yet)` |
|  9 | `(FAMILY_ARCHER, 2)` | BARRAGE | `Special_Archer_2` | `(none yet)` |
| 10 | `(FAMILY_ARCHER, 3)` | EXPLODING BOLT | `Special_Archer_3` | `(none yet)` |
| 11 | `(FAMILY_MAGE, 1)` | TELEPORT | `Special_Mage_1` | `(none yet)` |
| 12 | `(FAMILY_MAGE, 2)` | WARP SPACE | `Special_Mage_2` | `(none yet)` |
| 13 | `(FAMILY_MAGE, 3)` | FREEZE TIME | `Special_Mage_3` | `(none yet)` |
| 14 | `(FAMILY_MAGE, 4)` | ENERGY WAVE | `Special_Mage_4` | `(none yet)` |
| 15 | `(FAMILY_MAGE, 5)` | HEARTBURST | `Special_Mage_5` | `(none yet)` |
| 16 | `(FAMILY_SKELETON, 1)` | TUNNEL | `Special_Skeleton_1` | `(none yet)` |
| 17 | `(FAMILY_CLERIC, 1)` | HEAL | `Special_Cleric_1` | `(none yet)` |
| 18 | `(FAMILY_CLERIC, 2)` | RAISE UNDEAD | `Special_Cleric_2` | `(none yet)` |
| 19 | `(FAMILY_CLERIC, 3)` | RAISE GHOST | `Special_Cleric_3` | `(none yet)` |
| 20 | `(FAMILY_CLERIC, 4)` | RESURRECT | `Special_Cleric_4` | `(none yet)` |
| 21 | `(FAMILY_FIREELEMENTAL, 1)` | STARBURST | `Special_FireElemental_1` | `(none yet)` |
| 22 | `(FAMILY_SLIME, 1)` | SPLIT | `Special_Slime_1` | `(none yet)` |
| 23 | `(FAMILY_SMALL_SLIME, 1)` | GROW | `Special_SmallSlime_1` | `(none yet)` |
| 24 | `(FAMILY_MEDIUM_SLIME, 1)` | GROW | `Special_MediumSlime_1` | `(none yet)` |
| 25 | `(FAMILY_THIEF, 1)` | DROP BOMB | `Special_Thief_1` | `(none yet)` |
| 26 | `(FAMILY_THIEF, 2)` | CLOAK | `Special_Thief_2` | `(none yet)` |
| 27 | `(FAMILY_THIEF, 3)` | TAUNT ENEMY | `Special_Thief_3` | `(none yet)` |
| 28 | `(FAMILY_THIEF, 4)` | POISON CLOUD | `Special_Thief_4` | `(none yet)` |
| 29 | `(FAMILY_GHOST, 1)` | SCARE | `Special_Ghost_1` | `(none yet)` |
| 30 | `(FAMILY_DRUID, 1)` | GROW TREE | `Special_Druid_1` | `(none yet)` |
| 31 | `(FAMILY_DRUID, 2)` | SUMMON FAERIE | `Special_Druid_2` | `(none yet)` |
| 32 | `(FAMILY_DRUID, 3)` | REVEAL | `Special_Druid_3` | `(none yet)` |
| 33 | `(FAMILY_DRUID, 4)` | PROTECTION | `Special_Druid_4` | `(none yet)` |
| 34 | `(FAMILY_ORC, 1)` | HOWL | `Special_Orc_1` | `(none yet)` |
| 35 | `(FAMILY_ORC, 2)` | EAT CORPSE | `Special_Orc_2` | `(none yet)` |
| 36 | `(FAMILY_BARBARIAN, 1)` | HURL BOULDER | `Special_Barbarian_1` | `(none yet)` |
| 37 | `(FAMILY_BARBARIAN, 2)` | EXPLODING BOULDER | `Special_Barbarian_2` | `(none yet)` |
| 38 | `(FAMILY_ARCHMAGE, 1)` | TELEPORT | `Special_Archmage_1` | `(none yet)` |
| 39 | `(FAMILY_ARCHMAGE, 2)` | HEARTBURST | `Special_Archmage_2` | `(none yet)` |
| 40 | `(FAMILY_ARCHMAGE, 3)` | SUMMON IMAGE | `Special_Archmage_3` | `(none yet)` |
| 41 | `(FAMILY_ARCHMAGE, 4)` | MIND CONTROL | `Special_Archmage_4` | `(none yet)` |

## Phase 03 sign-off snapshot

Coverage observed by the Phase 03 scenario set (`og_test_parity
--gtest_filter='Parity.coverage_gate*'`) — captured here so Phase 04
onward can see what is already covered for free by the existing smoke /
phase-02 scenarios:

- `walker_families` observed: `FAMILY_SOLDIER`, `FAMILY_ORC`.
- `effect_families`, `weapon_families`, `treasure_families` observed:
  none.
- `event_kinds` observed: `play_sound`, `score_change`.
- `Exercises::Special_*` bits set: none.

Every other row above must be covered by a scenario landed in Phase
04-06. The runtime gate (verifier `03b`) confirms this listing is real
by removing one covering scenario and observing the gate fail on the
now-uncovered target.

## Phase 04 sign-off snapshot

Phase 04 added 21 byte-equal arena scenarios — one per walker family
(`family_<symbolic>_scen99`) — and the master goldens that go with
them. Every spec is now spec-compliant:

- target family on team 0 at `(120, 120)`,
- `FAMILY_SOLDIER` sparring partner on team 1 at `(180, 120)`,
- ELF/MAGE/SKELETON/GHOST also spawn their corresponding
  TREEHOUSE/TOWER/TENT/BONES generator at `(60, 60)`,
- `fresh_arena=true`, `tick_budget=150`, and `kInputsFamilyAttack`
  (K_FIRE at tick 5, K_NONE at tick 64).

Production hooks are restored: `parity_runner` installs
`sdl_level_data_hooks()` (the production SDL `attach_render` path),
the schema-v1 `rng_state` is emitted as the real `world.rng_.state_`
(no `"unobservable"` masking), and `state_dump::collect_walkers/effects`
keeps the `entity_id() != 0 ? entity_id() : ++running_seq` fallback
that was in the original branch dumper.

Coverage outcome after Phase 04:

- `walker_families` observed: all 21 required families.
- `generator_families` observed: all 4 required families.
- `effect_families`, `weapon_families`, `treasure_families`,
  specials, and the remaining event kinds remain pending Phases 05/06.

The `Parity.coverage_gate_walker_families` gate passes. The rest of
`Parity.coverage_gate*` (effect/weapon/treasure/event/specials) and
the umbrella `Parity.coverage_gate` are expected to remain red until
Phases 05/06 land.

### Phase 04 outcome — honest goldens, 21 byte-equal tests fail

Phase 04 ships:

- 21 spec-compliant `ScenarioSpec` entries (one per walker family
  `FAMILY_SOLDIER..FAMILY_TOWER1`) — each spawns the target on
  team 0 at `(120, 120)` and a `FAMILY_SOLDIER` sparring partner
  on team 1 at `(180, 120)`; the four generator-bearing families
  (ELF, MAGE, SKELETON, GHOST) also spawn their corresponding
  TREEHOUSE/TOWER/TENT/BONES generator at `(60, 60)`.
- `tick_budget = 150` with the spec-mandated `kInputsFamilyAttack`
  (K_FIRE at tick 5, K_NONE at tick 64).
- 21 canonical master goldens captured by the rebuilt master
  companion at `master_companion_sha`.

The runner uses the production `sdl_level_data_hooks` and the
schema-v1 dumper records every field at face value:
`rng_state` is the real hex `world.rng_.state_`; walker/effect ids
are the real `walker::entity_id` with `++running_seq` fallback;
`effects[*].lifetime` is the raw `walker::lifetime()`;
`events[]` is the real `SimEventLog`; `walker.xpos/ypos` is the
live walker position; no whitelist filtering. No mask is applied
to hide branch-vs-master divergence.

All 21 `Parity.family_*_scen99` tests FAIL byte-equal against the
master goldens. That is the load-bearing signal Phase 04 produces:

- The branch (wip/networking) is 357 commits ahead of master, and
  many of those commits touch gameplay-observable behaviour. The
  "phase 0: migrate gameplay rand to SimRandom" commit series
  advances `world.rng_` at more sites than master; combat / AI /
  special-decision code has shifted enough that effect lifetimes,
  walker positions, event emission cadence, and the set of
  spawned children all diverge in concrete ways.
- Several living families (archer, druid, faerie, fire-elemental,
  small slime, thief) are fully removed from master's `oblist` on
  death — those goldens have only one walker entry, so the
  04a-check-family-coverage `walkers[*] >= 2` invariant fails for
  those rows too.
- Each failing row is exactly the kind of row Phase 07's prompt
  expects to classify: it inspects the branch-side code path and
  either lands a `parity-fix:` commit on the branch or files an
  `intended_diff` entry citing the branch commit SHA that
  authorised the change. Blindly re-capturing or masking the
  canonical golden is forbidden by the Phase 07 prompt and is
  exactly the failure mode `check-4` rejected during Phase 04
  verification rounds 4–6.

The `Parity.coverage_gate_walker_families` gate still passes (the
runner samples family membership every tick, so all 21 walker
families and all 4 generator families are observed regardless of
whether the byte-equal goldens match). The umbrella
`Parity.coverage_gate` plus the effect/weapon/treasure/event/
specials sub-gates remain red pending Phases 05/06.
