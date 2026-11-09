---
name: openglad-test-integrity
description: The rules that keep OpenGlad's test and coverage numbers honest — the coverage denominator invariant, banned assertion shapes, hang traps, and parity-golden authenticity. Use whenever a task touches coverage.yml, chases a coverage/function target, writes or audits tests, recaptures parity goldens, or debugs a hung/slow test run.
---

# OpenGlad test integrity

## The honest-denominator invariant

The coverage denominator is a pure function of repository contents. NEVER:
- narrow the measured source/header inventory with a gcovr/lcov exclusion,
- omit tests from the required coverage population,
- use `--repeat until-pass` or replace a failed attempt's results with a rerun,
- move production code out of the measured roots, lower a threshold, or
  delete/disable/skip a behavioral test to make a gate pass.

Every required native CTest entry runs once. A later diagnostic run may help
explain a failure, but the first failure remains a failed gate until its cause
is fixed. Keep failed logs and profiles separate from a new measurement.

Coverage sharding is allowed only through the complete inventory contract in
`scripts/ci/test_matrix.py`: generate a plan from CTest, keep fixture setup,
consumers and cleanup together, and require exactly one result per planned test
and one report per shard. The union must equal the original inventory. The
explicit delegation of `emscripten_build_test` to the required WASM graph is
recorded in that plan; it is not a discretionary exclusion. An absent, skipped,
disabled, duplicated or failed result fails the gate. TSan's focused network
selection is a separate lane and never defines the coverage population.

Within a GoogleTest binary, bounded CTest entries use the upstream shard
protocol with a complete, unique index set and the original case filter. Each
case must run exactly once across those entries. Preserve deliberate fixture
leak pairs in one process; splitting an A/B cleanup oracle makes it vacuous.
Native result parsing retains full process output and rejects internal
GoogleTest skips of mandatory cases, even if the CTest entry exits zero.
Existing opt-in recording, benchmark, deployed-relay and unavailable display
checks are reported by exact name and reason; sanitizer budget skips are not
allowed.

Every coverage shard starts with clean profiles and the same compiler notes.
Collect the complete C++ source/header grid, including zero-hit translation
units, and verify identical grids before merging counts. Merge all Lua process
evidence and source sidecars; require the full recorder-process manifest.
Never copy one shard's profiles into another or merge failed/retried attempts.

One sanctioned exclusion list exists: `scripts/coverage/cpp_excluded.txt`
— tracked src/ TUs the coverage build genuinely cannot measure
(no executable lines, Emscripten-only), one per line with a mandatory reason,
rot-checked in both directions by `scripts/coverage/coverage_report.py`.
Tracked `include/openglad/` headers also belong to the C++ denominator; a header
with executable lines or functions cannot disappear from the report. Test-only
exerciser implementations belong in `tests/coverage_internal/` or linked
`tests/support/` code, with guarded declarations at the production boundary.
Check callers with `rg -uu` or `git grep` before classifying an unguarded API as
test-only; ignored source files can contain production callers. Runtime reads
of injected state and all shared behavior remain in the measured product.
`OPENGLAD_TEST_SUPPORT` may include test observers when a unit group links the
production archive; it must not select alternate runtime behavior.

Report before/after counts and percentages over the same declared inventory.
Say "denominator unchanged" only when it actually is; an authorized expansion
or a test-only helper relocation must be itemized with its effect on the counts.
Raising a CI floor is a separate, announced act. Read thresholds from the current
workflow and reporter configuration, never remembered values. Do not claim a
percentage until the complete fresh run and merged report have passed.
The current C++ gate requires strictly more than 98% line coverage and 100%
function coverage; exactly 98% does not pass.

## Banned assertion shapes

An assertion an empty or broken result also satisfies is not a test:

    ASSERT_TRUE(helper() > 0)          // banned
    EXPECT_GE(score, 3)                // banned when total > 3
    EXPECT_TRUE(x || !x); rc==0||rc==1 // banned tautologies
    parse(...).size() <= 1             // banned
    (void)call();                      // banned as the only "check"

Internal exercisers assert an EXACT check count —
`ASSERT_EQ(kExpectedChecks, helper())` — and each internal check asserts
a postcondition, not that the call returned. Every test asserts a
specific expected value or state transition. Before claiming a coverage
number, self-audit your new tests against this list.

