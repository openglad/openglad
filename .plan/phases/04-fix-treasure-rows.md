# Phase 04 — Per-Order family-symbol resolution + 17th FactKind

**Phase Name**: Treasure pickup scenarios pass via producer-side per-Order `family_symbol` resolution AND a new `TreasureFamilyOfOrderRemovedFromOblist` predicate kind. Schema-v1 JSON keys/types/ordering unchanged. Treasure cohort and the two behavioural gate tests (`behavioural_coverage_gate`, `behavioural_coverage_gate_treasures`) all green at end of phase. The WIP `FAMILY_SOLDIER` player walker at `(96,120)` from Phase 1 stays in place; no `FAMILY_DRUID` workaround.

**Implement Phase ID**: `04-fix-treasure-rows`

## Preexisting Inputs

- `tests/parity/scenario_table.h` (Phase 1 committed WIP)
- `tests/parity/golden/treasure_*.json` (Phase 3 captured under old dumper; replaced this phase)
- `../openglad-master/tools/parity_scenario_table.h` (Phase 2 byte-equal sync)
- `../openglad-master/tools/parity_dump_state.cpp`
- `../openglad-master/tools/fact_predicate.h`
- `../openglad-master/build/ci-test/parity_dump_master`
- `tests/parity/fact_predicate.{h,cpp}`
- `tests/parity/state_dump.{h,cpp}`
- `tests/parity/test_parity_coverage_gate.cpp`
- `tests/parity/parity_runner.cpp`, `tests/parity/scenario_runtime.cpp`
- `include/openglad/core/order.h` (`enum class Order` with Living=0, Weapon=1, Treasure=2, Generator=3, ...)
- `.plan/parity-present-state.md` (Phase 03 update)

## New Outputs

1. Updated `tests/parity/state_dump.cpp`:
   - New helper `std::string family_symbol_for_entity(const walker& w)` resolving the family symbol via `w.query_order()`. Table is `static constexpr std::array<std::array<const char*, kFamilyMax>, kOrderMax>` populated from the same source-of-truth as the existing `family_symbol(int)`. Resulting table must contain, at minimum, treasure-order names for ids 0–12 and weapon-order names for ids 0–19.
   - `collect_walkers` uses `family_symbol_for_entity(*w)` instead of `family_symbol(static_cast<int32_t>(w->family()))`.
   - `collect_effects` and `collect_weapons` keep their existing order-specific resolution — no edit needed.
2. Matching update to `../openglad-master/tools/parity_dump_state.cpp`: same helper, same call-site swap, same per-Order table.
3. Updated `tests/parity/fact_predicate.h`:
   - New enum entry `TreasureFamilyOfOrderRemovedFromOblist` appended (17th and last; no ordinals shift).
   - New `inline constexpr FactPredicate TreasureFamilyOfOrderRemovedFromOblist(std::int32_t family, std::int32_t order, std::string_view label = {}) noexcept` factory with `order` immediately after `family` (positional), defaulted `label` last.
   - Existing factory `TreasureFamilyRemovedFromOblist(std::int32_t family, std::string_view label = {})` preserved verbatim.
4. Updated `tests/parity/fact_predicate.cpp`:
   - New private helper `std::string family_symbol_by_order(std::int32_t order, std::int32_t family)` matching the dumper's table. Add `static_assert` that Treasure-order row's `kTreasureSymbol[0] == "FAMILY_STAIN"` and `kTreasureSymbol[8] == "FAMILY_EXIT"`.
   - `evaluate_one` gains case `FactKind::TreasureFamilyOfOrderRemovedFromOblist`: resolve `target_symbol = family_symbol_by_order(arg1, arg0)` (`arg1` is Order ordinal, `arg0` is family id); fail if any walker in `dump.walkers[]` has `walker.family == target_symbol` and `walker.alive == true`.
   - Existing `TreasureFamilyRemovedFromOblist` case untouched.
