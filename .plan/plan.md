# Plan: Finish the gameplay-parity comparison and prove semantic equivalence

## 1. Context

### Current state and gaps

The harness is real: the loader is wired, scripted input is routed through
`sim_process_player_input`, 39 non-trivial goldens exist under
`tests/parity/golden/`, all 50 cases in `og_test_parity` pass, and
`Parity.coverage_gate*` reports green. The user's goal — verifiable semantic
equivalence between `wip/networking` and `origin/master` for every entity
type, special, attack type, and emitted occurrence — is **not** satisfied.
Evidence:

1. **The coverage gate is structural-only.**
   `tests/parity/test_parity_coverage_gate.cpp` ORs per-scenario
   `CoverageObservation::{walker,weapon,treasure,generator,effect}_families`
   across `kScenarios`. A `SpawnSpec` of the matching `order/family`
   flips the gate green — no acting, colliding, or comparing required.
   `kFamilySpawns_golem_with_nonliving_targets`
   (`tests/parity/scenario_table.h:433-476`) is one synthetic spawn per
   missing family parked on team 2; it passes the gate while proving
   nothing.

2. **Specials coverage is a self-declared bit.** `Exercises::Special_*`
   bits in `scenario_table.h` are author-set and OR-ed across
   `kScenarios`. `coverage_gate_specials` only checks the bit was claimed;
   it never inspects `walker::current_special` post-tick and never
   compares effect/weapon/event. `kFacts_family_mage_scen99` widens
   `WalkerFamilyCount(FAMILY_MAGE, 0, 3)` to absorb the fact that
   branch summons mage images and master does not — satisfied by either,
   verifies neither.

3. **Predicates were widened to absorb divergences.** Comments labelled
   `(a)` in `tests/parity/scenario_table.h` (e.g. `kFacts_family_mage_scen99`
   at 693, `kFacts_family_slime_scen99` at 732) show the pattern:
   master golden disagreed → range widened → green → no `parity-fix:`
   commit, no `intended_diff` row, no `.plan/parity-fixes.md` entry.
   The user's instruction prohibits this corner-cutting.

4. **`.plan/parity-divergence-report.md` and `.plan/parity-fixes.md` are
   stale empty-world artefacts** still declaring "zero regressions" and
   not matching the 39-golden tree or widened predicate state.

5. **Master companion SHA drift.**
   `.plan/parity-coverage-manifest.md` pins
   `master_companion_sha: c9f18a7b1eead675a6b09ded9134ead6e8de5950`;
   `.plan/master-companion.md` claims
   `ce70d23286f1e8034284e7c718ec658065f525e5`. No SHA-1 record proves
   `tests/parity/scenario_table.h` and
   `tools/parity_scenario_table.h` are byte-identical.

6. **Coverage breadth gap.** Not exercised:
   - **Treasure pickup behaviour** (every `treasure_family_*.cpp`): spawned
     in the blob, never walked onto.
   - **Generator spawns over time** (`FAMILY_TENT/TOWER/BONES/TREEHOUSE`
     emit walkers every N ticks; scenarios are too short).
   - **Per-family multi-special cycling**:
     `kInputsFamilySpecialCoverage` claims slots 1..4 without proving them.
   - **Effect families with no triggering scenario**:
     `FAMILY_FLASH`, `FAMILY_MAGIC_SHIELD`, `FAMILY_KNIFE_BACK`,
     `FAMILY_BOOMERANG`, `FAMILY_CLOUD`, `FAMILY_MARKER`,
     `FAMILY_DOOR_OPEN`, `FAMILY_HIT`, `FAMILY_EXPAND`,
     `FAMILY_GHOST_SCARE` — only blob-covered.
   - **Event kinds**: `notification`, `set_palette`, `request_redraw`,
     `end_game`, `set_end` are not naturally produced.
   - **Attack-type axis** (melee / ranged / special-projectile / splash):
     only `combat_attack_scen99` exists.

### Goal (verbatim from user)

> use gameplay parity comparison against master in a wide variety of scenarios,
> ensuring that cumulative coverage includes every single entity type,
> special ability effect, attack type, and occurrence in the game.
> Everything must be tested with no exceptions. Continue iterating until
> everything is fully tested, with copious checking in place to ensure
> agents don't cut corners. The reality of RNG differences will mean that
> things might not be byte-identical, but they should be checked for
> *verifiable certainty* that they are semantically equivalent.

### Five obligations

1. **Honest re-audit.** Inventory every passing test, widened predicate,
   coverage-by-blob entry, stale golden, and suppressed divergence into
   `.plan/parity-honest-audit.md`. Replaces the empty-world artefacts at
   `.plan/parity-divergence-report.md` and `.plan/parity-fixes.md`.

2. **Tighten predicate surface.** Every `WalkerFamilyCount(F, mn, mx)` with
   `mn != mx` needs a written justification anchored to a behavioural diff,
   and a paired `intended_diff` row or `parity-fix:` commit.
   `scripts/parity/lint_scenario_facts.py` gets a new
   "no unjustified widening" rule.

3. **Replace structural with behavioural coverage for every FAMILY_*.**
   Each weapon, treasure, FX, special, generator, and event kind gets a
   scenario whose `expected_facts[]` observes the entity's effect on the
   world:
   - weapons → projectile travels, damages, or expires
   - treasures → pickup triggers `score_change` / stat delta /
     `treasure_collected` event
   - FX → lifetime curve and source-walker family
   - specials → resulting weapon/effect/event count post-cast
   - generators → ≥1 emitted walker observed before tick budget
   - event kinds → naturally emitted from gameplay, not spawn blob

4. **Recapture goldens from a fresh companion build at a pinned SHA.**
   Companion `tools/parity_scenario_table.h` must match branch
   `tests/parity/scenario_table.h` SHA-1; manifest's
   `master_companion_sha:` updated to the actual companion commit.

5. **Anti-cheating infrastructure.** Each verifier phase re-derives its
   evidence. Mutation canary widened to flip a predicate for every row.
   `Parity.no_unjustified_widening` reads source and fails on widened
   predicates with no paired justification. `Parity.behavioural_coverage_gate`
   asserts every `FAMILY_*` and `EventKind` is touched by a predicate.

### Codebase facts

- Runner `tests/parity/parity_runner.cpp:60-144` constructs
  `LevelRuntimeData(level_id, /*headless=*/true, &sdl_level_data_hooks())`,
  calls `level.load()`, reapplies `world.rng_.state_ = spec.rng_seed`,
  optionally clears for `fresh_arena=true`, applies spawns via
  `scenario_runtime::apply_post_load_spawns`, and ticks `tick_budget` times,
  applying scripted input through `sim_process_player_input`
  (`scenario_runtime.cpp:111-173`). Extend; do not rewrite.
- Schema-v1 emitter `tests/parity/state_dump.cpp` is unchanged; records
  `effects[]`, `events[]`, `level_done`, `level_tick_count`, `rng_state`,
  `score_per_team[4]`, `tick`, `walkers[]`, optional `inventory_keys`.
  Production hooks (`sdl_level_data_hooks()`) drive the load so loaded
  `.fss` scenarios produce real walkers.
- `tests/parity/fact_predicate.{h,cpp}` defines `FactKind`, `FactPredicate`,
  `evaluate_facts`, `parse_state_dump`. The 16 kinds (`TickReached`,
  `LevelDoneEquals`, `ScoreDelta`, `WalkerFamilyCount`,
  `WalkerOfTeamAlive`, `WalkerHpRangeAtFinalTick`, `WalkerKeysApplied`,
  `WalkerPositionMoved`, `WalkerDiedByFinal`, `WalkerAliveAtFinal`,
  `TreasureFamilyRemovedFromOblist`, `StatDeltaOnPickup`,
  `EffectFamilyCount`, `EventKindAtLeast`, `EventKindExactly`,
  `WeaponFamilyEmitted`) cover every behavioural axis below.
  **No new predicate kinds. No schema changes.**
  `StatDeltaOnPickup` returns `indeterminate` for any stat not in `hp`/
  `max_hp` and is **never** the primary predicate for any new row;
  treasure-pickup rows rely on `TreasureFamilyRemovedFromOblist` +
  `EventKindAtLeast` + `WalkerHpRangeAtFinalTick` + downstream emission.
- Master companion: `/home/yans/code/openglad-master` on branch
  `parity-companion`, last commit
  `ce70d23286f1e8034284e7c718ec658065f525e5`. Binary:
  `../openglad-master/build/ci-test/parity_dump_master`. Capture:
  `scripts/parity/capture_master_golden.sh`. Validator:
  `scripts/parity/validate_schema.py`. Diff: `scripts/parity/diff_dumps.py`.
- `scripts/parity/lint_scenario_facts.py` enforces "non-empty fact
  requirements" and "non-default mutation"; parses
  `tests/parity/scenario_table.h`. New "no unjustified widening" rule
  lives here and reuses the parser.
