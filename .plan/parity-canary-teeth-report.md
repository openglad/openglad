# Parity Mutation-Canary Teeth Report

Scope: the four `parity-cov: give toothless/stale scenarios canary teeth`
commits (rounds 1–4, `87a801ee` → `307d4f5b`). Goal of the pass: turn every
toothless/stale target scenario canary-POSITIVE — `run_mutation_canary.sh
--scenario <id>` reports `flips>=1` against a mutation that remains a genuine
break of the behaviour the scenario is named for, with the flipping predicate a
real, reasonably tight observation of that behaviour's consequence (no gaming).

## Headline result

- Target scenarios now canary-POSITIVE (teeth): **70**.
- Target scenarios STILL toothless/stale after all four rounds: **1** —
  `treasure_teleporter_pickup_scen99`.
- `ctest --preset ci-test -R '^og_test_parity'`: **0 failures** (output below).
  The full parity gtest binary runs **182 tests, all PASS** when invoked from
  the repo root (the working directory ctest uses).

## No-gaming attestation

No scenario in this report is marked "fixed" / "canary-positive" on the basis of
a predicate edit alone. A scenario is counted as having teeth ONLY because the
real mutation canary — which physically applies the scenario's
`discriminating_mutation` (the literal `from`→`to` swap at `file:line`) to the
actual game source, rebuilds, and re-evaluates the predicates — reported
`flips>=1`. The mutations are described in `kMut_*` and applied only by the
canary script; this report's verification process restored game source on exit
(`git diff src/` is clean — verified `SRC_CLEAN`). Predicates that flip are
genuine consequence observations (event counts, HP, alive/dead, positions), not
tautologies or trivially wide ranges; widened ranges in the touched scenarios
carry the gate-required `rng_drift:` / `intended_diff:` / `consequence:` labels.

## Fix shapes used (verified from the round-1..4 diff)

All three legitimate shapes were used; many scenarios combined them.

1. **Tighten / add a predicate (shape 1).** The dominant change in
   `scenario_table.h` across the four rounds was insertion/tightening of
   consequence predicates (event-count exacts, HP bands, alive/dead, position
   bands) into existing `kFacts_<id>[]` arrays so a predicate actually observes
   the consequence the mutation breaks. `fact_count` is `std::size(...)`-derived;
   no count was hand-edited.

2. **Strengthen the scenario, then recapture the master golden (shape 2).**
   **29 golden files were recaptured** in this pass (full list below). These are
   substantive recaptures, not metadata bumps: e.g.
   `treasure_drumstick_pickup_scen99.json` went from an empty-event, no-enemy
   final state to a full combat trace with an `FAMILY_ARCHER` enemy, score
   changes, and reduced soldier HP — i.e. the heal/pickup now has a visible
   effect because the walker is spawned into a real fight. Recaptured goldens:

   ```
   effect_bomb_emission effect_boomerang_emission effect_chain_emission
   effect_cloud_emission effect_door_open_emission effect_expand_emission
   effect_explosion_emission effect_flash_emission effect_ghost_scare_emission
   effect_magic_shield_emission effect_marker_emission special_cleric_scen124
   special_small_slime_1_scen99 special_thief_scen789 treasure_drumstick_pickup
   treasure_exit_pickup treasure_flight_potion_pickup treasure_gold_bar_pickup
   treasure_invis_potion_pickup treasure_invulnerable_potion_pickup
   treasure_key_pickup treasure_life_gem_pickup treasure_magic_potion_pickup
   treasure_silver_bar_pickup treasure_speed_potion_pickup treasure_stain_pickup
   treasure_teleporter_pickup weapon_fire_arrow_emission weapon_wave_emission
   ```

3. **Repoint the mutation (shape 3).** Stale mutations whose `from` text no
   longer byte-matched the source were repointed to the line that actually
   implements the named behaviour (correct `file`/`line`/`from`/`to`, exact
   tabs-vs-spaces). `kMut_*` descriptors edited in this pass include:
   `effect_bomb/boomerang/chain/cloud/door_open/expand/explosion/flash/
   ghost_scare/magic_shield_emission`, `exit_withdraw_path`,
   `family_archer_init`, `special_archmage_4`, `special_cleric_do_special`,
   `special_elf_4`, `special_thief_do_special`, `treasure_gold_bar_pickup`,
   `treasure_invulnerable_potion_pickup`, `treasure_life_gem_pickup`,
   `treasure_stain_pickup`, `walker_ai_wander`, `weapon_fire_arrow_emission`.
   These repoint into the real registries / family / handler sources
   (`treasure_family_registry.cpp`, `weapon_family_registry.cpp`,
   `effect_family_registry.cpp`, `generator_family_registry.cpp`,
   `sim_input_handler.cpp`, `family_ghost.cpp`, etc.). After repointing, each was
   confirmed by the canary to flip a predicate.

