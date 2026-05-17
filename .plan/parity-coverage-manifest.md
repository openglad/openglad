---
phase: 04-walker-family-scenarios
schema: v1
master_companion_sha: 1e32ce0d8c3842ee932a20e03dafcb7e8bf500b2
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
| `FAMILY_KNIFE` | `weapon_knife_emission_scen99` |
| `FAMILY_ROCK` | `weapon_rock_emission_scen99` |
| `FAMILY_ARROW` | `weapon_arrow_emission_scen99` |
| `FAMILY_FIREBALL` | `weapon_fireball_emission_scen99` |
| `FAMILY_TREE` | `weapon_tree_emission_scen99` |
| `FAMILY_METEOR` | `weapon_meteor_emission_scen99` |
| `FAMILY_SPRINKLE` | `weapon_sprinkle_emission_scen99` |
| `FAMILY_BONE` | `weapon_bone_emission_scen99` |
| `FAMILY_BLOOD` | `weapon_blood_emission_scen99` |
| `FAMILY_BLOB` | `weapon_blob_emission_scen99` |
| `FAMILY_FIRE_ARROW` | `weapon_fire_arrow_emission_scen99` |
| `FAMILY_LIGHTNING` | `weapon_lightning_emission_scen99` |
| `FAMILY_GLOW` | `weapon_glow_emission_scen99` |
| `FAMILY_WAVE` | `weapon_wave_emission_scen99` |
| `FAMILY_WAVE2` | `weapon_wave2_emission_scen99` |
| `FAMILY_WAVE3` | `weapon_wave3_emission_scen99` |
| `FAMILY_CIRCLE_PROTECTION` | `weapon_circle_protection_emission_scen99` |
| `FAMILY_HAMMER` | `weapon_hammer_emission_scen99` |
| `FAMILY_DOOR` | `weapon_door_emission_scen99` |
| `FAMILY_BOULDER` | `weapon_boulder_emission_scen99` |

## Required treasure families (13)

Observed via `CoverageObservation::treasure_families` (Order::Treasure
in oblist).

| family | covering_scenario_id |
|---|---|
| `FAMILY_STAIN` | `treasure_stain_observation_scen99` |
| `FAMILY_DRUMSTICK` | `treasure_drumstick_pickup_scen99` |
| `FAMILY_GOLD_BAR` | `treasure_gold_bar_pickup_scen99` |
| `FAMILY_SILVER_BAR` | `treasure_silver_bar_pickup_scen99` |
| `FAMILY_MAGIC_POTION` | `treasure_magic_potion_pickup_scen99` |
| `FAMILY_INVIS_POTION` | `treasure_invis_potion_pickup_scen99` |
| `FAMILY_INVULNERABLE_POTION` | `treasure_invulnerable_potion_pickup_scen99` |
| `FAMILY_FLIGHT_POTION` | `treasure_flight_potion_pickup_scen99` |
| `FAMILY_EXIT` | `treasure_stain_observation_scen99` |
| `FAMILY_TELEPORTER` | `treasure_teleporter_pickup_scen99` |
| `FAMILY_LIFE_GEM` | `treasure_life_gem_pickup_scen99` |
| `FAMILY_KEY` | `treasure_key_pickup_scen99` |
| `FAMILY_SPEED_POTION` | `treasure_speed_potion_pickup_scen99` |

## Required generator families (4)

Observed via `CoverageObservation::generator_families` (Order::Generator
in oblist). Not asserted by an individual `coverage_gate_*` case yet —
the umbrella `coverage_gate` requires walker / effect / weapon /
treasure / event-kind / specials. Generators are listed here so Phase
04-06 scenarios are graded against them too, in preparation for adding
the generator gate when a generator-bearing scenario lands.

