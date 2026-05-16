# Plan: Finish the gameplay-parity comparison and *actually* prove semantic equivalence

## 1. Context

### Where the previous workflow stopped, and why it is not done

The prior workflow built a real harness (the previous fraud — empty-world
goldens — has been torn down, the loader is wired, scripted input is routed
through `sim_process_player_input`, and 39 non-trivial goldens now exist
under `tests/parity/golden/`). 50/50 cases in `og_test_parity` pass, and the
coverage-gate cases (`Parity.coverage_gate*`) report green. **But the user's
goal — verifiable semantic equivalence between `wip/networking` and
`origin/master` for every entity type, special ability, attack type, and
emitted occurrence — is not satisfied.** Concrete evidence:

1. **The coverage gate is structural-only and is satisfied by spawn alone.**
   `tests/parity/test_parity_coverage_gate.cpp` ORs the per-scenario
   `CoverageObservation::{walker,weapon,treasure,generator,effect}_families`
   across `kScenarios`. The gate flips green as soon as a `SpawnSpec` of the
   matching `order/family` is added — even if the spawned entity never acts,
   never collides, never gets compared to master. `kFamilySpawns_golem_with_nonliving_targets`
   in `tests/parity/scenario_table.h:433-476` is precisely such a blob:
   one synthetic spawn per missing weapon / treasure / FX family, parked on
   team 2 at idle coordinates. That spawn satisfies the structural gate but
   proves nothing about semantic equivalence of the weapon's damage, the
   treasure's pickup effect, or the FX's lifetime.

2. **Specials coverage is a self-declared bit, not an observation.**
   `Exercises::Special_*` bits in `scenario_table.h` are set by the scenario
   author and are OR-ed across `kScenarios`. The gate `coverage_gate_specials`
   only checks that every bit was claimed by *some* scenario; it never
   verifies that the special was actually cast (the runner has no path that
   inspects `walker::current_special` post-tick) and it never compares the
   resulting effect/weapon/event between branch and master. Mage's
   `kFacts_family_mage_scen99` widens `WalkerFamilyCount(FAMILY_MAGE, 0, 3)`
   precisely to absorb the fact that branch summons mage images and master
   does not — the predicate is satisfied by *either* behaviour and so
   verifies neither.

3. **Predicates were repeatedly *widened* to absorb divergences instead of
   reconciling them.** Search `tests/parity/scenario_table.h` for the comments
   labelled `(a)` (e.g. `kFacts_family_mage_scen99` at line 693,
   `kFacts_family_slime_scen99` at line 732). The pattern is: the master
   golden disagreed with the branch dump → the predicate range was widened to
   cover both → the test went green → no `parity-fix:` commit, no
   `intended_diff` row, no `.plan/parity-fixes.md` entry. The user's
   instruction says explicitly: "Continue iterating until everything is fully
   tested, with copious checking in place to ensure agents don't cut corners."
   The current ranges are corner-cutting.

4. **`.plan/parity-divergence-report.md` and `.plan/parity-fixes.md` are
   stale empty-world artefacts.** Both were written under the original Phase
   06 / Phase 07 of the previous workflow, when goldens were 123-byte empty
   dumps. Both still declare "zero regressions". Neither matches the current
   39-golden tree or the widened predicate state.

5. **Master companion SHA drift.** `.plan/parity-coverage-manifest.md`
   frontmatter pins `master_companion_sha: c9f18a7b1eead675a6b09ded9134ead6e8de5950`,
   while `.plan/master-companion.md` claims `ce70d23286f1e8034284e7c718ec658065f525e5`.
   Two of the existing goldens (`family_*_scen99` rows) reference master commit
   identifiers from the master baseline; the rest were captured during phase
   01 of `parity-finish`. There is no SHA-1 record proving the current
   `tests/parity/scenario_table.h` and the companion's
   `tools/parity_scenario_table.h` are still byte-identical, nor that every
   golden was produced by the same companion build.

6. **Coverage breadth gap.** Many real gameplay-observable events are not
   exercised at all by the existing scenarios:
   - **Treasure pickup behaviour** (every `treasure_family_*.cpp`: gold,
     drumstick, potions, keys, life gem, teleporter, speed). They are
     spawned in the blob, but no scenario walks the player onto them.
   - **Generator spawns over time** (`FAMILY_TENT/TOWER/BONES/TREEHOUSE`
     emit walkers every N ticks; the existing scenarios spawn the generator
     but do not run long enough or compare the emitted walker stream).
   - **Per-family multi-special cycling** — `kInputsFamilySpecialCoverage`
     cycles `K_SPECIAL_SWITCH` and presses `K_SPECIAL` four times, but
     because `Exercises::Special_<family>_<n>` is a self-declared bit the
     scenario *claims* it covered slots 1..4 without proving them.
   - **Effect families that no scenario triggers**: `FAMILY_FLASH`,
     `FAMILY_MAGIC_SHIELD`, `FAMILY_KNIFE_BACK`, `FAMILY_BOOMERANG`,
     `FAMILY_CLOUD`, `FAMILY_MARKER`, `FAMILY_DOOR_OPEN`, `FAMILY_HIT`,
     `FAMILY_EXPAND`, `FAMILY_GHOST_SCARE` are only "covered" by the blob.
   - **Event kinds**: `notification`, `set_palette`, `request_redraw`,
     `end_game`, `set_end` are not naturally produced by the current
     scenarios; the blob and a few smoke arenas spuriously trigger them.
   - **Attack-type axis** (melee vs ranged vs special-projectile vs
     splash) is not enumerated — only `combat_attack_scen99` exists.

### What this plan must accomplish

The user said, verbatim:

> use gameplay parity comparison against master in a wide variety of scenarios,
> ensuring that cumulative coverage includes every single entity type,
> special ability effect, attack type, and occurrence in the game.
> Everything must be tested with no exceptions. Continue iterating until
> everything is fully tested, with copious checking in place to ensure
> agents don't cut corners. The reality of RNG differences will mean that
> things might not be byte-identical, but they should be checked for
> *verifiable certainty* that they are semantically equivalent.

Five obligations:

1. **Honest re-audit of the current state.** Inventory every passing test,
   every widened predicate, every "coverage-by-blob" entry, every stale
   golden, and every divergence the prior agent suppressed. This becomes
   `.plan/parity-honest-audit.md` and replaces the empty-world artefacts at
   `.plan/parity-divergence-report.md` and `.plan/parity-fixes.md`.

2. **Tighten the predicate surface so each scenario actually constrains
   behaviour.** Every `WalkerFamilyCount(F, mn, mx)` whose `mn != mx` must
   have a written justification anchored to a specific behavioural diff
   (and a paired `intended_diff` row or `parity-fix:` commit). The lint
   `scripts/parity/lint_scenario_facts.py` learns a new "no unjustified
   widening" rule.

3. **Replace structural coverage with behavioural coverage for every
   FAMILY_*.** For each weapon, treasure, FX, special, generator, and event
   kind, add (or upgrade) a scenario whose `expected_facts[]` *observes the
   entity's effect on the world*, not merely its presence:
   - weapons → projectile travels, damages, or expires;
   - treasures → pickup triggers `score_change` / stat delta / `treasure_collected` event;
   - FX → lifetime curve and source-walker family;
   - specials → resulting weapon/effect/event count post-cast;
   - generators → at least one emitted walker observed before tick budget;
   - event kinds → naturally emitted from gameplay, not spawn blob.

4. **Re-capture master goldens from a fresh companion build at a single
   pinned SHA, then diff every golden against the recapture.** No golden
   survives this phase unless the companion produced it byte-for-byte. The
   companion `tools/parity_scenario_table.h` must match the branch
   `tests/parity/scenario_table.h` SHA-1; the manifest's
   `master_companion_sha:` field is updated to the actual current companion
   commit.

5. **Anti-cheating infrastructure.** Every verifier phase re-derives its
   evidence (rebuild, re-run ctest, re-evaluate predicates from goldens,
   re-grep widened ranges). The mutation canary is widened so it has to
   flip a predicate for *every* row, not the original subset. A
   `Parity.no_unjustified_widening` test reads the source and fails if a
   widened predicate has no paired justification. A
   `Parity.behavioural_coverage_gate` test asserts every `FAMILY_*` and
   every `EventKind` is touched by a predicate (not just by a spawn).

### Codebase facts the plan relies on

- The runner at `tests/parity/parity_runner.cpp:60-144` is real: it
  constructs `LevelRuntimeData(level_id, /*headless=*/true,
  &sdl_level_data_hooks())`, calls `level.load()`, re-applies
  `world.rng_.state_ = spec.rng_seed`, optionally clears the world for
  `fresh_arena=true`, applies spawns via
  `scenario_runtime::apply_post_load_spawns`, and ticks `tick_budget`
  times, applying scripted input through `sim_process_player_input` in
  `scenario_runtime.cpp:111-173`. Do not rewrite this — extend it.
- Schema-v1 emitter at `tests/parity/state_dump.cpp` is unchanged; it
  records `effects[]`, `events[]`, `level_done`, `level_tick_count`,
  `rng_state`, `score_per_team[4]`, `tick`, `walkers[]`, and optional
  `inventory_keys`. Production hooks (`sdl_level_data_hooks()`) drive the
  load, so loaded `.fss` scenarios produce real walkers.