- `scripts/parity/run_mutation_canary.sh` iterates every row and applies
  `discriminating_mutation`. Some rows have no-op mutations
  (`kMut_save_corrupt` etc.); this plan requires ≥1 flip per row with
  explicit doc for harness-incapable subjects.

### Inputs on disk (consumed in place, not regenerated)

- `.plan/goal.md` — never rewritten.
- `.plan/parity-risk-inventory.md` — kept as-is.
- `.plan/parity-harness-design.md` — amended in place only where the
  predicate / coverage taxonomy actually grows.
- `.plan/parity-coverage-manifest.md` — edited in place: flip
  `(none yet)` rows to new covering scenario ids, reconcile
  `master_companion_sha`, add a *behavioural observation* column.
- `.plan/parity-redo-audit.md`, `.plan/parity-signoff-fraudulent.md`,
  `.plan/master-baseline.md`, `.plan/master-companion.md` — historical
  record.
- `tests/parity/*.{h,cpp}` and `tests/parity/golden/*.json` — extended in
  place; goldens may be replaced 1:1 by recapture but never deleted en masse.
- `tests/parity/scenario_table.h` and
  `../openglad-master/tools/parity_scenario_table.h` — extended; the
  byte-for-byte sync contract holds.
- `scripts/parity/*.{sh,py}` — extended; no script renamed or deleted.
- `../openglad-master/tools/parity_dump_state.{h,cpp}`,
  `tools/parity_dump_master.cpp`, `tools/parity_dump_master_stubs.cpp`,
  `tools/parity_bootstrap.{h,cpp}` — extended only if the schema needs a
  new emitted key (it should not).

### What does *not* change

- Schema-v1 JSON shape and byte-for-byte branch/master table sync. No
  schema-v1.1.
- `../openglad-master` worktree path; no rebase, no force-push.
- Test code location (`tests/parity/`) and `og_test_parity` CMake
  registration (`CMakeLists.txt:1807`).
- `.plan/parity-risk-inventory.md` (read-only history).

### Stale artefacts archived (not silently overwritten)

- `.plan/parity-divergence-report.md` → `git mv` to
  `.plan/parity-divergence-report-empty-world.md` in Phase 1.
- `.plan/parity-fixes.md` → `git mv` to
  `.plan/parity-fixes-empty-world.md` in Phase 1.
- New documents under fresh names: `parity-honest-audit.md`,
  `parity-second-divergence-report.md`, `parity-second-fixes.md`.

## 2. Generated Workflow Contract

The generated `workflow.yaml` must satisfy every rule. Phase files
under `.plan/phases/*.md` and `.plan/workflow-structure.yaml` must be
self-consistent.

1. **Linear execution only.** `linear: true`. No `parallel_groups`, no
   fan-out, no fan-in. Phases run in numeric order from 1 to N.
2. **Inline-only YAML.** `yaml_source_mode: inline-only`. No top-level
   `include:`, no phase-level `prompt_file:`, `workflow_file:`,
   `workflow_dir:`, `checks:`, or any other YAML-source indirection.
   Each phase's `prompt:` is the complete agent instructions as a
   multiline string.
3. **No agent-guided bounce.** Each check phase declares at most one
   `bounce_target`, a fixed string equal to the implement phase's id.
   No `bounce_targets:` list; no choose-between-these logic.
4. **Every verifier is a top-level `check` phase.** Pattern:
   ```
   N    implement (id: ##-name)             bounce_target: null
   N+1  check     (id: ##a-check-name)      bounce_target: ##-name
   N+2  check     (id: ##b-check-name)      bounce_target: ##-name
   ```
   A single implement phase may be followed by multiple check phases;
   each check carries `bounce_target: ##-name` pointing at the same
   implement.
5. **A verifier stays in its block.** A check phase never bounces
   anywhere but the immediately preceding implement phase in the same
   numeric block.
6. **Checks run commands, not reads.** All shell commands
   (`cmake --build`, `ctest`, `scripts/parity/diff_dumps.py`,
   `scripts/parity/validate_schema.py`,
   `scripts/parity/lint_scenario_facts.py`,
   `scripts/parity/run_mutation_canary.sh`, `git log`, `grep -nE`,
   `sha1sum`, `python3 -c`) are written into the checker's `prompt:`
   literally, with expected exit code and failure trigger spelled out.
   Verifiers are agent phases that run shell commands and decide
   pass/fail; verifiers are never modelled as non-agentic phases.
7. **Existing artefacts are reused, not regenerated.** Each implement
   phase names its `Preexisting Inputs`. The agent's prompt instructs it
   to *read or update* those files in place. In particular:
   - `.plan/parity-risk-inventory.md` is not re-derived.
   - `.plan/parity-harness-design.md` is amended in place; the schema-v1
     contract is unchanged.
   - `.plan/parity-coverage-manifest.md` is amended in place; existing
     `(none yet)` rows with a real covering scenario are filled in by
     reading runner output, not rewriting from scratch.
   - Master companion source files (`../openglad-master/tools/*`) are
     extended in place. Companion is rebuilt, not re-cloned or rebased.
   - The 39 existing goldens are kept; Phase 5 recaptures each against
     the pinned companion and diffs the recapture against the on-disk
     golden — surviving goldens are byte-equal to the recapture,
     divergent ones are replaced one by one with a per-golden
     `parity-recapture:` commit citing the master companion SHA.
8. **Commit-before-yield.** Every implement phase's prompt contains a
   literal instruction to `git add` modified files and
   `git commit -m "..."` *before* yielding. The following check phase
   expects HEAD to contain the change; the check runs
   `git log -1 --name-status` and asserts the expected files are listed.
   **Two-worktree phases** (Phase 2 and Phase 4, which modify both the
   branch and `../openglad-master/`): the prompt MUST instruct the
   agent to commit on both worktrees independently
   (`git -C ../openglad-master add … && git -C ../openglad-master commit
   -m "parity-companion: …"` in addition to the branch-side commit),
   and the matching check verifies both HEADs with
   `git -C ../openglad-master log -1 --name-status`.
9. **Fraud-resistant check semantics.** Every check phase asserts that
   the *content* of a produced artefact is non-trivial, not merely that
   it exists. Examples:
   - Audit doc must contain a literal count of widened predicates *and*
     enumerate them by `(scenario_id, FactKind, arg range)`.
   - A "predicate strengthened" assertion must compare the post-tighten
     range to the pre-tighten range.
   - Mutation canary must report ≥1 flip per row, with a documented
     exception list whose size is bounded and enumerated.
   - Coverage gates assert structural *and* behavioural reachability;
     the behavioural gate fails if a `FAMILY_*` is only present via the
     blob spawn list with no scenario-specific predicate referencing
     that family id in `arg0`.
10. **No new YAML source files outside `workflow.yaml`.** The generated
    workflow is one file. Auxiliary data lives in the project tree as
    normal source artefacts.
11. **Every implement-phase prompt commits to git before yielding.**
    Rule #8 restated: load-bearing invariant for check phases.

## 3. Implementation Phases

Verifier counts per implement phase: `2, 3, 3, 3, 3, 3, 2, 2`.
Total: 8 implement + 21 check = 29 phases.

---

### Phase 1 — Honest audit and stale-artefact rename

**Phase Name**: Honest audit; rename empty-world reports.

**Implement Phase ID**: `01-honest-audit`

**Verification Phases**:
- `01a-check-audit-content` (`check`, `bounce_target: 01-honest-audit`):
  - `test -f .plan/parity-honest-audit.md` (must exist)
  - `test -f .plan/parity-divergence-report-empty-world.md` (must exist)
  - `test -f .plan/parity-fixes-empty-world.md` (must exist)
  - `test ! -f .plan/parity-divergence-report.md` (must NOT exist)
  - `test ! -f .plan/parity-fixes.md` (must NOT exist)
  - `python3 - <<'PY'` counts widened predicates (every
    `WalkerFamilyCount(..., mn, mx)` with `mn != mx`, every
    `WalkerOfTeamAlive(..., mn, mx)` with `mn != mx`, every
    `WalkerHpRangeAtFinalTick(..., mn, mx)` with `mx - mn > 200`) and
    prints `WIDENED_COUNT=<N>`. Audit MUST contain a literal single
    line matching `^Widened predicates: <N>$` byte-equal to the python
    result. Verifier extracts via `grep -E '^Widened predicates: '`
    and `grep -E '^WIDENED_COUNT='` and `diff`s the integers.
  - `grep -c '^| ' .plan/parity-honest-audit.md` ≥ 25.
  - `git log -1 --name-status` lists the rename and the new audit.
