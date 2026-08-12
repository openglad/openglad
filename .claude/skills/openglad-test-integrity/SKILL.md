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