5. Matching update to `../openglad-master/tools/fact_predicate.h`: same enum addition.
6. Updated `tests/parity/test_parity_coverage_gate.cpp`:
   - Extend `any_predicate_binds` (`test_parity_coverage_gate.cpp:277`) with a helper:
     ```cpp
     bool any_treasure_binding(std::int32_t fam) {
         return any_predicate_binds(FactKind::TreasureFamilyRemovedFromOblist, fam)
             || any_predicate_binds(FactKind::TreasureFamilyOfOrderRemovedFromOblist, fam);
     }
     ```
     Swap the two `missing_family_bindings` callers for the treasure ledger to `any_treasure_binding`.
7. Updated `tests/parity/scenario_table.h`:
   - Every `kFacts_treasure_<F>_pickup_scen99[]` array swaps `pred::TreasureFamilyRemovedFromOblist(F, "...")` to `pred::TreasureFamilyOfOrderRemovedFromOblist(F, kOrderTreasure, "...")`. `kOrderTreasure` is defined at top of file (`inline constexpr std::uint8_t kOrderTreasure = 2;`).
   - Treasure-row spawn data and per-row `WalkerPositionMoved` / `WalkerHpRangeAtFinalTick` predicates remain on `FAMILY_SOLDIER` (Phase 1 WIP). `WalkerHpRangeAtFinalTick` HP value pinned only after a two-run stability check (see Implementation Details).
   - `kFacts_treasure_stain_pickup_scen99[]`: STAIN's `init_ignore=true` means the entity stays in oblist; WIP commit already stripped removal predicates and Phase 4 does NOT re-add one. Row keeps `TickReached(150)` + `WalkerPositionMoved(FAMILY_SOLDIER, 144, 120)`.
7a. **New dedicated structural-binding row `treasure_stain_and_exit_binding_scen99`** appended to `kScenarios`:
   ```cpp
   inline constexpr SpawnSpec kFamilySpawns_treasure_stain_and_exit_binding[] = {
       { /*FAMILY_ARCHER*/2, /*team*/0, kOrderLiving, 224, 224, 0, 0 },
   };
   inline constexpr FactPredicate kFacts_treasure_stain_and_exit_binding_scen99[] = {
       pred::TickReached(30),
       pred::WalkerFamilyCount(/*FAMILY_ARCHER*/2, 1, 1),
       pred::TreasureFamilyOfOrderRemovedFromOblist(/*FAMILY_STAIN*/0, /*order*/kOrderTreasure),
       pred::TreasureFamilyOfOrderRemovedFromOblist(/*FAMILY_EXIT*/8,  /*order*/kOrderTreasure),
   };
   inline constexpr Mutation kMut_treasure_stain_and_exit_binding = {
       "src/runtime/game_loop.cpp", /*line*/<TICK_INCREMENT_LINE>,
       "++screen->level_tick_count;",
       "/* tick freeze */",
       "Freezes screen->level_tick_count at its initial value; every TickReached(N>0) predicate flips because the dump's tick field never advances past 0."
   };
   ```
   Inputs: `kInputsEmpty`. `tick_budget=30`, `CompareMode::SemanticParity`, `is_branch_internal=false`. Append `OG_PARITY_TEST(treasure_stain_and_exit_binding_scen99)` to `tests/parity/test_parity_scenarios.cpp`. Phase 4's `--all` sweep writes `tests/parity/golden/treasure_stain_and_exit_binding_scen99.json`.
8. Updated `tests/parity/golden/*.json`: mass re-capture via `scripts/parity/capture_master_golden.sh --all`.
9. Append `## After Phase 04 — treasure rows green` to `.plan/parity-present-state.md` with literal `Passed: <P>`, `Skipped: <S>`, `Failing tests: <F>` integers. Add `## Phase 04 — FAMILY_STAIN/FAMILY_EXIT binding row` subsection citing the new row id, the `git show` line range in `tests/parity/scenario_table.h`, and the literal mutation line number for `kMut_treasure_stain_and_exit_binding`.
10. Mirror update (two-worktree commit pair):
    - Branch commit: `parity-finish-3: phase 04 — per-order family_symbol; +TreasureFamilyOfOrderRemovedFromOblist; treasure rows green`
    - Companion commit: `parity-companion: phase 04 — mirror dumper per-order family_symbol + FactKind addition`

## File Changes

