# Simulation semantics

Every branch point in the simulation is a game rule or a piece of plumbing,
and the **semantics manifest** says which, line by line. A rule names what it
decides, the outcome of each of its arms, and the tests that assert those
outcomes; plumbing says in one note why there is nothing to assert. The
manifest is checked on every CI run, so "every rule in the game has a
deterministic test that asserts it" is a fact the check proves, not a claim,
and it stays true when the simulation changes: a new `if` in `src/gameplay`
is red until someone says what it decides and how a test sees it.

The first manifest came from a review of the whole simulation: every
candidate branch line of the C++ in `src/gameplay` and its public headers
(networking, snapshot serialization, mapgen and the lobby are out of scope)
and of every class-pack Lua file under `packs/`.

## The rule

1. Every branch line in an in-scope file belongs to exactly one row.
2. A **rule** row cites at least one test, and every test it cites exists
   and is not disabled. A row deferred with an issue number may cite none:
   the issue is its pointer. So may an unreachable row, whose note says why
   no test can reach it.
3. A rule row's status is `asserted`: every arm's outcome is checked by an
   `EXPECT_*` / `ASSERT_*` on the outcome itself, or by a parity fact that
   reads it. Reaching the line is not asserting it. A row may instead be
   `deferred` with an issue number, for instance when the current game
   differs from the old game and the difference is filed rather than
   pinned, or `unreachable` with a note saying why no test can reach the
   arms it still lists as missing.
4. An **other** row (plumbing, a defensive guard, a loop, a dead hit of the
   grep, a test-only path) carries a note and no tests.

## Where it lives

| what | where |
|---|---|
| the manifest | `tests/semantics/manifest/<basename>.json`, one file per source file (`walker.cpp.json`, `living-03-mage.lua.json`; when two in-scope files share a basename, the parent directory is prefixed: `script-foo.cpp.json`) |
| the check | `scripts/semantics/check_manifest.py` |
| the in-scope file list and the branch grep | `SCOPE_CPP`, `SCOPE_LUA_GLOB`, `CPP_BRANCH` and `LUA_BRANCH` at the top of the check, the only place they are spelled |
| the generator | `scripts/semantics/gen_manifest.py`, which built the first manifest from the review's files |
| the tests the review added | `tests/unit/test_sem_<area>_<n>.cpp`, one unit binary each (`og_unit_sem_<area>_<n>`), registered in `cmake/semantics/<area>.cmake`; the areas are walker, ai, world, script, families and libs |
| ctest | `semantics_manifest_check` (the check on the tree) and `semantics_manifest_selftest` |

A manifest file is `{"path": ..., "rules": [...], "other": [...]}`, rows in
line order. A rule row:

```json
{
  "id": "treasure_navigation.lua:60",
  "lines": [60, 74],
  "kinds": ["and", "if"],
  "function": "exit_on_eat",
  "anchor": "and not og.level_completed(og.current_scenario())",
  "condition": "level_completed(dest) and not level_completed(current) ; if can_withdraw",
  "semantic": "With foes still alive, an exit to an already completed level withdraws ... but never from a replay of an already completed level.",
  "arms": [
    {"when": "dest done, current not", "observable": "withdraw_requested, RequestExitConfirmation b=1, WithdrawToLevel"},
    {"when": "current also done (replay)", "observable": "no withdraw; 'Foes remain!'"}
  ],
  "setting": "none",
  "status": "asserted",
  "tests": [
    {"parity": "level_withdraw_scen99", "fact": "EventKindAtLeast(withdraw_to_level,1)"},
    {"file": "tests/unit/test_sim_world_headless.cpp", "test": "SimWorldHeadless.replay_of_a_completed_level_cannot_withdraw"}
  ],
  "missing_arms": []
}
```

(The real row cites one more parity row; the generator writes `lines` and
`kinds` one entry per line.)

- `id` names the row; it keeps the line it was first written at and does not
  change when the row moves.
- `lines` are the branch lines the row covers (a rule whose condition spans
  an `if` and its `else if` covers both). Only branch lines: the opening
  brace of a function or a statement between two arms is not listed.
- `kinds` says, for each of `lines`, which branch the grep saw there: the
  keyword it matched (`if`, `else if`, `else`, `switch`, `case`, `default`,
  `while`, `for`, `do`, or `ternary`; in Lua `if`, `elseif`, `else`,
  `while`, `for`, `repeat`, `until`, `and`, `or`), or `""` for an anchor
  line that is no branch line. It is how the check tells that a line a row
  claims has become a different branch.
- `anchor` is the trimmed source text of the row's first line that says
  something of its own: the `if (...)` rather than a bare `else` or
  `default:` before it. When that is not the first of `lines`, the row also
  has `anchor_index`, the position of the anchor line in `lines` (0 is the
  default and is not written). The anchor is how the check finds the row
  again after an edit.