- `01b-check-history-preserved` (`check`, `bounce_target: 01-honest-audit`):
  - `git log --follow --oneline
    .plan/parity-divergence-report-empty-world.md | wc -l` ≥ 2 (rename
    commit plus ≥1 pre-rename commit), proving `git mv` and visible
    history.
  - Same assertion for `parity-fixes-empty-world.md`.
  - `git log -1 --diff-filter=R --name-status HEAD` lists both rename
    pairs; verifier greps for
    `R[0-9]+\s+\.plan/parity-divergence-report\.md` and
    `R[0-9]+\s+\.plan/parity-fixes\.md` in HEAD's name-status.

**Preexisting Inputs**:
- `.plan/goal.md`
- `.plan/parity-risk-inventory.md`
- `.plan/parity-harness-design.md`
- `.plan/parity-coverage-manifest.md`
- `.plan/parity-divergence-report.md` (stale empty-world era)
- `.plan/parity-fixes.md` (stale empty-world era)
- `.plan/parity-signoff-fraudulent.md`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json` (39 files)
- `tests/parity/test_parity_scenarios.cpp`
- `tests/parity/test_parity_coverage_gate.cpp`
- `tests/parity/fact_predicate.h`
- `scripts/parity/lint_scenario_facts.py`
- `scripts/parity/run_mutation_canary.sh`

**New Outputs**:
- `.plan/parity-honest-audit.md` — authoritative present-day audit.
  Required sections:
  (a) **Current test surface**: every `Parity.*` test name and current
      pass/fail (`build/ci-test/og_test_parity --gtest_list_tests`).
      Note 50 pass / 0 fail today.
  (b) **Widened-predicate inventory**: per scenario in `kScenarios`,
      every `FactPredicate` where `(min, max)` exceeds exact-value
      semantics (`mn != mx` for counts, `max - min > 200` for HP).
      Cite line numbers in `scenario_table.h`. Tag each row
      `widening_justification: present | absent`. Predicates with the
      inline `(a)` comment count as "present"; uncommented predicates
      count as "absent" and become Phase 3 work items.
  (c) **Structural-only coverage entries**: every `(FAMILY_*, order)`
      pair only reachable via
      `kFamilySpawns_golem_with_nonliving_targets` (not referenced by
      any `expected_facts[]` predicate's `arg0`). Cite the manifest
      row in `.plan/parity-coverage-manifest.md`.
  (d) **Master-companion SHA reconciliation**: list both current SHAs
      (`c9f18a7b...` in manifest frontmatter, `ce70d2328...` in
      `.plan/master-companion.md`), name the reconciliation target
      (current `parity-companion` HEAD on `../openglad-master/`, via
      `git -C ../openglad-master rev-parse HEAD`), state that Phase 5
      re-captures every golden from that SHA.
  (e) **Stale-document rename log**: list both renamed files and the
      reason. State that prior divergence-report and fixes were
      empty-world era and renamed (not deleted) to preserve history.
  (f) **Coverage-gap inventory by axis**:
      - Walker families with no behavioural predicate beyond
        `WalkerFamilyCount(family, 0, 0)`. Per family list the first
        missing behavioural axis (HP, position, event, damage).
      - Weapon families: every entry in `kRequiredWeaponFamilies` with
        no `WeaponFamilyEmitted(arg0=family)` predicate.
      - Treasure families: every entry in `kRequiredTreasureFamilies`
        with no `TreasureFamilyRemovedFromOblist` or
        `StatDeltaOnPickup` predicate.
      - Effect families: every entry in `kRequiredEffectFamilies` with
        no `EffectFamilyCount(arg0=family)` predicate.
      - Specials: every `(family, idx)` pair in `kRequiredSpecials`
        not appearing in any row's `discriminating_mutation` rationale
        or `expected_facts` and only claimed by an `Exercises::Special_*`
        bit.
      - Event kinds: every kind in `kRequiredEventKinds` not appearing
        in any `EventKindAtLeast` / `EventKindExactly` predicate.
  (g) **Mutation-canary delta**: every row whose
      `discriminating_mutation` doc admits "the parity runner does not
      invoke the subject" (today `save_roundtrip_scen99` and
      `rng_seed_stable_scen99`).
- `.plan/parity-divergence-report-empty-world.md` — renamed via `git mv`.
- `.plan/parity-fixes-empty-world.md` — renamed via `git mv`.

**File Changes**:
- `git mv .plan/parity-divergence-report.md .plan/parity-divergence-report-empty-world.md`
- `git mv .plan/parity-fixes.md .plan/parity-fixes-empty-world.md`
- Create `.plan/parity-honest-audit.md`.
- Commit message: `parity-finish-2: phase 01 — honest audit; rename empty-world reports`.

**Implementation Details**:
Agent runs live commands to populate the audit. No source code modified.
```bash
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_list_tests > /tmp/parity_tests.txt
build/ci-test/og_test_parity --gtest_brief=1 > /tmp/parity_run.txt
python3 - <<'PY' > /tmp/widened.txt
import re, pathlib
text = pathlib.Path('tests/parity/scenario_table.h').read_text()
# Enumerate every WalkerFamilyCount, WalkerOfTeamAlive, WalkerHpRangeAtFinalTick
# and report cases where mn != mx (or hp range > 200 cents).
# Print line:scenario:predicate for each.
PY
git -C ../openglad-master rev-parse HEAD > /tmp/companion_sha.txt
sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
```
Each audit section cites actual stdout of these commands.

**Verification**:
```
test -f .plan/parity-honest-audit.md
test -f .plan/parity-divergence-report-empty-world.md
test -f .plan/parity-fixes-empty-world.md
test ! -f .plan/parity-divergence-report.md
test ! -f .plan/parity-fixes.md
git log --follow --oneline .plan/parity-divergence-report-empty-world.md | tail -1
git log -1 --name-status | grep -c 'parity-honest-audit.md'
```

---

### Phase 2 — Master companion re-validation and SHA pinning

**Phase Name**: Rebuild master companion at a fresh SHA; pin in docs;
recapture-vs-existing diff across all 39 goldens.

**Implement Phase ID**: `02-companion-revalidation`

**Verification Phases**:
- `02a-check-companion-build` (`check`, `bounce_target: 02-companion-revalidation`):
  - `cd ../openglad-master && cmake --build --preset ci-test --target parity_dump_master` exits 0.
  - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
  - `sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
    — both SHA-1s must match.
- `02b-check-companion-list-matches` (`check`, `bounce_target: 02-companion-revalidation`):
  - `../openglad-master/build/ci-test/parity_dump_master --list > /tmp/cmaster_ids.txt`;
    compare to `python3 - <<'PY'` extracting every `kScenarios[].id`
    where `is_branch_internal == false`. Sets equal (ordering free).
- `02c-check-recapture-diff-log` (`check`, `bounce_target: 02-companion-revalidation`):
  - `test -f .plan/parity-recapture-diff.md` and grep for section
    `## Per-golden recapture diff` whose row count equals
    `kMasterComparableScenarioCount` (currently 38 — 39 goldens minus
    branch-internal).
  - `grep -c '^| ' .plan/parity-recapture-diff.md` ≥ 38.
  - `grep -E '^\| [a-z_0-9]+_scen[0-9]+ +\| (byte-equal|diff)' .plan/parity-recapture-diff.md | wc -l`
    matches the row count.
  - Phase 2 commit message contains the literal current companion SHA
    (`git log -1 --pretty=%B` matches `git -C ../openglad-master rev-parse HEAD`).

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md` (Phase 1)
- `.plan/parity-coverage-manifest.md` (SHA to reconcile)
- `.plan/master-companion.md` (SHA reference to reconcile)
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json` (39 files; not deleted)
- `../openglad-master/tools/parity_scenario_table.h`
- `../openglad-master/tools/parity_dump_master.cpp`
- `../openglad-master/tools/parity_dump_state.{h,cpp}`
- `../openglad-master/tools/parity_bootstrap.{h,cpp}`
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`
- `scripts/parity/diff_dumps.py`

**New Outputs**:
- `.plan/parity-recapture-diff.md` — per-golden recapture result.
  Required sections:
  - **Header**: pinned companion SHA (current HEAD of `parity-companion`),
    branch HEAD SHA, branch-side `tests/parity/scenario_table.h` SHA-1,
    companion-side `tools/parity_scenario_table.h` SHA-1. Two table
    SHA-1s must be equal.
  - **Per-golden recapture diff** table with one row per
    master-comparable scenario (38 rows). Columns:
    `scenario_id | bytes_before | bytes_after | result (byte-equal/diff) | notes`.
    `result == diff` rows include one-line summary of which fields
    changed (RNG state, events count, walker count); these become
    per-golden replacements in Phase 5 and are tracked by Phase 6.
  - **Outcome summary**: count of byte-equal vs diff vs schema-invalid rows.
- Updated `.plan/parity-coverage-manifest.md` frontmatter
  `master_companion_sha:` — set to actual companion HEAD.
- Updated `.plan/master-companion.md` — refresh SHA tables and
  "Drift-detection SHA-1s" section to current values.
- If `tests/parity/scenario_table.h` SHA differs from companion mirror:
  copy branch → master, commit on master, re-run. This is the only
  `tests/parity/` write Phase 2 may perform.

**File Changes**:
- Modify `.plan/parity-coverage-manifest.md` (frontmatter only).
- Modify `.plan/master-companion.md` (SHA tables and "Drift-detection" rows).
- Create `.plan/parity-recapture-diff.md`.
- If needed:
  `cp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
  and commit on `../openglad-master` (`parity-companion` branch).
