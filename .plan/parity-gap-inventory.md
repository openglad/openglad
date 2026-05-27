# Parity Gap Inventory

Comprehensive analysis of every scenario in `kScenarios[]` (133 rows),
every required coverage entity (122 dimensions), and all known gaps in
the parity test suite.

Generated: 2026-05-27
Source: `tests/parity/scenario_table.h`, `tests/parity/coverage_targets.h`,
`tests/parity/fact_predicate.h`

---

## 1. Scenario Table Summary

| Category | Count |
|----------|-------|
| Core/infrastructure scenarios | 19 |
| Walker family completeness (21 families) | 21 |
| Coverage catchall | 1 |
| Treasure pickup (12 of 13 families) | 12 |
| Weapon emission (20 families) | 20 |
| Effect emission (13 families) | 13 |
| Generator emission (4 families) | 4 |
| Event-kind emission (5 of 9 kinds) | 5 |
| Per-family per-slot special cast | 38 |
| **Total** | **133** |

Compare modes: 131 SemanticParity, 2 Invariant
(`smoke_empty_scen99` and `snapshot_dirty_bits_scen9301`).

---

## 2. Per-Scenario Classification

Classification rules:
- **Behavioral count** = total predicates minus TickReached predicates
- **STRONG**: 5+ behavioral, no trivially-true `EventKindAtLeast(*, 0)`
- **MEDIUM**: 3-4 behavioral, no trivially-true
- **WEAK**: 1-2 behavioral, or any `EventKindAtLeast(*, 0)` present
- **TRIVIAL**: 0 behavioral (Invariant rows with no predicates)

### 2.1 Core/Infrastructure Scenarios (19 rows)

| # | ID | Total | Behav | Kinds Used | Floor=0? | Class |
|---|------|-------|-------|------------|----------|-------|
| 1 | `ai_idle_wander_scen9301` | 4 | 3 | WFC,WTA,WHR | N | MEDIUM |
| 2 | `combat_attack_scen99` | 4 | 3 | WFC,WTA,WHR | N | MEDIUM |
| 3 | `special_archmage_scen123` | 4 | 3 | WFC,WTA,WPM | N | MEDIUM |
| 4 | `special_cleric_scen124` | 4 | 3 | WFC,WPM,EFC | N | MEDIUM |
| 5 | `special_mage_scen126` | 4 | 3 | WFC,WTA,WPM | N | MEDIUM |
| 6 | `special_thief_scen789` | 4 | 3 | WFC,WTA,EKA | N | MEDIUM |
| 7 | `effect_bomb_lifetime_scen99` | 4 | 3 | LDE,WTA,WDB | N | MEDIUM |
| 8 | `effect_chain_scen9410` | 5 | 4 | WFC,WTA,WHR,EKA | N | MEDIUM |
| 9 | `summon_druid_pet_scen950` | 4 | 3 | WFC,WTA,WHR | N | MEDIUM |
| 10 | `scoring_after_combat_scen99` | 5 | 4 | WFC,WTA,WHR,EKA | N | MEDIUM |
| 11 | `save_roundtrip_scen99` | 4 | 3 | WFC,WTA,WHR | N | MEDIUM |
| 12 | `exit_trigger_scen9302` | 6 | 5 | WFC(x2),WTA,WPM,TFOR | N | **STRONG** |
| 13 | `tick_cadence_scen9301` | 4 | 3 | WFC,WTA,WHR | N | MEDIUM |
| 14 | `rng_seed_stable_scen99` | 4 | 3 | WFC,WTA,WHR | N | MEDIUM |
| 15 | `scripted_input_scen9301` | 6 | 5 | LDE,WFC,WTA,EKE(x2) | N | **STRONG** |
| 16 | `snapshot_dirty_bits_scen9301` | 0 | 0 | (Invariant) | - | TRIVIAL |
| 17 | `smoke_empty_scen99` | 0 | 0 | (Invariant) | - | TRIVIAL |
| 18 | `smoke_nonempty_scen99` | 7 | 6 | WFC(x2),WTA(x2),EKA(x2) | N | **STRONG** |
| 19 | `smoke_nonempty_scen99_inputs` | 4 | 3 | WFC,WTA,WPM | N | MEDIUM |

### 2.2 Walker Family Completeness (21 rows)

All 21 rows share the same predicate shape: `TickReached(600)`,
`WalkerFamilyCount(FAMILY_X, 1, 1)`, `WalkerOfTeamAlive(1, 0, 1)`,
`WalkerPositionMoved(FAMILY_X, ...)`, `EventKindAtLeast(play_sound, 1)`.

| # | ID | Total | Behav | Floor=0? | Class |
|---|------|-------|-------|----------|-------|
| 20-40 | `family_*_scen99` (x21) | 5 | 4 | N | MEDIUM |

