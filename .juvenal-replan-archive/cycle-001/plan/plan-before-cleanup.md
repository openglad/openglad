# Plan: Drive the gameplay-parity harness to total coverage and verifiable semantic equivalence against master

## 1. Context

### Problem statement

The OpenGlad branch `wip/networking` carries a gameplay-parity comparison framework that compares deterministic state dumps of the branch against a master worktree at `../openglad-master` (branch `parity-companion`, current HEAD `de702ef0...`). The framework is built around:

- **`tests/parity/scenario_table.h`** — 132 scenarios (`kScenarios`), each naming a tick budget, RNG seed, scripted input, optional fresh-arena spawn list, comparison mode (`ByteEqual` / `Invariant` / `SemanticParity`), `expected_facts[]` predicate array, and a `discriminating_mutation` used by the mutation canary.
- **`tests/parity/fact_predicate.{h,cpp}`** — 14–16 `FactKind` values that express semantic-equivalence predicates evaluated against the canonical JSON dump.
- **`tests/parity/state_dump.{h,cpp}`** — canonical, lexicographically-sorted JSON dump of the simulation state (`walkers[]`, `effects[]`, `weapons[]`, `events[]`, plus rng/score/tick).
- **`tests/parity/parity_runner.{h,cpp}`** — driver that loads a scenario, seeds RNG post-load, drives the tick budget, and produces a RunOutcome.
- **`tests/parity/test_parity_scenarios.cpp`** — gtest entry which, for each `SemanticParity` row, loads `tests/parity/golden/<id>.json` (captured from master) and evaluates the row's predicates against both the master golden and the live branch dump; both sides must satisfy every predicate.
- **`tests/parity/test_parity_coverage_gate.cpp`** — gtest gates that fail if any required walker family / weapon family / treasure family / generator family / effect family / event kind / `(family,special_slot)` pair from `tests/parity/coverage_targets.h` is missing from the cumulative `CoverageObservation` across `kScenarios`.
- **`/home/yans/code/openglad-master/tools/parity_dump_master`** — companion binary built from a mirrored `tools/parity_scenario_table.h` and `tools/parity_dump_state.{cpp,h}`. Used by `scripts/parity/capture_master_golden.sh` to emit each golden.
- **`scripts/parity/run_mutation_canary.sh` + `run_mutation_canary_runtime.py`** — applies each row's `discriminating_mutation`, rebuilds, and verifies that gtest verdict or at least one predicate flips, proving the predicate set actually discriminates a real divergence.
- **`scripts/parity/lint_scenario_facts.py`** — static linter detecting fraudulent shortcuts (`unjustified_widening`, `effect_count_unqualified`, `vacuous_event_floor`, `dead_predicate`).

### Present-day state (live, captured 2026-05-17)

```
$ cmake --build --preset ci-test --target og_test_parity
$ build/ci-test/og_test_parity --gtest_brief=1
[==========] 150 tests from 1 test suite ran. (593 ms total)
[  PASSED  ] 56 tests.
[  SKIPPED ] 81 tests.
[  FAILED  ] 13 tests:
    Parity.behavioural_coverage_gate
    Parity.behavioural_coverage_gate_treasures
    Parity.treasure_drumstick_pickup_scen99 (and 10 more treasure_*_pickup rows)
$ git status --porcelain                  # only .plan/.juvenal-state.json + untracked __pycache__
$ git -C ../openglad-master rev-parse HEAD # de702ef0679e7d06b434da0b0725688160739f5d
$ sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
                                          # (verifier must re-derive; treat as MAY-DIFFER until phase 02)
```

The 13 failures fall into two root causes:

1. **Treasure-family/walker-family symbol aliasing.** `tests/parity/state_dump.cpp::collect_walkers` resolves `walker.family` to a string via a single `Order::Living` symbol table. Treasure family ids 0/1/2/3/4/8 (STAIN, DRUMSTICK, GOLD_BAR, SILVER_BAR, MAGIC_POTION, EXIT) collide with Living family ids (SOLDIER=0, ELF=1, ARCHER=2, MAGE=3, SKELETON=4, SLIME=8) per `include/openglad/core/constants.h:46-54`. The 11 treasure pickup rows spawn a player soldier walker + the target treasure; `TreasureFamilyRemovedFromOblist(arg0)` resolves `arg0` against the Living symbol table and trips on the still-present soldier.
2. **Behavioural-gate scenarios** require that every treasure-order family id is bound by at least one `TreasureFamilyRemovedFromOblist` predicate, but families 0/8 cannot be bound that way without aliasing. The binding requirement is enforced in two places that BOTH must accept the new FactKind in phase 03: the dedicated `TEST(Parity, behavioural_coverage_gate_treasures)` at `tests/parity/test_parity_coverage_gate.cpp:323-334` (single `missing_family_bindings(..., FactKind::TreasureFamilyRemovedFromOblist)` call at line 327-330), AND the umbrella `TEST(Parity, behavioural_coverage_gate)` at `tests/parity/test_parity_coverage_gate.cpp:399`, whose internal `append_family` lambda calls `any_predicate_binds(FactKind::TreasureFamilyRemovedFromOblist, ...)` at lines 421-424.

The 81 skips arise because `tests/parity/test_parity_scenarios.cpp:108` emits `GTEST_SKIP() << "master golden missing for <id> ..."` whenever `tests/parity/golden/<id>.json` is absent. Only 39 of 132 master-comparable rows have goldens on disk.

### Why this matters

The user's goal demands cumulative coverage of **every** entity type, special-ability slot, attack/weapon type, and event-kind occurrence in the game, with each row carrying enough RNG-insensitive predicates that semantic equivalence between the branch and master is verifiable beyond reasonable doubt. The prior agent failed because (a) it inflated the scenario table to enumerate coverage without fixing the underlying dumper bug, (b) most goldens were never captured so the suite skipped silently, and (c) the user-visible signal was a *growing* skip count that hid the lack of real verification. This plan fixes the dumper, captures all goldens, fills coverage gaps that the existing 132 rows still leave open, and locks the gate so that future agents cannot regress coverage or paper over divergences with widened predicates.

### Coverage targets (literal source: `tests/parity/coverage_targets.h`)

- **21 walker families** (Living order).
- **20 weapon families** (Weapon order).
- **13 treasure families** (Treasure order).
- **4 generator families** (Generator order).
- **13 effect families** (Effect order).
- **42 `(family, special_slot)` pairs** spanning every Living family with at least one special.
- **9 event kinds** (`kRequiredEventKinds`).

Cumulative parity coverage requires every one of these to be `observed=true` in at least one `kScenarios` row, with `SemanticParity` predicates that pin behaviour beyond mere presence.

### Existing artefacts consumed in place — never re-derived

| Artefact | Role |
|---|---|
| `.plan/goal.md` | Read-only. Verbatim goal. |
| `.plan/parity-honest-audit.md`, `.plan/parity-present-state.md` | Audit history. Append-only updates. |
| `.plan/parity-coverage-manifest.md` | Coverage manifest with `master_companion_sha` and one row per required target. Each phase updates `covering_scenario_id` cells in place. |
| `.plan/parity-harness-design.md` | Schema-v1 contract. Amended only when a FactKind is added. |
| `.plan/master-companion.md` | Master worktree pin. Updated when `master_companion_sha` advances. |
| `.plan/parity-canary-exemptions.md` | Mutation-canary exempt list. Edited only when justified. |
| `tests/parity/scenario_table.h` | Source of truth for branch-side scenarios. Mirrored byte-for-byte to `../openglad-master/tools/parity_scenario_table.h` on every change. |
| `tests/parity/fact_predicate.{h,cpp}` | Predicate kinds. Additive-only: existing kinds keep their argument layouts and semantics. |
| `tests/parity/state_dump.{h,cpp}` | Canonical JSON dump. Top-level keys, field order, and value encoding are frozen; only producer behaviour for the `walkers[].family` string changes (per-Order symbol resolution). |
| `tests/parity/test_parity_scenarios.cpp` | gtest entry. New rows registered with `OG_PARITY_TEST(id)`; existing rows are never removed. |
| `tests/parity/test_parity_coverage_gate.cpp` | Coverage gates. Hard-fails until cumulative observation covers every required target. |
| `tests/parity/golden/<id>.json` | One file per `SemanticParity` row. Captured from master companion; replaced wholesale when producer behaviour changes. |
| `scripts/parity/capture_master_golden.sh` | Captures one or all goldens by invoking the master companion. Extended with `--all` / `--out-dir`. |
| `scripts/parity/run_mutation_canary.sh` + `run_mutation_canary_runtime.py` | Mutation canary. Unchanged. |
| `scripts/parity/lint_scenario_facts.py` | Static linter. Existing rules untouched; new rule `requires_rng_insensitive_predicate` added in phase 08. |
| `../openglad-master/tools/parity_scenario_table.h` | Byte-equal mirror of branch scenario table. |
| `../openglad-master/tools/parity_dump_state.{cpp,h}` | Mirror of branch dumper. |
| `../openglad-master/build/ci-test/parity_dump_master` | Companion binary. Rebuilt on every mirror change. |

### Broken-state authorisation

The global rule "all tests pass at all times" is overridden by the user's verbatim goal only inside this multi-phase window. The override is bounded:

