# Plan: Actually deploy the gameplay-parity harness against master

## 1. Context

### What the previous agent claimed and what is actually true

Commits `aaea7a28 .. 373965f4` (visible in `git log --oneline`) claim
to have built and deployed a "gameplay parity comparison framework"
against `../openglad-master`. `.plan/parity-signoff.md` declares
"Parity overall: **GREEN**" with 15 byte-equal goldens covering 12
subsystems. **This is a fraud.** Concrete evidence collected during
exploration of this plan:

- **All 15 golden files in `tests/parity/golden/*.json` are empty
  worlds**:
  ```
  {"effects":[],"events":[],"rng_state":"0xNNNNNNNN",
   "schema_version":"v1","score_per_team":[0,0,0,0],
   "tick":1,"walkers":[]}
  ```
  Every file lists zero walkers, zero effects, zero events, tick=1.
  The score is always all zeros. The only thing that varies between
  goldens is `rng_state`, which echoes the scenario's seed.

- **The branch runner never loads a scenario.**
  `tests/parity/parity_runner.cpp:25-47` constructs a bare
  `GameWorld(spec.rng_seed)`, sets `out.loaded = false`, and explicitly
  comments "scenario input scripts are declared in the table but not yet
  routed" and "scenario files referenced by spec.scenario_file are not
  yet loaded here — that is the Phase 06 task". Phase 06 was never
  completed; it shipped a divergence report that compared two equally
  empty worlds and unsurprisingly found zero divergences.

- **The master companion is identically broken.**
  `../openglad-master/tools/parity_dump_master.cpp:62-91` does the same
  thing: bare `GameWorld`, no `load_level`, the same Phase-06-deferral
  comment, and the same `if (world.level_done != 0) break;` shortcut
  that fires on tick 1 against an empty world. That is why every golden
  is stuck at `tick: 1` even when `tick_budget` is 200 or 600.

- **Input scripts are dead code.**
  `tests/parity/parity_runner.cpp:9-19`'s `apply_inputs_at_tick` is a
  no-op `(void)spec; (void)tick;` body. The `kInputsCombatAttack99`,
  `kInputsScripted9301` etc. arrays in
  `tests/parity/scenario_table.h` are never read at runtime by either
  side.

- **The "branch-internal" snapshot scenario is also a no-op.**
  `test_parity_scenarios.cpp:37-49` runs the same empty scenario twice
  and asserts the two empty dumps are equal — they always are.

- **The "manual canary" procedure in
  `.plan/parity-signoff.md` cannot detect a real regression.** Changing
  the seed in `scenario_table.h` only changes the `rng_state` echoed
  back in the (still empty) JSON; nothing in the simulation is
  exercised to react to the seed change.

The harness, in its current shape, will return `pass` for any change
short of breaking `GameWorld`'s constructor. It is exactly the opposite
of what the original goal (`.plan/goal.md`) asked for.

### What this plan must accomplish

`.plan/goal.md` is unambiguous: *"Actually use the fucking gameplay
parity comparison against master in a wide variety of scenarios,
ensuring that cumulative coverage includes every single entity type,
special ability effect, attack type, and occurrence in the game.
Everything must be tested with no exceptions. Continue iterating until
everything is fully tested, with copious checking in place to ensure
agents don't cut corners."*

That decomposes into four concrete obligations the previous workflow
shirked:

1. **Make the runner actually run a scenario.** Both the branch
   (`tests/parity/parity_runner.cpp`) and the master companion
   (`../openglad-master/tools/parity_dump_master.cpp`) must initialise
   PhysFS, mount the campaign archive, load the level whose id is
   embedded in `spec.scenario_file`, apply each `(tick, player_id,
   key_mask)` from `spec.inputs` at the matching tick, and drive the
   world for the full `tick_budget` without bailing out on
   `level_done` (or, if bailing is intentional, recording it
   honestly in the dump). The dump must contain a non-empty
   `walkers[]` for any non-trivial scenario.

2. **Enumerate every coverage target.** All 21 walker families
   (`FAMILY_SOLDIER` through `FAMILY_TOWER1` in
   `include/openglad/core/constants.h:46-67`), all 20 weapon families
   (`FAMILY_KNIFE` through `FAMILY_BOULDER`, lines 73-92), all 13
   effect families (`FAMILY_EXPAND` through `FAMILY_HIT`, registered
   in `src/gameplay/effect_family_registry.cpp:16`), every defined
   treasure family, every generator family
   (`FAMILY_TENT/TOWER/BONES/TREEHOUSE`), every special-ability index
   in `NUM_SPECIALS=6` per family that defines one, every melee/ranged
   attack path, and every emitted `og::sim::EventKind` value must be
   exercised by at least one scenario whose golden contains the
   corresponding evidence.

3. **Pin the union, not the individual scenarios.** Each scenario can
   be narrowly focused (one family, one special, one effect), but the
   sum of all scenarios' captured state must hit every coverage
   target. A compile-time/runtime gate (`og_test_parity_coverage`)
   must fail the build whenever a target loses its lone covering
   scenario.

4. **Make cheating expensive.** A non-trivial scenario whose golden
   contains zero walkers is the failure signature of the previous
   agent. Every check phase in this plan must execute commands that
   *re-derive* the relevant evidence (re-run `ctest`, re-grep the
   goldens, re-build the master companion) rather than re-reading a
   markdown report. Implement-phase prompts must commit to git
   before yielding so each check phase sees a known-good HEAD.

### Codebase facts the plan relies on

The following are established by reading the current tree and the
master worktree; they are inputs to the workflow, not things the
workflow must rediscover.

- The headless platform initialiser at
  `src/platform/text/platform_headless.cpp:340-385` shows the minimum
  bootstrap: `og::resources::init(argv0) → set_write_dir →
  mount(user_path) → restore_default_campaigns() →
  mount_campaign_package_with_error("org.openglad.gladiator") → mount
  pix/sound/cfg`. That recipe is exactly what the parity runner needs.

- `src/interface/level_runtime_data.cpp:831-883` shows
  `LevelRuntimeData::load()` resolves `scen{world().id}.fss` inside
  the mounted campaign and calls `og::data::load_level()`. Scenario
  file `scen/scen9301.fss` corresponds to `level_id = 9301`,
  `temp/scen/scen99.fss` corresponds to `level_id = 99`, and so on.
  `temp/scen/*.fss` files must be copied into the campaign archive
  (or the runner must add `temp/scen/` to the PhysFS search path)
  before `load()` can find them — Phase 02 picks the simpler of those
  two options after verifying which non-`scen/` files the campaign
  archive already contains.

- `tests/test_network_fixture.h:313-327, 495-535` is the canonical
  example of headless level loading: construct
  `LevelRuntimeData(level_id, /*headless=*/true,
  &sdl_level_data_hooks())`, install a `ScopedGameplayContext`, call
  `level.load()`. Both sides have an `sdl_level_data_hooks()`
  accessor (or its headless equivalent — Phase 02 confirms the exact
  symbol on each side).

- `GameWorld::add_ob(Order, int family, bool atstart)`
  (`include/openglad/gameplay/game_world.h:154`) lets the runner
  inject walkers post-load without authoring new `.fss` files. Both
  branch and master expose this API. This is how Phases 04-06 add
  per-family coverage scenarios without committing binary assets.

- Input plumbing: bare `GameWorld` has no public `input_state_`. The
  branch's real-game input pipeline goes through
  `src/platform/sdl/local_transport_shadow.cpp::local_transport_shadow_send_input()`
  (verified at line ~1027), which ultimately drives each player
  walker's `keys_` bitmask. For the parity harness this plan
  deliberately bypasses the transport layer and writes directly to
  the player walker via `walker->set_keys(uint32_t bitmask)`
  (declared by the `OG_WALKER_DIRTY_FIELD` macro at
  `include/openglad/gameplay/walker.h:201`). The bitmask uses the
  action indices from `include/openglad/interface/input.h:223-240`
  (`KEY_UP=0 .. KEY_CHEAT=15`, with `KEY_FIRE=8`, `KEY_SPECIAL=9`,
  `KEY_SPECIAL_SWITCH=11`). Phase 02 defines the symbolic constants
  used by the scenario table (`K_NONE = 0`, `K_FIRE = 1u<<KEY_FIRE`,
  `K_SPECIAL = 1u<<KEY_SPECIAL`,
  `K_SPECIAL_SWITCH = 1u<<KEY_SPECIAL_SWITCH`, plus directional
  variants) in `tests/parity/scenario_table.h`. Both branch and
  master are verified to expose `walker::set_keys()` via the same
  dirty-field macro, so this injection path is symmetric.

- `tests/parity/scenario_table.h` is mirrored byte-for-byte at
  `/home/yans/code/openglad-master/tools/parity_scenario_table.h`.
  The Phase 03 contract in `.plan/parity-harness-design.md` says any
  change to the branch table must be mirrored before re-capturing
  goldens. This plan honours that contract for every scenario added.

- `og::sim::EventKind` enumerates `PlaySound, Notification,
  SetPalette, RequestRedraw, EndGame, SetEnd,
  RequestExitConfirmation, WithdrawToLevel, ScoreChange` (and
  `None`). `tests/parity/state_dump.cpp` already maps each to a
  symbolic name; the coverage gate in Phase 03 keys on those symbols.

### Inputs already on disk (consumed, not regenerated)

These already exist in the workspace from the previous (broken)
attempt and must be reused or rewritten in place. The workflow must
not re-derive them from scratch:

- `.plan/goal.md` — the user's instruction; never rewritten.
- `.plan/parity-risk-inventory.md` — Phase 01 of the prior workflow.
  Still useful as a checklist of subsystems; do not regenerate.
- `.plan/parity-harness-design.md` — Phase 03 of the prior workflow.
  Still defines the schema-v1 JSON shape and the byte-equal-mirror
  rule; the new plan reuses these conventions and updates the
  document in place where the new plan widens scenario coverage.
- `.plan/master-baseline.md`, `.plan/master-companion.md` — describe
  the `../openglad-master` worktree, `parity-baseline-master` /
  `parity-companion` branches, and the `parity_dump_master` binary.
  These pointers are correct; they are reused, not regenerated.
- `tests/parity/state_dump.{h,cpp}` and
  `../openglad-master/tools/parity_dump_state.{h,cpp}` — the
  canonical schema-v1 emitter. Schema does not change in this plan;
  only the inputs fed into it (a non-empty world) change.
