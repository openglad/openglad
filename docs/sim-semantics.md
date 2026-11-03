# Simulation semantics

Every branch point in the simulation is either a game rule or plumbing, and
the code says which, in a comment right above the branch. A rule says what
the branch decides and names the tests that assert it; plumbing says in one
line why there is nothing to assert. The test names and bodies are the
detailed spec: the comment says what is decided, the tests say how each
outcome looks.

A check runs on every CI build, so "every rule in the game has a
deterministic test that asserts it" is a fact the check proves, not a claim.
It stays true when the simulation changes: a new `if` in `src/gameplay` is
red until someone writes what it decides and which test sees it.

The rules were first written by a review of the whole simulation: every
candidate branch line of the C++ in `src/gameplay` and its public headers
(networking, snapshot serialization, mapgen and the lobby are out of scope)
and of every class-pack Lua file under `packs/`.

## The rule

1. Every branch line of an in-scope file is covered by exactly one
   semantics comment.
2. A `rule:` comment names at least one test, and every test it names exists
   and is not disabled. Instead of a test it may name an issue
   (`deferred: #N`), for instance when the current game differs from the
   old game and the difference is filed rather than pinned, or say why no
   test can reach the branch (`unreachable:`).
3. A `plumbing:` comment (a null guard, a container walk, determinism
   plumbing, a test-only path) says why and names no test.

## The comment

C++ uses `//`, Lua uses `--`; the words are the same.

```cpp
    // rule: A weapon still playing a non-walk animation finishes it before
    //   doing anything else that tick.
    // tests: WeapBehavior.weap_act_sit_with_non_skipping_family_and_act_animate_shortcut
    if (ani_type() != ANI_WALK)
        return animate();
```

```lua
  -- rule: Gameplay Fixes: a druid bot never casts REVEAL (it only changes a
  --   viewscreen's radar).
  -- tests: GameplayFixesBots.druid_reveal_is_never_cast_by_a_bot
  if slot == 3 then
```

The lines of a block:

| line | says |
|---|---|
| `rule: <text>` | what the branch decides; it starts the block |
| `  <text>` | continues the line above it: two or more spaces after the comment marker |
| `tests: A.b, C.d, parity:<row>` | the tests that assert the rule, comma-separated; a GoogleTest as `Suite.name`, a parity row as `parity:<row id>` from `tests/parity/scenario_table.h`; as many `tests:` lines as needed |
| `deferred: #<issue> <why>` | the behaviour is not pinned and the issue tracks it |
| `unreachable: <why>` | no test can reach the branch, and why |
| `plumbing: <why>` | no rule here; the block is this line (and its continuations) alone |

A rule block is `rule:`, then its `tests:` lines, then at most one
`deferred:` or `unreachable:`. It sits on the lines immediately above the
statement, at the statement's indentation, with no blank line or other
comment between. A rule that spans several statements written one after
another needs one block: it covers its whole paragraph (below). When the
statements of one rule are apart (a blank line between them, or one inside
the body of another), each paragraph has its own block, and each says the
rule.

### What a block covers

A block covers the paragraph right below it: the statement under it and
every following statement that starts at the same indentation, up to the
first of

- a blank line;
- a line indented less than the statement (the `}` or `end` that closes
  the enclosing block counts);
- the next block at that indentation or less;
- an arm of a statement that started before the block (the next `case` of
  the switch the paragraph sits in).

Plain comments and deeper lines (bodies, continuation lines) do not end it.

```cpp
    // rule: A frozen or dead walker does not act; a busy one waits.
    // tests: Walker.a_frozen_walker_skips_its_turn, Walker.a_busy_walker_waits
    if (frozen())
        return;
    if (busy() > 0)       // the same rule: same paragraph
        return;

    // rule: ...          // a blank line ended it: a new block
    if (dead())
```

Of every statement in the paragraph, the block covers:

- its first line and every continuation line (a condition, an argument
  list or an expression wrapped over several lines, so a ternary or a Lua
  `and` / `or` on a continuation line belongs to the block); in C++, the
  body of an `if`, `else`, `for`, `while` or `do` when it is one plain
  statement without braces;
- for an `if`: every `else if` and `else` of the chain at the same
  indentation, including a `} else if` line and a `}` line followed by
  `else`, also past a blank line inside a body; in Lua every `elseif` and
  `else` before the chain's `end`;
- for a `switch`: every `case` and `default` label of that switch (not of a
  switch nested inside it);
- for a `do`: its closing `} while (...)`; for a Lua `repeat`: its `until`;
- for a `while` or `for` loop: its head only;
- for a `#define`: every line its backslashes continue.

It never covers a body: a branch inside a body has its own block, and that
block's paragraph is its own. A nested block takes over the statements
below it inside the body; the outer block's paragraph goes on after the
body ends.

A `case` or `default` label that is a rule of its own has its block above
the label, at the label's indentation; that block covers the label alone,
and the switch's block then no longer covers it. An `else`, `else if` or
`elseif` arm that is a rule of its own cannot have a block above it (that
would sit inside the previous arm), so its block starts as a trailing
comment on the arm's first or last line, and its further lines follow right
after that line, indented deeper than the arm:

```cpp
    } else if (a < 0) {  // rule: A negative count halves the step.
        // tests: Walker.a_negative_count_halves_the_step
        step /= 2;
```

