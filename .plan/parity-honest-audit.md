# Parity Honest Audit (parity-finish-2 Phase 01)

This document is the authoritative present-day audit of the gameplay
parity harness on `wip/networking`. It records what the parity test
surface actually asserts today, where its predicates have been widened
to swallow divergences instead of resolving them, and which axes of the
required cumulative coverage are still untouched by any behavioural
predicate. It supersedes the empty-world era reports
(`parity-divergence-report-empty-world.md`,
`parity-fixes-empty-world.md`) which described the harness when no
non-trivial scenarios existed yet.

All numbers below come from live commands run in this phase; the
companion `Widened predicates: <N>` line is byte-compared by the
verifier against a fresh run of the widened-predicate enumerator.

## (a) Current test surface

`build/ci-test/og_test_parity --gtest_list_tests` enumerates one
GoogleTest suite `Parity` with **50 cases**; `--gtest_brief=1` reports
`[==========] 50 tests from 1 test suite ran.` and
`[  PASSED  ] 50 tests.` (0 failures). The test list is:

| # | test name |
|---|-----------|
| 1 | `Parity.ai_idle_wander_scen9301` |
| 2 | `Parity.combat_attack_scen99` |
| 3 | `Parity.special_archmage_scen123` |
| 4 | `Parity.special_cleric_scen124` |
| 5 | `Parity.special_mage_scen126` |
| 6 | `Parity.special_thief_scen789` |
| 7 | `Parity.effect_bomb_lifetime_scen99` |
| 8 | `Parity.effect_chain_scen9410` |
| 9 | `Parity.summon_druid_pet_scen950` |
| 10 | `Parity.scoring_after_combat_scen99` |
| 11 | `Parity.save_roundtrip_scen99` |
| 12 | `Parity.exit_trigger_scen9302` |
| 13 | `Parity.tick_cadence_scen9301` |
| 14 | `Parity.rng_seed_stable_scen99` |
| 15 | `Parity.scripted_input_scen9301` |
| 16 | `Parity.snapshot_dirty_bits_scen9301` |
| 17 | `Parity.smoke_nonempty_scen99` |
| 18 | `Parity.smoke_nonempty_scen99_inputs` |
| 19 | `Parity.family_soldier_scen99` |
| 20 | `Parity.family_elf_scen99` |
| 21 | `Parity.family_archer_scen99` |
| 22 | `Parity.family_mage_scen99` |
| 23 | `Parity.family_skeleton_scen99` |
| 24 | `Parity.family_cleric_scen99` |
| 25 | `Parity.family_fireelemental_scen99` |
| 26 | `Parity.family_faerie_scen99` |
| 27 | `Parity.family_slime_scen99` |
| 28 | `Parity.family_small_slime_scen99` |
| 29 | `Parity.family_medium_slime_scen99` |
| 30 | `Parity.family_thief_scen99` |
| 31 | `Parity.family_ghost_scen99` |
| 32 | `Parity.family_druid_scen99` |
| 33 | `Parity.family_orc_scen99` |
| 34 | `Parity.family_big_orc_scen99` |
| 35 | `Parity.family_barbarian_scen99` |
| 36 | `Parity.family_archmage_scen99` |
| 37 | `Parity.family_golem_scen99` |
| 38 | `Parity.family_giant_skeleton_scen99` |
| 39 | `Parity.family_tower1_scen99` |
| 40 | `Parity.smoke_inputs_diverge_from_no_inputs` |
| 41 | `Parity.exercises_bitcount_matches_required_specials` |
| 42 | `Parity.parse_state_dump_tolerates_legacy_v1_shape` |
| 43 | `Parity.weapon_family_emitted_matches_dump_weapons_only` |
| 44 | `Parity.coverage_gate_walker_families` |
| 45 | `Parity.coverage_gate_effect_families` |
| 46 | `Parity.coverage_gate_weapon_families` |
| 47 | `Parity.coverage_gate_treasure_families` |
| 48 | `Parity.coverage_gate_event_kinds` |
| 49 | `Parity.coverage_gate_specials` |
| 50 | `Parity.coverage_gate` |

50 pass / 0 fail. Goldens on disk: `tests/parity/golden/` contains 39
JSON files.

## (b) Widened-predicate inventory

