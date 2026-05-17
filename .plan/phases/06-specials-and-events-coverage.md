# Phase 06 — Specials, events, RNG-insensitive predicates

**Phase Name**: Drive remaining `[  FAILED  ]` and `[  SKIPPED ]` to zero. Every special_*, weapon_*, effect_*, generator_*, event_* scenario passes against its golden, with at least one RNG-insensitive predicate per `SemanticParity` row.

**Implement Phase ID**: `06-specials-and-events-coverage`

## Preexisting Inputs

- Phase 05 commit on tree
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `tests/parity/parity_runner.cpp`
- `tests/parity/scenario_runtime.cpp`
- `scripts/parity/lint_scenario_facts.py`
- `scripts/parity/capture_master_golden.sh` (Phase 03 `--all`/`--no-write` flags)
- `.plan/parity-present-state.md` (Phase 03 / 04 / 05 sections present)

## New Outputs

- Updated `tests/parity/scenario_table.h`:
  - Every `special_<family>_<idx>_scen99` row's `inputs[]` cycles to slot `<idx>` and casts; `expected_facts[]` includes at least one of `WeaponFamilyEmitted`, `EffectFamilyCount`, `WalkerFamilyCount(<summoned_family>, ≥1, ≤n)`, `EventKindAtLeast(<kind>, ≥1)`, `WalkerPositionMoved`, `WalkerHpRangeAtFinalTick` (with `mn==mx` if RNG-stable).
  - Every `weapon_<F>_emission_scen99` wielder spawn is set up so K_ATTACK fires weapon family F; primary fact is `WeaponFamilyEmitted(FAMILY_<F>)`.
  - Every `effect_<F>_emission_scen99` source spawn emits FX family F by tick budget; primary fact is `EffectFamilyCount(FAMILY_<F>, mn==mx, source=<wielder>)`.
  - Every `generator_<F>_emission_scen99` row sets `tick_budget=300` or higher; primary fact is `WalkerFamilyCount(FAMILY_<spawned>, mn>=1, mx<=master_pinned)`.
  - Every `event_<kind>_emission_scen99` row exercises a real gameplay path producing that event; primary fact is `EventKindAtLeast(<kind>, >=1)` or `EventKindExactly(<kind>, n)`.
  - Every widened `WalkerHpRangeAtFinalTick` and `WalkerOfTeamAlive` range is either narrowed to exact-value semantics or annotated with existing `intended_diff` / `rng_drift` markers.
  - For any row whose every predicate is RNG-sensitive, agent ADDS an RNG-insensitive predicate (never marks the row exempt). The only escape is to set `compare_mode != SemanticParity` on the scenario, justified in `.plan/parity-present-state.md` `## After Phase 06`.
  - `rng_seed_stable_scen99` and `save_roundtrip_scen99` (currently `SemanticParity` at `tests/parity/scenario_table.h:3365` and `:3392`) are flipped to `CompareMode::Invariant`.
- Possibly extended `tests/parity/scenario_runtime.cpp` for per-spawn `stats_level` / `magicpoints` overrides (verify before editing).
- Possibly extended `tests/parity/parity_runner.cpp` to allow `event_end_game_emission_scen99` to tick until `level_done==1` (only if needed; see decision tree).
- Updated golden files for every modified row (recapture via `scripts/parity/capture_master_golden.sh --all`).
- Append `## After Phase 06` to `.plan/parity-present-state.md` with final PASS / SKIP / FAIL counts (target: 150 / 0 / 0) and `## Compare-mode changes` subsection listing rows demoted from `SemanticParity` to `CompareMode::Invariant` (cap: ≤ 2 rows).
- Branch commit: `parity-finish-3: phase 06 — specials/events/RNG-insensitive predicates; zero skips zero failures`.
- Companion commit: `parity-companion: phase 06 — mirror scenario_table.h after specials/events pass`.

## File Changes

