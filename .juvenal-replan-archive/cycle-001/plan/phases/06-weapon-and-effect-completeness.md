# Phase 06 — Weapon and effect family completeness

## Phase Name
Every weapon family observed in `weapons[]` and every effect family in `effects[]`, via dedicated rows with RNG-insensitive predicates.

## Implement Phase ID
`06-weapon-and-effect-completeness`

## Preexisting Inputs
- `tests/parity/coverage_targets.h::kRequiredWeaponFamilies` (20), `::kRequiredEffectFamilies` (13).
- Existing `weapon_*_emission_scen99` and `effect_*_emission_scen99` rows (most skipped in baseline; goldens captured in phase 04).
- `.plan/parity-present-state.md` gap inventory tagged "needs emission predicate".
- `.plan/parity-coverage-manifest.md`.
- `tests/parity/scenario_table.h`.
- `tests/parity/test_parity_scenarios.cpp`.
- `tests/parity/scenario_facts_generated.json`.
- `tests/parity/golden/*.json`.
- `../openglad-master/build/ci-test/parity_dump_master`.
- `scripts/parity/capture_master_golden.sh`.
- `scripts/parity/lint_scenario_facts.py` (existing 4 rules).
- `scripts/parity/check_coverage_manifest.py`.

## New Outputs
- Rows ensuring each weapon family is emitted at least once with `WeaponFamilyEmitted(FAMILY_<W>, min_count=1)`. Emission must arise from a real spawn/special invocation (e.g. for `FAMILY_FIREBALL`, spawn `FAMILY_MAGE` and inject the magic-fire special input).
- Rows ensuring each effect family is observed with `EffectFamilyCount(FAMILY_<E>, source_family_qualifier, min, max)`. `EffectFamilyCount` must be qualified to avoid `effect_count_unqualified` lint violation.
- For weapons/effects that cannot be exercised without an opponent (e.g. `FAMILY_BLOOD`), add a two-team scenario with a deterministic-strike opening input.
- `.plan/parity-coverage-manifest.md` updated for weapon and effect sections.
- Mirror updated.
- New/replacement goldens for affected rows.

## File Changes
- `tests/parity/scenario_table.h` (rows added/modified).
- `tests/parity/test_parity_scenarios.cpp` (new `OG_PARITY_TEST` entries).
- `tests/parity/scenario_facts_generated.json` (regenerate).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `tests/parity/golden/weapon_*.json`, `tests/parity/golden/effect_*.json` (capture).
- `.plan/parity-coverage-manifest.md`.

## Implementation Details
- The 20 weapon families list (`KNIFE, ROCK, ARROW, FIREBALL, TREE, METEOR, SPRINKLE, BONE, BLOOD, BLOB, FIRE_ARROW, LIGHTNING, GLOW, WAVE, WAVE2, WAVE3, CIRCLE_PROTECTION, HAMMER, DOOR, BOULDER`) is enumerated in `kRequiredWeaponFamilies`. For each, identify the canonical emitting family (e.g. ARCHER → ARROW, MAGE → FIREBALL, BARBARIAN → HAMMER) and choose inputs that fire the weapon within the tick budget.
- Where weapon emission requires RNG-sensitive AI behaviour, set `tick_budget` high enough that emission probability across the run is effectively 1, and use `WeaponFamilyEmitted(family, min_count=1)`. Cite the RNG concession with an inline `// rng_drift:` comment so `lint_scenario_facts.py::unjustified_widening` accepts it.

## Verification Phases

### `06a-check-weapon-and-effect-emitted`
- Type: `check`
- Bounce target: `06-weapon-and-effect-completeness`
- Purpose: Every required weapon and effect family is bound by an emission predicate; linter does not trip on these rows.
- Commands:
  - In-line `python3 -c '<...>'` against `tests/parity/scenario_facts_generated.json`: for every name in `kRequiredWeaponFamilies`, assert at least one row has a `WeaponFamilyEmitted(<that family>, min>=1)` predicate. For every name in `kRequiredEffectFamilies`, assert at least one row has an `EffectFamilyCount(<that family>, qualified, min>=1)` predicate.
  - `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h` exits 0 (`effect_count_unqualified` and `vacuous_event_floor` must not trip).

### `06b-check-tests-green-and-mirror`
- Type: `check`
- Bounce target: `06-weapon-and-effect-completeness`
- Purpose: Weapon and effect emission tests green; mirror unchanged on companion-side filename.
- Commands:
  - `build/ci-test/og_test_parity --gtest_filter='Parity.weapon_*_emission_scen99:Parity.effect_*_emission_scen99' 2>&1 | tee /tmp/p06b.out`. `grep -cE '^\[  FAILED  \]' /tmp/p06b.out` equals `0`. `grep -cE '^\[  SKIPPED \]' /tmp/p06b.out` equals `0`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

### `06c-check-no-suite-regression`
- Type: `check`
- Bounce target: `06-weapon-and-effect-completeness`
- Purpose: Full parity suite green; manifest weapon + effect sections have no `(none yet)` cells.
- Commands:
  - `build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p06c.out`. `grep -cE '^\[  FAILED  \]' /tmp/p06c.out` equals `0`.
  - `python3 scripts/parity/check_coverage_manifest.py` exits 0 with no `(none yet)` in weapon + effect sections.

## Success Criteria
- All three check phases (`06a`, `06b`, `06c`) pass.
- Every required weapon and effect family is bound by an emission predicate.
- Linter trips no `effect_count_unqualified`/`vacuous_event_floor` violations.
- Weapon and effect emission tests green with no skips.
- Full parity suite remains `[  FAILED  ] 0`.
- Mirror still byte-equal.

## Git Commit Requirement
Commit BOTH worktrees before yielding.

Companion (in `../openglad-master`):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 06 — mirror weapon and effect family rows"
```

Branch:
```
git add tests/parity/scenario_table.h \
        tests/parity/test_parity_scenarios.cpp \
        tests/parity/scenario_facts_generated.json \
        tests/parity/golden/ \
        .plan/parity-coverage-manifest.md
git commit -m "parity-cov: phase 06 — weapon and effect family completeness"
```