- After phase 03, the treasure cohort and the two behavioural gates must be green. `og_test_parity` may still report `[SKIPPED]` rows but `[FAILED] 0`.
- After phase 04, every `SemanticParity` row in the current scenario table has a golden on disk; `[SKIPPED] 0` for those rows. New rows added in phases 05–09 are allowed to be `[SKIPPED]` until their own phase's verifier requires them green.
- After phase 09, `og_test_parity` reports `[PASSED] = total tests`, `[SKIPPED] 0`, `[FAILED] 0`.
- After phase 11, the entire repository test suite (`ctest --preset ci-test`) is green.

Each phase's verifier asserts the relevant bound; broken-state windows that escape these bounds are bounces.

## 2. Generated Workflow Contract

The generated `workflow.yaml` MUST obey every rule below. Generator phases that emit `workflow.yaml` are responsible for enforcing these as a literal contract:

1. **Linear execution only.** `linear: true`. No `parallel_groups`. Phases run strictly in numeric order from `01-...` to `11-...`, with each implement phase immediately followed by its check phases.
2. **Inline-only YAML.** `yaml_source_mode: inline-only`. No top-level `include:`. No phase-level `prompt_file:`, `workflow_file:`, `workflow_dir:`, `checks:`, or any other YAML-source indirection. Every `prompt:` is a complete multiline string.
3. **Fixed `bounce_target` only.** Each check phase declares exactly one `bounce_target` equal to the id of the implement phase it verifies. No `bounce_targets:` list. No agent-guided routing.
4. **Every verifier is an explicit top-level `check` phase.** Pattern:
   ```
   N    implement (id: NN-name)         bounce_target: null
   N+1  check     (id: NNa-...)         bounce_target: NN-name
   N+2  check     (id: NNb-...)         bounce_target: NN-name
   N+3  check     (id: NNc-...)         bounce_target: NN-name
   ```
5. **Verifier stays in its block.** A `check` phase bounces only to the immediately preceding implement phase.
6. **Checks execute shell commands literally.** Every command — `cmake --build`, `ctest`, `sha1sum`, `cmp`, `diff`, `grep`, `python3 scripts/parity/...`, `scripts/parity/*.sh`, `git -C ../openglad-master ...` — is written into the check phase `prompt:` verbatim with the expected exit code spelled out. No agent-discovered commands.
7. **Existing artefacts are reused; nothing is regenerated from scratch.** Each implement phase enumerates its `Preexisting Inputs` and is instructed to `read or update in place`. Specifically: `.plan/*.md` is amended; coverage manifest rows are updated by `covering_scenario_id` cell edits; golden JSONs are replaced one-for-one with `cp` from a recapture staging directory.
8. **Commit-before-yield.** Every implement phase's prompt ends with a literal instruction to `git add <enumerated files> && git commit -m "<prefix>: phase NN — <subject>"` before yielding. Two-worktree implement phases also commit on `../openglad-master` with prefix `parity-companion: phase NN — ...`. Check phases always start by asserting `git log -1 --name-status` lists the expected files on both worktrees.
9. **Fraud-resistant check semantics.** Checks assert content, not just existence:
   - Test-pass assertions use `grep -F "[  PASSED  ] N tests."` with an explicit numeric lower bound, and assert `grep -cE "^\[  (SKIPPED|FAILED) \]"` equals the value declared by the phase.
   - Coverage assertions re-derive numerator/denominator by parsing the coverage manifest and `kScenarios` (via `python3 scripts/parity/check_coverage_manifest.py`) and `grep -F` the literal totals in the audit doc.
   - Mutation-canary checks parse `/tmp/canary_runtime_<id>.json` and assert every non-exempt scenario has `flipped >= 1`. Exempt list is read with the runtime's own `load_exemptions()` at `scripts/parity/run_mutation_canary_runtime.py:74-91`.
   - Anti-cheat checks create throwaway worktrees under `/tmp/parity-bypass-<phase>-<step>`, apply a real `sed -i` mutation, run the guard, assert non-zero exit, then `git worktree remove --force`.
10. **No new YAML outside `workflow.yaml`.** Auxiliary data is `.md`, `.py`, `.sh`, `.cpp`, `.h`, `.json` only.
11. **No `bounce_targets` list, no agent-discovered checks, no skipped checks.** A phase advances only when all of its check phases pass.

## 3. Implementation Phases

11 implement phases + 33 check phases = 44 phases total. Verifier counts per implement phase: `3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3`.

| # | Implement phase | What it lands |
|---|---|---|
| 01 | `01-baseline-and-inventory` | Pin master companion SHA; capture present-day PASS/SKIP/FAIL; build a per-target gap inventory across walker/weapon/treasure/generator/effect/special/event categories; update `.plan/parity-coverage-manifest.md`. |
| 02 | `02-mirror-resync` | Make `../openglad-master/tools/parity_scenario_table.h` byte-equal with branch; rebuild `parity_dump_master`; SHA-verify both. |
| 03 | `03-fix-treasure-aliasing` | Add per-Order family symbol resolution in `state_dump.cpp::collect_walkers`. Add 17th `FactKind::TreasureFamilyOfOrderRemovedFromOblist`. Migrate every treasure pickup row to it. Treasure cohort + behavioural gates green. Mirror change. Replace all touched goldens. |
| 04 | `04-mass-golden-recapture` | Extend `capture_master_golden.sh --all`. Recapture every existing golden under the resynced companion (catches incidental drift). Capture every missing master golden so `[SKIPPED] 0` for the current 132 rows. |
| 05 | `05-walker-family-completeness` | Every walker family has a `SemanticParity` row that pins non-trivial behaviour with at least one RNG-insensitive predicate. Backfill missing rows. Update coverage manifest. |
| 06 | `06-weapon-and-effect-completeness` | Every weapon family observed in `weaplist`/`weapons[]` and every effect family in `effects[]` via at least one row with `WeaponFamilyEmitted` / `EffectFamilyCount` predicates. Backfill missing emissions. |
| 07 | `07-treasure-and-generator-completeness` | Every treasure family bound by a removal/pickup-style predicate (new TreasureFamilyOfOrderRemovedFromOblist for non-Living-aliased ids); every generator family has a spawn-from-generator row with `WalkerFamilyCount` floor; behavioural gates extended. |
| 08 | `08-specials-completeness-and-rng-insensitivity` | For each of the 42 `(family, special_slot)` pairs: scenario with `Exercises::Special_<...>` bit set, family spawned, slot bit pressed, at least one RNG-insensitive predicate verifying the slot fired. Add lint rule `requires_rng_insensitive_predicate`. |
| 09 | `09-event-kind-completeness` | Every required event kind organically emitted by at least one row with `EventKindAtLeast(kind, n>=1)` matched on both branch and master. |
| 10 | `10-mutation-canary-green` | Drive `run_mutation_canary.sh --all` to zero non-exempt zero-flip rows. For every row added in phases 05–09, define a `discriminating_mutation`. Update `.plan/parity-canary-exemptions.md` with literal justifications. |
| 11 | `11-anti-cheat-and-final-signoff` | Land `scripts/parity/ci_parity.sh` + `scripts/parity/anti_cheat_selftest.sh` covering 8 known bypasses; assert each guard exits non-zero. `.plan/parity-signoff-honest.md`. Full repo test suite green. |

---

### Phase 01 — Baseline and per-target gap inventory

**Phase Name**: Baseline test counts and per-target coverage gap inventory.

**Implement Phase ID**: `01-baseline-and-inventory`

