# Parity Present State - Phase 01
## Test count snapshot
Passed: 57
Skipped: 80
Failing: 14
## Failing tests
- Parity.special_cleric_scen124
- Parity.family_ghost_scen99
- Parity.treasure_stain_pickup_scen99
- Parity.treasure_drumstick_pickup_scen99
- Parity.treasure_gold_bar_pickup_scen99
- Parity.treasure_silver_bar_pickup_scen99
- Parity.treasure_magic_potion_pickup_scen99
- Parity.treasure_invis_potion_pickup_scen99
- Parity.treasure_invulnerable_potion_pickup_scen99
- Parity.treasure_flight_potion_pickup_scen99
- Parity.treasure_teleporter_pickup_scen99
- Parity.treasure_life_gem_pickup_scen99
- Parity.treasure_key_pickup_scen99
- Parity.treasure_speed_potion_pickup_scen99
## Skipped tests
- Other: coverage_catchall_scen99
- Weapons: weapon_knife_emission_scen99, weapon_rock_emission_scen99, weapon_arrow_emission_scen99, weapon_fireball_emission_scen99, weapon_tree_emission_scen99, weapon_meteor_emission_scen99, weapon_sprinkle_emission_scen99, weapon_bone_emission_scen99, weapon_blood_emission_scen99, weapon_blob_emission_scen99, weapon_fire_arrow_emission_scen99, weapon_lightning_emission_scen99, weapon_glow_emission_scen99, weapon_wave_emission_scen99, weapon_wave2_emission_scen99, weapon_wave3_emission_scen99, weapon_circle_protection_emission_scen99, weapon_hammer_emission_scen99, weapon_door_emission_scen99, weapon_boulder_emission_scen99
- Effects: effect_expand_emission_scen99, effect_ghost_scare_emission_scen99, effect_bomb_emission_scen99, effect_explosion_emission_scen99, effect_flash_emission_scen99, effect_magic_shield_emission_scen99, effect_knife_back_emission_scen99, effect_boomerang_emission_scen99, effect_cloud_emission_scen99, effect_marker_emission_scen99, effect_chain_emission_scen99, effect_door_open_emission_scen99, effect_hit_emission_scen99
- Generators/events: generator_tent_emission_scen99, generator_tower_emission_scen99, generator_bones_emission_scen99, generator_treehouse_emission_scen99, event_notification_emission_scen99, event_set_palette_emission_scen99, event_request_redraw_emission_scen99, event_end_game_emission_scen99, event_set_end_emission_scen99
- Specials: special_soldier_1_scen99, special_soldier_2_scen99, special_soldier_3_scen99, special_soldier_4_scen99, special_elf_1_scen99, special_elf_2_scen99, special_elf_3_scen99, special_elf_4_scen99, special_archer_1_scen99, special_archer_2_scen99, special_archer_3_scen99, special_mage_2_scen99, special_mage_3_scen99, special_mage_4_scen99, special_mage_5_scen99, special_skeleton_1_scen99, special_cleric_2_scen99, special_cleric_3_scen99, special_cleric_4_scen99, special_fireelemental_1_scen99, special_slime_1_scen99, special_small_slime_1_scen99, special_medium_slime_1_scen99, special_thief_2_scen99, special_thief_3_scen99, special_thief_4_scen99, special_ghost_1_scen99, special_druid_1_scen99, special_druid_3_scen99, special_druid_4_scen99, special_orc_1_scen99, special_orc_2_scen99, special_barbarian_1_scen99, special_barbarian_2_scen99, special_archmage_2_scen99, special_archmage_3_scen99, special_archmage_4_scen99
## Master companion SHA pinned this phase
Master companion SHA: b900addb5084bbf98b46267d1624289a25e5ff66
## Mirror SHA delta
4326c97f55e32239eab0871975ecd115fcfbfb91  tests/parity/scenario_table.h
4326c97f55e32239eab0871975ecd115fcfbfb91  ../openglad-master/tools/parity_scenario_table.h
## Per-target coverage gap inventory
### Walker families (21)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `FAMILY_SOLDIER` | yes | `ai_idle_wander_scen9301` | yes |
| `FAMILY_ELF` | yes | `family_elf_scen99` | yes |
| `FAMILY_ARCHER` | yes | `family_archer_scen99` | yes |
| `FAMILY_MAGE` | yes | `special_mage_scen126` | yes |
| `FAMILY_SKELETON` | yes | `effect_bomb_lifetime_scen99` | yes |
| `FAMILY_CLERIC` | yes | `special_cleric_scen124` | yes |
| `FAMILY_FIREELEMENTAL` | yes | `family_fireelemental_scen99` | yes |
| `FAMILY_FAERIE` | yes | `family_faerie_scen99` | yes |
| `FAMILY_SLIME` | yes | `family_slime_scen99` | yes |
| `FAMILY_SMALL_SLIME` | yes | `family_slime_scen99` | yes |
| `FAMILY_MEDIUM_SLIME` | yes | `family_medium_slime_scen99` | yes |
| `FAMILY_THIEF` | yes | `special_thief_scen789` | yes |
| `FAMILY_GHOST` | yes | `family_ghost_scen99` | yes |
| `FAMILY_DRUID` | yes | `summon_druid_pet_scen950` | yes |
| `FAMILY_ORC` | yes | `exit_trigger_scen9302` | yes |
| `FAMILY_BIG_ORC` | yes | `family_big_orc_scen99` | yes |
| `FAMILY_BARBARIAN` | yes | `family_barbarian_scen99` | yes |
| `FAMILY_ARCHMAGE` | yes | `special_archmage_scen123` | yes |
| `FAMILY_GOLEM` | yes | `family_golem_scen99` | yes |
| `FAMILY_GIANT_SKELETON` | yes | `family_giant_skeleton_scen99` | yes |
| `FAMILY_TOWER1` | yes | `special_thief_scen789` | yes |
### Weapon families (20)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `FAMILY_KNIFE` | yes | `weapon_knife_emission_scen99` | no |
| `FAMILY_ROCK` | yes | `weapon_rock_emission_scen99` | no |
| `FAMILY_ARROW` | yes | `weapon_arrow_emission_scen99` | no |
| `FAMILY_FIREBALL` | yes | `weapon_fireball_emission_scen99` | no |
| `FAMILY_TREE` | yes | `weapon_tree_emission_scen99` | no |
| `FAMILY_METEOR` | yes | `weapon_meteor_emission_scen99` | no |
| `FAMILY_SPRINKLE` | yes | `weapon_sprinkle_emission_scen99` | no |
| `FAMILY_BONE` | yes | `weapon_bone_emission_scen99` | no |
| `FAMILY_BLOOD` | yes | `weapon_blood_emission_scen99` | no |
| `FAMILY_BLOB` | yes | `weapon_blob_emission_scen99` | no |
| `FAMILY_FIRE_ARROW` | yes | `weapon_fire_arrow_emission_scen99` | no |
| `FAMILY_LIGHTNING` | yes | `weapon_lightning_emission_scen99` | no |
| `FAMILY_GLOW` | yes | `weapon_glow_emission_scen99` | no |
| `FAMILY_WAVE` | yes | `weapon_wave_emission_scen99` | no |
| `FAMILY_WAVE2` | yes | `weapon_wave2_emission_scen99` | no |
| `FAMILY_WAVE3` | yes | `weapon_wave3_emission_scen99` | no |
| `FAMILY_CIRCLE_PROTECTION` | yes | `weapon_circle_protection_emission_scen99` | no |
| `FAMILY_HAMMER` | yes | `weapon_hammer_emission_scen99` | no |
| `FAMILY_DOOR` | yes | `weapon_door_emission_scen99` | no |
| `FAMILY_BOULDER` | yes | `weapon_boulder_emission_scen99` | no |
### Treasure families (13)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `FAMILY_STAIN` | yes | `treasure_stain_pickup_scen99` | yes |
| `FAMILY_DRUMSTICK` | yes | `treasure_drumstick_pickup_scen99` | yes |
| `FAMILY_GOLD_BAR` | yes | `treasure_gold_bar_pickup_scen99` | yes |
| `FAMILY_SILVER_BAR` | yes | `treasure_silver_bar_pickup_scen99` | yes |
| `FAMILY_MAGIC_POTION` | yes | `treasure_magic_potion_pickup_scen99` | yes |
| `FAMILY_INVIS_POTION` | yes | `treasure_invis_potion_pickup_scen99` | yes |
| `FAMILY_INVULNERABLE_POTION` | yes | `treasure_invulnerable_potion_pickup_scen99` | yes |
| `FAMILY_FLIGHT_POTION` | yes | `treasure_flight_potion_pickup_scen99` | yes |
| `FAMILY_EXIT` | yes | `exit_trigger_scen9302` | yes |
| `FAMILY_TELEPORTER` | yes | `treasure_teleporter_pickup_scen99` | yes |
| `FAMILY_LIFE_GEM` | yes | `treasure_life_gem_pickup_scen99` | yes |
| `FAMILY_KEY` | yes | `treasure_key_pickup_scen99` | yes |
| `FAMILY_SPEED_POTION` | yes | `treasure_speed_potion_pickup_scen99` | yes |
### Generator families (4)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `FAMILY_TENT` | yes | `family_skeleton_scen99` | yes |
| `FAMILY_TOWER` | yes | `special_mage_scen126` | yes |
| `FAMILY_BONES` | yes | `family_ghost_scen99` | yes |
| `FAMILY_TREEHOUSE` | yes | `family_elf_scen99` | yes |
### Effect (FX) families (13)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `FAMILY_EXPAND` | yes | `effect_expand_emission_scen99` | no |
| `FAMILY_GHOST_SCARE` | yes | `effect_ghost_scare_emission_scen99` | no |
| `FAMILY_BOMB` | yes | `effect_bomb_emission_scen99` | no |
| `FAMILY_EXPLOSION` | yes | `effect_explosion_emission_scen99` | no |
| `FAMILY_FLASH` | yes | `effect_flash_emission_scen99` | no |
| `FAMILY_MAGIC_SHIELD` | yes | `effect_magic_shield_emission_scen99` | no |
| `FAMILY_KNIFE_BACK` | yes | `effect_knife_back_emission_scen99` | no |
| `FAMILY_BOOMERANG` | yes | `effect_boomerang_emission_scen99` | no |
| `FAMILY_CLOUD` | yes | `effect_cloud_emission_scen99` | no |
| `FAMILY_MARKER` | yes | `effect_marker_emission_scen99` | no |
| `FAMILY_CHAIN` | yes | `effect_chain_emission_scen99` | no |
| `FAMILY_DOOR_OPEN` | yes | `effect_door_open_emission_scen99` | no |
| `FAMILY_HIT` | yes | `effect_hit_emission_scen99` | no |
### Specials (42 (family, slot) pairs)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `FAMILY_SOLDIER:slot1` | yes | `family_soldier_scen99` | yes |
| `FAMILY_SOLDIER:slot2` | yes | `family_soldier_scen99` | yes |
| `FAMILY_SOLDIER:slot3` | yes | `family_soldier_scen99` | yes |
| `FAMILY_SOLDIER:slot4` | yes | `family_soldier_scen99` | yes |
| `FAMILY_ELF:slot1` | yes | `family_elf_scen99` | yes |
| `FAMILY_ELF:slot2` | yes | `family_elf_scen99` | yes |
| `FAMILY_ELF:slot3` | yes | `family_elf_scen99` | yes |
| `FAMILY_ELF:slot4` | yes | `family_elf_scen99` | yes |
| `FAMILY_ARCHER:slot1` | yes | `family_archer_scen99` | yes |
| `FAMILY_ARCHER:slot2` | yes | `family_archer_scen99` | yes |
| `FAMILY_ARCHER:slot3` | yes | `family_archer_scen99` | yes |
| `FAMILY_MAGE:slot1` | yes | `special_mage_scen126` | yes |
| `FAMILY_MAGE:slot2` | yes | `family_mage_scen99` | yes |
| `FAMILY_MAGE:slot3` | yes | `family_mage_scen99` | yes |
| `FAMILY_MAGE:slot4` | yes | `family_mage_scen99` | yes |
| `FAMILY_MAGE:slot5` | yes | `family_mage_scen99` | yes |
| `FAMILY_SKELETON:slot1` | yes | `family_skeleton_scen99` | yes |
| `FAMILY_CLERIC:slot1` | yes | `special_cleric_scen124` | yes |
| `FAMILY_CLERIC:slot2` | yes | `family_cleric_scen99` | yes |
| `FAMILY_CLERIC:slot3` | yes | `family_cleric_scen99` | yes |
| `FAMILY_CLERIC:slot4` | yes | `family_cleric_scen99` | yes |
| `FAMILY_FIREELEMENTAL:slot1` | yes | `family_fireelemental_scen99` | yes |
| `FAMILY_SLIME:slot1` | yes | `family_slime_scen99` | yes |
| `FAMILY_SMALL_SLIME:slot1` | yes | `family_small_slime_scen99` | yes |
| `FAMILY_MEDIUM_SLIME:slot1` | yes | `family_medium_slime_scen99` | yes |
| `FAMILY_THIEF:slot1` | yes | `special_thief_scen789` | yes |
| `FAMILY_THIEF:slot2` | yes | `family_thief_scen99` | yes |
| `FAMILY_THIEF:slot3` | yes | `family_thief_scen99` | yes |
| `FAMILY_THIEF:slot4` | yes | `family_thief_scen99` | yes |
| `FAMILY_GHOST:slot1` | yes | `family_ghost_scen99` | yes |
| `FAMILY_DRUID:slot1` | yes | `family_druid_scen99` | yes |
| `FAMILY_DRUID:slot2` | yes | `family_druid_scen99` | yes |
| `FAMILY_DRUID:slot3` | yes | `family_druid_scen99` | yes |
| `FAMILY_DRUID:slot4` | yes | `family_druid_scen99` | yes |
| `FAMILY_ORC:slot1` | yes | `family_orc_scen99` | yes |
| `FAMILY_ORC:slot2` | yes | `family_orc_scen99` | yes |
| `FAMILY_BARBARIAN:slot1` | yes | `family_barbarian_scen99` | yes |
| `FAMILY_BARBARIAN:slot2` | yes | `family_barbarian_scen99` | yes |
| `FAMILY_ARCHMAGE:slot1` | yes | `special_archmage_scen123` | yes |
| `FAMILY_ARCHMAGE:slot2` | yes | `family_archmage_scen99` | yes |
| `FAMILY_ARCHMAGE:slot3` | yes | `family_archmage_scen99` | yes |
| `FAMILY_ARCHMAGE:slot4` | yes | `family_archmage_scen99` | yes |
### Event kinds (9)
| target | observed_in_any_row | covering_scenario_id | golden_present |
|---|---|---|---|
| `play_sound` | yes | `special_thief_scen789` | yes |
| `notification` | yes | `event_notification_emission_scen99` | no |
| `set_palette` | yes | `event_set_palette_emission_scen99` | no |
| `request_redraw` | yes | `event_request_redraw_emission_scen99` | no |
| `end_game` | yes | `event_end_game_emission_scen99` | no |
| `set_end` | yes | `event_set_end_emission_scen99` | no |
| `request_exit_confirmation` | yes | `scripted_input_scen9301` | yes |
| `withdraw_to_level` | yes | `scripted_input_scen9301` | yes |
| `score_change` | yes | `scoring_after_combat_scen99` | yes |
## Broken-state authorisation
The global rule "all tests pass at all times" is overridden by the user's verbatim goal only inside this multi-phase window. The override is bounded:
- After phase 03, the treasure cohort and the two behavioural gates must be green. `og_test_parity` may still report `[SKIPPED]` rows but `[FAILED] 0`.
- After phase 04, every `SemanticParity` row in the current scenario table has a golden on disk; `[SKIPPED] 0` for those rows. New rows added in phases 05–09 are allowed to be `[SKIPPED]` until their own phase's verifier requires them green.
- After phase 09, `og_test_parity` reports `[PASSED] = total tests`, `[SKIPPED] 0`, `[FAILED] 0`.
- After phase 11, the entire repository test suite (`ctest --preset ci-test`) is green.
Each phase's verifier asserts the relevant bound; broken-state windows that escape these bounds are bounces.