| family | covering_scenario_id |
|---|---|
| `FAMILY_TENT` | `family_skeleton_scen99` |
| `FAMILY_TOWER` | `family_mage_scen99` |
| `FAMILY_BONES` | `family_ghost_scen99` |
| `FAMILY_TREEHOUSE` | `family_elf_scen99` |

## Required effect (FX) families (13)

Observed via `CoverageObservation::effect_families` (fxlist).

| family | covering_scenario_id |
|---|---|
| `FAMILY_EXPAND` | `effect_expand_emission_scen99` |
| `FAMILY_GHOST_SCARE` | `effect_ghost_scare_emission_scen99` |
| `FAMILY_BOMB` | `effect_bomb_emission_scen99` |
| `FAMILY_EXPLOSION` | `effect_explosion_emission_scen99` |
| `FAMILY_FLASH` | `effect_flash_emission_scen99` |
| `FAMILY_MAGIC_SHIELD` | `effect_magic_shield_emission_scen99` |
| `FAMILY_KNIFE_BACK` | `effect_knife_back_emission_scen99` |
| `FAMILY_BOOMERANG` | `effect_boomerang_emission_scen99` |
| `FAMILY_CLOUD` | `effect_cloud_emission_scen99` |
| `FAMILY_MARKER` | `effect_marker_emission_scen99` |
| `FAMILY_CHAIN` | `effect_chain_emission_scen99` |
| `FAMILY_DOOR_OPEN` | `effect_door_open_emission_scen99` |
| `FAMILY_HIT` | `effect_hit_emission_scen99` |

## Required event kinds (9)

Observed via `CoverageObservation::event_kinds` (canonical
lowercase strings from `state_dump.cpp::event_kind_symbol`).

| event_kind | covering_scenario_id |
|---|---|
| `play_sound` | `smoke_nonempty_scen99` |
| `notification` | `event_notification_emission_scen99` |
| `set_palette` | `event_set_palette_emission_scen99` |
| `request_redraw` | `event_request_redraw_emission_scen99` |
| `end_game` | `event_end_game_emission_scen99` |
| `set_end` | `event_set_end_emission_scen99` |
| `request_exit_confirmation` | `scripted_input_scen9301` |
| `withdraw_to_level` | `scripted_input_scen9301` |
| `score_change` | `scoring_after_combat_scen99` |

## Required specials (42)

Observed via `Exercises::Special_*` bits set in scenario `exercises`
fields. The bit position is fixed by the order of `kRequiredSpecials[]`
in `tests/parity/coverage_targets.h` and matches the enumerator order
in `Exercises` in `tests/parity/scenario_table.h`. Reorderings here MUST
mirror byte-for-byte to `../openglad-master/tools/parity_scenario_table.h`.