A predicate is "widened" if its `(min, max)` range exceeds exact-value
semantics: for `WalkerFamilyCount` / `WalkerOfTeamAlive` this means
`mn != mx`; for `WalkerHpRangeAtFinalTick` it means `(mx - mn) > 200`
hp-cents. The list below comes from a python pass over
`tests/parity/scenario_table.h`; each row cites the live line number.
A row is tagged `widening_justification: present` only if an inline
`(a)` marker or "Widen"/"widen" comment immediately precedes it
(per the Phase 01 contract); otherwise it is `absent` and becomes a
Phase 3 work item.

Widened predicates: 21

| line | scenario | kind | family/team | mn | mx | widening_justification |
|------|----------|------|-------------|----|----|------------------------|
| 527 | `ai_idle_wander_scen9301` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 7900 | 8200 | absent |
| 533 | `combat_attack_scen99` | `WalkerOfTeamAlive` | team=0 | 1 | 2 | absent |
| 534 | `combat_attack_scen99` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 1900 | 10700 | absent |
| 557 | `special_mage_scen126` | `WalkerOfTeamAlive` | team=0 | 1 | 2 | absent |
| 564 | `special_thief_scen789` | `WalkerOfTeamAlive` | team=0 | 2 | 3 | absent |
| 581 | `effect_chain_scen9410` | `WalkerOfTeamAlive` | team=1 | 1 | 2 | absent |
| 582 | `effect_chain_scen9410` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 11100 | 12000 | absent |
| 590 | `summon_druid_pet_scen950` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 7200 | 8400 | absent |
| 596 | `scoring_after_combat_scen99` | `WalkerOfTeamAlive` | team=0 | 1 | 2 | absent |
| 597 | `scoring_after_combat_scen99` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 1900 | 10700 | absent |
| 604 | `save_roundtrip_scen99` | `WalkerOfTeamAlive` | team=0 | 1 | 2 | absent |
| 605 | `save_roundtrip_scen99` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 6300 | 10100 | absent |
| 620 | `tick_cadence_scen9301` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 7900 | 8200 | absent |
| 627 | `rng_seed_stable_scen99` | `WalkerHpRangeAtFinalTick` | FAMILY_SOLDIER | 7900 | 8200 | absent |
| 693 | `family_mage_scen99` | `WalkerFamilyCount` | FAMILY_MAGE | 0 | 3 | present |
| 704 | `family_skeleton_scen99` | `WalkerFamilyCount` | FAMILY_SKELETON | 0 | 1 | absent |
| 732 | `family_slime_scen99` | `WalkerFamilyCount` | FAMILY_SLIME | 0 | 1 | present |
| 733 | `family_slime_scen99` | `WalkerFamilyCount` | FAMILY_SMALL_SLIME | 0 | 2 | present |
| 734 | `family_slime_scen99` | `WalkerOfTeamAlive` | team=0 | 1 | 2 | present |
| 763 | `family_ghost_scen99` | `WalkerFamilyCount` | FAMILY_GHOST | 1 | 2 | present |
| 765 | `family_ghost_scen99` | `WalkerOfTeamAlive` | team=1 | 1 | 2 | present |

15 of the 21 widened predicates carry no `(a)`/Widen comment. Each one
is a Phase 3 work item: tighten the predicate back to exact-value
semantics, or annotate the line with the divergence rationale that
justifies the looser bound, plus a `parity-fix:` or `intended_diff:`
commit. `family_skeleton_scen99:704` carries a five-line comment
explaining the divergence (dead skeleton retained on branch oblist) but
no `(a)` marker; rather than re-tagging the marker convention, Phase 3
will replace this with `WalkerAliveAtFinal(FAMILY_SKELETON, 0)` plus
`WalkerDiedByFinal(FAMILY_SKELETON)` so the count widening is no longer
load-bearing.

## (c) Structural-only coverage entries

`kFamilySpawns_golem_with_nonliving_targets`
(`tests/parity/scenario_table.h:433-476`) is the catch-all spawn list
used by `family_golem_scen99` (only consumer; verified at
scenario_table.h:1378). It parks one walker per missing weapon /
treasure / FX family on team 2 to flip the corresponding bit in
`CoverageObservation`. None of these families is referenced by any
`expected_facts[]` predicate's `arg0`, so the coverage gate goes green
on the bit alone — no event, weapon emission, treasure pickup, or
effect lifetime is asserted.