- `condition` is the rule in words; `semantic` says what it decides; `arms`
  list each outcome and how a test sees it.
- `setting` is `none`, `fixes-off`, `fixes-on`, `both` or `new-specials`:
  which value of Gameplay Fixes or New Specials the arms depend on. A row
  that depends on a setting is asserted under each value.
- `status` is `asserted`, `partial` (some arms are), `executed-only` (tests
  reach the line and assert none of its arms), `none` (nothing reaches it),
  `deferred` (with `"issue": "#451"`) or `unreachable` (with a `note`). A
  deferred row needs its issue; with one it may cite no test, which is how a
  classic-path row whose current game differs from the old game is
  recorded: no test pins the difference, the issue tracks it. A deferred row
  without an issue is red twice, as `no test` (when it cites none) and as
  `deferred without issue`.
- `unreachable` says that no test can reach the arms the row still lists in
  `missing_arms`: a defensive guard no caller can trip, a `switch` default
  no value reaches, a function nothing but tests calls. It is allowed only
  when the reviewer of the tests written for that file confirmed the arm is
  dead, and the maintainer may still ask for the code to be removed
  instead. The row's `note` says why the arm cannot be reached (the check
  prints the first entry of the list); with that note the row may cite no
  test and stays green. Without a note, or with an empty one, it is red as
  `unreachable without reason`, and also as `no test` when it cites none.
  An unreachable row carries no `issue`: there is nothing to fix, only
  code that cannot run.
- `tests` cite a GoogleTest as `{"file", "test": "Suite.name"}` or a parity
  row as `{"parity": "<row id>", "fact": "..."}`; either may carry a `note`
  saying which arm it asserts. A row-level `note` list holds remarks that
  cite no test, such as an arm nothing asserts yet.

An other row is `{"id", "lines", "kinds", "kind", "anchor", "note"}` (and `function`
when it has one), `kind` one of `plumbing`, `defensive`, `loop`, `dead`,
`test-only`.

## What counts as a branch line

The check uses the review's grep exactly. A C++ line is a candidate when it
starts with `if`, `else if`, `else`, `switch`, `case`, `default`, `while`,
`for` or `do` (optionally after a closing `}`), or holds a `? ... :` ternary.
A Lua line is a candidate when it starts with `if`, `elseif`, `else`,
`while`, `for`, `repeat` or `until`, or holds the word `and` or `or`
anywhere. Lines whose trimmed text starts with `//`, `--` or `*` are
skipped. The grep is deliberately blunt: it catches `and` inside a Lua
string, a `?:` in a format string, a `for` that only walks a list. Those
lines still need a row, usually `dead` or `loop` with a one-line note, so
the check never has to guess what a line means.

## Adding a branch

When you add an `if`, a `case`, a ternary or a Lua `and`/`or` to an
in-scope file, the check reports it:

```
uncovered: src/gameplay/walker.cpp:812: if (stats->frozen_delay > 0)
```

Add a row to that file's manifest:

- If the branch decides something a player could see, write a rule row:
  the condition, what it decides, every arm with its outcome, the setting
  it depends on, and the tests that assert each arm. Write the tests first;
  the row is `asserted` only when every arm is asserted.
- If it is plumbing (a null guard, a container walk, determinism plumbing,
  a test-only path), write an other row with a note that says so.
- If the line extends an existing rule (a new `else if` of the same
  decision), add the line to that rule's `lines` and its arm to `arms`,
  with the test that asserts the new arm.

Set `anchor` to the trimmed text of the row's first line (or of its first
line that is not a bare `else`, with `anchor_index` pointing at it). Leave
`kinds` out and run `--fix`, which writes them from the lines the row
claims; then run the check.

## Running the check

```bash
python3 scripts/semantics/check_manifest.py            # check the tree
python3 scripts/semantics/check_manifest.py --fix      # re-point moved rows, then check
python3 scripts/semantics/check_manifest.py --report /tmp/semantics.txt
python3 scripts/semantics/check_manifest.py --self-test
ctest -R semantics_manifest                            # both, from a build directory
```

Exit 0 means every rule holds; 1 means the output lists violations; 2 means
the check could not run (no manifest directory, a file that is not JSON, a
row missing a field). The output ends with the counts:

```
check_manifest: FAIL: 159 files, 4678 candidate lines, 1504 rule rows, 448 other rows
  R1 uncovered 0, double-claimed 0, stale line 0, missing kinds 0, drift 0, ...
  R2 no test 0, missing test 0, disabled test 0, missing parity row 0
  R3 not asserted 3 (partial 1, executed-only 2, none 0), deferred 2 (without issue 0), unreachable 1 (without reason 0)
```

### Moving rows: --fix