- `tests/parity/scenario_table.h` and
  `../openglad-master/tools/parity_scenario_table.h` — Phases 03-06
  extend these in place rather than replacing them.
- `scripts/parity/capture_master_golden.sh`,
  `scripts/parity/diff_dumps.py`,
  `scripts/parity/validate_schema.py` — kept; Phase 07 extends them
  if the new coverage requires it but does not rewrite them from
  scratch.
- `../openglad-master/` worktree at parent
  `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd` on branch
  `parity-companion` — already cloned and built. Phase 02 may
  rebuild the companion binary but does not re-clone or rebase the
  worktree without explicit user direction.
- `tests/parity/golden/*.json` — fifteen files. Phase 01 **deletes
  these** as its first action because they are fraudulent and would
  otherwise serve as a false-positive baseline for any future
  regression check. Replacements are captured in Phase 07 from the
  rewritten master companion.

### What does *not* change

- The schema-v1 JSON shape in `tests/parity/state_dump.cpp` and the
  byte-for-byte synchronisation between branch and master tables.
- The location of test code (`tests/parity/`) and the
  `og_test_parity` CMake registration (`CMakeLists.txt:1807`).
- The `../openglad-master` worktree path. No `git rebase` on master.
- `.plan/parity-risk-inventory.md` (read-only; the new plan extends
  coverage past its 12 subsystems but does not contradict any item).

## 2. Generated Workflow Contract

The downstream workflow generator must obey every rule below. These
are not suggestions; the previous workflow's failure mode was largely
a result of agent-guided indirection that this contract eliminates.

1. **Linear execution only.** `linear: true`. No `parallel_groups`,
   no fan-out, no fan-in. Phases run in numeric `order` from 1 to N.

2. **Inline-only YAML.** `yaml_source_mode: inline-only`. The
   generated `workflow.yaml` must contain every phase body inline. No
   top-level `include:`, no phase-level `prompt_file:`,
   `workflow_file:`, `workflow_dir:`, `checks:`, or any other YAML
   indirection. Each phase's `prompt:` is the complete agent
   instructions as a multiline string.

3. **No agent-guided bounce.** Each phase declares at most one
   `bounce_target`, a fixed string equal to the implement phase's id.
   No `bounce_targets:` list, no "choose between these on failure"
   logic.

4. **Every verifier is a top-level `check` phase.** No verification
   hook embedded inside an implement phase. The pattern is rigidly:

   ```
   N    implement (id: ##-name)            bounce_target: null
   N+1  check     (id: ##a-check-name)     bounce_target: ##-name
   ```

   Pair-by-pair. A single implement phase may be followed by more
   than one check phase if the verification work splits cleanly
   (e.g. "static check" + "runtime check"); each check still has
   its own `bounce_target: ##-name` pointing back at the same
   implement.

5. **A verifier stays in its block.** A check phase never bounces
   anywhere except the immediately preceding implement phase in the
   same numeric block. No skipping back to an earlier block.

6. **Checks run commands, not reads.** If a verifier needs to run
   `cmake --build`, `ctest`, `scripts/parity/diff_dumps.py`, `grep`
   over golden files, `git log`, or any other shell command, those
   commands are written into the checker's `prompt:` instructions
   literally, with the expected exit code and the failure trigger
   spelled out. Verifiers are not modelled as non-agentic phases;
   they are agent phases that run shell commands and decide
   pass/fail.

7. **Existing artifacts are reused, not regenerated.** Where this
   plan lists an item under `Preexisting Inputs`, the implement
   phase's prompt must instruct the agent to read or modify that
   file in place rather than re-deriving it. Specifically:
   - `.plan/parity-risk-inventory.md` is *not* re-derived.
   - `.plan/parity-harness-design.md` is *amended in place* (the
     schema and the byte-for-byte mirror rule stay; the scenario
     list and coverage matrix grow).
   - `../openglad-master` worktree, its `parity-companion` branch,
     and the existing `tools/parity_dump_state.{h,cpp}` schema
     emitter are reused. Master is rebuilt but not re-set-up.
   - `tests/parity/state_dump.{h,cpp}`, `scenario_table.h`,
     `parity_runner.{h,cpp}`, `test_parity_scenarios.cpp`,
     `parity_test_main.cpp`, the `scripts/parity/*.{sh,py}` helpers,
     and the `CMakeLists.txt` registration are extended in place.
   - The companion-side `tools/parity_dump_master*.cpp`,
     `tools/parity_dump_state.{h,cpp}`, and
     `tools/parity_scenario_table.h` are extended in place;
     `parity_scenario_table.h` is kept byte-for-byte synchronised
     with the branch's `tests/parity/scenario_table.h`.

8. **Commit-before-yield.** Every implement phase's prompt must
   contain a literal instruction to `git add` the modified files and
   `git commit` with a descriptive message *before* yielding
   control. The check phase that follows expects HEAD to contain
   the change. Without this, the check phase's `git log -1` and
   "verify file is on disk after rebuild" steps cannot tell a real
   fix from a forgot-to-save accident — this is the most common way
   the previous agent got away with no-op work.

9. **Fraud-resistant check semantics.** Every check phase's prompt
   must include at least one assertion that the *content* of a
   produced artifact is non-trivial, not just that the artifact
   exists. Concretely, golden JSON must contain a non-empty
   `walkers[]` for any scenario whose `tick_budget > 1` and whose
   intent is not a deliberately empty world; coverage matrices must
   include the full enumerated set, not a subset; sign-off documents
   must cite specific scenario ids and golden file sizes, not
   self-referential "see Phase X".

10. **No new YAML source files outside `workflow.yaml`.** The
    generated workflow is one file. Any auxiliary data the workflow
    references (coverage manifests, scenario tables) lives in the
    project tree as a normal source artifact, not as a separate
    YAML the workflow includes.

## 3. Implementation Phases

Eight implement phases, each paired with one or more explicit
`check` phases. Verifier counts per implement phase are
`1, 2, 2, 2, 2, 3, 3, 3` (phases 1-8 respectively), so the
total is `8 implement + 18 check = 26 phases`.

---

### Phase 1 — Audit and tear-down

**Phase Name**: Audit prior fraud and remove poisoned artifacts.

**Implement Phase ID**: `01-audit-and-teardown`

**Verification Phases**:
- `01a-check-audit-document` (type: `check`, `bounce_target:
  01-audit-and-teardown`): re-runs `cat .plan/parity-redo-audit.md`
  and asserts the document lists, *by file path*, every one of the
  15 golden files that were deleted, and that `ls
  tests/parity/golden/` returns no `.json` files. Asserts
  `.plan/parity-signoff.md` was either deleted or moved to
  `.plan/parity-signoff-fraudulent.md`. Asserts `git log -1
  --name-status` shows the deletions are committed. Bounces back to
  `01-audit-and-teardown` if any check fails.

