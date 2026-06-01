# Parity Test Suite — Coverage Manifest

Final coverage manifest for the parity hardening effort. Captured after
Phase 11 (final sweep). The scenario table at
`tests/parity/scenario_table.h` and the goldens in
`tests/parity/golden/*.json` are the canonical source of truth; this
document summarises what is bound where.

## New Scenario Added During Hardening

| Scenario id                    | Family    | Inputs                            | Purpose |
|--------------------------------|-----------|-----------------------------------|---------|
| `treasure_exit_pickup_scen99`  | EXIT      | `kInputsTreasureExitPickup`       | EXIT treasure family pickup coverage; asserts walker-on-exit state transitions, score deltas, and pickup event emission. Was missing from the original 13-treasure batch. |

This brings the total scenario count to **134**.

## Enhanced Predicate Bindings Per Entity Category

The table below lists, per category, the predicates each scenario now
carries beyond the baseline `TickReached(N)` floor. Every cell reflects
the post-hardening state checked in by the final commits of phases 1–11.

### Walker Families (21 scenarios)

Each `family_<X>_scen99`:
- `WalkerFamilyCount(FAMILY_<X>, n, n)`
- `WalkerOfTeamAlive(team=0, lo, hi)`
- `WalkerPositionMoved(FAMILY_<X>, x, y)`
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- **`WalkerHpRangeAtFinalTick(FAMILY_<X>, min, max)`**
  *(new in Phase 2)* — or `WalkerDiedByFinal(FAMILY_<X>)` if the family
  dies before tick 600.

Families covered: soldier, elf, archer, mage, skeleton, cleric,
fireelemental, faerie, slime, small_slime, medium_slime, thief, ghost,
druid, orc, big_orc, barbarian, archmage, golem, giant_skeleton, tower1.

### Weapon Emissions (20 scenarios)

Each `weapon_<X>_emission_scen99`:
- `WalkerFamilyCount(FAMILY_<wielder>, …)`
- `WeaponFamilyCount(FAMILY_<X>, …)` *(structural)*
- `EventKindAtLeast(/*play_sound*/1, ≥1)` *(tightened in Phase 1)*
- **Consequence predicate** *(added in Phase 7)*:
  `WalkerHpRangeAtFinalTick` / `WalkerDiedByFinal` for the impact target
  or `WeaponFamilyEmitted` confirming the projectile lifetime.

Weapons covered: knife, rock, arrow, fireball, tree, meteor, sprinkle,
bone, fire_arrow, lightning, glow, wave, wave2, wave3, circle_protection,
hammer, door, boulder, blood, blob.

### Effect Emissions (15 scenarios)

Each `effect_<X>_emission_scen99` (and the four `effect_<X>_lifetime`
sister rows):
- `WalkerFamilyCount(FAMILY_<emitter>, …)`
- `EffectFamilyCount(FAMILY_<X>, lo, hi)`
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- **Consequence predicate** *(added in Phase 8)*:
  `EffectFamilyCount(FAMILY_<X>, ≥1)` on a tightened range, or
  `WalkerHpRangeAtFinalTick` / `WalkerDiedByFinal` for target HP loss.

Effects covered: bomb, boomerang, chain, cloud, door_open,
fireball_secondary, glow, healing, meteor_explosion, poison, scare,
shield, spark, sprinkle, summon_indicator.

### Generators (4 scenarios)

Each `generator_<X>_scen99` *(enriched in Phase 8)*:
- `WalkerFamilyCount(FAMILY_<generator>, 1, 1)`
- `WalkerFamilyCount(FAMILY_<spawned>, lo, hi)` *(cross-family
  consequence)*
- `EventKindAtLeast(/*play_sound*/1, ≥1)`
- `WalkerOfTeamAlive(team, lo, hi)`

Generators covered: spawner, lever, fountain, gravestone.

### Treasure Pickups (13 scenarios + new EXIT row)

