# Review findings — Replan: Parity Coverage Pass 2 (cycle #1, architect re-review #2)

All findings from the prior review pass are **resolved** in the current `.plan/plan.md`,
and every load-bearing codebase claim was independently re-verified against the live tree.

## Resolved

1. **(was SUBSTANTIAL) Mechanical `consequence:` assertion vs. drafted specs.** The
   per-phase checker enforces "≥1 consequence predicate" via a literal
   `grep -q 'consequence:'` on the `kFacts_<id>[]` block, which is stricter than the
   gate's kind-based `is_consequence_predicate`. The §3 specs now give all 16 new
   scenarios an explicit `consequence:`-labeled predicate, including the 6 that
   previously relied only on kind-based consequence:
   - `weapon_rock_slot2_emit_scen99` → `WeaponFamilyEmitted(FAMILY_ROCK, "consequence: …")` (plan:392)
   - `effect_protection_emit_scen99` → `WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION, "consequence: …")` (plan:465)
   - `input_hold_fire_search_scen99` → `WeaponFamilyEmitted(FAMILY_KNIFE, "consequence: …")` (plan:532)
   - `multiplayer_two_teams_scen99` → `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 9000, "consequence: …")` (plan:586)
   - `midcombat_partial_hp_scen99` → `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4000, 11000, "consequence: …")` (plan:656)
   - `consumable_inventory_state_scen99` → `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 9000, 13000, "consequence: …")` (plan:670)
   §2 (lines 235-240) and the standard implement sequence (step 1, lines 736-739) now
   state the explicit-label rule. For the four widened HP ranges, the single
   `consequence:` label also satisfies `label_exempted`
   (`test_parity_coverage_gate.cpp:702-707`).

2. **(was MINOR) Phase 1 conditional commit.** `impl-confirm-prior-work` now reads
   "if all green and committed, yield immediately — there is nothing to commit. Only
   if something regressed … you must commit the repair in both worktrees before
   yielding" (plan:304-308). Contract satisfied without implying a no-op commit.

## Independently re-verified (all accurate)

- State counts: `grep -c 'OG_PARITY_TEST(' …` = **141** (140 + `#define`); **139** goldens;
  mirror `diff -q` byte-identical. 156/155/157 target arithmetic correct.
- Mutation source lines byte-for-byte (tabs vs spaces): `walker.cpp:1219` (1 tab),
  `walker.cpp:1723` (2 tabs), `family_soldier.cpp:46` (12-sp), `family_archmage.cpp:239`
  (24-sp), `treasure_family_navigation.cpp:86` (8-sp), `game_world.cpp:1357`
  (`level_done = 2;`), `walker_combat.cpp:189` (4-sp).
- Coverage gates: exactly **7** tests match `*depth_gate*:*golden_evaluation_gate*:*mutation_canary*`;
  `_emission_scen99`/`_pickup_scen99` and `starts_with("special_")` buckets confirmed
  to exclude all 16 new ids; `is_consequence_predicate` kind-based reading and
  `label_exempted` prefixes confirmed.
- Wide-range gate thresholds (HP>5000, WalkerFamilyCount>+5, WalkerOfTeamAlive>+3,
  EffectFamilyCount>+10, EventKindAtLeast min `arg1<=0`) all consistent with the
  drafted §3 ranges/labels; no zero-floor events; `EventKindAtLeast(kind, min)` arg
  mapping confirmed in `fact_predicate.h:206-209`.
- `scenario_facts_generated ALL` target + `add_dependencies(og_test_parity …)` at
  `CMakeLists.txt:1859-1870`; the awk/grep mechanical assertion in §2 runs cleanly
  against the real `inline constexpr FactPredicate kFacts_<id>[] = {` declaration style.
- Topology: linear, inline-only, one deterministic `check` per implement with a single
  fixed `bounce_target`, no `bounce_targets`, no role-checker swarm; idempotency clause
  and commit-before-yield (incl. `scenario_facts_generated.json`) in every implement.

## Non-blocking note (not a defect)

- Phase 6 `input_special_switch_wrap_scen99` refers to "full list in the original spec"
  (plan:552) for its 26-element K_SPECIAL_SWITCH/K_NONE input list rather than inlining
  it. The exact list is present verbatim in the supplied stuck-workflow YAML available to
  the workflow generator, and §2 grants explicit numeric latitude (the fixed requirement
  is "lands on slot 3 FREEZE TIME"), so this does not force the implementer to invent a
  detail. Optional polish: inline the list in §3 for full self-containment.

No substantial new issues. The plan is specific enough to generate
`.plan/phases/*.md` and the workflow YAML without inventing missing details or making
topology decisions later.