- Branch commit: `parity-finish-2: phase 02 — companion revalidation; pinned SHA <hash>; <N> goldens diverge from recapture`.
- Master commit (if any): `parity-companion: phase 02 — mirror scenario_table.h SHA-1 from branch <hash>`.

**Implementation Details**:
```bash
cd /home/yans/code/openglad-master
git checkout parity-companion
cmake --preset ci-test
cmake --build --preset ci-test --target parity_dump_master
COMPANION_SHA=$(git rev-parse HEAD)
cd /home/yans/code/openglad
mkdir -p /tmp/recapture
for id in $(../openglad-master/build/ci-test/parity_dump_master --list); do
    ../openglad-master/build/ci-test/parity_dump_master \
        --scenario "$id" --out "/tmp/recapture/$id.json"
    python3 scripts/parity/validate_schema.py "/tmp/recapture/$id.json"
done
for f in tests/parity/golden/*.json; do
    id=$(basename "$f" .json)
    if [ -f "/tmp/recapture/$id.json" ]; then
        if cmp -s "$f" "/tmp/recapture/$id.json"; then
            echo "$id byte-equal"
        else
            echo "$id diff:"
            diff <(python3 -m json.tool "$f") <(python3 -m json.tool "/tmp/recapture/$id.json") | head
        fi
    fi
done
```
Goldens are NOT modified in this phase (Phase 5 does the replacement);
this phase only produces the diff log. The two table SHA-1s on
branch/companion sides MUST match before recapture starts; if not, the
agent first mirrors the branch table to the companion.

**Verification**:
```
sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
diff <(awk '{print $1}' <(sha1sum tests/parity/scenario_table.h)) \
     <(awk '{print $1}' <(sha1sum ../openglad-master/tools/parity_scenario_table.h))
test -x ../openglad-master/build/ci-test/parity_dump_master
test -f .plan/parity-recapture-diff.md
grep -q "master_companion_sha: $(git -C ../openglad-master rev-parse HEAD)" .plan/parity-coverage-manifest.md
```

---

### Phase 3 — Tighten widened predicates and add the "no-widening" lint

**Phase Name**: Strengthen predicate surface; lint refuses unjustified
range widening.

**Implement Phase ID**: `03-tighten-predicates`

**Verification Phases**:
- `03a-check-lint-passes` (`check`, `bounce_target: 03-tighten-predicates`):
  - `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h`
    exits 0 with no diagnostics.
  - With a tampered table (one `WalkerFamilyCount` range artificially
    widened), the lint must exit non-zero with `unjustified_widening`
    diagnostic. Construct via
    `sed -E 's/WalkerFamilyCount\(([^,]+),\s*([0-9]+),\s*\2/WalkerFamilyCount(\1, \2, 99/'`
    over a `/tmp/tampered.h` copy, run
    `LINT_SCENARIO_TABLE=/tmp/tampered.h python3 scripts/parity/lint_scenario_facts.py`,
    assert exit code != 0 with diagnostic present.
- `03b-check-tests-still-green` (`check`, `bounce_target: 03-tighten-predicates`):
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity` — every `Parity.*` case still passes.
    Tightening relies on pre-Phase-2-diff goldens; Phase 5 may force
    another round if recapture replaces a golden.
- `03c-check-widening-justified` (`check`, `bounce_target: 03-tighten-predicates`):
  - `python3 - <<'PY'` scans `scenario_table.h` for every widened
    predicate; asserts either (i) the line is followed (within 3 lines)
    by an inline comment matching
    `// .*(branch|master|widen|intended_diff|parity-fix)` OR
    (ii) the row's `discriminating_mutation` rationale references the
    same FactKind. Failure prints offending
    `(scenario_id, line, predicate)` and exits non-zero.
  - The new lint rule lives in `scripts/parity/lint_scenario_facts.py`;
    verifier re-invokes the lint to demonstrate it is wired.

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md` (widened-predicate inventory drives the work list)
- `.plan/parity-recapture-diff.md` (if a golden was replaced, predicate must match new golden)
- `tests/parity/scenario_table.h`
- `tests/parity/fact_predicate.{h,cpp}`
- `scripts/parity/lint_scenario_facts.py`

**New Outputs**:
- Updated `tests/parity/scenario_table.h`:
  - Every `WalkerFamilyCount(family, mn, mx)` with `mn != mx` is either:
    (a) narrowed to `(mx, mx)` or `(mn, mn)` if recapture confirms
        master value is stable;
    (b) replaced by `EffectFamilyCount`, `WalkerDiedByFinal`, or
        `WeaponFamilyEmitted` capturing the actual behavioural diff
        with exact count; or
    (c) accompanied by inline `// intended_diff: <reason>; cited commit <sha>`
        recognised by the new lint rule, with a corresponding entry in
        `.plan/parity-honest-audit.md` "Reclassified rows" section.
  - Same treatment for `WalkerOfTeamAlive(team, mn, mx)` widened ranges.
  - `WalkerHpRangeAtFinalTick` ranges wider than 200 cents either narrow
    to ≤200 OR cite `// rng_drift: <reason>` linked to a new
    `intended_diff` row.
- Updated `scripts/parity/lint_scenario_facts.py` with
  `unjustified_widening` rule. Parser walks `kFacts_<id>[]`, identifies
  widened predicates, requires per-predicate justification.
- Updated `.plan/parity-honest-audit.md` — append "Reclassified rows"
  subsection listing every narrowed/widened-with-citation/deleted row.
- Parser for inline `intended_diff` / `rng_drift` comments lives inside
  `scripts/parity/lint_scenario_facts.py`. Reuses existing C++ table parser.

**File Changes**:
- Modify `tests/parity/scenario_table.h` (predicate tightenings).
- Modify `scripts/parity/lint_scenario_facts.py` (new rule).
- Modify `.plan/parity-honest-audit.md` (append section).
- Commit: `parity-finish-2: phase 03 — tighten predicates and add no-unjustified-widening lint`.

**Implementation Details**:
- Re-run `build/ci-test/og_test_parity` after each tightening; reverts
  to the `intended_diff` citation path with an audit doc entry if a row
  regresses.
- Lint rule grammar: an `intended_diff` citation is an inline C++
  comment matching `// intended_diff: .{20,}; commit [0-9a-f]{7,40}`
  placed immediately after the predicate in `kFacts_<id>[]`. An
  `rng_drift` citation has the same shape with the leading keyword
  `rng_drift`.
- Updated audit subsection format:
  ```markdown
  ## Reclassified rows

  | scenario_id | predicate | before | after | citation |
  |---|---|---|---|---|
  | family_mage_scen99 | WalkerFamilyCount(FAMILY_MAGE, ...) | (0, 3) | (0, 0) | <sha-or-reason> |
  ```

**Verification**:
```
python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h && echo OK
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_brief=1 | grep -E '\[  FAILED  \]' || echo "all green"
grep -c '^| family_' .plan/parity-honest-audit.md
```

---

### Phase 4 — Behavioural coverage scenarios (weapons, treasures, FX, generators, events)

**Phase Name**: Replace blob-spawn coverage with per-entity behavioural
scenarios.

**Implement Phase ID**: `04-behavioural-coverage`

**Verification Phases**:
- `04a-check-behavioural-gate` (`check`, `bounce_target: 04-behavioural-coverage`):
  - Compile and run new tests:
    `Parity.behavioural_coverage_gate_weapons`,
    `Parity.behavioural_coverage_gate_treasures`,
    `Parity.behavioural_coverage_gate_effects`,
    `Parity.behavioural_coverage_gate_generators`,
    `Parity.behavioural_coverage_gate_event_kinds`,
    `Parity.behavioural_coverage_gate` (umbrella). Each must pass.
- `04b-check-no-blob-scenario-needed` (`check`, `bounce_target: 04-behavioural-coverage`):
  - `python3 - <<'PY'` reads `scenario_table.h` and asserts that
    removing `kFamilySpawns_golem_with_nonliving_targets` (simulated by
    name-grepping out) still leaves every required family covered by
    another scenario's `expected_facts[]`. Failure exits non-zero.
