# Phase 08 — Specials completeness and RNG-insensitivity rule

## Phase Name
All 42 special slots exercised with deterministic verifying predicates; lint rule enforces at least one RNG-insensitive predicate per `SemanticParity` row.

## Implement Phase ID
`08-specials-completeness-and-rng-insensitivity`

## Preexisting Inputs
- `tests/parity/coverage_targets.h::kRequiredSpecials[]` (42 pairs).
- Existing `special_<family>_<idx>_scen99` rows (42, mostly skipped before phase 04).
- `Exercises` bitset in `tests/parity/scenario_table.h`.
- `scripts/parity/lint_scenario_facts.py` (existing 4 rules).
- `tests/parity/test_parity_scenarios.cpp`.
- `tests/parity/scenario_facts_generated.json`.
- `tests/parity/golden/*.json`.
- `../openglad-master/build/ci-test/parity_dump_master`.
- `scripts/parity/capture_master_golden.sh`.
- `.plan/parity-coverage-manifest.md`.

## New Outputs
- For every `(family, slot)` pair in `kRequiredSpecials`, ensure exactly one `special_<lowercase_family>_<slot>_scen99` row with:
  - `fresh_arena = true`, target family spawned, a player on the same team holding the special button.
  - `inputs[]` injecting the corresponding special-button key at a tick chosen so the special actually fires (lookup via `src/gameplay/families/family_<x>.cpp::do_special`).
  - `Exercises::Special_<bit>` set in the row's exercises bitmap.
  - `expected_facts[]` containing at least one RNG-insensitive predicate that specifically verifies the special fired. Examples by slot type:
    - Teleport-style → `WalkerPositionMoved(FAMILY_<X>, dx_min, dx_max)` with `dx_max-dx_min >= 100`.
    - Projectile-emit → `WeaponFamilyEmitted(FAMILY_<projectile>, min=1)`.
    - Effect-emit → `EffectFamilyCount(FAMILY_<effect>, source=FAMILY_<X>, min=1)`.
    - Self-buff (e.g. CIRCLE_PROTECTION) → `WalkerFamilyCount(FAMILY_<X>, 1, 1)` AND `EffectFamilyCount(FAMILY_CIRCLE_PROTECTION, source=FAMILY_<X>, 1, 1)`.
    - Whirlwind/melee → `EventKindAtLeast("play_sound", 2)` plus `WalkerOfTeamAlive(enemy_team, 0, k)`.
- **New lint rule `requires_rng_insensitive_predicate`** in `scripts/parity/lint_scenario_facts.py`:
  - For every row with `compare_mode == SemanticParity`, the `expected_facts[]` must contain at least one predicate of `RNG_INSENSITIVE_KINDS = {WalkerFamilyCount, EffectFamilyCount, WeaponFamilyEmitted, EventKindAtLeast, EventKindExactly, WalkerDiedByFinal, WalkerAliveAtFinal, TreasureFamilyRemovedFromOblist, TreasureFamilyOfOrderRemovedFromOblist, LevelDoneEquals, TickReached}`.
  - Emits a structured violation `(row_id, "no_rng_insensitive_predicate")` and the linter exits non-zero.
  - Exempt set is **computed** (not enumerated): the rule loads `tests/parity/scenario_facts_generated.json`, reads each row's `compare_mode`, and skips every row whose `compare_mode != "SemanticParity"`. With today's table this exempts `rng_seed_stable_scen99` (Invariant) and any `ByteEqual` rows; no hand-edited exempt list.
- Mirror updated.
- New/replacement goldens.
- `.plan/parity-coverage-manifest.md` updated for specials section.

## File Changes
- `tests/parity/scenario_table.h` (42 rows audited; many existing inputs adjusted).
- `tests/parity/test_parity_scenarios.cpp` (`OG_PARITY_TEST` confirmed for each special row).
- `scripts/parity/lint_scenario_facts.py` (new rule).
- `tests/parity/scenario_facts_generated.json` (regenerate).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `tests/parity/golden/special_*.json` (capture).
- `.plan/parity-coverage-manifest.md`.

## Implementation Details
- Slot-to-effect mapping is read from `src/gameplay/families/family_<x>.cpp`. For unfamiliar specials, run the scenario, capture the dump, and look at the actual delta — the predicate should pin the observed delta tightly.
- For specials whose observable effect is purely RNG-driven (e.g. random teleport), use widely bracketed `WalkerPositionMoved` with `// rng_drift:` justification AND pair with an RNG-insensitive `EventKindAtLeast` of the special's sound effect (so the row still satisfies the new lint rule).

## Verification Phases

### `08a-check-every-special-row-and-bit`
- Type: `check`
- Bounce target: `08-specials-completeness-and-rng-insensitivity`
- Purpose: Every required `(family, slot)` pair has a row that spawns the family, sets the slot bit, and pins the special with an RNG-insensitive predicate.
- Commands:
  - In-line `python3 -c '<...>'` against `tests/parity/scenario_facts_generated.json`: for each `(family, slot)` in `kRequiredSpecials`, assert a row whose `family_spawns` includes the family AND whose `exercises` bitmap has `Special_<slot>` set AND whose `expected_facts[]` contains an RNG-insensitive predicate.

### `08b-check-lint-rule-trips-and-passes`
- Type: `check`
- Bounce target: `08-specials-completeness-and-rng-insensitivity`
- Purpose: The new lint rule passes on the real file AND trips on a realistic bypass.
- Commands:
  - `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h` exits 0 on the real file.
  - Anti-cheat: create throwaway worktree `/tmp/parity-bypass-08b` via `git worktree add /tmp/parity-bypass-08b HEAD`. In that worktree, `sed -i` removes every RNG-insensitive predicate from `family_archer_scen99` (a `SemanticParity` row). Then `python3 scripts/parity/lint_scenario_facts.py /tmp/parity-bypass-08b/tests/parity/scenario_table.h` exits non-zero with stdout containing `no_rng_insensitive_predicate`. Cleanup: `git worktree remove --force /tmp/parity-bypass-08b`.

### `08c-check-specials-tests-green`
- Type: `check`
- Bounce target: `08-specials-completeness-and-rng-insensitivity`
- Purpose: Special tests all pass; full suite still green.
- Commands:
  - `build/ci-test/og_test_parity --gtest_filter='Parity.special_*' 2>&1 | tee /tmp/p08c.out`. `grep -cE '^\[  FAILED  \]' /tmp/p08c.out` equals `0`. `grep -cE '^\[  SKIPPED \]' /tmp/p08c.out` equals `0`.
  - `build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p08c-full.out`. `grep -cE '^\[  FAILED  \]' /tmp/p08c-full.out` equals `0`.

## Success Criteria
- All three check phases (`08a`, `08b`, `08c`) pass.
- Every `(family, slot)` in `kRequiredSpecials` has a row that exercises the special with an RNG-insensitive predicate.
- New lint rule passes on the real table and trips on a realistic bypass mutation.
- All `Parity.special_*` tests pass with no skips; full suite still `[  FAILED  ] 0`.
- Throwaway worktree cleaned up.

## Git Commit Requirement
Commit BOTH worktrees before yielding.

Companion (in `../openglad-master`):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 08 — mirror specials rows"
```

Branch:
```
git add tests/parity/scenario_table.h \
        tests/parity/test_parity_scenarios.cpp \
        scripts/parity/lint_scenario_facts.py \
        tests/parity/scenario_facts_generated.json \
        tests/parity/golden/ \
        .plan/parity-coverage-manifest.md
git commit -m "parity-cov: phase 08 — specials completeness and RNG-insensitivity rule"
```