- `tests/parity/state_dump.cpp` (helper + call-site)
- `tests/parity/fact_predicate.h` (enum entry + factory)
- `tests/parity/fact_predicate.cpp` (helper + evaluator case)
- `tests/parity/test_parity_coverage_gate.cpp` (cross-kind binding)
- `tests/parity/scenario_table.h` (treasure predicate migration + new structural row + spawn/facts/mut arrays)
- `tests/parity/test_parity_scenarios.cpp` (append `OG_PARITY_TEST(treasure_stain_and_exit_binding_scen99)`)
- `../openglad-master/tools/parity_dump_state.cpp` (mirror helper)
- `../openglad-master/tools/fact_predicate.h` (mirror enum)
- `../openglad-master/tools/parity_scenario_table.h` (`cp` from branch)
- `tests/parity/golden/*.json` (recapture; directory-wide `git add`)
- `.plan/parity-present-state.md` (append)

Mirror commit sequence (literal):

```
cp tests/parity/state_dump.cpp     ../openglad-master/tools/parity_dump_state.cpp || \
  echo "manual port required: helper added to branch state_dump.cpp"
# If branch state_dump.cpp differs structurally from companion parity_dump_state.cpp,
# port helper + call-site by hand. Verify with: grep -n family_symbol_by_order \
#   ../openglad-master/tools/parity_dump_state.cpp
cp tests/parity/fact_predicate.h   ../openglad-master/tools/fact_predicate.h
cp tests/parity/scenario_table.h   ../openglad-master/tools/parity_scenario_table.h
BRANCH_SHA=$(git rev-parse HEAD)
git -C ../openglad-master add tools/parity_dump_state.cpp \
                               tools/fact_predicate.h \
                               tools/parity_scenario_table.h
git -C ../openglad-master commit -m \
  "parity-companion: phase 04 — mirror dumper per-order family_symbol + FactKind addition (branch ${BRANCH_SHA})"
cd /home/yans/code/openglad-master && \
  cmake --build --preset ci-test --target parity_dump_master && \
  cd /home/yans/code/openglad
scripts/parity/capture_master_golden.sh --all
git add tests/parity/state_dump.cpp \
        tests/parity/fact_predicate.h tests/parity/fact_predicate.cpp \
        tests/parity/test_parity_coverage_gate.cpp \
        tests/parity/test_parity_scenarios.cpp \
        tests/parity/scenario_table.h tests/parity/golden \
        .plan/parity-present-state.md
git commit -m "parity-finish-3: phase 04 — per-order family_symbol; +TreasureFamilyOfOrderRemovedFromOblist; treasure rows green"
```

`git add tests/parity/golden` is intentionally directory-wide. Do NOT narrow to `tests/parity/golden/treasure_*.json` — that would leave stale non-treasure recaptures out of the commit and trip verifier 04c.

## Implementation Details

- Producer-side change is the minimum to disambiguate treasure-order entities. Per-Order `family_symbol` is content-only; schema-v1 JSON keys/types/ordering are frozen.
- New FactKind appended at end of enum so no existing ordinal shifts. Lint refers to kinds by string name.
- **HP-pinning stability** for `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, hp, hp)`: implement agent runs recapture TWICE (`scripts/parity/capture_master_golden.sh --all --out-dir /tmp/recap1 --no-write` then `--out-dir /tmp/recap2 --no-write`) and `cmp -s` every treasure golden between the two dirs. If any pair diverges, demote to `WalkerAliveAtFinal(FAMILY_SOLDIER, 1)` or `WalkerDiedByFinal(FAMILY_SOLDIER)`. The 2-arg `WalkerAliveAtFinal(family, min_alive)` signature exists at `tests/parity/fact_predicate.h:161-164`; do NOT invent a third argument.
- Companion's `parity_dump_master` is rebuilt after the companion-side mirror commit.
- After recapture, diff the resulting treasure goldens against Phase-3 captures and confirm only `family` strings for non-Living oblist entries changed. Log diff into `.plan/parity-present-state.md` `## Phase 04 diff summary`.

## Verification Phases