- `tests/parity/scenario_table.h` (large block of edits per row class).
- Optionally `tests/parity/scenario_runtime.cpp` and/or `tests/parity/parity_runner.cpp`.
- `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.
- `git -C ../openglad-master commit -m "parity-companion: phase 06 — mirror scenario_table.h after specials/events pass"`.
- `cd ../openglad-master && cmake --build --preset ci-test --target parity_dump_master`.
- `scripts/parity/capture_master_golden.sh --all`.
- Branch commit lists all files.

## Implementation Details

- Four specials cohorts (one per family × index):
  - `kInputsSpecialSlot1..5` exist; agent picks the matching one per slot and ensures `stats_level` / `magicpoints` in spawn spec are sufficient for the cycle gate (`sim_input_handler.cpp:218`) and firing gate (`living.cpp:532-533`).
  - For each `(family, idx)` pair, agent inspects the master golden post-capture and reads off the actual emitted weapon / effect / summoned walker — predicate is keyed to that observed entity.
- For RNG-sensitive HP variance, agent prefers replacing HP predicate with a discrete RNG-insensitive predicate ("alive at final tick", "died by final tick"). Only if no substitute exists does the row keep a widened HP range with inline `// rng_drift: <reason>; commit <sha>` citation.
- **Two-run HP-stability gate (mandatory).** Before pinning ANY `WalkerHpRangeAtFinalTick(F, hp, hp)` predicate (mn==mx):
  ```
  scripts/parity/capture_master_golden.sh --all --out-dir /tmp/recap1 --no-write
  scripts/parity/capture_master_golden.sh --all --out-dir /tmp/recap2 --no-write
  for f in tests/parity/golden/*.json; do
    id=$(basename "$f" .json)
    cmp -s "/tmp/recap1/$id.json" "/tmp/recap2/$id.json" \
      || { echo "UNSTABLE: $id"; exit 1; }
  done
  ```
  If any pair diverges, demote to `WalkerAliveAtFinal(F, 1)` or `WalkerDiedByFinal(F)` — both 2-arg factories; do NOT invent a third-arg `max_alive`.
- `event_end_game_emission_scen99` and `event_set_end_emission_scen99` rows: agent reads master golden, observes how many ticks master needs to emit `end_game` / `set_end`, uses that as `tick_budget`. **Runner-edit decision tree:** first run the row against the existing runner with `master_tick + 10`. If `level_done==1` emits inside that budget and the predicate fires, NO runner edit needed; row ships with fixed `tick_budget`. Otherwise the runner gains a single additive `terminate_on_level_done` flag on `SpawnSpec` (defaults false; only this row sets true); runner checks flag once per tick and exits the tick loop when both flag and `screen->level_done == 1` hold. Implementer cites which path was taken in `.plan/parity-present-state.md` `## After Phase 06`.
- `event_set_palette_emission_scen99`: organic source is level-load palette swap on `glad_main` start. Spawn player walker on `temp/scen/scen99.fss` and assert `EventKindAtLeast(set_palette, 1)`.
- `event_request_redraw_emission_scen99`: reuses scoring scenario; asserts `EventKindAtLeast(request_redraw, ≥1)`.
- `event_notification_emission_scen99`: reuses MAGE DIED notification path from `effect_chain_scen9410`.
- **`dead_predicate` lint interaction.** `scripts/parity/lint_scenario_facts.py:560-583` only rejects `pred::branch_only(pred::master_only(...))` or symmetric inversion. It does NOT flag bare RNG-insensitive predicates like `TickReached(N)`, `WalkerKeysApplied(K)`, `LevelDoneEquals(L)`, `WeaponFamilyEmitted(F)`. Agent is free to add any allow-listed RNG-insensitive predicate.

## Verification Phases

### `06a-check-zero-skip-zero-fail`
- **Type**: `check`
- **Bounce target**: `06-specials-and-events-coverage`
- **Purpose**: confirm `og_test_parity` is zero-FAIL zero-SKIP and `PASSED` equals the listed test count; CTest runs the binary green.
- **Commands**:
  ```
  cmake --build --preset ci-test --target og_test_parity
  build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p06.out
  FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p06.out)
  SKIPPED_LINES=$(grep -cE '^\[  SKIPPED \] Parity\.' /tmp/p06.out)
  test "$FAILED"        -eq 0
  test "$SKIPPED_LINES" -eq 0
  PASSED=$(grep -oE '^\[  PASSED  \] [0-9]+ tests?\.' /tmp/p06.out | awk '{print $3}' | head -1)
  LISTED=$(build/ci-test/og_test_parity --gtest_list_tests | grep -cE '^  [a-z_0-9]+')
  test "$PASSED" = "$LISTED"
  ctest --preset ci-test --output-on-failure -R '^og_test_parity$'
  ```

