# Phase 01 — Semantic-parity contract and predicate framework

## Phase Name
Replace byte-equal contract with semantic-equivalence predicates.

## Implement Phase ID
`01-semantic-parity-contract`

## Preexisting Inputs
- `.plan/goal.md`
- `.plan/parity-harness-design.md`
- `.plan/parity-coverage-manifest.md`
- `tests/parity/parity_runner.h`
- `tests/parity/parity_runner.cpp`
- `tests/parity/scenario_runtime.h`
- `tests/parity/scenario_runtime.cpp`
- `tests/parity/state_dump.h`
- `tests/parity/state_dump.cpp`
- `tests/parity/scenario_table.h` (39 pre-existing `ScenarioSpec` rows)
- `tests/parity/test_parity_scenarios.cpp`
- `tests/parity/test_parity_coverage_gate.cpp`
- `tests/parity/coverage_targets.h`
- `tests/parity/golden/*.json` (38 canonical master dumps pinned to `master_companion_sha: 952b7b4155ab44931bc86d07de30f4edf77c37a7`)
- `../openglad-master/tools/parity_scenario_table.h`
- `../openglad-master/tools/parity_dump_master.cpp`
- `include/openglad/core/constants.h`
- `include/openglad/gameplay/walker.h`
- `include/openglad/gameplay/event.h`
- `src/gameplay/sim_input_handler.cpp`
- `src/gameplay/living.cpp`
- `CMakeLists.txt`

## New Outputs
- `tests/parity/fact_predicate.h`, `tests/parity/fact_predicate.cpp` — `FactKind` enum, `FactPredicate`/`FactEvalResult` structs, `evaluate_facts(...)` evaluator, and `parse_state_dump(std::string_view)` JSON-to-`StateDump` parser. Predicate kinds: `WalkerFamilyCount`, `WalkerOfTeamAlive`, `EffectFamilyCount`, `EventKindAtLeast`, `EventKindExactly`, `ScoreDelta`, `WalkerHpRangeAtFinalTick`, `WalkerKeysApplied`, `WalkerPositionMoved`, `WalkerDiedByFinal`, `WalkerAliveAtFinal`, `TreasureFamilyRemovedFromOblist`, `StatDeltaOnPickup`, `WeaponFamilyEmitted`, `TickReached`, `LevelDoneEquals`.
- `WeaponFamilyEmitted` is pinned to `dump.weapons[].family` only — never matches `dump.effects[]`. `FAMILY_DOOR` (id 18, Order::Weapon) matches; `FAMILY_DOOR_OPEN` (id 11, effect) does not.
- `EffectFamilyCount` predicates must be qualified by source-walker family via `arg2` OR a `[min_tick,max_tick]` window via `arg2`/`arg3`. Unqualified predicates are rejected by lint.
- `scripts/parity/lint_scenario_facts.py` — parses `tests/parity/scenario_table.h`; asserts (a) `expected_facts != nullptr || fact_count == 0` matched pair, (b) every `ByteEqual`/`SemanticParity` spec has `fact_count > 0` and at least one non-`tick` predicate, (c) every spec has a non-default `discriminating_mutation` (`file`, `line>0`, `from`, `to`, `rationale` all non-empty), (d) for every `special_<family>_<idx>_scen*` with `idx >= 2`, the spec has a team-0 caster `SpawnSpec` with `stats_level >= (idx-1)*3 + 1` AND `magicpoints >= 600`. Supports `LINT_SCENARIO_TABLE=<path>` env override.
- `scripts/parity/evaluate_facts.py` — Python mirror of `evaluate_facts`; reads `tests/parity/scenario_facts_generated.json`.
- `tests/parity/scenario_facts_dump_main.cpp` — CMake-time tool that emits `tests/parity/scenario_facts_generated.json` (one source of truth).
- Two new trailing fields on `SpawnSpec`: `std::int32_t stats_level = 0;` and `std::int32_t magicpoints = 0;` (defaults preserve byte-mirror layout).
- New `CompareMode::SemanticParity` enumerator appended to `enum class CompareMode : std::uint8_t { ByteEqual, Invariant, SemanticParity };`.
- `ScenarioSpec` gains: `const FactPredicate* expected_facts = nullptr;`, `std::size_t fact_count = 0;`, and `struct Mutation { std::string_view file; int line; std::string_view from; std::string_view to; std::string_view rationale; } discriminating_mutation = {};`.
- `struct StateDump` gains `std::optional<std::vector<std::uint8_t>> inventory_keys;` (default `std::nullopt`). Producer not yet wired; parser tolerates missing key (Phase 04 wires both).
- All 39 existing rows backfilled with `expected_facts[]` and `discriminating_mutation`. Pre-existing `ByteEqual` rows whose master golden diverges from a fresh branch dump get `compare_mode` flipped to `SemanticParity` (detected by running `parity_runner_smoke --scenario <id>` and `cmp -s` against `tests/parity/golden/<id>.json`).
- Four pre-existing special rows get their `exercises` field backfilled: `special_mage_scen126` → `Exercises::Special_Mage_1` (bit 11); `special_cleric_scen124` → `Exercises::Special_Cleric_1` (bit 17); `special_thief_scen789` → `Exercises::Special_Thief_1` (bit 25); `special_archmage_scen123` → `Exercises::Special_Archmage_1` (bit 38).
- `tests/parity/test_parity_scenarios.cpp::run_one_scenario` switches on `spec.compare_mode`: `ByteEqual` keeps existing strict logic; `SemanticParity` loads `tests/parity/golden/<name>.json`, parses, evaluates predicates on both master golden and branch dump, asserts every predicate passes on both sides; `Invariant` keeps existing in-process body.
- Master mirror at `../openglad-master/tools/parity_scenario_table.h` gains the same two `SpawnSpec` fields and the same `CompareMode` value plus `expected_facts`/`discriminating_mutation` field declarations on `ScenarioSpec` (byte-mirrored layout; master never applies them).
- New gtest cases in `og_test_parity`:
  - `Parity.exercises_bitcount_matches_required_specials` — asserts `std::size(kRequiredSpecials) == 42` and the highest `Special_<Family>_<N>` enumerator equals `1ULL << (std::size(kRequiredSpecials) - 1)`.
  - `Parity.parse_state_dump_tolerates_legacy_v1_shape` — reads `tests/parity/golden/family_soldier_scen99.json`, asserts the parser succeeds, `parsed.inventory_keys.has_value() == false`, and at least one backfilled predicate evaluates to a defined `bool`.
  - `Parity.weapon_family_emitted_matches_dump_weapons_only` — synthetic dump fixture asserting `WeaponFamilyEmitted(FAMILY_DOOR)` matches `dump.weapons[]` family=18 and rejects `dump.effects[]` family=11.