### `04a-check-treasure-rows-and-gates-pass`
- **Type**: `check`
- **Bounce target**: `04-fix-treasure-rows`
- **Purpose**: confirm the treasure cohort plus both behavioural gates are zero-SKIP zero-FAIL, the full-suite FAIL count has dropped by ≥13, no previously-passing scenario regressed, the gate code mentions the new FactKind, and structural bindings exist for FAMILY_STAIN(0) and FAMILY_EXIT(8).
- **Commands**:
  ```
  cmake --build --preset ci-test --target og_test_parity
  build/ci-test/og_test_parity \
    --gtest_filter='Parity.treasure_*:Parity.behavioural_coverage_gate*' --gtest_brief=1 \
    2>&1 | tee /tmp/p04-cohort.out
  test "$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p04-cohort.out)" -eq 0
  test "$(grep -cE '^\[  SKIPPED \] Parity\.' /tmp/p04-cohort.out)" -eq 0

  PHASE3_FAIL=$(awk '/^## After Phase 03/,0' .plan/parity-present-state.md \
                | grep -oE '^Failing tests: [0-9]+' | awk '{print $3}' | head -1)
  build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p04.out
  POST_FAIL=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p04.out)
  test "$POST_FAIL" -le "$((PHASE3_FAIL - 13))"

  PHASE3_FAIL_IDS=$(awk '/^### Newly-FAILing/,/^## /' .plan/parity-present-state.md \
                    | grep -oE 'Parity\.[a-z_0-9]+' | sort -u)
  POST_FAIL_IDS=$(grep -oE '^\[  FAILED  \] Parity\.[a-z_0-9]+' /tmp/p04.out \
                  | awk '{print $NF}' | sort -u)
  comm -23 <(printf '%s\n' "$POST_FAIL_IDS") <(printf '%s\n' "$PHASE3_FAIL_IDS") \
    | { read -r line; test -z "$line" || { echo "regression: $line"; exit 1; }; }

  test "$(grep -cE 'TreasureFamilyOfOrderRemovedFromOblist' tests/parity/test_parity_coverage_gate.cpp)" -ge 1

  python3 - <<'PY'
  from pathlib import Path
  from scripts.parity.lint_scenario_facts import _load_table, parse_predicate_calls
  text = _load_table(Path('tests/parity/scenario_table.h'))
  calls = parse_predicate_calls(text)
  bound = {0: False, 8: False}
  for arr, preds in calls.items():
      for p in preds:
          if p['kind'] != 'TreasureFamilyOfOrderRemovedFromOblist':
              continue
          args = p['args']
          if not args:
              continue
          fam = args[0].strip().split('/')[-1].split('*')[-1].strip()
          try:
              fam_int = int(fam)
          except ValueError:
              continue
          if fam_int in bound:
              bound[fam_int] = True
  assert bound[0], 'no row binds FAMILY_STAIN(0) under TreasureFamilyOfOrderRemovedFromOblist'
  assert bound[8], 'no row binds FAMILY_EXIT(8) under TreasureFamilyOfOrderRemovedFromOblist'
  PY
  ```

### `04b-check-dumper-emits-per-order-symbols-and-new-factkind`
- **Type**: `check`
- **Bounce target**: `04-fix-treasure-rows`
- **Purpose**: confirm both dumpers carry the per-Order helper, schema-v1 keys are unchanged, the freshly-captured STAIN golden contains both FAMILY_STAIN and FAMILY_SOLDIER walkers, the FactKind enum has exactly 17 entries with both old and new kinds, and the new factory signature is `(family, order, label)`.
- **Commands**:
  ```
  grep -nE 'family_symbol_by_order|family_symbol_for_entity|order.*family_symbol' \
    tests/parity/state_dump.cpp
  grep -nE 'family_symbol_by_order|family_symbol_for_entity|order.*family_symbol' \
    ../openglad-master/tools/parity_dump_state.cpp
  python3 -c "
  import json
  d = json.load(open('tests/parity/golden/treasure_stain_pickup_scen99.json'))
  keys = sorted(d.keys())
  expected = sorted(['effects','events','level_done','level_tick_count','rng_observable',
                     'rng_state','schema_version','score_per_team','tick','walkers','weapons'])
  for k in expected: assert k in keys, (k, keys)
  assert 'inventory_keys' not in keys, ('master golden carries inventory_keys', keys)
  fams = {w['family'] for w in d['walkers']}
  assert 'FAMILY_STAIN'   in fams, ('STAIN missing', fams)
  assert 'FAMILY_SOLDIER' in fams, ('SOLDIER missing', fams)
  "
  python3 -c "
  from pathlib import Path
  import re
  t = Path('tests/parity/fact_predicate.h').read_text()
  start = t.index('enum class FactKind')
  block = t[start:t.index('};', start)]
  names = re.findall(r'^\s+([A-Z][A-Za-z0-9_]*),', block, re.MULTILINE)
  assert len(names) == 17, names
  assert 'TreasureFamilyRemovedFromOblist'        in names
  assert 'TreasureFamilyOfOrderRemovedFromOblist' in names
  "
  grep -nE 'TreasureFamilyOfOrderRemovedFromOblist\s*\(\s*std::int32_t\s+family\s*,\s*std::int32_t\s+order' \
    tests/parity/fact_predicate.h
  test "$(grep -cE 'inline constexpr FactPredicate TreasureFamilyRemovedFromOblist\(std::int32_t family,\s*std::string_view label' tests/parity/fact_predicate.h)" -eq 1
  ```

