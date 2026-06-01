# Phase 09 — Event kind completeness

## Phase Name
Every required event kind organically emitted by at least one row.

## Implement Phase ID
`09-event-kind-completeness`

## Preexisting Inputs
- `tests/parity/coverage_targets.h::kRequiredEventKinds[]` (9 strings).
- Existing `event_*_emission_scen99` rows.
- `tests/parity/scenario_table.h`.
- `tests/parity/test_parity_scenarios.cpp`.
- `tests/parity/scenario_facts_generated.json`.
- `tests/parity/golden/*.json`.
- `../openglad-master/build/ci-test/parity_dump_master`.
- `scripts/parity/capture_master_golden.sh`.
- `.plan/parity-coverage-manifest.md`.

## New Outputs
- For every kind in `kRequiredEventKinds` (`play_sound`, `notification`, `set_palette`, `request_redraw`, `end_game`, `set_end`, `request_exit_confirmation`, `withdraw_to_level`, `score_change`), at least one row whose run organically emits it AND has `EventKindAtLeast(kind, n>=1)` matched on both branch and master.
- For hard-to-emit kinds (`end_game`, `withdraw_to_level`), construct a minimal scenario that drives the game state into the emitting branch (e.g. exit-treasure pickup for `withdraw_to_level`, all-enemies-dead for `end_game`).
- Mirror updated.
- New/replacement goldens for affected rows.
- `.plan/parity-coverage-manifest.md` updated for events section.

## File Changes
- `tests/parity/scenario_table.h`.
- `tests/parity/test_parity_scenarios.cpp` (new `OG_PARITY_TEST` entries if needed).
- `tests/parity/scenario_facts_generated.json` (regenerate).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `tests/parity/golden/event_*.json` (capture).
- `.plan/parity-coverage-manifest.md`.

## Implementation Details
- For each kind, pick the simplest deterministic trigger in the gameplay code (e.g. `play_sound` is emitted by any combat strike; `withdraw_to_level` by EXIT-treasure pickup; `end_game` by killing all enemies on the final level).
- After authoring the row, regenerate `scenario_facts_generated.json` and recapture the golden.

## Verification Phases

### `09a-check-every-event-kind-bound`
- Type: `check`
- Bounce target: `09-event-kind-completeness`
- Purpose: Every required event kind is bound by at least one `EventKindAtLeast` predicate.
- Commands:
  - In-line `python3 -c '<...>'` against `tests/parity/scenario_facts_generated.json`: for every string `k` in `kRequiredEventKinds`, assert at least one row has `EventKindAtLeast(k, >=1)` in `expected_facts[]`.

### `09b-check-event-rows-green`
- Type: `check`
- Bounce target: `09-event-kind-completeness`
- Purpose: All `Parity.event_*` rows pass with no skips.
- Commands:
  - `build/ci-test/og_test_parity --gtest_filter='Parity.event_*' 2>&1 | tee /tmp/p09b.out`. `grep -cE '^\[  FAILED  \]' /tmp/p09b.out` equals `0`. `grep -cE '^\[  SKIPPED \]' /tmp/p09b.out` equals `0`.

### `09c-check-no-suite-regression`
- Type: `check`
- Bounce target: `09-event-kind-completeness`
- Purpose: Full suite green and skip-free; mirror byte-equal.
- Commands:
  - `build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p09c.out`. `grep -cE '^\[  FAILED  \]' /tmp/p09c.out` equals `0`. `grep -cE '^\[  SKIPPED \]' /tmp/p09c.out` equals `0`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

## Success Criteria
- All three check phases (`09a`, `09b`, `09c`) pass.
- Every required event kind is bound by a row's `EventKindAtLeast` predicate.
- `Parity.event_*` rows pass with no skips.
- Full parity suite is `[  PASSED  ] = total`, `[  SKIPPED ] 0`, `[  FAILED  ] 0`.
- Mirror byte-equal.

## Git Commit Requirement
Commit BOTH worktrees before yielding.

Companion (in `../openglad-master`):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 09 — mirror event kind rows"
```

Branch:
```
git add tests/parity/scenario_table.h \
        tests/parity/test_parity_scenarios.cpp \
        tests/parity/scenario_facts_generated.json \
        tests/parity/golden/ \
        .plan/parity-coverage-manifest.md
git commit -m "parity-cov: phase 09 — event kind completeness"
```