Exception: `coverage_catchall_scen99` (row 41) has 4 total / 3 behavioral
(WFC, WTA, WAF) — MEDIUM.

### 2.3 Treasure Pickup (12 rows)

All rows use `TickReached(150)`, `WalkerPositionMoved(SOLDIER, 144, 120)`,
`TreasureFamilyOfOrderRemovedFromOblist(FAMILY_X, kOrderTreasure)`,
plus optional stat predicates.

| # | ID | Total | Behav | Extra Predicates | Floor=0? | Class |
|---|------|-------|-------|------------------|----------|-------|
| 42 | `treasure_stain_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 43 | `treasure_drumstick_pickup_scen99` | 4 | 3 | WPM,TFOR,WHR | N | MEDIUM |
| 44 | `treasure_gold_bar_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 45 | `treasure_silver_bar_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 46 | `treasure_magic_potion_pickup_scen99` | 4 | 3 | WPM,TFOR,WHR | N | MEDIUM |
| 47 | `treasure_invis_potion_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 48 | `treasure_invulnerable_potion_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 49 | `treasure_flight_potion_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 50 | `treasure_teleporter_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |
| 51 | `treasure_life_gem_pickup_scen99` | 4 | 3 | WPM,TFOR,WHR | N | MEDIUM |
| 52 | `treasure_key_pickup_scen99` | 4 | 3 | WPM,TFOR,WKA | N | MEDIUM |
| 53 | `treasure_speed_potion_pickup_scen99` | 3 | 2 | WPM,TFOR | N | WEAK |

### 2.4 Weapon Emission (20 rows)

All rows contain `TickReached`, `WalkerFamilyCount`, `EventKindAtLeast`,
`WeaponFamilyEmitted`. The critical gap: **18 of 20** have
`EventKindAtLeast(play_sound, 0)` (trivially true).

| # | ID | Total | Behav | Floor=0? | Class |
|---|------|-------|-------|----------|-------|
| 54 | `weapon_knife_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 55 | `weapon_rock_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 56 | `weapon_arrow_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 57 | `weapon_fireball_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 58 | `weapon_tree_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 59 | `weapon_meteor_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 60 | `weapon_sprinkle_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 61 | `weapon_bone_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 62 | `weapon_blood_emission_scen99` | 4 | 3 | N (min=20) | MEDIUM |
| 63 | `weapon_blob_emission_scen99` | 4 | 3 | N (min=24) | MEDIUM |
| 64 | `weapon_fire_arrow_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 65 | `weapon_lightning_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 66 | `weapon_glow_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 67 | `weapon_wave_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 68 | `weapon_wave2_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 69 | `weapon_wave3_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 70 | `weapon_circle_protection_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 71 | `weapon_hammer_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 72 | `weapon_door_emission_scen99` | 4 | 3 | **Y** | **WEAK** |
| 73 | `weapon_boulder_emission_scen99` | 4 | 3 | **Y** | **WEAK** |

### 2.5 Effect Emission (13 rows)

All 13 rows share: `TickReached(150)`, `WalkerFamilyCount(SOLDIER, 1, 2)`,
`EffectFamilyCount(FAMILY_X, 0, 0, source=0)`, `EventKindAtLeast(play_sound, 1)`.
The `EffectFamilyCount(*, 0, 0)` range is the honest schema-v1 snapshot (combat
FX expire before tick 150), but carries no discriminating behavioral signal at
the final tick.

| # | ID | Total | Behav | Floor=0? | Class |
|---|------|-------|-------|----------|-------|
| 74-86 | `effect_*_emission_scen99` (x13) | 4 | 3 | N | MEDIUM |

Note: `EffectFamilyCount(*, 0, 0)` is structurally present but behaviorally
vacuous — it asserts "zero effects alive at dump time" which is tautologically
true for short-lived combat FX. The mutation canary targets the registry index
(not the emission path), so it can flip the predicate, but the scenario does
not actually exercise the named FX in a way observable at the dump snapshot.

### 2.6 Generator Emission (4 rows)

| # | ID | Total | Behav | Floor=0? | Class |
|---|------|-------|-------|----------|-------|
| 87 | `generator_tent_emission_scen99` | 2 | 1 | N | WEAK |
| 88 | `generator_tower_emission_scen99` | 2 | 1 | N | WEAK |
| 89 | `generator_bones_emission_scen99` | 2 | 1 | N | WEAK |
| 90 | `generator_treehouse_emission_scen99` | 2 | 1 | N | WEAK |

### 2.7 Event-Kind Emission (5 rows)

| # | ID | Total | Behav | Floor=0? | Class |
|---|------|-------|-------|----------|-------|
| 91 | `event_notification_emission_scen99` | 4 | 3 | N | MEDIUM |
| 92 | `event_set_palette_emission_scen99` | 4 | 3 | N | MEDIUM |
| 93 | `event_request_redraw_emission_scen99` | 4 | 3 | N | MEDIUM |
| 94 | `event_end_game_emission_scen99` | 4 | 3 | N | MEDIUM |
| 95 | `event_set_end_emission_scen99` | 4 | 3 | N | MEDIUM |

Note: `set_palette`, `request_redraw`, `end_game`, `set_end` rows use
`EventKindExactly(*, 0)` — negative assertions that the event was NOT
triggered. These are honest (the combat arena doesn't produce them) but
they do not positively exercise the event emission path.

### 2.8 Per-Family Per-Slot Special Cast (38 rows)

All 38 rows share: `TickReached(150)`, `WalkerFamilyCount(FAMILY_X, N, M)`,
`EventKindAtLeast(play_sound, 0)`. **37 of 38** have `floor=0` (trivially
true). Only `special_skeleton_1_scen99` has `floor=10`.

| Family | Slots | All Floor=0? | Class |
|--------|-------|--------------|-------|
| SOLDIER | 1,2,3,4 | Y | WEAK |
| ELF | 1,2,3,4 | Y | WEAK |
| ARCHER | 1,2,3 | Y | WEAK |
| MAGE | 2,3,4,5 | Y | WEAK |
| SKELETON | 1 | N (min=10) | WEAK |
| CLERIC | 2,3,4 | Y | WEAK |
| FIREELEMENTAL | 1 | Y | WEAK |
| SLIME | 1 | Y | WEAK |
| SMALL_SLIME | 1 | Y | WEAK |
| MEDIUM_SLIME | 1 | Y | WEAK |
| THIEF | 2,3,4 | Y | WEAK |
| GHOST | 1 | Y | WEAK |
| DRUID | 1,2,3,4 | Y | WEAK |
| ORC | 1,2 | Y | WEAK |
| BARBARIAN | 1,2 | Y | WEAK |
| ARCHMAGE | 2,3,4 | Y | WEAK |

### 2.9 Classification Summary

| Classification | Count | Percentage |
|----------------|-------|------------|
| STRONG | 3 | 2.3% |
| MEDIUM | 57 | 42.9% |
| WEAK | 71 | 53.4% |
| TRIVIAL | 2 | 1.5% |
| **Total** | **133** | |

---

## 3. Trivially-True Predicate Inventory

**55 instances** of `EventKindAtLeast(play_sound, 0)` — a predicate that
is satisfied by any dump (0 >= 0 is always true) and therefore adds zero
discriminating power.

### 3.1 Weapon Emission Scenarios (18 instances)

```
weapon_knife_emission_scen99        EventKindAtLeast(1, 0)
weapon_rock_emission_scen99         EventKindAtLeast(1, 0)
weapon_arrow_emission_scen99        EventKindAtLeast(1, 0)
weapon_fireball_emission_scen99     EventKindAtLeast(1, 0)
weapon_tree_emission_scen99         EventKindAtLeast(1, 0)
weapon_meteor_emission_scen99       EventKindAtLeast(1, 0)
weapon_sprinkle_emission_scen99     EventKindAtLeast(1, 0)
weapon_bone_emission_scen99         EventKindAtLeast(1, 0)
weapon_fire_arrow_emission_scen99   EventKindAtLeast(1, 0)
weapon_lightning_emission_scen99    EventKindAtLeast(1, 0)
weapon_glow_emission_scen99         EventKindAtLeast(1, 0)
weapon_wave_emission_scen99         EventKindAtLeast(1, 0)
weapon_wave2_emission_scen99        EventKindAtLeast(1, 0)
weapon_wave3_emission_scen99        EventKindAtLeast(1, 0)
weapon_circle_protection_emission_scen99  EventKindAtLeast(1, 0)
weapon_hammer_emission_scen99       EventKindAtLeast(1, 0)
weapon_door_emission_scen99         EventKindAtLeast(1, 0)
weapon_boulder_emission_scen99      EventKindAtLeast(1, 0)
```

### 3.2 Per-Slot Special Cast Scenarios (37 instances)

```
special_soldier_1_scen99            EventKindAtLeast(1, 0)
special_soldier_2_scen99            EventKindAtLeast(1, 0)
special_soldier_3_scen99            EventKindAtLeast(1, 0)
special_soldier_4_scen99            EventKindAtLeast(1, 0)
special_elf_1_scen99                EventKindAtLeast(1, 0)
special_elf_2_scen99                EventKindAtLeast(1, 0)
special_elf_3_scen99                EventKindAtLeast(1, 0)
special_elf_4_scen99                EventKindAtLeast(1, 0)
special_archer_1_scen99             EventKindAtLeast(1, 0)
special_archer_2_scen99             EventKindAtLeast(1, 0)
special_archer_3_scen99             EventKindAtLeast(1, 0)
special_mage_2_scen99               EventKindAtLeast(1, 0)
special_mage_3_scen99               EventKindAtLeast(1, 0)
special_mage_4_scen99               EventKindAtLeast(1, 0)
special_mage_5_scen99               EventKindAtLeast(1, 0)
special_cleric_2_scen99             EventKindAtLeast(1, 0)
special_cleric_3_scen99             EventKindAtLeast(1, 0)
special_cleric_4_scen99             EventKindAtLeast(1, 0)
special_fireelemental_1_scen99      EventKindAtLeast(1, 0)
special_slime_1_scen99              EventKindAtLeast(1, 0)
special_small_slime_1_scen99        EventKindAtLeast(1, 0)
special_medium_slime_1_scen99       EventKindAtLeast(1, 0)
special_thief_2_scen99              EventKindAtLeast(1, 0)
special_thief_3_scen99              EventKindAtLeast(1, 0)
special_thief_4_scen99              EventKindAtLeast(1, 0)
special_ghost_1_scen99              EventKindAtLeast(1, 0)
special_druid_1_scen99              EventKindAtLeast(1, 0)
special_druid_2_scen99              EventKindAtLeast(1, 0)
special_druid_3_scen99              EventKindAtLeast(1, 0)
special_druid_4_scen99              EventKindAtLeast(1, 0)
special_orc_1_scen99                EventKindAtLeast(1, 0)
special_orc_2_scen99                EventKindAtLeast(1, 0)
special_barbarian_1_scen99          EventKindAtLeast(1, 0)
special_barbarian_2_scen99          EventKindAtLeast(1, 0)
special_archmage_2_scen99           EventKindAtLeast(1, 0)
special_archmage_3_scen99           EventKindAtLeast(1, 0)
special_archmage_4_scen99           EventKindAtLeast(1, 0)
```

---

## 4. Per-Entity Coverage Matrix

### 4.1 Living Families (21 entities)

All 21 living families have structural coverage (spawned in at least one
scenario) AND behavioral coverage (arg0 of WalkerFamilyCount in at least
one non-trivial predicate).

| Family | Structural | Behavioral | Bound Kinds | Classification |
|--------|-----------|------------|-------------|----------------|
| SOLDIER | YES | YES | WFC,WTA,WHR,WPM,EKA,EKE,LDE,WDB | FULL |
| ELF | YES | YES | WFC,WTA,WPM,EKA | FULL |
| ARCHER | YES | YES | WFC,WTA,WPM,EKA | FULL |
| MAGE | YES | YES | WFC,WTA,WPM,EKA | FULL |
| SKELETON | YES | YES | WFC,WTA,WPM,EKA,WDB | FULL |
| CLERIC | YES | YES | WFC,WTA,WPM,EKA | FULL |
| FIREELEMENTAL | YES | YES | WFC,WTA,WPM,EKA | FULL |
| FAERIE | YES | YES | WFC,WTA,WPM,EKA | FULL |
| SLIME | YES | YES | WFC,WTA,WPM,EKA | FULL |
| SMALL_SLIME | YES | YES | WFC,WTA,WPM,EKA | FULL |
| MEDIUM_SLIME | YES | YES | WFC,WTA,WPM,EKA | FULL |
| THIEF | YES | YES | WFC,WTA,WPM,EKA | FULL |
| GHOST | YES | YES | WFC,WTA,WPM,EKA | FULL |
| DRUID | YES | YES | WFC,WTA,WPM,EKA,WHR | FULL |
| ORC | YES | YES | WFC,WTA,WPM,EKA | FULL |
| BIG_ORC | YES | YES | WFC,WTA,WPM,EKA | FULL |
| BARBARIAN | YES | YES | WFC,WTA,WPM,EKA | FULL |
| ARCHMAGE | YES | YES | WFC,WTA,WPM,EKA | FULL |
| GOLEM | YES | YES | WFC,WTA,WPM,EKA,WAF | FULL |
| GIANT_SKELETON | YES | YES | WFC,WTA,WPM,EKA | FULL |
| TOWER1 | YES | YES | WFC,WTA,WPM,EKA | FULL |

### 4.2 Effect Families (13 entities)

All 13 effect families have structural coverage via `effect_*_emission_scen99`
rows and behavioral coverage via `EffectFamilyCount(FAMILY_X, 0, 0)` predicates.
However, the `(0, 0)` range is vacuously true for combat FX that expire before
the dump snapshot.

| Family | Structural | Behavioral | Bound Kinds | Classification |
|--------|-----------|------------|-------------|----------------|
| EXPAND | YES | YES* | EFC(0,0) | FULL (vacuous) |
| GHOST_SCARE | YES | YES* | EFC(0,0) | FULL (vacuous) |
| BOMB | YES | YES* | EFC(0,0) | FULL (vacuous) |
| EXPLOSION | YES | YES* | EFC(0,0) | FULL (vacuous) |
| FLASH | YES | YES* | EFC(0,0) | FULL (vacuous) |
| MAGIC_SHIELD | YES | YES* | EFC(0,0) | FULL (vacuous) |
| KNIFE_BACK | YES | YES* | EFC(0,0) | FULL (vacuous) |
| BOOMERANG | YES | YES* | EFC(0,0) | FULL (vacuous) |
| CLOUD | YES | YES* | EFC(0,0) | FULL (vacuous) |
| MARKER | YES | YES* | EFC(0,0) | FULL (vacuous) |
| CHAIN | YES | YES* | EFC(0,0) | FULL (vacuous) |
| DOOR_OPEN | YES | YES* | EFC(0,0) | FULL (vacuous) |
| HIT | YES | YES* | EFC(0,0) | FULL (vacuous) |

*Behavioral coverage is formally satisfied but practically vacuous: the
`EffectFamilyCount(*, 0, 0)` predicate asserts zero effects alive at
the final tick, which is tautologically true for short-lived combat FX.
A schema-v2 per-tick coverage observation would be needed to positively
assert that the named FX was emitted during the run.

### 4.3 Weapon Families (20 entities)

All 20 weapon families have structural coverage via `weapon_*_emission_scen99`
rows and behavioral coverage via `WeaponFamilyEmitted(FAMILY_X)` predicates.

| Family | Structural | Behavioral | Notes | Classification |
|--------|-----------|------------|-------|----------------|
| KNIFE | YES | YES | K_FIRE wielder emits naturally | FULL |
| ROCK | YES | YES | K_FIRE wielder emits naturally | FULL |
| ARROW | YES | YES | K_FIRE wielder emits naturally | FULL |
| FIREBALL | YES | YES | K_FIRE wielder emits naturally | FULL |
| TREE | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| METEOR | YES | YES | K_FIRE wielder emits naturally | FULL |
| SPRINKLE | YES | YES | K_FIRE wielder emits naturally | FULL |
| BONE | YES | YES | K_FIRE wielder emits naturally | FULL |
| BLOOD | YES | YES | Combat-death side-effect; branch_only | FULL |
| BLOB | YES | YES | K_FIRE wielder emits naturally; branch_only | FULL |
| FIRE_ARROW | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| LIGHTNING | YES | YES | K_FIRE wielder emits naturally | FULL |
| GLOW | YES | YES | K_FIRE wielder emits naturally | FULL |
| WAVE | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| WAVE2 | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| WAVE3 | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| CIRCLE_PROTECTION | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| HAMMER | YES | YES | K_FIRE wielder emits naturally | FULL |
| DOOR | YES | YES | Direct kOrderWeapon spawn; dual-side-gated | FULL (structural only) |
| BOULDER | YES | YES | K_FIRE wielder emits naturally | FULL |

Note: 7 weapon rows (TREE, FIRE_ARROW, WAVE, WAVE2, WAVE3,
CIRCLE_PROTECTION, DOOR) use dual-side-gated `WeaponFamilyEmitted`
predicates (`applies_to_branch=false AND applies_to_master=false`). The
predicate is structurally present for the static coverage scan but
evaluates as pass on both sides — it does not actually verify weapon
emission during the run.

### 4.4 Treasure Families (13 entities)

| Family | Structural | Behavioral | Bound Kinds | Classification |
|--------|-----------|------------|-------------|----------------|
| STAIN | YES | YES | WPM, TFOR(indeterminate) | FULL |
| DRUMSTICK | YES | YES | WPM, TFOR, WHR | FULL |
| GOLD_BAR | YES | YES | WPM, TFOR | FULL |
| SILVER_BAR | YES | YES | WPM, TFOR | FULL |
| MAGIC_POTION | YES | YES | WPM, TFOR, WHR | FULL |
| INVIS_POTION | YES | YES | WPM, TFOR | FULL |
| INVULNERABLE_POTION | YES | YES | WPM, TFOR | FULL |
| FLIGHT_POTION | YES | YES | WPM, TFOR | FULL |
| EXIT | YES | YES | WPM, TFOR (via exit_trigger_scen9302) | FULL |
| TELEPORTER | YES | YES | WPM, TFOR | FULL |
| LIFE_GEM | YES | YES | WPM, TFOR, WHR | FULL |
| KEY | YES | YES | WPM, TFOR, WKA | FULL |
| SPEED_POTION | YES | YES | WPM, TFOR | FULL |

Note: EXIT is covered by `exit_trigger_scen9302` rather than a dedicated
`treasure_exit_pickup_scen99` row. See section 7.5 for the missing
scenario.

### 4.5 Generator Families (4 entities)

| Family | Structural | Behavioral | Bound Kinds | Classification |
|--------|-----------|------------|-------------|----------------|
| TENT | YES | YES | WFC(spawned SKELETON) | FULL |
| TOWER | YES | YES | WFC(spawned MAGE) | FULL |
| BONES | YES | YES | WFC(spawned GHOST) | FULL |
| TREEHOUSE | YES | YES | WFC(spawned ELF) | FULL |

### 4.6 Event Kinds (9 entities)

| Event Kind | Covered? | Covering Scenario(s) | Predicate |
|------------|----------|---------------------|-----------|
| play_sound | YES | smoke_nonempty, special_thief, family_*, etc. | EKA(1, N>0) |
| notification | YES | event_notification_emission_scen99 | EKA(2, 1) |
| set_palette | YES | event_set_palette_emission_scen99 | EKE(3, 0) negative |
| request_redraw | YES | event_request_redraw_emission_scen99 | EKE(4, 0) negative |
| end_game | YES | event_end_game_emission_scen99 | EKE(5, 0) negative |
| set_end | YES | event_set_end_emission_scen99 | EKE(6, 0) negative |
| request_exit_confirmation | YES | scripted_input_scen9301 | EKE(7, 1) positive |
| withdraw_to_level | YES | scripted_input_scen9301 | EKE(8, 1) positive |
| score_change | YES | smoke_nonempty, scoring_after_combat | EKA(9, N>0) |

Note: 4 of 9 event kinds (`set_palette`, `request_redraw`, `end_game`,
`set_end`) are covered only by negative assertions (`EventKindExactly(*, 0)`).
These prove the event did NOT fire in the arena but do not positively exercise
the emission path.

### 4.7 Specials (42 entities)

42 required `(family, special_index)` pairs from `kRequiredSpecials[]`.
All 42 are covered by either a dedicated `special_*_N_scen99` row or
one of the legacy `special_*_scen{123,124,126,789}` rows.

| Family | Required Slots | Covered Slots | Gap |
|--------|---------------|---------------|-----|
| SOLDIER | 1,2,3,4 | 1,2,3,4 | - |
| ELF | 1,2,3,4 | 1,2,3,4 | - |
| ARCHER | 1,2,3 | 1,2,3 | - |
| MAGE | 1,2,3,4,5 | 1(via scen126),2,3,4,5 | - |
| SKELETON | 1 | 1 | - |
| CLERIC | 1,2,3,4 | 1(via scen124),2,3,4 | - |
| FIREELEMENTAL | 1 | 1 | - |
| SLIME | 1 | 1 | - |
| SMALL_SLIME | 1 | 1 | - |
| MEDIUM_SLIME | 1 | 1 | - |
| THIEF | 1,2,3,4 | 1(via scen789),2,3,4 | - |
| GHOST | 1 | 1 | - |
| DRUID | 1,2,3,4 | 1,2,3,4 | - |
| ORC | 1,2 | 1,2 | - |
| BARBARIAN | 1,2 | 1,2 | - |
| ARCHMAGE | 1,2,3,4 | 1(via scen123),2,3,4 | - |

---

## 5. Widened Predicate Review

### 5.1 Summary

The honest audit (`parity-honest-audit.md`) identified 21 widened predicates.
8 were narrowed to tighter ranges; 13 retained with justification tags.

### 5.2 Retained Widened Predicates (13)

| Scenario | Predicate Kind | Range | Justification Tag | Tight? |
|----------|---------------|-------|-------------------|--------|
| `combat_attack_scen99` | WalkerOfTeamAlive(team=0) | 1-2 | intended_diff: fire-elemental escort | YES |
| `combat_attack_scen99` | WalkerHpRangeAtFinalTick(SOLDIER) | 1900-10700 | rng_drift: combat damage sequencing | NO (8800 hp-cents) |
| `special_mage_scen126` | WalkerOfTeamAlive(team=0) | 1-2 | intended_diff: branch summons escort | YES |
| `special_thief_scen789` | WalkerOfTeamAlive(team=0) | 2-3 | intended_diff: ghost residue | YES |
| `effect_chain_scen9410` | WalkerOfTeamAlive(team=1) | 1-2 | intended_diff: chain-spawned elf | YES |
| `effect_chain_scen9410` | WalkerHpRangeAtFinalTick(SOLDIER) | 11100-12000 | rng_drift: chain-effect timing | YES (900 hp-cents) |
| `summon_druid_pet_scen950` | WalkerHpRangeAtFinalTick(SOLDIER) | 7200-8400 | rng_drift: pet attack pattern | YES (1200 hp-cents) |
| `scoring_after_combat_scen99` | WalkerOfTeamAlive(team=0) | 1-2 | intended_diff: fire-elemental escort | YES |
| `scoring_after_combat_scen99` | WalkerHpRangeAtFinalTick(SOLDIER) | 1900-10700 | rng_drift: combat damage sequencing | NO (8800 hp-cents) |
| `save_roundtrip_scen99` | WalkerOfTeamAlive(team=0) | 1-2 | intended_diff: escort walkers | YES |
| `save_roundtrip_scen99` | WalkerHpRangeAtFinalTick(SOLDIER) | 6300-10100 | rng_drift: combat sequencing | NO (3800 hp-cents) |
| `family_*_scen99` (x21) | WalkerOfTeamAlive(enemy team=1) | 0-1 | rng_drift: far enemy survivor | YES |

### 5.3 Unreasonably Wide Ranges (HP range > 5000 hp-cents)

| Scenario | Range | Spread | Justification |
|----------|-------|--------|---------------|
| `combat_attack_scen99` | 1900-10700 | 8800 | rng_drift: RNG-driven attack ordering |
| `scoring_after_combat_scen99` | 1900-10700 | 8800 | rng_drift: RNG-driven attack ordering |

These two scenarios share the same arena and inputs — the wide range is a
consequence of fundamentally different combat damage sequencing between
master and branch due to the RNG migration. The spread is justified by
the commit citation but is a candidate for replacement with a non-HP
behavioral axis (e.g. WalkerDiedByFinal or event counts).

### 5.4 Label Migration Readiness

All 13 retained widened predicates carry inline `// rng_drift:` or
`// intended_diff:` comments. These are ready for verbatim migration to
the `label` field of `FactPredicate` once Phase 03+ standardizes label
encoding. No additional editorial work is needed.