- `tests/parity/fact_predicate.{h,cpp}` defines `FactKind`,
  `FactPredicate`, `evaluate_facts`, and `parse_state_dump`. `FactKind`
  already covers the 16 predicate kinds (`TickReached`, `LevelDoneEquals`,
  `ScoreDelta`, `WalkerFamilyCount`, `WalkerOfTeamAlive`,
  `WalkerHpRangeAtFinalTick`, `WalkerKeysApplied`, `WalkerPositionMoved`,
  `WalkerDiedByFinal`, `WalkerAliveAtFinal`,
  `TreasureFamilyRemovedFromOblist`, `StatDeltaOnPickup`,
  `EffectFamilyCount`, `EventKindAtLeast`, `EventKindExactly`,
  `WeaponFamilyEmitted`). This plan adds **no new predicate kinds** and
  **no schema changes**: every behavioural axis needed below is
  expressible with the existing 16 kinds operating on the existing
  schema-v1 dump (`walkers[]` with `hp`/`max_hp`, `effects[]`,
  `events[]`, `weapons[]`, `score_per_team[]`, `inventory_keys`).
  `StatDeltaOnPickup` continues to return `indeterminate` for any stat
  not already covered by `hp`/`max_hp` — it is **never** the primary
  predicate for any new row; treasure-pickup rows in Phase 4 rely on
  `TreasureFamilyRemovedFromOblist` + `EventKindAtLeast` +
  `WalkerHpRangeAtFinalTick` + downstream emission predicates instead.
- Master companion lives at `/home/yans/code/openglad-master` on branch
  `parity-companion`, last committed at `ce70d23286f1e8034284e7c718ec658065f525e5`.
  Binary: `../openglad-master/build/ci-test/parity_dump_master`.
  Capture script: `scripts/parity/capture_master_golden.sh`. Schema
  validator: `scripts/parity/validate_schema.py`. Diff: `scripts/parity/diff_dumps.py`.
- `scripts/parity/lint_scenario_facts.py` already enforces the
  "non-empty fact requirements" and "non-default mutation" rules; it
  parses `tests/parity/scenario_table.h`. The new "no unjustified
  widening" rule lives here and reuses the existing parser.
- `scripts/parity/run_mutation_canary.sh` already iterates every row and
  applies its `discriminating_mutation`. Today some rows have
  `kMut_save_corrupt` etc. that the canary admits are no-ops; this plan
  requires every row to produce ≥1 predicate flip, with explicit doc for
  any row where the harness mechanically cannot exercise the subject.

### Inputs already on disk (consumed in place, not regenerated)

- `.plan/goal.md` — never rewritten.
- `.plan/parity-risk-inventory.md` — 12 subsystem checklist, kept as-is.
- `.plan/parity-harness-design.md` — schema-v1 contract; amended in place
  only where the predicate / coverage taxonomy actually grows.
- `.plan/parity-coverage-manifest.md` — long-form manifest, edited in
  place to flip `(none yet)` rows to the new covering scenario ids,
  reconcile the `master_companion_sha` field, and add a new column
  documenting *behavioural* observation (not just structural).
- `.plan/parity-redo-audit.md`, `.plan/parity-signoff-fraudulent.md`,
  `.plan/master-baseline.md`, `.plan/master-companion.md` — kept as
  historical record.
- `tests/parity/*.{h,cpp}` and `tests/parity/golden/*.json` — extended in
  place; goldens may be replaced 1:1 by recapture against the pinned
  companion but never deleted en masse.
- `tests/parity/scenario_table.h` and the master mirror
  `../openglad-master/tools/parity_scenario_table.h` — extended; the
  byte-for-byte synchronisation contract holds.
- `scripts/parity/*.{sh,py}` — extended (new lint rules, new behavioural
  coverage check). No script renamed or deleted.
- `../openglad-master/tools/parity_dump_state.{h,cpp}`,
  `tools/parity_dump_master.cpp`, `tools/parity_dump_master_stubs.cpp`,
  `tools/parity_bootstrap.{h,cpp}` — extended if and only if the schema
  needs a new emitted key (it should not, for this plan).

### What does *not* change

- The schema-v1 JSON shape and the byte-for-byte branch/master table
  sync. No schema-v1.1.
- The `../openglad-master` worktree path; no rebase, no force-push.
- Test code location (`tests/parity/`) and `og_test_parity` CMake
  registration (`CMakeLists.txt:1807`).
- `.plan/parity-risk-inventory.md` (read-only history).

### Stale artefacts to be archived (not silently overwritten)

- `.plan/parity-divergence-report.md` is renamed to
  `.plan/parity-divergence-report-empty-world.md` via `git mv` in Phase 1,
  preserving history while making clear it described the empty-world era.
- `.plan/parity-fixes.md` is renamed to
  `.plan/parity-fixes-empty-world.md` via `git mv` in Phase 1. New
  divergence and fixes documents are written under fresh names
  (`parity-honest-audit.md`, `parity-second-divergence-report.md`,
  `parity-second-fixes.md`).

## 2. Generated Workflow Contract

The generated `workflow.yaml` produced by the planner from this plan must
satisfy every rule below. The phase files under `.plan/phases/*.md` and
the structure file `.plan/workflow-structure.yaml` must be self-consistent
with this contract; the generator is not free to invent topology.

1. **Linear execution only.** `linear: true`. No `parallel_groups`, no
   fan-out, no fan-in. Phases run in numeric order from 1 to N.
2. **Inline-only YAML.** `yaml_source_mode: inline-only`. No top-level
   `include:`, no phase-level `prompt_file:`, `workflow_file:`,
   `workflow_dir:`, `checks:`, or any other YAML-source indirection. Each
   phase's `prompt:` is the complete agent instructions as a multiline
   string.
3. **No agent-guided bounce.** Each check phase declares at most one
   `bounce_target`, a fixed string equal to the implement phase's id. No
   `bounce_targets:` list; no choose-between-these logic.
4. **Every verifier is a top-level `check` phase.** Pattern:
   ```
   N    implement (id: ##-name)             bounce_target: null
   N+1  check     (id: ##a-check-name)      bounce_target: ##-name
   N+2  check     (id: ##b-check-name)      bounce_target: ##-name
   ```
   A single implement phase may be followed by multiple check phases; each
   check carries `bounce_target: ##-name` pointing at the same implement.
5. **A verifier stays in its block.** A check phase never bounces anywhere
   but the immediately preceding implement phase in the same numeric block.
6. **Checks run commands, not reads.** All shell commands (`cmake --build`,
   `ctest`, `scripts/parity/diff_dumps.py`, `scripts/parity/validate_schema.py`,
   `scripts/parity/lint_scenario_facts.py`, `scripts/parity/run_mutation_canary.sh`,
   `git log`, `grep -nE`, `sha1sum`, `python3 -c`) are written into the
   checker's `prompt:` literally, with the expected exit code and failure
   trigger spelled out. Verifiers are agent phases that run shell commands
   and decide pass/fail; verifiers are never modelled as non-agentic phases.
7. **Existing artefacts are reused, not regenerated.** Each implement phase
   names its `Preexisting Inputs` (this plan does so explicitly per phase).
   The agent's prompt instructs it to *read or update* those files in
   place. In particular:
   - `.plan/parity-risk-inventory.md` is not re-derived.
   - `.plan/parity-harness-design.md` is amended in place; the schema-v1
     contract is unchanged.
   - `.plan/parity-coverage-manifest.md` is amended in place; existing
     `(none yet)` rows that already have a real covering scenario are
     filled in by reading the runner output, not by rewriting the manifest
     from scratch.
   - Master companion source files (`../openglad-master/tools/*`) are
     extended in place. The companion is rebuilt, not re-cloned or
     rebased.
   - The 39 existing goldens are kept; Phase 5 recaptures every one
     against the pinned companion and the verifier diffs the recapture
     against the on-disk golden — surviving goldens are byte-equal to the
     recapture, divergent ones are replaced one by one with a per-golden
     `parity-recapture:` commit citing the master companion SHA.
8. **Commit-before-yield.** Every implement phase's prompt contains a
   literal instruction to `git add` the modified files and `git commit -m
   "..."` *before* yielding. The following check phase expects HEAD to
   contain the change; the check runs `git log -1 --name-status` and
   asserts the expected files are listed. **Two-worktree phases** (Phase
   2 and Phase 4, which modify both the branch and
   `../openglad-master/`): the prompt MUST instruct the agent to commit
   on both worktrees independently (`git -C ../openglad-master add … &&
   git -C ../openglad-master commit -m "parity-companion: …"` in
   addition to the branch-side commit), and the matching check verifies
   both HEADs with `git -C ../openglad-master log -1 --name-status`.
