## Findings — Plan Review (parity framework, redo pass)

The previously-blocking findings have been addressed (the plan now uses
`world.tick()` and `world.rng_.state_ = spec.rng_seed` instead of the
hallucinated `GameWorld::act()` / `current_session->rng()` accessors).
Most other codebase facts cross-check: `add_ob(Order, std::int32_t,
bool)` at `game_world.h:154`, `walker::set_keys` via
`OG_WALKER_DIRTY_FIELD(... keys ...)` at `walker.h:201`,
`Order::{Living=0,Weapon=1,Treasure=2,Generator=3,FX=4,Special=5,Button1=6}`
at `order.h:12-19`, `KEY_FIRE=8 / KEY_SPECIAL=9 / KEY_SPECIAL_SWITCH=11`
at `input.h:232-235`, the 21/20/13/13/4 family counts in
`constants.h`, `EventKind` and `kind_string` in `event.h` /
`state_dump.cpp:137-146`, `walker_combat.cpp:302`'s
`- tempdamage_i)` substring, and `sdl_level_data_hooks()` at
`level_data_hooks.h:37`. The topology contract (linear, inline-only,
single fixed `bounce_target`, no `bounce_targets`, no
`parallel_groups`, commit-before-yield, `Preexisting Inputs` and
`New Outputs` separated) is intact.

The remaining issues below must be resolved before generating
`.plan/phases/*.md` and `.plan/workflow-structure.yaml`.

### Blocking issues

1. **Phase 06's weapon-coverage scenarios reference a `SpawnSpec`
   field that Phase 02 does not define.** Phase 06 Implementation
   Details state *"For weapon coverage, the carrier walker's
   `walker->weapon_type` (or equivalent) is set via the spawn spec
   if the family's default weapon doesn't match the target weapon
   family."* But Phase 02 lays down `SpawnSpec` as exactly
   `{ std::int32_t family; std::uint8_t team; std::uint8_t order;
   std::int32_t x; std::int32_t y; }` (plan lines 525-535), with no
   weapon field, no overrides table, and no post-spawn hook. There
   is also no `walker::weapon_type` member on either side; the
   actual fields are `default_weapon_` / `current_weapon_` (private,
   exposed via `set_default_weapon` / `set_current_weapon` per
   `walker.h:170-171`). The plan must either (a) extend `SpawnSpec`
   in Phase 02 with a `std::uint16_t default_weapon = 0;` (and
   `std::uint16_t current_weapon = 0;`) field, with a documented
   "0 means leave at family default" convention, and have
   `apply_post_load_spawns` call `set_default_weapon` /
   `set_current_weapon` on the returned walker, or (b) define a
   separate post-load weapon-injection table that Phase 06 uses.
   The current text leaves the workflow writer to invent a missing
   data structure.

2. **Phase 03's coverage-gate target is ambiguous about whether it
   is a new binary or a new test case inside the existing test
   group.** The plan's New Outputs say *"register
   `og_test_parity_coverage_gate` as part of the parity test group
   (or as its own ctest entry)"* (Phase 03 File Changes) — the
   "or" is unresolved. CLAUDE.md and `CMakeLists.txt:1807`
   establish that integration tests live under `og_add_test_group`
   binaries (`og_test_parity`), not as standalone binaries. The
   Phase 04 verifier runs `./build/ci-test/og_test_parity_coverage_gate
   --gtest_filter=...` as if it were its own binary (plan lines
   856-857), but the Phase 03 verifier runs
   `cmake --build --preset ci-test --target
   og_test_parity_coverage_gate` (plan line 835-836), which would
   fail unless the target name maps to either a standalone
   executable or a CMake interface target. The plan must commit
   to one of:
   - **(A)** Coverage gate is registered as separate cases under
     the existing `og_test_parity` group binary (e.g. cases named
     `Parity.coverage_gate_*` inside `og_test_parity`), in which
     case the verifier commands must be rewritten as
     `./build/ci-test/og_test_parity --gtest_filter='Parity.coverage_gate*'`
     and the `og_add_test_group` call grows
     `test_parity_coverage_gate.cpp`.
   - **(B)** A new standalone executable target, in which case
     Phase 03 declares it with `add_executable(og_test_parity_coverage_gate ...)`
     rather than `og_add_test_group`, and the Phase 04/05/06
     verifiers correctly invoke that binary path.
   Either is fine; the plan must pick one and update every
   verifier command accordingly.

3. **Phase 03 verifier `03b` mixes "fails to compile" and "fails at
   runtime" as acceptable outcomes.** Plan line 716-718: *"asserts
   the resulting binary **fails** to compile or **fails** at
   runtime"*. The gate is a runtime check (it iterates `kScenarios`
   in `SetUpTestSuite()` and compares observed-vs-required arrays —
   per Phase 03's own description at lines 770-786, this is
   runtime behaviour, not a `static_assert`). A workflow writer
   that treats compile-failure as a valid outcome will write a
   different bash assertion than one that treats only runtime
   failure as valid. Pick one: the gate fails at runtime with a
   diagnostic naming every uncovered target. Compile-failure is
   only relevant if Phase 03 *also* adds a `static_assert` on
   manifest size; if so, the plan must commit to that and state
   the trigger.