---

## 6. Mutation Canary Audit

### 6.1 Broken Canaries

| Scenario | Mutation | Issue |
|----------|----------|-------|
| `save_roundtrip_scen99` | `kMut_save_corrupt` (save_data.cpp:107) | Runner does NOT invoke save subsystem; mutating save header version has no effect on the parity run |
| `rng_seed_stable_scen99` | `kMut_save_corrupt` (save_data.cpp:107) | Runner does NOT invoke save subsystem; same issue |

Both scenarios declare `kMut_save_corrupt` which changes the save file
version byte from 9 to 0. The parity runner loads a scenario, runs
simulation ticks, and captures a state dump — it never performs a
save/load roundtrip. The mutation target is never executed during the
parity run, so no predicate can flip. These canaries are **broken** and
cannot detect divergence.

**Remediation**: Replace `kMut_save_corrupt` with mutations that target
code paths actually exercised by these scenarios:
- `save_roundtrip_scen99`: Should use a mutation that disrupts combat
  damage or walker spawning (e.g., `kMut_combat_damage` or
  `kMut_walker_ai_wander`) since the scenario's predicates test
  walker HP ranges and alive counts.
- `rng_seed_stable_scen99`: Should use a mutation that disrupts the
  walker AI or HP initialization (e.g., `kMut_walker_ai_wander`)
  since the scenario's predicates test idle walker state.