9. **Fraud-resistant check semantics.** Every check phase asserts that the
   *content* of a produced artefact is non-trivial, not merely that it
   exists. Examples:
   - Audit doc must contain a literal count of widened predicates *and*
     enumerate them by `(scenario_id, FactKind, arg range)`.
   - A "predicate strengthened" assertion must compare the post-tighten
     range to the pre-tighten range, not the post-tighten range to itself.
   - Mutation canary must report ≥1 flip per row, with a documented
     exception list whose size is bounded and enumerated in the verifier.
   - Coverage gates assert structural *and* behavioural reachability; the
     behavioural gate fails if a `FAMILY_*` is only present via the blob
     spawn list with no scenario-specific predicate referencing that
     family id in `arg0`.
10. **No new YAML source files outside `workflow.yaml`.** The generated
    workflow is one file. Auxiliary data (coverage manifests, scenario
    tables) lives in the project tree as normal source artefacts, not
    separate YAML includes.
11. **Every implement-phase prompt commits to git before yielding.** This
    is rule #8 stated again for emphasis: it is the load-bearing
    invariant that allows check phases to operate on HEAD.

## 3. Implementation Phases

Eight implement phases, each paired with the minimum number of fixed
check phases. Verifier counts per implement phase:
`2, 3, 3, 3, 3, 3, 2, 2`. Total: `8 implement + 21 check = 29 phases`.

---

### Phase 1 — Honest audit and stale-artefact rename

**Phase Name**: Honest audit; rename empty-world reports.

**Implement Phase ID**: `01-honest-audit`

**Verification Phases**:
- `01a-check-audit-content` (`check`, `bounce_target: 01-honest-audit`)
  — runs:
  - `test -f .plan/parity-honest-audit.md` (must exist)
  - `test -f .plan/parity-divergence-report-empty-world.md` (must exist)
  - `test -f .plan/parity-fixes-empty-world.md` (must exist)
  - `test ! -f .plan/parity-divergence-report.md` (must NOT exist)
  - `test ! -f .plan/parity-fixes.md` (must NOT exist)
  - `python3 - <<'PY'` — counts widened predicates the audit must list
    (every `WalkerFamilyCount(..., mn, mx)` where `mn != mx`, every
    `WalkerOfTeamAlive(..., mn, mx)` where `mn != mx`, every
    `WalkerHpRangeAtFinalTick(..., mn, mx)` where `mx - mn > 200`) and
    prints `WIDENED_COUNT=<N>`. The audit document MUST contain a
    literal single line matching `^Widened predicates: <N>$` whose `<N>`
    is byte-equal to the python result. Verifier extracts both via
    `grep -E '^Widened predicates: '` and `grep -E '^WIDENED_COUNT='`
    and `diff`s the two integers.
  - `grep -c '^| ' .plan/parity-honest-audit.md` ≥ 25 (rough row count).
  - `git log -1 --name-status` lists the rename and the new audit.
- `01b-check-history-preserved` (`check`, `bounce_target: 01-honest-audit`)
  — runs:
  - `git log --follow --oneline .plan/parity-divergence-report-empty-world.md
    | wc -l` returns a value ≥ 2 (at least the rename commit plus one
    pre-rename commit), proving the rename was `git mv` and history is
    visible across the rename.
  - Same assertion for `parity-fixes-empty-world.md`.
  - `git log -1 --diff-filter=R --name-status HEAD` lists both rename
    pairs (`R<score>  parity-divergence-report.md  parity-divergence-
    report-empty-world.md` and the analogous `parity-fixes.md` rename);
    verifier greps for both `R[0-9]+\s+\.plan/parity-divergence-report\.md`
    and `R[0-9]+\s+\.plan/parity-fixes\.md` in HEAD's name-status.

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
- `.plan/parity-honest-audit.md` — replaces the empty-world reports as the
  authoritative present-day audit. Required sections:
  (a) **Current test surface**: enumerate every `Parity.*` test name and
      its current pass/fail (capture via
      `build/ci-test/og_test_parity --gtest_list_tests`). Note 50 pass /
      0 fail today.
  (b) **Widened-predicate inventory**: for every scenario in
      `kScenarios`, list each `FactPredicate` where the predicate's
      `(min, max)` range exceeds an "exact value" semantic (i.e. `mn != mx`
      for the count predicates, `max - min > 200` for HP). Cite line
      numbers in `scenario_table.h`. Tag each row with
      `widening_justification: present | absent`. Predicates with the
      `(a)` inline comment ("Branch... master..." pattern) count as
      "present"; predicates with no in-line comment count as "absent" and
      are work items for Phase 3.
  (c) **Structural-only coverage entries**: every
      `(FAMILY_*, order)` pair that is only reachable via a spawn in
      `kFamilySpawns_golem_with_nonliving_targets` (i.e. not referenced
      by any `expected_facts[]` predicate's `arg0`). Cite the manifest
      row in `.plan/parity-coverage-manifest.md`.
  (d) **Master-companion SHA reconciliation**: list both SHAs
      currently in the docs (`c9f18a7b...` in manifest frontmatter,
      `ce70d2328...` in `.plan/master-companion.md`), name the
      reconciliation target (the *current* `parity-companion` HEAD on
      `../openglad-master/`, captured via
      `git -C ../openglad-master rev-parse HEAD`), and state explicitly
      that Phase 5 re-captures every golden from that SHA.
  (e) **Stale-document rename log**: list the two renamed files and the
      reason. State explicitly that the prior divergence-report and
      fixes were for the empty-world era and are renamed (not deleted)
      so history is preserved.
  (f) **Coverage-gap inventory by axis**:
      - Walker families with no behavioural predicate touching them
        beyond `WalkerFamilyCount(family, 0, 0)` (dead-final) — that is
        a count assertion, not a behavioural one. Per family list the
        first missing behavioural axis (HP, position, event emission,
        damage dealt).
      - Weapon families: every entry in `kRequiredWeaponFamilies` with
        no `WeaponFamilyEmitted(arg0=family)` predicate in any row's
        `expected_facts[]`.
      - Treasure families: every entry in `kRequiredTreasureFamilies`
        with no `TreasureFamilyRemovedFromOblist` or `StatDeltaOnPickup`
        predicate.
      - Effect families: every entry in `kRequiredEffectFamilies` with
        no `EffectFamilyCount(arg0=family)` predicate.
      - Specials: every `(family, idx)` pair in `kRequiredSpecials` not
        appearing in any row's `discriminating_mutation` rationale or
        `expected_facts` and only claimed by an `Exercises::Special_*`
        bit.
      - Event kinds: every kind in `kRequiredEventKinds` not appearing
        in any `EventKindAtLeast` / `EventKindExactly` predicate.
  (g) **Mutation-canary delta**: list every row whose
      `discriminating_mutation` documentation admits "the parity runner
      does not invoke the subject" (today `save_roundtrip_scen99` and
      `rng_seed_stable_scen99`, per
      `.plan/parity-coverage-manifest.md` "Known limitations").
- `.plan/parity-divergence-report-empty-world.md` — renamed in place via
  `git mv .plan/parity-divergence-report.md ...`.
- `.plan/parity-fixes-empty-world.md` — renamed in place via
  `git mv .plan/parity-fixes.md ...`.

**File Changes**:
- `git mv .plan/parity-divergence-report.md .plan/parity-divergence-report-empty-world.md`
- `git mv .plan/parity-fixes.md .plan/parity-fixes-empty-world.md`
- Create `.plan/parity-honest-audit.md`.
- Commit message: `parity-finish-2: phase 01 — honest audit; rename
  empty-world reports`.

**Implementation Details**:
The agent runs the live commands to populate the audit. No source code is
modified.
```bash
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_list_tests > /tmp/parity_tests.txt
build/ci-test/og_test_parity --gtest_brief=1 > /tmp/parity_run.txt
python3 - <<'PY' > /tmp/widened.txt   # the script the verifier will rerun
import re, pathlib
text = pathlib.Path('tests/parity/scenario_table.h').read_text()
# Enumerate every WalkerFamilyCount, WalkerOfTeamAlive, WalkerHpRangeAtFinalTick
# and report cases where mn != mx (or hp range > 200 cents).
# Print line:scenario:predicate for each.
PY
git -C ../openglad-master rev-parse HEAD > /tmp/companion_sha.txt
sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h
```
Each section in the audit cites the actual stdout of these commands.

**Verification**:
```
test -f .plan/parity-honest-audit.md
test -f .plan/parity-divergence-report-empty-world.md
test -f .plan/parity-fixes-empty-world.md
test ! -f .plan/parity-divergence-report.md
test ! -f .plan/parity-fixes.md
git log --follow --oneline .plan/parity-divergence-report-empty-world.md | tail -1   # original commit visible
git log -1 --name-status | grep -c 'parity-honest-audit.md'   # the new file is in HEAD
```

---

### Phase 2 — Master companion re-validation and SHA pinning

**Phase Name**: Rebuild master companion at a fresh SHA; pin it in docs;
recapture-vs-existing diff across all 39 goldens.

**Implement Phase ID**: `02-companion-revalidation`

**Verification Phases**:
- `02a-check-companion-build` (`check`, `bounce_target: 02-companion-revalidation`):
  - `cd ../openglad-master && cmake --build --preset ci-test --target parity_dump_master`
    must exit 0.
  - `test -x ../openglad-master/build/ci-test/parity_dump_master`.
  - `sha1sum tests/parity/scenario_table.h
    ../openglad-master/tools/parity_scenario_table.h` — both SHA-1s must
    match. If they differ, the check fails. The branch table and the
    companion table are synchronised by the Phase 2 implement step.