Editing a file moves the lines below the edit. The check notices because a
row's anchor line no longer holds its anchor, and finds the anchor
elsewhere in the file. The same text often appears more than once (two
`if (target)` a few lines apart), so the check takes, in this order: the
occurrence inside the row's function (a function definition above it, not
yet closed by another at the same depth, has a name the row's `function`
mentions); one below the row before it, because an edit never reorders
lines; and the one nearest where the row would be had it moved as far as
the row before it did. When the line the row would be on is inside its
function and no occurrence is, the condition was rewritten in place, and
the row is reported lost rather than moved onto a twin in another
function. A row whose anchor is a bare
`else`, `default:` or `end` says nothing about where it belongs, so it
moves with the row before it in the same function (or, failing that, in
the file), and only when that lands it on a line that reads exactly its
anchor.

The anchors found cut the file into stretches. Between two anchors, the
branch lines the rows claim are matched in order with the branch lines the
file now has there, whenever there are as many of each, and each match
only between two branches of the same kind: an edit between two arms of
one rule then moves the lower arm and leaves the upper one, while an
`else` deleted and an `if` added in the same stretch leaves the `if`
uncovered instead of handing it the `else`'s place. When a stretch gained
or lost a branch line, or the kinds do not match, its lines move with the
anchor above them, and the check reports what does not fit: the new
branch as `uncovered`, the line a removed arm leaves behind as `stale
line`.

Without `--fix` every row that moved is a `drift` error naming the line
its anchor now sits on. `--fix` writes the new lines into the manifest
file and prints a `fixed:` line for every row it moved; read them, since a
moved line is only matched by position and kind, never by its text.
Running it twice changes nothing the second time. What `--fix` cannot place
stays red: an edit that adds or removes a branch always needs a row written
or a line moved by hand.

The check stores text only for a row's anchor line. A line below the
anchor is known by its position and its kind, so a rewrite that keeps the
kind (`case 3:` becoming `case 4:`, `if (a)` becoming `if (b)` on an arm's
line) passes unseen; one that changes it (`else` becoming `else if (x)`) is
a `stale line`.

`--fix` only ever writes files in `tests/semantics/manifest/`. It never
touches the simulation, never changes a status and never adds a row.

### What each error means

| error | meaning | what to do |
|---|---|---|
| `uncovered` | a branch line no row claims | add a row, or add the line to the rule it belongs to |
| `double-claimed` | two rows claim the same line | give the line to one of them |
| `stale line` | a line a row claims is no longer the branch its `kinds` says (an arm was deleted, or rewritten into another kind of branch) | move the line out of the row, or update the row's `lines`, `kinds` and `arms` to the code |
| `missing kinds` | a row has no `kinds` | run `--fix`, which writes them |
| `drift` | a row's anchor, or a line below it, moved | run `--fix` and read the `fixed:` lines it prints: each says where a row went, and a line matched by position may have landed on a different branch of the same kind |
| `lost anchor` | the anchor text is nowhere in the file, or only in other functions | the condition was rewritten: update `anchor` and `lines` by hand, and check the rule still says what the code does |
| `unanchored` | a bare `else` / `default:` row is no longer on its text and no row before it carried it there | point its `lines` at the line by hand |
| `out of range` | a row names a line past the end of the file | the file shrank: fix the row's `lines` |
| `missing file` | an in-scope C++ file is gone | a rename: update `SCOPE_CPP` and the manifest's `path` |
| `stray manifest` | a manifest file describes a file outside the scope | delete it, or move its rows to the file's new name |
| `duplicate manifest` | two manifest files describe one source file | merge them |
| `no test` | a rule row cites no test, and is neither deferred with an issue nor unreachable with a reason | cite the test that asserts it, or write one |
| `missing test` | the cited file has no `TEST`, `TEST_F` or `TEST_P` of that name outside comments and `#if 0` (a test stamped out by a file-local macro counts) | the test was renamed or removed: cite the current name |
| `disabled test` | the cited test's suite or name starts with `DISABLED_`, so gtest never runs it (a disabled test never satisfies a citation) | enable the test, or cite one that runs |
| `missing parity row` | the cited parity row is not in `tests/parity/scenario_table.h` | cite the row id as the table spells it |
| `not asserted` | the rule's status is `partial`, `executed-only` or `none` | write the tests for the missing arms, then set `asserted` |
| `deferred without issue` | `deferred` needs an `issue` | file the issue and add its number |
| `unreachable without reason` | an `unreachable` row has no `note`, or an empty one | write in `note` why no test can reach the arm, or write the test and set the status the tests earn |

A `deferred: ... issue #N` line is a notice, not an error: the row waits on
that issue and the check stays green. An `unreachable: ... <reason>` line is
a notice too: it prints the row's reason, so that a reader can judge whether
the arm is really dead.
