# Parity Test Suite — Coverage Manifest

Coverage manifest for the parity hardening effort, refreshed after
**Coverage Pass 2** (gap-fill). The scenario table at
`tests/parity/scenario_table.h` and the goldens in
`tests/parity/golden/*.json` are the canonical source of truth; this
document summarises what is bound where. The suite now stands at
**156 scenarios** (154 `SemanticParity` + 2 `Invariant`, 155 goldens).

## Scenario-Count Lineage

| Milestone                              | Total scenarios |
|----------------------------------------|----------------:|
| End of Phase 11 (final sweep)          |             134 |
| `treasure_exit_pickup_scen99` (Phase 9)| (included in 134) |
| **Coverage Pass 2 (gap-fill, +22)**    |         **156** |

Coverage Pass 2 added 22 `SemanticParity` scenarios across the gap-fill
phases (`02-generator-saturation` … `09-midcombat-state`), targeting entity
behaviours and runtime paths that the Phase-1–11 batches under-exercised:
status/timer effects, summon lifetimes, generator saturation, weapon
trajectories, multi-target effect emission, the input pipeline, multiplayer
teams, level withdrawal, and mid-combat state.

## Gap-Fill Scenarios Added in Coverage Pass 2

The 22 new scenarios, grouped by the Coverage Pass 2 phase categories. The
"category total" column is the post-gap-fill scenario count for that
category across the whole suite (pre-existing rows + the new gap-fill rows).

### `status_timer` — status/timer effects (category total: 4)

All four scenarios are new in Coverage Pass 2 (phase `05-effect-timers`).

| Scenario id                     | Ticks | Consequence asserted |
|---------------------------------|------:|----------------------|
| `enemy_freeze_mage_scen99`      |   150 | mage freeze holds the archer in place — `WalkerPositionMoved(FAMILY_ARCHER, …)` |
| `invisibility_thief_scen99`     |   150 | invisible thief survives — `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1300, 2500)` |
| `speed_potion_movement_scen99`  |    60 | speed-potion pickup + extended travel — `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_SPEED_POTION, 2)`, `WalkerPositionMoved` |
| `invulnerable_potion_scen99`    |   250 | invulnerable soldier keeps full HP — `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11500, 12000)` |

### `summon` — summon lifetimes (category total: 3)

Two new gap-fill rows join the pre-existing `summon_druid_pet_scen950`.

| Scenario id                                | Ticks | Consequence asserted |
|--------------------------------------------|------:|----------------------|
| `summon_lifetime_faerie_scen99`            |   650 | summoned faerie expires by the final tick — `WalkerDiedByFinal(FAMILY_FAERIE)` |
| `summon_lifetime_decrement_faerie_scen99`  |   650 | lifetime countdown retires the faerie — `WalkerDiedByFinal(FAMILY_FAERIE)` |

### `generator` — generator saturation (category total: 5)

One new gap-fill row joins the four pre-existing generator emission rows.

| Scenario id                  | Ticks | Consequence asserted |
|------------------------------|------:|----------------------|
| `generator_saturation_scen99`|  2500 | sustained spawning saturates the arena — `WalkerFamilyCount(FAMILY_MAGE, 3, 30)`, `WalkerOfTeamAlive(1, 3, 40)` |

### `weapon` — weapon trajectories (category total: 23)

Three new gap-fill rows join the 20 weapon emission rows.

| Scenario id                       | Ticks | Consequence asserted |
|-----------------------------------|------:|----------------------|
| `weapon_rock_slot2_emit_scen99`   |    30 | secondary-slot rock connects — `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 2000, 9000)`, `score_change` |
| `weapon_boomerang_return_scen99`  |    80 | boomerang return path damages on the way back — `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 14000)` |
| `weapon_exploding_boulder_scen99` |    60 | boulder explodes, thinning the soldier line — `WalkerFamilyCount(FAMILY_SOLDIER, 0, 3)`, `score_change` |

### `effect` — multi-target / emission effects (category total: 19)

Four new gap-fill rows join the 13 emission rows plus the
`effect_bomb_lifetime` / `effect_chain` sister rows.

| Scenario id                            | Ticks | Consequence asserted |
|----------------------------------------|------:|----------------------|
| `effect_heartburst_multitarget_scen99` |    30 | heartburst damages a cluster — `WalkerFamilyCount(FAMILY_SOLDIER, 0, 4)`, `WalkerHpRangeAtFinalTick` |
| `effect_poison_cloud_emit_scen99`      |    45 | poison cloud erodes HP over time — `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 0, 15000)` |
| `effect_protection_emit_scen99`        |    25 | druid emits the protection circle — `WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION)` |
| `effect_bomb_timer_scen99`             |    30 | bomb timer detonates within budget — `WalkerOfTeamAlive(0, 2, 3)` |

### `input` — input pipeline (category total: 4)

All four scenarios are new in Coverage Pass 2 (phase `06-input-pipeline`).