- `04c-check-gtests-pass` (`check`, `bounce_target: 04-behavioural-coverage`):
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity --gtest_brief=1` — no failures.
  - `build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'`
    — all seven structural gates pass.

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md` (coverage-gap inventory in §(f))
- `.plan/parity-coverage-manifest.md`
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_table.h`
- `tests/parity/parity_runner.cpp` (extended for treasure pickup script)
- `tests/parity/scenario_runtime.cpp` (extended for input combinations)
- `tests/parity/fact_predicate.{h,cpp}` (no new predicate kinds; existing 16 suffice)
- `tests/parity/test_parity_coverage_gate.cpp` (extended)
- `tests/parity/test_parity_scenarios.cpp` (one new `OG_PARITY_TEST(id)` per new scenario)
- `tests/parity/state_dump.{h,cpp}` (schema-v1 unchanged — read-only)
- `tests/parity/golden/*.json` (existing 39 untouched; Phase 5 captures new ones)
- `../openglad-master/tools/parity_scenario_table.h` (mirror)
- `scripts/parity/lint_scenario_facts.py` (extended if widening reappears in new rows)

**New Outputs** — concrete new scenarios with binding-predicate facts:

- **Treasure-pickup scenarios** (one per treasure family except
  `FAMILY_EXIT` already exercised, `FAMILY_STAIN` passive blood splash):
  - `treasure_gold_bar_pickup_scen99`,
    `treasure_silver_bar_pickup_scen99`,
    `treasure_drumstick_pickup_scen99`,
    `treasure_magic_potion_pickup_scen99`,
    `treasure_invis_potion_pickup_scen99`,
    `treasure_invulnerable_potion_pickup_scen99`,
    `treasure_flight_potion_pickup_scen99`,
    `treasure_teleporter_pickup_scen99`,
    `treasure_life_gem_pickup_scen99`,
    `treasure_key_pickup_scen99`,
    `treasure_speed_potion_pickup_scen99`,
    `treasure_stain_observation_scen99` (passive — soldier walks over
    STAIN spawn, treasure stays in oblist with stable position).
  - Spawn pattern: lone soldier on team 0 at `(96, 120)`; one treasure
    of target family at `(160, 120)` via `kOrderTreasure`. Script
    `K_RIGHT` for ticks 1..20 so the soldier walks east through it.
  - Predicates per row (all required):
    - `TickReached(150)`
    - `WalkerPositionMoved(FAMILY_SOLDIER, ≥160, 120)` — reached treasure tile
    - `TreasureFamilyRemovedFromOblist(FAMILY_<TREASURE>)` — consumed (except STAIN)
    - `EventKindAtLeast(score_change, 1)` for value-bearing treasures (gold, silver, gem)
    - `EventKindAtLeast(play_sound, 2)` for any audible pickup
    - For HP-bearing treasures (`FAMILY_DRUMSTICK`, `FAMILY_GEM`,
      `FAMILY_LIFE_GEM`): `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, mn, mx)`
      with master-pinned exact bounds.
    - For stat-bearing treasures whose effect is not in schema-v1
      (`FAMILY_MAGIC_POTION` raises magicpoints, `FAMILY_SPEED_POTION`
      raises speed, `FAMILY_INVIS_POTION` / `FAMILY_INVULNERABLE_POTION` /
      `FAMILY_FLIGHT_POTION` set timed flags): primary predicate is
      **downstream emission** — after pickup, soldier casts a special
      or attacks; predicate asserts resulting `WeaponFamilyEmitted` /
      `EffectFamilyCount` / movement only possible because pickup happened.
      `StatDeltaOnPickup` included for documentation; allowed to
      evaluate `indeterminate`; never the sole gating predicate.
    - For key pickup (`FAMILY_KEY`):
      `WalkerKeysApplied(FAMILY_SOLDIER, mask)` confirms bit set.
    - For teleporter (`FAMILY_TELEPORTER`): `WalkerPositionMoved` with
      exact post-warp coordinate matches master.
    - A `discriminating_mutation` whose subject is the treasure's
      pickup hook in `src/gameplay/families/treasure_family_*.cpp`.
      Mutation neuters the pickup; canary asserts ≥1 predicate flip.
- **Weapon-emission scenarios** — for each weapon family not naturally
  emitted by existing arenas, add wielder + sparring partner +
  `kInputsCombatAttack99`:
  - `weapon_knife_emission_scen99` (SOLDIER w/ knife),
    `weapon_arrow_emission_scen99` (ARCHER default),
    `weapon_fireball_emission_scen99` (MAGE special 1),
    `weapon_tree_emission_scen99` (DRUID special 1 — GROW TREE),
    `weapon_meteor_emission_scen99`,
    `weapon_sprinkle_emission_scen99`,
    `weapon_bone_emission_scen99`,
    `weapon_blood_emission_scen99` (passive blood-splash sprite),
    `weapon_blob_emission_scen99`,
    `weapon_fire_arrow_emission_scen99` (Archer special 1),
    `weapon_lightning_emission_scen99` (chain via Archmage),
    `weapon_glow_emission_scen99`,
    `weapon_wave_emission_scen99` (Mage special 4),
    `weapon_wave2_emission_scen99`, `weapon_wave3_emission_scen99` (cascade),
    `weapon_circle_protection_emission_scen99` (DRUID special 4 — PROTECTION),
    `weapon_hammer_emission_scen99`,
    `weapon_door_emission_scen99` (door object is `Order::Weapon`),
    `weapon_boulder_emission_scen99` (BARBARIAN special 1).
  - Spawn: wielder on team 0 at `(120, 120)`, target on team 1 at
    `(180, 120)`. Use `set_default_weapon` / `set_current_weapon` in
    `SpawnSpec` to force wielder onto target weapon family when family
    default does not match. ROCK is naturally emitted by ELF; if missing
    today, add `weapon_rock_emission_scen99`.
  - Predicates per row:
    - `TickReached(150)`
    - `WeaponFamilyEmitted(FAMILY_<WEAPON>)` — primary; searches `dump.weapons[]`.
    - `EffectFamilyCount(FAMILY_HIT, ≥1, ≤8, source=FAMILY_<wielder>)` where collision produces HIT.
    - Discriminating mutation pointing at weapon family's `act()` that
      suppresses emission or zeros damage.
- **Effect-family scenarios** — one per missing FX family:
  - `effect_expand_emission_scen99`, `effect_ghost_scare_emission_scen99`,
    `effect_explosion_emission_scen99` (use existing bomb spec extended),
    `effect_flash_emission_scen99`, `effect_magic_shield_emission_scen99`,
    `effect_knife_back_emission_scen99`,
    `effect_boomerang_emission_scen99` (Soldier special 2),
    `effect_cloud_emission_scen99` (Thief special 4 — POISON CLOUD),
    `effect_marker_emission_scen99`,
    `effect_door_open_emission_scen99` (walk onto door tile with a key),
    `effect_hit_emission_scen99`.
  - Predicates:
    - `TickReached(<budget>)`
    - `EffectFamilyCount(FAMILY_<EFFECT>, mn, mx, source=FAMILY_<source>)`
      with `mn == mx` (exact count from master golden after Phase 5).
- **Generator scenarios** — for each of TENT/TOWER/BONES/TREEHOUSE:
  - `generator_tent_emission_scen99`, `generator_tower_emission_scen99`,
    `generator_bones_emission_scen99`, `generator_treehouse_emission_scen99`.
  - Spawn: just the generator on team 1 at `(120, 120)`. Tick budget = 300.
    `fresh_arena = true`.
  - Predicates:
    - `TickReached(300)`
    - `WalkerFamilyCount(FAMILY_<SPAWNED>, mn, mx)` with `mn ≥ 1` and
      `mx ≤ 6` (master-pinned cap after Phase 5).
- **Event-kind scenarios** — for each `EventKind` not yet produced:
  - `event_notification_emission_scen99` — trigger via MAGE DIED or
    similar death-message path (existing `effect_chain_scen9410` shows
    `notification: MAGE DIED`; promote that to a primary fact).
  - `event_set_palette_emission_scen99` — palette change on level start
    with palette-changing cast. If no organic path, use level-transition
    palette-set in `glad.cpp` via an `EXIT` treasure pickup.
  - `event_request_redraw_emission_scen99` — emitted by HUD updates on
    score change; reuse scoring scenario and assert
    `EventKindAtLeast(request_redraw, 1)`.
  - `event_end_game_emission_scen99` — last-player-dies path; spawn one
    player walker, no allies, three enemies, no input; assert `EndGame`
    event at game-end tick.
  - `event_set_end_emission_scen99` — `level_done == 1` plus `set_end`;
    reuse exit-trigger arena and assert `EventKindExactly(set_end, 1)`.