- `02b-check-companion-list-matches` (`check`, `bounce_target: 02-companion-revalidation`):
  - `../openglad-master/build/ci-test/parity_dump_master --list >
    /tmp/cmaster_ids.txt`; compare to
    `python3 - <<'PY'` extracting every `kScenarios[].id` where
    `is_branch_internal == false`. Sets must be equal (ordering free).
- `02c-check-recapture-diff-log` (`check`, `bounce_target: 02-companion-revalidation`):
  - `test -f .plan/parity-recapture-diff.md` and grep for the section
    `## Per-golden recapture diff` whose row count equals
    `kMasterComparableScenarioCount` (currently 38 — 39 goldens minus the
    branch-internal one).
  - `grep -c '^| ' .plan/parity-recapture-diff.md` ≥ 38.
  - `grep -E '^\| [a-z_0-9]+_scen[0-9]+ +\| (byte-equal|diff)' .plan/parity-recapture-diff.md | wc -l`
    matches the row count.
  - The Phase 2 commit message contains the literal current companion
    SHA (`git log -1 --pretty=%B` must match
    `git -C ../openglad-master rev-parse HEAD`).

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md` (Phase 1 output)
- `.plan/parity-coverage-manifest.md` (frontmatter SHA to be reconciled)
- `.plan/master-companion.md` (SHA reference to be reconciled)
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
- `.plan/parity-recapture-diff.md` — per-golden recapture result. Required
  sections:
  - **Header**: pinned companion SHA (the *current* HEAD of
    `parity-companion`), branch HEAD SHA, branch-side
    `tests/parity/scenario_table.h` SHA-1, companion-side
    `tools/parity_scenario_table.h` SHA-1. The two table SHA-1s must be
    equal.
  - **Per-golden recapture diff** table with one row per
    master-comparable scenario (38 rows). Columns:
    `scenario_id | bytes_before | bytes_after | result (byte-equal/diff) |
    notes`. `result == diff` rows must include a one-line summary of
    which fields changed (RNG state, events count, walker count); these
    rows become per-golden replacements in Phase 5 and are tracked by
    Phase 6's regression report.
  - **Outcome summary**: count of byte-equal vs diff vs schema-invalid
    rows.
- Updated `.plan/parity-coverage-manifest.md` frontmatter
  `master_companion_sha:` field — set to the actual companion HEAD.
- Updated `.plan/master-companion.md` — refresh SHA tables and the
  "Drift-detection SHA-1s" section to the current values.
- If `tests/parity/scenario_table.h` SHA differs from the companion
  mirror: copy branch → master and commit on master, then re-run; this is
  the only `tests/parity/` write Phase 2 may perform and it is purely a
  mirror sync.

**File Changes**:
- Modify `.plan/parity-coverage-manifest.md` (frontmatter only).
- Modify `.plan/master-companion.md` (SHA tables and the literal SHA-1
  rows in the "Drift-detection" table).
- Create `.plan/parity-recapture-diff.md`.
- If needed, `cp tests/parity/scenario_table.h
  ../openglad-master/tools/parity_scenario_table.h` and commit on
  `../openglad-master` (`parity-companion` branch).
- Commit message on branch: `parity-finish-2: phase 02 — companion
  revalidation; pinned SHA <hash>; <N> goldens diverge from recapture`.
- Commit message on master worktree (if any): `parity-companion: phase
  02 — mirror scenario_table.h SHA-1 from branch <hash>`.

**Implementation Details**:
The agent runs:
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
# Compare every recapture to the on-disk golden.
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
branch/companion sides MUST match before recapture starts; if they
don't, the agent first mirrors the branch table to the companion.

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

**Phase Name**: Strengthen the predicate surface; lint refuses unjustified
range widening.

**Implement Phase ID**: `03-tighten-predicates`

**Verification Phases**:
- `03a-check-lint-passes` (`check`, `bounce_target: 03-tighten-predicates`):
  - `python3 scripts/parity/lint_scenario_facts.py
    tests/parity/scenario_table.h` exits 0 with no diagnostics.
  - The lint output, when given a tampered table where one
    `WalkerFamilyCount` range was artificially widened, must exit
    non-zero with a `unjustified_widening` diagnostic. The check
    constructs the tampered table by `sed -E
    's/WalkerFamilyCount\(([^,]+),\s*([0-9]+),\s*\2/WalkerFamilyCount(\1,
    \2, 99/'` over a `/tmp/tampered.h` copy, runs
    `LINT_SCENARIO_TABLE=/tmp/tampered.h python3
    scripts/parity/lint_scenario_facts.py`, and asserts exit code != 0
    with the diagnostic string present. Restores nothing (only the
    tmp file was touched).
- `03b-check-tests-still-green` (`check`, `bounce_target: 03-tighten-predicates`):
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity` — every `Parity.*` case still passes.
    Predicate tightening must not produce new failures because the
    tightening is paired with golden recapture (Phase 5 runs later;
    Phase 3 may rely on the pre-Phase-2-diff goldens here, accepting
    that Phase 5 may force another tightening round if recapture
    replaces a golden).
- `03c-check-widening-justified` (`check`, `bounce_target: 03-tighten-predicates`):
  - `python3 - <<'PY'` scans `scenario_table.h` for every widened
    predicate; for each, asserts either (i) the line is followed (within
    3 lines) by an inline comment matching `// .*(branch|master|widen|
    intended_diff|parity-fix)` OR (ii) the row's `discriminating_mutation`
    rationale references the same FactKind. Failure prints the offending
    `(scenario_id, line, predicate)` and exits non-zero.
  - The new lint rule "no unjustified widening" lives in
    `scripts/parity/lint_scenario_facts.py`; the verifier re-invokes the
    lint to demonstrate the rule is wired (not just the inline check).

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md` (Phase 1; the widened-predicate
  inventory drives the work list)
- `.plan/parity-recapture-diff.md` (Phase 2; if a golden has been
  replaced, the predicate must match the new golden)
- `tests/parity/scenario_table.h`
- `tests/parity/fact_predicate.{h,cpp}`
- `scripts/parity/lint_scenario_facts.py`

**New Outputs**:
- Updated `tests/parity/scenario_table.h`:
  - Every `WalkerFamilyCount(family, mn, mx)` predicate with `mn != mx`
    is either:
    (a) narrowed to `(mx, mx)` or `(mn, mn)` if the recapture confirms
        the master value is stable;
    (b) replaced by an `EffectFamilyCount`, `WalkerDiedByFinal`, or
        `WeaponFamilyEmitted` predicate that captures the actual
        behavioural diff with an exact count; or
    (c) accompanied by an inline `// intended_diff: <reason>; cited
        commit <sha>` comment that the new lint rule recognises, with
        a corresponding entry in `.plan/parity-honest-audit.md`'s
        "Reclassified rows" section.
  - Same treatment for `WalkerOfTeamAlive(team, mn, mx)` widened ranges.
  - `WalkerHpRangeAtFinalTick` ranges wider than 200 cents must either
    narrow to ≤200 cents OR cite `// rng_drift: <reason>` and link to a
    new `intended_diff` row.
- Updated `scripts/parity/lint_scenario_facts.py` with a new
  `unjustified_widening` rule. The rule's parser walks the
  `kFacts_<id>[]` array, identifies widened predicates, and requires
  the per-predicate justification described above.
- Updated `.plan/parity-honest-audit.md` (in place) — append a
  "Reclassified rows" subsection listing every row whose predicate was
  narrowed, every row that stays widened with a citation, and every row
  that was deleted.
- The parser for inline `intended_diff` / `rng_drift` comments lives
  inside `scripts/parity/lint_scenario_facts.py` (no separate file).
  The new rule reuses the existing C++ table parser in that script.

**File Changes**:
- Modify `tests/parity/scenario_table.h` (predicate tightenings).
- Modify `scripts/parity/lint_scenario_facts.py` (new rule).
- Modify `.plan/parity-honest-audit.md` (append section).
- Commit message: `parity-finish-2: phase 03 — tighten predicates and
  add no-unjustified-widening lint`.

**Implementation Details**:
- The agent re-runs `build/ci-test/og_test_parity` after each tightening
  to make sure no row regresses to failure. Tightenings that cause a row
  to fail are reverted and the row is moved to the `intended_diff`
  citation path with a doc entry in the audit.
