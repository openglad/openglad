# OpenGlad Change Audit (Last ~2 Days)

## Scope and Commit Range
- Branch audited: `feat/sim-rendering-split` (PR #33 context).
- Time window audited: commits from `2026-02-19 01:08:28 +0000` through `2026-02-20 17:32:30 +0000` (135 commits total, from `4603e28` to `81cb27a`).
- Likely checkpoint inference for "view ... segfault" fix: **`731647e`** (`tests: fix segfault in round 9B tests causing coverage drop`, 2026-02-20 03:13:57 +0000).
  - Why this inference: it is the only explicit segfault-fix commit in the window and edits `CMakeLists.txt` to remove several tests from integration/module targets, including nearby `view`-era suites.
  - Ambiguity noted: the user phrase may also refer to earlier "view suite" expansion in `7ed060f` (`tests: broaden runtime coverage with view and level/save suites`).
- Focus range after inferred checkpoint: **`731647e..81cb27a`** (25 commits).

## Executive Summary
- The last ~2 days are dominated by aggressive coverage expansion and CI iteration.
- Post-checkpoint (`731647e..HEAD`), the branch added many new unit coverage bundles (`r11` through `r20`) and two significant runtime-hardening commits in `src/`:
  - `f787e91` added defensive bounds/type checks in core runtime/entity paths.
  - `81cb27a` changed gparser help/version exits to `_Exit(0)` and lowered enforced coverage thresholds in CI to 69% line / 86% function.
- CI/gate behavior changed repeatedly over the window (1/3 -> 70/75 -> 85/95 -> temporary non-blocking -> blocking again -> 69/86 final).
- Tests were relaxed/removed in several places to stabilize CI and prevent hangs/segfaults; some removed tests remain present on disk but are no longer built/run.

## Non-Test Code Changes
### Runtime/source behavior changes (`src/`)
1. `aa21f6d` (`src/sdl_client/ui/picker_dialogs.cpp`, `src/sdl_client/ui/results_screen.cpp`)
- Added test-mode switches to bypass modal UI loops by default and only run full UI loops when force-enabled:
  - `picker_testing_set_force_real_dialogs(bool)`
  - `results_screen_testing_set_force_full(bool)`
- Effect: in `TESTING` builds, dialog/result paths default to non-blocking short-circuit behavior unless explicitly forced.

2. `f787e91` (multiple runtime files)
- `src/entities/walker.cpp`: clamp/validate animation indices (`curdir`, `ani_type`) before indexing animation tables.
- `src/entities/walker_movement.cpp`: clamp invalid facing directions before turn math.
- `src/entities/walker_specials.cpp`: validate `current_special` bounds before indexing `special_cost`.
- `src/runtime/sim_input_handler.cpp`: guard `special_names` indexing with family/special bounds and null checks.
- `src/runtime/gloader.cpp`: sanitize invalid `Order` values in creation/stat setup paths.
- `src/io/zip_api.cpp`: avoid archiving the output zip file into itself (`fs::equivalent(src, out_path)`).
- `src/entities/families/family_soldier.cpp`: switched to `dynamic_cast<living*>` guards in soldier hooks.
- Net effect: primarily sanitizer/UB hardening and invalid-input safety, with some semantic fallback behavior changes (invalid values now coerced to defaults).

3. `81cb27a` (`src/data/gparser.cpp`)
- `-h` and `-v` command-line exits changed from `exit(0)` to `std::fflush(nullptr); std::_Exit(0);`.
- Net effect: immediate process termination after flush (bypasses normal C/C++ teardown/atexit handlers), likely to make coverage/test termination deterministic.

### Build/config behavior changes with runtime impact
1. `1504d3a` (`CMakeLists.txt`)
- Coverage flags (`--coverage`) applied to configured libraries/targets when `ENABLE_COVERAGE` is on.
- `ENABLE_COVERAGE` define added to test targets to support explicit gcov dump paths.

## Test/Coverage Changes
- Very large test expansion focused on branch/line coverage (`aa21f6d`, `217ab43`, `a9e31a4`, `9163d1e`, `7ed060f`, `35cd0f0`, rounds 2-20, etc.).
- Post-checkpoint additions include new coverage files `tests/test_coverage_r19.cpp` (`3370972`) and `tests/test_coverage_r20.cpp` (`5c0dafc`) plus many targeted r11-r18 bundles.
- `226e5f7`, `e6e320d`, and `f787e91` added explicit `__gcov_dump()` usage in test mains to preserve coverage data with `_exit`-style termination.
- Several tests were edited to avoid hangs/sanitizer instability (`05917f2`, `f787e91`).

## CI/Workflow/Gate Changes
### Coverage workflow introduced and iterated heavily
- New workflow introduced in `4603e28`: `.github/workflows/coverage.yml`.
- Multiple reliability/observability updates:
  - PR head SHA checkout (`a6c3372`, `10c8bf4`).
  - Summary + PR comment reporting (`d2759f9`) and metric derivation from enforced gcovr run (`7eb053c`).
  - Retry/timeouts/concurrency tuning (`9ca2457`, `6c6ad5d`, `222983e`, `290f0e3`, `813b4e9`, `06f1875`, `05917f2`).
  - Ignore gcov suspicious-hit parse warnings (`881e0ea`).

### Threshold/gating timeline (important)
- `23a957f`: fail-under reduced to `0/0` temporarily.
- `6a11527`: raised to `1/3`.
- `567da7b`: raised to `70/75`.
- `653c235`: raised to `85/95`.
- `2e4f21e`: temporarily made threshold failures non-blocking.
- `e529107`: reverted non-blocking behavior (blocking restored).
- `81cb27a` (current state): enforced thresholds changed to `69/86` and reporting aligned to those values.

## Test Disablement/Relaxation Findings
### Were tests disabled/skipped/softened?
Yes.

1. **Removed from build/run targets to stop instability**
- `731647e` removed several test files from `CMakeLists.txt` target lists (including `tests/test_effect_more_paths.cpp`, `tests/test_walker_core_more.cpp`, `tests/test_stats_more_paths.cpp`, `tests/test_family_behaviors.cpp`, `tests/test_level_data_coverage.cpp`).
- These files still exist under `tests/` but are not referenced by `CMakeLists.txt` at HEAD.

2. **Flaky dialog stress tests deleted**
- `9dcee0b` deleted `tests/test_picker_dialogs_real.cpp` and corresponding CMake inclusion.

3. **Selective skip under ASan**
- `f787e91` added ASan-conditional early return in `tests/test_gparser_funcs.cpp` for fork/exit help-version assertions.

4. **Non-failing timeout behavior for portions of coverage run**
- In `.github/workflows/coverage.yml`, `og_data_tests`, `og_runtime_tests`, and retry branches can continue after timeout/failure for partial coverage collection.

### Were thresholds or quality gates changed?
Yes, repeatedly (see CI section).
- Current enforced gate at HEAD is lower than the earlier 85/95 goal: **69% line / 86% function** (`81cb27a`, `.github/workflows/coverage.yml`).

## Unexpected Changes / Risks
1. **Coverage gate reduction at end of sequence**
- Final thresholds (69/86) are materially below prior enforced 85/95, reducing strictness.
- Risk: lower minimum accepted quality signal in CI.

2. **Persistent removal of several stress/path tests from CMake targets**
- Tests are present on disk but not built/executed.
- Risk: blind spots regress silently unless tests are reattached/replaced deliberately.

3. **`_Exit` in gparser help/version path**
- `std::_Exit(0)` bypasses normal cleanup and static destructors.
- Risk: side effects in tooling/integration contexts that relied on teardown hooks.

4. **Runtime coercion defaults introduced for invalid enum/index states**
- Safer than UB, but can mask caller bugs by silently clamping/fallback.
- Risk: latent data-flow errors become harder to detect without explicit logging/asserts.

## Recommended Follow-ups
1. Re-evaluate and document intended long-term coverage thresholds; if 69/86 is temporary, set a ratchet plan back to prior targets.
2. Audit removed tests (`test_effect_more_paths`, `test_walker_core_more`, `test_stats_more_paths`, `test_family_behaviors`, `test_level_data_coverage`, deleted `test_picker_dialogs_real`) and either:
   - restore with deterministic harnessing, or
   - replace with equivalent stable coverage cases and track the mapping.
3. Add explicit logging/assert telemetry where runtime now clamps invalid values (walker/special/order paths) to avoid silently hiding upstream defects.
4. Run a focused regression pass for:
   - command-line help/version path behavior after `_Exit` change,
   - zip self-archive avoidance edge cases,
   - sim input special-selection behavior with malformed family/special states.

## Appendix: commit list reviewed
Chronological list of commits reviewed in this audit window (`--since='2 days ago'`):

- `4603e28` 2026-02-19 01:08:28 +0000 Fix coverage CI workflow: upload full report dir, use relative paths
- `1504d3a` 2026-02-19 01:25:12 +0000 cmake: instrument libraries for coverage builds
- `b2c736c` 2026-02-19 01:43:14 +0000 ci: include openglad_test and exclude untestable paths in coverage
- `23a957f` 2026-02-19 02:10:40 +0000 ci: fix coverage test failures and include all src in coverage
- `6a11527` 2026-02-19 02:20:14 +0000 ci: set coverage thresholds to 1% line, 3% function
- `6c6ad5d` 2026-02-19 02:52:15 +0000 ci: increase coverage timeout and add per-test timeout guard
- `226e5f7` 2026-02-19 03:53:29 +0000 fix: call __gcov_dump() explicitly so coverage data is written on _exit()
- `567da7b` 2026-02-19 03:53:38 +0000 ci: raise coverage thresholds to 70% line, 75% function
- `a6c3372` 2026-02-19 04:02:56 +0000 ci: run PR coverage on head SHA instead of merge ref
- `d2759f9` 2026-02-19 04:32:08 +0000 ci: surface coverage metrics in job summary and PR comment
- `7eb053c` 2026-02-19 04:41:57 +0000 ci: derive displayed coverage from enforced gcovr run
- `10c8bf4` 2026-02-19 04:50:31 +0000 ci: split checkout by event to pin the intended ref
- `9ca2457` 2026-02-19 04:59:31 +0000 ci: retry openglad_test once in coverage workflow
- `aa21f6d` 2026-02-19 06:45:08 +0000 tests: add coverage tests for runtime paths, dialogs, and results
- `653c235` 2026-02-19 06:45:25 +0000 ci: raise coverage thresholds to 85 percent line, 95 percent function
- `2e4f21e` 2026-02-19 06:55:49 +0000 ci: keep coverage report non-blocking while thresholds are raised
- `e529107` 2026-02-19 07:12:45 +0000 Revert "ci: keep coverage report non-blocking while thresholds are raised"
- `217ab43` 2026-02-19 07:25:36 +0000 tests: add comprehensive coverage tests to reach 85 percent line 95 percent function
- `9dcee0b` 2026-02-19 07:49:55 +0000 tests: stabilize coverage suite by removing flaky dialog stress cases
- `a9e31a4` 2026-02-19 08:08:13 +0000 tests: add targeted coverage tests to reach 85% line 95% function
- `9163d1e` 2026-02-19 08:17:47 +0000 tests: expand runtime suite with stats/effect coverage targets
- `7ed060f` 2026-02-19 08:27:28 +0000 tests: broaden runtime coverage with view and level/save suites
- `35cd0f0` 2026-02-19 08:43:27 +0000 tests: add editor, picker, and results runtime coverage suites
- `a591203` 2026-02-19 09:12:04 +0000 tests: add targeted coverage for uncovered branches in level_data, smooth, entities, io, stats
- `7273dd6` 2026-02-19 09:42:28 +0000 tests: add targeted sim_input and cleric special tests
- `1e7feb7` 2026-02-19 09:49:43 +0000 tests: mass coverage - call uncovered functions across runtime modules
- `0440473` 2026-02-19 10:33:08 +0000 tests: deep branch coverage for cleric and living
- `475379c` 2026-02-19 10:40:10 +0000 tests: deep branch coverage for game logic modules
- `2e41c07` 2026-02-19 10:58:49 +0000 tests: deep branch coverage round 2 - families, effects, treasures, io
- `881e0ea` 2026-02-19 11:23:05 +0000 ci: ignore gcov suspicious-hit parse errors in coverage
- `6bdc356` 2026-02-19 11:51:33 +0000 tests: deep branch coverage round 3 batch 1 - smooth/sim_input/walker_movement
- `ac3cb79` 2026-02-19 11:54:21 +0000 tests: deep branch coverage round 3 batch 2 - walker/level_data/stats
- `0f63646` 2026-02-19 12:01:15 +0000 tests: deep branch coverage round 3 batch 3 - family callbacks cluster
- `c1c279d` 2026-02-19 12:04:30 +0000 tests: deep branch coverage round 3 batch 3 - effects cluster
- `6708fb5` 2026-02-19 12:10:12 +0000 tests: deep branch coverage round 3 batch 3 - weapons and treasure cluster
- `a6f440e` 2026-02-19 12:12:31 +0000 tests: deep branch coverage round 3 batch 3 - io core cluster
- `e671f20` 2026-02-19 12:36:56 +0000 tests: deep branch coverage round 4 - smooth level_data walker
- `9688859` 2026-02-19 12:39:36 +0000 tests: deep branch coverage round 4 - sim_input_handler stats archmage
- `83ba8ce` 2026-02-19 12:46:06 +0000 tests: deep branch coverage round 4 - cleric chain living obmap gparser zip
- `acb1472` 2026-02-19 12:50:34 +0000 tests: deep branch coverage round 4 - walker specials combat guy simworld treasure
- `37befe8` 2026-02-19 12:56:27 +0000 tests: deep branch coverage round 4 - druid soldier orc thief zip
- `c7b33ad` 2026-02-19 13:02:47 +0000 tests: deep branch coverage round 4 - cleric chain stability
- `5f204f9` 2026-02-19 13:07:55 +0000 tests: deep branch coverage round 4 - combat treasure simworld guy zip
- `cc0eda0` 2026-02-19 13:16:24 +0000 tests: deep branch coverage round 4 - walker combat simworld treasure zip gparser
- `b8a16dc` 2026-02-19 13:27:52 +0000 tests: extend branch coverage for smooth level_data sim_input walker
- `81228b2` 2026-02-19 13:30:57 +0000 tests: deep branch coverage round 4 - level_data sim_input walker
- `96f750a` 2026-02-19 13:32:34 +0000 tests: deep branch coverage round 4 - smooth level_data sim_input
- `6684b4a` 2026-02-19 13:45:03 +0000 tests: deep branch coverage round 4 - stats walker level_data
- `5dd025d` 2026-02-19 13:47:48 +0000 tests: deep branch coverage round 4 - sim_input living obmap
- `3dffad6` 2026-02-19 13:52:37 +0000 tests: deep branch coverage round 4 - sim_input walker followup
- `85b01ae` 2026-02-19 13:54:33 +0000 tests: deep branch coverage round 4 - sim_input walker branch probes
- `c2b7ce3` 2026-02-19 13:56:09 +0000 tests: deep branch coverage round 4 - walker init_fire/query paths
- `401b480` 2026-02-19 14:05:40 +0000 tests: deep branch coverage round 5 - walker
- `18a40d3` 2026-02-19 14:07:32 +0000 tests: deep branch coverage round 5 - level_data
- `26aa0b2` 2026-02-19 14:08:45 +0000 tests: deep branch coverage round 5 - walker_movement
- `4a5dc34` 2026-02-19 14:22:17 +0000 tests: deep branch coverage round 5 - tier2 tier3
- `c3f9b2b` 2026-02-19 14:39:50 +0000 tests: expand coverage for obmap/effect/druid/io/save paths
- `b9591e5` 2026-02-19 14:50:21 +0000 tests: add physfs wrapper, pixie move, and headless weap coverage
- `6e0a49c` 2026-02-19 15:00:16 +0000 tests: extend physfs wrapper and walker_specials edge coverage
- `023f8d5` 2026-02-19 15:04:07 +0000 tests: add branch coverage for zip, chain movement, and family callbacks
- `18bce0d` 2026-02-19 15:11:01 +0000 tests(unit): force og_file and physfs wrapper constructor coverage
- `5d171f5` 2026-02-19 15:21:40 +0000 tests: deep branch coverage round 6 - level_data walker smooth
- `9cc7118` 2026-02-19 15:24:35 +0000 tests: deep branch coverage round 6 - gparser stats walker_movement
- `1517995` 2026-02-19 15:31:11 +0000 tests: deep branch coverage round 6 - zip/og_file/platform/living/families/treasure
- `85db699` 2026-02-19 15:34:27 +0000 tests: deep branch coverage round 6 - sim_world/picker_state/gloader
- `59061d7` 2026-02-19 15:39:08 +0000 tests: deep branch coverage round 6 - level_data/walker/smooth
- `1f655bc` 2026-02-19 15:41:21 +0000 tests: deep branch coverage round 6 - walker_movement/stats/gparser
- `94670b5` 2026-02-19 15:43:42 +0000 tests: deep branch coverage round 6 - family mage/thief/soldier guards
- `79b9f2c` 2026-02-19 15:50:19 +0000 tests: deep branch coverage round 6 - treasure/living/sim_world/save_data/platform_io_common
- `77952c8` 2026-02-19 15:52:41 +0000 tests: deep branch coverage round 6 - zip_api/og_file/family_cleric/family_druid
- `ca5bd96` 2026-02-19 16:01:05 +0000 tests: deepen line coverage for level_data, stats, and walker movement
- `2e62b33` 2026-02-19 16:02:27 +0000 tests: scripted walker movement fallback branch coverage
- `bca4eff` 2026-02-19 16:05:44 +0000 tests: add walker and stats edge-path coverage checks
- `e383f61` 2026-02-19 16:18:54 +0000 tests: coverage round 7A - stats+smooth
- `a49f96c` 2026-02-19 16:19:05 +0000 tests: coverage round 7A - level_data+walker
- `97146dc` 2026-02-19 16:41:41 +0000 tests: coverage round 7 - gparser/level_data/picker/treasure/walker
- `6f7d35c` 2026-02-19 16:44:46 +0000 tests: coverage round 7 - picker/treasure
- `b700b7c` 2026-02-19 16:46:19 +0000 tests: coverage round 7 - io/walker-movement
- `8ad087c` 2026-02-19 16:47:48 +0000 tests: coverage round 7 - level-data paths
- `8f57fc1` 2026-02-19 16:54:50 +0000 tests: coverage round 7 - walker/stats/smooth/living/cleric
- `46e36d0` 2026-02-19 17:16:25 +0000 tests: coverage round 8 - walker_movement,smooth
- `2ef6416` 2026-02-19 17:21:00 +0000 tests: coverage round 8 - level_data,stats
- `c6895cf` 2026-02-19 17:22:00 +0000 tests: coverage round 8 - walker
- `a21d4de` 2026-02-19 17:28:03 +0000 tests: fix smooth ops make_center_pattern declaration
- `0ef1404` 2026-02-19 17:29:19 +0000 tests: coverage round 8 - family,io,save_data
- `b48b4a6` 2026-02-19 17:31:26 +0000 tests: coverage round 8 - combat,effect,sim,io
- `a522ad6` 2026-02-19 17:32:24 +0000 tests: coverage round 8 - effect,sim_world,save_data followup
- `116fd7e` 2026-02-19 17:37:14 +0000 tests: coverage round 8 - living,pathing,families,ui
- `cc10acc` 2026-02-19 17:44:54 +0000 tests: coverage round 8 - combat_math,weap,yaml,gloader
- `2265fcc` 2026-02-19 17:55:38 +0000 tests: coverage round 8 - walker,level_data,stats,smooth,families
- `ef9955a` 2026-02-19 17:57:32 +0000 tests: coverage round 9 - stats and smooth branches
- `94d92a1` 2026-02-19 18:02:22 +0000 tests: coverage round 10 - level_data and walker branches
- `89a1a58` 2026-02-19 18:03:50 +0000 tests: coverage round 11 - extra level_data and walker paths
- `50a5474` 2026-02-19 18:05:32 +0000 tests: coverage round 12 - level_data, stats, walker extras
- `424a23c` 2026-02-19 18:09:55 +0000 tests: coverage round 13 - level_data, smooth, walker branches
- `993594b` 2026-02-19 18:12:36 +0000 tests: coverage round 14 - obmap effect zip_api branches
- `0f9db8c` 2026-02-19 18:14:40 +0000 tests: coverage round 15 - walker and smooth extra branches
- `ec6dd40` 2026-02-19 18:17:26 +0000 tests: coverage round 16 - final level_data and walker branches
- `85dbe29` 2026-02-19 20:53:35 +0000 ci: trigger coverage run
- `222983e` 2026-02-19 21:47:48 +0000 ci: increase coverage job timeout to 60 minutes
- `290f0e3` 2026-02-19 23:00:48 +0000 ci: add concurrency group to prevent coverage run cancellation
- `813b4e9` 2026-02-20 00:04:56 +0000 ci: increase job timeout to 90min, reduce openglad_test per-attempt timeout to 600s
- `06f1875` 2026-02-20 01:37:20 +0000 ci: add timeouts to all test binaries to prevent CI hangs
- `05917f2` 2026-02-20 02:25:01 +0000 tests: fix hanging tests in data/runtime suites, adjust CI timeouts
- `08d9055` 2026-02-20 02:43:03 +0000 tests: cover additional walker movement edge branches
- `90d69ae` 2026-02-20 02:43:36 +0000 tests: cover gparser help/version commandline exit branches
- `8ba2ba3` 2026-02-20 02:44:21 +0000 tests: add targeted smooth dark-grass single-neighbor branches
- `1728322` 2026-02-20 02:51:44 +0000 tests: coverage round 9B - walker level_data stats
- `b77a098` 2026-02-20 02:51:52 +0000 tests: coverage round 9B - family callbacks and effect edges
- `731647e` 2026-02-20 03:13:57 +0000 tests: fix segfault in round 9B tests causing coverage drop
- `6c72b03` 2026-02-20 03:36:23 +0000 tests: add unit coverage push for gparser and family callbacks
- `d249849` 2026-02-20 03:36:30 +0000 tests: add smooth unit coverage branch matrix
- `b91dcd7` 2026-02-20 03:43:21 +0000 tests: add unit coverage push for walker, stats, and movement
- `06b03a5` 2026-02-20 03:43:36 +0000 tests: add unit coverage push for level_data and sim_input
- `eff56a0` 2026-02-20 04:10:07 +0000 tests: add r11 coverage pushes for smooth cleric stats input specials living
- `57e1841` 2026-02-20 04:14:13 +0000 tests: add r11 coverage for walker, level_data, movement
- `72a36d1` 2026-02-20 04:46:07 +0000 tests: add focused r12 coverage tests for runtime/entity branches
- `e1cb12d` 2026-02-20 05:19:28 +0000 tests: expand r11/r12 branch coverage for walker and families
- `370c985` 2026-02-20 05:22:37 +0000 tests: add r12 branch coverage for smooth gparser movement cleric
- `abd332f` 2026-02-20 05:26:06 +0000 tests: expand stats and living branch coverage
- `67fbc9b` 2026-02-20 05:51:24 +0000 tests: add r14 coverage for stats living and cleric
- `9f16b7c` 2026-02-20 05:51:33 +0000 tests: add r14 coverage for walker and walker movement
- `f64abc7` 2026-02-20 05:56:08 +0000 tests: add r14 coverage for smooth and level-data loaders
- `a7627ee` 2026-02-20 06:23:45 +0000 tests: add r15 orc and sim world coverage pushes
- `8e7f53b` 2026-02-20 06:28:59 +0000 tests: add r15 coverage for walker level_data cleric and zip
- `7fbd18b` 2026-02-20 06:55:00 +0000 tests: add final round r16 coverage bundle
- `8d98a68` 2026-02-20 07:14:55 +0000 tests: add r17 final coverage push
- `caa3c83` 2026-02-20 07:41:07 +0000 Add coverage round 18 unit tests for picker and movement branches
- `081008e` 2026-02-20 07:49:03 +0000 test: extend round 18 coverage for walker level_data smooth gparser
- `3f41cef` 2026-02-20 08:25:45 +0000 Stabilize coverage by chunking openglad_test execution
- `3370972` 2026-02-20 08:40:21 +0000 Revert coverage chunking and add r19 unit coverage tests
- `e6e320d` 2026-02-20 08:53:59 +0000 tests: dump gcov in unit test main before exit
- `5c0dafc` 2026-02-20 09:15:38 +0000 tests: add r20 coverage push across walker/family/smooth/level
- `f787e91` 2026-02-20 17:10:26 +0000 Fix CI test, baseline, and sanitizer failures
- `81cb27a` 2026-02-20 17:32:30 +0000 Fix gparser help/version exit path and align coverage gate