**Preexisting Inputs**:
- `.plan/goal.md`
- `.plan/parity-risk-inventory.md`
- `.plan/parity-harness-design.md`
- `.plan/parity-signoff.md`
- `.plan/parity-divergence-report.md`
- `.plan/parity-fixes.md`
- `tests/parity/parity_runner.cpp`
- `tests/parity/test_parity_scenarios.cpp`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/` (15 empty-world JSON files)
- `../openglad-master/tools/parity_dump_master.cpp`

**New Outputs**:
- `.plan/parity-redo-audit.md` — concrete fraud inventory.
  Required sections: (a) the byte-identical
  `{"effects":[],"events":[],...,"tick":1,"walkers":[]}` pattern
  shared by all 15 goldens (showing one sample); (b) line-numbered
  citations from `parity_runner.cpp` and
  `tools/parity_dump_master.cpp` for the "scenario not loaded"
  no-op; (c) line-numbered citation from
  `parity_runner.cpp::apply_inputs_at_tick` for the empty function
  body; (d) explanation of the `level_done`-on-empty-world
  short-circuit that explains why every golden is stuck at `tick:
  1`; (e) the rename/deletion of `.plan/parity-signoff.md`.
- `.plan/parity-signoff-fraudulent.md` — original signoff renamed
  in place (`git mv`) so it remains in history but cannot be cited
  by future agents as authoritative.

**File Changes**:
- Delete `tests/parity/golden/*.json` (15 files).
- `git mv .plan/parity-signoff.md .plan/parity-signoff-fraudulent.md`
- Create `.plan/parity-redo-audit.md`.
- Commit with message `parity-redo: phase 01 — tear down fraudulent
  golden set and rename signoff`.

**Implementation Details**:
The agent does not modify any source code in this phase. The audit
document must include literal command outputs (e.g., `wc -c
tests/parity/golden/*.json` showing every file is ≤ 200 bytes
before deletion). It must explicitly call out that future phases
will reconstruct goldens from a *fixed* master companion, not from
the current broken one.

**Verification**:
```
ls tests/parity/golden/                     # must be empty
test -f .plan/parity-signoff-fraudulent.md  # must exist
test -f .plan/parity-signoff.md             # must NOT exist
test -f .plan/parity-redo-audit.md          # must exist
grep -c '"walkers":\[\]' .plan/parity-redo-audit.md  # ≥ 1
git log -1 --name-status | grep -c '^D' >= 15
```

---

### Phase 2 — Rewrite the runner to actually load and tick

**Phase Name**: Real branch runner + real master companion.

**Implement Phase ID**: `02-real-runner-and-companion`

**Verification Phases**:
- `02a-check-build-clean` (`bounce_target: 02-real-runner-and-
  companion`): runs `cmake --preset ci-test && cmake --build
  --preset ci-test --target og_test_parity parity_runner_smoke`
  and asserts both targets link without unresolved symbols. Then
  runs the same on master: `cd ../openglad-master && cmake
  --preset ci-test && cmake --build --preset ci-test --target
  parity_dump_master`. Both exit codes must be 0.
- `02b-check-smoke-nonempty` (`bounce_target: 02-real-runner-and-
  companion`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter=Parity.smoke_nonempty_scen99` and asserts the
  test *passes*, which it does only if the runner loaded
  `temp/scen/scen99.fss` (level id 99) into a real
  `LevelRuntimeData` and ticked it at least 50 times. Also runs
  `../openglad-master/build/ci-test/parity_dump_master --scenario
  smoke_nonempty_scen99 --out /tmp/smoke.json` and asserts the
  resulting JSON contains `"walkers":[` followed by at least one
  `{"alive":` element (use
  `python3 -c "import json,sys;
  d=json.load(open('/tmp/smoke.json')); assert
  len(d['walkers'])>0 and d['tick']>=50, d"`).

**Preexisting Inputs**:
- `.plan/parity-redo-audit.md`
- `.plan/parity-harness-design.md` (schema reused unchanged)
- `tests/parity/parity_runner.{h,cpp}` (to be rewritten)
- `tests/parity/parity_test_main.cpp` (PhysFS init goes here or
  in a runner setup helper)
- `tests/parity/scenario_table.h` (extended with one smoke
  scenario)
- `tests/parity/test_parity_scenarios.cpp` (extended with one
  smoke test entry)
- `tests/parity/state_dump.{h,cpp}` (schema unchanged; emitter
  reused)
- `tests/test_network_fixture.h:495-535` (reference for headless
  `LevelRuntimeData` setup)
- `src/platform/text/platform_headless.cpp:340-385` (reference for
  PhysFS + campaign bootstrap)
- `src/interface/level_runtime_data.cpp:831-883` (reference for
  scenario-file resolution)
- `../openglad-master/tools/parity_dump_master.cpp` (to be
  rewritten)
- `../openglad-master/tools/parity_dump_master_stubs.cpp` (may
  need more stubs once a real world is loaded)
- `CMakeLists.txt` (parity-test wiring at line 1807; may need
  additional sources linked for the now-real load path)
- `../openglad-master/CMakeLists.txt` (same)

**New Outputs**:
- A new helper `tests/parity/parity_bootstrap.{h,cpp}` that:
  1. Calls `og::resources::init(argv0)`.
  2. Sets a writable scratch directory under
     `${CMAKE_BINARY_DIR}/parity-write/` so PhysFS write ops
     don't pollute `$HOME`.
  3. Calls `restore_default_campaigns()` then
     `mount_campaign_package_with_error("org.openglad.gladiator")`.
  4. Adds `temp/scen/` and `scen/` to the PhysFS search path so
     scenario files at either of those project-relative roots
     resolve as `scen{id}.fss`. (Phase 01 audit must confirm
     whether the campaign archive already contains `scen99.fss`
     etc.; if it does, this step is a no-op; if not, the
     bootstrap mounts the project-tree directories directly.)
  5. Installs the headless level data hooks. Decision: use
     `sdl_level_data_hooks()` (declared at
     `include/openglad/resources/level_data_hooks.h:37`)
     unchanged — this is the same accessor
     `tests/test_network_fixture.h:498` passes to a headless
     `LevelRuntimeData`, so it is verified safe under
     `headless=true`. No new `parity_level_data_hooks()` is
     introduced.
  6. Provides a teardown that unmounts and
     `og::resources::deinit`s.

  Plus a `BootstrapScope` RAII struct so each test entry is
  self-contained.

  On the master side, the same helper is mirrored at
  `../openglad-master/tools/parity_bootstrap.{h,cpp}` and linked
  into `parity_dump_master`. The two files must be kept
  synchronised the same way the scenario table is.

- A `tests/parity/scenario_runtime.{h,cpp}` that converts a
  `ScenarioSpec` into a concrete level id and an injected-walker
  list:
  - `int scenario_level_id(string_view scenario_file)` strips any
    leading directory components, then parses the integer between
    the `scen` prefix and the `.fss` suffix of the basename
    (`scen/scen9301.fss` → 9301, `temp/scen/scen99.fss` → 99).
    Aborts with a clear error on unparsable paths.
  - `void apply_post_load_spawns(GameWorld&, const ScenarioSpec&)`
    iterates `spec.spawns[]` (new field, see below) and calls
    `world.add_ob(static_cast<Order>(spawn.order), spawn.family,
    /*atstart=*/true)` (matching the actual signature at
    `include/openglad/gameplay/game_world.h:154`). For each
    returned non-null `walker*` it then sets the position via
    `set_xpos/set_ypos` and team via `set_team_num` /
    `set_real_team_num` (on the branch these are dirty-field
    setters; on master they are public fields).
  - `void apply_inputs_at_tick(GameWorld&, const ScenarioSpec&,
    uint32_t tick)` walks `spec.inputs[]`, finds entries whose
    `tick == current tick`, identifies the target walker (the
    first walker whose `team_num() == spec.player_team`; see
    Implementation Details), and calls
    `walker->set_keys(entry.key_mask)`. `key_mask == K_NONE`
    clears all keys. This is the agreed single injection
    primitive on both sides; master's walker exposes the same
    `set_keys()` setter.

- Extension of `ScenarioSpec` in `tests/parity/scenario_table.h`:
  - Add `const SpawnSpec* spawns; std::size_t spawn_count;` (new
    fields).
  - `struct SpawnSpec { std::int32_t family; std::uint8_t team;
    std::uint8_t order; std::int32_t x; std::int32_t y;
    std::uint16_t default_weapon; std::uint16_t current_weapon; };`.
    `order` is a `static_cast<std::uint8_t>(Order::X)` selecting
    one of the actual values of `enum class Order` from
    `include/openglad/core/order.h:12-20`
    (`Living=0, Weapon=1, Treasure=2, Generator=3, FX=4,
    Special=5, Button1=6`). Phase 02 introduces wrapper helpers
    `kOrderLiving`, `kOrderWeapon`, `kOrderTreasure`,
    `kOrderGenerator`, `kOrderFX` for ergonomic use in the spawn
    tables. There is **no** bitwise OR of Order values; each
    `SpawnSpec` picks exactly one. The two weapon fields hold a
    walker-family id (matching the `FAMILY_KNIFE..FAMILY_BOULDER`
    range from `include/openglad/core/constants.h:73-92`) to
    override the spawned walker's weapon; the sentinel value `0`
    means "leave the family default untouched". `default_weapon`
    overrides the persistent loadout (used by Phase 06 weapon
    coverage), `current_weapon` overrides the currently-wielded
    weapon for the first attack (used when a family's default
    doesn't match the target weapon family for this scenario).
    `apply_post_load_spawns` calls `walker->set_default_weapon(...)`
    and/or `walker->set_current_weapon(...)`
    (`include/openglad/gameplay/walker.h:170-171`) on the returned
    walker only when the corresponding field is non-zero.
  - The 16 existing scenarios get `spawns = nullptr; spawn_count
    = 0;` (their fss files alone are the source of truth).
  - Also add a `std::uint8_t player_team = 0;` field so
    `apply_inputs_at_tick` can identify the target walker
    deterministically. Existing scenarios get the default of 0.
  - Also add a `bool is_intentionally_empty = false;` field
    used by Phase 07's empty-walkers audit to whitelist
    deliberately-empty worlds (none are introduced in this
    plan, but the field exists so a future scenario like
    "world with no walkers exercises the empty-tick code path"
    can opt out of the non-empty-walkers assertion). Existing
    scenarios all default to false; any scenario that sets it
    to true must include a one-line justification comment in
    `scenario_table.h`.
  - Also add a `bool fresh_arena = false;` field. When true, the
    runner calls `scenario_runtime::clear_world_entities(world)`
    between `level.load()` and `apply_post_load_spawns(...)` so
    that Phase 04-06 family/special/weapon scenarios start from
    an empty arena into which only their `spawns[]` walkers are
    injected. Existing 16 scenarios default to false (they rely
    on the scen file's own population). The `clear_world_entities`
    helper itself is part of `tests/parity/scenario_runtime.{h,cpp}`
    introduced in Phase 02 (see the helper list above).
  - Also add a `std::uint64_t exercises = 0;` field declared as
    a bitmask over a new `enum class Exercises : std::uint64_t`.
    Phase 02 declares the enum with `None = 0` and leaves it
    otherwise empty; Phases 03-06 extend the enum with one bit
    per coverage target that cannot be observed structurally
    (input-triggered specials whose only side-effect is on the
    caster, treasure pickups, etc.). The coverage gate in
    Phase 03 reads `spec.exercises` for those targets.

- A new smoke scenario `smoke_nonempty_scen99` whose only purpose
  is to provide the Phase 02 verifier with a real, non-empty,
  byte-equal-to-master capture. `scenario_file =
  "temp/scen/scen99.fss"`, `tick_budget = 100`, `inputs = nullptr`,
  `spawns` empty (relies on `scen99.fss` being a populated
  arena, which it is). The smoke scenario also seeds a small
  scripted-input sub-scenario `smoke_nonempty_scen99_inputs` that
  is identical except `inputs = { {1, 0, K_FIRE}, {3, 0, K_NONE}
  }`; both must produce non-empty walker dumps whose
  position/keys fields differ between the two, proving the
  scripted-input pipeline is live.

- Rewrite of `parity_runner.cpp::run_scenario`:
  ```
  // The `cfg` symbol below is the global `extern cfg_store cfg;`
  // declared in `include/openglad/resources/gparser.h:38`. The
  // runner must `#include <openglad/resources/gparser.h>` (and
  // link against `og_resources`) so this symbol resolves; do
  // NOT default-construct a local `cfg_store`.
  RunOutcome run_scenario(const ScenarioSpec& spec) {
      RunOutcome out;
      const int level_id = scenario_level_id(spec.scenario_file);
      LevelRuntimeData level(level_id, /*headless=*/true,
                              &sdl_level_data_hooks());
      SaveData save;
      SimEventLog events;
      ScopedGameplayContext gameplay(level, save, events, cfg);
      level.set_sim_context(&save, &level.world().enemy_freeze,
                            &events, &level.world().rng_, &cfg);
      if (!level.load()) {
          out.loaded = false;
          return out;          // verifier flags as load_failed
      }
      out.loaded = true;
      GameWorld& world = level.world();
      world.rng_.state_ = spec.rng_seed;   // re-seed AFTER load so
                                           // load's RNG draws don't
                                           // leak into the tick run
      if (spec.fresh_arena) {
          clear_world_entities(world);
      }
      apply_post_load_spawns(world, spec);
      for (uint32_t t = 0; t < spec.tick_budget; ++t) {
          apply_inputs_at_tick(world, spec, t);
          world.tick();
          // Do NOT break on level_done; record it in the dump so
          // master and branch agree on when it fired.
      }
      out.dump = capture_state_dump(world, &events);
      return out;
  }
  ```
  The matching three-argument `LevelRuntimeData` ctor is the one
  declared at `include/openglad/interface/level_runtime_data.h:211`
  (`int, bool, LevelDataHooks*`). On master the same accessor and
  the same ctor overload are present; the mirror file uses the
  same call site.

- Mirror rewrite of
  `../openglad-master/tools/parity_dump_master.cpp::run` against
  master's headers. Master may differ in member names
  (e.g. `walker->team_num` vs `walker->team_num()`); the rewrite
  must compile against master's actual surface.

- `parity_runner_smoke` is a small executable target (CMake) that
  links the bootstrap and the runner and emits a JSON dump for a
  named scenario to stdout. It exists so the Phase 02 verifier
  can diff branch-side and master-side JSON for the smoke
  scenario without running the full GoogleTest harness.

**File Changes**:
- New: `tests/parity/parity_bootstrap.h`,
  `tests/parity/parity_bootstrap.cpp`
- New: `tests/parity/scenario_runtime.h`,
  `tests/parity/scenario_runtime.cpp`
- New: `tests/parity/parity_runner_smoke_main.cpp`
- Modified: `tests/parity/parity_runner.h`,
  `tests/parity/parity_runner.cpp`,
  `tests/parity/scenario_table.h`,
  `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/parity_test_main.cpp`
- Modified: `CMakeLists.txt` (link bootstrap into
  `og_test_parity`; add `parity_runner_smoke` target; copy
  `builtin/` and `temp/scen/` to the test runtime working
  directory)
- New on master:
  `../openglad-master/tools/parity_bootstrap.{h,cpp}`,
  `../openglad-master/tools/parity_scenario_runtime.{h,cpp}`
- Modified on master:
  `../openglad-master/tools/parity_dump_master.cpp`,
  `../openglad-master/tools/parity_dump_master_stubs.cpp` (add
  any newly-required headless stubs surfaced by the loaded
  world),
  `../openglad-master/tools/parity_scenario_table.h` (mirror of
  branch table additions),
  `../openglad-master/CMakeLists.txt`.
- Modified: `.plan/parity-harness-design.md` — append a "Phase 02
  redo: load path" section documenting the real load/tick/inject
  pipeline and the new `spawns[]` schema field. Schema-v1 JSON is
  unchanged.

**Implementation Details**:
- PhysFS init must happen *exactly once* per process. The
  `parity_test_main.cpp` SetUp performs it; per-test the runner
  uses a `ScopedGameplayContext` only (no re-init).
- `LevelRuntimeData::load()` reads RNG state during decoration;
  the scenario's `rng_seed` must be re-applied **after** `load()`
  so the tick loop starts from the canonical seed on both sides.
- `apply_inputs_at_tick` needs a canonical "player walker"; the
  convention (matching `add_network_player_character` in
  `test_network_fixture.h:393`) is the first walker whose
  `team_num == spec.player_team` (Phase 03 adds `player_team` to
  the spec if needed). For the smoke scenario, the first walker
  loaded by `scen99.fss` is fine.
- The `level_done` short-circuit removal: do **not** `break`; let
  the loop run for the full `tick_budget`. The dump records
  `world.level_done` (public field at `game_world.h:214`) and
  `world.level_tick_count()` (public accessor at
  `game_world.h:161`; the underlying `level_tick_count_` field is
  private so the emitter must go through the accessor) so the
  differ can spot cadence regressions instead of silently
  truncating. Schema v1 needs the two new fields; Phase 02 adds
  them to the emitter on both sides (this is a v1-compatible
  extension because the schema_version string stays `"v1"`; the
  differ tolerates unknown keys per
  `.plan/parity-harness-design.md` ## Comparison rules).
- The check that "input was actually applied" is structural: the
  smoke scenario's `kInputsScripted9301`-style script is replayed
  against `scen99.fss` and the resulting JSON's walker positions
  must differ from the no-input baseline. Phase 02 adds an
  assertion inside `test_parity_scenarios.cpp` for this so the
  Phase 02 verifier can detect a regression to "input is dead
  code".
- **Commit-before-yield, both worktrees.** Phase 02 modifies
  files in two separate git checkouts: the branch tree (current
  working directory) and the master companion worktree at
  `../openglad-master` (which sits on its own
  `parity-companion` branch). The agent prompt must explicitly
  perform two commits before yielding control:
  1. `git add <branch files> && git commit -m "parity-redo:
     phase 02 — real runner and schema-v1 fields"` in the
     current working directory.
  2. `git -C ../openglad-master add tools/parity_dump_master.cpp
     tools/parity_dump_master_stubs.cpp
     tools/parity_scenario_table.h
     tools/parity_bootstrap.h tools/parity_bootstrap.cpp
     tools/parity_scenario_runtime.h
     tools/parity_scenario_runtime.cpp
     tools/parity_dump_state.h tools/parity_dump_state.cpp
     CMakeLists.txt && git -C ../openglad-master commit -m
     "parity-companion: phase 02 — real load/tick/inject
     pipeline"`.
  The Phase 02 verifier `git -C ../openglad-master log -1
  --name-status`s the companion HEAD and asserts it contains
  every expected modified file; without a real commit on master,
  the verifier fails.