| line | (family, order) | only-via-golem-spawn? | predicate referencing arg0? |
|------|-----------------|-----------------------|-----------------------------|
| 438 | (1=ROCK, Weapon) | yes | no |
| 439 | (2=ARROW, Weapon) | yes | no |
| 440 | (5=METEOR, Weapon) | yes | no |
| 441 | (6=SPRINKLE, Weapon) | yes | no |
| 442 | (7=BONE, Weapon) | yes | no |
| 443 | (9=BLOB, Weapon) | yes | no |
| 444 | (10=FIRE_ARROW, Weapon) | yes | no |
| 445 | (12=GLOW, Weapon) | yes | no |
| 446 | (13=WAVE, Weapon) | yes | no |
| 447 | (14=WAVE2, Weapon) | yes | no |
| 448 | (15=WAVE3, Weapon) | yes | no |
| 449 | (16=CIRCLE_PROTECTION, Weapon) | yes | no |
| 450 | (17=HAMMER, Weapon) | yes | no |
| 451 | (18=DOOR, Weapon) | yes | no |
| 452 | (19=BOULDER, Weapon) | yes | no |
| 455 | (1=DRUMSTICK, Treasure) | yes | no |
| 456 | (2=GOLD_BAR, Treasure) | yes | no |
| 457 | (3=SILVER_BAR, Treasure) | yes | no |
| 458 | (4=MAGIC_POTION, Treasure) | yes | no |
| 459 | (5=INVIS_POTION, Treasure) | yes | no |
| 460 | (6=INVULNERABLE_POTION, Treasure) | yes | no |
| 461 | (7=FLIGHT_POTION, Treasure) | yes | no |
| 462 | (8=EXIT, Treasure) | yes | no |
| 463 | (9=TELEPORTER, Treasure) | yes | no |
| 464 | (10=LIFE_GEM, Treasure) | yes | no |
| 465 | (11=KEY, Treasure) | yes | no |
| 466 | (12=SPEED_POTION, Treasure) | yes | no |
| 469 | (0=EXPAND, FX) | yes | no |
| 470 | (1=GHOST_SCARE, FX) | yes | no |
| 471 | (7=BOOMERANG, FX) | yes | no |
| 472 | (8=CLOUD, FX) | yes | no |
| 473 | (9=MARKER, FX) | yes | no |
| 474 | (10=CHAIN, FX) | yes | no |
| 475 | (11=DOOR_OPEN, FX) | yes | no |

Every entry above is structural-only: the harness verifies the bit is
flipped, never that the entity emits / decays / collides correctly.

## (d) Master-companion SHA reconciliation

Two SHAs currently coexist:

| source | SHA | role |
|--------|-----|------|
| `.plan/parity-coverage-manifest.md` frontmatter (`master_companion_sha`) | `c9f18a7b1eead675a6b09ded9134ead6e8de5950` | live HEAD of `parity-companion` worktree |
| `.plan/master-companion.md` body (`Commit SHA`) | `ce70d23286f1e8034284e7c718ec658065f525e5` | historical commit recorded during initial master capture |

Live `git -C ../openglad-master rev-parse HEAD` returns
`c9f18a7b1eead675a6b09ded9134ead6e8de5950` (Phase 01 captured
verbatim). The reconciliation target is therefore the manifest
frontmatter SHA `c9f18a7b...`; the body of `master-companion.md`
records the original empty-world capture SHA and will be brought back
into sync in Phase 02. Phase 05 re-captures every committed golden
under `tests/parity/golden/` from the reconciled companion SHA so the
goldens, the manifest, and `master-companion.md` all agree.

sha1 of scenario_table mirrors (informational; Phase 02 reconciles
them):

| file | sha1 |
|------|------|
| `tests/parity/scenario_table.h` | `78a0aec5eee0d7729661a4ca84eca1bcc64fe37b` |
| `../openglad-master/tools/parity_scenario_table.h` | `abca15a89397f7128db640b1c794848f834ba056` |

## (e) Stale-document rename log

The pre-existing `parity-divergence-report.md` and `parity-fixes.md`
were written for the empty-world era (the harness's initial 15-row
state where every scenario was effectively a `nullptr` placeholder).
Their content is no longer authoritative; they describe a divergence
inventory and a Phase 07 fix log that pre-date the entire walker /
special / effect / smoke expansion that produced today's 39 goldens.
They are preserved (not deleted) so the history of how the harness got
here remains in tree.

