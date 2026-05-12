# Phase 02 — Real Runner and Master Companion

## Phase Name
Real branch runner + real master companion.

## Implement Phase ID
`02-real-runner-and-companion`

## Preexisting Inputs
- `.plan/parity-redo-audit.md`
- `.plan/parity-harness-design.md` (schema reused unchanged)
- `tests/parity/parity_runner.{h,cpp}` (to be rewritten)
- `tests/parity/parity_test_main.cpp` (PhysFS init lands here or in a
  runner setup helper)
- `tests/parity/scenario_table.h` (extended with one smoke scenario)
- `tests/parity/test_parity_scenarios.cpp` (extended with one smoke test
  entry)
- `tests/parity/state_dump.{h,cpp}` (schema unchanged; emitter reused)
- `tests/test_network_fixture.h:495-535` (reference for headless
  `LevelRuntimeData` setup)
- `src/platform/text/platform_headless.cpp:340-385` (reference for
  PhysFS + campaign bootstrap)
- `src/interface/level_runtime_data.cpp:831-883` (reference for
  scenario-file resolution)
- `../openglad-master/tools/parity_dump_master.cpp` (to be rewritten)
- `../openglad-master/tools/parity_dump_master_stubs.cpp`
- `CMakeLists.txt` (parity-test wiring at line 1807)
- `../openglad-master/CMakeLists.txt`

## New Outputs
- `tests/parity/parity_bootstrap.{h,cpp}` — PhysFS + campaign +
  search-path setup that:
  1. Calls `og::resources::init(argv0)`.
  2. Sets writable scratch directory under `${CMAKE_BINARY_DIR}/parity-write/`.
  3. Calls `restore_default_campaigns()` then
     `mount_campaign_package_with_error("org.openglad.gladiator")`.
  4. Adds `temp/scen/` and `scen/` to the PhysFS search path so scenario
     files at either root resolve as `scen{id}.fss`. If the campaign
     archive already contains `scen99.fss`, mounting the project-tree
     directories is a no-op fallback.
  5. Installs `sdl_level_data_hooks()` (declared at
     `include/openglad/resources/level_data_hooks.h:37`) unchanged.
  6. Provides teardown that unmounts and `og::resources::deinit`s.
  Includes `BootstrapScope` RAII struct so each test entry is
  self-contained. Mirrored byte-for-byte at
  `../openglad-master/tools/parity_bootstrap.{h,cpp}`.

- `tests/parity/scenario_runtime.{h,cpp}`:
  - `int scenario_level_id(string_view scenario_file)` parses the integer
    between `scen` and `.fss` in the basename.
  - `void apply_post_load_spawns(GameWorld&, const ScenarioSpec&)` iterates
    `spec.spawns[]` and calls
    `world.add_ob(static_cast<Order>(spawn.order), spawn.family, /*atstart=*/true)`,
    then sets position via `set_xpos/set_ypos`, team via
    `set_team_num`/`set_real_team_num`, and optionally
    `set_default_weapon`/`set_current_weapon` when non-zero.
  - `void apply_inputs_at_tick(GameWorld&, const ScenarioSpec&, uint32_t tick)`
    finds entries whose `tick == current tick`, identifies the target
    walker (first walker with `team_num() == spec.player_team`), and
    calls `walker->set_keys(entry.key_mask)`.
  - `void clear_world_entities(GameWorld&)` for `fresh_arena` scenarios.

- Extensions of `ScenarioSpec` in `tests/parity/scenario_table.h`:
  - `const SpawnSpec* spawns; std::size_t spawn_count;`
  - `struct SpawnSpec { std::int32_t family; std::uint8_t team;
    std::uint8_t order; std::int32_t x; std::int32_t y;
    std::uint16_t default_weapon; std::uint16_t current_weapon; };`
  - Wrapper helpers `kOrderLiving`, `kOrderWeapon`, `kOrderTreasure`,
    `kOrderGenerator`, `kOrderFX`.
  - `std::uint8_t player_team = 0;`
  - `bool is_intentionally_empty = false;`
  - `bool fresh_arena = false;`
  - `std::uint64_t exercises = 0;` plus a new
    `enum class Exercises : std::uint64_t { None = 0 };` (extended in
    Phase 03).
  - Existing 16 scenarios initialised with `spawns = nullptr; spawn_count = 0;`.
  - Input constants: `K_NONE = 0`, `K_FIRE = 1u<<KEY_FIRE`,
    `K_SPECIAL = 1u<<KEY_SPECIAL`, `K_SPECIAL_SWITCH = 1u<<KEY_SPECIAL_SWITCH`,
    plus directional variants.

- New smoke scenarios `smoke_nonempty_scen99` (no inputs, populated
  arena) and `smoke_nonempty_scen99_inputs` (scripted
  `{1, 0, K_FIRE}, {3, 0, K_NONE}`); both must produce non-empty walker
  dumps whose position/keys fields differ.

- Rewrite of `parity_runner.cpp::run_scenario` to use
  `LevelRuntimeData(level_id, /*headless=*/true, &sdl_level_data_hooks())`,
  install a `ScopedGameplayContext`, call `level.load()`, re-seed
  `world.rng_.state_ = spec.rng_seed` **after** `load()`, optionally
  `clear_world_entities(world)` for `fresh_arena`, apply spawns, then
  run the full `tick_budget` ticks applying inputs each tick **without
  breaking on `level_done`**. The schema-v1 dump records `world.level_done`
  and `world.level_tick_count()` (schema v1 tolerates unknown keys).
  References `extern cfg_store cfg;` from
  `include/openglad/resources/gparser.h:38` — does not default-construct
  a local `cfg_store`.