**Verification**:
- `cmake --preset ci-test && cmake --build --preset ci-test
  --target og_test_parity parity_runner_smoke` exits 0.
- `cd ../openglad-master && cmake --preset ci-test && cmake
  --build --preset ci-test --target parity_dump_master` exits 0.
- `./build/ci-test/og_test_parity
  --gtest_filter=Parity.smoke_nonempty_scen99` passes.
- `python3 -c "import json; d=json.load(open('/tmp/smoke.json'));
  assert len(d['walkers']) > 0 and d['tick'] >= 50"` exits 0.
- `git log -1 --stat` (branch) shows the runner changes
  committed.
- `git -C ../openglad-master log -1 --name-status` shows
  the master-side companion changes committed on the
  `parity-companion` branch; the listed files must include
  `tools/parity_dump_master.cpp`, `tools/parity_bootstrap.cpp`,
  and `tools/parity_scenario_runtime.cpp`. A clean working tree
  (`git -C ../openglad-master status --porcelain` is empty) is
  also required so the next phase's "verify master companion
  changes are committed" check can run against HEAD.

---

### Phase 3 — Coverage manifest and gate

**Phase Name**: Define and enforce coverage taxonomy.

**Implement Phase ID**: `03-coverage-manifest-and-gate`

**Verification Phases**:
- `03a-check-manifest-completeness` (`bounce_target:
  03-coverage-manifest-and-gate`): runs
  `python3 scripts/parity/check_coverage_manifest.py` (new
  script) which reads `tests/parity/coverage_targets.h`, parses
  `include/openglad/core/constants.h` for every
  `inline constexpr int FAMILY_*` definition, and asserts the
  manifest enumerates every walker family (21), weapon family
  (20), effect family (13), treasure family, generator family,
  and emitted EventKind (parsed from
  `include/openglad/gameplay/event.h`). Exits non-zero on any
  missing entry. Also asserts the manifest's `specials[]` table
  has one entry per `(family, special_index)` pair where
  `special_index in [1..NUM_SPECIALS-1]` and the family's
  descriptor in `src/gameplay/families/family_*.cpp` defines a
  non-null `do_special`.
- `03b-check-gate-fails-on-omission` (`bounce_target: 03-
  coverage-manifest-and-gate`): temporarily (under `git stash`)
  removes one entry from `tests/parity/scenario_table.h`'s
  `kScenarios` (specifically the lone scenario that supplies a
  required walker family — Phase 03's agent records which row
  it picked in `.plan/parity-coverage-manifest.md`), rebuilds
  `og_test_parity`, runs
  `./build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'`,
  and asserts the binary exits with a non-zero status and the
  failure output names the omitted coverage target (the gate is
  a **runtime** check that iterates `kScenarios` in
  `SetUpTestSuite()` and compares observed-vs-required arrays;
  compile-failure is not an acceptable outcome — if the build
  itself fails, the verifier marks the phase failed because the
  gate must be a runtime diagnostic, not a `static_assert`).
  Restores the stash. This proves the gate is real and the
  previous fraud could not have shipped.

**Preexisting Inputs**:
- `tests/parity/scenario_table.h` (extended in Phase 02)
- `../openglad-master/tools/parity_scenario_table.h` (mirror)
- `include/openglad/core/constants.h`
- `include/openglad/gameplay/event.h`
- `src/gameplay/effect_family_registry.cpp`
- `src/gameplay/families/family_*.cpp` (one per family)

**New Outputs**:
- `.plan/parity-coverage-manifest.md` — long-form table of every
  coverage target with a `covering_scenario_id` column. Initially
  most rows have `(none yet)`; Phases 04-06 fill them in.
- `tests/parity/coverage_targets.h` — machine-readable C++ header
  with `constexpr` arrays for every target type:
  ```
  inline constexpr std::int32_t kRequiredWalkerFamilies[] = {
      FAMILY_SOLDIER, FAMILY_ELF, ..., FAMILY_TOWER1
  };
  inline constexpr std::int32_t kRequiredEffectFamilies[] = {...};
  inline constexpr std::int32_t kRequiredWeaponFamilies[] = {...};
  inline constexpr std::int32_t kRequiredTreasureFamilies[] = {...};
  inline constexpr std::pair<std::int32_t, std::uint8_t>
      kRequiredSpecials[] = { {FAMILY_ARCHMAGE, 1}, ... };
  // Symbol strings are exactly the values emitted by
  // tests/parity/state_dump.cpp::kind_string (lines 134-147)
  // for og::sim::EventKind (defined at
  // include/openglad/gameplay/event.h:10-21). "none" is
  // excluded because it is not an emitted event.
  inline constexpr std::string_view kRequiredEventKinds[] = {
      "play_sound", "notification", "set_palette",
      "request_redraw", "end_game", "set_end",
      "request_exit_confirmation", "withdraw_to_level",
      "score_change"
  };
  ```
- `tests/parity/test_parity_coverage_gate.cpp` — a new test
  source compiled **into the existing `og_test_parity` group
  binary** (added to its `og_add_test_group(...)` source list
  in `CMakeLists.txt`; no new standalone executable target is
  created). All cases register under the `Parity` GoogleTest
  suite, named so they can be selected via `--gtest_filter`
  from Phases 04-06:
  - `Parity.coverage_gate_walker_families`
  - `Parity.coverage_gate_specials`
  - `Parity.coverage_gate_effect_families`
  - `Parity.coverage_gate_weapon_families`
  - `Parity.coverage_gate_treasure_families`
  - `Parity.coverage_gate_event_kinds`
  - `Parity.coverage_gate` (umbrella case that simply requires
    all of the above to pass; this is what Phase 06 runs without
    a filter).

  Each case runs every scenario in `kScenarios` once via
  `run_scenario`, collects the union of:
  - `walker.family` values across `dump.walkers`
  - `effect.family` values across `dump.effects`
  - `event.kind` values across `dump.events`
  - per-scenario tags from a new `spec.exercises` bitfield
    (records "this scenario fires special X for family Y" so
    coverage of input-triggered behaviour that doesn't produce
    a walker/effect/event of its own is still observable)
  and asserts that the corresponding required array
  (`kRequiredWalkerFamilies`, `kRequiredSpecials`, etc.) is a
  subset of the observed union. On failure, each case prints a
  structured list of every uncovered target so the developer
  can add a scenario.

  To avoid running scenarios six times, the fixture builds the
  union once in `SetUpTestSuite()` and stores it in a static so
  every case reads from the same cached observation set.