4. **Phase 07's "Regenerate all goldens" mandate contradicts the
   workflow contract item #7 ("Existing artifacts are reused, not
   regenerated").** Phase 07 New Outputs (plan line 1227-1228)
   say *"A fully populated `tests/parity/golden/` matching the
   final `kScenarios` list"*, implying Phase 04-06's per-tranche
   goldens are overwritten. The Generated Workflow Contract item
   #7 (plan line 281-300) lists categories that may be modified
   in place but never says "golden files are draft until Phase
   07". Two cheap reconciliations: (a) explicitly mark Phase
   04/05/06 goldens as draft, with Phase 07 producing the
   canonical set against the rebuilt-once master companion at a
   pinned SHA; or (b) require Phases 04-06 to do their own
   master-side capture and Phase 07 only catches stragglers
   (existing-file diffs that re-run the master companion once
   and assert no change). As written the workflow writer cannot
   tell whether Phase 07's `capture_master_golden.sh` invocation
   is a re-capture (which can mask drift) or a verification pass.

### Non-blocking but worth tightening

5. **The "19 phases" total is wrong.** Plan line 330: *"Eight
   implement phases, each paired with one or more explicit check
   phases. Total: 8 implement + 11 check = 19 phases."* The actual
   verifier count across Phases 1-8 is `1+2+2+2+2+3+3+3 = 18` check
   phases, so the total is `8 + 18 = 26` phases. The number isn't
   load-bearing but a workflow writer that trusts the header could
   under-allocate phase ids.

6. **Phase 02's `run_scenario` snippet does not declare where the
   `cfg` symbol comes from.** Plan line 572:
   `ScopedGameplayContext gameplay(level, save, events, cfg);`
   and line 573-574: `&level.world().rng_, &cfg`. The actual
   symbol is the global `extern cfg_store cfg;` declared in
   `include/openglad/resources/gparser.h:38` (verified). Phase 02
   should name the include explicitly so the workflow writer
   doesn't try to default-construct a local `cfg`.

7. **Phase 02 does not explicitly require a commit on the master
   worktree.** Rule #8 ("Commit-before-yield") is stated globally
   in the contract, but Phase 02 modifies both
   `tests/parity/*` on the branch and
   `../openglad-master/tools/*` on the master worktree, and the
   master worktree is a separate git checkout on the
   `parity-companion` branch. The Phase 02 prompt must spell out
   that the agent runs `git -C ../openglad-master add ... &&
   git -C ../openglad-master commit ...` in addition to the
   branch-side commit, or the next phase's "verify master
   companion changes are committed" check has nothing to grep.

8. **The dump emitter extension wording is imprecise.** Phase 02
   Implementation Details say *"the dump records `world.level_done`
   and `world.level_tick_count_`"* (plan line 660). `level_done` is
   public (`game_world.h:214`) but `level_tick_count_` is private
   (`game_world.h:253`); the emitter must use the public
   accessor `world.level_tick_count()` (`game_world.h:161`). One-
   line fix; not a contract issue.

9. **Phase 04's "fresh_arena = true" feature is introduced in
   Phase 04 but the field is not added in Phase 02.** Plan line
   926-927 introduces `kScenarios[].fresh_arena = true` as a new
   `ScenarioSpec` bool; Phase 02's `ScenarioSpec` extension list
   (lines 522-549) names `spawns`, `spawn_count`, `player_team`,
   `is_intentionally_empty`, `exercises` but not `fresh_arena`.
   Either move `fresh_arena` into Phase 02's spec extensions, or
   make Phase 04's `clear_world_entities()` an unconditional
   helper invoked only by scenarios whose `spawns[]` is non-empty
   (the existing `spawn_count > 0` is a cheap proxy). The plan
   should commit one way.

10. **Treasure-family coverage drops one family.** Plan line 1086:
    *"each treasure family (`FAMILY_STAIN..FAMILY_KEY`)"* — the
    range `FAMILY_STAIN(0)..FAMILY_KEY(11)` is 12 entries, but
    `constants.h:95-108` defines 13 treasure families through
    `FAMILY_SPEED_POTION(12)`. The intent ("every defined treasure
    family") is correct; the cited range omits one family. Fix
    the range to `FAMILY_STAIN..FAMILY_SPEED_POTION` (or list
    `MAX_TREASURE = 12` so the range is inclusive of `[0..12]`).

VERDICT: items 1-4 are concrete enough that the workflow writer
would need to invent missing structural decisions; items 5-10
are tightenings that the next refinement pass can fold in
quickly.