- **Per-family special-cast scenarios** — for each of the 42
  `kRequiredSpecials` pairs not already covered by a per-family arena
  (or whose arena uses `kInputsFamilySpecialCoverage` without isolating
  per-slot behaviour), add a targeted scenario:
  - `special_<family>_<idx>_scen99` (e.g. `special_soldier_2_scen99` for
    BOOMERANG, `special_archer_2_scen99` for BARRAGE,
    `special_cleric_2_scen99` for RAISE UNDEAD).
  - Each scenario:
    - `stats_level` raised via `SpawnSpec::stats_level` to the floor
      `(idx - 1) * 3 + 1` so the cycle gate
      (`sim_input_handler.cpp:218`) accepts the slot.
    - `magicpoints` raised to ≥ `special_cost(idx)` via
      `SpawnSpec::magicpoints` so the firing gate
      (`living.cpp:532-533`) permits the cast.
    - Inputs: cycle `K_SPECIAL_SWITCH` exactly `(idx - 1)` times to
      arrive at target slot, then press `K_SPECIAL` once.
    - Exercises bit: exactly the one `Special_<family>_<idx>` bit.
    - Predicates: at least one of
      `WeaponFamilyEmitted(...)`, `EffectFamilyCount(...)`,
      `WalkerFamilyCount(<summoned-family>, 1, n)`,
      `EventKindExactly(<kind>, n)`,
      `WalkerPositionMoved` (teleport/blink),
      `WalkerHpRangeAtFinalTick` (heal/drain).
- Updated `tests/parity/test_parity_coverage_gate.cpp` with new gate
  cases per `04a-check-behavioural-gate`. Gates enumerate
  `kRequiredWeaponFamilies` and assert every family is `arg0` of at
  least one `WeaponFamilyEmitted` predicate in any scenario's
  `expected_facts[]`. Same for treasures, effects, generators, event
  kinds, specials.
- Updated `tests/parity/scenario_table.h` registering every new row and
  its `kFacts_*` / `kMut_*` constants.
- Mirror update of `../openglad-master/tools/parity_scenario_table.h`
  (byte-for-byte) and rebuild of `parity_dump_master`.
- Updated `.plan/parity-coverage-manifest.md` — flip every `(none yet)`
  cell to the new scenario id; add a "behavioural predicate" column
  citing the predicate that locks the entity to a specific behaviour.
- Updated `.plan/parity-harness-design.md` — append "Phase 04 redo:
  behavioural coverage" section documenting new gate cases and
  per-family slot scenarios.
- `kFamilySpawns_golem_with_nonliving_targets` may stay (some entities
  may still rely on it); `04b` enforces no required family loses coverage
  if blob is removed. If anything fails, the blob row stays.

**File Changes**:
- Modify `tests/parity/scenario_table.h` (new spawns, inputs, facts,
  mutations, `kScenarios` entries).
- Modify `tests/parity/test_parity_scenarios.cpp` (append one
  `OG_PARITY_TEST(<scenario_id>)` per new scenario id).
- Modify `tests/parity/test_parity_coverage_gate.cpp` (new gate cases).
- Modify `tests/parity/parity_runner.cpp` and
  `tests/parity/scenario_runtime.cpp` only if a new input pattern is
  required (verify before editing; runner is not extended to read or
  write `StateDump` fields).
- **Do NOT** modify `tests/parity/state_dump.{h,cpp}` — schema-v1 freeze
  is a hard rule. Treasure-pickup observability uses predicate
  alternatives spelled out above.
- Mirror to `../openglad-master/tools/parity_scenario_table.h` and
  recompile `parity_dump_master`.
- Modify `.plan/parity-coverage-manifest.md` and
  `.plan/parity-harness-design.md`.
- Branch commit: `parity-finish-2: phase 04 — behavioural coverage scenarios`.
- `../openglad-master` commit: `parity-companion: phase 04 — mirror scenario_table.h (<branch sha or short>)`.

**Implementation Details**:
- The 42 special-cast scenarios reuse the cycle/fire pattern from
  `kInputsFamilySpecialCoverage`; new per-slot scenarios constrain the
  cycle to the target slot only.
- Generator scenarios use `tick_budget = 300` because
  TENT / TOWER / etc. emit at intervals of ~150 ticks.
- Behavioural gate is `TEST(Parity, behavioural_coverage_gate_*)` in
  `test_parity_coverage_gate.cpp`; body scans `kScenarios[].expected_facts`
  arrays at runtime and asserts each required family / kind appears as
  `arg0` of at least one matching predicate.
- No changes to `fact_predicate.cpp` needed.

**Verification**:
```
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage*'
build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'
build/ci-test/og_test_parity --gtest_brief=1
sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
build/ci-test/og_test_parity --gtest_list_tests | grep -E '^  (treasure_|weapon_|effect_|generator_|event_|special_)[a-z0-9_]+$' | wc -l
```

---

### Phase 5 — Recapture and reconcile every golden

**Phase Name**: Run the pinned companion against every scenario (old +
new); commit the canonical golden set.

**Implement Phase ID**: `05-recapture-and-reconcile`

**Verification Phases**:
- `05a-check-golden-count` (`check`, `bounce_target: 05-recapture-and-reconcile`):
  - `ls tests/parity/golden/*.json | wc -l` equals
    `kMasterComparableScenarioCount` (python helper extracts count by
    parsing `scenario_table.h`).
  - `python3 - <<'PY'` checks every golden against
    `scripts/parity/validate_schema.py` (exit 0 on every file).
- `05b-check-recapture-fresh` (`check`, `bounce_target: 05-recapture-and-reconcile`):
  - Re-run `parity_dump_master` for every scenario into `/tmp/recheck/`;
    compare with `cmp -s` against committed `tests/parity/golden/`.
    Every golden byte-equal to recapture. Any diff fails.
- `05c-check-tests-green-against-new-goldens` (`check`, `bounce_target: 05-recapture-and-reconcile`):
  - `cmake --build --preset ci-test --target og_test_parity`
  - `build/ci-test/og_test_parity` — every case passes. Predicates
    tightened in Phase 3 must still hold; regression means
    `Parity.<id>` fails and the row needs another tightening round.

