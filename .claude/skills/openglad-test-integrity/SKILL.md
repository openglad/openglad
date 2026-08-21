---
name: openglad-test-integrity
description: The rules that keep OpenGlad's test and coverage numbers honest — the coverage denominator invariant, banned assertion shapes, hang traps, and parity-golden authenticity. Use whenever a task touches coverage.yml, chases a coverage/function target, writes or audits tests, recaptures parity goldens, or debugs a hung/slow test run.
---

# OpenGlad test integrity

## The honest-denominator invariant

The coverage denominator is a pure function of repo contents. NEVER:
- add a gcovr/lcov `--exclude` or `--filter` narrowing,
- add a ctest `-E`/`-R` selection to the coverage workflow,
- add or raise a `--repeat until-pass` retry to turn a red run green
  (the `until-pass:3` pinned in test.yml and coverage.yml is deliberate
  and moves only in lockstep across both; a test that fails every
  attempt fails the gate, and a flake "fixed" by more retries is still
  a flake to root-cause),
- move a file out of src/, lower a threshold, or delete/disable/skip a
  test to go green.

One sanctioned exclusion list exists: `scripts/coverage/cpp_excluded.txt`
— tracked src/ TUs the coverage build genuinely cannot measure
(fuzz-only, emscripten-only), one per line with a mandatory reason,
rot-checked in both directions by scripts/coverage/coverage_report.py.
It makes an unmeasured TU an error instead of a silently smaller
denominator; it is never a lever for dodging poorly covered code. Any
coverage-gate PR reports the no-exclude numbers before and after and
states "denominator unchanged" explicitly. Raising a CI floor is a
separate, announced act — never a silent side effect. Read the current
thresholds from coverage.yml; never trust remembered numbers. CI's
retried runs can read slightly higher than a local single pass — judge
local work by its before/after delta.

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

## Coverage run mechanics (local)

- Judge a change by the local baseline→change DELTA in a worktree, not
  the absolute number: CI's retried runs accumulate `.gcda` and read
  higher than any local single pass.
- Incremental ci-coverage rebuilds leave stale `.gcda` ("overwriting ...
  different checksum" corrupts the number) — delete them after building,
  before running tests.
- Guard style: a one-line `if (!x) return v;` keeps the bad-path return
  on a covered line; a two-line if/return leaves an uncovered line each.
- To find CI's exact uncovered functions, read `FNDA:0` records from the
  run artifact's combined.info; local function HIT/no-hit is reliable
  even where line counts undercount.
- A function whose last caller was deleted is dead code that reds the
  function floor — delete it, don't write a test for it.
- Coverage-lane unit groups run ~10x slower under instrumentation
  against a fixed ctest timeout ceiling. When a group nears it, SPLIT
  the group (new binary + recorder_processes.txt line); never bump the
  ceiling.

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
- Known pre-existing shuffle hangs (proven on master; CI runs
  declaration order and is unaffected): og_test_picker seed 29
  (promote_orc detail-menu test) and og_test_view seed 7
  (base_camp_name_tap). Don't attribute these to new tests without
  reproducing on a clean tree.
- `Difficulty.submenu_door_flow` (og_test_menu_ui) is load-flaky and DOES
  hit CI ASan occasionally — a rerun clears it; a missed injector click
  under load leaves its cycle one short.
- Never bump a deadline to fix a timing-flaky test: measure the real
  cost, fix it, then convert the flat delay into a wait-on-condition
  with a generous ceiling, and prove the wait can still fail by planting
  a break. Gate cost regressions with counts (call counts), not clocks.

## Tests that hang (the three known traps)

1. Menu/prompt/picker paths need the injector-thread pattern
   (`wait_for_interactable` + `SDL_Delay(750)` + `interact`). Never
   drive a prompt from a TESTING exerciser — restrict exercisers to
   non-blocking data/mode/draw paths.
2. Never feed malformed YAML to gparser as a coverage target; it does
   not return.
3. Run ctest with stdin PIPED, never under a pty — headless clients
   block on terminal input under a pty and mimic a 180s regression.

Run long targets in the background or under a hard timeout; once stdin
is closed you cannot interrupt them.

## Proving a parity golden is authentic

The companion worktree must be the baseline commit plus recorder-only
commits. Verify: `git -C <companion> log --oneline <baseline>..HEAD` and
confirm every entry touches only tools/parity_*. A matching
`git merge-base` is NOT sufficient proof — never quote it as such.
`tests/parity/scenario_table.h` and the companion copy must be
byte-identical (`cmp`) before any recapture. "All N goldens matched
after rebasing to a different baseline" is a red flag to investigate,
never a success report.

For the full parity playbook — running the harness, the drift ledger,
canary teeth, pin rot modes, and harness blind spots — see the
openglad-parity skill.
