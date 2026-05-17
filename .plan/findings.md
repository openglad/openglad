## Findings — Plan Review (parity-finish-3, checker pass after fifth refinement)

The prior round's blocking finding (no row binds FAMILY_STAIN(0) /
FAMILY_EXIT(8) post-Phase-1) is **fully resolved**:

- Phase 4 §7a introduces a concrete new row
  `treasure_stain_and_exit_binding_scen99` whose `expected_facts[]`
  carries both `TreasureFamilyOfOrderRemovedFromOblist(0,
  kOrderTreasure)` and `TreasureFamilyOfOrderRemovedFromOblist(8,
  kOrderTreasure)`. The row spawns only a FAMILY_ARCHER walker, so
  both predicates pass trivially under the per-Order family-symbol
  resolution Phase 4 lands (no STAIN/EXIT walker exists in the
  dump's `walkers[]`).
- Phase 4 verifier 04a has a structural backstop (Python block that
  re-derives, from `tests/parity/scenario_table.h`, that family ids 0
  and 8 are both bound under `TreasureFamilyOfOrderRemovedFromOblist`).
- Phase 5 adds a separate `TEST(Parity,
  behavioural_coverage_gate_treasure_kinds_required)` case asserting
  the same — moves the binding requirement from a structural file
  walk to a real gtest case.
- Phase 4 §7 explicitly states the `treasure_stain_pickup_scen99` row
  keeps its WIP shape (`TickReached(150)` +
  `WalkerPositionMoved(FAMILY_SOLDIER, 144, 120)`) and does NOT
  re-add a STAIN-removal predicate; the binding is provided by the
  dedicated §7a row.

Spot-checks against the codebase confirm the plan's mechanical
claims:

- `FactKind` enum currently has exactly 16 entries
  (`tests/parity/fact_predicate.h:33-54`); the 17th `TreasureFamilyOfOrderRemovedFromOblist`
  appended at end matches the plan.
- `WalkerAliveAtFinal(family, min_alive)` is 2-arg as cited
  (`tests/parity/fact_predicate.h:161-164`).
- `CompareMode` has exactly `ByteEqual`, `Invariant`, `SemanticParity`
  (`tests/parity/scenario_table.h:46-51`); `BranchOnly` /
  `RngObservation` correctly identified as non-existent.
- `kOrderTreasure = 2` exists at `tests/parity/scenario_table.h:153`.
- `kInputsEmpty[1] = { {0, 0, K_NONE} }` exists at
  `tests/parity/scenario_table.h:228`.
- `enum class Order : unsigned char { Living=0, Weapon=1, Treasure=2,
  Generator=3, FX=4, Special=5, Button1=6 }` exists at
  `include/openglad/core/order.h:12-20`.
- `walker::query_order()` exists and is overridden by treasure
  (verified at `src/gameplay/treasure.cpp:94,110`).
- `dead_predicate` rule exists at
  `scripts/parity/lint_scenario_facts.py:560-583`; matches plan's
  cited line range.
- `load_exemptions()` parser exists at
  `scripts/parity/run_mutation_canary_runtime.py:74-91` and accepts
  exactly `#`-comments + `^-\s*<id>\s*$` rows + bare tokens.
- `behavioural_coverage_gate_no_dead_predicates` test exists at
  `tests/parity/test_parity_coverage_gate.cpp:572`.
- `tests/parity/golden/family_soldier_scen99.json` exists and is
  exercised by `OG_PARITY_TEST(family_soldier_scen99)`
  (`tests/parity/test_parity_scenarios.cpp:172`).
- `fact_predicate.h` between branch and master is currently byte-equal
  (`diff -q` returns rc=0) — Phase 4's `cp` mirror step is sound.
- `treasure` overrides `query_order() -> Order::Treasure` so the new
  `family_symbol_for_entity(w)` will resolve correctly.

### Non-blocking observations

These are real but do not block workflow generation; the plan's
verifiers catch them and the bounces are well-bounded.

1. **`kMut_treasure_stain_and_exit_binding` cites a non-existent
   source file.** Plan Phase 4 §7a defines:
   ```
   { "src/runtime/game_loop.cpp", /*line*/<TICK_INCREMENT_LINE>,
     "++screen->level_tick_count;", "/* tick freeze */", ... }
   ```
   No `src/runtime/game_loop.cpp` exists in the tree — only
   `src/runtime/game_loop.h`. The actual tick-advance statement is
   `tick_count_++;` at `src/gameplay/game_world.cpp:1359`
   (or `level_tick_count_++;` at line 1366). `game_frame()` lives at
   `src/platform/sdl/game_loop.cpp:434`, NOT in `src/runtime/`, and
   the parity runner does not invoke the SDL `game_frame` — it calls
   `world.tick()` directly (`tests/parity/parity_runner.cpp:124`).

   The plan does include a fallback ("If no exact-match
   `++screen->level_tick_count;` line exists in the present tree, the
   agent picks any tick-advance statement in `game_frame()` whose
   removal verifiably keeps `dump.tick == 0`"), but the `game_frame()`
   reference in the fallback is also stale. The implementer would
   need to discover the real mutation site
   (`src/gameplay/game_world.cpp:1359`'s `tick_count_++;`) and
   adjust `file`, `line`, `before`, `after` accordingly.

   The mutation is data in `scenario_table.h`; Phase 4 ships it
   without applying it. The first verifier to detect a broken mutation
   is Phase 7 verifier 07a (canary). That is a 3-phase bounce
   distance, which is workable but rough.

2. **Phase 4 verifier 04a's FAIL-delta arithmetic prose is
   internally inconsistent.** Plan Phase 1 inventory says "total 13
   FAIL" composed of "11 `treasure_*_pickup_scen99` failures, the
   `treasure_stain_pickup_scen99` row's distinct failure, and the 2
   behavioural-gate failures" (which arithmetically is 11 + 1 + 2 =
   14, not 13). Plan §1 also states "The 12th `treasure_*_pickup_scen99`
   id, `treasure_stain_pickup_scen99`, does NOT appear in the
   failing-test set" — so stain is NOT failing.

   Plan Phase 4 verifier 04a asserts `FAIL count ≤ Phase 3 FAIL − 13`
   citing "11 treasure + 1 stain + 2 gate failures Phase 1 inventoried
   all close in this phase". The "1 stain" inclusion conflicts with
   Phase 1's "does NOT appear" claim.

   If stain genuinely doesn't fail (no master golden in Phase 1 means
   it's SKIPPED, then Phase 3 captures golden and either turns it
   PASS or new-FAIL), the −13 delta is correct (11 pickup + 2 gate);
   the "1 stain" mention is sloppy prose. If stain newly fails in
   Phase 3 and Phase 4 closes it, the delta should be −14. The
   verifier hard-codes −13.

   Recommendation: pick one accounting and reconcile the prose. Until
   then the implementer may bounce 04a with an off-by-one mismatch.

3. **"The two `missing_family_bindings` callers for the treasure
   ledger swap to `any_treasure_binding` instead" is wrong by count.**
   There is exactly ONE `missing_family_bindings(...,
   FactKind::TreasureFamilyRemovedFromOblist)` call site — at
   `tests/parity/test_parity_coverage_gate.cpp:327-330` in
   `behavioural_coverage_gate_treasures`. The umbrella
   `behavioural_coverage_gate` (cpp:399) uses its own internal
   `append_family` lambda that calls `any_predicate_binds` directly
   for `treasure_family` (cpp:421-424).

   For BOTH gates to accept the new FactKind (Phase 4 verifier 04a
   asserts both green), the implementer must:
   - swap the `missing_family_bindings` call in
     `behavioural_coverage_gate_treasures` to `any_treasure_binding`,
     AND
   - patch the lambda inside `behavioural_coverage_gate` (or replace
     its treasure call with the new helper).

   Verifier 04a's test-count assertion catches a missed second
   patch (umbrella gate would FAIL), so the bounce is short — but
   the plan instruction is imprecise.

4. **Family-collision prose mismatch.** Plan §1 claims treasure ids
   "0/1/2/3/4/8 collide with walker families FAMILY_SOLDIER, FAMILY_ELF,
   FAMILY_ARCHER, FAMILY_MAGE, FAMILY_SKELETON, FAMILY_CLERIC". Per
   `include/openglad/core/constants.h:51,54`, FAMILY_CLERIC=5 and
   FAMILY_SLIME=8. The correct enumeration for id 8 is FAMILY_SLIME.
   Cosmetic but worth fixing — Phase 1's failure-message section
   uses "treasure_family: FAMILY_SLIME" elsewhere (correct), so the
   inline prose contradicts itself.

VERDICT: The previously-blocking finding is resolved. The plan is
structurally sound: linear topology, fixed bounce_targets, explicit
verifiers, inline-only YAML preserved, commit-before-yield restated,
Preexisting vs. New Outputs cleanly separated. The four observations
above are real but non-blocking — verifiers catch each one within a
finite bounce loop, and the topology / artifact flow does not change.
Workflow generation may proceed.