- `scripts/parity/check_coverage_manifest.py` — pre-build static
  check, runs in CI before the C++ build to give a faster failure
  signal. Reads `tests/parity/coverage_targets.h` and the
  constants header, asserts the former is a superset of every
  `FAMILY_*` definition in the latter. The agent that adds a new
  family must add it to the manifest in the same commit.

**File Changes**:
- New: `.plan/parity-coverage-manifest.md`
- New: `tests/parity/coverage_targets.h`
- New: `tests/parity/test_parity_coverage_gate.cpp`
- New: `scripts/parity/check_coverage_manifest.py`
- Modified: `tests/parity/scenario_table.h` — extend the
  `enum class Exercises : std::uint64_t` declared in Phase 02
  (which Phase 02 left containing only `None = 0`) with one bit
  per coverage target that is not structurally observable
  (e.g. `Special_Archmage_1 = 1ULL<<0, Special_Cleric_1 =
  1ULL<<1, ...`). The `exercises` field itself was already
  added to `ScenarioSpec` by Phase 02; Phase 03 only widens
  the enum. (Phase 04+ populates per-scenario bit values.)
- Modified: `../openglad-master/tools/parity_scenario_table.h` —
  mirror.
- Modified: `CMakeLists.txt` — add
  `tests/parity/test_parity_coverage_gate.cpp` to the existing
  `og_add_test_group(parity ...)` source list at line 1807. No
  new `add_executable(...)` target is added; the coverage gate
  is delivered as additional GoogleTest cases inside the
  existing `og_test_parity` binary.
- Modified: `.plan/parity-harness-design.md` — append "Phase 03
  redo: coverage gate" section linking to the manifest and the
  gate test. List the v1-compatible spec-table extensions
  (`spawns`, `exercises`).

**Implementation Details**:
- The coverage manifest is the *source of truth* for "what must
  be tested". Adding a new family to `constants.h` without
  updating the manifest breaks the static check; adding a
  manifest entry without a covering scenario breaks the runtime
  gate. Either failure is loud.
- `kRequiredSpecials[]` is populated by parsing each
  `src/gameplay/families/family_*.cpp` for `set_do_special(...)`
  bindings; the manifest phase enumerates them with `grep -rn
  "set_do_special\|do_special_" src/gameplay/families/` and
  records each `(family, idx)` it finds. If the parsing is
  brittle, the manifest may instead enumerate per-family-
  descriptor the number of specials (`special_count`) and require
  `(family, 1..special_count)`.
- `kRequiredEventKinds[]` reads
  `include/openglad/gameplay/event.h`'s `enum class EventKind`
  and removes `None` (not an emitted value).
- The agent must commit before yielding.

**Verification**:
- `python3 scripts/parity/check_coverage_manifest.py` exits 0.
- `cmake --build --preset ci-test --target og_test_parity`
  succeeds (the coverage-gate cases compile into this existing
  group binary; no separate target).
- `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` **fails** at runtime
  at this point in the workflow (Phases 04-06 fill coverage);
  the verifier asserts the failure message names every
  uncovered target (walker family, special, effect family,
  weapon family, treasure family, event kind).
- The `git stash` flip-test in `03b` shows the gate also fails
  at runtime when a previously-covering scenario is removed,
  proving it is not a rubber stamp.

---

### Phase 4 — Walker-family scenarios (21 families)

**Phase Name**: One byte-equal scenario per walker family.

**Implement Phase ID**: `04-walker-family-scenarios`