| original path | new path | reason |
|---------------|----------|--------|
| `.plan/parity-divergence-report.md` | `.plan/parity-divergence-report-empty-world.md` | empty-world era; superseded by Phase 02–05 work in parity-finish-2 |
| `.plan/parity-fixes.md` | `.plan/parity-fixes-empty-world.md` | empty-world era; superseded by Phase 06 (canary regressions) and Phase 07 (anti-cheating locks) |

Renames use `git mv` so `git log --follow` still finds the history.
`.plan/parity-signoff-fraudulent.md` is left in place by name as the
ground-truth record of the previous agent's claimed-clean state, which
this plan is contradicting.

## (f) Coverage-gap inventory by axis

### Walker families lacking a behavioural axis predicate (HP / position)

Every required walker family has at least one liveness predicate
(`WalkerFamilyCount` plus `WalkerDiedByFinal` or `WalkerAliveAtFinal`),
but only `SOLDIER`, `MAGE`, and `ARCHMAGE` carry a `WalkerPositionMoved`
or `WalkerHpRangeAtFinalTick` predicate. The first missing behavioural
axis for each family is below.

| family | has HP pred? | has position pred? | first missing axis |
|--------|--------------|--------------------|--------------------|
| SOLDIER | yes | yes | (none — HP/position both present) |
| ELF | no | no | HP |
| ARCHER | no | no | HP |
| MAGE | no | yes | HP |
| SKELETON | no | no | HP |
| CLERIC | no | no | HP |
| FIREELEMENTAL | no | no | HP |
| FAERIE | no | no | HP |
| SLIME | no | no | HP |
| SMALL_SLIME | no | no | HP |
| MEDIUM_SLIME | no | no | HP |
| THIEF | no | no | HP |
| GHOST | no | no | HP |
| DRUID | no | no | HP |
| ORC | no | no | HP |
| BIG_ORC | no | no | HP |
| BARBARIAN | no | no | HP |
| ARCHMAGE | no | yes | HP |
| GOLEM | no | no | HP |
| GIANT_SKELETON | no | no | HP |
| TOWER1 | no | no | HP |

### Weapon families with no `WeaponFamilyEmitted` predicate

`kRequiredWeaponFamilies` lists 20 weapon families; zero of them appear
as `arg0` of any `pred::WeaponFamilyEmitted(...)` call in
`scenario_table.h`. Phase 04 work items: ADD `WeaponFamilyEmitted`
predicates for KNIFE, ROCK, ARROW, FIREBALL, TREE, METEOR, SPRINKLE,
BONE, BLOOD, BLOB, FIRE_ARROW, LIGHTNING, GLOW, WAVE, WAVE2, WAVE3,
CIRCLE_PROTECTION, HAMMER, DOOR, BOULDER.

### Treasure families with no behavioural predicate

`kRequiredTreasureFamilies` lists 13 treasure families; zero appear in
any `pred::TreasureFamilyRemovedFromOblist(...)` or
`pred::StatDeltaOnPickup(...)` call. Phase 04 work items: STAIN,
DRUMSTICK, GOLD_BAR, SILVER_BAR, MAGIC_POTION, INVIS_POTION,
INVULNERABLE_POTION, FLIGHT_POTION, EXIT, TELEPORTER, LIFE_GEM, KEY,
SPEED_POTION.

### Effect families with no `EffectFamilyCount` predicate

`kRequiredEffectFamilies` lists 13 effect families. The only
`EffectFamilyCount` call site is
`kFacts_special_cleric_scen124:551` whose `arg0=0`; its inline comment
labels that as `FAMILY_SOLDIER` but in FX-order space `0` is
`FAMILY_EXPAND` — that single call site is mislabeled and the
effect-family axis is otherwise unexercised. Phase 04 work items
(every required effect family): EXPAND, GHOST_SCARE, BOMB, EXPLOSION,
FLASH, MAGIC_SHIELD, KNIFE_BACK, BOOMERANG, CLOUD, MARKER, CHAIN,
DOOR_OPEN, HIT.

### Specials only claimed by `Exercises::Special_*` bit