### 6.2 Reused Mutations (not broken, but diluted)

Several mutations are shared across multiple scenario rows, which dilutes
the per-row falsification signal:

| Mutation | Used By | Count |
|----------|---------|-------|
| `kMut_combat_damage` | combat_attack, scoring_after_combat | 2 |
| `kMut_walker_ai_wander` | ai_idle_wander, tick_cadence, scripted_input | 3 |
| `kMut_effect_lifetime` | effect_bomb_lifetime, effect_chain | 2 |
| `kMut_family_spawn_identity` | 19 of 21 family rows | 19 |
| `kMut_smoke_score_event` | smoke_nonempty | 1 |

The `kMut_family_spawn_identity` reuse is acceptable because the mutation
(rotate family identity by +1 mod 21) reliably flips the per-family
`WalkerFamilyCount` predicate for every row. The combat/wander reuse is
acceptable because each scenario exercises the mutated code path.

---

## 7. Missing Scenario Inventory

### 7.1 Missing Treasure Pickup

`FAMILY_EXIT` (id=8) lacks a dedicated `treasure_exit_pickup_scen99` row.
EXIT is covered by `exit_trigger_scen9302` which includes
`TreasureFamilyOfOrderRemovedFromOblist(FAMILY_EXIT, kOrderTreasure)`,
but this is an exit-trigger scenario (K_RIGHT walk to exit tile) rather
than a standard treasure-pickup arena. The coverage gap is structural
only — EXIT's behavioral coverage is satisfied by exit_trigger_scen9302.