- `.plan/parity-harness-design.md` — appended section `## Phase 01 redo: semantic parity contract`.
- `.plan/parity-coverage-manifest.md` — appended section `## Parity contract` with byte-equal vs semantic-parity rules and pointer to `fact_predicate.h`.

## File Changes
- New: `tests/parity/fact_predicate.h`, `tests/parity/fact_predicate.cpp`
- New: `scripts/parity/lint_scenario_facts.py`
- New: `scripts/parity/evaluate_facts.py`
- New: `tests/parity/scenario_facts_dump_main.cpp`
- Modified: `tests/parity/scenario_table.h` (CompareMode value, `ScenarioSpec` struct fields, trailing `stats_level`/`magicpoints` on `SpawnSpec`, fill all 39 rows, flip diverging `ByteEqual` rows to `SemanticParity`, backfill 4 special rows' `exercises`)
- Modified: `tests/parity/scenario_runtime.cpp` (`apply_post_load_spawns` applies the two new `SpawnSpec` fields via `w->stats()->set_level(...)` and `w->stats()->set_magicpoints(...)` when non-zero)
- Modified: `tests/parity/state_dump.h` (add `std::optional<std::vector<std::uint8_t>> inventory_keys` to `StateDump`; producer/parser unwired)
- Modified: `tests/parity/test_parity_scenarios.cpp` (switch dispatch on `compare_mode`; new gtest cases)
- Modified: `../openglad-master/tools/parity_scenario_table.h` (mirror struct layout)
- Modified: `CMakeLists.txt` (link `fact_predicate.cpp` into `og_test_parity`; register `scenario_facts_dump` pre-build tool to emit `scenario_facts_generated.json`)
- Modified: `.plan/parity-harness-design.md`
- Modified: `.plan/parity-coverage-manifest.md`

## Implementation Details
Order of operations (commit at each marked step):
1. Build `parity_runner_smoke` against the untouched table.
2. Run the divergence detector: loop every `ByteEqual` spec, capture branch dump via `parity_runner_smoke --scenario <id> --out /tmp/parity-branch-<id>.json`, `cmp -s` against `tests/parity/golden/<id>.json`; write `(id, cmp_exit_status)` to `/tmp/parity-divergence-report.txt`. Detector must NOT mutate `scenario_table.h`.
3. Rewrite `compare_mode` on every row whose detector exit was non-zero from `ByteEqual` to `SemanticParity`. Quote the report verbatim in the commit message. **Commit 1** (branch).
4. Backfill `expected_facts[]` and `discriminating_mutation` for every row. Backfill the four `Exercises` bits on pre-existing special rows. Append framework additions (`fact_predicate.{h,cpp}`, `CompareMode::SemanticParity`, `SpawnSpec` fields, `inventory_keys` optional, schema-tolerance test). Run lint, re-run parity suite. **Commit 2** (branch).
5. Mirror master `parity_scenario_table.h` layout. **Commit 3** (master worktree).

Backfill rules per row type:
- `family_<name>_scen99`: `TickReached(spec.tick_budget)` + `WalkerFamilyCount(FAMILY_<NAME>, 1, 99)` + `WalkerOfTeamAlive(/*team=*/0, 1, 99)` where team identity matters. Mutation neuters `init` in `src/gameplay/families/family_<name>.cpp`, flipping `WalkerAliveAtFinal(FAMILY_<NAME>, 0)`.
- `special_<family>_scen<n>`: `EventKindAtLeast` for documented output; for summons, `WalkerFamilyCount(<summoned>, 1, 99)`. Mutation neuters `do_special`.
- Combat/attack: `WalkerDiedByFinal` on weaker team; mutation sets damage to 0 in `src/gameplay/walker_combat.cpp:302`.
- Save-roundtrip: `LevelDoneEquals(0)` + `WalkerFamilyCount(<expected>, ...)`; mutation corrupts save header.

The mutation list is declarative; Phase 02 applies it via the canary.

Caster level/MP precondition for slot ≥ 2: cycling gate at `sim_input_handler.cpp:218` requires `(N-1)*3 + 1 <= stats.level()`; firing gate at `living.cpp:532-533` requires `magicpoints >= special_cost(current_special)`. Phase 06 sets `stats_level = 30, magicpoints = 1000`; lint enforces `magicpoints >= 600` (500 MP non-sentinel cap + 100 MP headroom).

## Verification Phases

### `01a-check-framework-builds`
- Type: `check`
- Bounce target: `01-semantic-parity-contract`
- Purpose: assert both branch and master parity binaries link with the framework additions.
- Commands:
  - `cmake --preset ci-test`
  - `cmake --build --preset ci-test --target og_test_parity parity_runner_smoke`
  - `git -C ../openglad-master log -1 --name-status` — must list `tools/parity_scenario_table.h`.
  - `(cd ../openglad-master && cmake --preset ci-test && cmake --build --preset ci-test --target parity_dump_master)`
  - All commands must exit 0.

### `01b-check-existing-scenarios-have-facts`
- Type: `check`
- Bounce target: `01-semantic-parity-contract`
- Purpose: assert lint catches missing/empty fact sets, all existing scenarios evaluate without crash, byte-equal rows still byte-match, semantic-parity rows pass facts on master, new gtest cases pass.
- Commands:
  - `python3 scripts/parity/lint_scenario_facts.py` — exit 0.
  - Tampered-table lint: in a Python helper, regex-delete one spec's `expected_facts` initialiser to a temp file via `mktemp --suffix=.h`, run lint with `LINT_SCENARIO_TABLE=<temp>`, assert non-zero exit, `os.unlink`. Original `tests/parity/scenario_table.h` is never touched.
  - `./build/ci-test/og_test_parity --gtest_filter='Parity.*'` — does not crash; new gtest cases pass (`exercises_bitcount_matches_required_specials`, `parse_state_dump_tolerates_legacy_v1_shape`, `weapon_family_emitted_matches_dump_weapons_only`).
  - For every spec with `compare_mode == ByteEqual`: invoke `./build/ci-test/parity_runner_smoke --scenario <id> --out /tmp/parity-recheck-<id>.json`, `cmp -s` against `tests/parity/golden/<id>.json`; any non-zero diff fails the phase.
  - For every spec with `compare_mode == SemanticParity`: parse `tests/parity/golden/<id>.json` via `parse_state_dump`, evaluate `expected_facts` on the master dump; every fact must evaluate `true`.
  - Re-run `python3 scripts/parity/lint_scenario_facts.py` to enforce `magicpoints >= 600` and `stats_level >= (idx-1)*3 + 1` thresholds (short-circuits early if no slot-≥2 row exists yet).
  - `git log -1 --name-status` lists framework files; `git -C ../openglad-master log -1 --name-status` lists the mirror commit.

## Success Criteria
- `cmake --build --preset ci-test --target og_test_parity` exits 0.
- `python3 scripts/parity/lint_scenario_facts.py` exits 0.
- `./build/ci-test/og_test_parity --gtest_filter='Parity.*'` does not crash; new gtest cases pass.
- Every surviving `ByteEqual` row's branch dump matches its golden via `cmp -s`.
- Every `SemanticParity` row's `expected_facts` evaluate `true` on the master golden.
- The 4 pre-existing special rows set bits 11, 17, 25, 38 in the `Exercises` union; bits 0..10, 12..16, 18..24, 26..37, 39..41 remain uncovered (closed in Phase 06).
- Master mirror commit lists `tools/parity_scenario_table.h`.

## Git Commit Requirement
The implementer MUST commit work to git on both trees BEFORE yielding:
- Branch: `git add tests/parity/ scripts/parity/ CMakeLists.txt .plan/ && git commit -m "parity-semantic: phase 01 — predicate framework, ByteEqual→SemanticParity flips, backfilled facts"` (two commits — one for the divergence flip, one for the framework + backfill).
- Master worktree: `git -C ../openglad-master add tools/parity_scenario_table.h && git -C ../openglad-master commit -m "parity-companion: phase 01 — mirror SpawnSpec layout (stats_level, magicpoints)"`.
- After both commits, `git log -1 --name-status` and `git -C ../openglad-master log -1 --name-status` MUST list the expected files. Yielding without these commits is a phase failure.