| bit | (family, special_idx) | special name | Exercises enumerator | covering_scenario_id |
|---:|---|---|---|---|
|  0 | `(FAMILY_SOLDIER, 1)` | CHARGE | `Special_Soldier_1` | `special_soldier_1_scen99` |
|  1 | `(FAMILY_SOLDIER, 2)` | BOOMERANG | `Special_Soldier_2` | `special_soldier_2_scen99` |
|  2 | `(FAMILY_SOLDIER, 3)` | WHIRLWIND | `Special_Soldier_3` | `special_soldier_3_scen99` |
|  3 | `(FAMILY_SOLDIER, 4)` | DISARM | `Special_Soldier_4` | `special_soldier_4_scen99` |
|  4 | `(FAMILY_ELF, 1)` | ROCKS | `Special_Elf_1` | `special_elf_1_scen99` |
|  5 | `(FAMILY_ELF, 2)` | BOUNCING ROCKS | `Special_Elf_2` | `special_elf_2_scen99` |
|  6 | `(FAMILY_ELF, 3)` | LOTS OF ROCKS | `Special_Elf_3` | `special_elf_3_scen99` |
|  7 | `(FAMILY_ELF, 4)` | MEGA ROCKS | `Special_Elf_4` | `special_elf_4_scen99` |
|  8 | `(FAMILY_ARCHER, 1)` | FIRE ARROWS | `Special_Archer_1` | `special_archer_1_scen99` |
|  9 | `(FAMILY_ARCHER, 2)` | BARRAGE | `Special_Archer_2` | `special_archer_2_scen99` |
| 10 | `(FAMILY_ARCHER, 3)` | EXPLODING BOLT | `Special_Archer_3` | `special_archer_3_scen99` |
| 11 | `(FAMILY_MAGE, 1)` | TELEPORT | `Special_Mage_1` | `special_mage_scen126` |
| 12 | `(FAMILY_MAGE, 2)` | WARP SPACE | `Special_Mage_2` | `special_mage_2_scen99` |
| 13 | `(FAMILY_MAGE, 3)` | FREEZE TIME | `Special_Mage_3` | `special_mage_3_scen99` |
| 14 | `(FAMILY_MAGE, 4)` | ENERGY WAVE | `Special_Mage_4` | `special_mage_4_scen99` |
| 15 | `(FAMILY_MAGE, 5)` | HEARTBURST | `Special_Mage_5` | `special_mage_5_scen99` |
| 16 | `(FAMILY_SKELETON, 1)` | TUNNEL | `Special_Skeleton_1` | `special_skeleton_1_scen99` |
| 17 | `(FAMILY_CLERIC, 1)` | HEAL | `Special_Cleric_1` | `special_cleric_scen124` |
| 18 | `(FAMILY_CLERIC, 2)` | RAISE UNDEAD | `Special_Cleric_2` | `special_cleric_2_scen99` |
| 19 | `(FAMILY_CLERIC, 3)` | RAISE GHOST | `Special_Cleric_3` | `special_cleric_3_scen99` |
| 20 | `(FAMILY_CLERIC, 4)` | RESURRECT | `Special_Cleric_4` | `special_cleric_4_scen99` |
| 21 | `(FAMILY_FIREELEMENTAL, 1)` | STARBURST | `Special_FireElemental_1` | `special_fireelemental_1_scen99` |
| 22 | `(FAMILY_SLIME, 1)` | SPLIT | `Special_Slime_1` | `special_slime_1_scen99` |
| 23 | `(FAMILY_SMALL_SLIME, 1)` | GROW | `Special_SmallSlime_1` | `special_small_slime_1_scen99` |
| 24 | `(FAMILY_MEDIUM_SLIME, 1)` | GROW | `Special_MediumSlime_1` | `special_medium_slime_1_scen99` |
| 25 | `(FAMILY_THIEF, 1)` | DROP BOMB | `Special_Thief_1` | `special_thief_scen789` |
| 26 | `(FAMILY_THIEF, 2)` | CLOAK | `Special_Thief_2` | `special_thief_2_scen99` |
| 27 | `(FAMILY_THIEF, 3)` | TAUNT ENEMY | `Special_Thief_3` | `special_thief_3_scen99` |
| 28 | `(FAMILY_THIEF, 4)` | POISON CLOUD | `Special_Thief_4` | `special_thief_4_scen99` |
| 29 | `(FAMILY_GHOST, 1)` | SCARE | `Special_Ghost_1` | `special_ghost_1_scen99` |
| 30 | `(FAMILY_DRUID, 1)` | GROW TREE | `Special_Druid_1` | `special_druid_1_scen99` |
| 31 | `(FAMILY_DRUID, 2)` | SUMMON FAERIE | `Special_Druid_2` | `summon_druid_pet_scen950` |
| 32 | `(FAMILY_DRUID, 3)` | REVEAL | `Special_Druid_3` | `special_druid_3_scen99` |
| 33 | `(FAMILY_DRUID, 4)` | PROTECTION | `Special_Druid_4` | `special_druid_4_scen99` |
| 34 | `(FAMILY_ORC, 1)` | HOWL | `Special_Orc_1` | `special_orc_1_scen99` |
| 35 | `(FAMILY_ORC, 2)` | EAT CORPSE | `Special_Orc_2` | `special_orc_2_scen99` |
| 36 | `(FAMILY_BARBARIAN, 1)` | HURL BOULDER | `Special_Barbarian_1` | `special_barbarian_1_scen99` |
| 37 | `(FAMILY_BARBARIAN, 2)` | EXPLODING BOULDER | `Special_Barbarian_2` | `special_barbarian_2_scen99` |
| 38 | `(FAMILY_ARCHMAGE, 1)` | TELEPORT | `Special_Archmage_1` | `special_archmage_scen123` |
| 39 | `(FAMILY_ARCHMAGE, 2)` | HEARTBURST | `Special_Archmage_2` | `special_archmage_2_scen99` |
| 40 | `(FAMILY_ARCHMAGE, 3)` | SUMMON IMAGE | `Special_Archmage_3` | `special_archmage_3_scen99` |
| 41 | `(FAMILY_ARCHMAGE, 4)` | MIND CONTROL | `Special_Archmage_4` | `special_archmage_4_scen99` |

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