| Scenario id                          | Ticks | Consequence asserted |
|--------------------------------------|------:|----------------------|
| `input_diagonal_movement_scen99`     |    80 | diagonal key mask drives diagonal travel — `WalkerPositionMoved(FAMILY_SOLDIER, 175, 175)` |
| `input_hold_fire_search_scen99`      |   150 | held fire engages the auto-search target — `EventKindAtLeast(play_sound, 5)` |
| `input_switch_char_scen99`           |   150 | character switch re-targets fire — `WeaponFamilyEmitted(FAMILY_ARROW)` |
| `input_special_switch_wrap_scen99`   |   150 | special-switch wraps to a palette-changing special — `EventKindAtLeast(set_palette, 1)` |

### `multiplayer` — multiplayer teams (category total: 1)

| Scenario id                      | Ticks | Consequence asserted |
|----------------------------------|------:|----------------------|
| `multiplayer_two_teams_scen99`   |    44 | two human teams fight — `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 10000)` |

### `level_transition` — level withdrawal (category total: 2)

One new gap-fill row joins the pre-existing `exit_trigger_scen9302`.

| Scenario id              | Ticks | Consequence asserted |
|--------------------------|------:|----------------------|
| `level_withdraw_scen99`  |   200 | withdraw flow advances the level — `LevelDoneEquals(2)`, `withdraw_to_level`, `request_exit_confirmation` events |

### `midcombat` — mid-combat state (category total: 2)

Both scenarios are new in Coverage Pass 2 (phase `09-midcombat-state`).

| Scenario id                          | Ticks | Consequence asserted |
|--------------------------------------|------:|----------------------|
| `midcombat_partial_hp_scen99`        |    80 | combat leaves a soldier at partial HP — `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 500, 11500)` |
| `consumable_inventory_state_scen99`  |    60 | two consumables are spent mid-fight — two `TreasureFamilyOfOrderRemovedFromOblist` (drumstick, magic_potion) |

**Gap-fill total:** 4 + 2 + 1 + 3 + 4 + 4 + 1 + 1 + 2 = **22** new scenarios.

## Enhanced Predicate Bindings Per Entity Category

The table below lists, per category, the predicates each scenario carries
beyond the baseline `TickReached(N)` floor. Membership reflects the current
`scenario_table.h`.

### Walker Families (21 scenarios)

Each `family_<X>_scen99`:
- `WalkerFamilyCount(FAMILY_<X>, n, n)`
- `WalkerOfTeamAlive(team=0, lo, hi)`
- `WalkerPositionMoved(FAMILY_<X>, x, y)`
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- **`WalkerHpRangeAtFinalTick(FAMILY_<X>, min, max)`**
  — or `WalkerDiedByFinal(FAMILY_<X>)` if the family dies before the final
  tick.

Families covered: soldier, elf, archer, mage, skeleton, cleric,
fireelemental, faerie, slime, small_slime, medium_slime, thief, ghost,
druid, orc, big_orc, barbarian, archmage, golem, giant_skeleton, tower1.

### Weapon Emissions (23 scenarios)

Each `weapon_<X>_emission_scen99`:
- `WalkerFamilyCount(FAMILY_<wielder>, …)`
- `WeaponFamilyCount(FAMILY_<X>, …)` *(structural)*
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- **Consequence predicate**:
  `WalkerHpRangeAtFinalTick` / `WalkerDiedByFinal` for the impact target
  or `WeaponFamilyEmitted` confirming the projectile lifetime.

Weapons covered: knife, rock, arrow, fireball, tree, meteor, sprinkle,
bone, blood, blob, fire_arrow, lightning, glow, wave, wave2, wave3,
circle_protection, hammer, door, boulder, plus the three Coverage Pass 2
trajectory rows (rock slot-2, boomerang return, exploding boulder).

### Effect Emissions (19 scenarios)

Each `effect_<X>_emission_scen99` (plus the `effect_bomb_lifetime` /
`effect_chain` sister rows and the four Coverage Pass 2 rows):
- `WalkerFamilyCount(FAMILY_<emitter>, …)`
- `EffectFamilyCount(FAMILY_<X>, lo, hi)` or a `WalkerFamilyCount` on the
  target population
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- **Consequence predicate**:
  `WalkerHpRangeAtFinalTick` / `WalkerDiedByFinal` for target HP loss, or
  `WeaponFamilyEmitted` for an effect that spawns a projectile.

Effects covered: expand, ghost_scare, bomb, explosion, flash, magic_shield,
knife_back, boomerang, cloud, marker, chain, door_open, hit (emission rows),
plus bomb_lifetime, chain (sister rows), and heartburst_multitarget,
poison_cloud, protection, bomb_timer (Coverage Pass 2).

### Generators (5 scenarios)

Each `generator_<X>_scen99`:
- `WalkerFamilyCount(FAMILY_<generator>, …)`
- `WalkerFamilyCount(FAMILY_<spawned>, lo, hi)` *(cross-family
  consequence)*
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- `WalkerOfTeamAlive(team, lo, hi)`