### 7.2 Missing Positive Event-Kind Assertions

4 event kinds have only negative assertions (`EventKindExactly(*, 0)`):
- `set_palette` (ordinal 3)
- `request_redraw` (ordinal 4)
- `end_game` (ordinal 5)
- `set_end` (ordinal 6)

These scenarios prove the event was NOT emitted, but no scenario
positively asserts that these events ARE emitted when expected. Creating
positive-assertion scenarios would require arenas that trigger these
events (e.g., total player death for `end_game`).

### 7.3 Dual-Side-Gated Weapon Predicates

7 weapon emission rows use `WeaponFamilyEmitted` predicates that are
gated `applies_to_branch=false AND applies_to_master=false`:
TREE, FIRE_ARROW, WAVE, WAVE2, WAVE3, CIRCLE_PROTECTION, DOOR.

These satisfy the static coverage scan but do not verify weapon emission
at runtime. The weapons require K_SPECIAL input scripts with
`stats_level` / `magicpoints` preconditions (or, for DOOR, scripted
level content) that the current arena doesn't provide.

---

## 8. Prioritized Remediation List

### Priority 1: Broken Mutation Canaries (2 instances)

Cannot detect divergence at all — the mutation target is never invoked.

1. `save_roundtrip_scen99` uses `kMut_save_corrupt` — replace with
   `kMut_combat_damage` or equivalent