## Parity contract

Per `.plan/phases/01-semantic-parity-contract.md` the parity harness now
recognises three comparison modes; the lint and the runner both refuse
empty / default values for the new metadata fields.

- `CompareMode::ByteEqual` — the canonical JSON dump produced by
  `og::parity::canonical_serialize(...)` must match the master golden at
  `tests/parity/golden/<id>.json` byte-for-byte. Phase 01's divergence
  detector (`parity_runner_smoke --scenario <id> --out <tmp>` + `cmp -s
  ... golden`) selects which rows can keep this mode. Currently 12 rows.
- `CompareMode::SemanticParity` — the master golden is parsed via
  `og::parity::parse_state_dump(...)`; both the parsed master and the
  freshly captured branch dump must satisfy every entry in
  `spec.expected_facts[]`. The 26 rows whose byte dump diverged from the
  master golden carry this mode.
- `CompareMode::Invariant` — branch-only run-time check; no golden
  comparison. 1 row (snapshot_dirty_bits_scen9301).

Predicate layout, semantics, and the `WeaponFamilyEmitted` / dump.weapons
binding are described in `tests/parity/fact_predicate.h`. The lint
(`scripts/parity/lint_scenario_facts.py`) enforces the rules listed in
`.plan/phases/01-semantic-parity-contract.md` and the manifest gate
(`tests/parity/test_parity_coverage_gate.cpp`) is untouched in Phase 01.

`EffectFamilyCount` predicates must be qualified by either a source-walker
family (`arg3 >= 0`) or a `[min_tick, max_tick]` window
(`arg2 / arg3`); the lint rejects unqualified entries.

## Mutation canary

Phase 02 mutation canary: every scenario's `discriminating_mutation`
(declared in `tests/parity/scenario_table.h`) is applied by
`scripts/parity/run_mutation_canary.sh` and the per-predicate
evaluation is diffed pre vs post to assert at least one predicate
flips. Each `kMut_*` constant targets a line in the TU named by its
identifier (combat damage in `walker_combat.cpp`, effect lifetime in
`effect.cpp`, save header in `save_data.cpp`, family-specific
`do_special` in `families/family_*.cpp`, etc.) so that the canary
actually exercises the subsystem its predicates claim to verify.

### Known limitations

- `save_roundtrip_scen99` and `rng_seed_stable_scen99` target
  `src/resources/save_data.cpp:107` (SaveData::load() body) which is
  the correct subject for the rows' claimed save-corruption behaviour,
  but the parity runner (`tests/parity/parity_runner.cpp`) does not
  invoke `SaveData::load()` during scenario execution — the runner
  constructs a fresh `SaveData` and never reads from disk. The
  mutation cannot flip these rows' predicates without a Phase 04+
  scenario redesign that explicitly exercises save/load round-trip
  via a runner extension. The canary reports 0 flips for these two
  rows honestly; we keep the subject-specific target rather than
  redirecting to an unrelated subsystem.