- The lint rule grammar (formal): an `intended_diff` citation is an
  inline C++ comment matching
  `// intended_diff: .{20,}; commit [0-9a-f]{7,40}` placed immediately
  after the predicate in the `kFacts_<id>[]` array. An `rng_drift`
  citation has the same shape with the leading keyword `rng_drift`.
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
grep -c '^| family_' .plan/parity-honest-audit.md   # at least one row reclassified per family adjusted
```

---

### Phase 4 — Behavioural coverage scenarios (weapons, treasures, FX, generators, events)

**Phase Name**: Replace blob-spawn coverage with per-entity behavioural
scenarios.

**Implement Phase ID**: `04-behavioural-coverage`

**Verification Phases**:
- `04a-check-behavioural-gate` (`check`, `bounce_target: 04-behavioural-coverage`):
  - Compile and run a new test
    `Parity.behavioural_coverage_gate_weapons`,
    `Parity.behavioural_coverage_gate_treasures`,
    `Parity.behavioural_coverage_gate_effects`,
    `Parity.behavioural_coverage_gate_generators`,
    `Parity.behavioural_coverage_gate_event_kinds`,
    `Parity.behavioural_coverage_gate` (umbrella). Each must pass —
    failure shows every entity that is reached only by spawn (no
    predicate references it).
- `04b-check-no-blob-scenario-needed` (`check`, `bounce_target: 04-behavioural-coverage`):
  - `python3 - <<'PY'` reads `scenario_table.h` and asserts that
    removing the `kFamilySpawns_golem_with_nonliving_targets`
    entry from `kScenarios` (simulated by name-grepping the row out of
    the file) still leaves every required family covered by another
    scenario's `expected_facts[]`. The check reports any
    family that would lose coverage; failure exits non-zero.
- `04c-check-gtests-pass` (`check`, `bounce_target: 04-behavioural-coverage`):
  - `cmake --build --preset ci-test --target og_test_parity` exits 0.
  - `build/ci-test/og_test_parity --gtest_brief=1` — no failures across
    the now-extended scenario set.
  - `build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'`
    — all seven structural gates pass.

