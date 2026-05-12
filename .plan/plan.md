# Plan: Actually deploy the gameplay-parity harness against master

## 1. Context

### What the previous workflow produced and why it is invalid

Commits `aaea7a28 .. 373965f4` claim to have built a "gameplay parity
comparison framework" against `../openglad-master`.
`.plan/parity-signoff.md` declares "Parity overall: **GREEN**" with
15 byte-equal goldens. The artifacts do not match the claim:

- **All 15 golden files in `tests/parity/golden/*.json` are empty
  worlds**:
  ```
  {"effects":[],"events":[],"rng_state":"0xNNNNNNNN",
   "schema_version":"v1","score_per_team":[0,0,0,0],
   "tick":1,"walkers":[]}
  ```
  Zero walkers, zero effects, zero events, tick=1, all-zero scores.
  Only `rng_state` varies (echo of the scenario's seed).

- **The branch runner never loads a scenario.**
  `tests/parity/parity_runner.cpp:25-47` constructs a bare
  `GameWorld(spec.rng_seed)`, sets `out.loaded = false`, and comments
  that scenario loading is "the Phase 06 task". Phase 06 was never
  completed.

- **The master companion is identically broken.**
  `../openglad-master/tools/parity_dump_master.cpp:62-91` uses a bare
  `GameWorld`, no `load_level`, the same deferral comment, and the
  same `if (world.level_done != 0) break;` shortcut that fires on
  tick 1 against an empty world — explaining why every golden is
  stuck at `tick: 1` even when `tick_budget` is 200 or 600.

- **Input scripts are dead code.**
  `tests/parity/parity_runner.cpp:9-19`'s `apply_inputs_at_tick` is
  `(void)spec; (void)tick;`. The `kInputs*` arrays in
  `tests/parity/scenario_table.h` are never read at runtime.

- **The "branch-internal" snapshot test is a tautology.**
  `test_parity_scenarios.cpp:37-49` runs the same empty scenario
  twice and asserts the two empty dumps are equal.

- **The "manual canary" cannot detect a regression.** Changing the
  seed only changes the echoed `rng_state`; no simulation runs.

The harness will return `pass` for any change short of breaking
`GameWorld`'s constructor.

### What this plan must accomplish

`.plan/goal.md`: *"Actually use the fucking gameplay parity
comparison against master in a wide variety of scenarios, ensuring
that cumulative coverage includes every single entity type, special
ability effect, attack type, and occurrence in the game. Everything
must be tested with no exceptions."*

Four obligations:

1. **Make the runner actually run a scenario.** Both branch
   (`tests/parity/parity_runner.cpp`) and master companion
   (`../openglad-master/tools/parity_dump_master.cpp`) must
   initialise PhysFS, mount the campaign archive, load the level
   identified by `spec.scenario_file`, apply each
   `(tick, player_id, key_mask)` from `spec.inputs` at the matching
   tick, and drive the world for the full `tick_budget` without
   bailing on `level_done` (or, if bailing is intentional, recording
   it in the dump). Dumps must contain non-empty `walkers[]` for any
   non-trivial scenario.

2. **Enumerate every coverage target.** All 21 walker families
   (`FAMILY_SOLDIER` through `FAMILY_TOWER1` in
   `include/openglad/core/constants.h:46-67`), all 20 weapon
   families (`FAMILY_KNIFE` through `FAMILY_BOULDER`, lines 73-92),
   all 13 effect families (registered in
   `src/gameplay/effect_family_registry.cpp:16`), every treasure
   family, every generator family (`FAMILY_TENT/TOWER/BONES/
   TREEHOUSE`), every special-ability index in `NUM_SPECIALS=6` per
   family that defines one, every melee/ranged attack path, and
   every emitted `og::sim::EventKind` value must be exercised by at
   least one scenario whose golden contains the evidence.

3. **Pin the union, not the individual scenarios.** Scenarios may be
   narrowly focused; the sum of captured state must hit every
   coverage target. A runtime gate (`og_test_parity_coverage`) must
   fail whenever a target loses its lone covering scenario.

4. **Make cheating expensive.** Every check phase must re-derive the
   evidence (re-run `ctest`, re-grep goldens, re-build the master
   companion) rather than re-reading a markdown report. Implement
   phases must commit to git before yielding so check phases see a
   known-good HEAD.

### Codebase facts the plan relies on

Inputs to the workflow, not things to rediscover:

- The headless platform initialiser at
  `src/platform/text/platform_headless.cpp:340-385` shows the
  minimum bootstrap: `og::resources::init(argv0) → set_write_dir →
  mount(user_path) → restore_default_campaigns() →
  mount_campaign_package_with_error("org.openglad.gladiator") →
  mount pix/sound/cfg`.

- `src/interface/level_runtime_data.cpp:831-883` shows
  `LevelRuntimeData::load()` resolves `scen{world().id}.fss` inside
  the mounted campaign. `scen/scen9301.fss` → `level_id = 9301`,
  `temp/scen/scen99.fss` → `level_id = 99`. `temp/scen/*.fss` must
  be copied into the campaign archive or `temp/scen/` must be added
  to the PhysFS search path — Phase 02 picks the simpler option
  after verifying campaign archive contents.

- `tests/test_network_fixture.h:313-327, 495-535` is the canonical
  example of headless level loading: construct
  `LevelRuntimeData(level_id, /*headless=*/true,
  &sdl_level_data_hooks())`, install a `ScopedGameplayContext`,
  call `level.load()`.

- `GameWorld::add_ob(Order, int family, bool atstart)`
  (`include/openglad/gameplay/game_world.h:154`) injects walkers
  post-load without authoring new `.fss` files. Both branch and
  master expose this API.

- Input plumbing: bare `GameWorld` has no public `input_state_`.
  The real-game pipeline goes through
  `src/platform/sdl/local_transport_shadow.cpp::local_transport_shadow_send_input()`
  (line ~1027), which drives each player walker's `keys_` bitmask.
  The harness bypasses the transport layer and writes directly via
  `walker->set_keys(uint32_t bitmask)` (declared by
  `OG_WALKER_DIRTY_FIELD` at
  `include/openglad/gameplay/walker.h:201`). Bitmask uses action
  indices from `include/openglad/interface/input.h:223-240`
  (`KEY_UP=0 .. KEY_CHEAT=15`, with `KEY_FIRE=8`, `KEY_SPECIAL=9`,
  `KEY_SPECIAL_SWITCH=11`). Phase 02 defines `K_NONE = 0`,
  `K_FIRE = 1u<<KEY_FIRE`, `K_SPECIAL = 1u<<KEY_SPECIAL`,
  `K_SPECIAL_SWITCH = 1u<<KEY_SPECIAL_SWITCH`, plus directional
  variants, in `tests/parity/scenario_table.h`. Both sides expose
  `walker::set_keys()` via the same dirty-field macro.

- `tests/parity/scenario_table.h` is mirrored byte-for-byte at
  `/home/yans/code/openglad-master/tools/parity_scenario_table.h`.
  The Phase 03 contract in `.plan/parity-harness-design.md`
  requires byte-identical mirroring; this plan honours it for every
  added scenario.

- `og::sim::EventKind` enumerates `PlaySound, Notification,
  SetPalette, RequestRedraw, EndGame, SetEnd,
  RequestExitConfirmation, WithdrawToLevel, ScoreChange` (and
  `None`). `tests/parity/state_dump.cpp` maps each to a symbolic
  name; the Phase 03 coverage gate keys on those symbols.

### Inputs already on disk (consumed, not regenerated)

- `.plan/goal.md` — user's instruction; never rewritten.
- `.plan/parity-risk-inventory.md` — kept as subsystem checklist.
- `.plan/parity-harness-design.md` — schema-v1 JSON shape and
  byte-equal-mirror rule kept; amended in place when coverage
  widens.
- `.plan/master-baseline.md`, `.plan/master-companion.md` —
  describe `../openglad-master` worktree, `parity-baseline-master`
  / `parity-companion` branches, and the `parity_dump_master`
  binary. Reused as-is.
- `tests/parity/state_dump.{h,cpp}` and
  `../openglad-master/tools/parity_dump_state.{h,cpp}` — canonical
  schema-v1 emitter. Schema unchanged; only the input world
  changes.
- `tests/parity/scenario_table.h` and
  `../openglad-master/tools/parity_scenario_table.h` — extended in
  place by Phases 03-06.
- `scripts/parity/capture_master_golden.sh`,
  `scripts/parity/diff_dumps.py`,
  `scripts/parity/validate_schema.py` — kept; Phase 07 extends if
  required.
- `../openglad-master/` worktree at parent
  `16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd` on branch
  `parity-companion`. Phase 02 rebuilds the companion binary;
  does not re-clone or rebase without explicit direction.
- `tests/parity/golden/*.json` — 15 files. Phase 01 deletes them
  first because they are false-positive baselines. Replacements
  captured in Phase 07 from the rewritten companion.

### What does *not* change

- The schema-v1 JSON shape and byte-for-byte branch/master table
  sync.
- Test code location (`tests/parity/`) and `og_test_parity` CMake
  registration (`CMakeLists.txt:1807`).
- The `../openglad-master` worktree path. No `git rebase` on
  master.
- `.plan/parity-risk-inventory.md` (read-only).

## 2. Generated Workflow Contract

1. **Linear execution only.** `linear: true`. No `parallel_groups`,
   no fan-out, no fan-in. Phases run in numeric `order` from 1 to N.

2. **Inline-only YAML.** `yaml_source_mode: inline-only`. The
   generated `workflow.yaml` contains every phase body inline. No
   top-level `include:`, no phase-level `prompt_file:`,
   `workflow_file:`, `workflow_dir:`, `checks:`, or any other YAML
   indirection. Each phase's `prompt:` is the complete agent
   instructions as a multiline string.

3. **No agent-guided bounce.** Each phase declares at most one
   `bounce_target`, a fixed string equal to the implement phase's
   id. No `bounce_targets:` list, no choose-between-these logic.

4. **Every verifier is a top-level `check` phase.** No
   verification hook embedded inside an implement phase. Pattern:

   ```
   N    implement (id: ##-name)            bounce_target: null
   N+1  check     (id: ##a-check-name)     bounce_target: ##-name
   ```

   A single implement phase may be followed by more than one check
   phase if the verification splits cleanly; each check has its
   own `bounce_target: ##-name` pointing back at the same
   implement.

5. **A verifier stays in its block.** A check phase never bounces
   anywhere except the immediately preceding implement phase in
   the same numeric block. No skipping back to an earlier block.

6. **Checks run commands, not reads.** Verifier shell commands
   (`cmake --build`, `ctest`, `scripts/parity/diff_dumps.py`,
   `grep` over goldens, `git log`, etc.) are written into the
   checker's `prompt:` literally, with the expected exit code and
   failure trigger spelled out. Verifiers are agent phases that
   run shell commands and decide pass/fail.

7. **Existing artifacts are reused, not regenerated.** Where this
   plan lists an item under `Preexisting Inputs`, the implement
   phase's prompt must read or modify it in place:
   - `.plan/parity-risk-inventory.md` is not re-derived.
   - `.plan/parity-harness-design.md` is amended in place (schema
     and mirror rule stay; scenario list and coverage matrix
     grow).
   - `../openglad-master` worktree, its `parity-companion`
     branch, and the existing `tools/parity_dump_state.{h,cpp}`
     emitter are reused. Master is rebuilt but not re-set-up.
   - `tests/parity/state_dump.{h,cpp}`, `scenario_table.h`,
     `parity_runner.{h,cpp}`, `test_parity_scenarios.cpp`,
     `parity_test_main.cpp`, `scripts/parity/*.{sh,py}`, and the
     `CMakeLists.txt` registration are extended in place.
   - Companion-side `tools/parity_dump_master*.cpp`,
     `tools/parity_dump_state.{h,cpp}`, and
     `tools/parity_scenario_table.h` are extended in place;
     `parity_scenario_table.h` is kept byte-for-byte synchronised
     with the branch table.

8. **Commit-before-yield.** Every implement phase's prompt
   contains a literal instruction to `git add` modified files and
   `git commit` with a descriptive message *before* yielding. The
   following check phase expects HEAD to contain the change.

9. **Fraud-resistant check semantics.** Every check phase asserts
   that the *content* of a produced artifact is non-trivial, not
   merely that it exists. Golden JSON must contain non-empty
   `walkers[]` for any scenario with `tick_budget > 1` whose
   intent is not a deliberately empty world; coverage matrices
   must include the full enumerated set; sign-off documents must
   cite specific scenario ids and golden file sizes, not
   self-referential "see Phase X".

10. **No new YAML source files outside `workflow.yaml`.** The
    generated workflow is one file. Auxiliary data (coverage
    manifests, scenario tables) lives in the project tree as
    normal source artifacts, not separate YAML includes.

## 3. Implementation Phases

Eight implement phases, each paired with one or more `check`
phases. Verifier counts per implement phase: `1, 2, 2, 2, 2, 3,
3, 3`. Total: `8 implement + 18 check = 26 phases`.

---

### Phase 1 — Audit and tear-down

**Phase Name**: Audit prior fraud and remove poisoned artifacts.

**Implement Phase ID**: `01-audit-and-teardown`

**Verification Phases**:
- `01a-check-audit-document` (type: `check`,
  `bounce_target: 01-audit-and-teardown`): runs
  `cat .plan/parity-redo-audit.md` and asserts the document lists,
  by file path, every one of the 15 deleted golden files, and
  that `ls tests/parity/golden/` returns no `.json` files.
  Asserts `.plan/parity-signoff.md` was deleted or moved to
  `.plan/parity-signoff-fraudulent.md`. Asserts
  `git log -1 --name-status` shows the deletions are committed.

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
- `.plan/parity-redo-audit.md` — fraud inventory. Required
  sections:
  (a) the byte-identical
  `{"effects":[],"events":[],...,"tick":1,"walkers":[]}` pattern
  shared by all 15 goldens (with one sample);
  (b) line-numbered citations from `parity_runner.cpp` and
  `tools/parity_dump_master.cpp` for the scenario-not-loaded
  no-op;
  (c) line-numbered citation from
  `parity_runner.cpp::apply_inputs_at_tick` for the empty body;
  (d) explanation of the `level_done`-on-empty-world short-
  circuit that keeps every golden at `tick: 1`;
  (e) the rename/deletion of `.plan/parity-signoff.md`.
- `.plan/parity-signoff-fraudulent.md` — original signoff renamed
  in place via `git mv` so it remains in history but is not
  authoritative.

**File Changes**:
- Delete `tests/parity/golden/*.json` (15 files).
- `git mv .plan/parity-signoff.md .plan/parity-signoff-fraudulent.md`
- Create `.plan/parity-redo-audit.md`.
- Commit with message `parity-redo: phase 01 — tear down
  fraudulent golden set and rename signoff`.

**Implementation Details**:
No source code is modified. The audit document must include
literal command outputs (e.g., `wc -c tests/parity/golden/*.json`
showing every file ≤ 200 bytes before deletion) and explicitly
state that future phases will reconstruct goldens from a fixed
master companion, not the current broken one.

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
- `02a-check-build-clean`
  (`bounce_target: 02-real-runner-and-companion`): runs
  `cmake --preset ci-test && cmake --build --preset ci-test
  --target og_test_parity parity_runner_smoke` and asserts both
  targets link. Then runs on master:
  `cd ../openglad-master && cmake --preset ci-test && cmake
  --build --preset ci-test --target parity_dump_master`. Both
  exit 0.
- `02b-check-smoke-nonempty`
  (`bounce_target: 02-real-runner-and-companion`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter=Parity.smoke_nonempty_scen99` and asserts pass
  (only passes if the runner loaded `temp/scen/scen99.fss` into a
  real `LevelRuntimeData` and ticked ≥ 50 times). Runs
  `../openglad-master/build/ci-test/parity_dump_master
  --scenario smoke_nonempty_scen99 --out /tmp/smoke.json` and
  asserts the JSON contains `"walkers":[` followed by at least
  one `{"alive":` element via
  `python3 -c "import json,sys;
  d=json.load(open('/tmp/smoke.json')); assert
  len(d['walkers'])>0 and d['tick']>=50, d"`.

**Preexisting Inputs**:
- `.plan/parity-redo-audit.md`
- `.plan/parity-harness-design.md` (schema reused unchanged)
- `tests/parity/parity_runner.{h,cpp}` (to be rewritten)
- `tests/parity/parity_test_main.cpp` (PhysFS init lands here or
  in a runner setup helper)
- `tests/parity/scenario_table.h` (extended with one smoke
  scenario)
- `tests/parity/test_parity_scenarios.cpp` (extended with one
  smoke test entry)
- `tests/parity/state_dump.{h,cpp}` (schema unchanged; emitter
  reused)
- `tests/test_network_fixture.h:495-535` (reference for headless
  `LevelRuntimeData` setup)
- `src/platform/text/platform_headless.cpp:340-385` (reference
  for PhysFS + campaign bootstrap)
- `src/interface/level_runtime_data.cpp:831-883` (reference for
  scenario-file resolution)
- `../openglad-master/tools/parity_dump_master.cpp` (to be
  rewritten)
- `../openglad-master/tools/parity_dump_master_stubs.cpp` (may
  need more stubs once a real world is loaded)
- `CMakeLists.txt` (parity-test wiring at line 1807)
- `../openglad-master/CMakeLists.txt`

**New Outputs**:
- `tests/parity/parity_bootstrap.{h,cpp}`:
  1. Calls `og::resources::init(argv0)`.
  2. Sets writable scratch directory under
     `${CMAKE_BINARY_DIR}/parity-write/`.
  3. Calls `restore_default_campaigns()` then
     `mount_campaign_package_with_error("org.openglad.gladiator")`.
  4. Adds `temp/scen/` and `scen/` to the PhysFS search path so
     scenario files at either root resolve as `scen{id}.fss`.
     Phase 01 audit must confirm whether the campaign archive
     already contains `scen99.fss`; if so this is a no-op,
     otherwise the bootstrap mounts the project-tree directories
     directly.
  5. Installs `sdl_level_data_hooks()` (declared at
     `include/openglad/resources/level_data_hooks.h:37`)
     unchanged — same accessor
     `tests/test_network_fixture.h:498` passes to a headless
     `LevelRuntimeData`, verified safe under `headless=true`. No
     new `parity_level_data_hooks()` is introduced.
  6. Teardown that unmounts and `og::resources::deinit`s.

  Plus a `BootstrapScope` RAII struct so each test entry is
  self-contained.

  Mirrored at `../openglad-master/tools/parity_bootstrap.{h,cpp}`
  and linked into `parity_dump_master`. Kept synchronised.

- `tests/parity/scenario_runtime.{h,cpp}`:
  - `int scenario_level_id(string_view scenario_file)` strips
    directory components, parses the integer between `scen` and
    `.fss` in the basename (`scen/scen9301.fss` → 9301,
    `temp/scen/scen99.fss` → 99). Aborts on unparsable paths.
  - `void apply_post_load_spawns(GameWorld&, const ScenarioSpec&)`
    iterates `spec.spawns[]` and calls
    `world.add_ob(static_cast<Order>(spawn.order), spawn.family,
    /*atstart=*/true)` (signature at
    `include/openglad/gameplay/game_world.h:154`). For each
    returned non-null `walker*` it sets position via
    `set_xpos/set_ypos` and team via `set_team_num` /
    `set_real_team_num` (branch: dirty-field setters; master:
    public fields).
  - `void apply_inputs_at_tick(GameWorld&, const ScenarioSpec&,
    uint32_t tick)` walks `spec.inputs[]`, finds entries whose
    `tick == current tick`, identifies the target walker (first
    walker whose `team_num() == spec.player_team`), calls
    `walker->set_keys(entry.key_mask)`. `key_mask == K_NONE`
    clears all keys. Master's walker exposes the same setter.
  - `void clear_world_entities(GameWorld&)` — used by
    `fresh_arena` scenarios.

- Extension of `ScenarioSpec` in
  `tests/parity/scenario_table.h`:
  - `const SpawnSpec* spawns; std::size_t spawn_count;`.
  - `struct SpawnSpec { std::int32_t family; std::uint8_t team;
    std::uint8_t order; std::int32_t x; std::int32_t y;
    std::uint16_t default_weapon; std::uint16_t current_weapon; };`.
    `order` is `static_cast<std::uint8_t>(Order::X)` selecting
    one of the values of `enum class Order` from
    `include/openglad/core/order.h:12-20`
    (`Living=0, Weapon=1, Treasure=2, Generator=3, FX=4,
    Special=5, Button1=6`). Phase 02 introduces wrapper helpers
    `kOrderLiving`, `kOrderWeapon`, `kOrderTreasure`,
    `kOrderGenerator`, `kOrderFX`. No bitwise OR; each
    `SpawnSpec` picks one. The two weapon fields hold a walker-
    family id (range `FAMILY_KNIFE..FAMILY_BOULDER` from
    `include/openglad/core/constants.h:73-92`); sentinel `0`
    means "leave default untouched". `default_weapon` overrides
    persistent loadout; `current_weapon` overrides the
    currently-wielded weapon for the first attack.
    `apply_post_load_spawns` calls
    `walker->set_default_weapon(...)` and/or
    `walker->set_current_weapon(...)`
    (`include/openglad/gameplay/walker.h:170-171`) only when the
    field is non-zero.
  - The 16 existing scenarios get `spawns = nullptr;
    spawn_count = 0;`.
  - `std::uint8_t player_team = 0;` so `apply_inputs_at_tick`
    can identify the target walker. Existing scenarios default
    to 0.
  - `bool is_intentionally_empty = false;` — used by Phase 07's
    empty-walkers audit to whitelist deliberately empty worlds.
    Any scenario setting it true must include a one-line
    justification comment.
  - `bool fresh_arena = false;` — when true, the runner calls
    `scenario_runtime::clear_world_entities(world)` between
    `level.load()` and `apply_post_load_spawns(...)` so Phase
    04-06 scenarios start from an empty arena. Existing
    scenarios default false.
  - `std::uint64_t exercises = 0;` — bitmask over a new
    `enum class Exercises : std::uint64_t`. Phase 02 declares
    the enum with `None = 0` and leaves it otherwise empty.
    Phases 03-06 extend the enum with one bit per coverage
    target not structurally observable (input-triggered
    specials, treasure pickups, etc.).

- New smoke scenario `smoke_nonempty_scen99`: `scenario_file =
  "temp/scen/scen99.fss"`, `tick_budget = 100`, `inputs =
  nullptr`, `spawns` empty (relies on populated arena). Plus
  scripted variant `smoke_nonempty_scen99_inputs`, identical
  except `inputs = { {1, 0, K_FIRE}, {3, 0, K_NONE} }`. Both must
  produce non-empty walker dumps whose position/keys fields
  differ between the two.

- Rewrite of `parity_runner.cpp::run_scenario`:
  ```
  // `cfg` is the global `extern cfg_store cfg;` declared in
  // `include/openglad/resources/gparser.h:38`. Runner must
  // `#include <openglad/resources/gparser.h>` and link
  // `og_resources`; do NOT default-construct a local cfg_store.
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
          return out;          // verifier flags load_failed
      }
      out.loaded = true;
      GameWorld& world = level.world();
      world.rng_.state_ = spec.rng_seed;   // re-seed AFTER load
      if (spec.fresh_arena) {
          clear_world_entities(world);
      }
      apply_post_load_spawns(world, spec);
      for (uint32_t t = 0; t < spec.tick_budget; ++t) {
          apply_inputs_at_tick(world, spec, t);
          world.tick();
          // Do NOT break on level_done; record it in the dump.
      }
      out.dump = capture_state_dump(world, &events);
      return out;
  }
  ```
  Three-argument `LevelRuntimeData` ctor is at
  `include/openglad/interface/level_runtime_data.h:211`
  (`int, bool, LevelDataHooks*`). Master exposes the same
  overload.

- Mirror rewrite of
  `../openglad-master/tools/parity_dump_master.cpp::run` against
  master's headers. Master may differ in member names
  (e.g. `walker->team_num` vs `walker->team_num()`); the rewrite
  must compile against master's actual surface.

- `parity_runner_smoke` — small CMake executable target that
  links bootstrap + runner and emits JSON for a named scenario
  to stdout. Lets the Phase 02 verifier diff branch/master JSON
  without the full GoogleTest harness.

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
  newly-required headless stubs),
  `../openglad-master/tools/parity_scenario_table.h` (mirror),
  `../openglad-master/CMakeLists.txt`.
- Modified: `.plan/parity-harness-design.md` — append "Phase 02
  redo: load path" section documenting the real load/tick/inject
  pipeline and the new `spawns[]` schema field. Schema-v1 JSON
  unchanged.

**Implementation Details**:
- PhysFS init happens exactly once per process. The
  `parity_test_main.cpp` SetUp performs it; per-test the runner
  uses a `ScopedGameplayContext` only.
- `LevelRuntimeData::load()` reads RNG state during decoration;
  the scenario's `rng_seed` is re-applied **after** `load()` so
  the tick loop starts from the canonical seed on both sides.
- `apply_inputs_at_tick`'s canonical "player walker" is the
  first walker whose `team_num == spec.player_team` (matches
  `add_network_player_character` in
  `test_network_fixture.h:393`). For the smoke scenario, the
  first walker loaded by `scen99.fss` is fine.
- Do **not** `break` on `level_done`; let the loop run the full
  `tick_budget`. The dump records `world.level_done` (public
  field at `game_world.h:214`) and `world.level_tick_count()`
  (public accessor at `game_world.h:161`; underlying
  `level_tick_count_` is private). Schema v1 gains these two
  fields; v1-compatible because the differ tolerates unknown
  keys per `.plan/parity-harness-design.md` ## Comparison rules.
- "Input was actually applied" is structural: the smoke
  scenario's scripted script is replayed against `scen99.fss`
  and the resulting JSON's walker positions must differ from
  the no-input baseline. Phase 02 adds the assertion to
  `test_parity_scenarios.cpp`.
- **Commit-before-yield, both worktrees.** Two commits before
  yielding:
  1. `git add <branch files> && git commit -m "parity-redo:
     phase 02 — real runner and schema-v1 fields"` in cwd.
  2. `git -C ../openglad-master add
     tools/parity_dump_master.cpp
     tools/parity_dump_master_stubs.cpp
     tools/parity_scenario_table.h
     tools/parity_bootstrap.h tools/parity_bootstrap.cpp
     tools/parity_scenario_runtime.h
     tools/parity_scenario_runtime.cpp
     tools/parity_dump_state.h tools/parity_dump_state.cpp
     CMakeLists.txt && git -C ../openglad-master commit -m
     "parity-companion: phase 02 — real load/tick/inject
     pipeline"`.
  The verifier runs `git -C ../openglad-master log -1
  --name-status` and asserts every expected file is listed.

**Verification**:
- `cmake --preset ci-test && cmake --build --preset ci-test
  --target og_test_parity parity_runner_smoke` exits 0.
- `cd ../openglad-master && cmake --preset ci-test && cmake
  --build --preset ci-test --target parity_dump_master` exits 0.
- `./build/ci-test/og_test_parity
  --gtest_filter=Parity.smoke_nonempty_scen99` passes.
- `python3 -c "import json;
  d=json.load(open('/tmp/smoke.json'));
  assert len(d['walkers']) > 0 and d['tick'] >= 50"` exits 0.
- `git log -1 --stat` (branch) shows runner changes committed.
- `git -C ../openglad-master log -1 --name-status` shows
  master-side companion changes committed on `parity-companion`;
  listed files must include `tools/parity_dump_master.cpp`,
  `tools/parity_bootstrap.cpp`, `tools/parity_scenario_runtime.cpp`.
  Clean working tree
  (`git -C ../openglad-master status --porcelain` empty)
  required.

---

### Phase 3 — Coverage manifest and gate

**Phase Name**: Define and enforce coverage taxonomy.

**Implement Phase ID**: `03-coverage-manifest-and-gate`

**Verification Phases**:
- `03a-check-manifest-completeness`
  (`bounce_target: 03-coverage-manifest-and-gate`): runs
  `python3 scripts/parity/check_coverage_manifest.py` (new),
  which reads `tests/parity/coverage_targets.h`, parses
  `include/openglad/core/constants.h` for every
  `inline constexpr int FAMILY_*`, and asserts the manifest
  enumerates every walker family (21), weapon family (20),
  effect family (13), treasure family, generator family, and
  emitted EventKind (parsed from
  `include/openglad/gameplay/event.h`). Exits non-zero on any
  missing entry. Asserts `specials[]` has one entry per
  `(family, special_index)` pair where `special_index in
  [1..NUM_SPECIALS-1]` and the family's descriptor in
  `src/gameplay/families/family_*.cpp` defines a non-null
  `do_special`.
- `03b-check-gate-fails-on-omission`
  (`bounce_target: 03-coverage-manifest-and-gate`): under `git
  stash`, removes one entry from `tests/parity/scenario_table.h`'s
  `kScenarios` (specifically the lone scenario supplying a
  required walker family — Phase 03's agent records which row
  in `.plan/parity-coverage-manifest.md`), rebuilds
  `og_test_parity`, runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'`, and asserts non-zero
  exit and that the failure output names the omitted target.
  The gate is a **runtime** check that iterates `kScenarios` in
  `SetUpTestSuite()` and compares observed-vs-required arrays;
  compile-failure is not an acceptable outcome — if the build
  itself fails, the verifier marks the phase failed. Restores
  the stash.

**Preexisting Inputs**:
- `tests/parity/scenario_table.h` (extended in Phase 02)
- `../openglad-master/tools/parity_scenario_table.h` (mirror)
- `include/openglad/core/constants.h`
- `include/openglad/gameplay/event.h`
- `src/gameplay/effect_family_registry.cpp`
- `src/gameplay/families/family_*.cpp`

**New Outputs**:
- `.plan/parity-coverage-manifest.md` — long-form table of every
  coverage target with a `covering_scenario_id` column.
  Initially most rows show `(none yet)`; Phases 04-06 fill them.
- `tests/parity/coverage_targets.h`:
  ```
  inline constexpr std::int32_t kRequiredWalkerFamilies[] = {
      FAMILY_SOLDIER, FAMILY_ELF, ..., FAMILY_TOWER1
  };
  inline constexpr std::int32_t kRequiredEffectFamilies[] = {...};
  inline constexpr std::int32_t kRequiredWeaponFamilies[] = {...};
  inline constexpr std::int32_t kRequiredTreasureFamilies[] = {...};
  inline constexpr std::pair<std::int32_t, std::uint8_t>
      kRequiredSpecials[] = { {FAMILY_ARCHMAGE, 1}, ... };
  // Symbol strings match
  // tests/parity/state_dump.cpp::kind_string (lines 134-147) for
  // og::sim::EventKind (include/openglad/gameplay/event.h:10-21).
  // "none" excluded (not emitted).
  inline constexpr std::string_view kRequiredEventKinds[] = {
      "play_sound", "notification", "set_palette",
      "request_redraw", "end_game", "set_end",
      "request_exit_confirmation", "withdraw_to_level",
      "score_change"
  };
  ```
- `tests/parity/test_parity_coverage_gate.cpp` — compiled
  **into the existing `og_test_parity` group binary** (added to
  its `og_add_test_group(...)` source list at
  `CMakeLists.txt:1807`; no new executable target). Cases under
  `Parity` GoogleTest suite, selectable via `--gtest_filter`:
  - `Parity.coverage_gate_walker_families`
  - `Parity.coverage_gate_specials`
  - `Parity.coverage_gate_effect_families`
  - `Parity.coverage_gate_weapon_families`
  - `Parity.coverage_gate_treasure_families`
  - `Parity.coverage_gate_event_kinds`
  - `Parity.coverage_gate` (umbrella requiring all the above)

  Each case runs every scenario in `kScenarios` once via
  `run_scenario`, collects the union of:
  - `walker.family` across `dump.walkers`
  - `effect.family` across `dump.effects`
  - `event.kind` across `dump.events`
  - per-scenario `spec.exercises` bits
  and asserts the corresponding required array is a subset of
  the observed union. On failure, prints a structured list of
  every uncovered target.

  The fixture builds the union once in `SetUpTestSuite()` and
  stores it in a static; every case reads the same cached
  observation.
- `scripts/parity/check_coverage_manifest.py` — pre-build
  static check; runs in CI before the C++ build. Reads
  `coverage_targets.h` and the constants header; asserts the
  former is a superset of every `FAMILY_*` in the latter.

**File Changes**:
- New: `.plan/parity-coverage-manifest.md`
- New: `tests/parity/coverage_targets.h`
- New: `tests/parity/test_parity_coverage_gate.cpp`
- New: `scripts/parity/check_coverage_manifest.py`
- Modified: `tests/parity/scenario_table.h` — extend
  `enum class Exercises : std::uint64_t` (Phase 02 left it with
  only `None = 0`) with one bit per coverage target that is not
  structurally observable (e.g. `Special_Archmage_1 = 1ULL<<0,
  Special_Cleric_1 = 1ULL<<1, ...`). The `exercises` field on
  `ScenarioSpec` was added in Phase 02; Phase 03 only widens the
  enum.
- Modified: `../openglad-master/tools/parity_scenario_table.h`
  (mirror).
- Modified: `CMakeLists.txt` — add
  `tests/parity/test_parity_coverage_gate.cpp` to the existing
  `og_add_test_group(parity ...)` source list at line 1807. No
  new `add_executable(...)`.
- Modified: `.plan/parity-harness-design.md` — append "Phase 03
  redo: coverage gate" section linking to the manifest and gate
  test. List v1-compatible spec extensions (`spawns`,
  `exercises`).

**Implementation Details**:
- The manifest is the source of truth for "what must be tested".
  Adding a family to `constants.h` without updating the manifest
  breaks the static check; adding a manifest entry without a
  covering scenario breaks the runtime gate.
- `kRequiredSpecials[]` is populated by parsing each
  `src/gameplay/families/family_*.cpp` for `set_do_special(...)`
  bindings: `grep -rn "set_do_special\|do_special_"
  src/gameplay/families/` records each `(family, idx)`. If
  parsing is brittle, the manifest may instead enumerate per
  family-descriptor `special_count` and require
  `(family, 1..special_count)`.
- `kRequiredEventKinds[]` reads
  `include/openglad/gameplay/event.h`'s `enum class EventKind`
  and removes `None`.
- The agent must commit before yielding.

**Verification**:
- `python3 scripts/parity/check_coverage_manifest.py` exits 0.
- `cmake --build --preset ci-test --target og_test_parity`
  succeeds.
- `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` **fails** at runtime
  (Phases 04-06 fill coverage); the verifier asserts the
  failure message names every uncovered target.
- `03b` stash flip-test shows the gate also fails at runtime
  when a previously-covering scenario is removed.

---

### Phase 4 — Walker-family scenarios (21 families)

**Phase Name**: One byte-equal scenario per walker family.

**Implement Phase ID**: `04-walker-family-scenarios`

**Verification Phases**:
- `04a-check-family-coverage`
  (`bounce_target: 04-walker-family-scenarios`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate_walker_families'` and
  asserts every entry in `kRequiredWalkerFamilies[]` is present
  in the union of `dump.walkers[*].family` across the 21 new
  scenarios. Cross-checks
  `ls tests/parity/golden/family_*.json | wc -l == 21`.
- `04b-check-byte-equal-vs-master`
  (`bounce_target: 04-walker-family-scenarios`): for each of
  the 21 family scenarios, runs
  `../openglad-master/build/ci-test/parity_dump_master
  --scenario family_<id> --out /tmp/family_<id>.json` and
  `diff -q` against `tests/parity/golden/family_<id>.json`. Any
  divergence fails the check (Phase 07 classifies as regression
  vs. intended_diff).

**Preexisting Inputs**:
- `tests/parity/scenario_table.h` (extended in Phases 02-03)
- `tests/parity/coverage_targets.h`
- `tests/parity/scenario_runtime.{h,cpp}`
- `../openglad-master/tools/parity_scenario_table.h`
- `scen/scen99.fss` (blank arena base for spawn injection)
- `scripts/parity/capture_master_golden.sh`

**New Outputs**:
- 21 new `ScenarioSpec` entries in `kScenarios`, one per family
  `FAMILY_SOLDIER..FAMILY_TOWER1`. Naming:
  `family_<symbolic_lowercase>_scen99` (e.g.
  `family_soldier_scen99`, `family_archmage_scen99`).
  - Base scenario file: `temp/scen/scen99.fss` for combat-
    capable families; `scen/scen9301.fss` for families needing a
    wider map (slimes, generators).
  - `spawns[]`: two walkers — target family on team 0 at
    `(120, 120)`, `FAMILY_SOLDIER` on team 1 at `(180, 120)` as
    a sparring partner. Generators get an additional `spawns[]`
    entry of the corresponding generator family on team 1.
  - `inputs[]`: `{ {5, 0, K_FIRE}, {64, 0, K_NONE} }` — target
    attacks at tick 5 and stops at tick 64. Input target is the
    first walker on `spec.player_team = 0`. Non-combatant
    generators: `inputs` empty.
  - `tick_budget`: 150 (gives slow families like BIG_ORC /
    GOLEM time to engage).
  - `exercises`: appropriate bit flag.
- Mirror entries in
  `../openglad-master/tools/parity_scenario_table.h`. Committed
  to the master companion's `parity-companion` branch in this
  phase (`git -C ../openglad-master commit ...`).
- 21 new golden files in `tests/parity/golden/family_*.json`
  captured **in this phase** via the rebuilt master companion
  (`scripts/parity/capture_master_golden.sh
  tests/parity/golden/` filtered to the 21 new scenario ids),
  schema-validated, and committed. Canonical; Phase 07
  re-captures into a throwaway directory and asserts zero diffs.
- `.plan/parity-coverage-manifest.md` updated with
  `covering_scenario_id` filled for the 21 walker families.

**File Changes**:
- Modified: `tests/parity/scenario_table.h`
- Modified: `../openglad-master/tools/parity_scenario_table.h`
- New: 21 `tests/parity/golden/family_*.json` files
- Modified: `.plan/parity-coverage-manifest.md`
- Modified: `tests/parity/test_parity_scenarios.cpp` — add
  `OG_PARITY_TEST(N, family_<name>_scen99)` lines.

**Implementation Details**:
- Some families may behave non-deterministically in their first
  150 ticks if the loaded scenario's RNG-consuming code paths
  fire before spawn injection. The post-load
  `world.rng_.state_ = spec.rng_seed` re-seed (Phase 02)
  prevents this, but Phase 04 must additionally clear
  `world.oblist` of pre-existing walkers when a clean per-
  family probe is needed. Phase 04 uses the `fresh_arena =
  true` field (Phase 02), which causes the runner to call
  `scenario_runtime::clear_world_entities(world)` between
  `level.load()` and `apply_post_load_spawns(world, spec)`.
  Phase 04 introduces no new spec field; only sets
  `fresh_arena = true` on family scenario rows.
- Family-specific gotchas:
  - `FAMILY_SLIME` / `FAMILY_SMALL_SLIME` /
    `FAMILY_MEDIUM_SLIME`: on death, slime splits; the dump
    captures split products — also covers the split code path.
  - `FAMILY_GHOST`: ghost-scare effect (`FAMILY_GHOST_SCARE`)
    fires on enemy contact; also covers an effect family from
    Phase 06.
  - `FAMILY_TOWER1` is a static turret; will not move but fires
    weapons — covers weapon spawn paths.
- The agent must commit before yielding.

**Verification**:
- `./build/ci-test/og_test_parity
  --gtest_filter='Parity.family_*'` passes (21 tests).
- 21 goldens exist, each contains `"walkers":[{...}]` with
  ≥ 2 entries and `"tick": 150`.
- Coverage-gate failures no longer mention any `FAMILY_*`
  walker family.

---

### Phase 5 — Special-ability scenarios (every family × every special index)

**Phase Name**: Per-family per-special-index coverage.

**Implement Phase ID**: `05-special-ability-scenarios`

**Verification Phases**:
- `05a-check-specials-coverage`
  (`bounce_target: 05-special-ability-scenarios`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate_specials'` and asserts
  every `(family, special_index)` pair in `kRequiredSpecials[]`
  is observed (by `exercises` bit or by an effect/event known
  to be produced).
- `05b-check-byte-equal-vs-master`
  (`bounce_target: 05-special-ability-scenarios`): per-scenario
  diff against `tests/parity/golden/special_*.json` (regenerated
  by master companion in this phase).

**Preexisting Inputs**:
- Outputs of Phase 04.
- `tests/parity/coverage_targets.h::kRequiredSpecials[]`
- `src/gameplay/families/family_*.cpp`
- `../openglad-master/src/gameplay/families/family_*.cpp`

**New Outputs**:
- ~40-60 new `ScenarioSpec` entries (one per `(family,
  special_index)`; upper bound `NUM_SPECIALS=6 × NUM_FAMILIES=
  21 = 126`, but only families with `do_special` bindings have
  specials; Phase 03 manifest enumerates the real set).
- Naming: `special_<family>_<idx>_scen<base>`, e.g.
  `special_archmage_1_scen99`. Idx ordering matches the family
  descriptor's `special_actions[]` index.
- Each spec uses `inputs = { {10, 0, K_SPECIAL}, {11, 0, K_NONE}
  }` for `special_index == 0`. Higher indices step the active
  special first via `K_SPECIAL_SWITCH` (= `1u<<KEY_SPECIAL_SWITCH`
  = `1u<<11`): e.g., for `special_index == 2`,
  `inputs = { {5, 0, K_SPECIAL_SWITCH}, {6, 0, K_NONE},
  {7, 0, K_SPECIAL_SWITCH}, {8, 0, K_NONE}, {10, 0, K_SPECIAL},
  {11, 0, K_NONE} }`. `K_SPECIAL`, `K_SPECIAL_SWITCH`, `K_NONE`
  are defined in Phase 02's extensions.
- Mirror entries in
  `../openglad-master/tools/parity_scenario_table.h`,
  committed to the master `parity-companion` branch.
- Per-spec golden under `tests/parity/golden/special_*.json`,
  captured **in this phase** by the rebuilt master companion
  (`capture_master_golden.sh tests/parity/golden/` filtered to
  new ids), schema-validated, and committed. Canonical;
  Phase 07 verifies via re-capture into a throwaway dir.
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
  table — `special_archmage_scen123`, `special_cleric_scen124`,
  `special_mage_scen126`, `special_thief_scen789` — are
  **renamed** to fit the new convention and re-pointed at the
  spawn-injection model. Original scen files remain on disk;
  Phase 05 just stops loading them.
- Summoning specials (druid familiar, cleric heal, mage rocks):
  `tick_budget >= 80` for the spawned entity to appear in the
  dump.
- Caster-only specials (magic shield, invisibility): the dump
  captures the caster's applied-effect `lifetime`; the differ
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
- `06a-check-residual-coverage`
  (`bounce_target: 06-residual-coverage-scenarios`): runs
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` (all coverage-gate
  cases) and asserts every case passes with **zero** uncovered
  targets across walker families, effect families, weapon
  families, treasure families, generator families, specials,
  and event kinds.
- `06b-check-event-kind-coverage`
  (`bounce_target: 06-residual-coverage-scenarios`): runs
  `python3 scripts/parity/audit_event_coverage.py
  tests/parity/golden/` (new) which loads every golden, unions
  the `events[*].kind` values, asserts the union equals the
  full set in `kRequiredEventKinds[]`.
- `06c-check-byte-equal-vs-master`
  (`bounce_target: 06-residual-coverage-scenarios`): per-
  scenario diff against `tests/parity/golden/effect_*.json`,
  `weapon_*.json`, `treasure_*.json`, `generator_*.json`,
  `event_*.json`.

**Preexisting Inputs**:
- Outputs of Phases 04-05.
- `tests/parity/coverage_targets.h`
- `src/gameplay/effect_family_registry.cpp`
- `src/resources/save_data.cpp` (for on-disk save coverage)

**New Outputs**:
- Effect scenarios (~13): `effect_expand`, `effect_ghost_scare`,
  `effect_bomb`, `effect_explosion`, `effect_flash`,
  `effect_magic_shield`, `effect_knife_back`,
  `effect_boomerang`, `effect_cloud`, `effect_marker`,
  `effect_chain`, `effect_door_open`, `effect_hit`. Some are
  implicit consequences of other scenarios; Phase 06 adds only
  the residue not covered by Phases 04-05.
- Weapon scenarios (~20): each
  `FAMILY_KNIFE..FAMILY_BOULDER` weapon family is exercised by
  an attacker who carries that weapon. Each scenario's
  `spawns[]` entry sets `SpawnSpec::default_weapon` (and
  `current_weapon` for the initial swing) to the target weapon
  family id; `apply_post_load_spawns` overrides the carrier's
  loadout. Carrier family is chosen so its attack path fires
  the weapon's emit-projectile logic (e.g. `FAMILY_ARCHER` for
  ranged, `FAMILY_SOLDIER` for melee).
- Treasure scenarios (13): every treasure family in
  `include/openglad/core/constants.h:95-108`
  (`FAMILY_STAIN(0)..FAMILY_SPEED_POTION(12)`, with
  `MAX_TREASURE=12` defined as the highest index) is spawned
  via `spawns[]` and collected by a walker that walks onto it
  via scripted directional input. **No `treasure_collected`
  EventKind exists** — pickup is verified by the union of:
  (a) the treasure walker's id no longer appearing in
  `dump.walkers[]` at the final tick, and
  (b) for value-bearing treasures, the post-pickup change in
  the collector's stats (HP/MP for potions) or
  `dump.score_per_team` (for gold).
  The coverage gate keys on the absent-treasure observation
  and the `exercises` bit, not an event-kind string.
- Generator scenarios (4): tent / tower / bones / treehouse.
  Spawn generator, tick long enough for a child walker, verify
  the child in `walkers[]`.
- Save round-trip scenario `save_roundtrip_disk_99`: loads
  scen99, runs 20 ticks, calls `level.save()` to write a real
  `.glad` archive into the PhysFS write directory, opens it,
  deserialises into a fresh `LevelRuntimeData`, dumps, asserts
  the resulting dump matches the in-memory dump byte for byte.
  Golden captures the in-memory dump; master does the same.
- Exit-trigger scenario `exit_trigger_real_9302`: walks the
  player to the exit tile (script generated by reading the
  scen9302 map metadata to compute the exit position),
  captures the `level_exited` event with the correct
  `next_level`.
- Replay / determinism scenario `rng_reseed_after_load_99`:
  validates the seed-after-load contract from Phase 02. Loads
  with seed A, re-seeds to B post-load, ticks; compared to
  loading with seed B and not re-seeding, the post-tick dumps
  must differ. Both sides agree which is canonical.
- Event-emission scenarios for `EventKind` values not
  naturally emitted by family/special scenarios. Required set:
  the nine non-`None` values from `kRequiredEventKinds[]`:
  - `play_sound` — covered by any combat scenario; audit
    confirms at least one golden's `events[]` contains it.
  - `notification` — cleric heal or yell
    (`K_NONE | (1u<<KEY_YELL)`) scenario.
  - `set_palette` — `FAMILY_GHOST` scare scenario.
  - `request_redraw` — any scenario that loads and ticks ≥ 1.
  - `end_game`, `set_end`, `request_exit_confirmation`,
    `withdraw_to_level` — covered by
    `exit_trigger_real_9302`.
  - `score_change` — any kill in a scoring-active arena (e.g.,
    `family_soldier_scen99` from Phase 04 may already cover
    it).
- Mirror table entries in
  `../openglad-master/tools/parity_scenario_table.h`,
  committed to master `parity-companion`.
- Goldens for every new scenario, captured **in this phase**
  via the rebuilt master companion
  (`capture_master_golden.sh tests/parity/golden/` filtered to
  new ids), schema-validated, committed. Phase 07 re-captures
  into a throwaway directory and asserts zero diffs.
- `.plan/parity-coverage-manifest.md` final state — all rows
  filled.

**File Changes**:
- Modified: `tests/parity/scenario_table.h`
- Modified: `../openglad-master/tools/parity_scenario_table.h`
- New: numerous `tests/parity/golden/*.json` files
- New: `scripts/parity/audit_event_coverage.py`
- Modified: `tests/parity/test_parity_scenarios.cpp`
- Modified: `.plan/parity-coverage-manifest.md`
- Modified: `.plan/parity-harness-design.md` (final scenario
  count updated; coverage matrix replaced with pointer to the
  manifest)

**Implementation Details**:
- `apply_post_load_spawns` must support spawning weapons
  (`Order::WEAPON`) and treasures (`Order::TREASURE`) in
  addition to LIVING / GENERATOR. The `SpawnSpec::order` field
  (Phase 02) covers this.
- For weapon coverage, the spawn entry sets one or both of the
  new `SpawnSpec::default_weapon` / `SpawnSpec::current_weapon`
  fields (Phase 02) to the target weapon family id whenever
  the carrier's default doesn't match.
  `apply_post_load_spawns` calls
  `walker->set_default_weapon(spawn.default_weapon)` and
  `walker->set_current_weapon(spawn.current_weapon)`
  (dirty-field setters at
  `include/openglad/gameplay/walker.h:170-171`) for any
  non-zero field. Same two-field convention mirrored on master
  via `walker::set_default_weapon` /
  `walker::set_current_weapon` (verified on
  `parity-companion`). There is no `walker::weapon_type`
  member; do not invent one.
- Save round-trip on disk requires PhysFS write-dir setup
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

**Phase Name**: Verify (do not overwrite) goldens, classify
divergences, fix.

**Boundary clarification — Phase 04-06 vs Phase 07 captures.**
Phases 04, 05, 06 each produce their tranche of goldens by
running the rebuilt `parity_dump_master` once (at that phase's
master HEAD), writing JSON into `tests/parity/golden/<id>.json`,
validating with `scripts/parity/validate_schema.py`, and
committing. Those goldens are canonical — not regenerated by
any later phase except Phase 07's audit.

Phase 07 does **not** re-run `capture_master_golden.sh` over
the existing golden set in place. It captures into a
**throwaway directory** (`/tmp/golden-rebuild/`) and runs
`diff -r tests/parity/golden/ /tmp/golden-rebuild/` against
the committed set. Expected outcome: **zero diffs**. Any diff
is a real divergence (branch drifted, master companion
drifted, or non-determinism leaked). The agent classifies each
diff per `.plan/parity-fixes.md` (regression / intended_diff)
and applies a branch-side code fix or records an intended_diff
row citing a branch commit SHA. Re-writing a committed golden
to silence a diff without first classifying it is forbidden by
contract item #9; the Phase 07 verifier rejects any commit
modifying a `tests/parity/golden/*.json` unless the same
commit also touches `.plan/parity-fixes.md` with an
`intended_diff:` row citing a branch commit SHA introducing
the intended behaviour change.

The master commit SHA the goldens are pinned to is captured by
Phase 02 (master companion rebuild) and recorded in
`.plan/parity-coverage-manifest.md`'s frontmatter as
`master_companion_sha:` so Phases 04-06 and Phase 07 diff
against the same fixed master. If a later phase needs to
re-rebuild the companion (e.g., schema fix), the agent updates
the recorded SHA in the manifest and the Phase 07 verifier re-
runs the capture against the new SHA; any drifted golden then
becomes a diff that Phase 07 must classify.

**Implement Phase ID**: `07-master-capture-and-fix`

**Verification Phases**:
- `07a-check-all-goldens-present`
  (`bounce_target: 07-master-capture-and-fix`): runs
  `./build/ci-test/parity_runner_smoke --list | wc -l` (the
  smoke runner from Phase 02 is extended here to support
  `--list`, printing one scenario id per line) to get the
  expected count. Asserts
  `ls tests/parity/golden/*.json | wc -l` equals it and that
  for every listed id there is exactly one matching
  `tests/parity/golden/<id>.json`. Asserts no golden is empty
  via
  `python3 scripts/parity/audit_event_coverage.py
  --reject-empty-walkers` applied to every scenario whose
  `tick_budget > 1` and whose `is_intentionally_empty` flag is
  false.
- `07b-check-parity-clean`
  (`bounce_target: 07-master-capture-and-fix`): runs
  `ctest --preset ci-test -R '^og_test_parity'` and asserts
  100% pass. Any failure must be resolved by a branch-side
  code fix (commit message tagged `parity-fix:`) or an
  `intended_diff` row in `.plan/parity-fixes.md` citing a
  branch commit. The verifier rejects the phase if an
  `intended_diff`'s cited commit doesn't exist in `git log`.
- `07c-check-noop-perturbation`
  (`bounce_target: 07-master-capture-and-fix`): canary that
  perturbs a single gameplay constant on the branch under
  `git stash`, recompiles `og_test_parity`, asserts at least
  one parity test now **fails**. Restores the stash.

**Preexisting Inputs**:
- All outputs of Phases 04-06.
- `scripts/parity/capture_master_golden.sh` (extended to drive
  every scenario)
- `../openglad-master/build/ci-test/parity_dump_master` rebuilt
  under Phase 02's runner
- `.plan/parity-fixes.md` (reused as divergence log, rewritten
  to reflect real fixes)

**New Outputs**:
- `.plan/parity-fixes.md` rewritten with real divergence
  classifications observed by re-capturing the master
  companion into a throwaway directory and diffing against the
  committed `tests/parity/golden/` tree. Each row is either
  `regression` (with a code fix commit SHA on the branch) or
  `intended_diff` (with the branch commit SHA authorising the
  change). The "no divergences observed vacuously" wording
  from the prior version is forbidden. If re-capture produces
  zero diffs, the doc says so explicitly and lists the
  scenario count diffed.
- Any source-code fixes needed to restore parity (in `src/` or
  `include/`). Committed; the committed goldens are **not**
  overwritten except via the intended_diff path. Phase 04-06
  goldens are canonical; Phase 07 is a verification pass.
- `.plan/parity-coverage-manifest.md` — annotated with the
  final committed master companion SHA (read from frontmatter
  set in Phase 02) and the branch HEAD SHA at the time of
  Phase 07's re-capture.

**File Changes**:
- Modified: `scripts/parity/capture_master_golden.sh` (drive
  every scenario; output to a configurable destination
  directory rather than always writing into
  `tests/parity/golden/`, so Phase 07 uses a throwaway dir)
- Modified (rare): `tests/parity/golden/*.json` — only via the
  intended_diff path, never as a blind re-capture. A commit
  modifying any golden in Phase 07 must also modify
  `.plan/parity-fixes.md` with the matching intended_diff row.
- Modified: `.plan/parity-fixes.md`
- Modified: `.plan/parity-coverage-manifest.md` (final SHAs)
- Possibly modified: `src/gameplay/*`, `src/interface/*`, etc.,
  if real regressions surface

**Implementation Details**:
- `capture_master_golden.sh` accepts a destination directory
  argument (e.g. `/tmp/golden-rebuild/`), iterates
  `${master}/build/ci-test/parity_dump_master --list`, writes
  one JSON per scenario into the destination,
  `validate_schema.py`s each dump, aborts on malformed golden.
  Phases 04-06 invoked it with `tests/parity/golden/` as
  destination; Phase 07 uses a throwaway directory.
- Phase 07 re-capture sequence:
  1. `mkdir -p /tmp/golden-rebuild && rm -rf
     /tmp/golden-rebuild/*`.
  2. `(cd ../openglad-master && cmake --build --preset ci-test
      --target parity_dump_master)`.
  3. `scripts/parity/capture_master_golden.sh
      /tmp/golden-rebuild/`.
  4. `diff -r tests/parity/golden/ /tmp/golden-rebuild/`.
- When a diff fires: read the diff (which entity/event/
  field), inspect the branch-side code path, then either (a)
  fix branch code or (b) classify as `intended_diff` if a
  branch commit (cited by SHA) explicitly changed the
  behaviour. Contract item #9 forbids silently re-capturing
  the golden; verifier `07b` enforces by rejecting any commit
  that modifies `tests/parity/golden/*.json` without a matching
  `.plan/parity-fixes.md` intended_diff row.
- The `07c` canary automates the prior signoff's "manual
  canary": `git stash`es, applies
  `sed -i 's/- tempdamage_i)/- (tempdamage_i + 1))/'
  src/gameplay/walker_combat.cpp` (one-line damage bump on the
  verified statement at `walker_combat.cpp:302`), rebuilds
  `og_test_parity`, runs the suite, requires non-zero exit.
  Then `git checkout -- src/gameplay/walker_combat.cpp` and
  `git stash pop` to restore; rebuilds to confirm clean state
  still passes.
- The agent must commit before yielding.

**Verification**:
- 100% pass on
  `ctest --preset ci-test -R '^og_test_parity'`.
- No empty goldens.
- Canary perturbation breaks at least one test.

---

### Phase 8 — CI wiring and honest sign-off

**Phase Name**: Lock the gate, replace the fraudulent sign-off.

**Implement Phase ID**: `08-ci-and-honest-signoff`

**Verification Phases**:
- `08a-check-ci-yaml-runs-coverage`
  (`bounce_target: 08-ci-and-honest-signoff`): greps
  `.github/workflows/test.yml` for the coverage-gate
  invocation. Because the coverage gate lives inside the
  existing `og_test_parity` binary, the required CI line is
  `./build/ci-test/og_test_parity
  --gtest_filter='Parity.coverage_gate*'` (verifier accepts
  this literal or an equivalent
  `ctest ... -R 'Parity.coverage_gate'` form selecting the
  same cases). Also greps for
  `scripts/parity/check_coverage_manifest.py`. Both must run
  *before* the final ctest pass so a missing target fails CI
  fast.
- `08b-check-signoff-honest`
  (`bounce_target: 08-ci-and-honest-signoff`): reads
  `.plan/parity-signoff.md` (newly written, not the renamed
  fraudulent one) and asserts:
  1. Cites a non-zero number of golden files and confirms
     each is non-empty (with `ls -l` evidence).
  2. Lists every coverage category from the manifest with
     observed-vs-required counts.
  3. Includes literal output of
     `ctest --preset ci-test -R '^og_test_parity'` showing
     the full pass count.
  4. Does not contain "vacuously satisfied", "indirectly
     covered", or "not currently registered".
  5. Cites the master companion commit SHA and branch HEAD
     SHA the goldens were captured against.
- `08c-check-end-to-end-rebuild`
  (`bounce_target: 08-ci-and-honest-signoff`): on a clean
  tree (`rm -rf build/`), runs
  `cmake --preset ci-test && cmake --build --preset ci-test
  && ctest --preset ci-test` end-to-end; exit code 0.

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
  `.plan/parity-coverage-manifest.md`), divergence ledger
  (cites `.plan/parity-fixes.md`), reproduction commands,
  canary perturbation evidence, master commit SHAs, branch
  HEAD SHA, full `ctest` output excerpt.
- `.github/workflows/test.yml` extended with two CI steps:
  1. "Parity coverage manifest check" — runs
     `scripts/parity/check_coverage_manifest.py` before the
     build.
  2. "Parity coverage gate" — runs
     `./build/ci-test/og_test_parity
     --gtest_filter='Parity.coverage_gate*' --gtest_color=no`
     immediately after the build (the coverage-gate cases
     live inside `og_test_parity`; no separate executable).
  The existing parity-test step still runs the full
  `og_test_parity` group via
  `ctest --preset ci-test -R '^og_test_parity'`.

**File Changes**:
- New: `.plan/parity-signoff.md`
- Modified: `.github/workflows/test.yml`
- Modified: `.plan/parity-harness-design.md` — add "Phase 08
  redo" section pointing at the rewritten sign-off and
  explicitly superseding any contradictory claim in older
  sections.

**Implementation Details**:
- The agent rewriting the sign-off must run every command it
  cites before writing; the verifier (`08b`) compares cited
  counts against re-execution and fails on stale numbers.
- CI step ordering: manifest check (cheap) before build,
  coverage gate immediately after build, full parity suite
  as part of the normal ctest pass. Each step uses
  `if: always()` so a single failure surfaces all three.
- The agent must commit before yielding.

**Verification**:
- `.github/workflows/test.yml` has the two new steps.
- `.plan/parity-signoff.md` exists and passes the honesty
  grep rules.
- Clean rebuild + full ctest succeeds.

---

## 4. Critical Files

### New source files (branch)

- `tests/parity/parity_bootstrap.{h,cpp}` — PhysFS + campaign +
  search-path setup.
- `tests/parity/scenario_runtime.{h,cpp}` —
  `scenario_level_id`, `apply_post_load_spawns`,
  `apply_inputs_at_tick`, `clear_world_entities`.
- `tests/parity/parity_runner_smoke_main.cpp` — small
  executable used by the Phase 02 verifier.
- `tests/parity/coverage_targets.h` — required-coverage
  manifest in C++.
- `tests/parity/test_parity_coverage_gate.cpp` — runtime gate
  test.

### New scripts

- `scripts/parity/check_coverage_manifest.py` — static check
  against `include/openglad/core/constants.h` and `event.h`.
- `scripts/parity/audit_event_coverage.py` — verifies every
  emitted `EventKind` appears in at least one golden's
  `events[]`.

### Modified source files (branch)

- `tests/parity/parity_runner.{h,cpp}` — full rewrite: load
  scenarios, apply inputs, drop `level_done` short-circuit.
- `tests/parity/scenario_table.h` — extended with `SpawnSpec`,
  `spawns[]`, `spawn_count`, `exercises`, `fresh_arena`,
  `player_team`; grows from 16 to ~100+ scenarios.
- `tests/parity/test_parity_scenarios.cpp` — one
  `OG_PARITY_TEST(idx, name)` per added scenario.
- `tests/parity/parity_test_main.cpp` — hosts PhysFS init via
  `parity_bootstrap`.
- `tests/parity/state_dump.{h,cpp}` — extend schema-v1 emitter
  with `level_done`, `level_tick_count` fields (still v1).
- `CMakeLists.txt` — link `parity_bootstrap`,
  `scenario_runtime`, `coverage_gate`; register
  `parity_runner_smoke`; copy `temp/scen/` and `builtin/` to
  the test working dir.
- `.github/workflows/test.yml` — coverage manifest + gate
  steps.

### Modified source files (master)

- `../openglad-master/tools/parity_dump_master.cpp` — full
  rewrite mirroring branch runner.
- `../openglad-master/tools/parity_dump_master_stubs.cpp` —
  additional headless stubs as needed.
- `../openglad-master/tools/parity_scenario_table.h` — mirror.
- `../openglad-master/tools/parity_bootstrap.{h,cpp}` (new).
- `../openglad-master/tools/parity_scenario_runtime.{h,cpp}`
  (new).
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
- `.plan/parity-divergence-report.md` (rewritten or appended
  in Phase 07 with real divergences)
- `.plan/parity-harness-design.md` (amended in 02/03/06 with
  the load-path section, spawns schema, manifest pointer)
- `.plan/parity-signoff.md` (new, Phase 08)

### Golden files

- Phase 01 deletes the existing 15.
- Phases 04-06 each contribute a tranche; Phase 07 finalises
  the full set against the rebuilt master companion.
- Final count: equal to `kMasterComparableScenarioCount`
  derived at compile time from `scenario_table.h` (expected
  ~80-120).

## 5. Final Verification

After Phase 8 completes and commits, run from a clean tree, in
order:

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
   `og_test_parity` binary; select via gtest filter:
   ```
   ./build/ci-test/og_test_parity \
       --gtest_filter='Parity.coverage_gate*'
   ```

4. **Full parity suite** (must exit 0):
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

7. **Canary perturbation detects a regression** (must report
   at least one failing test, then revert). The damage line at
   `src/gameplay/walker_combat.cpp:302` is
   `stats_->set_hitpoints( stats_->hitpoints() - tempdamage_i);`
   — perturb `tempdamage_i` to `(tempdamage_i + 1)`. The
   verifier first asserts the working tree is clean
   (`git status --porcelain` empty) so the `git checkout --`
   revert is sufficient:
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
   Phase 07's verifier wraps this sequence; if post-patch
   `ctest` exits 0, the harness is not load-bearing and the
   verifier rejects the phase.

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

Only when all nine pass is the work complete. Phase 08's
verifier wraps steps 1-3 and 5-9; step 4 is wrapped by
Phase 07's verifier; step 6 is wrapped by Phases 04-07's
byte-equal checks.
