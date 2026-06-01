# Master Baseline — Worktree, Build, Smoke Run, and Tooling Deltas

This document records the result of materializing `origin/master` as a sibling
worktree at `../openglad-master`, building it with the `ci-test` preset, and
running the full `ctest --preset ci-test` suite as the parity reference point.
It is the canonical "master baseline" consumed by all later phases.

- Primary repo: `/home/yans/code/openglad` (branch `wip/networking`).
- Master worktree: `/home/yans/code/openglad-master` (branch `parity-baseline-master`).
- Date: 2026-05-12.
- Host: Linux 6.17 / GCC 13.3.0 / CMake (system) / SDL2 2.30.0 / SDL2_mixer 2.8.0
  / GoogleTest 1.14.0.

## Worktree setup

Command executed from `/home/yans/code/openglad` (a clean `git fetch origin`
ran first):

```bash
git worktree add -b parity-baseline-master ../openglad-master origin/master
```

Output:

```
Preparing worktree (new branch 'parity-baseline-master')
branch 'parity-baseline-master' set up to track 'origin/master'.
HEAD is now at 16963de0 Fix ghost scare special and add regression tests for every special (#109)
```

Verification:

```bash
$ git -C ../openglad-master rev-parse HEAD
16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd
$ git -C /home/yans/code/openglad rev-parse origin/master
16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd
$ git -C ../openglad-master branch --show-current
parity-baseline-master
```

The worktree HEAD matches `origin/master`'s SHA exactly
(`16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`), as required by the plan. The
fresh local branch `parity-baseline-master` was created so Phase 05 can later
branch `parity-companion` off of it and commit master-side companion code
without touching `origin/master` itself.

## Build result

Configure:

```bash
$ cd ../openglad-master && cmake --preset ci-test
```

Configure summary (full log: `.plan/logs/master-ci-test-build.log`):

```
Preset CMake variables:
  BUILD_EDITOR="ON"
  BUILD_TESTING="ON"
  CMAKE_BUILD_TYPE="Debug"
  ENABLE_COVERAGE="OFF"
  ENABLE_SANITIZERS="OFF"

-- The C compiler identification is GNU 13.3.0
-- The CXX compiler identification is GNU 13.3.0
-- Found PkgConfig: /usr/bin/pkg-config (found version "1.8.1")
-- Checking for modules 'sdl2;SDL2_mixer'
--   Found sdl2, version 2.30.0
--   Found SDL2_mixer, version 2.8.0
-- Found GTest: /usr/lib/x86_64-linux-gnu/cmake/GTest/GTestConfig.cmake (found version "1.14.0")
-- Configuring done (18.5s)
-- Generating done (0.1s)
-- Build files have been written to: /home/yans/code/openglad-master/build/ci-test
```

No missing dependencies — `libsdl2-dev`, `libsdl2-mixer-dev`, and
`libgtest-dev` were already present from the branch's prior install.

Build:

```bash
$ cd ../openglad-master && cmake --build --preset ci-test
```

Result: all 739 build targets compiled and linked successfully (`[739/739]`).
Final linker steps include `og_unit_data`, `og_unit_entity`, `og_unit_families`,
`og_unit_sim`, every `og_test_*` group, and the user-facing binaries
`openglad`, `openscen`, `openglad_demo`, and `openglad_text`.

Sanity check on the artifact required by the verifier (`02a-check-baseline`):

```bash
$ test -x ../openglad-master/build/ci-test/openglad && echo OK
OK
```

Build warnings observed (all benign, all pre-existing on master, none promoted
to errors):

- `tests/test_mass_coverage.cpp:104` — unused `write_handler`.
- `tests/test_smooth_unit.cpp:212` — unused `get_at`.
- `tests/test_smooth_unit.cpp:584-585` — `int` → `unsigned char` narrowing
  conversions in test fixture.

These do not affect parity comparison and require no patching on master.

## Smoke run

```bash
$ cd ../openglad-master && ctest --preset ci-test
```

Exit code: **0**.

Totals (full log at `.plan/logs/master-ci-test-tests.log`):