**Preexisting Inputs**:
- `.plan/parity-honest-audit.md` (coverage-gap inventory in §(f))
- `.plan/parity-coverage-manifest.md`
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_table.h`
- `tests/parity/parity_runner.cpp` (extended for treasure pickup script)
- `tests/parity/scenario_runtime.cpp` (extended for input combinations)
- `tests/parity/fact_predicate.{h,cpp}` (no new predicate kinds in this
  phase; the existing 16 cover every behavioural axis required here)
- `tests/parity/test_parity_coverage_gate.cpp` (extended; new cases added)
- `tests/parity/test_parity_scenarios.cpp` (one new `OG_PARITY_TEST(id)`
  line per new scenario id)
- `tests/parity/state_dump.{h,cpp}` (schema-v1 unchanged — read-only here)
- `tests/parity/golden/*.json` (existing 39 untouched in this phase;
  Phase 5 captures the new ones)
- `../openglad-master/tools/parity_scenario_table.h` (mirror, kept in
  sync)
- `scripts/parity/lint_scenario_facts.py` (extended with one more rule
  if predicate widening reappears in new rows)

**New Outputs** — concrete new scenarios with binding-predicate facts:

- **Treasure-pickup scenarios** (one per treasure family except
  `FAMILY_EXIT` which is already exercised, `FAMILY_STAIN` which is a
  passive blood splash):
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
    `treasure_stain_observation_scen99` (passive — no pickup, the
    soldier walks over a STAIN spawn and asserts the treasure remains
    in oblist with stable position).
  - Spawn pattern: lone soldier on team 0 at `(96, 120)`; one
    treasure of the target family at `(160, 120)` via
    `kOrderTreasure`. Script `K_RIGHT` for ticks 1..20 so the
    soldier walks east through the treasure tile.
  - Predicates (per row, all required):
    - `TickReached(150)`
    - `WalkerPositionMoved(FAMILY_SOLDIER, ≥160, 120)` — soldier
      reached the treasure tile
    - `TreasureFamilyRemovedFromOblist(FAMILY_<TREASURE>)` —
      treasure consumed (for everything except STAIN)
    - `EventKindAtLeast(score_change, 1)` for value-bearing
      treasures (gold, silver, gem)
    - `EventKindAtLeast(play_sound, 2)` for any audible pickup
    - For HP-bearing treasures (`FAMILY_DRUMSTICK`, `FAMILY_GEM`,
      `FAMILY_LIFE_GEM`): `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER,
      mn, mx)` with master-pinned exact bounds confirms the heal.
    - For stat-bearing treasures whose effect is not observable in
      schema-v1 (`FAMILY_MAGIC_POTION` raises magicpoints,
      `FAMILY_SPEED_POTION` raises speed, `FAMILY_INVIS_POTION` /
      `FAMILY_INVULNERABLE_POTION` / `FAMILY_FLIGHT_POTION` set
      timed flags): the row's primary predicate is a **downstream
      emission** check — after pickup, the soldier casts a special
      or attacks, and the predicate asserts the resulting
      `WeaponFamilyEmitted` / `EffectFamilyCount` / movement that is
      only possible because the pickup happened. The
      `StatDeltaOnPickup` predicate is included for documentation
      purposes only and is allowed to evaluate `indeterminate`; it is
      never the sole gating predicate for a row.
    - For key pickup (`FAMILY_KEY`): `WalkerKeysApplied(FAMILY_SOLDIER,
      mask)` confirms the bit was set on the soldier.
    - For teleporter (`FAMILY_TELEPORTER`): `WalkerPositionMoved`
      with an exact post-warp coordinate matches master.
    - A `discriminating_mutation` whose subject is the treasure's
      pickup hook in `src/gameplay/families/treasure_family_*.cpp`.
      The mutation neuters the pickup (drops the effect or zeros the
      delta) and the canary asserts ≥1 predicate flips post-mutation.
- **Weapon-emission scenarios** — for each weapon family not naturally
  emitted by the existing arenas (verify against the audit), add an
  arena with a wielder + sparring partner + `kInputsCombatAttack99`:
  - `weapon_knife_emission_scen99` (SOLDIER w/ knife),
    `weapon_arrow_emission_scen99` (ARCHER default),
    `weapon_fireball_emission_scen99` (MAGE special 1),
    `weapon_tree_emission_scen99` (DRUID special 1 — GROW TREE),
    `weapon_meteor_emission_scen99`,
    `weapon_sprinkle_emission_scen99`,
    `weapon_bone_emission_scen99`, `weapon_blood_emission_scen99`
    (passive, blood-splash sprite),
    `weapon_blob_emission_scen99`,
    `weapon_fire_arrow_emission_scen99` (Archer special 1),
    `weapon_lightning_emission_scen99` (chain via Archmage),
    `weapon_glow_emission_scen99`,
    `weapon_wave_emission_scen99` (Mage special 4),
    `weapon_wave2_emission_scen99`, `weapon_wave3_emission_scen99`
    (cascading wave),
    `weapon_circle_protection_emission_scen99` (DRUID special 4 —
    PROTECTION),
    `weapon_hammer_emission_scen99`,
    `weapon_door_emission_scen99` (door object is an Order::Weapon),
    `weapon_boulder_emission_scen99` (BARBARIAN special 1).
  - Spawn: wielder on team 0 at `(120, 120)`, target on team 1 at
    `(180, 120)`. Use `set_default_weapon` / `set_current_weapon` in
    the `SpawnSpec` to force the wielder onto the target weapon
    family when the family's default does not match. ROCK is
    naturally emitted by ELF; if missing today, an explicit row
    `weapon_rock_emission_scen99` lands too.
  - Predicates (each row):
    - `TickReached(150)`
    - `WeaponFamilyEmitted(FAMILY_<WEAPON>)` — primary assertion;
      the predicate already searches `dump.weapons[]` only.
    - `EffectFamilyCount(FAMILY_HIT, ≥1, ≤8, source=FAMILY_<wielder>)`
      where the weapon's collision produces a HIT effect.
    - A discriminating mutation pointing at the weapon family's
      `act()` body that suppresses emission or zeros damage.
- **Effect-family scenarios** — one per missing FX family:
  - `effect_expand_emission_scen99`, `effect_ghost_scare_emission_scen99`,
    `effect_explosion_emission_scen99` (use the existing bomb spec
    extended), `effect_flash_emission_scen99`,
    `effect_magic_shield_emission_scen99`,
    `effect_knife_back_emission_scen99`,
    `effect_boomerang_emission_scen99` (Soldier special 2),
    `effect_cloud_emission_scen99` (Thief special 4 — POISON CLOUD),
    `effect_marker_emission_scen99`, `effect_door_open_emission_scen99`
    (player walks onto a door tile with a key), `effect_hit_emission_scen99`.
  - Predicates:
    - `TickReached(<budget>)`
    - `EffectFamilyCount(FAMILY_<EFFECT>, mn, mx, source=FAMILY_<source>)`
      with `mn == mx` (exact count from master golden after Phase 5).
- **Generator scenarios** — for each of TENT/TOWER/BONES/TREEHOUSE, run
  a scenario long enough that the generator emits ≥1 walker:
  - `generator_tent_emission_scen99`,
    `generator_tower_emission_scen99`,
    `generator_bones_emission_scen99`,
    `generator_treehouse_emission_scen99`.
  - Spawn: just the generator on team 1 at `(120, 120)`. Tick budget
    = 300 (generators are slow). `fresh_arena = true`.
  - Predicates:
    - `TickReached(300)`
    - `WalkerFamilyCount(FAMILY_<SPAWNED>, mn, mx)` where `mn ≥ 1`
      and `mx ≤ 6` (the master-pinned cap after Phase 5).
- **Event-kind scenarios** — for each `EventKind` not yet produced by
  any existing row:
  - `event_notification_emission_scen99` — trigger via a MAGE DIED
    or similar death-message path (see the existing
    `effect_chain_scen9410` golden which already shows
    `notification: MAGE DIED` — promote that to a primary fact).
  - `event_set_palette_emission_scen99` — palette change emitted on
    level start with `FAMILY_<palette-changing>` cast. If no path
    produces it organically, use the existing palette-set on level
    transition in `glad.cpp` — invoke via an `EXIT` treasure pickup.
  - `event_request_redraw_emission_scen99` — emitted by HUD updates
    on score change; reuse the scoring scenario and assert
    `EventKindAtLeast(request_redraw, 1)`.
  - `event_end_game_emission_scen99` — last-player-dies path; spawn
    one player walker, no allies, three enemies, no input; assert
    `EndGame` event at game-end tick.
  - `event_set_end_emission_scen99` — `level_done == 1` plus
    `set_end` event; reuse the exit-trigger arena and assert
    `EventKindExactly(set_end, 1)`.
- **Per-family special-cast scenarios** — for each of the 42
  `kRequiredSpecials` pairs *not* already covered by a per-family arena
  (or whose existing arena uses `kInputsFamilySpecialCoverage` that
  presses K_SPECIAL multiple times without isolating per-slot
  behaviour), add a targeted scenario:
  - `special_<family>_<idx>_scen99` (e.g. `special_soldier_2_scen99`
    for BOOMERANG, `special_archer_2_scen99` for BARRAGE,
    `special_cleric_2_scen99` for RAISE UNDEAD, etc.).
  - Each scenario:
    - `stats_level` raised via `SpawnSpec::stats_level` to the floor
      `(idx - 1) * 3 + 1` so the cycle gate
      (`sim_input_handler.cpp:218`) accepts the slot.
    - `magicpoints` raised to ≥ `special_cost(idx)` via
      `SpawnSpec::magicpoints` so the firing gate
      (`living.cpp:532-533`) lets the cast happen.
    - Inputs: cycle `K_SPECIAL_SWITCH` exactly `(idx - 1)` times to
      arrive at the target slot, then press `K_SPECIAL` once.
    - Exercises bit: exactly the one `Special_<family>_<idx>` bit.
    - Predicates: at least one of
      `WeaponFamilyEmitted(...)`, `EffectFamilyCount(...)`,
      `WalkerFamilyCount(<summoned-family>, 1, n)`,
      `EventKindExactly(<kind>, n)`,
      `WalkerPositionMoved` (teleport / blink),
      `WalkerHpRangeAtFinalTick` (heal / drain).
- Updated `tests/parity/test_parity_coverage_gate.cpp` with new gate
  cases as listed in `04a-check-behavioural-gate`. Behavioural gates
  enumerate `kRequiredWeaponFamilies` and assert that every family is
  the `arg0` of at least one `WeaponFamilyEmitted` predicate in any
  scenario's `expected_facts[]`. Same for treasures, effects,
  generators, event kinds, and specials.
- Updated `tests/parity/scenario_table.h` to register every new row and
  its `kFacts_*` / `kMut_*` constants.
- Mirror update of `../openglad-master/tools/parity_scenario_table.h`
  (byte-for-byte copy) and rebuild of `parity_dump_master`.
- Updated `.plan/parity-coverage-manifest.md` — flip every `(none yet)`
  cell to the new scenario id; add a new column "behavioural predicate"
  citing the predicate that locks the entity to a specific behaviour.
- Updated `.plan/parity-harness-design.md` — append a "Phase 04 redo:
  behavioural coverage" section documenting the new gate cases and the
  per-family slot scenarios.
- Optional: deprecate `kFamilySpawns_golem_with_nonliving_targets` —
  the blob may stay in `scenario_table.h` because some entities
  (e.g. `FAMILY_STAIN` if no organic scenario triggers it) may still
  rely on it; the `04b` check enforces that no required family loses
  coverage if the blob is removed, and if anything fails, the blob row
  stays.

**File Changes**:
- Modify `tests/parity/scenario_table.h` (new spawns, inputs, facts,
  mutations, kScenarios entries).
- Modify `tests/parity/test_parity_scenarios.cpp` (append one
  `OG_PARITY_TEST(<scenario_id>)` per new scenario id introduced in
  this phase).
- Modify `tests/parity/test_parity_coverage_gate.cpp` (new gate cases).
- Modify `tests/parity/parity_runner.cpp` and
  `tests/parity/scenario_runtime.cpp` only if a new input pattern is
  required (e.g. a multi-press scripted sequence that the existing
  `apply_inputs_at_tick` already supports — verify before editing;
  the runner is not extended to read or write `StateDump` fields).
- **Do NOT** modify `tests/parity/state_dump.{h,cpp}` in Phase 4. The
  schema-v1 freeze (Section 1, "What does *not* change") is a hard
  rule. Treasure-pickup observability is achieved with the predicate
  alternatives spelled out in the per-row predicate list above.
- Mirror to `../openglad-master/tools/parity_scenario_table.h` and
  recompile `parity_dump_master`.
- Modify `.plan/parity-coverage-manifest.md` and
  `.plan/parity-harness-design.md`.
- Commit on branch:
  `parity-finish-2: phase 04 — behavioural coverage scenarios`.
- Commit on `../openglad-master`:
  `parity-companion: phase 04 — mirror scenario_table.h
  (<branch sha or short>)`.

**Implementation Details**:
- The 42 special-cast scenarios reuse the cycle/fire input pattern
  already shipped as `kInputsFamilySpecialCoverage`; the new per-slot
  scenarios constrain the cycle to the target slot only.
- Generator scenarios use the lengthened `tick_budget = 300` because
  TENT / TOWER / etc. emit at intervals of ~150 ticks.
- The behavioural gate is implemented as a `TEST(Parity,
  behavioural_coverage_gate_*)` set in
  `test_parity_coverage_gate.cpp`; the gate body scans
  `kScenarios[].expected_facts` arrays at runtime and asserts each
  required family / kind appears as the `arg0` of at least one
  matching predicate.
- No changes to `fact_predicate.cpp` are needed unless the schema is
  extended (which it should not be in this plan).

**Verification**:
```
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_filter='Parity.behavioural_coverage*'   # all pass
build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'          # all pass
build/ci-test/og_test_parity --gtest_brief=1                                 # no regressions
sha1sum tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h  # equal
# Every new scenario id introduced in Phase 4 is registered as a gtest case:
build/ci-test/og_test_parity --gtest_list_tests | grep -E '^  (treasure_|weapon_|effect_|generator_|event_|special_)[a-z0-9_]+$' | wc -l
# Verifier compares the count above to the number of new ids added to
# kScenarios in this commit.
```

---

### Phase 5 — Recapture and reconcile every golden

**Phase Name**: Run the pinned companion against every scenario (old + new);
commit the canonical golden set.

**Implement Phase ID**: `05-recapture-and-reconcile`

**Verification Phases**:
- `05a-check-golden-count` (`check`, `bounce_target: 05-recapture-and-reconcile`):
  - `ls tests/parity/golden/*.json | wc -l` equals
    `kMasterComparableScenarioCount` (a python helper extracts the count
    by parsing `scenario_table.h`).
  - `python3 - <<'PY'` checks every golden against
    `scripts/parity/validate_schema.py` (exit 0 on every file).
- `05b-check-recapture-fresh` (`check`, `bounce_target: 05-recapture-and-reconcile`):
  - Re-run `parity_dump_master` for every scenario into `/tmp/recheck/`;
    compare with `cmp -s` against the committed `tests/parity/golden/`.
    Every golden must be byte-equal to the recapture. Any diff fails
    the check.
- `05c-check-tests-green-against-new-goldens` (`check`,
  `bounce_target: 05-recapture-and-reconcile`):
  - `cmake --build --preset ci-test --target og_test_parity`
  - `build/ci-test/og_test_parity` — every case passes. Predicates
    that were tightened in Phase 3 must still hold against the new
    goldens; any regression here means a `Parity.<id>` case fails
    and the row needs another tightening / reclassification round.

**Preexisting Inputs**:
- `.plan/parity-recapture-diff.md` (Phase 2; identifies which existing
  goldens diverge from a fresh capture)
- `tests/parity/scenario_table.h` (Phase 4-extended)
- `../openglad-master/tools/parity_scenario_table.h` (mirror; SHA-equal)
- `../openglad-master/build/ci-test/parity_dump_master`
- `tests/parity/golden/*.json` (existing 39 files)
- `scripts/parity/capture_master_golden.sh`
- `scripts/parity/validate_schema.py`
- `scripts/parity/diff_dumps.py`

**New Outputs**:
- Refreshed `tests/parity/golden/*.json` — every scenario in
  `kScenarios` with `is_branch_internal == false` has a corresponding
  canonical golden file at `tests/parity/golden/<id>.json`. Goldens for
  Phase-4 new scenarios are added; goldens that diverged in Phase 2 are
  replaced in place; goldens that were already byte-equal to the
  recapture stay unchanged.
- `.plan/parity-second-divergence-report.md` — the per-golden
  recapture & predicate-evaluation report (replaces the empty-world
  `.plan/parity-divergence-report-empty-world.md` as the
  current-state authority). Required sections:
  - **Header**: pinned companion SHA (same as Phase 2 unless
    re-rebuilt), branch HEAD SHA, total golden count, golden-replace
    count.
  - **Per-golden replacement log**: one row per replaced golden,
    columns `scenario_id | bytes_before | bytes_after | reason
    (recapture-diff / new-scenario)`.
  - **Predicate-evaluation table**: for every scenario, list each
    `expected_facts[]` entry and its branch / master evaluation
    result on the new golden. Rows where one side passes and the
    other fails are `regression` candidates (carried into Phase 6).
  - **Classified divergences**: every dump field where the
    semantic-evaluator branch result differs from the master result
    (e.g. branch HP `78.000000`, master HP `82.000000`). One of:
    - `regression` (Phase 6 owns the fix); cite the branch commit
      range likely responsible via `git log
      origin/master..HEAD -- src/...`.
    - `intended_diff` (cited branch commit explicitly authorising
      the change).
    - `rng_drift` (RNG draws differ but every fact-predicate still
      holds; this is the user's "verifiable certainty of semantic
      equivalence" rule — allowed only when the predicate surface is
      strong enough to confirm equivalence under RNG variation).
- Updated `.plan/parity-coverage-manifest.md` `Phase X sign-off
  snapshot` section with the new coverage outcome (every cell
  populated).

**File Changes**:
- `tests/parity/golden/*.json` (replace/add as needed).
- Create `.plan/parity-second-divergence-report.md`.
- Modify `.plan/parity-coverage-manifest.md` (sign-off snapshot
  section).
- Commit message: `parity-finish-2: phase 05 — recapture goldens
  against companion <sha>; <N> replaced, <M> added`.

**Implementation Details**:
The agent runs:
```bash
COMPANION_SHA=$(git -C ../openglad-master rev-parse HEAD)
# Sanity: table SHA-1 still matches.
diff <(sha1sum tests/parity/scenario_table.h | awk '{print $1}') \
     <(sha1sum ../openglad-master/tools/parity_scenario_table.h | awk '{print $1}')
mkdir -p /tmp/golden_capture
for id in $(../openglad-master/build/ci-test/parity_dump_master --list); do
    ../openglad-master/build/ci-test/parity_dump_master \
        --scenario "$id" --out "/tmp/golden_capture/$id.json"
    python3 scripts/parity/validate_schema.py "/tmp/golden_capture/$id.json"
done
# Replace any diff'd golden.
for f in /tmp/golden_capture/*.json; do
    id=$(basename "$f" .json)
    target="tests/parity/golden/$id.json"
    if [ ! -f "$target" ] || ! cmp -s "$f" "$target"; then
        cp "$f" "$target"
    fi
done
# Re-run the branch tests to confirm green.
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity
```
Predicates are NOT silently widened to accommodate a fresh golden in
this phase. A predicate that fails on a fresh golden is a Phase 3 work
item (the agent loops back via the verifier failure if any
predicate-vs-new-golden test fails).

**Verification**:
```
ls tests/parity/golden/*.json | wc -l   # equals kMasterComparableScenarioCount
for f in tests/parity/golden/*.json; do python3 scripts/parity/validate_schema.py "$f"; done
build/ci-test/og_test_parity   # all pass
diff -r <(cd /tmp/golden_capture && md5sum *.json | sort) \
        <(cd tests/parity/golden && md5sum *.json | sort)   # every committed file == recapture
```

---

### Phase 6 — Mutation canary expansion and regression classification

**Phase Name**: Re-run the canary across every scenario; classify and
fix gameplay regressions.

**Implement Phase ID**: `06-canary-and-regressions`

**Verification Phases**:
- `06a-check-canary-every-row` (`check`, `bounce_target: 06-canary-and-regressions`):
  - `scripts/parity/run_mutation_canary.sh --all` exits 0.
  - The canary's stdout lists ≥1 flip for every scenario, **with one
    exception list**: scenarios in `parity-canary-exemptions.md` may
    register 0 flips, and every entry there cites the mechanical
    reason (e.g. "harness does not invoke `SaveData::load()`"). The
    verifier enforces that the exemptions list has size ≤ the number
    of rows in the published list and that every other row flips.
- `06b-check-regressions-resolved` (`check`, `bounce_target: 06-canary-and-regressions`):
  - `python3 - <<'PY'` parses `.plan/parity-second-divergence-report.md`
    "Classified divergences" section and extracts every row whose
    classification is `regression`. For each such row, the verifier
    asserts a corresponding row exists in `.plan/parity-second-fixes.md`
    keyed by `scenario_id` (column 1), and asserts that row's
    `parity result after fix` cell starts with `green` and its
    `lock-in test` cell is non-empty. If zero rows were classified
    `regression`, the verifier accepts an empty `parity-second-fixes.md`
    body (header-only) and continues.
  - `cmake --build --preset ci-test && ctest --preset ci-test
    --output-on-failure` — full ctest must be green; no regression
    fix may break an unrelated test.
- `06c-check-no-residual-regression` (`check`, `bounce_target: 06-canary-and-regressions`):
  - Re-evaluate every scenario's `expected_facts[]` on the current
    branch dump and the master golden. Both sides must satisfy every
    predicate. The check fails if any predicate evaluates differently
    on the two sides (i.e. the divergence detector finds an
    unclassified diff).

**Preexisting Inputs**:
- `.plan/parity-second-divergence-report.md` (Phase 5)
- `.plan/parity-honest-audit.md` (Phase 1)
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `tests/parity/parity_runner.cpp`, `tests/parity/scenario_runtime.cpp`
- `scripts/parity/run_mutation_canary.sh`,
  `scripts/parity/_apply_mutation.py`

**New Outputs**:
- `.plan/parity-second-fixes.md` — one row per regression classified in
  Phase 5. Columns:
  `scenario_id | root cause (file:line + suspected commit) | fix
  description | files modified | parity result after fix | lock-in test`.
  Each fix commit is in the branch history with prefix
  `parity-fix:`. Each fix lands a focused unit test under
  `tests/unit/parity_fixes/test_<scenario>_<short>.cpp` that locks the
  behaviour in place (so a future regression of the same code path is
  caught even outside the parity harness).
- `.plan/parity-canary-exemptions.md` — explicit list of rows the
  canary cannot exercise mechanically, with rationale and a follow-up
  ticket for each. Rationale must cite specific source lines
  (e.g. "runner does not invoke `SaveData::load()`; loading happens at
  picker time via `og::scope::resources_io_init`"). The verifier
  asserts every row in this list has a `Why:` line and a
  `Future work:` line.
- Updated `.plan/parity-second-divergence-report.md` — every
  `regression` row gets a back-reference to the
  `parity-second-fixes.md` row that addresses it.
- Optional: new unit tests under `tests/unit/parity_fixes/`.

**File Changes**:
- Source-code fixes for every gameplay regression that Phase 5 surfaced.
  Each fix is a discrete `parity-fix:` commit (one commit per fix).
- New unit tests under `tests/unit/parity_fixes/`.
- Create `.plan/parity-second-fixes.md`,
  `.plan/parity-canary-exemptions.md`.
- Final commit: `parity-finish-2: phase 06 — mutation canary green;
  regressions classified`.

**Implementation Details**:
- The canary loop in `run_mutation_canary.sh` already covers `--all`;
  the agent confirms each row's `discriminating_mutation` actually
  reaches the runner (re-reading the `Known limitations` section in
  `.plan/parity-coverage-manifest.md`). Rows admitted in the
  exemption list keep their existing mutation entries but are
  whitelisted in the canary script via a parsed `parity-canary-
  exemptions.md`.
- For each `regression` row from Phase 5 divergence-report:
  1. `git log origin/master..HEAD -- <suspected files>` lists
     candidate commits.
  2. Reproduce in a focused unit test: spawn the involved walkers,
     trigger the path, assert master-side value (read from the
     golden) vs branch-side value.
  3. Either land a branch-side `parity-fix:` commit that brings the
     branch behaviour back to master's, or — if the divergence is
     deliberate (e.g. a documented refactor changing emission order)
     — reclassify the row as `intended_diff` with the commit SHA
     cited in the divergence-report.
  4. Re-run the canary on the touched row and confirm the flip
     still works post-fix.

**Verification**:
```
scripts/parity/run_mutation_canary.sh --all   # exit 0; ≥1 flip per non-exempt row
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
  - `.github/workflows/test.yml` (or wherever CI is defined) is grep'd
    for invocations of `og_test_parity`, `lint_scenario_facts.py`,
    `run_mutation_canary.sh`, and `behavioural_coverage_gate`. Missing
    invocation fails the check.
  - If the project does not have CI YAML, this verifier checks
    `scripts/parity/ci_parity.sh` (a new script Phase 7 adds) that runs
    the full bundle and exits 0.
- `07b-check-no-bypass-known-tricks` (`check`,
  `bounce_target: 07-anti-cheating-locks`):
  - The verifier creates a throwaway `git worktree add /tmp/parity-
    bypass HEAD`, applies three concrete in-tree mutations (described
    below) one at a time via `sed -i` directly inside the worktree —
    no `.patch` file is generated, the verifier scripts the edit
    inline so the bypass content is reproducible — runs the
    appropriate guard, asserts non-zero exit, then restores the
    worktree with `git -C /tmp/parity-bypass checkout -- .` before
    the next bypass:
    - **Bypass 1 (widening lint)**: `sed -i -E
      's/WalkerFamilyCount\(FAMILY_SOLDIER,\s*[0-9]+,\s*[0-9]+\)/
      WalkerFamilyCount(FAMILY_SOLDIER, 0, 99)/' tests/parity/
      scenario_table.h` against the first occurrence. Then
      `python3 scripts/parity/lint_scenario_facts.py tests/parity/
      scenario_table.h` must exit non-zero with the diagnostic
      `unjustified_widening` on stderr.
    - **Bypass 2 (behavioural coverage)**: pick a specific weapon
      family the behavioural gate enforces (e.g. `FAMILY_KNIFE`) and
      `sed -i` the corresponding `WeaponFamilyEmitted(FAMILY_KNIFE,
      ...)` predicate out of `tests/parity/scenario_table.h`. Then
      build and run
      `build/ci-test/og_test_parity --gtest_filter='Parity.
      behavioural_coverage_gate_weapons'`; the test must FAIL with a
      diagnostic naming `FAMILY_KNIFE`.
    - **Bypass 3 (golden tampering)**: pick the first existing golden,
      `printf 'X' > tests/parity/golden/<id>.json`. Then
      `python3 scripts/parity/validate_schema.py tests/parity/golden/
      <id>.json` must exit non-zero, AND
      `build/ci-test/og_test_parity --gtest_filter='Parity.<id>'`
      must FAIL.
  - All three guards must trigger; verifier exits non-zero if any of
    the three bypasses pass silently. After the three runs the
    worktree is removed with `git worktree remove --force
    /tmp/parity-bypass`.

**Preexisting Inputs**:
- `tests/parity/test_parity_coverage_gate.cpp`
- `scripts/parity/lint_scenario_facts.py`
- `scripts/parity/run_mutation_canary.sh`
- `scripts/parity/validate_schema.py`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/*.json`
- `.github/workflows/*.yml` (if present)

**New Outputs**:
- `scripts/parity/ci_parity.sh` — single-shot driver that runs:
  ```
  cmake --build --preset ci-test --target og_test_parity
  build/ci-test/og_test_parity
  python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h
  scripts/parity/run_mutation_canary.sh --all
  scripts/parity/capture_master_golden.sh --dry-run-compare-only
  ```
  Last command is a new `--dry-run-compare-only` mode of
  `capture_master_golden.sh` that recaptures every golden into
  `/tmp/recapture/` and asserts byte-equal vs committed; exit 1 on any
  diff.
- New mode `--dry-run-compare-only` in `capture_master_golden.sh`.
- Updated `.github/workflows/test.yml` (if present) — add a
  `parity-strict` job that runs `scripts/parity/ci_parity.sh`. If no CI
  YAML exists, the verifier accepts the `ci_parity.sh` script as the
  CI integration surface and documents the invocation in
  `.plan/parity-second-divergence-report.md` "How to run in CI".

**File Changes**:
- Create `scripts/parity/ci_parity.sh` (executable).
- Modify `scripts/parity/capture_master_golden.sh` (new flag).
- Modify `.github/workflows/test.yml` (CI job) — if file exists.
- Commit: `parity-finish-2: phase 07 — anti-cheating gate + CI wiring`.

**Implementation Details**:
- The bypass-3 test relies on `validate_schema.py` returning non-zero
  on a malformed JSON; verify the script already does so (it does).
- The bypass-1 test relies on the new lint rule from Phase 3; the
  verifier exercises the rule end-to-end (parse → diagnose →
  non-zero exit).
- The bypass-2 test relies on the new behavioural gate from Phase 4.

**Verification**:
```
test -x scripts/parity/ci_parity.sh
scripts/parity/ci_parity.sh   # exit 0
# Bypass tests, executed in a throwaway worktree (full sequence in 07b above).
git worktree add /tmp/parity-bypass HEAD
( cd /tmp/parity-bypass && \
    sed -i -E 's/WalkerFamilyCount\(FAMILY_SOLDIER,[^)]*\)/WalkerFamilyCount(FAMILY_SOLDIER, 0, 99)/' \
        tests/parity/scenario_table.h && \
    ! python3 scripts/parity/lint_scenario_facts.py tests/parity/scenario_table.h )
git worktree remove --force /tmp/parity-bypass
```

---

### Phase 8 — Final honest sign-off

**Phase Name**: Write the final sign-off; close the loop on the user's
goal.

**Implement Phase ID**: `08-final-signoff`

**Verification Phases**:
- `08a-check-signoff-content` (`check`, `bounce_target: 08-final-signoff`):
  - `test -f .plan/parity-signoff-honest.md`.
  - The signoff document references the actual current state, not
    self-referential "see Phase X". Required sections (the verifier
    asserts each header exists and the body is non-empty):
    - `## Final test surface` — list of test cases and pass/fail
    - `## Coverage outcome` — every required family / event / special
      backed by a behavioural predicate (gate-passing evidence)
    - `## Mutation canary outcome` — flip count per row
    - `## Classified divergences` — final per-row classification
    - `## Anti-cheating locks` — names of every check that catches
      a future regression
    - `## Open risks` — partial coverage carry-overs (e.g. on-disk
      save round-trip if still untested)
  - The signoff lists every Phase 1 → Phase 7 commit SHA range.
  - `git log -1 --name-status | grep parity-signoff-honest.md`.
- `08b-check-full-suite-green` (`check`, `bounce_target: 08-final-signoff`):
  - `cmake --build --preset ci-test && ctest --preset ci-test
    --output-on-failure` — exit 0.
  - `scripts/parity/ci_parity.sh` — exit 0.
  - The verifier runs the same set of checks that the CI job would and
    confirms each exits 0.

**Preexisting Inputs**:
- All prior phase outputs:
  - `.plan/parity-honest-audit.md`
  - `.plan/parity-recapture-diff.md`
  - `.plan/parity-second-divergence-report.md`
  - `.plan/parity-second-fixes.md`
  - `.plan/parity-canary-exemptions.md`
  - `.plan/parity-coverage-manifest.md`
- `tests/parity/golden/*.json`
- `scripts/parity/ci_parity.sh`

**New Outputs**:
- `.plan/parity-signoff-honest.md` — the final, definitive sign-off
  document. Includes a one-line statement of the form *"Parity overall:
  GREEN. Every required family, event kind, weapon, treasure, FX, and
  special is exercised by at least one scenario whose
  `expected_facts[]` predicate constrains its behaviour; the mutation
  canary flips ≥1 predicate per non-exempt row; the recapture verifier
  confirms every golden was produced by companion SHA <pinned>."*
  All other claims cite specific verifier outputs from Phase 7's
  `ci_parity.sh`.

**File Changes**:
- Create `.plan/parity-signoff-honest.md`.
- Commit: `parity-finish-2: phase 08 — honest signoff`.

**Implementation Details**:
The agent runs the full CI bundle one more time and writes the document
based on the actual output. The signoff lists each test name as it
appears in `og_test_parity --gtest_list_tests` and the exact pass
counts.

**Verification**:
```
test -f .plan/parity-signoff-honest.md
grep -c '^## ' .plan/parity-signoff-honest.md >= 6
scripts/parity/ci_parity.sh   # exit 0
ctest --preset ci-test --output-on-failure   # exit 0
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

After all phases land, the user can confirm the goal is met with a
single command:

```bash
scripts/parity/ci_parity.sh
```

Which runs (and must each exit 0):

1. `cmake --build --preset ci-test --target og_test_parity` — builds.
2. `build/ci-test/og_test_parity` — every test green, including the new
   behavioural coverage gates and the existing structural gates.
3. `python3 scripts/parity/lint_scenario_facts.py
   tests/parity/scenario_table.h` — no predicate is widened without
   citation.
4. `scripts/parity/run_mutation_canary.sh --all` — every non-exempt row
   flips at least one predicate when its `discriminating_mutation`
   is applied.
5. `scripts/parity/capture_master_golden.sh --dry-run-compare-only` —
   recapture matches every committed golden byte-for-byte (proves the
   pinned companion still produces the same bytes).

Manual cross-checks:

- `cat .plan/parity-signoff-honest.md` shows every required family /
  event / special is referenced by a covering scenario, and every
  divergence is either fixed (`parity-fix:` commit) or classified
  (`intended_diff` row with commit SHA).
- `sha1sum tests/parity/scenario_table.h
  ../openglad-master/tools/parity_scenario_table.h` — equal.
- `git -C ../openglad-master rev-parse HEAD` matches the pinned SHA
  recorded in `.plan/parity-coverage-manifest.md` frontmatter and in
  `.plan/parity-signoff-honest.md`.
- `grep -r 'parity-fix:' --oneline $(git log origin/master..HEAD
  --pretty=%H) | head` — every regression got a fix commit.

If any of these fails, the relevant phase's bounce target is the
implement phase that owned the artifact; no agent-guided routing is
needed because the contract is fully linear.