2. `rng_seed_stable_scen99` uses `kMut_save_corrupt` — replace with
   `kMut_walker_ai_wander` or equivalent

### Priority 2: Trivially-True Predicates (55 instances)

`EventKindAtLeast(play_sound, 0)` is always true and adds no
discriminating power. Each instance should be replaced with either:
- A non-zero floor pinned to the observed master-golden value
- A different predicate kind (WalkerHpRangeAtFinalTick, WalkerDiedByFinal,
  WeaponFamilyEmitted, etc.)

| Category | Count | Remediation |
|----------|-------|-------------|
| Weapon emission rows | 18 | Pin play_sound floor to observed value |
| Per-slot special rows | 37 | Pin play_sound floor to observed value, or add WalkerDiedByFinal/WPM/WHR predicates |

### Priority 3: Missing Behavioral-Consequence Predicates

**Effect emission rows (13)**: `EffectFamilyCount(*, 0, 0)` vacuously
asserts zero FX alive at the final tick. The named FX is structurally
bound but behaviorally unobserved. Remediation requires schema-v2
per-tick coverage observation or longer tick budgets that keep FX alive
at snapshot time.

**Treasure pickup rows (7 WEAK)**: 7 of 12 treasure rows have only 2
behavioral predicates (WPM + TFOR). Adding a StatDeltaOnPickup or
EventKindAtLeast(play_sound, N>0) predicate would strengthen them.