- Mirror rewrite of `../openglad-master/tools/parity_dump_master.cpp::run`
  against master's actual surface, including adding any additional
  headless stubs to `parity_dump_master_stubs.cpp`.

- `parity_runner_smoke` — small CMake executable target linking
  bootstrap + runner; emits JSON for a named scenario to stdout (used
  by the Phase 02 verifier and extended in Phase 07 to support `--list`).

## File Changes
- New: `tests/parity/parity_bootstrap.{h,cpp}`,
  `tests/parity/scenario_runtime.{h,cpp}`,
  `tests/parity/parity_runner_smoke_main.cpp`.
- Modified: `tests/parity/parity_runner.{h,cpp}`,
  `tests/parity/scenario_table.h`,
  `tests/parity/test_parity_scenarios.cpp`,
  `tests/parity/parity_test_main.cpp`.
- Modified: `CMakeLists.txt` (link bootstrap into `og_test_parity`; add
  `parity_runner_smoke` target; copy `builtin/` and `temp/scen/` to the
  test runtime working directory).
- Modified: `tests/parity/state_dump.{h,cpp}` — extend schema-v1 emitter
  with `level_done`, `level_tick_count` fields (still v1).
- New on master:
  `../openglad-master/tools/parity_bootstrap.{h,cpp}`,
  `../openglad-master/tools/parity_scenario_runtime.{h,cpp}`.
- Modified on master:
  `../openglad-master/tools/parity_dump_master.cpp`,
  `../openglad-master/tools/parity_dump_master_stubs.cpp`,
  `../openglad-master/tools/parity_scenario_table.h` (byte-for-byte mirror),
  `../openglad-master/tools/parity_dump_state.{h,cpp}`,
  `../openglad-master/CMakeLists.txt`.
- Modified: `.plan/parity-harness-design.md` — append "Phase 02 redo:
  load path" section documenting the real load/tick/inject pipeline and
  the new `spawns[]` schema field. Schema-v1 JSON unchanged.

## Implementation Details
- PhysFS init happens exactly once per process (in
  `parity_test_main.cpp` SetUp); per-test the runner uses a
  `ScopedGameplayContext` only.
- `LevelRuntimeData::load()` reads RNG state during decoration; the
  scenario's `rng_seed` is re-applied **after** `load()` so the tick
  loop starts from the canonical seed on both sides.
- `apply_inputs_at_tick`'s canonical "player walker" is the first walker
  whose `team_num == spec.player_team` (matches
  `add_network_player_character` in `test_network_fixture.h:393`).
- Do **not** `break` on `level_done`; let the loop run the full
  `tick_budget`.
- Mirror table and bootstrap files between branch and
  `../openglad-master/tools/` are kept byte-for-byte synchronised.

## Verification Phases
- **Phase ID**: `02a-check-build-clean`
  - **Type**: `check`
  - **Bounce Target**: `02-real-runner-and-companion`
  - **Purpose**: Confirm both branch and master companion build cleanly.
  - **Commands**:
    ```
    cmake --preset ci-test
    cmake --build --preset ci-test --target og_test_parity parity_runner_smoke
    (cd ../openglad-master && cmake --preset ci-test \
       && cmake --build --preset ci-test --target parity_dump_master)
    git log -1 --stat                                         # branch commit visible
    git -C ../openglad-master log -1 --name-status            # master-side commit visible
    [ -z "$(git -C ../openglad-master status --porcelain)" ]  # clean tree
    ```
    All commands exit 0; the master-side commit's `name-status` must
    list `tools/parity_dump_master.cpp`, `tools/parity_bootstrap.cpp`,
    and `tools/parity_scenario_runtime.cpp`.

- **Phase ID**: `02b-check-smoke-nonempty`
  - **Type**: `check`
  - **Bounce Target**: `02-real-runner-and-companion`
  - **Purpose**: Confirm the rewritten runner actually loads a scenario,
    runs ≥ 50 ticks, and emits a non-empty walker dump on both branch
    and master.
  - **Commands**:
    ```
    ./build/ci-test/og_test_parity \
        --gtest_filter=Parity.smoke_nonempty_scen99
    ../openglad-master/build/ci-test/parity_dump_master \
        --scenario smoke_nonempty_scen99 --out /tmp/smoke.json
    python3 -c "import json; d=json.load(open('/tmp/smoke.json')); \
        assert len(d['walkers'])>0 and d['tick']>=50, d"
    ```
    All commands exit 0.

## Success Criteria
- `og_test_parity` and `parity_runner_smoke` build cleanly on branch.
- `parity_dump_master` builds cleanly on `../openglad-master`.
- `Parity.smoke_nonempty_scen99` passes; smoke JSON contains
  `len(walkers) > 0` and `tick >= 50`.
- Inputs applied at runtime cause observably-different walker state
  between `smoke_nonempty_scen99` and `smoke_nonempty_scen99_inputs`.
- Branch and `../openglad-master` worktrees both have clean status
  with the phase commit visible at HEAD.

## Git Commit Requirement
Two commits **before yielding**, one in each worktree:
1. `git add` branch-side files and
   `git commit -m "parity-redo: phase 02 — real runner and schema-v1 fields"`.
2. `git -C ../openglad-master add` companion files and
   `git -C ../openglad-master commit -m "parity-companion: phase 02 — real load/tick/inject pipeline"`.
The check phase asserts both commits exist and that both working trees
are clean (`git status --porcelain` empty).