Each `treasure_<X>_pickup_scen99`:
- `WalkerFamilyCount(FAMILY_PLAYER, …)`
- `TreasureFamilyRemovedFromOblist(FAMILY_<X>)` or
  `TreasureFamilyOfOrderRemovedFromOblist(...)`
- `TreasurePickup(FAMILY_<X>)` event
- `EventKindAtLeast(/*play_sound*/1, ≥1)` *(score_change ≥1 for
  score-bearing pickups)*

Pickups covered: gold, food, magic_potion, lifegem, key, scroll,
exp_potion, drumstick, ring, lifegem_large, exp_potion_large,
magic_potion_large, special_scroll, and **EXIT** (new in Phase 9).

### Event Emissions (5 scenarios)

Each `event_<X>_scen99`:
- `EventKindExactly(<kind>, n)` or `EventKindAtLeast(<kind>, ≥1)`
- `WalkerFamilyCount` for the emitter
- `EventKindAtLeast(/*play_sound*/1, ≥1)`

### Special Abilities (42 scenarios)

The four core specials (archmage / cleric / mage / thief base rows) and
38 `special_<family>_<n>_scen99` companion rows each carry:
- `WalkerFamilyCount(FAMILY_<wielder>, …)`
- `WalkerOfTeamAlive(team, lo, hi)`
- `WalkerPositionMoved(FAMILY_<wielder>, x, y)`
- `EventKindAtLeast(/*play_sound*/1, ≥1)` *(every floor > 0 after
  Phases 3 and 4)*
- **Consequence predicate** *(added across Phases 5 and 6)* — at least
  one of: cross-family `WalkerFamilyCount`, `WeaponFamilyEmitted`,
  `WalkerHpRangeAtFinalTick`, `WalkerDiedByFinal`, `WalkerPositionMoved`
  for a non-wielder target, or `WalkerAliveAtFinal` for a summon.

### Smoke and Singleton Scenarios

- `smoke_nonempty_scen99` and `smoke_nonempty_scen99_inputs`: 6
  predicates each, covering walker presence + RNG + event emission.
- `smoke_empty_scen99`: Invariant mode, no master golden, predicate-free
  by design.
- `snapshot_dirty_bits_scen9301`: Invariant mode dumper-determinism
  probe.
- Singleton arenas (`ai_idle_wander`, `combat_attack`, `summon_*`,
  `scoring_*`, `save_roundtrip`, `exit_trigger`, `tick_reached`,
  `rng_seed_stable`, `scripted_event`, `coverage_catchall`): each carries
  4–6 predicates, with at least one consequence predicate where
  applicable.

## Final Behavioral Coverage Status Per Entity

| Entity category | Scenarios | Has structural pred. | Has consequence pred. | Has labelled widening (where required) | Status |
|-----------------|----------:|---------------------:|----------------------:|---------------------------------------:|--------|
| Walker family   |        21 |                  21  |                  21  |                                    — | OK |
| Weapon          |        20 |                  20  |                  20  |                                  yes | OK |
| Effect          |        15 |                  15  |                  15  |                                  yes | OK |
| Treasure        |        13 |                  13  |                  13  |                                  yes | OK |
| Generator       |         4 |                   4  |                   4  |                                  yes | OK |
| Event           |         5 |                   5  |                   5  |                                    — | OK |
| Special ability |        42 |                  42  |                  42  |                                  yes | OK |
| Smoke/singleton |        13 |                  11  |                  11  |                                    — | OK |
| Invariant       |         2 |                   —  |                   —  |                                    — | OK (by design) |

Every SemanticParity scenario now has:
- a master golden at `tests/parity/golden/<id>.json`,
- at least 4 predicates (≥3 non-`TickReached`),
- at least one structural predicate,
- at least one consequence predicate where the entity category demands
  one,
- a justification label on every widened range.

A failure of any of these invariants is now a hard build/test failure
via either `test_parity_depth_gates.cpp` or
`test_parity_scenarios.cpp:108` (the converted `ADD_FAILURE`).