When the arm's line already has a comment, the block follows it:
`} else {  // an old note // rule: ...`. A trailing block anywhere but on
the first or last line of an `else` arm is an error.

A rule that does not sit on a branch line (a gate function whose whole body
is one comparison, a family-table field that names a gate) still gets a
`rule:` block with its `tests:`: a rule block that names tests may sit above
any statement, and the check reads its tests like any other. A `plumbing:`
block, or a rule naming no test, above a paragraph with no branch line is
stray.

## What counts as a branch line

A C++ line is a candidate when it starts with `if`, `else if`, `else`,
`switch`, `case`, `default`, `while`, `for` or `do` (optionally after a
closing `}`), or holds a `?` (a ternary; any `?` left once strings and
comments are blanked, so a ternary wrapped with its `:` on a later line is a
candidate on its `?` line). A Lua line is a candidate when
it starts with `if`, `elseif`, `else`, `while`, `for`, `repeat` or
`until`, or holds the word `and` or `or`.

This is the review's grep with two changes. First, it runs on the code
after every comment and every string and character literal is blanked out.
An `and` inside a Lua string, a `?:` inside a format string and an `if`
inside a comment are no longer branch lines. That retired 42 of the 48 lines
the review had marked as dead hits of its grep (the six left are real
branches that decide nothing, such as a log-only `if` or a `switch` default
no roll reaches, and their `plumbing:` line says so), and four other
plumbing lines. Second, the review's `? ... :` pattern needed both halves of
a ternary on one line; any `?` now counts, which added 38 lines (a wrapped
ternary such as `return valid_floor(floor)` / `? extra_floors_[...]` /
`: grid;`).
The grep is otherwise as blunt as before: a `for` that only walks a list is a
candidate and needs its `plumbing:` line, so the check never has to guess
what a line means.

## Adding a branch

When you add an `if`, a `case`, a ternary or a Lua `and` / `or` to an
in-scope file, the check reports it:

```
uncovered: src/gameplay/walker.cpp:812: if (stats->frozen_delay > 0)
```

- If the branch decides something a player could see, write the test
  first, then a `rule:` block above the branch naming it.
- If it is plumbing, write a `plumbing:` line saying why.
- If it is a new arm of an existing rule (an `else if` of the same
  decision), it is already covered by the chain's block: extend the rule's
  text if it no longer says everything, and add the test for the new arm to
  its `tests:`.

Moving code moves its comments with it; there is nothing to re-point.

## Running the check

```bash
python3 scripts/semantics/check_semantics.py            # check the tree
python3 scripts/semantics/check_semantics.py --report /tmp/semantics.txt
python3 scripts/semantics/check_semantics.py --self-test
ctest -R semantics_                                     # both, from a build directory
```

The in-scope file list and the branch grep live at the top of the check
(`SCOPE_CPP`, `SCOPE_LUA_GLOB`, `CPP_BRANCH`, `LUA_BRANCH`), the only place
they are spelled. The tests the review added are
`tests/unit/test_sem_<area>_<n>.cpp`, one unit binary each
(`og_unit_sem_<area>_<n>`), registered in `cmake/semantics/<area>.cmake`.

Exit 0 means every rule holds; 1 means the output lists violations; 2 means
the check could not run (an in-scope file or the parity table is missing).
`deferred:` and `unreachable:` lines are printed as notices, not errors, so
a reader can judge them. The output ends with the counts:

```
check_semantics: OK: 159 files, 4635 candidate lines, 3150 rule comments, 677 plumbing comments | uncovered 0, double-covered 0, stray 0, no tests 0, missing test 0, disabled test 0, missing parity row 0, malformed 0 | deferred 17, unreachable 24
```

### What each error means

| error | meaning | what to do |
|---|---|---|
| `uncovered` | a branch line no block covers | write its `rule:` or `plumbing:` block |
| `double-covered` | two blocks cover one line (two trailing blocks on one arm) | keep one |
| `stray` | a block that covers no branch line: a `plumbing:` block or a rule naming no test above a paragraph with no branch, cut off from its statement by a blank line or another comment, inside a statement's continuation lines, above an `else` arm, or trailing anywhere but the first or last line of an `else` arm | move it to the branch it describes, or delete it if the branch is gone |
| `no tests` | a `rule:` with no `tests:`, `deferred:` or `unreachable:` | name the test that asserts it, or write one |
| `missing test` | no `TEST`, `TEST_F` or `TEST_P` under `tests/` defines that `Suite.name` outside comments and `#if 0` (a test a file-local macro stamps out counts) | the test was renamed or removed: name the current one |
| `disabled test` | the test's suite or name starts with `DISABLED_`, so it never runs | enable it, or name one that runs |
| `missing parity row` | the `parity:` row is not in `tests/parity/scenario_table.h` | spell the row id as the table does |
| `malformed` | the block breaks the grammar: an unknown keyword (`test:`), keywords out of order, a plain comment inside the block, a `deferred:` without `#<issue>`, an `unreachable:` or `plumbing:` without a reason, a `plumbing:` block with tests, a line not at the statement's indentation, a `tests:` line outside a block, a test name that is neither `Suite.name` nor `parity:<row>` | fix the block |