### Priority 4: Missing Specific-Axis Predicates

**Family completeness rows (21)**: No `WalkerHpRangeAtFinalTick`
predicate — the HP consequence of combat is unobserved. Adding
HP-range predicates would catch combat-damage regressions that the
current position/alive/count predicates miss.

**Special cast rows (38)**: Only `WalkerFamilyCount` + trivially-true
`EventKindAtLeast`. No `WalkerDiedByFinal`, `WalkerHpRangeAtFinalTick`,
`WalkerPositionMoved`, `WeaponFamilyEmitted`, or `EffectFamilyCount`
predicates — the special's gameplay consequence is unobserved beyond
caster survival.

### Priority 5: Missing Dedicated Scenarios

1. `treasure_exit_pickup_scen99` — EXIT treasure family covered only by
   exit_trigger_scen9302; a standard treasure-pickup arena would
   strengthen behavioral coverage.

2. Positive event-kind assertion scenarios for `set_palette`,
   `request_redraw`, `end_game`, `set_end` — current rows only assert
   these events did NOT fire.

3. Un-gate the 7 dual-side-gated weapon emission rows by adding
   K_SPECIAL input scripts with `stats_level`/`magicpoints` preconditions
   for TREE, FIRE_ARROW, WAVE, WAVE2, WAVE3, CIRCLE_PROTECTION, and
   DOOR-interaction scenarios.

