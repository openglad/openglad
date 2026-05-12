# Phase 05 — Special-Ability Scenarios

## Phase Name
Per-family per-special-index coverage.

## Implement Phase ID
`05-special-ability-scenarios`

## Preexisting Inputs
- All outputs of Phase 04 (walker-family scenarios + goldens committed).
- `tests/parity/scenario_table.h` (extended with `SpawnSpec`,
  `exercises`, `fresh_arena`, etc.).
- `tests/parity/coverage_targets.h::kRequiredSpecials[]`.
- `src/gameplay/families/family_*.cpp` (canonical special-action bindings).
- `../openglad-master/src/gameplay/families/family_*.cpp` (mirror surface).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `../openglad-master/build/ci-test/parity_dump_master` (rebuilt against
  the SHA in `.plan/parity-coverage-manifest.md` frontmatter).
- `scripts/parity/capture_master_golden.sh`,
  `scripts/parity/validate_schema.py`.

## New Outputs
- ~40-60 new `ScenarioSpec` entries (one per `(family, special_index)`
  pair enumerated in `kRequiredSpecials[]`). Upper bound is
  `NUM_SPECIALS=6 × NUM_FAMILIES=21 = 126`, but only families with
  `do_special` bindings have specials.
- Naming: `special_<family>_<idx>_scen<base>`, e.g.
  `special_archmage_1_scen99`. Index ordering matches the family
  descriptor's `special_actions[]` index.
- Input sequences:
  - For `special_index == 0`:
    `inputs = { {10, 0, K_SPECIAL}, {11, 0, K_NONE} }`.
  - For higher indices, step the active special with
    `K_SPECIAL_SWITCH` first, e.g. for `special_index == 2`:
    `{ {5, 0, K_SPECIAL_SWITCH}, {6, 0, K_NONE},
       {7, 0, K_SPECIAL_SWITCH}, {8, 0, K_NONE},
       {10, 0, K_SPECIAL},        {11, 0, K_NONE} }`.
- Mirror entries committed to `../openglad-master/tools/parity_scenario_table.h`.
- Per-scenario goldens under `tests/parity/golden/special_*.json`,
  captured **in this phase** by the rebuilt master companion
  (`capture_master_golden.sh tests/parity/golden/` filtered to the new
  ids), schema-validated, and committed. Canonical — Phase 07 only
  re-captures into a throwaway directory.
- `.plan/parity-coverage-manifest.md` updated with covering scenario
  ids for every `(family, special_index)` pair.

## File Changes
- Modified: `tests/parity/scenario_table.h`.
- Modified: `../openglad-master/tools/parity_scenario_table.h`.
- New: per-scenario goldens under `tests/parity/golden/special_*.json`.
- Modified: `tests/parity/test_parity_scenarios.cpp` (add macro
  invocations).
- Modified: `.plan/parity-coverage-manifest.md`.

## Implementation Details
- The four pre-existing special-X scenarios from the original table
  (`special_archmage_scen123`, `special_cleric_scen124`,
  `special_mage_scen126`, `special_thief_scen789`) are **renamed** to
  fit the new convention and re-pointed at the spawn-injection model.
  Their `.fss` files remain on disk; Phase 05 just stops loading them
  except via the new specs.
- Summoning specials (druid familiar, cleric heal, mage rocks) need
  `tick_budget >= 80` for the spawned entity to appear in the dump.
- Caster-only specials (magic shield, invisibility) rely on the dump's
  applied-effect `lifetime` field, which the differ already compares.
- Capture sequence mirrors Phase 04: invoke the master companion built
  from the SHA recorded in the manifest frontmatter; validate schema;
  commit alongside the scenario table entries.

## Verification Phases
- **Phase ID**: `05a-check-specials-coverage`
  - **Type**: `check`
  - **Bounce Target**: `05-special-ability-scenarios`
  - **Purpose**: Confirm every `(family, special_index)` pair in
    `kRequiredSpecials[]` is observed (by `exercises` bit or by an
    effect/event known to be produced by the special).
  - **Commands**:
    ```
    cmake --build --preset ci-test --target og_test_parity
    ./build/ci-test/og_test_parity \
        --gtest_filter='Parity.coverage_gate_specials'         # exits 0
    ```

- **Phase ID**: `05b-check-byte-equal-vs-master`
  - **Type**: `check`
  - **Bounce Target**: `05-special-ability-scenarios`
  - **Purpose**: Per-scenario `diff -q` between branch goldens
    (`tests/parity/golden/special_*.json`) and freshly captured master
    dumps. Re-derives evidence rather than reading a status document.
  - **Commands**:
    ```
    (cd ../openglad-master && cmake --build --preset ci-test \
        --target parity_dump_master)
    for id in $(ls tests/parity/golden/special_*.json \
                  | xargs -n1 basename | sed 's/\.json$//'); do
      ../openglad-master/build/ci-test/parity_dump_master \
          --scenario "$id" --out "/tmp/${id}.json"
      diff -q "tests/parity/golden/${id}.json" "/tmp/${id}.json"
    done
    ./build/ci-test/og_test_parity --gtest_filter='Parity.special_*'
    ```
    All `diff -q` invocations and the test run must exit 0.

## Success Criteria
- All `special_*` parity tests pass.
- `Parity.coverage_gate_specials` reports zero uncovered pairs.
- Every new `tests/parity/golden/special_*.json` is byte-equal to the
  re-captured master dump.
- `.plan/parity-coverage-manifest.md` shows
  `covering_scenario_id` filled for every `(family, special_index)` in
  `kRequiredSpecials[]`.

## Git Commit Requirement
Two commits **before yielding**:
1. Branch-side: `git add` scenario table, test registrations, manifest
   updates, and all new `tests/parity/golden/special_*.json`;
   `git commit -m "parity-redo: phase 05 — per-family special-ability
   scenarios + goldens"`.
2. Master-side: `git -C ../openglad-master add
   tools/parity_scenario_table.h` and
   `git -C ../openglad-master commit -m "parity-companion: phase 05 —
   special-ability scenario mirror"`.
Both worktrees must have clean status at verification time.