## Proving a test has teeth (planted breaks)

Every fixed or new test is proven by breaking the product and watching it
go red:

1. Plant the break in the product implementation (`src/`, or the measured
   `include/openglad/` header that defines the rule): an early return, a
   flipped comparison, or a wrong constant. For a source tripwire (a test whose oracle is the source
   tree) the plant goes in the sources it scans, not in src/.
2. Batch independent mutations in different functions: one build, one run
   of the group's binaries, each test red for ITS mutation. Two mutations
   that could mask each other go in separate batches.
3. Record per test the mutation (file:line, old → new) and the
   `[  FAILED  ]` line it produced.
4. Revert src/, rebuild, rerun: green again. `git status --short src
   include packs` is empty before you commit — a planted break is never
   committed.

A test that stays green under its mutation is not fixed: strengthen it,
or find the observable the rule actually moves.

Plant breaks that compile. The ci-test, ci-asan and ci-tsan presets set
`OPENGLAD_WARNINGS_AS_ERRORS=ON` (CMakePresets.json; ci-coverage and
ci-fuzz do not, and the option defaults OFF in CMakeLists.txt) over
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow`
(CMakeLists.txt, `OG_WARNING_FLAGS`), so on those lanes a mutation that
does not build proves nothing and costs a rebuild. The forms that
survive -Werror:

- Prepend the early exit and brace it — `if (true) { return v; }` — above
  the old body rather than replacing it, so the function's parameters and
  locals stay used.
- Do not plant into the body of an unbraced `if`/`for`/`while`: the plant
  becomes that body, orphans the statement under it, and GCC reports
  `this 'if' clause does not guard... [-Werror=misleading-indentation]`.
  Bracing the plant does not cure that one — move it into a braced block.
- Replacing a body wholesale needs `(void)` on every parameter:
  `(void)x; (void)y; return true;`. The old locals went with the body,
  so what fires is `error: unused parameter 'x'
  [-Werror=unused-parameter]`, once per parameter.
- Substituting a value for a parameter keeps a read of it: `alpha | 0xFF`,
  not `255`; an out-parameter forced to `nullptr` needs `(void)r;`.
- Deleting a variable's last READER — the statement that consumed it —
  leaves it set-but-unused: `error: variable 'name' set but not used
  [-Werror=unused-but-set-variable]`, or `error: unused variable 'name'
  [-Werror=unused-variable]` when the declaration was its only write.
  Add `(void)name;`. Deleting its last WRITER is the other case and
  `(void)` does not help there: with an initialiser on the declaration
  it compiles clean, without one it is `error: 'name' is used
  uninitialized [-Werror=uninitialized]` and the plant needs a value.
- Constants must match the target type and must not shadow: `return -1;`
  from an `unsigned char` function is an error under -Wsign-conversion,
  and a plant that re-declares a live local name is one under -Wshadow.

## Coverage run mechanics (local)

- Compare baseline and change using one complete first-attempt population
  each, the same toolchain, and clean profiles. Report counts with the delta;
  partial or diagnostic runs do not establish the required coverage result.
- Incremental ci-coverage rebuilds leave stale `.gcda` ("overwriting ...
  different checksum" corrupts the number) — delete them after building,
  before running tests.
- Keep branches readable. Do not combine an unexecuted return with a covered
  line to improve the percentage; test the bad path through an observable result.
- Read uncovered function identities and declaration lines from the canonical
  report's `summary.json` or `cpp.json`. Compare the same source/header scope
  and compiler settings before interpreting a local/CI difference.
- A missing direct caller does not prove an API or default interface contract
  is obsolete. Audit callers and contracts before deleting an implementation;
  preserve required behavior and test its observable effects.
- Instrumented groups keep their existing CTest timeout ceilings. Partition a
  slow group's cases into bounded CTest entries with a complete, disjoint union;
  keep cross-test cleanup pairs together. A new `recorder_processes.txt` entry
  is needed only when a new executable actually records Lua, not for another
  CTest entry using the same binary. Never raise the timeout to avoid a split.

## Rejected deletion classes

A guard that exists because a LIBRARY promises never to hand us the bad
value is not dead code, and deleting it is not a coverage lever. These
are rejected as a class (feat/coverage-97 ruling, 2026-09-30): libyaml
and libzip failure arms and caller-checked null guards (wave 1's WP-RES
deletions, e.g. 729e72d7 and f6fbe004, never merged), SDL 3.4.x
argument-contract arms (`SDL_LockSurface`, `SDL_FillSurfaceRect`,
`SDL_MUSTLOCK`), and IXWebSocket contract guards (dc2bb8fb, reverted).
The pinned library version is not a proof: the guard is what keeps a
bump from becoming a crash. Leave these lines uncovered, list them in the
report as "rejected class", and never propose them again. A deletion
needs a REPO-INTERNAL proof (the last caller, the arithmetic, the enum).

## Entity pointers do not survive the tick (ASan-only bug class)

`GameWorld::tick`'s erase sweep frees dead weapons/effects the same tick
(living corpses persist). A raw `walker*` held across `tick()` is a
use-after-free that non-ASan builds "pass" — only the ci-asan preset
catches it. Idiom: capture `entity_id()` at spawn, judge fate via
`world().find_by_id(id)` after the tick, and pair a survivor control
with the reaped entity or the assertion has no teeth. New heavy test
files run their group under ci-asan locally before push. ASan aborts on
first error, so after fixing an ASan red, run the WHOLE binary — every
test after the failure never ran.

## Flaky vs real (adjudication before blame)

- Order-dependence fixes are verified with a ~30-seed `--gtest_shuffle`
  sweep, not one run. In render tests, compute `world_to_screen_*` only
  AFTER a settle redraw (the first redraw pans the camera) — the classic
  passes-in-order, fails-shuffled shape.
- The two shuffle hangs recorded here — og_test_picker seed 29
  (promote_orc detail-menu test) and og_test_view seed 7
  (base_camp_name_tap) — were one bug each and are fixed: a picker
  lobby client outliving the test that made it, whose next
  picker_lobby_poll() freed the following test's roster. The
  integration harness now drops the standalone client between tests
  ([LOBBY-R1] in tests/integration/integration_main.cpp); run with
  OPENGLAD_TEST_LOBBY_CENSUS=1 to list the tests that end holding one.
  Still don't attribute a shuffle failure to new tests without
  reproducing on a clean tree.
- [SAVE-R9], the same treatment for company files: between every pair
  of integration tests the harness deletes every save/*.gtl (save0
  included) and every save/backups/<slot>.NNN.gtl whose slot is not in
  the process baseline, un-pins the company clock and zeroes the live
  save's last_played_unix_s, so company files, the live stamp and the
  clock pin all return to the process baseline. The baseline is the
  [SAVE-R5](c) stray-slot seeds, which is why the diagnostic survives
  the whole run. The a/b pin for it lives in
  tests/integration/test_company_litter_guard.cpp; a per-test teardown
  reaper is now a rule twin, not a safety net. CONTINUE opens the MOST
  RECENT company, so a leaked file re-targets a later flow's whole
  session and the red lands nowhere near the leaker.
- A FILTERED `--gtest_shuffle` run preserves the full run's relative
  order for the same seed AND the same binary (gtest shuffles the whole
  registered set, then applies the filter), so a cheap filtered census
  finds a bad order without paying for the full binary. Any change to
  the registered test set re-deals every seed, so a seed recorded
  before you added a test proves nothing after.
- `Difficulty.submenu_door_flow` (og_test_menu_ui) used to be recorded
  here as load-flaky with "a rerun clears it". It was not load: its
  per-click oracle was `wait_for_interactable_label_change`, which
  returns true for ANY label that differs from the snapshot, so a click
  the row never saw still counted and the lap assertion sixty lines
  later read one short. The helper now reads the stored VALUE on the
  menu thread and re-clicks, deadline-bounded, until it moves — the
  shape `click_cycle_step` in test_options_menu.cpp has always used.
  A rerun is never the answer to a flaky injector flow; find the oracle
  that certifies a click nobody consumed.
- Never bump a deadline to fix a timing-flaky test: measure the real
  cost, fix it, then convert the flat delay into a wait-on-condition
  with a generous ceiling, and prove the wait can still fail by planting
  a break (see "Proving a test has teeth"). Gate cost regressions with
  counts (call counts), not clocks.

## Tests that hang (the three known traps)

1. Menu/prompt/picker paths need the injector-thread pattern
   (`wait_for_interactable` + `wait_for_menu_frames(n)` + `interact`;
   there is no fade to wait for under TESTING, so never a flat delay).
   Never drive a prompt from a TESTING exerciser — restrict exercisers
   to non-blocking data/mode/draw paths. An injector that drives a
   BLOCKING menu from the main thread must never bound its escape tail
   by wall clock: `run_pause_menu` returns only when something clicks
   its way out, so a tail that stops clicking guarantees the hang it
   exists to prevent. Loop until the main thread signals it left the
   menu, and click BACK as well as RESUME — the player sub-screen
   publishes no RESUME. The rule lives once, in
   `tests/test_escape_tail.h` (`escape_to_the_main_thread`, with an
   `EscapeDoor` table for the flow's screens and a `hold_lap` predicate
   for laps that must not press); every injector in the tree calls it,
   and `scripts/check_escape_tail_twins.py` fails the test build on a
   new hand-rolled copy (the openglad-menus skill carries the binding
   procedure). A capture/scene test never skips on a missing
   env var — it asserts its oracle always and writes media only when
   asked:
   `RenderEffects.damage_number_glyphs_paint_in_the_requested_band`
   through `fx_capture::dump_frame`, `MenuCapture.zz_capture_*`
   through the one presented-frame handshake in
   `tests/test_frame_capture.h` (`capture_presented_frame` /
   `verify_captured_frames`); do not add another copy of either.
   The `NewSpecials.*` and `GameplayFixes.*` scene tests
   (`tests/integration/test_special_scenes.cpp`, binary
   `og_test_special_scenes`) are the same shape: one gtest per scene, each
   asserting its tells on every lane and writing frames only under
   `OG_FX_CAPTURE_DIR`, which is what the two tools under `scripts/media/`
   set; one scene is selected with `--gtest_filter`, never with an env var
   of its own. The shared play-through helpers live once, in
   `tests/test_live_game.h`.
   Cinematic utilities with no behavioral regression oracle belong in the
   explicit `og_capture_gameplay` tool (`tools/capture_gameplay/scenes.inc`),
   which has no CTest registration. Moving one requires retaining its recording
   behavior and proving no semantic case or coverage obligation was removed.
   The real-gameplay, spectator, split-screen and physical-HUD regressions stay
   unconditional; `OG_FX_CAPTURE_DIR` controls only their output files.
2. Never feed malformed YAML to gparser as a coverage target; it does
   not return.
3. Run ctest with stdin PIPED, never under a pty — headless clients
   block on terminal input under a pty and mimic a 180s regression.

Run long targets in the background or under a hard timeout; once stdin
is closed you cannot interrupt them.

## Proving a parity golden is authentic

A golden is authentic when it `cmp`s equal to a capture from the old game
(branch `parity-companion`) at the commit on the pin line of
`tests/parity/golden/DRIFT_LEDGER.md` ("Old-game commit the goldens are
captured from: ..."), made with `scripts/parity/capture_master_golden.sh
--out-dir <scratch>`; the ledger's "Recapturing every golden from the old
game" is the recipe. No golden is ever captured from the current game, and
since #349 every golden `cmp`s equal to that capture (the ledger's "Open"
list is empty).

The old game is the e761 baseline rebuilt as `a2d9d470`, plus two kinds of
commit: recorder-only commits and behaviour ports, one fix per commit, whose
message names the current-game commit they mirror. Every old-game commit that
touches `src/` must have an `old-game commit` cell in the ledger's "Ported
fixes" (the ports made before the rule, such as `4e1af2a1` and the
armor-roll commits, have cells too). `scripts/check_parity_companion_refs.sh`
(an `og_test_parity` build dependency) makes that verification: ancestry on
`origin/parity-companion`, the scenario table `cmp` at the pin, and a cell
for every `src/` commit. A matching `git merge-base` is NOT sufficient proof
— never quote it as such; merge-base adjudication only tells you whether a
divergence is yours.
`tests/parity/scenario_table.h` and the old game's
`tools/parity_scenario_table.h` must be byte-identical (`cmp`) before any
capture. A hand-edited golden fails the byte compare; an edited golden is
never a way to green. "All N goldens matched after rebasing to a different
baseline" is a red flag to investigate, never a success report.

For the full parity playbook — running the harness, the drift ledger,
canary teeth, pin rot modes, and harness blind spots — see the
openglad-parity skill.