---

## Appendix A: Predicate Kind Legend

| Abbreviation | FactKind |
|-------------|----------|
| TR | TickReached |
| LDE | LevelDoneEquals |
| SD | ScoreDelta |
| WFC | WalkerFamilyCount |
| WTA | WalkerOfTeamAlive |
| WHR | WalkerHpRangeAtFinalTick |
| WKA | WalkerKeysApplied |
| WPM | WalkerPositionMoved |
| WDB | WalkerDiedByFinal |
| WAF | WalkerAliveAtFinal |
| TFRO | TreasureFamilyRemovedFromOblist |
| TFOR | TreasureFamilyOfOrderRemovedFromOblist |
| SDP | StatDeltaOnPickup |
| EFC | EffectFamilyCount |
| EKA | EventKindAtLeast |
| EKE | EventKindExactly |
| WFE | WeaponFamilyEmitted |

## Appendix B: Scenario Count Verification

```
Core/infrastructure:    19
Family completeness:    21
Coverage catchall:       1
Treasure pickup:        12
Weapon emission:        20
Effect emission:        13
Generator emission:      4
Event-kind emission:     5
Special cast:           38
                       ---
Total:                 133 (matches kScenarioCount)
```

Coverage target dimensions: 21 + 13 + 20 + 13 + 4 + 9 + 42 = 122