**Preexisting Inputs**:
- `.plan/parity-recapture-diff.md` (identifies divergent goldens)
- `tests/parity/scenario_table.h` (Phase 4-extended)
- `../openglad-master/tools/parity_scenario_table.h` (mirror; SHA-equal)
- `../openglad-master/build/ci-test/parity_dump_master`
- `tests/parity/golden/*.json` (existing 39 files)
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`
- `scripts/parity/diff_dumps.py`

**New Outputs**:
- Refreshed `tests/parity/golden/*.json` — every scenario in
  `kScenarios` with `is_branch_internal == false` has canonical golden
  at `tests/parity/golden/<id>.json`. Phase-4 new scenarios added;
  Phase-2 divergent goldens replaced in place; byte-equal goldens
  unchanged.
- `.plan/parity-second-divergence-report.md` — per-golden recapture &
  predicate-evaluation report (replaces empty-world
  `.plan/parity-divergence-report-empty-world.md` as current authority).
  Required sections:
  - **Header**: pinned companion SHA (same as Phase 2 unless rebuilt),
    branch HEAD SHA, total golden count, golden-replace count.
  - **Per-golden replacement log**: one row per replaced golden,
    columns `scenario_id | bytes_before | bytes_after | reason (recapture-diff / new-scenario)`.
  - **Predicate-evaluation table**: per scenario, each `expected_facts[]`
    entry and its branch / master evaluation result on the new golden.
    Rows where one side passes and the other fails are `regression`
    candidates (Phase 6).
  - **Classified divergences**: every dump field where semantic-evaluator
    branch result differs from master (e.g. branch HP `78.000000`,
    master HP `82.000000`). One of:
    - `regression` (Phase 6 owns the fix); cite suspect branch commit
      range via `git log origin/master..HEAD -- src/...`.
    - `intended_diff` (cited branch commit explicitly authorising the change).
    - `rng_drift` (RNG draws differ but every fact-predicate still
      holds; allowed only when predicate surface is strong enough to
      confirm equivalence under RNG variation).
- Updated `.plan/parity-coverage-manifest.md` `Phase X sign-off snapshot`
  section with new coverage outcome (every cell populated).

**File Changes**:
- `tests/parity/golden/*.json` (replace/add as needed).
- Create `.plan/parity-second-divergence-report.md`.
- Modify `.plan/parity-coverage-manifest.md` (sign-off snapshot).
- Commit: `parity-finish-2: phase 05 — recapture goldens against companion <sha>; <N> replaced, <M> added`.

**Implementation Details**:
```bash
COMPANION_SHA=$(git -C ../openglad-master rev-parse HEAD)
diff <(sha1sum tests/parity/scenario_table.h | awk '{print $1}') \
     <(sha1sum ../openglad-master/tools/parity_scenario_table.h | awk '{print $1}')
mkdir -p /tmp/golden_capture
for id in $(../openglad-master/build/ci-test/parity_dump_master --list); do
    ../openglad-master/build/ci-test/parity_dump_master \
        --scenario "$id" --out "/tmp/golden_capture/$id.json"
    python3 scripts/parity/validate_schema.py "/tmp/golden_capture/$id.json"
done
for f in /tmp/golden_capture/*.json; do
    id=$(basename "$f" .json)
    target="tests/parity/golden/$id.json"
    if [ ! -f "$target" ] || ! cmp -s "$f" "$target"; then
        cp "$f" "$target"
    fi
done
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity
```
Predicates are NOT widened to accommodate a fresh golden. A predicate
failing on a fresh golden is a Phase 3 work item (agent loops back via
verifier failure).

**Verification**:
```
ls tests/parity/golden/*.json | wc -l
for f in tests/parity/golden/*.json; do python3 scripts/parity/validate_schema.py "$f"; done
build/ci-test/og_test_parity
diff -r <(cd /tmp/golden_capture && md5sum *.json | sort) \
        <(cd tests/parity/golden && md5sum *.json | sort)
```

---

### Phase 6 — Mutation canary expansion and regression classification

**Phase Name**: Re-run the canary across every scenario; classify and
fix gameplay regressions.

**Implement Phase ID**: `06-canary-and-regressions`

**Verification Phases**:
- `06a-check-canary-every-row` (`check`, `bounce_target: 06-canary-and-regressions`):
  - `scripts/parity/run_mutation_canary.sh --all` exits 0.
  - Canary stdout lists ≥1 flip for every scenario, **with one exception
    list**: scenarios in `parity-canary-exemptions.md` may register 0
    flips, each citing the mechanical reason (e.g. "harness does not
    invoke `SaveData::load()`"). Verifier enforces exemption list size ≤
    published list and every other row flips.
- `06b-check-regressions-resolved` (`check`, `bounce_target: 06-canary-and-regressions`):
  - `python3 - <<'PY'` parses `.plan/parity-second-divergence-report.md`
    "Classified divergences" section, extracts every `regression` row.
    For each, asserts a corresponding row exists in
    `.plan/parity-second-fixes.md` keyed by `scenario_id` (column 1),
    asserts `parity result after fix` starts with `green` and `lock-in
    test` is non-empty. Zero regressions → empty fixes body
    (header-only) accepted.
  - `cmake --build --preset ci-test && ctest --preset ci-test --output-on-failure`
    — full ctest green; no regression fix may break an unrelated test.
- `06c-check-no-residual-regression` (`check`, `bounce_target: 06-canary-and-regressions`):
  - Re-evaluate every scenario's `expected_facts[]` on current branch
    dump and master golden. Both sides satisfy every predicate.
    Check fails if any predicate evaluates differently (unclassified
    diff).

**Preexisting Inputs**:
- `.plan/parity-second-divergence-report.md`
- `.plan/parity-honest-audit.md`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `tests/parity/parity_runner.cpp`, `tests/parity/scenario_runtime.cpp`
- `scripts/parity/run_mutation_canary.sh`, `scripts/parity/_apply_mutation.py`

**New Outputs**:
- `.plan/parity-second-fixes.md` — one row per Phase-5-classified regression.
  Columns: `scenario_id | root cause (file:line + suspected commit) |
  fix description | files modified | parity result after fix | lock-in test`.
  Each fix commit has prefix `parity-fix:`. Each lands a focused unit
  test under `tests/unit/parity_fixes/test_<scenario>_<short>.cpp`
  locking the behaviour in place.
- `.plan/parity-canary-exemptions.md` — explicit list of rows the canary
  cannot exercise mechanically, with rationale and follow-up ticket for
  each. Rationale cites specific source lines (e.g. "runner does not
  invoke `SaveData::load()`; loading happens at picker time via
  `og::scope::resources_io_init`"). Verifier asserts every row has
  `Why:` and `Future work:` lines.
- Updated `.plan/parity-second-divergence-report.md` — every `regression`
  row back-references its `parity-second-fixes.md` row.
- Optional: new unit tests under `tests/unit/parity_fixes/`.

**File Changes**:
- Source-code fixes for every Phase-5 regression. Each fix is a discrete
  `parity-fix:` commit (one per fix).
- New unit tests under `tests/unit/parity_fixes/`.
- Create `.plan/parity-second-fixes.md`, `.plan/parity-canary-exemptions.md`.
- Final commit: `parity-finish-2: phase 06 — mutation canary green; regressions classified`.

**Implementation Details**:
- Canary `--all` already iterates every row; agent confirms each
  `discriminating_mutation` reaches the runner (re-reading
  `.plan/parity-coverage-manifest.md` "Known limitations"). Exempt rows
  keep their mutations but are whitelisted in the canary script via a
  parsed `parity-canary-exemptions.md`.
- For each Phase-5 `regression` row:
  1. `git log origin/master..HEAD -- <suspected files>` lists candidates.
  2. Reproduce in focused unit test: spawn involved walkers, trigger
     path, assert master-side value (from golden) vs branch-side.
  3. Either land a `parity-fix:` commit bringing branch behaviour back
     to master, or reclassify as `intended_diff` with commit SHA cited
     in divergence-report.
  4. Re-run canary on touched row; confirm flip still works post-fix.

**Verification**:
```
scripts/parity/run_mutation_canary.sh --all
cmake --build --preset ci-test && ctest --preset ci-test --output-on-failure
grep -c '^| .* | regression' .plan/parity-second-fixes.md
grep -c 'parity-fix:' <(git log origin/master..HEAD --oneline)
test -f .plan/parity-canary-exemptions.md && grep -c '^| ' .plan/parity-canary-exemptions.md
```

---

### Phase 7 — Anti-cheating checks and CI wiring

**Phase Name**: Lock the harness against future widening / blob-cover /
silent recapture.

**Implement Phase ID**: `07-anti-cheating-locks`

**Verification Phases**:
- `07a-check-ci-runs-everything` (`check`, `bounce_target: 07-anti-cheating-locks`):
  - `.github/workflows/test.yml` (or CI YAML) is grep'd for invocations
    of `og_test_parity`, `lint_scenario_facts.py`,
    `run_mutation_canary.sh`, `behavioural_coverage_gate`. Missing
    invocation fails.
  - If no CI YAML exists, this verifier checks
    `scripts/parity/ci_parity.sh` (new Phase 7 script) runs the full
    bundle and exits 0.
- `07b-check-no-bypass-known-tricks` (`check`, `bounce_target: 07-anti-cheating-locks`):
  - Verifier creates throwaway `git worktree add /tmp/parity-bypass HEAD`,
    applies three concrete mutations one at a time via `sed -i` inline
    (no `.patch` file), runs the appropriate guard, asserts non-zero
    exit, restores the worktree with
    `git -C /tmp/parity-bypass checkout -- .` before the next bypass:
    - **Bypass 1 (widening lint)**:
      `sed -i -E 's/WalkerFamilyCount\(FAMILY_SOLDIER,\s*[0-9]+,\s*[0-9]+\)/WalkerFamilyCount(FAMILY_SOLDIER, 0, 99)/' tests/parity/scenario_table.h`
      on first occurrence. Then
      `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h`
      must exit non-zero with `unjustified_widening` on stderr.
    - **Bypass 2 (behavioural coverage)**: pick a weapon family the
      gate enforces (e.g. `FAMILY_KNIFE`), `sed -i` the corresponding
      `WeaponFamilyEmitted(FAMILY_KNIFE, ...)` predicate out of
      `tests/parity/scenario_table.h`. Build and run
      `build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage_gate_weapons'`;
      test must FAIL naming `FAMILY_KNIFE`.
    - **Bypass 3 (golden tampering)**: pick first existing golden,
      `printf 'X' > tests/parity/golden/<id>.json`. Then
      `python3 scripts/parity/validate_schema.py tests/parity/golden/<id>.json`
      must exit non-zero, AND
      `build/ci-test/og_test_parity --gtest_filter='Parity.<id>'` must FAIL.
  - All three guards must trigger; verifier exits non-zero if any
    bypass passes silently. Worktree removed with
    `git worktree remove --force /tmp/parity-bypass`.

**Preexisting Inputs**:
- `tests/parity/test_parity_coverage_gate.cpp`
- `scripts/parity/lint_scenario_facts.py`
- `scripts/parity/run_mutation_canary.sh`
- `scripts/parity/validate_schema.py`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `.github/workflows/*.yml` (if present)

**New Outputs**:
- `scripts/parity/ci_parity.sh` — single-shot driver:
  ```
  cmake --build --preset ci-test --target og_test_parity
  build/ci-test/og_test_parity
  python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h
  scripts/parity/run_mutation_canary.sh --all
  scripts/parity/capture_master_golden.sh --dry-run-compare-only
  ```
  Last command is the new `--dry-run-compare-only` mode of
  `capture_master_golden.sh` that recaptures every golden into
  `/tmp/recapture/` and asserts byte-equal vs committed; exit 1 on any
  diff.
- New mode `--dry-run-compare-only` in `capture_master_golden.sh`.
- Updated `.github/workflows/test.yml` (if present) — add `parity-strict`
  job running `scripts/parity/ci_parity.sh`. If no CI YAML, verifier
  accepts `ci_parity.sh` as CI integration surface and documents the
  invocation in `.plan/parity-second-divergence-report.md` "How to run
  in CI".

**File Changes**:
- Create `scripts/parity/ci_parity.sh` (executable).
- Modify `scripts/parity/capture_master_golden.sh` (new flag).
- Modify `.github/workflows/test.yml` (CI job) — if file exists.
- Commit: `parity-finish-2: phase 07 — anti-cheating gate + CI wiring`.

**Implementation Details**:
- Bypass-3 relies on `validate_schema.py` returning non-zero on
  malformed JSON (it does).
- Bypass-1 relies on Phase 3 lint rule (parse → diagnose → non-zero exit).
- Bypass-2 relies on Phase 4 behavioural gate.

**Verification**:
```
test -x scripts/parity/ci_parity.sh
scripts/parity/ci_parity.sh
git worktree add /tmp/parity-bypass HEAD
( cd /tmp/parity-bypass && \
    sed -i -E 's/WalkerFamilyCount\(FAMILY_SOLDIER,[^)]*\)/WalkerFamilyCount(FAMILY_SOLDIER, 0, 99)/' \
        tests/parity/scenario_table.h && \
    ! python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h )
git worktree remove --force /tmp/parity-bypass
```

---

### Phase 8 — Final honest sign-off

**Phase Name**: Write the final sign-off; close the loop on the user's goal.

**Implement Phase ID**: `08-final-signoff`

**Verification Phases**:
- `08a-check-signoff-content` (`check`, `bounce_target: 08-final-signoff`):
  - `test -f .plan/parity-signoff-honest.md`.
  - Required sections (verifier asserts each header exists and body is non-empty):
    - `## Final test surface` — test cases and pass/fail
    - `## Coverage outcome` — every required family / event / special
      backed by a behavioural predicate
    - `## Mutation canary outcome` — flip count per row
    - `## Classified divergences` — final per-row classification
    - `## Anti-cheating locks` — names of every check catching future regression
    - `## Open risks` — partial coverage carry-overs (e.g. on-disk save
      round-trip if still untested)
  - Signoff lists every Phase 1 → Phase 7 commit SHA range.
  - `git log -1 --name-status | grep parity-signoff-honest.md`.
- `08b-check-full-suite-green` (`check`, `bounce_target: 08-final-signoff`):
  - `cmake --build --preset ci-test && ctest --preset ci-test --output-on-failure` — exit 0.
  - `scripts/parity/ci_parity.sh` — exit 0.

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md`
- `.plan/parity-recapture-diff.md`
- `.plan/parity-second-divergence-report.md`
- `.plan/parity-second-fixes.md`
- `.plan/parity-canary-exemptions.md`
- `.plan/parity-coverage-manifest.md`
- `tests/parity/golden/*.json`
- `scripts/parity/ci_parity.sh`

**New Outputs**:
- `.plan/parity-signoff-honest.md` — final sign-off. Includes a one-line
  statement of the form *"Parity overall: GREEN. Every required family,
  event kind, weapon, treasure, FX, and special is exercised by at least
  one scenario whose `expected_facts[]` predicate constrains its
  behaviour; the mutation canary flips ≥1 predicate per non-exempt row;
  the recapture verifier confirms every golden was produced by companion
  SHA <pinned>."* All other claims cite specific verifier outputs from
  Phase 7's `ci_parity.sh`.

**File Changes**:
- Create `.plan/parity-signoff-honest.md`.
- Commit: `parity-finish-2: phase 08 — honest signoff`.

**Implementation Details**:
Agent runs full CI bundle once more and writes the document from actual
output. Signoff lists each test name as it appears in
`og_test_parity --gtest_list_tests` and exact pass counts.

**Verification**:
```
test -f .plan/parity-signoff-honest.md
grep -c '^## ' .plan/parity-signoff-honest.md >= 6
scripts/parity/ci_parity.sh
ctest --preset ci-test --output-on-failure
```

---

## 4. Critical Files

| File | Phase(s) | What changes |
|---|---|---|
| `.plan/goal.md` | none | read-only |
| `.plan/parity-risk-inventory.md` | none | read-only |
| `.plan/parity-harness-design.md` | 4 | append "Phase 04 redo: behavioural coverage" section |
| `.plan/parity-coverage-manifest.md` | 2, 4, 5 | reconcile master_companion_sha; flip `(none yet)` rows; add behavioural column; sign-off snapshot |
| `.plan/master-companion.md` | 2 | refresh SHA tables |
| `.plan/parity-honest-audit.md` | 1, 3 | new; appended in Phase 3 |
| `.plan/parity-recapture-diff.md` | 2 | new |
| `.plan/parity-second-divergence-report.md` | 5, 6 | new; back-references added in Phase 6 |
| `.plan/parity-second-fixes.md` | 6 | new |
| `.plan/parity-canary-exemptions.md` | 6 | new |
| `.plan/parity-signoff-honest.md` | 8 | new |
| `.plan/parity-divergence-report.md` | 1 | `git mv` → `parity-divergence-report-empty-world.md` |
| `.plan/parity-fixes.md` | 1 | `git mv` → `parity-fixes-empty-world.md` |
| `tests/parity/scenario_table.h` | 3, 4 | predicate tightenings, new scenarios |
| `tests/parity/fact_predicate.{h,cpp}` | 4 (optional) | no schema change desired |
| `tests/parity/state_dump.{h,cpp}` | 4 (optional) | no schema change desired |
| `tests/parity/parity_runner.cpp` | 4 | minor input/spawn extensions for treasure pickup |
| `tests/parity/scenario_runtime.cpp` | 4 | per-spawn `stats_level` / `magicpoints` already supported; no major change |
| `tests/parity/test_parity_scenarios.cpp` | 4 | new `OG_PARITY_TEST(...)` entries per new scenario |
| `tests/parity/test_parity_coverage_gate.cpp` | 4 | new behavioural gates |
| `tests/parity/golden/*.json` | 2 (capture), 5 (commit) | replaced in place where they diverged; new files for new scenarios |
| `tests/unit/parity_fixes/test_*.cpp` | 6 (optional, only if a regression needs locking in) | new |
| `scripts/parity/lint_scenario_facts.py` | 3 | new `unjustified_widening` rule |
| `scripts/parity/run_mutation_canary.sh` | 6 | read `parity-canary-exemptions.md` whitelist |
| `scripts/parity/capture_master_golden.sh` | 7 | new `--dry-run-compare-only` mode |
| `scripts/parity/ci_parity.sh` | 7 | new |
| `.github/workflows/test.yml` | 7 (if present) | new `parity-strict` job |
| `../openglad-master/tools/parity_scenario_table.h` | 2, 4 | byte-for-byte mirror updates |
| `../openglad-master/tools/parity_dump_master.cpp` | 2 (rebuild only), 4 (mirror change) | no logic change; rebuilt |
| `../openglad-master/build/ci-test/parity_dump_master` | 2, 4 | rebuilt |

## 5. Final Verification

After all phases land:

```bash
scripts/parity/ci_parity.sh
```

Runs (each must exit 0):

1. `cmake --build --preset ci-test --target og_test_parity` — builds.
2. `build/ci-test/og_test_parity` — every test green, including new
   behavioural coverage gates and existing structural gates.
3. `python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h`
   — no predicate widened without citation.
4. `scripts/parity/run_mutation_canary.sh --all` — every non-exempt row
   flips at least one predicate when `discriminating_mutation` is applied.
5. `scripts/parity/capture_master_golden.sh --dry-run-compare-only` —
   recapture matches every committed golden byte-for-byte.

Manual cross-checks:

- `cat .plan/parity-signoff-honest.md` shows every required family /
  event / special referenced by a covering scenario, and every
  divergence is either fixed (`parity-fix:` commit) or classified
  (`intended_diff` row with commit SHA).
- `sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` — equal.
- `git -C ../openglad-master rev-parse HEAD` matches pinned SHA in
  `.plan/parity-coverage-manifest.md` frontmatter and
  `.plan/parity-signoff-honest.md`.
- `grep -r 'parity-fix:' --oneline $(git log origin/master..HEAD --pretty=%H) | head`
  — every regression got a fix commit.

If any check fails, the bounce target is the implement phase that owned
the artifact.
