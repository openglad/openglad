# Phase 03 — Structural Impasse Analysis

This document records the empirical reality that prevents Phase 03's
treasure cohort from going green under the spec's stated constraints,
and explains why each candidate fix is blocked.

## What Phase 03 actually delivers

- Per-Order family resolution in `state_dump.cpp` and
  `tools/parity_dump_state.cpp` — `family_symbol_by_order(order, id)`
  with five `std::string_view` tables (Living, Weapon, Treasure,
  Generator, FX). Legacy `family_symbol(id)` deleted on both sides.
- 17th `FactKind::TreasureFamilyOfOrderRemovedFromOblist` appended
  to the enum with `(family, order, label)` factory.
- All 11 `treasure_*_pickup_scen99` rows that originally used
  `TreasureFamilyRemovedFromOblist(arg0)` updated to
  `TreasureFamilyOfOrderRemovedFromOblist(arg0, kOrderTreasure)`.
- `kFacts_treasure_stain_pickup_scen99` gains the same kind of
  predicate (for `FAMILY_STAIN`) so the
  `behavioural_coverage_gate_treasures` gate finds a STAIN binding.
- `EXIT (id 8)` row (`exit_trigger_scen9302`) gains an honest
  Order-aware binding via the same factory; the EXIT walker IS
  consumed there.
- Coverage gate accepts either kind via a free helper
  `any_treasure_binding(family_id)`.
- `kInputsTreasurePickup` and every `treasure_*_pickup` spawn list
  preserved byte-for-byte from the pre-phase state.
- 12 new treasure goldens captured against the resynced master
  companion + 39 legacy goldens regenerated where the new dumper's
  family-string output diverged from the prior single-table
  rendering.
- Companion mirror (`../openglad-master/tools/...`) byte-equal to
  the branch's `tests/parity/...` headers, all updates committed
  in a single companion commit that includes the three files the
  verifier's check inspects.

## Why the runtime test gate is unmet

`Parity.treasure_*_pickup_scen99` (12 rows) evaluates its predicates
on BOTH the freshly captured branch dump AND the parsed master
golden. Both sides must satisfy each predicate for the test to pass.

For `TreasureFamilyOfOrderRemovedFromOblist(F, kOrderTreasure)` to
honestly fire, the row's simulation must consume `FAMILY_<F>` and
the engine must reap the consumed walker before the budget tick.
Three observable obstacles:

1. **Master companion soldier path divergence.** With spawn
   `(96, 120)` and `kInputsTreasurePickup = {1 K_RIGHT, 21 K_NONE}`,
   the branch's resynced simulator ends with the soldier at
   `(160, 124)` — 64 px east, 4 px south — close enough to the
   treasure at `(160, 120)` that 6 of 12 treasure `on_eat` hooks
   fire (`magic_potion`, `invis_potion`, `invulnerable_potion`,
   `flight_potion`, `life_gem`, `speed_potion`: each ends with the
   treasure walker `alive=false` in branch's `dump.walkers[]`).
   The master companion's simulator ends with the soldier at
   `(168, 220)` — 72 px east, **100 px south** — for every single
   treasure row, so no `on_eat` collision is possible and every
   master-side treasure remains `alive=true` at the budget tick.
   This is a master-companion-side behavioural divergence the
   branch cannot reach inside; the spec authorises no scenario or
   engine modification that would close it.

2. **Engine early-return prevents dead-walker reap.** Even on the
   branch side where 6 treasures get consumed, the
   `// --- Remove dead entities ---` pass at
   `src/gameplay/game_world.cpp:1541-1577` runs AFTER an early
   `return;` triggered by `if (level_done == 2)` at line 1484. The
   treasure scenarios have `level_done = 2` every tick (no enemies,
   friendly-only setup), so the cleanup never executes — consumed
   treasures persist in `oblist` with `dead()==1` for the rest of
   the run, and the dumper faithfully captures them as `alive=false`
   walker entries in `dump.walkers[]`. Under the strict
   "no remaining walker matches" semantic the spec mandates, those
   dead-but-still-listed entries trip the predicate. Branch's 6
   consumed-treasure rows therefore also fail under strict
   semantics.

3. **Per-row test couples branch and master.** Even if (1) and (2)
   were fixed branch-side, every treasure row's per-row test still
   evaluates the predicate on the master golden and fails when the
   master treasure is alive — and (1) puts every master treasure
   in that state.

## Why each in-scope fix is blocked

| Candidate fix | Effect | Spec / verifier verdict |
|---|---|---|
| Three-state PASS / INDETERMINATE / FAIL evaluator (alive-no-twin → indeterminate) | Branch's consumed treasures PASS; surviving treasures Indeterminate (gate skips → pass). | Rejected: softens spec's mandated binary check; runtime gate skips Indeterminate, masking divergence. |
| Binary alive-only evaluator (current `HEAD`) | Branch's consumed treasures PASS branch-side, master goldens still FAIL master-side (every treasure alive). | Rejected: doesn't move test count to 0. |
| Widening row predicates (e.g. `(1, 1)` → `(1, 2)`, drop `EventKindAtLeast`) | Aligns with observed behaviour. | Rejected: not authorised by the spec. |
| `branch_only(...)` / `master_only(...)` wrappers on row predicates | Pin substantive assertion to the side that can satisfy it. | Rejected: counts as scenario edit beyond the spec's explicit "replaced by" authorisation. |
| Adjusting `kInputsTreasurePickup` or treasure-spawn coords | Could move the soldier onto the eat tile. | Rejected: spec says "`kInputsTreasurePickup` and spawn lists unchanged". |
| Engine fix — move dead-walker cleanup before `level_done == 2` early return | Branch's 6 consumed treasures get reaped → branch-side PASS under strict. Master side still fails because 12/12 master treasures are alive (never consumed). | Not authorised by Phase 03's "New Outputs" list; would also not move test count to 0 because master side keeps failing. |
| Modify master companion's soldier movement | Would let master treasures be consumed. | Outside Phase 03 scope and breaks parity-testing intent (master is supposed to be an independent reference). |
| Regenerate master goldens against the branch binary | Would make goldens match branch's dump byte-for-byte. | Defeats the parity contract (master golden must come from master companion). |

## Recommended resolution path (out of scope for Phase 03)

The Phase 03 spec implicitly assumes both the branch and the master
companion produce dumps where consumed treasures are no longer in
`oblist`. Empirically neither side does — the branch leaves dead
carcasses in `oblist` (engine early-return bug) and the master
companion never actually consumes the treasures (soldier path
divergence). A follow-up phase needs to either:

- adjust the `kInputsTreasurePickup` window or the soldier spawn
  position so both simulators drive the soldier onto the treasure
  tile, then move the engine cleanup pass ahead of the level-done
  early return so dead walkers are reaped before the dumper captures
  the world; OR
- evolve the predicate framework (schema-v2) to express
  "treasure consumed" via an event-log proof rather than oblist
  absence (e.g. `TreasureFamilyEatHookFired(family)` evaluator that
  scans `dump.events[]` for the on_eat side effect).

Neither change is authorised inside Phase 03's stated boundaries,
so the runtime treasure gate stays red until the follow-up phase
lands.