Generators covered: tent, tower, bones, treehouse, and the Coverage Pass 2
`generator_saturation` row (2500-tick sustained-spawn stress).

### Treasure Pickups (13 scenarios)

Each `treasure_<X>_pickup_scen99`:
- `WalkerFamilyCount(FAMILY_PLAYER, …)`
- `TreasureFamilyRemovedFromOblist(FAMILY_<X>)` or
  `TreasureFamilyOfOrderRemovedFromOblist(...)`
- `TreasurePickup(FAMILY_<X>)` / pickup event
- `EventKindAtLeast(/*play_sound*/1, ≥1)` *(score_change ≥1 for
  score-bearing pickups)*

Pickups covered: stain, drumstick, gold_bar, silver_bar, magic_potion,
invis_potion, invulnerable_potion, flight_potion, teleporter, life_gem,
key, speed_potion, exit.

### Event Emissions (5 scenarios)

Each `event_<X>_emission_scen99`:
- `EventKindExactly(<kind>, n)` or `EventKindAtLeast(<kind>, ≥1)`
- `WalkerFamilyCount` for the emitter
- `EventKindAtLeast(/*play_sound*/1, ≥1)`

Events covered: notification, set_palette, request_redraw, end_game,
set_end.

### Special Abilities (42 scenarios)

The four core specials (archmage / cleric / mage / thief base rows) and
38 `special_<family>_<n>_scen99` companion rows each carry:
- `WalkerFamilyCount(FAMILY_<wielder>, …)`
- `WalkerOfTeamAlive(team, lo, hi)`
- `WalkerPositionMoved(FAMILY_<wielder>, x, y)`
- `EventKindAtLeast(/*play_sound*/1, ≥1)` *(every floor > 0)*
- **Consequence predicate** — at least one of: cross-family
  `WalkerFamilyCount`, `WeaponFamilyEmitted`, `WalkerHpRangeAtFinalTick`,
  `WalkerDiedByFinal`, `WalkerPositionMoved` for a non-wielder target, or
  `WalkerAliveAtFinal` for a summon.

### Coverage Pass 2 Categories (status_timer / input / multiplayer / level_transition / midcombat / summon)

See the **Gap-Fill Scenarios Added in Coverage Pass 2** section above for the
per-scenario predicate bindings. Each carries a structural
`WalkerFamilyCount` plus at least one consequence predicate matched to the
behaviour under test (position movement, HP range, treasure removal,
`LevelDoneEquals`, or a withdrawal/exit event).

### Smoke and Singleton Scenarios

- `smoke_nonempty_scen99` and `smoke_nonempty_scen99_inputs`: 6
  predicates each, covering walker presence + RNG + event emission.
- `smoke_empty_scen99`: Invariant mode, no master golden, predicate-free
  by design.
- `snapshot_dirty_bits_scen9301`: Invariant mode dumper-determinism
  probe.
- Singleton arenas (`ai_idle_wander`, `combat_attack`, `scoring_after_combat`,
  `save_roundtrip`, `tick_cadence`, `rng_seed_stable`, `scripted_input`,
  `coverage_catchall`): each carries 4–6 predicates, with at least one
  consequence predicate where applicable.

## Final Behavioral Coverage Status Per Entity

| Entity category   | Scenarios | Has structural pred. | Has consequence pred. | Has labelled widening (where required) | Status |
|-------------------|----------:|---------------------:|----------------------:|---------------------------------------:|--------|
| Walker family     |        21 |                  21  |                  21  |                                  yes | OK |
| Weapon            |        23 |                  23  |                  23  |                                  yes | OK |
| Effect            |        19 |                  19  |                  19  |                                  yes | OK |
| Treasure          |        13 |                  13  |                  13  |                                  yes | OK |
| Generator         |         5 |                   5  |                   5  |                                  yes | OK |
| Event             |         5 |                   5  |                   5  |                                    — | OK |
| Special ability   |        42 |                  42  |                  42  |                                  yes | OK |
| status_timer      |         4 |                   4  |                   4  |                                  yes | OK |
| input             |         4 |                   4  |                   4  |                                    — | OK |
| summon            |         3 |                   3  |                   3  |                                    — | OK |
| level_transition  |         2 |                   2  |                   2  |                                    — | OK |
| midcombat         |         2 |                   2  |                   2  |                                  yes | OK |
| multiplayer       |         1 |                   1  |                   1  |                                    — | OK |
| Smoke/singleton   |        10 |                  10  |                   7  |                                    — | OK |
| Invariant         |         2 |                   —  |                   —  |                                    — | OK (by design) |

Every SemanticParity scenario now has:
- a master golden at `tests/parity/golden/<id>.json`,
- at least 4 predicates (≥3 non-`TickReached`),
- at least one structural predicate,
- at least one consequence predicate where the entity category demands
  one,
- a justification label on every widened range.

A failure of any of these invariants is now a hard build/test failure
via either `test_parity_coverage_gate.cpp` or
`test_parity_scenarios.cpp:108` (the converted `ADD_FAILURE`).