**Preexisting Inputs**:
- `.plan/goal.md`
- `.plan/parity-coverage-manifest.md`
- `.plan/parity-honest-audit.md`
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json` (39 files)
- `../openglad-master/` worktree at HEAD `de702ef0...`

**New Outputs**:
- `.plan/parity-present-state.md` (≤200 lines) containing:
  - **Test count snapshot**: lines `Passed: P`, `Skipped: S`, `Failing: F` derived from `og_test_parity --gtest_brief=1`. (Note: `--gtest_brief=1` does not emit a `[  FAILED  ] N tests.` summary line; failure count is `grep -cE "^\[  FAILED  \] Parity\." /tmp/p01.out`.)
  - **Failing tests**: one bullet per `[  FAILED  ] Parity.<id>` line.
  - **Skipped tests**: one bullet per `master golden missing for <id>` line.
  - **Master companion SHA pinned this phase**: one line `Master companion SHA: <40-hex>` equal to `git -C ../openglad-master rev-parse HEAD`.
  - **Mirror SHA delta**: literal `sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` output. If unequal, the doc explicitly states "BRANCH ≠ COMPANION — phase 02 resyncs."
  - **Per-target coverage gap inventory** (the new core artefact). Seven tables, one per category, each with columns `target | observed_in_any_row | covering_scenario_id | golden_present`:
    - 21 walker families
    - 20 weapon families
    - 13 treasure families
    - 4 generator families
    - 13 effect families
    - 42 (family, special_slot) pairs, each emitted as a literal token `FAMILY_<name>:slot<N>`. `kRequiredSpecials` is declared as `std::pair<std::int32_t, std::uint8_t>` in `tests/parity/coverage_targets.h`, so its members are accessed as `kRequiredSpecials[i].first` (the int32 family id) and `kRequiredSpecials[i].second` (the uint8 slot index). The implementer reverse-maps `.first` to the uppercase Living-family symbol via the `family_symbol_by_order(Order::Living, ...)` table from Phase 03 (or the equivalent pre-Phase-03 mapping for this phase, since Phase 03 hasn't run yet — for Phase 01, use the bare `family_symbol` table that already exists in `state_dump.cpp`), and renders `.second` as its decimal value to produce the token `FAMILY_<NAME>:slot<N>`. The token appears verbatim in the first column of the specials gap table so that 01c's `grep -F` step is unambiguous.
    - 9 event kinds
  - **Broken-state authorisation**: quote the bounds from §1 verbatim.
- `.plan/parity-coverage-manifest.md` updated in place so its `master_companion_sha` frontmatter equals the live companion SHA and its `(none yet)` cells reflect current `kScenarios` observations.

**File Changes**:
- Write `.plan/parity-present-state.md`.
- Edit `.plan/parity-coverage-manifest.md` `master_companion_sha:` and `covering_scenario_id` cells.
- Edit `scripts/parity/check_coverage_manifest.py` to add `argparse` plus the `--emit-gap-table` and `--emit-scenario-list` subcommands (both consumed below in this phase / in Phase 04 verifier 04b).
- `git add .plan/parity-present-state.md .plan/parity-coverage-manifest.md scripts/parity/check_coverage_manifest.py && git commit -m "parity-cov: phase 01 — baseline and per-target gap inventory"`.

**Implementation Details**:
1. Run `cmake --build --preset ci-test --target og_test_parity && build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p01.out`.
2. Extract counts: `PASSED=$(grep -oE '^\[  PASSED  \] [0-9]+' /tmp/p01.out | head -1 | awk '{print $3}')`, `SKIPPED=$(grep -oE '^\[  SKIPPED \] [0-9]+' /tmp/p01.out | head -1 | awk '{print $3}')`, `FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p01.out)`.
3. Extend `scripts/parity/check_coverage_manifest.py` with an `argparse` front-end (the script today has none — running it with any argument is silently ignored) that adds TWO subcommands in this phase:
   - `--emit-gap-table`: parses `tests/parity/coverage_targets.h` and `tests/parity/scenario_table.h` and writes the seven gap tables (one per category) to stdout in pipe-table form; consumed below as `/tmp/gap.md`.
   - `--emit-scenario-list`: parses `tests/parity/scenario_table.h` and writes one line per `kScenarios` row formatted as exactly three tab-separated fields `<id>\t<compare_mode>\t<is_branch_internal>`, where:
     - `<id>` is the C++ identifier-style scenario id literal (e.g. `smoke_empty_scen99`),
     - `<compare_mode>` is one of the three literal strings `ByteEqual`, `Invariant`, `SemanticParity` (matching the enum identifier verbatim, without `CompareMode::` prefix),
     - `<is_branch_internal>` is the lowercase string `true` or `false`.
     Lines are emitted in the order rows appear in `kScenarios`; a single trailing newline terminates the last line. Literal example output for the first two present-day rows:
     ```
     smoke_empty_scen99	SemanticParity	false
     rng_seed_stable_scen99	Invariant	false
     ```
     Consumed by Phase 04 verifier 04b, which splits on a single ASCII tab (`\t`, 0x09) and asserts exactly three fields per non-empty line.
   Both subcommands are pure stdout, exit 0 on success and ≠0 on parse failure. Default behaviour (no flags) remains identical to the present-day script.
4. Run `python3 scripts/parity/check_coverage_manifest.py --emit-gap-table > /tmp/gap.md` and embed the seven tables verbatim in `.plan/parity-present-state.md`.
5. For each target row, `golden_present` is `yes` iff `tests/parity/golden/<covering_scenario_id>.json` exists.

**Verification Phases**:

- **`01a-check-tree-clean-and-counts`** (`check`, `bounce_target: 01-baseline-and-inventory`):
  - `git status --porcelain | grep -v '^?? .plan/.juvenal-state.json$' | grep -v '^?? scripts/parity/__pycache__' | wc -l` must equal `0`.
  - `test -f .plan/parity-present-state.md`.
  - Re-runs the build and `og_test_parity --gtest_brief=1`, re-derives `PASSED/SKIPPED/FAILED`, and `grep -F`s each integer in `.plan/parity-present-state.md` under the "Test count snapshot" heading.
  - `git log -1 --name-status` lists `.plan/parity-present-state.md` and `.plan/parity-coverage-manifest.md`.

- **`01b-check-companion-sha-pinned`** (`check`, `bounce_target: 01-baseline-and-inventory`):
  - `SHA=$(git -C ../openglad-master rev-parse HEAD)`. `grep -F "Master companion SHA: $SHA" .plan/parity-present-state.md` succeeds.
  - `grep -F "master_companion_sha: $SHA" .plan/parity-coverage-manifest.md` succeeds.

- **`01c-check-gap-inventory-shape`** (`check`, `bounce_target: 01-baseline-and-inventory`):
  - Verifier loops over each header constant via `grep -oE '"FAMILY_[A-Z0-9_]+"' tests/parity/coverage_targets.h` (and analogous for event kinds and specials) and `grep -F` each one in `.plan/parity-present-state.md`.
  - For the 9 event kinds in `kRequiredEventKinds`, `grep -F` each.
  - For the 42 specials in `kRequiredSpecials` (a `std::pair<std::int32_t, std::uint8_t>` array, so members are `.first` and `.second`, not named fields), the verifier emits the canonical token `FAMILY_<name>:slot<N>` — where `<name>` is the reverse-mapped Living-order symbol for `.first` and `<N>` is the decimal `.second` — from each `kRequiredSpecials[i]` and `grep -F`s the literal token in `.plan/parity-present-state.md`.

---

### Phase 02 — Mirror resync and companion rebuild

**Phase Name**: Make `../openglad-master/tools/parity_scenario_table.h` byte-equal with branch; rebuild master companion.

**Implement Phase ID**: `02-mirror-resync`

**Preexisting Inputs**:
- `tests/parity/scenario_table.h` (branch, current).
- `tests/parity/state_dump.{h,cpp}` (branch).
- `../openglad-master/tools/parity_scenario_table.h` (possibly stale).
- `../openglad-master/tools/parity_dump_state.{cpp,h}`.
- `../openglad-master/build/` directory.

**New Outputs**:
- `../openglad-master/tools/parity_scenario_table.h` byte-equal to branch.
- `../openglad-master/build/ci-test/parity_dump_master` rebuilt.
- `.plan/master-companion.md` updated with the new pin (post-commit SHA of `../openglad-master`).
- `.plan/parity-coverage-manifest.md` `master_companion_sha:` updated to new SHA.

**File Changes**:
- `cp -f tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`.
- `cmake --build ../openglad-master/build/ci-test --target parity_dump_master`.
- Branch commit `parity-cov: phase 02 — resync companion mirror`.
- Companion commit (in `../openglad-master`): `parity-companion: phase 02 — mirror scenario_table.h to <branch-sha>`.

**Implementation Details**:
1. Confirm there is no uncommitted branch state.
2. Copy file, build companion, capture new companion SHA, write it to `.plan/master-companion.md` and `.plan/parity-coverage-manifest.md`.

**Verification Phases**:

- **`02a-check-mirror-sha-equal`** (`check`, `bounce_target: 02-mirror-resync`):
  - `sha1sum tests/parity/scenario_table.h | awk '{print $1}'` must equal `sha1sum ../openglad-master/tools/parity_scenario_table.h | awk '{print $1}'`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

- **`02b-check-companion-binary-fresh`** (`check`, `bounce_target: 02-mirror-resync`):
  - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
  - Verifier runs the companion on a known small scenario id (e.g. `smoke_empty_scen99`) into `/tmp/p02-dump.json`, then runs `python3 scripts/parity/validate_schema.py /tmp/p02-dump.json` and asserts exit 0.

- **`02c-check-sha-pin-doc-consistent`** (`check`, `bounce_target: 02-mirror-resync`):
  - `SHA=$(git -C ../openglad-master rev-parse HEAD)`. `grep -F "$SHA" .plan/master-companion.md` succeeds. `grep -F "master_companion_sha: $SHA" .plan/parity-coverage-manifest.md` succeeds.
  - `git -C ../openglad-master log -1 --name-status` contains `tools/parity_scenario_table.h`.

---

### Phase 03 — Fix treasure / walker family symbol aliasing

**Phase Name**: Producer-side per-Order family resolution; 17th FactKind; treasure cohort and behavioural gates green.

**Implement Phase ID**: `03-fix-treasure-aliasing`

**Preexisting Inputs**:
- `tests/parity/state_dump.{h,cpp}`.
- `tests/parity/fact_predicate.{h,cpp}`.
- `tests/parity/scenario_table.h` (with the 11 treasure pickup rows that currently fail).
- `tests/parity/test_parity_coverage_gate.cpp` (`behavioural_coverage_gate*`).
- `../openglad-master/tools/parity_scenario_table.h`, `parity_dump_state.{cpp,h}` (must be patched identically).

**New Outputs**:
- Updated `state_dump.cpp` family resolution. Today (`tests/parity/state_dump.cpp:98-130`) there is a single `family_symbol(int32_t)` function backed by one Living-order table, and `collect_walkers` (`:168`), `collect_effects` (`:193`), and `collect_weapons` (`:218`) all call it — this is the root defect because Treasure/Generator/Weapon/Effect family ids alias Living family ids. Phase 03 introduces a new free function `family_symbol_by_order(int32_t order, int32_t family_id)` in `state_dump.cpp` backed by 5 small static `std::string_view` tables (one per `Order::Living`, `Order::Weapon`, `Order::Treasure`, `Order::Generator`, `Order::Effect`), and rewrites `collect_walkers`, `collect_weapons`, and `collect_effects` to call `family_symbol_by_order(static_cast<int32_t>(w->query_order()), static_cast<int32_t>(w->family()))` instead of the bare `family_symbol(...)`. The legacy `family_symbol` free function is **deleted outright** (no thin-wrapper option): verifier 03b asserts both `grep -c '^std::string family_symbol(' tests/parity/state_dump.cpp` equals `0` (definition removed) AND the regex check against the three collectors passes. Any pre-existing call to `family_symbol(` from a site other than the three collectors must be migrated to `family_symbol_by_order(...)` in the same commit. The change is mirrored byte-for-byte to `../openglad-master/tools/parity_dump_state.cpp` (where the legacy definition must also be removed, asserted by the analogous grep against the companion file).
- New `FactKind::TreasureFamilyOfOrderRemovedFromOblist` appended at the end of the enum. Factory signature: `TreasureFamilyOfOrderRemovedFromOblist(int32_t family, int32_t order, std::string label)`. Evaluator: walks `dump.walkers[]` and compares each entry's `family` string to the table-resolved symbol for `(family, order)` — the predicate is satisfied iff no remaining walker matches. The existing `TreasureFamilyRemovedFromOblist(family, label)` factory is left intact.
- Updated `tests/parity/scenario_table.h`: every `treasure_*_pickup_scen99` row's `TreasureFamilyRemovedFromOblist(arg0)` is replaced by `TreasureFamilyOfOrderRemovedFromOblist(arg0, kOrderTreasure)`. `kInputsTreasurePickup` and spawn lists unchanged.
- Updated `tests/parity/test_parity_coverage_gate.cpp`: BOTH the dedicated `behavioural_coverage_gate_treasures` gate (cpp:323-334) AND the umbrella `behavioural_coverage_gate`'s treasure-family branch (cpp:421-424 — the `append_family("treasure_family", ..., FactKind::TreasureFamilyRemovedFromOblist)` call inside the lambda) accept either `TreasureFamilyRemovedFromOblist` (legacy) or `TreasureFamilyOfOrderRemovedFromOblist` (new) when computing the bound-family set, so previously-aliased families (STAIN=0, SLIME-aliased EXIT=8) can be bound via the new factory. Implementation introduces a free helper `any_treasure_binding(int32_t family_id)` in `test_parity_coverage_gate.cpp` that returns true iff any predicate of either kind binds `arg0 == family_id` (with `arg1 == kOrderTreasure` for the new kind). Both gate sites call this helper. The `missing_family_bindings(..., FactKind::TreasureFamilyRemovedFromOblist)` call at cpp:327-330 is replaced with an equivalent loop that uses `any_treasure_binding`.
- `tests/parity/scenario_facts_generated.json` regenerated.
- Mirror updates: `../openglad-master/tools/parity_scenario_table.h`, `tools/parity_dump_state.{cpp,h}`.
- New goldens for every treasure row: `tests/parity/golden/treasure_*_pickup_scen99.json` (12 files), captured via `scripts/parity/capture_master_golden.sh <id>` against the resynced + patched companion. (The script today already accepts a single scenario id; `--all` is not introduced until Phase 04.)
- Existing goldens regenerated where the new dumper would produce a different `walkers[].family` string. Phase 03 does not depend on the Phase 04 `--all --no-write --diff` extension; instead it enumerates via an inline shell loop:
  ```
  mkdir -p /tmp/p03-staging
  for g in tests/parity/golden/*.json; do
      id=$(basename "$g" .json)
      ../openglad-master/build/ci-test/parity_dump_master "$id" > "/tmp/p03-staging/$id.json"
      if ! diff -q "$g" "/tmp/p03-staging/$id.json" > /dev/null; then
          cp "/tmp/p03-staging/$id.json" "$g"
      fi
  done
  ```
  This relies only on the companion binary's existing single-scenario invocation contract and does not require any new flags on `capture_master_golden.sh`.
- `.plan/parity-harness-design.md` appended with one short section documenting the new FactKind and the per-Order resolution rule.

**File Changes**:
- `tests/parity/state_dump.cpp` (modify `collect_walkers`).
- `tests/parity/fact_predicate.h` (new enum value, new factory).
- `tests/parity/fact_predicate.cpp` (new evaluator).
- `tests/parity/scenario_table.h` (treasure rows).
- `tests/parity/test_parity_coverage_gate.cpp` (gate walks both kinds).
- `tests/parity/scenario_facts_generated.json` (regenerate).
- `../openglad-master/tools/parity_dump_state.cpp` (mirror change).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `tests/parity/golden/*.json` (selective regeneration).
- `.plan/parity-harness-design.md` (append section).

Commit messages:
- Branch: `parity-cov: phase 03 — per-Order family resolution and TreasureFamilyOfOrderRemovedFromOblist`.
- Companion: `parity-companion: phase 03 — mirror per-Order family resolution`.

**Implementation Details**:
- The dump JSON schema is unchanged. Only the string content of `walkers[].family` changes for non-Living oblist entries.
- The new enum value is appended (not inserted) so existing serialised `FactKind` ordinals never shift.
- `scenario_facts_generated.json` is regenerated by `scripts/parity/scenario_facts_dump_main` (or the existing tooling that produces this file). Implementation phase explicitly runs that tool, not hand-edits the JSON.

**Verification Phases**:

- **`03a-check-build-and-treasure-green`** (`check`, `bounce_target: 03-fix-treasure-aliasing`):
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity --gtest_filter='Parity.treasure_*_pickup_scen99:Parity.behavioural_coverage_gate*' 2>&1 | tee /tmp/p03a.out`.
  - `grep -cE '^\[  FAILED  \]' /tmp/p03a.out` equals `0`.
  - `grep -cE '^\[  SKIPPED \]' /tmp/p03a.out` equals `0`.

- **`03b-check-schema-and-mirror-equal`** (`check`, `bounce_target: 03-fix-treasure-aliasing`):
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.
  - Verifier extracts every JSON top-level key from a small golden (e.g. `tests/parity/golden/smoke_nonempty_scen99.json`) and confirms the set matches the frozen schema-v1 list declared in `.plan/parity-harness-design.md`.
  - `git -C ../openglad-master log -1 --name-status` lists `tools/parity_scenario_table.h`, `tools/parity_dump_state.cpp`, AND `tools/parity_dump_state.h` (header mirrored too).
  - Verifier asserts the companion binary is fresh by re-running `cmake --build ../openglad-master/build/ci-test --target parity_dump_master` (idempotent; exits 0) and then asserting `test ../openglad-master/build/ci-test/parity_dump_master -nt ../openglad-master/tools/parity_dump_state.cpp` AND `test ../openglad-master/build/ci-test/parity_dump_master -nt ../openglad-master/tools/parity_dump_state.h`.
  - Verifier asserts the bare single-table `family_symbol` free function is gone entirely by running `grep -c '^std::string family_symbol(' tests/parity/state_dump.cpp` and asserting the output equals `0`; same check is run against `../openglad-master/tools/parity_dump_state.cpp` and asserts `0`.
  - Verifier additionally asserts no leftover non-`_by_order` call sites remain anywhere in `state_dump.cpp` by running `python3 -c "import re,sys; src=open('tests/parity/state_dump.cpp').read(); bad=re.findall(r'\\bfamily_symbol\\s*\\((?!_by_order)', src); sys.exit(1 if bad else 0)"`; expects exit 0. Same check is run against `../openglad-master/tools/parity_dump_state.cpp`.

- **`03c-check-no-new-non-treasure-regressions`** (`check`, `bounce_target: 03-fix-treasure-aliasing`):
  - `build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p03c.out`.
  - `FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p03c.out)`.
  - The verifier re-derives the prior failing set from `.plan/parity-present-state.md` (every bulleted `Parity.<id>` line under the "Failing tests" heading) and the set of treasure/gate ids closed by this phase: every `[  FAILED  ] Parity.treasure_*_pickup_scen99` line plus `behavioural_coverage_gate` and `behavioural_coverage_gate_treasures`. Assertion: every prior failing id NOT in {`Parity.treasure_*_pickup_scen99`, `Parity.behavioural_coverage_gate`, `Parity.behavioural_coverage_gate_treasures`} must still be in `/tmp/p03c.out`'s `[  FAILED  ]` lines (no regression), and every id IN that closing set must NOT be in `/tmp/p03c.out`'s `[  FAILED  ]` lines (closed by this phase). The verifier scripts this set difference in Python inline; no hardcoded numeric delta is used.
  - `grep -cE '^\[  FAILED  \] Parity\.treasure_' /tmp/p03c.out` equals `0`.
  - `grep -cE '^\[  FAILED  \] Parity\.behavioural_coverage_gate' /tmp/p03c.out` equals `0`.

---

### Phase 04 — Mass master golden recapture

**Phase Name**: Capture every missing master golden; refresh existing ones under the resynced companion.

**Implement Phase ID**: `04-mass-golden-recapture`

**Preexisting Inputs**:
- `scripts/parity/capture_master_golden.sh` (extended in this phase).
- `tests/parity/scenario_table.h` (final treasure-fixed version from phase 03).
- `../openglad-master/build/ci-test/parity_dump_master` (rebuilt in phase 02 with phase 03's mirror patch).
- `tests/parity/golden/*.json` (39+ existing).

**New Outputs**:
- `scripts/parity/capture_master_golden.sh` accepts:
  - `--all` (iterate over every `SemanticParity` row in `kScenarios` whose `is_branch_internal == false`).
  - `--out-dir <path>` (default `tests/parity/golden`).
  - `--no-write --diff` (emit JSON to a tmp dir and `diff -ru tests/parity/golden tmpdir` instead of overwriting; used by verifier).
- Up to 93 new + replacement golden files under `tests/parity/golden/` so every `SemanticParity` master-comparable row in the current scenario table has a fresh golden.
- `.plan/parity-recapture-diff.md` listing every file added/replaced (one line per change, with `+`, `M`, sha1 prefix).
- `tests/parity/scenario_facts_generated.json` regenerated.

**File Changes**:
- `scripts/parity/capture_master_golden.sh` (extend).
- `tests/parity/golden/*.json` (mass replace/add).
- `.plan/parity-recapture-diff.md` (new).
- `tests/parity/scenario_facts_generated.json` (regenerate).
- Branch commit `parity-cov: phase 04 — mass golden recapture (N goldens)`.

**Implementation Details**:
1. Implement `--all` by reading `kScenarios` from `tests/parity/scenario_table.h` via the companion binary's own enumeration (the simplest contract: pass each `id` to `parity_dump_master` and write `<out-dir>/<id>.json` if `compare_mode == SemanticParity && is_branch_internal == false`).
2. Reuse the existing `parity_dump_master` invocation pattern in `capture_master_golden.sh`. Do not rewrite the companion binary.
3. After the bulk capture, run `git status tests/parity/golden/ | wc -l` and write the count into `.plan/parity-recapture-diff.md`.

**Verification Phases**:

- **`04a-check-zero-skipped-and-pass-grows`** (`check`, `bounce_target: 04-mass-golden-recapture`):
  - `cmake --build --preset ci-test --target og_test_parity && build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p04a.out`.
  - `grep -cE '^\[  SKIPPED \] Parity\.' /tmp/p04a.out` equals `0`.
  - Verifier reads `Passed: P`, `Skipped: S`, `Failing: F` from `.plan/parity-present-state.md` and asserts the live `[  PASSED  ] N` equals `P + F + S` (i.e. baseline-passed + baseline-failures-closed-by-Phase-03 + baseline-skips-resolved-by-Phase-04, with no new rows introduced in Phase 04). With today's baseline (P=56, S=81, F=13) the assertion is `PASSED == 150`. A single golden whose row's predicate set silently disagrees with the recapture would short the count and trip this check.

- **`04b-check-every-master-comparable-row-has-golden`** (`check`, `bounce_target: 04-mass-golden-recapture`):
  - Verifier enumerates `kScenarios` (parsing via `python3 scripts/parity/check_coverage_manifest.py --emit-scenario-list`) and asserts that for every row with `compare_mode == SemanticParity && is_branch_internal == false`, `tests/parity/golden/<id>.json` exists, is non-empty, and `python3 scripts/parity/validate_schema.py tests/parity/golden/<id>.json` exits 0.

- **`04c-check-recapture-doc-and-mirror-untouched`** (`check`, `bounce_target: 04-mass-golden-recapture`):
  - `test -f .plan/parity-recapture-diff.md`.
  - `grep -cE '^[+M] [0-9a-f]{8} ' .plan/parity-recapture-diff.md` ≥ 1 and equal to `git diff --name-only HEAD~1 HEAD -- tests/parity/golden/ | wc -l`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0 (mirror unchanged in this phase).

---

### Phase 05 — Walker family completeness

**Phase Name**: Every walker family has at least one `SemanticParity` row that pins non-trivial behaviour with at least one RNG-insensitive predicate.

**Implement Phase ID**: `05-walker-family-completeness`

**Preexisting Inputs**:
- `.plan/parity-present-state.md` gap inventory.
- `.plan/parity-coverage-manifest.md`.
- `tests/parity/coverage_targets.h::kRequiredWalkerFamilies` (21).
- `tests/parity/scenario_table.h` with existing `family_<name>_scen99` rows.
- `../openglad-master/build/ci-test/parity_dump_master`.

**New Outputs**:
- For every walker family in `kRequiredWalkerFamilies` whose row currently lacks an RNG-insensitive predicate (per gap inventory) or is missing entirely, add or modify a `family_<lowercase>_scen99` row in `tests/parity/scenario_table.h` with:
  - `fresh_arena = true`, two-team spawn (player + one of target family), tick budget 600.
  - `expected_facts[]` containing at least: `WalkerFamilyCount(FAMILY_<X>, 1, 1)` (RNG-insensitive), `WalkerOfTeamAlive(team_enemy, 0, 1)`, `WalkerPositionMoved(FAMILY_<X>, dx_min, dx_max)` with bounds wide enough to absorb path RNG, and one combat-side event (`EventKindAtLeast("play_sound", 1)` if the family is melee-only).
  - `discriminating_mutation` chosen against a deterministic field (e.g. swap `FAMILY_<X>` with a sibling family in the spawn list, or alter the tick budget by ±1).
- `.plan/parity-coverage-manifest.md` walker-table `covering_scenario_id` cells updated.
- Mirrored `tools/parity_scenario_table.h`.
- New goldens (`tests/parity/golden/family_*_scen99.json`).

**File Changes**:
- `tests/parity/scenario_table.h` (rows added/updated).
- `tests/parity/test_parity_scenarios.cpp` (`OG_PARITY_TEST(family_<x>_scen99)` if missing).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `tests/parity/golden/family_*_scen99.json` (capture).
- `.plan/parity-coverage-manifest.md`.

Commits: branch `parity-cov: phase 05 — walker family completeness` + companion `parity-companion: phase 05 — mirror walker family rows`.

**Implementation Details**:
- Existing `family_*_scen99` rows (one per family) are kept. This phase audits each row for `at least one RNG-insensitive predicate` (one of: `WalkerFamilyCount`, `EffectFamilyCount`, `WeaponFamilyEmitted`, `EventKindAtLeast/Exactly`, `WalkerDiedByFinal`, `WalkerAliveAtFinal`, `TreasureFamilyOfOrderRemovedFromOblist`, `LevelDoneEquals`, `TickReached`). Rows lacking one get an additional predicate, not a widening of existing ones.
- Walker families with no row at all (none currently per the manifest, but the verifier still asserts presence) get a brand new row, registered with `OG_PARITY_TEST(...)`.

**Verification Phases**:

- **`05a-check-walker-rows-and-rng-insensitive`** (`check`, `bounce_target: 05-walker-family-completeness`):
  - For every name `FAMILY_*` in `tests/parity/coverage_targets.h::kRequiredWalkerFamilies`, assert at least one `kScenarios` row has the family in its `family_spawns[]` AND has at least one predicate of the listed RNG-insensitive set. Verifier implements this by parsing `tests/parity/scenario_facts_generated.json` (regenerated by implementation phase) with an inline `python3 -c '<...>'`.

- **`05b-check-cov-manifest-and-tests-pass`** (`check`, `bounce_target: 05-walker-family-completeness`):
  - `python3 scripts/parity/check_coverage_manifest.py` exits 0 (no `(none yet)` remaining in the walker section).
  - `build/ci-test/og_test_parity --gtest_filter='Parity.family_*'` exits 0 with `[  FAILED  ] 0` and `[  SKIPPED ] 0`.

- **`05c-check-no-regression`** (`check`, `bounce_target: 05-walker-family-completeness`):
  - Full `og_test_parity --gtest_brief=1` exits 0 with `[  FAILED  ] 0`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

---

### Phase 06 — Weapon and effect family completeness

**Phase Name**: Every weapon family is observed in `weapons[]` and every effect family in `effects[]`, via dedicated rows with RNG-insensitive predicates.

**Implement Phase ID**: `06-weapon-and-effect-completeness`

**Preexisting Inputs**:
- `kRequiredWeaponFamilies` (20), `kRequiredEffectFamilies` (13).
- Existing `weapon_*_emission_scen99` and `effect_*_emission_scen99` rows (most are skipped in baseline; goldens just captured in phase 04).
- Gap inventory tagged as "needs emission predicate".

**New Outputs**:
- Rows ensuring each weapon family is emitted at least once. Predicate: `WeaponFamilyEmitted(FAMILY_<W>, min_count=1)`. The emission must arise from a real spawn/special invocation (e.g. for `FAMILY_FIREBALL`, spawn a `FAMILY_MAGE` and inject the magic-fire special input).
- Rows ensuring each effect family is observed: `EffectFamilyCount(FAMILY_<E>, source_family_qualifier, min, max)`. `EffectFamilyCount` qualified to avoid `effect_count_unqualified` lint violation.
- For weapons/effects that cannot be exercised without an opponent (e.g. `FAMILY_BLOOD`), add a two-team scenario with a deterministic-strike opening input.
- `.plan/parity-coverage-manifest.md` updated for weapon + effect sections.

**File Changes**:
- `tests/parity/scenario_table.h` (rows added/modified).
- `tests/parity/test_parity_scenarios.cpp` (new `OG_PARITY_TEST` entries).
- Mirror.
- Goldens.
- Manifest.

Commits: branch `parity-cov: phase 06 — weapon and effect family completeness` + companion mirror.

**Implementation Details**:
- The 20 weapon families list (`KNIFE, ROCK, ARROW, FIREBALL, TREE, METEOR, SPRINKLE, BONE, BLOOD, BLOB, FIRE_ARROW, LIGHTNING, GLOW, WAVE, WAVE2, WAVE3, CIRCLE_PROTECTION, HAMMER, DOOR, BOULDER`) is enumerated in `kRequiredWeaponFamilies`. For each, identify the canonical emitting family (e.g. ARCHER → ARROW, MAGE → FIREBALL, BARBARIAN → HAMMER) and choose inputs that fire the weapon emission within the tick budget.
- Where a weapon's emission requires AI behaviour that is RNG-sensitive, set `tick_budget` high enough that emission probability across the run is effectively 1, and use `WeaponFamilyEmitted(family, min_count=1)`. Cite the RNG concession with an inline `// rng_drift:` comment so `lint_scenario_facts.py::unjustified_widening` accepts it.

**Verification Phases**:

- **`06a-check-weapon-and-effect-emitted`** (`check`, `bounce_target: 06-weapon-and-effect-completeness`):
  - For every name in `kRequiredWeaponFamilies`, assert at least one row has a `WeaponFamilyEmitted(<that family>, min>=1)` predicate.
  - For every name in `kRequiredEffectFamilies`, assert at least one row has an `EffectFamilyCount(<that family>, qualified, min>=1)` predicate.
  - `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h` exits 0 (the `effect_count_unqualified` and `vacuous_event_floor` rules must not trip).

- **`06b-check-tests-green-and-mirror`** (`check`, `bounce_target: 06-weapon-and-effect-completeness`):
  - `og_test_parity --gtest_filter='Parity.weapon_*_emission_scen99:Parity.effect_*_emission_scen99'` exits 0 with `[  FAILED  ] 0` and `[  SKIPPED ] 0`.
  - `cmp` mirror equal.

- **`06c-check-no-suite-regression`** (`check`, `bounce_target: 06-weapon-and-effect-completeness`):
  - Full `og_test_parity` `[  FAILED  ] 0`.
  - `python3 scripts/parity/check_coverage_manifest.py` exits 0 in the weapon + effect sections.

---

### Phase 07 — Treasure and generator family completeness

**Phase Name**: Every treasure family bound by a removal predicate; every generator family covered by a spawn-emit row.

**Implement Phase ID**: `07-treasure-and-generator-completeness`

**Preexisting Inputs**:
- `kRequiredTreasureFamilies` (13), `kRequiredGeneratorFamilies` (4).
- Treasure rows from phase 03 (now using `TreasureFamilyOfOrderRemovedFromOblist`).
- Existing `generator_*_emission_scen99` rows (4).
- `tests/parity/test_parity_coverage_gate.cpp` (`behavioural_coverage_gate_treasures`).

**New Outputs**:
- For every treasure family in `kRequiredTreasureFamilies` that does not yet have a `Treasure*RemovedFromOblist` (any kind) predicate, add the predicate to its row (or a new dedicated row if a sensible existing row is not available). For STAIN (`init_ignore=true`), add `WalkerFamilyCount(FAMILY_STAIN, 1, 1)` (under per-Order resolution) as the RNG-insensitive pin alongside the new TreasureFamilyOfOrderRemovedFromOblist row.
- For every generator family, ensure a row spawns one and runs long enough that it emits its enemy or treasure via `WalkerFamilyCount(<spawned-child>, min, max)` and `EventKindAtLeast("play_sound", N)`.
- Extend `behavioural_coverage_gate_treasures` so its required-bound set is `{ legacy_TreasureFamilyRemovedFromOblist_args | per_Order_TreasureFamilyOfOrderRemovedFromOblist_args_with_kOrderTreasure }`. The gate fails if any required treasure family is unbound.

**File Changes**:
- `tests/parity/scenario_table.h`.
- `tests/parity/test_parity_coverage_gate.cpp`.
- Mirror.
- Goldens.
- `.plan/parity-coverage-manifest.md`.

Commits: branch `parity-cov: phase 07 — treasure and generator completeness` + companion mirror.

**Implementation Details**:
- Implementation enumerates each treasure family id and resolves which existing row binds it; for unbound ids, adds a binding predicate to the most natural existing row (e.g. `treasure_<name>_pickup_scen99`) or creates a small new row.
- Generator emission rows: tick budget must be large enough that the generator's emission roll (RNG-dependent in master) fires at least once. Use `EventKindAtLeast` rather than `WalkerFamilyCount` exact if the emit count varies.

**Verification Phases**:

- **`07a-check-every-treasure-and-generator-bound`** (`check`, `bounce_target: 07-treasure-and-generator-completeness`):
  - Verifier loops `kRequiredTreasureFamilies` and `kRequiredGeneratorFamilies`. For each treasure id, asserts at least one row has a predicate of one of the two `Treasure*RemovedFromOblist` kinds with `arg0 == <id>`. For each generator family, asserts a `WalkerFamilyCount(<that family>, ...)` predicate on some row.

- **`07b-check-behavioural-gates-green`** (`check`, `bounce_target: 07-treasure-and-generator-completeness`):
  - `og_test_parity --gtest_filter='Parity.behavioural_coverage_gate*:Parity.treasure_*:Parity.generator_*'` exits 0 with `[  FAILED  ] 0` and `[  SKIPPED ] 0`.

- **`07c-check-no-suite-regression`** (`check`, `bounce_target: 07-treasure-and-generator-completeness`):
  - Full `og_test_parity` `[  FAILED  ] 0`.
  - `cmp` mirror equal.

---

### Phase 08 — Specials completeness and RNG-insensitivity rule

**Phase Name**: All 42 special slots exercised with deterministic verifying predicates; lint rule enforces at least one RNG-insensitive predicate per `SemanticParity` row.

**Implement Phase ID**: `08-specials-completeness-and-rng-insensitivity`

**Preexisting Inputs**:
- `kRequiredSpecials[]` (42 pairs).
- Existing `special_<family>_<idx>_scen99` rows (42, mostly skipped before phase 04).
- `Exercises` bitset in `tests/parity/scenario_table.h`.
- `scripts/parity/lint_scenario_facts.py` (existing 4 rules).

**New Outputs**:
- For every `(family, slot)` pair in `kRequiredSpecials`, ensure exactly one `special_<lowercase_family>_<slot>_scen99` row with:
  - `fresh_arena = true`, the target family spawned, a player on the same team that holds the special button.
  - `inputs[]` injecting the corresponding special-button key at a tick chosen so the special actually fires (lookup via `src/gameplay/families/family_<x>.cpp::do_special`).
  - `Exercises::Special_<bit>` set in the row's exercises bitmap.
  - `expected_facts[]` containing at least one RNG-insensitive predicate that specifically verifies the special fired. Examples by slot type:
    - Teleport-style → `WalkerPositionMoved(FAMILY_<X>, dx_min, dx_max)` with `dx_max-dx_min >= 100`.
    - Projectile-emit → `WeaponFamilyEmitted(FAMILY_<projectile>, min=1)`.
    - Effect-emit → `EffectFamilyCount(FAMILY_<effect>, source=FAMILY_<X>, min=1)`.
    - Self-buff (e.g. CIRCLE_PROTECTION) → `WalkerFamilyCount(FAMILY_<X>, 1, 1)` AND `EffectFamilyCount(FAMILY_CIRCLE_PROTECTION, source=FAMILY_<X>, 1, 1)`.
    - Whirlwind/melee → `EventKindAtLeast("play_sound", 2)` plus `WalkerOfTeamAlive(enemy_team, 0, k)`.
- New lint rule `requires_rng_insensitive_predicate` in `scripts/parity/lint_scenario_facts.py`:
  - For every row with `compare_mode == SemanticParity`, the `expected_facts[]` must contain at least one predicate of `RNG_INSENSITIVE_KINDS = {WalkerFamilyCount, EffectFamilyCount, WeaponFamilyEmitted, EventKindAtLeast, EventKindExactly, WalkerDiedByFinal, WalkerAliveAtFinal, TreasureFamilyRemovedFromOblist, TreasureFamilyOfOrderRemovedFromOblist, LevelDoneEquals, TickReached}`.
  - The rule emits a structured violation `(row_id, "no_rng_insensitive_predicate")` and the linter exits non-zero.
  - The exempt set is **computed**, not enumerated: the rule loads `tests/parity/scenario_facts_generated.json`, reads each row's `compare_mode` field, and skips every row whose `compare_mode != "SemanticParity"`. With today's table this exempts `rng_seed_stable_scen99` (Invariant) and any `ByteEqual` rows; the implementer does not maintain a hand-edited exempt list. Verifier 08b's anti-cheat mutation operates on a `SemanticParity` row (`family_archer_scen99`) and asserts the rule trips.

**File Changes**:
- `tests/parity/scenario_table.h` (42 rows audited; many existing inputs adjusted).
- `tests/parity/test_parity_scenarios.cpp` (`OG_PARITY_TEST` confirmed for each).
- `scripts/parity/lint_scenario_facts.py` (new rule).
- Mirror.
- Goldens.
- `.plan/parity-coverage-manifest.md`.

Commits: branch `parity-cov: phase 08 — specials completeness and RNG-insensitivity rule` + companion mirror.

**Implementation Details**:
- Slot-to-effect mapping is read from `src/gameplay/families/family_<x>.cpp`. For unfamiliar specials, implementation runs the scenario, captures the dump, and looks at the actual delta — the predicate should pin the observed delta tightly.
- For specials whose observable effect is purely RNG-driven (e.g. random teleport), use widely bracketed `WalkerPositionMoved` with `// rng_drift:` justification and pair with an RNG-insensitive `EventKindAtLeast` of the special's sound effect.

**Verification Phases**:

- **`08a-check-every-special-row-and-bit`** (`check`, `bounce_target: 08-specials-completeness-and-rng-insensitivity`):
  - For each `(family, slot)` in `kRequiredSpecials`, assert a row whose `family_spawns` includes the family AND whose `exercises` bitmap has `Special_<slot>` set, AND whose `expected_facts[]` contains an RNG-insensitive predicate.

- **`08b-check-lint-rule-trips-and-passes`** (`check`, `bounce_target: 08-specials-completeness-and-rng-insensitivity`):
  - `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h` exits 0 on the real file.
  - Anti-cheat: on a throwaway worktree `/tmp/parity-bypass-08b`, verifier removes every RNG-insensitive predicate from `family_archer_scen99` (via `sed -i`) and asserts `python3 scripts/parity/lint_scenario_facts.py <worktree>/tests/parity/scenario_table.h` exits non-zero with stdout containing `no_rng_insensitive_predicate`. Cleanup: `git worktree remove --force`.

- **`08c-check-specials-tests-green`** (`check`, `bounce_target: 08-specials-completeness-and-rng-insensitivity`):
  - `og_test_parity --gtest_filter='Parity.special_*'` exits 0 with `[  FAILED  ] 0` and `[  SKIPPED ] 0`.
  - Full `og_test_parity` `[  FAILED  ] 0`.

---

### Phase 09 — Event kind completeness

**Phase Name**: Every required event kind organically emitted by at least one row.

**Implement Phase ID**: `09-event-kind-completeness`

**Preexisting Inputs**:
- `kRequiredEventKinds[]` (9 strings).
- Existing `event_*_emission_scen99` rows.

**New Outputs**:
- For every kind in `kRequiredEventKinds` (e.g. `play_sound`, `notification`, `set_palette`, `request_redraw`, `end_game`, `set_end`, `request_exit_confirmation`, `withdraw_to_level`, `score_change`), at least one row whose run organically emits it AND has `EventKindAtLeast(kind, n>=1)` matched on both branch and master.
- For hard-to-emit kinds (`end_game`, `withdraw_to_level`), construct a minimal scenario that drives the game state into the emitting branch (e.g. exit-treasure pickup for `withdraw_to_level`, all-enemies-dead for `end_game`).

**File Changes**:
- `tests/parity/scenario_table.h`.
- Mirror.
- Goldens.
- Manifest.

Commits: branch `parity-cov: phase 09 — event kind completeness` + companion mirror.

**Verification Phases**:

- **`09a-check-every-event-kind-bound`** (`check`, `bounce_target: 09-event-kind-completeness`):
  - For every string `k` in `kRequiredEventKinds`, assert at least one row has `EventKindAtLeast(k, >=1)` in `expected_facts[]`.

- **`09b-check-event-rows-green`** (`check`, `bounce_target: 09-event-kind-completeness`):
  - `og_test_parity --gtest_filter='Parity.event_*'` exits 0 `[  FAILED  ] 0` `[  SKIPPED ] 0`.

- **`09c-check-no-suite-regression`** (`check`, `bounce_target: 09-event-kind-completeness`):
  - Full `og_test_parity` `[  FAILED  ] 0` `[  SKIPPED ] 0`.
  - `cmp` mirror equal.

---

### Phase 10 — Mutation canary green

**Phase Name**: Every non-exempt scenario row has a `discriminating_mutation` that flips ≥1 predicate; exempt list is finite and justified.

**Implement Phase ID**: `10-mutation-canary-green`

**Preexisting Inputs**:
- `scripts/parity/run_mutation_canary.sh`, `run_mutation_canary_runtime.py`.
- `.plan/parity-canary-exemptions.md` (may not yet exist — phase creates it).

**New Outputs**:
- For every row added in phases 05–09 whose `discriminating_mutation` is incomplete or whose canary does not flip, supply a real mutation (a one-line `sed`-applicable change in `tests/parity/scenario_table.h` or in source).
- `.plan/parity-canary-exemptions.md` listing each exempt scenario with a 1–2 sentence justification in the format `load_exemptions()` accepts. The literal grammar (per `scripts/parity/run_mutation_canary_runtime.py:74-91`): each exempt scenario id appears on its own line as a single Markdown bullet `- <scenario_id>` at column 0 (no nested indentation). The bullet's scenario id token is `\S+` after the leading `- `. Free-form prose (rationale, headings, paragraphs) may appear between bullets and is ignored by the parser; pipe-table rows (`| a | b |`) are NOT understood and MUST NOT be used. A literal example block (exactly two exempt rows):
  ```
  ## Exempt scenarios

  Justification for each row is captured below the bullet.

  - rng_seed_stable_scen99
    The Invariant compare-mode row has no `discriminating_mutation` because the
    invariant under test is "RNG seed survives load" — every mutation we could
    apply either changes the invariant (cheating) or has nothing to do with
    seed survival (vacuous). Verified by hand 2026-05-17.

  - smoke_noinput_scen99
    The canary's discriminating_mutation tick-budget bump leaves all
    expected_facts trivially satisfied because no input arrives within the
    window. Replaced with a non-trivial input would change the scenario's
    semantic identity; accept canary exemption.
  ```
  Verifier 10b uses `load_exemptions()` itself as the parsing authority — if the file parses to a non-empty Python `set`, the format is by definition correct.
- A list of every non-exempt scenario id and the flipped-predicate count from the canary run.

**File Changes**:
- `tests/parity/scenario_table.h` (mutations updated).
- `.plan/parity-canary-exemptions.md` (new or updated).
- Mirror.

Commits: branch `parity-cov: phase 10 — mutation canary green` + companion mirror.

**Verification Phases**:

- **`10a-check-canary-all-flip`** (`check`, `bounce_target: 10-mutation-canary-green`):
  - `scripts/parity/run_mutation_canary.sh --all 2>&1 | tee /tmp/p10a.out`. Verifier parses every per-scenario `/tmp/canary_runtime_<id>.json`, asserts every non-exempt id has `"flipped":` ≥ 1, and every exempt id is listed in `.plan/parity-canary-exemptions.md`.
  - Exit 0.

- **`10b-check-exemptions-parseable`** (`check`, `bounce_target: 10-mutation-canary-green`):
  - `python3 -c "import sys; sys.path.insert(0, 'scripts/parity'); import run_mutation_canary_runtime; print(run_mutation_canary_runtime.load_exemptions('.plan/parity-canary-exemptions.md'))"` exits 0 and prints a non-empty set.
  - Anti-cheat: `/tmp/parity-bypass-10b` adds a `| pipe | table | row |` line and asserts the parser rejects the file (returns the same set or raises). Cleanup.

- **`10c-check-no-suite-regression`** (`check`, `bounce_target: 10-mutation-canary-green`):
  - Full `og_test_parity` `[  FAILED  ] 0` `[  SKIPPED ] 0`.
  - `cmp` mirror equal.

---

### Phase 11 — Anti-cheat self-test and final sign-off

**Phase Name**: Codify and self-test every bypass; final sign-off doc; full repo test suite green.

**Implement Phase ID**: `11-anti-cheat-and-final-signoff`

**Preexisting Inputs**:
- `scripts/parity/lint_scenario_facts.py` (existing 4 + new 1 = 5 rules).
- Mutation canary (phase 10).
- `og_test_parity` (phase 09 green).

**New Outputs**:
- `scripts/parity/anti_cheat_selftest.sh` shell script that, for each known bypass, spins up a throwaway worktree, applies a realistic mutation, runs the corresponding guard, and asserts non-zero. Bypasses to cover:
  - **A**: Skip a treasure row via `compare_mode = ByteEqual` to dodge predicate evaluation → guard: `scripts/parity/check_coverage_manifest.py --require-semantic-parity-for-required-rows`.
  - **B**: Remove all RNG-insensitive predicates from a row → guard: `lint_scenario_facts.py::requires_rng_insensitive_predicate`.
  - **C**: Replace `EffectFamilyCount(FAMILY_X, source=Y, ...)` with unqualified version → `lint_scenario_facts.py::effect_count_unqualified`.
  - **D**: Add a dead predicate that is never evaluated → `lint_scenario_facts.py::dead_predicate`.
  - **E**: Widen `WalkerFamilyCount(FAMILY_X, 1, 1)` to `(0, 999)` without `rng_drift` comment → `lint_scenario_facts.py::unjustified_widening`.
  - **F**: Delete a golden to make a row skip → flip `test_parity_scenarios.cpp` so missing-golden + `compare_mode == SemanticParity` becomes `ADD_FAILURE` instead of `GTEST_SKIP`. The narrow target is the SINGLE call site at `tests/parity/test_parity_scenarios.cpp:108` whose literal message is `"master golden missing for "` (the SemanticParity-missing-golden branch). The other two `GTEST_SKIP` sites in this file are intentionally left intact: `:118` is the `ByteEqual` missing-golden path with message `"golden not yet captured for "` (a structurally different failure mode that this bypass narrative explicitly scopes out by saying "`compare_mode == SemanticParity`"), and `:144` is the `OG_PARITY_TEST(NAME)` macro's missing-scenario-in-`kScenarios` SKIP with message `"scenario \"" #NAME "\" is not present in kScenarios; "` (also a different failure mode — registration mismatch, not missing golden). Verifier asserts `grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `0` and `grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `1`. (Hardening the ByteEqual and missing-scenario-in-`kScenarios` paths is intentionally out of scope for Bypass F; if needed in the future, they belong in separate bypasses with their own per-site literal greps and rationales — not lumped under F.)
  - **G**: Stage a branch-side `scenario_table.h` change without mirroring → guard: a new pre-commit-friendly script `scripts/parity/check_mirror_sha.sh` that `cmp`s the two files and exits non-zero.
  - **H**: Add a row but never register `OG_PARITY_TEST(id)` → guard: `scripts/parity/check_test_registration.py` greps for every row id in `test_parity_scenarios.cpp`.
- `scripts/parity/ci_parity.sh` orchestrator that runs: build, full `og_test_parity`, `lint_scenario_facts.py`, `check_coverage_manifest.py`, `check_mirror_sha.sh`, `check_test_registration.py`, `run_mutation_canary.sh --all`, `anti_cheat_selftest.sh`. Exits non-zero if any fails.
- `.plan/parity-signoff-honest.md` summarising final state: literal totals (rows, predicates, RNG-insensitive ratio, coverage targets covered out of total), canary green count, anti-cheat bypass count.
- CI hookup: if `.github/workflows/ci.yml` exists at implement time, add a job entry that invokes `scripts/parity/ci_parity.sh`; if absent, this output is skipped (the orchestrator is still landed). Phase 11 verifier makes this conditional total and explicit: it runs `if [ -f .github/workflows/ci.yml ]; then grep -F 'scripts/parity/ci_parity.sh' .github/workflows/ci.yml > /dev/null && grep -F 'parity-ci' .github/workflows/ci.yml > /dev/null; else echo "ci.yml absent — orchestrator-only path"; fi`, and asserts exit 0 in either branch (so the check is a total function over both repo states).

**File Changes**:
- `scripts/parity/anti_cheat_selftest.sh` (new).
- `scripts/parity/ci_parity.sh` (new).
- `scripts/parity/check_mirror_sha.sh` (new).
- `scripts/parity/check_test_registration.py` (new).
- `scripts/parity/check_coverage_manifest.py` (add `--require-semantic-parity-for-required-rows` if not present).
- `tests/parity/test_parity_scenarios.cpp` (flip missing-golden path from SKIP to ADD_FAILURE when `compare_mode == SemanticParity`).
- `.github/workflows/ci.yml` (if present).
- `.plan/parity-signoff-honest.md` (new).

Commit: branch `parity-cov: phase 11 — anti-cheat self-test and final signoff` + companion mirror (mirror almost certainly unchanged in phase 11).

**Verification Phases**:

- **`11a-check-anti-cheat-selftest-passes`** (`check`, `bounce_target: 11-anti-cheat-and-final-signoff`):
  - `scripts/parity/anti_cheat_selftest.sh 2>&1 | tee /tmp/p11a.out`. Exit 0. Stdout contains `Bypass A: guard tripped`, `Bypass B: guard tripped`, ..., `Bypass H: guard tripped` (verifier `grep -F`s each line).
  - All `/tmp/parity-bypass-*` worktrees cleaned up (verifier asserts the directory contains no `parity-bypass-*` subdirs).

- **`11b-check-ci-orchestrator-and-fullsuite-green`** (`check`, `bounce_target: 11-anti-cheat-and-final-signoff`):
  - `scripts/parity/ci_parity.sh` exits 0.
  - `cmake --build --preset ci-test && ctest --preset ci-test` exits 0.
  - CI-file conditional (total): `if [ -f .github/workflows/ci.yml ]; then grep -F 'scripts/parity/ci_parity.sh' .github/workflows/ci.yml && grep -F 'parity-ci' .github/workflows/ci.yml; else true; fi` exits 0.

- **`11c-check-signoff-doc-and-manifest`** (`check`, `bounce_target: 11-anti-cheat-and-final-signoff`):
  - `.plan/parity-signoff-honest.md` exists and contains literal lines: `Total rows: <N>`, `Rows green: <N>`, `Rows with ≥1 RNG-insensitive predicate: <N>`, `Coverage manifest: <X>/<X>`, `Anti-cheat bypasses caught: 8/8`, `Canary non-exempt rows green: <N>/<N>`.
  - Verifier re-derives every integer and `grep -F`s into the doc.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

---

## 4. Critical Files

| File | Phase(s) touched | Nature of change |
|---|---|---|
| `tests/parity/state_dump.cpp` | 03 | Producer-side per-Order resolution of `walkers[].family` string. |
| `tests/parity/fact_predicate.h` | 03 | Append `FactKind::TreasureFamilyOfOrderRemovedFromOblist` and factory. |
| `tests/parity/fact_predicate.cpp` | 03 | Evaluator for the new FactKind. |
| `tests/parity/scenario_table.h` | 03, 05, 06, 07, 08, 09, 10 | Mass edits: treasure rows, walker rows, weapon/effect rows, generator/treasure rows, specials rows, event rows, mutation tweaks. |
| `tests/parity/test_parity_scenarios.cpp` | 03, 05–09, 11 | `OG_PARITY_TEST(...)` per new row; missing-golden ADD_FAILURE (phase 11). |
| `tests/parity/test_parity_coverage_gate.cpp` | 03, 07 | Gates walk both Treasure*RemovedFromOblist FactKinds. |
| `tests/parity/scenario_facts_generated.json` | 03–09 | Regenerated wholesale each affected phase. |
| `tests/parity/golden/*.json` | 03–09 | Mass replace via `capture_master_golden.sh`. |
| `../openglad-master/tools/parity_scenario_table.h` | 02–10 | Byte-equal mirror. |
| `../openglad-master/tools/parity_dump_state.{cpp,h}` | 03 | Mirror per-Order resolution. |
| `../openglad-master/build/ci-test/parity_dump_master` | 02, 03 | Rebuilt. |
| `scripts/parity/capture_master_golden.sh` | 04 | `--all`, `--out-dir`, `--no-write --diff`. |
| `scripts/parity/lint_scenario_facts.py` | 08 | Add `requires_rng_insensitive_predicate` rule. |
| `scripts/parity/check_coverage_manifest.py` | 01, 11 | Phase 01 adds `argparse`, `--emit-gap-table`, and `--emit-scenario-list`. Phase 11 adds `--require-semantic-parity-for-required-rows`. |
| `scripts/parity/check_mirror_sha.sh` | 11 | New: `cmp` branch vs companion. |
| `scripts/parity/check_test_registration.py` | 11 | New: every row id has `OG_PARITY_TEST`. |
| `scripts/parity/ci_parity.sh` | 11 | New orchestrator. |
| `scripts/parity/anti_cheat_selftest.sh` | 11 | New: 8 bypasses self-test. |
| `.plan/parity-present-state.md` | 01 | New: baseline + gap inventory. |
| `.plan/parity-coverage-manifest.md` | 01, 02, 05–09 | `master_companion_sha`, `covering_scenario_id` cells. |
| `.plan/parity-harness-design.md` | 03 | Append section on per-Order resolution and new FactKind. |
| `.plan/parity-recapture-diff.md` | 04 | New: list of recaptured goldens. |
| `.plan/parity-canary-exemptions.md` | 10 | New: parser-compatible exempt list. |
| `.plan/parity-signoff-honest.md` | 11 | New: final tally. |
| `.plan/master-companion.md` | 01, 02 | SHA pin. |
| `.github/workflows/ci.yml` | 11 (only if present) | Add `parity-ci` job invoking `ci_parity.sh`. |

## 5. Final Verification

After phase 11 the user can run the full verification chain:

```
# Branch worktree, clean
cd /home/yans/code/openglad
git status --porcelain                                    # empty
sha1sum tests/parity/scenario_table.h \
        ../openglad-master/tools/parity_scenario_table.h  # equal

# Build and test
cmake --build --preset ci-test
ctest --preset ci-test                                    # green

# Parity specifics
build/ci-test/og_test_parity --gtest_brief=1
# Expect:
#   [==========] N tests from 1 test suite ran.
#   [  PASSED  ] N tests.
#   No [  SKIPPED ] line.
#   No [  FAILED  ] line.

# Coverage manifest
python3 scripts/parity/check_coverage_manifest.py         # exit 0, no '(none yet)'

# Lint
python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h
                                                          # exit 0

# Mutation canary
scripts/parity/run_mutation_canary.sh --all               # every non-exempt scenario flips ≥1 predicate

# Anti-cheat self-test
scripts/parity/anti_cheat_selftest.sh                     # all 8 bypasses tripped

# CI orchestrator (composite)
scripts/parity/ci_parity.sh                               # exit 0

# Sign-off doc
test -f .plan/parity-signoff-honest.md
grep -F 'Anti-cheat bypasses caught: 8/8' .plan/parity-signoff-honest.md
grep -F 'Coverage manifest:' .plan/parity-signoff-honest.md
```

Every literal number in `.plan/parity-signoff-honest.md` is re-derivable from the live tooling. The mirror SHA equality and the absence of `(none yet)` cells in the coverage manifest, combined with the anti-cheat self-test catching every known bypass, give a verifiable certainty that branch and master are semantically equivalent for every entity type, special ability, attack type, and event occurrence required by `tests/parity/coverage_targets.h`.