`kRequiredSpecials` lists 42 `(family, special_index)` pairs. The
coverage gate (`Parity.coverage_gate_specials`) is satisfied when the
union of `Exercises::Special_*` bits across `kScenarios` covers every
required pair. Today every `Special_*` claim is "structural" in the
sense that the bit is OR-ed onto the row but the row's
`expected_facts[]` never inspects `walker::current_special` post-tick.
The specials inventoried in `family_<X>_scen99` rows that have **no
matching event / effect / weapon predicate** are therefore "claimed
only":

- FAMILY_SOLDIER (4 specials: 1–4)
- FAMILY_ELF (4 specials: 1–4)
- FAMILY_ARCHER (3 specials: 1–3)
- FAMILY_MAGE specials 2–5 (only `special_mage_scen126` runs a mage
  special with a behavioural predicate, and it covers `Special_Mage_1`
  via `kInputsSpecialOnce20` — the family row covers 2–5 by bit alone)
- FAMILY_SKELETON (1 special: 1)
- FAMILY_CLERIC specials 2–4 (`special_cleric_scen124` covers index 1
  behaviourally)
- FAMILY_FIREELEMENTAL (1 special: 1)
- FAMILY_SLIME (1 special: 1)
- FAMILY_SMALL_SLIME (1 special: 1)
- FAMILY_MEDIUM_SLIME (1 special: 1)
- FAMILY_THIEF specials 2–4 (`special_thief_scen789` covers index 1
  behaviourally via `EventKindAtLeast(play_sound, 31)`)
- FAMILY_GHOST (1 special: 1)
- FAMILY_DRUID specials 1, 3, 4 (`summon_druid_pet_scen950` covers
  index 2 behaviourally)
- FAMILY_ORC (2 specials: 1–2)
- FAMILY_BARBARIAN (2 specials: 1–2)
- FAMILY_ARCHMAGE specials 2–4 (`special_archmage_scen123` covers
  index 1 behaviourally)

### Event kinds with no `EventKindAtLeast`/`EventKindExactly` predicate

`kRequiredEventKinds` lists 9 emitted event names. The five with NO
fact predicate referencing them are work items for Phase 4:

| event kind | ordinal | covered by fact predicate? |
|------------|---------|----------------------------|
| `play_sound` | 1 | yes |
| `notification` | 2 | no |
| `set_palette` | 3 | no |
| `request_redraw` | 4 | no |
| `end_game` | 5 | no |
| `set_end` | 6 | no |
| `request_exit_confirmation` | 7 | yes |
| `withdraw_to_level` | 8 | yes |
| `score_change` | 9 | yes |

## (g) Mutation-canary delta

`discriminating_mutation` declares the single source-line edit Phase 02
(canary) applies to verify each row's predicates can flip. Two rows
admit, by inspection, that the parity runner does not actually invoke
the mutated subject — the predicates therefore cannot flip and the
canary either spuriously passes or has nothing to assert against:

| scenario | mutation | reason the runner does not invoke the subject |
|----------|----------|-------------------------------------------------|
| `save_roundtrip_scen99` | `kMut_save_corrupt` (`src/resources/save_data.cpp:107`) | the parity runner does not save/load: the row uses `kFamilySpawns_soldier` with no input and no save round-trip stage, so flipping `temp_version` cannot affect the dump |
| `rng_seed_stable_scen99` | `kMut_save_corrupt` (`src/resources/save_data.cpp:107`) | same: this row is an RNG-stability check; the runner never calls the save subsystem under any code path, so the mutation is a no-op against this row |

Phase 06 (canary + regressions) replaces both mutations with edits the
runner does invoke (a per-tick RNG advance for `rng_seed_stable_scen99`
and a save/load round-trip stage for `save_roundtrip_scen99`).

## Closing

The honest position: the parity surface is broad (50 tests, 39
goldens), but its assertions are still mostly liveness counts plus
structural-bit coverage. 15 of 21 widened predicates have no inline
justification; 0 of 20 weapon families, 0 of 13 treasure families, and
0 of 13 effect families are exercised by a behavioural fact predicate;
2 mutations target subsystems the runner does not invoke. Phases 03–07
of `.plan/plan.md` are scoped to close each gap and replace every
widened bound with either an exact predicate or a documented
`intended_diff` row.