```
100% tests passed, 0 tests failed out of 28
```

Breakdown:

| Category    | Count | Wall time (sum)        | Status       |
|-------------|-------|------------------------|--------------|
| integration | 20    | 176.26 sec*proc        | all passed   |
| unit        |  4    |   0.94 sec*proc        | all passed   |
| build       |  1    |   0.01 sec*proc        | passed       |
| emscripten  |  1    |   0.01 sec*proc        | skipped (no emsdk) |
| **Total**   | **28**| **181.26 sec real**    | **27 passed, 1 skipped, 0 failed** |

Per-test result (one ctest entry per binary; each binary contains many
GoogleTest cases):

```
 1 og_test_walker_combat               Passed
 2 og_test_walker_move                 Passed
 3 og_test_walker_core                 Passed
 4 og_test_families                    Passed
 5 og_test_effects                     Passed
 6 og_test_living                      Passed
 7 og_test_stats                       Passed
 8 og_test_guy                         Passed
 9 og_test_game_core                   Passed
10 og_test_screen                      Passed
11 og_test_view                        Passed
12 og_test_rendering                   Passed
13 og_test_picker                      Passed
14 og_test_menu_ui                     Passed
15 og_test_input                       Passed
16 og_test_level                       Passed
17 og_test_io                          Passed
18 og_test_smooth                      Passed
19 og_test_external                    Passed
20 og_test_mass_coverage               Passed
21 og_unit_sim                         Passed
22 og_unit_families                    Passed
23 og_unit_entity                      Passed
24 og_unit_data                        Passed
25 openglad_text_sim                   Passed
26 openglad_text_picker_interactive    Passed
27 openglad_text_unsupported           Passed
28 emscripten_build_test               Skipped (no emsdk in PATH)
```

**No pre-existing failures on `origin/master`.** Master is a clean green
baseline at SHA `16963de0`, so any divergence the parity harness later finds
that classifies as `regression` can be attributed to changes on
`wip/networking` and not to master flakiness.

The `emscripten_build_test` skip is expected: the host does not have `emsdk`
sourced; CMakePresets gates that test on `EMSCRIPTEN_AVAILABLE`. Skipping is
not a failure and is not relevant to gameplay parity.

## Tooling deltas vs branch

Branch = `wip/networking` (this repo); Master = `origin/master`
(`../openglad-master`). The parity harness must work around these deltas
because the design (Phase 03) requires equivalent state dumps and scenario
drivers on **both** sides.

### Test groups registered in CMakeLists.txt

| Test group                   | Branch | Master |
|------------------------------|--------|--------|
| `og_test_walker_combat`      | yes    | yes    |
| `og_test_walker_move`        | yes    | yes    |
| `og_test_walker_core`        | yes    | yes    |
| `og_test_families`           | yes    | yes    |
| `og_test_effects`            | yes    | yes    |
| `og_test_living`             | yes    | yes    |
| `og_test_stats`              | yes    | yes    |
| `og_test_guy`                | yes    | yes    |
| `og_test_game_core`          | yes    | yes    |
| `og_test_snapshot_safety`    | **yes (branch-only)** | no |
| `og_test_screen`             | yes    | yes    |
| `og_test_view`               | yes    | yes    |
| `og_test_rendering`          | yes    | yes    |
| `og_test_picker`             | yes    | yes    |
| `og_test_picker_network`     | **yes (branch-only)** | no |
| `og_test_menu_ui`            | yes    | yes    |
| `og_test_input`              | yes    | yes    |
| `og_test_level`              | yes    | yes    |
| `og_test_io`                 | yes    | yes    |
| `og_test_snapshot_benchmark` | **yes (branch-only)** | no |
| `og_test_smooth`             | yes    | yes    |
| `og_test_external`           | yes    | yes    |
| `og_test_mass_coverage`      | yes    | yes    |
| `og_unit_core`               | **yes (branch-only)** | no |
| `og_unit_sim`                | yes    | yes    |
| `og_unit_emscripten_transport` | **yes (branch-only)** | no |
| `og_unit_families`           | yes    | yes    |
| `og_unit_entity`             | yes    | yes    |
| `og_unit_data`               | yes    | yes    |