### `06b-check-every-row-has-rng-insensitive-pred`
- **Type**: `check`
- **Bounce target**: `06-specials-and-events-coverage`
- **Purpose**: confirm every `SemanticParity` row carries at least one RNG-insensitive predicate (no per-row escape hatch).
- **Commands**:
  ```
  python3 - <<'PY'
  from pathlib import Path
  from scripts.parity.lint_scenario_facts import (
      _load_table, parse_scenarios, parse_predicate_calls,
  )
  text = _load_table(Path('tests/parity/scenario_table.h'))
  scenarios = parse_scenarios(text)
  calls = parse_predicate_calls(text)
  semantic = {s['id'] for s in scenarios if s.get('compare_mode') == 'SemanticParity'}
  OK = {'TickReached','LevelDoneEquals','WeaponFamilyEmitted',
        'TreasureFamilyRemovedFromOblist','TreasureFamilyOfOrderRemovedFromOblist',
        'EventKindExactly','WalkerDiedByFinal','WalkerKeysApplied','WalkerAliveAtFinal'}
  bad = []
  for sid in semantic:
      arr = f'kFacts_{sid}'
      preds = calls.get(arr, [])
      ok = False
      for p in preds:
          k = p['kind']; args = p['args']
          if k in OK: ok = True; break
          if k == 'WalkerFamilyCount' and len(args) >= 3 and args[1] == args[2]: ok = True; break
          if k == 'EffectFamilyCount' and len(args) >= 3 and args[1] == args[2]: ok = True; break
          if k == 'EventKindAtLeast' and len(args) >= 2 and args[1] in {'1'}: ok = True; break
      if not ok:
          bad.append(sid)
  assert not bad, bad
  PY
  ```

### `06c-check-no-widening-without-citation`
- **Type**: `check`
- **Bounce target**: `06-specials-and-events-coverage`
- **Purpose**: confirm lint passes and every widened `WalkerHpRangeAtFinalTick` carries an `intended_diff` / `rng_drift` citation.
- **Commands**:
  ```
  python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h
  python3 - <<'PY'
  from pathlib import Path
  import re
  src = Path('tests/parity/scenario_table.h').read_text().splitlines()
  bad = []
  for i, line in enumerate(src):
      m = re.search(r'WalkerHpRangeAtFinalTick\(\s*[A-Za-z0-9_]+\s*,\s*(-?\d+)\s*,\s*(-?\d+)', line)
      if not m: continue
      mn, mx = int(m.group(1)), int(m.group(2))
      if mx - mn == 0: continue
      window = '\n'.join(src[i:i+4])
      if 'intended_diff' not in window and 'rng_drift' not in window:
          bad.append((i+1, line.strip()))
  assert not bad, bad
  PY
  ```

## Success Criteria

- `og_test_parity` reports zero FAIL and zero SKIP, with PASSED equal to listed-test count.
- Every `SemanticParity` row has at least one RNG-insensitive predicate.
- Lint exits 0; every widened HP range is followed (within 3 lines) by an `intended_diff` or `rng_drift` citation.
- `.plan/parity-present-state.md` has `## After Phase 06` with literal integers (150 / 0 / 0) and `## Compare-mode changes` listing at most two demoted rows.
- Mirror SHAs equal; companion `parity_dump_master` rebuilt post-mirror.

## Git Commit Requirement

The implementer **must** create both commits before yielding:
1. Companion commit `parity-companion: phase 06 — mirror scenario_table.h after specials/events pass`.
2. Branch commit `parity-finish-3: phase 06 — specials/events/RNG-insensitive predicates; zero skips zero failures`.

Companion binary must be rebuilt before `scripts/parity/capture_master_golden.sh --all` runs. Branch commit must include any modified runtime / runner files plus all updated goldens.