## The one scenario that is STILL toothless — honest root cause

`treasure_teleporter_pickup_scen99` — **flips=0, NOT fixed.** This was
independently re-verified during this report:

```
--- treasure_teleporter_pickup_scen99 ---
  mutation: src/gameplay/families/treasure_family_navigation.cpp:154
  pre:  gtest=PASS
  _apply_mutation: applied at ...:154
  post: gtest=PASS
  flips=0  detail=-
```

The mutation applies CLEANLY (it is not stale — the `from` text
`.on_eat = teleporter_on_eat,` byte-matches line 154), so the failure mode is
pure **toothlessness**: the mutation produces no observable change.

Root cause, verified by hand (apply mutation → rebuild → dump → restore):

- The scenario's named behaviour is "picking up the co-located teleporter bumps
  the eater's `skip_exit`, which trips `exit_on_eat`'s early `skip_exit()>1`
  guard so the co-located withdraw EXIT emits NEITHER `RequestExitConfirmation`
  (kind 7) NOR `WithdrawToLevel` (kind 8)." The two teeth predicates are
  `EventKindExactly(7,0)` and `EventKindExactly(8,0)`.
- On the **unmutated** branch and on the **master golden**, the EXIT already
  emits zero kind-7 and zero kind-8 events (verified: branch + master event-kind
  counts are `{play_sound, score_change}` only — no withdraw/exit-confirm of any
  kind).
- With the mutation applied (teleporter hook removed) and the runner rebuilt,
  the dump's event-kind counts are **byte-identical** to the unmutated run
  (`play_sound:42, score_change:9`; kind-7 count 0, kind-8 count 0). The EXIT
  still fires neither event.
- Therefore the predicates `EventKindExactly(7,0)`/`(8,0)` cannot flip: their
  observed value (0) is invariant under the mutation. The scenario's documented
  theory ("neutering the hook lets the EXIT fire both events, count 1") does NOT
  hold in this harness: the EXIT never reaches its withdraw branch regardless of
  the teleporter hook. The withdraw branch is gated by other preconditions
  (`exit_on_eat`'s `act_type()==ACT_CONTROL` + eat-distance + `can_withdraw`
  conditions) that this oblist-only spawn arrangement and the UP→RIGHT→FIRE
  input script do not satisfy at the moment the EXIT would be eaten, so the
  observable event stream is identical with or without the teleporter bump.

In other words, removing the teleporter `on_eat` hook is a meaningful code
change, but THIS scenario does not place the eater into a state where that
removal changes any state the schema-v1 dump can observe. Making it
canary-positive would require redesigning the scenario so the EXIT actually
takes its withdraw branch in the unmutated build (so the teleporter suppression
is the thing that toggles it) — i.e. a shape-2 strengthen with a fresh golden
recapture, plus likely an `exit_withdraw_path`-style mutation rather than the
teleporter-hook mutation. That redesign was not completed in this pass; the
scenario remains a known toothless holdout and is reported as such rather than
papered over. Its current predicates still PASS on both branch and master (it is
a valid, non-failing coverage anchor), but it does NOT have canary teeth.

## Required ctest output (verbatim)

Command: `ctest --preset ci-test --output-on-failure -R '^og_test_parity'`

```
Test project /home/yans/code/openglad/build/ci-test
    Start 24: og_test_parity
1/1 Test #24: og_test_parity ...................   Passed    2.87 sec

100% tests passed, 0 tests failed out of 1

Label Time Summary:
integration    =   2.87 sec*proc (1 test)

Total Test time (real) =   2.87 sec
```

0 failures. (Direct invocation of `og_test_parity` from inside `build/ci-test`
shows spurious failures because the golden files are resolved relative to the
working directory; ctest runs it from the repo root — the authoritative path —
where all 182 parity gtests pass.)

## Bottom line

70 of 71 target scenarios have genuine canary teeth, each verified by the real
mutation canary reporting `flips>=1`. One scenario,
`treasure_teleporter_pickup_scen99`, remains toothless (`flips=0`) because the
teleporter-hook mutation provably leaves the observable event stream unchanged
in this scenario's setup; it is documented here as an unresolved holdout, not
claimed fixed. The parity test suite is green (0 ctest failures, 182/182 gtests
pass).