Net: branch adds 5 test groups (`og_test_snapshot_safety`,
`og_test_picker_network`, `og_test_snapshot_benchmark`, `og_unit_core`,
`og_unit_emscripten_transport`) that master cannot run. Master adds zero
groups the branch is missing.

### Headers (only those that matter for parity)

| Header                                                 | Branch | Master |
|--------------------------------------------------------|--------|--------|
| `include/openglad/gameplay/world_snapshot.h`           | yes    | **MISSING on master** |
| `include/openglad/gameplay/dirty_field_bits.h`         | yes    | **MISSING on master** |
| `include/openglad/gameplay/sim_event_log.h`            | yes    | yes (stub on master) |
| `include/openglad/server/headless_server_runtime.h`    | yes    | **MISSING on master** |

`SimEntity` accessor surface (`xpos()` / `set_xpos(...)`, `team_num()` /
`set_team_num(...)`, `family()` / `set_family(...)`) is branch-only. Master
exposes those as public data members. The parity dumpers must use the
per-branch idiom — accessor calls on the branch, public-field reads on master
— to populate the schema-v1 JSON.

### Source subtrees / scripts

| Path                                              | Branch | Master |
|---------------------------------------------------|--------|--------|
| `src/server/` (`headless_server_runtime.cpp`, `headless_tick_interval.cpp`, `server_main.cpp`) | present | **MISSING** |
| `scripts/test_headless_server_cli.sh`             | present | **MISSING** |
| `scripts/run_jitter_gtests.sh`                    | present | **MISSING** |
| `scripts/build_test.sh`                           | present | present (likely different content; not parity-relevant) |
| `tests/unit/test_world_snapshot.cpp`              | present | **MISSING** |
| `tests/unit/test_headless_server_runtime.cpp`     | present | **MISSING** |
| `tests/unit/test_replay.cpp`                      | present | **MISSING** |
| `tests/unit/test_sim_world_headless.cpp`          | present | present (different content) |

### Implications for Phase 03+

1. The branch's deterministic-sim test scaffolding
   (`test_world_snapshot.cpp`, `test_headless_server_runtime.cpp`,
   `test_replay.cpp`) cannot be ported verbatim to master — master lacks the
   underlying snapshot / dirty-bit / headless-server-runtime infrastructure.
2. The master companion (Phase 05) must drive scenarios with a hand-rolled
   `world().tick()` loop modelled on `tests/test_game_world_fixture.h` (master
   side) and `tests/unit/test_replay.cpp` (branch side, as the canonical
   pattern). RNG seeding via `world().rng_.state_ = seed;` works on both
   sides because `og::sim::SimRandom::state_` is public on both branches.
3. The state dumper must be implemented twice — one TU per branch — but emit
   identical canonical JSON (schema v1, defined in Phase 03). On master it
   reads `walker->xpos`, `walker->team_num`, `walker->family`; on the
   branch it calls the accessors. Both call `walker->stats()->hitpoints`,
   which is available on both branches.
4. Master has **no pre-existing test failures**, so the parity-vs-branch
   diff is unambiguous: any scenario where the branch dump differs from the
   master golden is either a real regression or an intended divergence to be
   cited per Phase 06's strict classification rule.

## Logs

- `.plan/logs/master-ci-test-build.log` — full `cmake --preset ci-test` plus
  `cmake --build --preset ci-test` output (2060 lines, gitignored).
- `.plan/logs/master-ci-test-tests.log` — full `ctest --preset ci-test`
  output (70 lines, gitignored).

## How to reproduce

```bash
cd /home/yans/code/openglad
git fetch origin
git worktree add -b parity-baseline-master ../openglad-master origin/master   # one-time
cd ../openglad-master
cmake --preset ci-test
cmake --build --preset ci-test
ctest --preset ci-test
```

Expected: exit 0, 27 passed, 1 skipped (`emscripten_build_test`), 0 failed.