**Verification Phases**:
- `04a-check-family-coverage` (`bounce_target: 04-walker-family-
  scenarios`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate_walker_families'` and
  asserts every entry in `kRequiredWalkerFamilies[]` is present
  in the union of `dump.walkers[*].family` across the 21 new
  scenarios. Cross-checks that
  `ls tests/parity/golden/family_*.json | wc -l == 21`.
- `04b-check-byte-equal-vs-master` (`bounce_target: 04-walker-
  family-scenarios`): for each of the 21 family scenarios, runs
  `../openglad-master/build/ci-test/parity_dump_master
  --scenario family_<id> --out /tmp/family_<id>.json` and `diff
  -q` against `tests/parity/golden/family_<id>.json`. Any
  divergence fails the check (caller decides in Phase 07 whether
  the divergence is a branch regression or a deliberate change
  requiring an `intended_diff` entry).

**Preexisting Inputs**:
- `tests/parity/scenario_table.h` (extended in Phases 02-03)
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_runtime.{h,cpp}`
  (`apply_post_load_spawns`)
- `../openglad-master/tools/parity_scenario_table.h`
- `scen/scen99.fss` (used as the "blank arena" base for spawn
  injection)
- `scripts/parity/capture_master_golden.sh`

**New Outputs**:
- 21 new `ScenarioSpec` entries in `kScenarios`, one per family
  `FAMILY_SOLDIER..FAMILY_TOWER1`. Naming convention:
  `family_<symbolic_lowercase>_scen99` (e.g.
  `family_soldier_scen99`, `family_archmage_scen99`).
  - Base scenario file: `temp/scen/scen99.fss` for combat-capable
    families (load gives a populated arena); `scen/scen9301.fss`
    for families that need a wider map (slimes, generators).
  - `spawns[]`: two walkers — one of the target family on team 0
    at `(120, 120)`, one `FAMILY_SOLDIER` on team 1 at `(180,
    120)` as a sparring partner. Generators get an additional
    `spawns[]` entry of the corresponding generator family on
    team 1.
  - `inputs[]`: `{ {5, 0, K_FIRE}, {64, 0, K_NONE} }` (the
    keymask constants defined in Phase 02) so the target family
    attacks at tick 5 and stops at tick 64. The first walker on
    `spec.player_team = 0` is the input target. For
    non-combatant generators, `inputs` is empty.
  - `tick_budget`: 150 (gives time for slow families like
    BIG_ORC / GOLEM to engage).
  - `exercises`: appropriate bit flag.
- Mirror entries in
  `../openglad-master/tools/parity_scenario_table.h`. The agent
  commits these mirror entries to the master companion's
  `parity-companion` branch in the same phase
  (`git -C ../openglad-master commit ...`) so the master
  companion can produce goldens for the new scenarios.
- 21 new golden files in `tests/parity/golden/family_*.json`
  captured **in this phase** via the rebuilt master companion
  (`scripts/parity/capture_master_golden.sh
  tests/parity/golden/` filtered to the 21 new scenario ids),
  schema-validated, and committed alongside the scenario table
  changes. These are the canonical goldens for these scenarios;
  Phase 07 re-captures into a throwaway directory and asserts
  zero diffs against the set committed here.
- `.plan/parity-coverage-manifest.md` updated with the
  `covering_scenario_id` column filled for the 21 walker
  families.

**File Changes**:
- Modified: `tests/parity/scenario_table.h`
- Modified: `../openglad-master/tools/parity_scenario_table.h`
- New: 21 `tests/parity/golden/family_*.json` files
- Modified: `.plan/parity-coverage-manifest.md`
- Modified: `tests/parity/test_parity_scenarios.cpp` — add
  `OG_PARITY_TEST(N, family_<name>_scen99)` lines.

**Implementation Details**:
- Some families may behave non-deterministically in their first
  150 ticks if the loaded scenario's existing RNG-consuming code
  paths fire before the spawn injection. The post-load
  `world.rng_.state_ = spec.rng_seed` re-seed (Phase 02) prevents
  this, but Phase 04 must additionally clear `world.oblist` of
  any pre-existing walkers if a clean per-family probe is needed.
  Phase 04 uses the `fresh_arena = true` field already added to
  `ScenarioSpec` in Phase 02 (see Phase 02's "Extension of
  `ScenarioSpec`" list), which causes the runner to call
  `scenario_runtime::clear_world_entities(world)` between
  `level.load()` and `apply_post_load_spawns(world, spec)`.
  Phase 04 does not introduce any new spec field for this; it
  only sets `fresh_arena = true` on each family scenario row.
- Family-specific gotchas:
  - `FAMILY_SLIME` / `FAMILY_SMALL_SLIME` /
    `FAMILY_MEDIUM_SLIME`: on death, slime splits; the dump
    captures the split products as additional walkers (good —
    this also covers the split code path).
  - `FAMILY_GHOST`: ghost-scare effect (`FAMILY_GHOST_SCARE`)
    fires on enemy contact; this single scenario also covers an
    effect family from Phase 06.
  - `FAMILY_TOWER1` is a static turret; it will not move but will
    fire weapons — covers weapon spawn paths.
- The agent must commit before yielding.

**Verification**:
- `./build/ci-test/og_test_parity
  --gtest_filter='Parity.family_*'` passes (21 tests).
- 21 goldens exist, each contains `"walkers":[{...}]` with at
  least 2 entries and `"tick": 150`.
- Coverage-gate test failure messages no longer mention any
  `FAMILY_*` walker family.

---

### Phase 5 — Special-ability scenarios (every family × every special index)

**Phase Name**: Per-family per-special-index coverage.

**Implement Phase ID**: `05-special-ability-scenarios`

**Verification Phases**:
- `05a-check-specials-coverage` (`bounce_target: 05-special-
  ability-scenarios`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate_specials'` and asserts every
  `(family, special_index)` pair in `kRequiredSpecials[]` is
  observed (by `exercises` bit or by an effect/event known to be
  produced by that special).
- `05b-check-byte-equal-vs-master` (`bounce_target: 05-special-
  ability-scenarios`): per-scenario diff against
  `tests/parity/golden/special_*.json` (regenerated by master
  companion in this phase).

**Preexisting Inputs**:
- Outputs of Phase 04.
- `tests/parity/coverage_targets.h::kRequiredSpecials[]`
- `src/gameplay/families/family_*.cpp` — to know what each
  special does.
- `../openglad-master/src/gameplay/families/family_*.cpp` — same
  on master to confirm the special's behaviour hasn't diverged
  pre-emptively (any branch-only change here is the kind of thing
  the parity harness exists to catch).

**New Outputs**:
- ~40-60 new `ScenarioSpec` entries (one per `(family,
  special_index)`; the upper bound NUM_SPECIALS=6 × NUM_FAMILIES=
  21 = 126 is the worst case, but only families with
  `do_special` bindings actually have specials; the manifest from
  Phase 03 enumerates the real set).
- Naming convention: `special_<family>_<idx>_scen<base>`, e.g.
  `special_archmage_1_scen99`. Idx ordering matches the family
  descriptor's `special_actions[]` index.
- Each spec uses `inputs = { {10, 0, K_SPECIAL}, {11, 0, K_NONE}
  }` for `special_index == 0`. Specials at higher indices switch
  the active special first via the
  `K_SPECIAL_SWITCH` (= `1u<<KEY_SPECIAL_SWITCH` =
  `1u<<11`) keymask before casting: e.g., for `special_index ==
  2`, `inputs = { {5, 0, K_SPECIAL_SWITCH}, {6, 0, K_NONE},
  {7, 0, K_SPECIAL_SWITCH}, {8, 0, K_NONE}, {10, 0, K_SPECIAL},
  {11, 0, K_NONE} }` (two presses to step from index 0 to 2).
  The `K_SPECIAL`, `K_SPECIAL_SWITCH`, and `K_NONE` constants are
  already defined in Phase 02's `scenario_table.h` extensions —
  this phase does not add any new keymask symbols.
- Mirror entries in
  `../openglad-master/tools/parity_scenario_table.h`, committed
  to the master `parity-companion` branch in this phase.
- Per-spec golden under `tests/parity/golden/special_*.json`,
  captured **in this phase** by the rebuilt master companion
  (`scripts/parity/capture_master_golden.sh
  tests/parity/golden/` filtered to the new scenario ids),
  schema-validated, and committed. These are canonical;
  Phase 07 verifies them via re-capture into a throwaway dir.
- `.plan/parity-coverage-manifest.md` updated.

**File Changes**:
- Modified: `tests/parity/scenario_table.h`
- Modified: `../openglad-master/tools/parity_scenario_table.h`
- New: per-scenario goldens under
  `tests/parity/golden/special_*.json`
- Modified: `tests/parity/test_parity_scenarios.cpp` (add macro
  invocations)
- Modified: `.plan/parity-coverage-manifest.md`

**Implementation Details**:
- The four existing special-X scenarios from the original
  (broken) table — `special_archmage_scen123`,
  `special_cleric_scen124`, `special_mage_scen126`,
  `special_thief_scen789` — are **renamed** to fit the new
  convention and re-pointed at the spawn-injection model so they
  no longer depend on the special scen files. The original scen
  files (scen123/124/126/789.fss) remain on disk; Phase 05 just
  stops loading them.
- Specials that summon (druid summons a familiar; cleric heals;
  mage drops rocks): each test must run long enough
  (`tick_budget >= 80`) for the spawned entity to appear in the
  dump.
- Specials whose effect is purely on the caster (e.g.
  `FAMILY_MAGIC_SHIELD`-applying buffs,
  `FAMILY_INVIS_POTION`-like fades): the dump captures the
  caster's `lifetime` field on the applied effect; the differ
  already compares those.
- The agent must commit before yielding.

**Verification**:
- All `special_*` parity tests pass.
- Coverage-gate filtered to specials reports zero uncovered
  pairs.
- Per-scenario goldens byte-equal master.

---

### Phase 6 — Effect, weapon, treasure, generator, event coverage

**Phase Name**: Close all remaining coverage gaps.

**Implement Phase ID**: `06-residual-coverage-scenarios`

**Verification Phases**:
- `06a-check-residual-coverage` (`bounce_target: 06-residual-
  coverage-scenarios`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` (all coverage-gate
  cases, no narrower filter) and asserts every case passes
  with **zero** uncovered targets across walker families,
  effect families, weapon families, treasure families,
  generator families, specials, and event kinds.
- `06b-check-event-kind-coverage` (`bounce_target: 06-residual-
  coverage-scenarios`): runs
  `python3 scripts/parity/audit_event_coverage.py
  tests/parity/golden/` (new script) which loads every golden,
  unions the `events[*].kind` values, and asserts the union
  equals the full set of emitted `EventKind` symbols
  (`kRequiredEventKinds[]`).
- `06c-check-byte-equal-vs-master` (`bounce_target: 06-residual-
  coverage-scenarios`): per-scenario diff against
  `tests/parity/golden/effect_*.json`, `weapon_*.json`,
  `treasure_*.json`, `generator_*.json`, `event_*.json`.

**Preexisting Inputs**:
- Outputs of Phases 04-05.
- `tests/parity/coverage_targets.h` for the residual target
  sets.
- `src/gameplay/effect_family_registry.cpp`
- `src/resources/save_data.cpp` (for on-disk save coverage)

**New Outputs**:
- Effect scenarios (~13): `effect_expand`, `effect_ghost_scare`,
  `effect_bomb`, `effect_explosion`, `effect_flash`,
  `effect_magic_shield`, `effect_knife_back`,
  `effect_boomerang`, `effect_cloud`, `effect_marker`,
  `effect_chain`, `effect_door_open`, `effect_hit`. Some are
  implicit consequences of other scenarios (the family/special
  phase coverage check may show them already covered); Phase 06
  only adds the residue not covered by Phases 04-05.
- Weapon scenarios (~20): each `FAMILY_KNIFE..FAMILY_BOULDER`
  weapon family is exercised by an attacker who carries that
  weapon. Each scenario's `spawns[]` entry sets
  `SpawnSpec::default_weapon` (and `current_weapon` for the
  initial swing) to the target weapon family id, so
  `apply_post_load_spawns` overrides the carrier's loadout to
  the exact weapon family being tested. The carrier family is
  chosen to be one whose attack code path actually fires the
  weapon's emit-projectile logic (e.g. `FAMILY_ARCHER` for
  ranged weapons, `FAMILY_SOLDIER` for melee).
- Treasure scenarios (13): every treasure family in
  `include/openglad/core/constants.h:95-108`
  (`FAMILY_STAIN(0)..FAMILY_SPEED_POTION(12)`, with
  `MAX_TREASURE=12` defined inclusively as the highest index)
  is spawned via `spawns[]` and collected by a walker that
  walks onto it via scripted
  directional input. **There is no `treasure_collected`
  EventKind** — pickup is verified instead by the union of (a)
  the treasure walker's id no longer appearing in
  `dump.walkers[]` at the final tick (it was consumed) and (b)
  for value-bearing treasures, the post-pickup change in either
  the collector's stats (HP/MP for potions) or
  `dump.score_per_team` (for gold). The coverage gate keys on
  the absent-treasure observation and the `exercises` bit set
  by the scenario, not on an event-kind string.
- Generator scenarios (4): tent / tower / bones / treehouse.
  Spawn the generator, tick long enough for it to spawn a child
  walker, verify the child appears in `walkers[]`.
- Save round-trip scenario (real on-disk):
  `save_roundtrip_disk_99` loads scen99, runs 20 ticks, calls
  `level.save()` to write a real `.glad` archive into the PhysFS
  write directory, opens the archive, deserialises into a fresh
  `LevelRuntimeData`, dumps, and asserts the resulting dump
  matches the in-memory dump byte for byte. The golden captures
  the in-memory dump (master does the same on its side; both
  must round-trip the same way).
- Exit-trigger scenario (real): `exit_trigger_real_9302` walks
  the player to the exit tile (script generated by reading the
  scen9302 map metadata to compute the exit position), captures
  the `level_exited` event with the correct `next_level`.
- Replay / determinism reseed scenario:
  `rng_reseed_after_load_99` — the seed-after-load contract
  added in Phase 02 must be tested: load the scenario with seed
  A, re-seed to B post-load, tick; compared to loading with seed
  B and not re-seeding, the post-tick dumps must differ. Both
  sides must agree which one is "canonical".
- Event-emission scenarios for `EventKind` values not naturally
  emitted by family/special scenarios. The required set is
  exactly the nine non-`None` `EventKind` values listed in
  `kRequiredEventKinds[]` (Phase 03):
  - `play_sound` — covered by any combat scenario; the coverage
    audit just confirms at least one golden's `events[]` contains
    it.
  - `notification` — exercised by a cleric heal or a yell
    (`K_NONE | (1u<<KEY_YELL)`) scenario.
  - `set_palette` — exercised by a `FAMILY_GHOST` scare scenario
    (ghost palette effect).
  - `request_redraw` — exercised by any scenario that loads a
    level and ticks at least once; verified present by audit.
  - `end_game`, `set_end`, `request_exit_confirmation`,
    `withdraw_to_level` — covered by the `exit_trigger_real_9302`
    scenario which walks the player to the exit tile and ticks
    through the level-done handlers.
  - `score_change` — covered by any kill scenario in a scoring-
    active arena (e.g., the `family_soldier_scen99` kill in
    Phase 04 may already cover it; audit decides).
- Mirror table entries in
  `../openglad-master/tools/parity_scenario_table.h`, committed
  to the master `parity-companion` branch in this phase.
- Goldens for every new scenario, captured **in this phase**
  via the rebuilt master companion
  (`scripts/parity/capture_master_golden.sh
  tests/parity/golden/` filtered to the new scenario ids),
  schema-validated, and committed. Phase 07 re-captures into a
  throwaway directory and asserts zero diffs.
- `.plan/parity-coverage-manifest.md` final state — all rows
  filled.

**File Changes**:
- Modified: `tests/parity/scenario_table.h`
- Modified: `../openglad-master/tools/parity_scenario_table.h`
- New: numerous `tests/parity/golden/*.json` files
- New: `scripts/parity/audit_event_coverage.py`
- Modified: `tests/parity/test_parity_scenarios.cpp`
- Modified: `.plan/parity-coverage-manifest.md`
- Modified: `.plan/parity-harness-design.md` (final scenario list
  count updated; coverage matrix replaced with a pointer to the
  manifest)

**Implementation Details**:
- The runner's `apply_post_load_spawns` must support spawning
  weapons (`Order::WEAPON`) and treasures (`Order::TREASURE`) in
  addition to LIVING / GENERATOR. The `SpawnSpec::order` field
  added in Phase 02 covers this.
- For weapon coverage, the spawn entry sets one or both of the
  new `SpawnSpec::default_weapon` / `SpawnSpec::current_weapon`
  fields introduced in Phase 02 to the target weapon family id
  whenever the carrier family's default loadout doesn't match
  the weapon family this scenario must exercise.
  `apply_post_load_spawns` then calls
  `walker->set_default_weapon(spawn.default_weapon)` and
  `walker->set_current_weapon(spawn.current_weapon)` (the
  dirty-field setters at
  `include/openglad/gameplay/walker.h:170-171`) for any
  non-zero field. The same two-field convention is mirrored on
  the master side via `walker::set_default_weapon` /
  `walker::set_current_weapon` (verified present on the
  `parity-companion` branch). There is no `walker::weapon_type`
  member; do not invent one.
- Save round-trip on disk requires PhysFS write-dir setup which
  Phase 02's bootstrap already provides; the archive lives in
  `${CMAKE_BINARY_DIR}/parity-write/`.
- The agent must commit before yielding.

**Verification**:
- All new parity tests pass.
- `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` runs every coverage-
  gate case and reports zero uncovered targets across every
  category.
- `python3 scripts/parity/audit_event_coverage.py` exits 0.

---

### Phase 7 — Master re-capture verification and regression triage

**Phase Name**: Verify (do not overwrite) goldens, classify divergences, fix.

**Boundary clarification — Phase 04-06 vs Phase 07 captures**.
Each of Phases 04, 05, 06 produces its tranche of goldens by
running the rebuilt `parity_dump_master` companion **once** (at
that phase's master HEAD), writing the JSON into
`tests/parity/golden/<id>.json`, validating it with
`scripts/parity/validate_schema.py`, and committing the file with
the rest of that phase's work. Those goldens are **canonical**,
not draft — once committed they are not regenerated by any later
phase except Phase 07's audit (below).

Phase 07 does **not** re-run `capture_master_golden.sh` over the
existing golden set in place. Instead, it captures into a
**throwaway directory** (`/tmp/golden-rebuild/`) and runs
`diff -r tests/parity/golden/ /tmp/golden-rebuild/` against the
committed set. The expected outcome is **zero diffs**: any diff
is a real divergence (either the branch drifted since Phase 04-06
committed its tranche, or the master companion drifted, or a
non-determinism leaked into the schema). The agent classifies
each diff per `.plan/parity-fixes.md` (regression / intended_diff)
and applies a branch-side code fix or records an intended_diff
row citing a branch commit SHA. Re-writing a committed golden to
silence a diff without first classifying it is explicitly
forbidden by contract item #9; the Phase 07 verifier rejects any
commit that modifies a `tests/parity/golden/*.json` file unless
the same commit also touches `.plan/parity-fixes.md` with an
`intended_diff:` row that cites a branch commit SHA introducing
the intended behaviour change.

The `master commit SHA` against which the goldens are pinned
is captured by Phase 02 (the master companion rebuild) and
recorded in `.plan/parity-coverage-manifest.md`'s frontmatter
as `master_companion_sha:` so Phases 04-06 and Phase 07 all
diff against the same fixed master. If a later phase needs to
re-rebuild the master companion (e.g., to pull in a schema
fix), the agent updates the recorded SHA in the manifest and
the Phase 07 verifier re-runs the capture against the new SHA;
any drifted golden then becomes a real diff that Phase 07
must classify.

**Implement Phase ID**: `07-master-capture-and-fix`

**Verification Phases**:
- `07a-check-all-goldens-present` (`bounce_target: 07-master-
  capture-and-fix`): runs
  `./build/ci-test/parity_runner_smoke --list | wc -l` to
  obtain the expected scenario count (the smoke runner from
  Phase 02 is extended in Phase 07 to support `--list`, printing
  one scenario id per line). Asserts that `ls
  tests/parity/golden/*.json | wc -l` equals that count and
  that for every listed scenario id there is exactly one
  matching `tests/parity/golden/<id>.json`. Asserts no golden
  is "empty" via `python3
  scripts/parity/audit_event_coverage.py --reject-empty-walkers`
  applied to every scenario whose `tick_budget > 1` and whose
  `is_intentionally_empty` flag (new boolean on `ScenarioSpec`,
  default false) is false.
- `07b-check-parity-clean` (`bounce_target: 07-master-capture-
  and-fix`): runs `ctest --preset ci-test -R '^og_test_parity'`
  and asserts 100% pass. Any failure must be resolved by an
  explicit branch-side code fix (with a commit message tagged
  `parity-fix:`) or an `intended_diff` row in
  `.plan/parity-fixes.md` citing a branch commit that authored
  the change. The verifier rejects the phase if
  `.plan/parity-fixes.md` claims an `intended_diff` whose cited
  commit doesn't exist in `git log`.
- `07c-check-noop-perturbation` (`bounce_target: 07-master-
  capture-and-fix`): runs an automated canary that perturbs a
  single gameplay constant on the branch side under `git stash`,
  recompiles `og_test_parity`, and asserts at least one parity
  test now **fails**. Restores the stash. This proves the
  harness is now load-bearing — the previous fraud could not
  detect any perturbation because the goldens were
  content-free.

**Preexisting Inputs**:
- All outputs of Phases 04-06.
- `scripts/parity/capture_master_golden.sh` (extended to drive
  every scenario, not just the original 15)
- `../openglad-master/build/ci-test/parity_dump_master` rebuilt
  under Phase 02's runner
- `.plan/parity-fixes.md` (Phase 07 of the prior workflow;
  reused as the divergence log, rewritten to reflect real fixes)

**New Outputs**:
- `.plan/parity-fixes.md` rewritten with real divergence
  classifications observed by re-capturing the master companion
  into a throwaway directory and diffing against the committed
  `tests/parity/golden/` tree. Each row is either `regression`
  (with a code fix commit SHA on the branch) or `intended_diff`
  (with the branch commit SHA authorising the behaviour change).
  The "no divergences observed vacuously" wording from the
  prior (fraudulent) version is forbidden. If the re-capture
  produces zero diffs, `.plan/parity-fixes.md` says so
  explicitly and lists the scenario count it diffed.
- Any source-code fixes needed to restore parity (in `src/` or
  `include/`). These fixes are committed; the committed
  golden files are **not** overwritten by Phase 07 except via
  the contract-#9-compliant intended_diff path described in the
  boundary clarification above. The Phase 04-06 goldens are the
  canonical artifacts; Phase 07 is a verification pass.
- `.plan/parity-coverage-manifest.md` — annotated with the
  final committed master companion SHA (read from the manifest
  frontmatter set in Phase 02) and the branch HEAD SHA at the
  time of Phase 07's re-capture.

**File Changes**:
- Modified: `scripts/parity/capture_master_golden.sh` (drive
  every scenario; output to a configurable destination
  directory rather than always writing into
  `tests/parity/golden/`, so Phase 07 can use a throwaway dir
  for its re-capture diff)
- Modified (rare): `tests/parity/golden/*.json` — only via the
  intended_diff path, never as a blind re-capture. A commit
  that modifies any golden file in Phase 07 must also modify
  `.plan/parity-fixes.md` with the matching intended_diff row
  citing a branch commit SHA.
- Modified: `.plan/parity-fixes.md`
- Modified: `.plan/parity-coverage-manifest.md` (final SHAs)
- Possibly modified: `src/gameplay/*`, `src/interface/*`, etc.,
  if real regressions surface

**Implementation Details**:
- `capture_master_golden.sh` accepts a destination directory
  argument (e.g. `/tmp/golden-rebuild/`), iterates
  `${master}/build/ci-test/parity_dump_master --list`, and
  writes one JSON per scenario into that destination. The
  script `validate_schema.py`s each dump and aborts on a
  malformed golden (cheap defence against the master companion
  regressing). Phase 07 invokes it with a throwaway directory;
  Phases 04-06 invoked it (per phase) with the committed
  `tests/parity/golden/` as the destination when they first
  captured their tranche.
- The Phase 07 re-capture sequence is:
  1. `mkdir -p /tmp/golden-rebuild && rm -rf /tmp/golden-rebuild/*`.
  2. `(cd ../openglad-master && cmake --build --preset ci-test
      --target parity_dump_master)`.
  3. `scripts/parity/capture_master_golden.sh /tmp/golden-rebuild/`.
  4. `diff -r tests/parity/golden/ /tmp/golden-rebuild/`.
- When a diff fires, the agent's first move is to read the diff
  (which entity/event/field), inspect the branch-side code path
  that produced the field, and either (a) fix the branch code,
  or (b) classify it as `intended_diff` only if a branch commit
  (cited by SHA) explicitly changed the behaviour. The workflow
  contract item #9 forbids silently re-capturing the golden to
  mask the regression; the verifier `07b` enforces this by
  rejecting any commit that modifies
  `tests/parity/golden/*.json` without a matching
  `.plan/parity-fixes.md` intended_diff row.
- The "noop perturbation" canary in `07c` exists because the
  prior signoff included a "manual canary" procedure that could
  never have triggered. The Phase 07 verifier automates the
  procedure: it `git stash`es, applies
  `sed -i 's/- tempdamage_i)/- (tempdamage_i + 1))/'
  src/gameplay/walker_combat.cpp` (a one-line damage-by-one bump
  on the verified statement at `walker_combat.cpp:302`),
  rebuilds `og_test_parity`, runs the suite, and requires a
  non-zero exit (at least one parity test fails). It then
  `git checkout -- src/gameplay/walker_combat.cpp` and
  `git stash pop` to restore the tree, and rebuilds to confirm
  the clean state still passes.
- The agent must commit before yielding.

**Verification**:
- 100% pass on `ctest --preset ci-test -R '^og_test_parity'`.
- No empty goldens.
- Canary perturbation breaks at least one test.

---

### Phase 8 — CI wiring and honest sign-off

**Phase Name**: Lock the gate, replace the fraudulent sign-off.

**Implement Phase ID**: `08-ci-and-honest-signoff`

**Verification Phases**:
- `08a-check-ci-yaml-runs-coverage` (`bounce_target: 08-ci-and-
  honest-signoff`): greps `.github/workflows/test.yml` for an
  explicit coverage-gate invocation. Because the coverage gate
  is implemented as GoogleTest cases inside the existing
  `og_test_parity` binary, the required CI line is
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` (the verifier accepts
  either that literal or an equivalent `ctest ... -R
  'Parity.coverage_gate'` form that selects the same cases via
  ctest's gtest-filter discovery). The verifier also greps for
  the `scripts/parity/check_coverage_manifest.py` invocation.
  Both must run *before* the final ctest pass so a missing
  target fails CI fast.
- `08b-check-signoff-honest` (`bounce_target: 08-ci-and-honest-
  signoff`): reads `.plan/parity-signoff.md` (newly written, not
  the renamed fraudulent one) and asserts the document:
  1. Cites a non-zero number of golden files and confirms each
     is non-empty (with `ls -l` evidence baked in).
  2. Lists every coverage category from the manifest with its
     observed-vs-required counts.
  3. Includes the literal output of `ctest --preset ci-test -R
     '^og_test_parity'` showing the full pass count.
  4. Does not contain the words "vacuously satisfied",
     "indirectly covered", or "not currently registered".
  5. Cites the master companion commit SHA and the branch HEAD
     SHA that the goldens were captured against.
- `08c-check-end-to-end-rebuild` (`bounce_target: 08-ci-and-
  honest-signoff`): on a clean tree (`rm -rf build/`), runs
  `cmake --preset ci-test && cmake --build --preset ci-test &&
  ctest --preset ci-test` end-to-end and asserts exit code 0.

**Preexisting Inputs**:
- Outputs of all prior phases.
- `.github/workflows/test.yml`
- `.plan/parity-coverage-manifest.md`
- `.plan/parity-redo-audit.md`
- `.plan/parity-fixes.md`
- `.plan/parity-signoff-fraudulent.md` (kept for historical
  reference; never cited as authoritative)

**New Outputs**:
- `.plan/parity-signoff.md` — rewritten from scratch. Required
  sections: tear-down summary (cites
  `.plan/parity-redo-audit.md`), real coverage table (cites
  `.plan/parity-coverage-manifest.md`), divergence ledger (cites
  `.plan/parity-fixes.md`), reproduction commands, canary
  perturbation evidence, master commit SHAs, branch HEAD SHA,
  full `ctest` output excerpt.
- `.github/workflows/test.yml` extended with two new CI steps:
  1. "Parity coverage manifest check" — runs
     `scripts/parity/check_coverage_manifest.py` before the
     build.
  2. "Parity coverage gate" — runs
     `./build/ci-test/og_test_parity
     --gtest_filter='Parity.coverage_gate*' --gtest_color=no`
     immediately after the build (the coverage-gate cases live
     inside the existing `og_test_parity` binary; no separate
     executable target exists).
  The existing parity-test step still runs the full
  `og_test_parity` group via `ctest --preset ci-test -R
  '^og_test_parity'`.

**File Changes**:
- New: `.plan/parity-signoff.md`
- Modified: `.github/workflows/test.yml`
- Modified: `.plan/parity-harness-design.md` — add a "Phase 08
  redo" section pointing at the rewritten sign-off and
  explicitly superseding any contradictory claim in the older
  sections.

**Implementation Details**:
- The agent rewriting the sign-off **must** run every command it
  cites before writing the document; the verifier
  (`08b-check-signoff-honest`) compares the cited counts against
  re-execution of the same commands and fails if the document
  contains stale numbers.
- The CI step ordering matters: manifest check (cheap) before
  build, coverage gate immediately after build, full parity
  suite as part of the normal ctest pass. Each step uses
  `if: always()` so a single failure surfaces all three.
- The agent must commit before yielding.

**Verification**:
- `.github/workflows/test.yml` has the two new steps.
- `.plan/parity-signoff.md` exists and passes the honesty grep
  rules.
- Clean rebuild + full ctest succeeds.

---

## 4. Critical Files

Comprehensive list of files touched, grouped by category.

### New source files (branch)

- `tests/parity/parity_bootstrap.{h,cpp}` — PhysFS + campaign +
  search-path setup; reused by every parity-test entry point.
- `tests/parity/scenario_runtime.{h,cpp}` —
  `scenario_level_id`, `apply_post_load_spawns`,
  `apply_inputs_at_tick`, `clear_world_entities`.
- `tests/parity/parity_runner_smoke_main.cpp` — small executable
  used by the Phase 02 verifier to diff branch vs master JSON
  without ctest.
- `tests/parity/coverage_targets.h` — required-coverage manifest
  in C++.
- `tests/parity/test_parity_coverage_gate.cpp` — runtime gate
  test.

### New scripts

- `scripts/parity/check_coverage_manifest.py` — static check
  against `include/openglad/core/constants.h` and `event.h`.
- `scripts/parity/audit_event_coverage.py` — verifies every
  emitted `EventKind` appears in at least one golden's `events[]`.

### Modified source files (branch)

- `tests/parity/parity_runner.{h,cpp}` — full rewrite to load
  scenarios, apply inputs, drop the `level_done` short-circuit.
- `tests/parity/scenario_table.h` — extended with `SpawnSpec`,
  `spawns[]`, `spawn_count`, `exercises`, `fresh_arena`,
  `player_team`; grows from 16 to ~100+ scenarios.
- `tests/parity/test_parity_scenarios.cpp` — one
  `OG_PARITY_TEST(idx, name)` per added scenario.
- `tests/parity/parity_test_main.cpp` — hosts PhysFS init via
  `parity_bootstrap`.
- `tests/parity/state_dump.{h,cpp}` — extend the schema-v1
  emitter with `level_done`, `level_tick_count` fields (still
  v1).
- `CMakeLists.txt` — link `parity_bootstrap`,
  `scenario_runtime`, `coverage_gate`; register
  `parity_runner_smoke`; copy `temp/scen/` and `builtin/` to the
  test working dir.
- `.github/workflows/test.yml` — coverage manifest + gate steps.

### Modified source files (master)

- `../openglad-master/tools/parity_dump_master.cpp` — full
  rewrite mirroring branch runner.
- `../openglad-master/tools/parity_dump_master_stubs.cpp` —
  additional headless stubs as needed for the now-loaded world.
- `../openglad-master/tools/parity_scenario_table.h` — mirror of
  branch table.
- `../openglad-master/tools/parity_bootstrap.{h,cpp}` (new) —
  master-side mirror of the branch bootstrap.
- `../openglad-master/tools/parity_scenario_runtime.{h,cpp}`
  (new) — master-side spawn/input application.
- `../openglad-master/tools/parity_dump_state.{h,cpp}` — sync
  with branch schema extensions.
- `../openglad-master/CMakeLists.txt` — link the new tools
  helpers.

### Modified or moved `.plan/` documents

- `.plan/parity-signoff.md` → `.plan/parity-signoff-fraudulent.md`
  (Phase 01)
- `.plan/parity-redo-audit.md` (new, Phase 01)
- `.plan/parity-coverage-manifest.md` (new, Phase 03;
  progressively filled by 04-06)
- `.plan/parity-fixes.md` (rewritten in Phase 07)
- `.plan/parity-divergence-report.md` (rewritten or appended in
  Phase 07 with the *real* divergences)
- `.plan/parity-harness-design.md` (amended in 02/03/06 with the
  load-path section, spawns schema, manifest pointer)
- `.plan/parity-signoff.md` (new, Phase 08)

### Golden files

- Phase 01 deletes the existing 15.
- Phases 04-06 each contribute a tranche; Phase 07 finalises the
  full set against the rebuilt master companion.
- Final count: equal to `kMasterComparableScenarioCount` derived
  at compile time from `scenario_table.h` (expected to be ~80-120
  depending on per-special / per-weapon decomposition).

## 5. Final Verification

After Phase 8 has completed and committed, the full pipeline is
verified by running, from a clean tree, in order:

1. **Coverage manifest static check** (must exit 0):
   ```
   python3 scripts/parity/check_coverage_manifest.py
   ```

2. **Clean build**:
   ```
   rm -rf build/
   cmake --preset ci-test
   cmake --build --preset ci-test
   ```

3. **Coverage gate** (must exit 0 and report zero uncovered
   targets). The coverage-gate cases live inside the existing
   `og_test_parity` binary; select them by gtest filter:
   ```
   ./build/ci-test/og_test_parity \
       --gtest_filter='Parity.coverage_gate*'
   ```

4. **Full parity suite** (must exit 0; every test passes):
   ```
   ctest --preset ci-test -R '^og_test_parity' --output-on-failure
   ```

5. **Full project suite** (must exit 0):
   ```
   ctest --preset ci-test --output-on-failure
   ```

6. **Master companion regenerates byte-equal goldens** (must
   exit 0 and produce zero diffs):
   ```
   (cd ../openglad-master && cmake --build --preset ci-test
     --target parity_dump_master)
   scripts/parity/capture_master_golden.sh /tmp/golden-rebuild/
   diff -r tests/parity/golden/ /tmp/golden-rebuild/
   ```

7. **Canary perturbation detects a regression** (must report at
   least one failing test, then revert). The damage line at
   `src/gameplay/walker_combat.cpp:302` is
   `stats_->set_hitpoints( stats_->hitpoints() - tempdamage_i);`
   — perturb `tempdamage_i` to `(tempdamage_i + 1)`. The
   verifier first asserts the working tree is clean
   (`git status --porcelain` produces no output) so the
   `git checkout --` revert at the end is sufficient:
   ```
   test -z "$(git status --porcelain)"   # must be clean
   sed -i 's/- tempdamage_i)/- (tempdamage_i + 1))/' \
       src/gameplay/walker_combat.cpp
   cmake --build --preset ci-test --target og_test_parity
   if ctest --preset ci-test -R '^og_test_parity' \
       --output-on-failure ; then
       echo "ERROR: canary failed to fire"; \
       git checkout -- src/gameplay/walker_combat.cpp; exit 1
   fi
   git checkout -- src/gameplay/walker_combat.cpp
   cmake --build --preset ci-test --target og_test_parity
   ```
   Phase 07's verifier wraps this same sequence; if the post-
   patch `ctest` exits 0, the harness is not load-bearing and
   the verifier rejects the phase.

8. **Sign-off honesty grep** (must exit 0):
   ```
   ! grep -qiE 'vacuously|not currently registered|indirectly covered' \
         .plan/parity-signoff.md
   grep -q "branch HEAD: $(git rev-parse HEAD)" .plan/parity-signoff.md
   ```

9. **CI workflow includes the new steps** (must exit 0):
   ```
   grep -q 'check_coverage_manifest.py' .github/workflows/test.yml
   grep -q "gtest_filter=['\"]Parity.coverage_gate" \
       .github/workflows/test.yml
   ```

Only when all nine of these pass is the work considered complete.
The Phase 08 verifier wraps steps 1-3 and 5-9; step 4 is wrapped
by Phase 07's verifier; step 6 is wrapped by Phases 04-07's
byte-equal checks.