| Scenario | file:line | Mutation token | Rationale |
| --- | --- | --- | --- |
| `ai_idle_wander_scen9301` | `src/gameplay/walker_combat.cpp:282` | `kMut_walker_ai_wander` | Forces the walker_combat dispatch site to pass tempdamage=0 into do_combat_damage; in AI-driven combat scenarios the target takes no damage so AI walkers don't lose HP. Distinct from kMut_combat_damage (line 189) which mutates the target HP decrement inside the do_combat_damag... |
| `combat_attack_scen99` | `src/gameplay/walker_combat.cpp:189` | `kMut_combat_damage` | Zeroes the per-hit damage applied to combat targets in walker::do_combat_damage; for any scenario that actually exercises melee combat this leaves the target alive and flips WalkerDiedByFinal and team-alive predicates. |
| `special_archmage_scen123` | `src/gameplay/families/family_archmage.cpp:506` | `kMut_special_archmage_do_special` | Descriptor sets archmage do_special to nullptr while still referencing the function symbol (silences -Wunused-function). Any scenario that actually invokes the archmage special sees the gating play_sound suppressed, flipping EventKindExactly(play_sound, 0) / LevelDoneEquals pr... |
| `special_cleric_scen124` | `src/gameplay/families/family_cleric.cpp:348` | `kMut_special_cleric_do_special` | Descriptor neuters cleric heal/raise specials by setting do_special to nullptr; scenarios that invoke a cleric special lose the resulting events / heals, flipping EventKindExactly predicates. |
| `special_mage_scen126` | `src/gameplay/families/family_mage.cpp:300` | `kMut_special_mage_do_special` | Descriptor neuters mage teleport/warp/freeze specials; scenarios that fire a mage special see no resulting events, flipping EventKindExactly predicates. |
| `special_thief_scen789` | `src/gameplay/families/family_thief.cpp:212` | `kMut_special_thief_do_special` | Descriptor neuters thief bomb/cloak/taunt specials; scenarios that fire a thief special see no resulting events, flipping EventKindExactly predicates. |
| `effect_bomb_lifetime_scen99` | `src/gameplay/effect.cpp:91` | `kMut_effect_lifetime` | Cancels the end-of-animation death in effect::act() so effects never expire; bomb/chain scenarios that rely on effects winding down see a residual effect count and flip EffectFamilyCount / dependent walker-death predicates. |
| `effect_chain_scen9410` | `src/gameplay/effect.cpp:91` | `kMut_effect_lifetime` | Cancels the end-of-animation death in effect::act() so effects never expire; bomb/chain scenarios that rely on effects winding down see a residual effect count and flip EffectFamilyCount / dependent walker-death predicates. |
| `summon_druid_pet_scen950` | `src/gameplay/families/family_druid.cpp:184` | `kMut_summon_druid_do_special` | Descriptor neuters druid summon-faerie special; the faerie pet never appears, flipping LevelDoneEquals(2) downstream and any predicate that counts the summoned child. |
| `scoring_after_combat_scen99` | `src/gameplay/walker_combat.cpp:189` | `kMut_combat_damage` | Zeroes the per-hit damage applied to combat targets in walker::do_combat_damage; for any scenario that actually exercises melee combat this leaves the target alive and flips WalkerDiedByFinal and team-alive predicates. |
| `save_roundtrip_scen99` | `src/resources/save_data.cpp:107` | `kMut_save_corrupt` | Save header claims version 0 (below any supported save format); the round-trip load refuses the file and the post-load world is empty, flipping WalkerOfTeamAlive(team=0,1,1) and LevelDoneEquals(2). |
| `exit_trigger_scen9302` | `src/gameplay/sim_input_handler.cpp:335` | `kMut_exit_neuter` | Force-zeroes the east/west walk vector at the sim_input_handler movement dispatch site (distinct line from kMut_smoke_inputs_no_move which mutates the walkstep call site at line 340); exit_trigger scenarios rely on K_RIGHT translation to reach the exit tile, and zeroing walkx ... |
| `tick_cadence_scen9301` | `src/gameplay/walker_combat.cpp:282` | `kMut_walker_ai_wander` | Forces the walker_combat dispatch site to pass tempdamage=0 into do_combat_damage; in AI-driven combat scenarios the target takes no damage so AI walkers don't lose HP. Distinct from kMut_combat_damage (line 189) which mutates the target HP decrement inside the do_combat_damag... |
| `rng_seed_stable_scen99` | `src/resources/save_data.cpp:107` | `kMut_save_corrupt` | Save header claims version 0 (below any supported save format); the round-trip load refuses the file and the post-load world is empty, flipping WalkerOfTeamAlive(team=0,1,1) and LevelDoneEquals(2). |
| `scripted_input_scen9301` | `src/gameplay/walker_combat.cpp:282` | `kMut_walker_ai_wander` | Forces the walker_combat dispatch site to pass tempdamage=0 into do_combat_damage; in AI-driven combat scenarios the target takes no damage so AI walkers don't lose HP. Distinct from kMut_combat_damage (line 189) which mutates the target HP decrement inside the do_combat_damag... |
| `snapshot_dirty_bits_scen9301` | `src/gameplay/game_world.cpp:1355` | `kMut_snapshot_dirty` | state_dump.cpp (the original Phase 01 target) lives under tests/parity/ which the canary refuses to mutate; the next-best upstream subject is the game_world per-tick level_done assignment that flows straight into the snapshot dump. A static-counter lambda persists across run_s... |
| `smoke_nonempty_scen99` | `src/gameplay/walker_combat.cpp:89` | `kMut_smoke_score_event` | Re-labels score_change emissions to EventKind::None; the canonical event-kind field flips so EventKindAtLeast(score_change,2) reads 0 occurrences, flipping that predicate in both smoke rows. |
| `smoke_nonempty_scen99_inputs` | `src/gameplay/sim_input_handler.cpp:340` | `kMut_smoke_inputs_no_move` | Drops the input-driven walkstep delta so the player walker no longer steps east when K_RIGHT is held; flips WalkerPositionMoved(SOLDIER,240,0). |
| `family_soldier_scen99` | `src/gameplay/families/family_soldier.cpp:170` | `kMut_family_soldier_init` | Cranks SOLDIER HP so soldier survives the sparring partner; flips WalkerOfTeamAlive(team=0,0,0) and WalkerDiedByFinal(SOLDIER). |
| `family_elf_scen99` | `src/gameplay/families/family_elf.cpp:121` | `kMut_family_elf_init` | Cranks ELF HP so elf survives; flips WalkerOfTeamAlive(team=1,1,1) (sparring soldier dies) and WalkerDiedByFinal(ELF). |
| `family_archer_scen99` | `src/gameplay/families/family_archer.cpp:121` | `kMut_family_archer_init` | Cranks ARCHER HP so archer survives; flips WalkerDiedByFinal(ARCHER). |
| `family_mage_scen99` | `src/gameplay/families/family_mage.cpp:281` | `kMut_family_mage_init` | Cranks MAGE HP so mage survives; flips WalkerOfTeamAlive(team=1,1,1) and WalkerDiedByFinal(MAGE). |
| `family_skeleton_scen99` | `src/gameplay/families/family_skeleton.cpp:60` | `kMut_family_skeleton_init` | Cranks SKELETON HP; flips WalkerOfTeamAlive(team=1,1,1) and WalkerDiedByFinal(SKELETON). |
| `family_cleric_scen99` | `src/gameplay/families/family_cleric.cpp:329` | `kMut_family_cleric_init` | Cranks CLERIC HP; flips WalkerFamilyCount(CLERIC,1,1) (one extra alive) and WalkerDiedByFinal(CLERIC). |
| `family_fireelemental_scen99` | `src/gameplay/families/family_fire_elemental.cpp:94` | `kMut_family_fireelemental_init` | Cranks FIREELEMENTAL HP; flips WalkerDiedByFinal(FIREELEMENTAL). |
| `family_faerie_scen99` | `src/gameplay/families/family_faerie.cpp:32` | `kMut_family_faerie_init` | Cranks FAERIE HP; flips WalkerDiedByFinal(FAERIE). |
| `family_slime_scen99` | `src/gameplay/families/family_slime.cpp:155` | `kMut_family_slime_init` | SLIME HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(SLIME,1) and WalkerOfTeamAlive(team=0,1,1). |
| `family_small_slime_scen99` | `src/gameplay/families/family_slime.cpp:215` | `kMut_family_small_slime_init` | Cranks SMALL_SLIME HP; flips WalkerDiedByFinal(SMALL_SLIME). |
| `family_medium_slime_scen99` | `src/gameplay/families/family_slime.cpp:275` | `kMut_family_medium_slime_init` | Cranks MEDIUM_SLIME HP; flips WalkerFamilyCount(SMALL_SLIME,1,1) (medium never splits) and WalkerDiedByFinal(MEDIUM_SLIME). |
| `family_thief_scen99` | `src/gameplay/families/family_thief.cpp:193` | `kMut_family_thief_init` | Cranks THIEF HP; flips WalkerDiedByFinal(THIEF). |
| `family_ghost_scen99` | `src/resources/gloader.cpp:608` | `kMut_family_ghost_init` | Forces every gloader-spawned walker to be tagged FAMILY_SOLDIER; in any build env the GHOST walker is dumped as SOLDIER, so WalkerFamilyCount(GHOST,1,1) drops to 0 and WalkerAliveAtFinal(GHOST,1) loses its quorum. |
| `family_druid_scen99` | `src/gameplay/families/family_druid.cpp:165` | `kMut_family_druid_init` | Cranks DRUID HP; flips WalkerDiedByFinal(DRUID). |
| `family_orc_scen99` | `src/gameplay/families/family_orc.cpp:130` | `kMut_family_orc_init` | Cranks ORC HP; flips WalkerDiedByFinal(ORC). |
| `family_big_orc_scen99` | `src/gameplay/families/family_big_orc.cpp:31` | `kMut_family_big_orc_init` | BIG_ORC HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(BIG_ORC,1) and WalkerOfTeamAlive(team=0,1,1). |
| `family_barbarian_scen99` | `src/gameplay/families/family_barbarian.cpp:77` | `kMut_family_barbarian_init` | BARBARIAN HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(BARBARIAN,1) and WalkerOfTeamAlive(team=0,1,1). |
| `family_archmage_scen99` | `src/gameplay/families/family_archmage.cpp:487` | `kMut_family_archmage_init` | ARCHMAGE HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(ARCHMAGE,1) and WalkerOfTeamAlive(team=0,1,1). |
| `family_golem_scen99` | `src/gameplay/families/family_golem.cpp:30` | `kMut_family_golem_init` | GOLEM HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(GOLEM,1) and WalkerOfTeamAlive(team=0,1,1). |
| `family_giant_skeleton_scen99` | `src/gameplay/families/family_giant_skeleton.cpp:22` | `kMut_family_giant_skeleton_init` | GIANT_SKELETON HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(GIANT_SKELETON,1) and WalkerOfTeamAlive(team=0,1,1). |
| `family_tower1_scen99` | `src/gameplay/families/family_tower1.cpp:22` | `kMut_family_tower1_init` | Cranks TOWER1 HP; flips WalkerDiedByFinal(TOWER1). |