### `04c-check-mirror-and-goldens-fresh`
- **Type**: `check`
- **Bounce target**: `04-fix-treasure-rows`
- **Purpose**: confirm the mirror is byte-equal, the companion binary rebuilt this phase, every golden touched by HEAD is byte-stable under recapture, and both worktrees have the expected files in their last commit.
- **Commands**:
  ```
  diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
  cd /home/yans/code/openglad-master && cmake --build --preset ci-test --target parity_dump_master
  test -x ../openglad-master/build/ci-test/parity_dump_master
  rm -rf /tmp/recap && mkdir -p /tmp/recap
  scripts/parity/capture_master_golden.sh --all --out-dir /tmp/recap --no-write
  AFFECTED=$(git show --name-only HEAD \
             | grep -E '^tests/parity/golden/.*\.json$' \
             | xargs -n1 basename | sed 's/\.json$//')
  test -n "$AFFECTED" || { echo "no goldens touched by HEAD"; exit 1; }
  for id in $AFFECTED; do
    cmp -s "tests/parity/golden/$id.json" "/tmp/recap/$id.json" \
      || { echo "DIFF: $id"; exit 1; }
  done
  git log -1 --name-status | grep -F tests/parity/state_dump.cpp
  git log -1 --name-status | grep -F tests/parity/fact_predicate.h
  git log -1 --name-status | grep -F tests/parity/test_parity_coverage_gate.cpp
  git log -1 --name-status | grep -F tests/parity/scenario_table.h
  git log -1 --name-status | grep -F tests/parity/golden/
  git log -1 --name-status | grep -F .plan/parity-present-state.md
  git -C ../openglad-master log -1 --name-status | grep -F tools/parity_dump_state.cpp
  git -C ../openglad-master log -1 --name-status | grep -F tools/parity_scenario_table.h
  git -C ../openglad-master log -1 --name-status | grep -F tools/fact_predicate.h
  ```

## Success Criteria

- Treasure cohort + both behavioural-gate tests pass (zero SKIP, zero FAIL).
- Full-suite FAIL count drops by ≥13 versus Phase 3, with no non-deferred regression.
- Both dumpers carry `family_symbol_by_order` / `family_symbol_for_entity`.
- `FactKind` enum has exactly 17 entries; both old and new treasure kinds present; new factory has positional `(family, order, label)` signature.
- Mirror SHAs equal; companion `parity_dump_master` rebuilt this phase; every golden touched by HEAD is byte-stable under recapture.
- HEAD on both worktrees lists the expected files.

## Git Commit Requirement

The implementer **must** create both commits before yielding:
1. Companion commit `parity-companion: phase 04 — mirror dumper per-order family_symbol + FactKind addition (branch <branch-sha>)` on `../openglad-master`.
2. Branch commit `parity-finish-3: phase 04 — per-order family_symbol; +TreasureFamilyOfOrderRemovedFromOblist; treasure rows green`.

Companion binary must be rebuilt **after** the companion commit and **before** `scripts/parity/capture_master_golden.sh --all`. Branch `git add tests/parity/golden` must be directory-wide.
