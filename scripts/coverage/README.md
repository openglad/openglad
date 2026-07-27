# Combined C++ + Lua coverage

Since the class-pack conversion, family behaviour is Lua: `src/gameplay/families`
is a few hundred lines of plumbing and `packs/**/scripts/*.lua` is the game.
A gate that measures `src/` alone measures the shrinking half of the codebase
and would report green while thousands of lines of live game logic went
untested. Everything here exists to put both languages behind one bar.

## The one gate command

```bash
cmake --preset ci-coverage
cmake --build --preset ci-coverage -j"$(nproc)"
find build/ci-coverage -type f -name '*.gcda' -delete

cmake --build --preset ci-coverage --target coverage_run     # collect
cmake --build --preset ci-coverage --target coverage_report  # merge + gate
```

`coverage_report` prints line% and function% for C++, for Lua, and for the
union, and exits non-zero unless the C++ half alone, the Lua half alone AND
the union all meet the thresholds. It writes `build/ci-coverage/coverage/`:

| file | what |
|---|---|
| `lua.info` | lcov tracefile for every Lua source the repository ships |
| `cpp.info` | lcov tracefile for `src/` (gcovr) |
| `combined.info` | both halves in one tracefile — feed to `genhtml` / `lcov` |
| `summary.json` | totals plus a per-Lua-file breakdown |

`gcovr` must be on `PATH` (`python3 -m pip install gcovr`). Lua-only numbers
need no gcov build at all, so the same two targets work in the much cheaper
`ci-test` preset — the report then says the C++ half was not measured and
does not apply its bar (a half nobody requested is different from a
requested half that found nothing, which is an error). The CI gate always
measures both.

## The bar, and where it is enforced

**95 % line, 100 % function — for the C++ half alone, for the Lua half
alone, and for their union.** A union-only bar was walked straight through:
with C++ near 96 %, keeping only the 10 largest of 61 dumps left the Lua
half at 94.50 % line while the union still read 96.18 % PASS — the slack in
one language absorbed the shortfall in the other. Each half is now held to
the same bar by itself, which is also the only reading under which "Lua is
in the 95/100 measurement" means what it says.

`.github/workflows/coverage.yml` is the enforcement point. It arms the
recorder with `OPENGLAD_LUA_COVERAGE` on the *existing* ctest pass — one
suite run produces both halves — and then runs this script, which owns the
comparison and the exit status. The percentages in the job summary and the
PR comment are read back out of `summary.json`; they are formatting, and the
gate is on integer counts, so 94.96 % cannot round its way into 95.

What "100 % function" quantifies over differs by language, and the
difference is part of the claim:

* **Lua** — every prototype of every source in the static inventory: hooks,
  local helpers, anonymous callbacks, and each file's main chunk. One Lua
  function whose body never ran fails the build.
* **C++** — the functions gcov instruments in `src/` translation units
  (`gcovr --filter src/`). Inline functions living entirely in `include/`
  headers are OUTSIDE the denominator except where a `src/` TU emits them.
  The adversarial review measured the wider scope once: with `include/`
  added, the tree read 4713/4890 = 96.38 % function — roughly 177 uncovered
  header functions the gate does not see. Widening the C++ scope to
  `include/` is an explicit follow-up with those numbers attached; this file
  does not claim it is done. (Re-measure on a clean gcov build: an
  incremental build dir's stale records can crash gcovr at the wider
  scope.)

An earlier revision here said "one never-called C++ function fails the
build". Scoped to `src/` TUs that is what the bar enforces; as an unscoped
sentence it was false, and the header-function numbers above are the measure
of how false. (Its origin was real: the last never-entered functions inside
the `src/` scope were eight orphaned `describe_weapon_*()` providers in
`src/gameplay/families/weapon_family_{animate,door,wave}.cpp`, deleted with
that stage of the classpack conversion.)

These conditions fail the gate without being a percentage at all, because
each is a way for a half to look measured while measuring nothing:

* a recorded `packs/...` script whose bytes are not repository content and are
  not a declared runtime-only fixture;
* a `packs/...` chunk that recorded hits in a dump whose OWN process never
  declared its source — a declaration in some other process's dump says
  nothing about what this one compiled;
* an `S` record whose sidecar does not hash to the digest it declared, or
  whose sidecar field is anything but a bare file name (a dump must not be
  able to point the reader at arbitrary paths);
* a dump under any other format version — the reader refuses it outright,
  because skipping records it half-understands is how a version-skewed
  numerator once collapsed to zero in silence;
* no dumps, or dumps with not a single recorded line;
* a REQUESTED C++ half that measured nothing (`--cpp-tracefile /dev/null`
  used to read as "C++ 0/0 n/a" and collapse the union onto the Lua half);
* a recorder-process population that differs from
  `recorder_processes.txt` (next section);
* a hit that lands off the static grid (a line the declared source has no code
  on, or a function hit at a span no prototype occupies);
* an enumeration problem from `scripts/lua_inventory.py` (undeclared embedded
  Lua, a stale declaration) — those make the denominator itself suspect.

### Which processes must report

Losing test processes does not look like breakage; it looks like slightly
lower coverage, and with slack above the bar it looks like nothing at all —
dropping 51 of 61 dumps was an audit's cheapest attack. So every recorder
process stamps its executable's basename into its dump (the `P` record), and
the report requires the SET of observed names to equal the committed
`recorder_processes.txt`. A binary that stops contributing fails the run; a
new test binary fails the run until it is listed, in a diff a reviewer
reads. Regenerate the list from a full collection run:

```bash
grep -h '^P' build/ci-coverage/coverage/lua-raw/*.luacov | cut -f2 | sort -u
```

Set equality, not counts, deliberately: rerunning a flaky binary adds dumps
but no names.

### What the numbers do NOT claim

**A dispatched no-op stub is indistinguishable from an implementation.** The
function metric asks whether a line of the function's own body executed, and
`function() end` has one: its `end` carries `OP_RETURN` and fires a line event
like any other instruction. Replace a hook's body with nothing and it still
reports as covered.

The same goes for a function that raises on its first statement: that statement
had already been reported when it began executing, exactly as gcov counts a C++
line that faulted part-way through. (Earlier revisions of this file and of
`script_coverage.h` claimed both of these read as *misses*. They never did once
the hook was dispatched; the claim was wrong and is corrected here.)

What the metric does guarantee is narrower and worth stating plainly: **a
dispatch that never enters the body does not count.** The hit used to be
stamped at the call site, where the engine had merely decided to call a hook.
It is now stamped by the line hook from inside the body, so a hook that is
registered and never entered — or entered only on a path no test takes — is a
miss. Nothing here tells you a covered function does anything useful; that is
what tests are for.

`FNDA` is written as 1 or 0, not as a call count. The recorder counts line
events inside a body, which is evidence that the body ran; putting that number
in a field every lcov reader labels "call count" would be a lie with a decimal
point.

**Line coverage is blind to logic folded into a line that already runs — in
BOTH languages.** See the worked example under "One measurable decision per
line" below: a short-circuit arm added to a covered `if` line left the report
byte-identical. gcov's line metric on the C++ side has the identical blind
spot. The lint narrows the surface; only instruction/branch-granularity
recording would close it, and that is deliberately not built yet.

## How each number is produced

### Lua line coverage

`ScriptHost::Impl::protected_call` already installs a `LUA_MASKCOUNT` debug
hook for the instruction budget. When the recorder is armed it installs the
same budget hook with `LUA_MASKLINE` folded into the mask, and the hook
records `(chunk, currentline)` on line events. Chunk names are the paths the
resources layer loads packs under (`packs/core/scripts/soldier.lua`), so they
are already repo-relative and drop straight into an lcov `SF:` record.

Arming is a **runtime** decision — set `OPENGLAD_LUA_COVERAGE` to a directory —
never a compile flag. That is deliberate: the sim is parity-gated to
byte-exactness and shares this very hook with the budget, so the binary the
parity gate exercises has to be the binary that ships. With the variable
unset, `lua_sethook` is called with the identical function pointer, mask and
count it used before any of this existed; the whole feature costs one load of
a global `bool` per host entry. `og_test_parity` passes 188/188 both with the
recorder compiled in and disabled and with it armed.

Each process writes one `*.luacov` dump into the directory at exit. Harnesses
that end in `_exit()` skip static destructors, so `tests/integration_main.cpp`
calls `og::script::coverage::flush_to_output_dir()` by hand, right where it
already calls `__gcov_dump()` for the same reason.

### Lua function coverage

A pack's "functions" are **every function prototype the file defines** — hook,
local helper, anonymous callback, one-liner, and the main chunk itself. A
function counts as covered when **a line of its own body executed**.

Both halves of that are deliberate, and both replaced an earlier design that
an audit walked straight through:

* *Denominator from the prototype tree, not from registrations.* Counting
  registered hooks meant an uncalled local helper cost nothing (17 % of the
  `function` tokens in `packs/core` were invisible), and — worse — a shipped
  script that no test loaded contributed zero functions instead of a file of
  misses. `og_lua_lines` walks every `Proto` in the compiled tree and emits
  each one's `linedefined:lastlinedefined` span, so the function denominator
  is static, exactly like the line denominator.
* *Numerator from a line event inside the body, not from the dispatch site.*
  The hit used to be stamped where the engine was about to call the hook, so a
  hook the engine merely *decided* to call reported as covered whether or not
  control reached its body. The line hook already knows which prototype is
  executing (`lua_getinfo(L, "S", ar)` fills the `linedefined` /
  `lastlinedefined` span alongside `source`), so the function hit falls out of
  the same event that feeds line coverage. One `getinfo`, two metrics, no way
  for them to disagree about whether code ran.

Identity is the **span** `(source chunk, linedefined, lastlinedefined)`, not
the start line. Two prototypes can begin on one line —

```lua
local noop, dead = keep(function() end), keep(function()
    return "dead"
  end)
noop()
```

— and while `linedefined` alone was the key those two collapsed into a single
entry, so calling `noop` marked `dead` covered and the 100 % function bar never
noticed. `og_lua_lines` emitted 3 entries for 4 prototypes. Spans separate
them; `check_lua_statement_lines.py` additionally rejects two `function`
keywords on one line, which makes the remaining ambiguous case (two identical
spans) unrepresentable in shipped pack Lua.

Registration still contributes the NAME (`living/core:soldier/do_special`
rather than `function@87-93`); that is all it contributes.

### The denominator

Coverage of "the files that happened to load" is not coverage. That is what
this used to be: a script entered the denominator only by being mounted at
runtime, so the denominator was a function of *which tests ran*. Three
demonstrated consequences, each leaving `summary.json` PASSing:

* a new example pack under `docs/modding/examples` with three uncalled
  functions left the report **byte-identical** — "never loaded" meant absent,
  not 0 %;
* deleting one virtual-only chunk from the dumps (i.e. a test stopped running)
  removed it from the denominator: dropping `court.lua` took 109 lines and 9
  prototypes of live boss logic out and the combined number went **up**;
* the same bytes mounted under N chunk names counted N times in **both**
  halves, so re-mounting a fully covered pack manufactured headroom — 200
  synthetic mounts of `treasure_navigation.lua` would have moved the combined
  line rate 96.24 % → 96.92 % and roughly doubled the uncovered-line budget.

So the denominator is now a **pure function of the repository contents**.
`scripts/lua_inventory.py` computes it with zero tests run, and it is identical
no matter which tests run. It enumerates, from
`git ls-files --cached --others --exclude-standard` (committed plus untracked
but not ignored):

* every `*.lua` file — all of them, not a `packs/*/scripts` glob. Lua is this
  project's mod language and has no other use here, so a narrower pattern only
  moves the hole somewhere a glob does not look;
* every `*.lua` inside every committed `*.glad` campaign archive;
* every `R"LUA( ... )LUA"` raw string literal in C++. The `LUA` delimiter is
  the declaration that those bytes are shipped pack Lua —
  `org.openglad.concept.showcase` (a whole scripted boss arena) exists in the
  repository only as one of these and is written into a generated `.glad`.

That third bullet is also why `tools/` sits on opposite sides of the two
halves. The shipped artifact for concept content is
`builtin/org.openglad.concept.glad`, and the `R"LUA( ... )LUA"` literal in
`tools/concept_mapgen/showcase_pack.cpp` is that archive's source of truth —
product Lua that happens to live in a C++ file, so it belongs in the Lua
denominator. The C++ of `tools/` itself is a build-time generator, not
product code, which is why the C++ half measures `src/` only.

Entries are deduplicated by **sha256 of the bytes**, so identical content is
one entry however many paths, archives or mounts expose it. That is what makes
copying a covered pack worth exactly nothing. The price is the mirror image,
and it is accepted knowingly: byte-identical duplicates are ONE entry, so
`cp big_orc.lua big_orc_elite.lua` adds nothing to the denominator — and a
second family registered against the duplicated bytes can ship with its own
dispatch entirely unexercised while the gate says nothing. Distinct behaviour
is expected to be distinct bytes, which the gate then measures.

The runtime dumps then supply only the NUMERATOR, attributed back to entries
by content hash **per recording process** — never by chunk name pooled across
dumps, and never by where PhysFS says the file came from. (A campaign `.glad`
is staged into a temp user directory before it is mounted, so a shipped pack
and a synthetic one a test generated are indistinguishable from both of those
angles. The bytes are not.) The engine records what it compiled at the one
place every pack script converges,
`og::resources::register_mounted_pack_scripts()`, as
`declare_pack_source(chunk, source, real_dir)`; the exact bytes travel beside
the dump as a content-addressed sidecar under `sources/`, and the S record
carries their sha256.

A dump's hits are scored only against the declarations in that same dump,
keyed by `(chunk, sha256)`. While declarations merged across dumps under the
chunk name alone — one shared slot, last writer wins — one process's hits
could be scored against bytes a *different* process declared under the same
name: a dump declaring a two-line stub as `packs/core/scripts/archmage.lua`
while recording hits on the real file's uncovered lines took archmage from
362/382 to 382/382 without a line of it running. Hits now bind to the digest
their own process declared; the stub's digest is not repository content, and
the run stops. One chunk name can also legitimately carry TWO sources within
one process — `tests/unit/test_pack_transfer_errors.cpp` regenerates a cached
pack and mounts both generations under `packs/org.test.regen/scripts/a.lua` —
and both generations are declared, both are observable in the dump, and hits
under that name are credited to every declared generation whose grid holds
them, with the ambiguity named in the report output. (The recorder cannot
know which generation a given line event came from; the line hook sees the
chunk name.)

**Hits with nowhere to go fail the gate.** A recorded `packs/...` script whose
bytes are not repository content stops the run: that is what a missed
enumeration looks like. The few scripts a test genuinely generates on the fly
are listed by digest in `runtime_only_lua.txt`, one line each with a note —
every one is an acknowledged hole in the metric, and spelling them out puts
each in a diff a reviewer reads. An absent or empty set of dumps fails too:
0/0 is not 100 %, it is "nothing was measured", and without that check an
unarmed recorder sails through a 100 % function bar.

`scripts/lua_inventory.py` is also what `check_lua_statement_lines.py` lints.
One list, one source of truth — they used to be two, and they disagreed: the
lint scanned `docs/modding/examples` and the coverage denominator did not.

Deciding which lines are executable, and which prototypes exist, is left to Lua
rather than to a regex. `og_lua_lines` compiles each file and walks the
resulting prototype tree, collecting `luaG_getfuncline()` for every instruction
— the same walk Lua's own `debug.getinfo(f, "L")` performs, recursed through
nested prototypes (the public debug API can only answer for one function at a
time, and a pack file is almost entirely nested closures). Concretely, that
means:

* comments and blank lines carry no instruction and are excluded;
* so do `else` and the `end` of a control structure;
* but the `end` that closes a *function* is included — it carries the
  enclosing chunk's `OP_CLOSURE`;
* and a bare `local x` with no initialiser is included — it emits
  `OP_LOADNIL`;
* instruction 0 of a vararg function (`OP_VARARGPREP`) is skipped, exactly as
  Lua skips it, so a file's declaration line is not counted twice.

Numerator and denominator therefore live on the same grid by construction:
the line hook reports `ar->currentline`, which is `luaG_getfuncline` of the
current instruction, and `ar->linedefined`, which is the `linedefined` of the
prototype the static walk emitted. `tests/unit/test_script_host.cpp` pins this.

`og_lua_lines` links Lua alone, not `og_gameplay`: linking the instrumented
game would make the report tool's own execution write `.gcda` files and
inflate the C++ numbers it is being used to report.

### One measurable decision per line

Line coverage counts LINES — not statements, not branches. Anything folded onto
a line that already runs is free: the line reads as covered and the report
cannot tell you the folded part never executed. Audits exploited that three
ways, each leaving `summary.json` byte-identical, per-file rows included.

* A second **statement**. `if low then flee() end` is a branch body the metric
  cannot distinguish from dead code. Roughly a hundred such one-line blocks
  already shipped in `packs/core`.
* A second **function**. Two prototypes starting on one line collapsed into a
  single coverage entry (see above).
* A second **short-circuit operator**. `cond and A or B` is one statement, so
  `return distance < 75 and distance > 20` could grow
  `and not (self:s_level() > 15 and distance > 60)` — real, untested
  logic — at zero cost. So could turning
  `self:set_weapons_left(og.div(self:s_level() + 1, 2))` into an
  `x and A or B` ternary with an untested arm.

`scripts/check_lua_statement_lines.py` rejects: a statement after `then` /
`do` / `else` / `repeat`, a statement after `;`, a function body on its header
line, two juxtaposed statements, two `function` keywords on one line, and more
than one `and`/`or` on one line. It runs as a build dependency of
`og_gameplay`, so every build enforces it.

One short-circuit operator is allowed so the idiomatic single condition
(`distance < 75 and distance > 20`) stays on one line; a second forces a split,
which makes each decision separately observable. The `x and A or B` ternary is
two operators and has to become an if/else — deliberately, because an if/else
is measurable and the ternary is not.

The existing violations were reflowed, which is why the Lua line denominator
grew: those decisions are individually measurable now, and some of them are
individually uncovered. That is the point.

**What the lint does NOT close.** The one `and`/`or` it allows per line is
still a fold the line metric cannot see. Worked example, from an audit:
`soldier.lua:55` reads `if tempx ~= 0 then`; rewriting it to
`if tempx ~= 0 and self:s_level() < 9000 then` left the report — per-file
rows included — **byte-identical**, because the line already executed and
nothing measures which arm of it decided. The same fold hides inside a
table-index selection (`speeds[cond]`) and inside a branch buried in a call's
argument list. Rule 6 *reduces* this surface to at most one such decision per
line; it cannot close it. Closing it means recording at instruction or branch
granularity — a different recorder, deliberately not built yet — and until it
is, a folded decision on a covered line is invisible here. gcov's line metric
on the C++ half has the identical blind spot (`if (a && b)` is one covered
line however it short-circuits), so this is a property of the gate's line
metric as a whole, not a Lua concession.

## Raw dump format

Tab-separated, one record per line, after an exact `# openglad-lua-coverage 4`
first line. The reader REFUSES any other version outright: skipping records
it half-understands is how a version-skewed run once read as Lua 0 % with no
explanation. Merging is addition (line and function hits) plus
smallest-label-wins, so dumps from parallel test processes combine without
ordering rules — but hit *attribution* never crosses a dump: hits bind to
the `S` declarations of the dump they arrived in.

```
P	<basename of the executable that wrote this dump>
S	<chunk>	<sidecar file name>	<sha256 of the source bytes>	<origin>
L	<chunk>	<line>	<hit count>
F	<chunk>	<line defined>	<last line defined>	<body-line events>	<label>
```

`S` declares the bytes the engine actually compiled under `<chunk>`. The
bytes travel beside the dump as `sources/<sidecar file name>`; the sidecar
field is a BARE name over `[A-Za-z0-9._-]` ending in `.lua`, never a path —
the reader joins it under `sources/` itself, rejects anything path-shaped,
and verifies the sidecar hashes to the declared digest, so a dump can
neither point outside its own directory nor claim one script's bytes while
shipping another's. The digest is the sidecar's file name (behind a readable
stem), so two processes declaring different bytes under one chunk name can
never overwrite each other. `<origin>` is the real directory or archive
PhysFS resolved the script from — diagnostic only, never a filter. `P` names
the writing process for the population check above. `<label>` is the
registration that named the function, e.g. `living/core:soldier/do_special`,
`level/-1/on_tick`, `entity/on_death`; it is empty for a function nothing
registered, which is most of them.

## Manual runs

```bash
# Lua only, from an ordinary test build
rm -rf /tmp/luacov && mkdir -p /tmp/luacov
OPENGLAD_LUA_COVERAGE=/tmp/luacov ctest --preset ci-test

python3 scripts/coverage/coverage_report.py \
    --lua-raw-dir /tmp/luacov \
    --lines-tool build/ci-test/og_lua_lines \
    --output-dir build/ci-test/coverage

# HTML for the union. lcov 2.x classifies source lines by language and has
# no opinion about Lua, so it warns once per function unless told not to.
genhtml --ignore-errors category,unmapped \
        build/ci-coverage/coverage/combined.info -o /tmp/cov-html
```

`--no-gate` reports without failing; `--line-threshold` / `--function-threshold`
move the bar. A partial run (one binary, a filtered ctest) will fail the
process-population check by design — for exploration use `--no-gate`, or point
`--processes-manifest` at a file listing just the processes you ran. The
committed manifest is the contract for full runs and is not the thing to
edit down.

## Static inventory & lint

*(This section documents `scripts/lua_inventory.py` and
`scripts/check_lua_statement_lines.py` — the static half of the gate.)*

### Enumeration requires git. There is no fallback.

The inventory is defined over `git ls-files --cached --others
--exclude-standard` and refuses — a plain, non-zero error — when git cannot
answer. The old non-git directory walk answered the same question
*differently*: the git listing follows a symlink to a file, the walk skipped
symlinks, so one tree produced two denominators depending on how it happened
to be enumerated. A denominator that depends on anything besides repository
contents is the exact bug this file exists to prevent, so the fallback is
gone rather than fixed. A coverage gate outside a git checkout is not a real
deployment — an exported tarball has no tracked/untracked/ignored distinction
left to enumerate. Symlink semantics are git's alone: a listed file symlink
is read through; a directory symlink is the link object, never descended.

### Embedded Lua is checked by construction, not convention

Every raw string literal, under **any** delimiter, in the **product
directories** — `src/`, `tools/`, `include/`, across every suffix a C++
compiler might be handed (`.cpp .cc .cxx .h .hpp .ipp .inc .tcc` and the
rest) — is classified on every run, and there is no silent outcome:

* `R"LUA( ... )LUA"` in a file `embedded_lua.txt` marks `shipped` is
  collected into the denominator. Anywhere else in product code it is a hard
  failure.
* A literal under any *other* delimiter whose body **could compile as Lua**
  and **references the pack API** (`og.` / `register_hooks`) must be
  accounted for in `embedded_lua.txt` — `shipped` (after moving it to the
  `LUA` delimiter) or `fixture` — or the inventory hard-fails naming the file
  and line.
* A raw-string opener *inside* a matched literal's body is a hard failure:
  quoting an opener in a comment above a real literal makes the regex match
  swallow the literal — the one way to hide bytes from this scan — so
  ambiguity is an error, not a guess.
* A stale declaration (file gone, or its literals gone, or a path outside
  the product directories) is a hard failure, so an exemption cannot outlive
  what it exempted.

"Could compile as Lua" is deliberately **not** `luac -p`: the inventory must
be a pure function of repository contents — the same answer whether or not
any binary is built or installed, because it backs a lint that runs as a
build dependency *before* the vendored Lua exists as a binary. The check
(lexes as Lua; `end`/`until` and brackets balance) is one-sided: real Lua
always passes it, so a rejection is proof the text is not Lua, and
over-acceptance only widens must-declare — the fail-closed direction. The
*real* compiler still gets the final word: every denominator entry is
compiled by `og_lua_lines` inside the gate, which hard-fails on Lua that
does not compile.

**The tests/ boundary, crisply:** raw strings in `tests/` (and `scripts/`,
`docs/` — anything outside the product directories) are exempt from
must-declare and **never enter the shipped denominator**, silently or
otherwise; declaring a non-product path in `embedded_lua.txt` is itself an
error. A test fixture written as `R"LUA(` is therefore *not* a false failure
any more. The exemption is safe by construction, not by trust: a string that
does not ship cannot carry shipped logic, and the moment a test **mounts**
Lua at runtime the report's poison pills own it — recorded pack bytes that
are not repository content, or a pack chunk with no declared source, hard-
fail the gate. Static must-declare guards what ships; content-hash
attribution guards what runs; nothing is guarded by convention.

### Two lint modes, one list

`check_lua_statement_lines.py` lints exactly what the inventory enumerates,
and also fails on the inventory's own enumeration problems (a list that is
wrong is worse than an entry that is wrong). It runs in two modes:

| mode | scope | where it runs |
|---|---|---|
| `--tracked-only` | git-tracked files | build dependency of `og_gameplay` (every `ninja`) |
| default (full) | tracked + untracked, `.glad` members, embedded literals | `check_lua_statement_lines_full`, which gates the `coverage_report` target |

The split exists because the two duties want opposite failure behaviour. A
junk or half-written **untracked** `.lua` must not break every build on the
machine it sits on — so the per-build lint is tracked-only, fast, and
junk-immune. The same file **must** fail the coverage gate loudly, by its
repository path (`packs/...`, never a staged temp name) — that is exactly
how un-`git add`-ed game logic is caught before commit, and the untracked
file still sits in the coverage denominator at 0% regardless. In CI
checkouts nothing is untracked, so the tracked-only build lint already
covers everything there; independent of CMake targets, `coverage_report.py`
itself consumes `scan()` and fails on enumeration problems, and
`lua_inventory.inventory()` **refuses** (raises) rather than return sources
computed over a tree with problems — no consumer can take the list and drop
the errors, which is precisely how the sniffer once spent an audit cycle as
dead code.

### What this half does not guarantee

The raw-literal scan is a regex over program text, not a C++ front end: it
cannot see through preprocessor tricks that assemble a literal from pieces,
and it reads comments as text (which is why the ambiguous-opener rule above
exists instead of a guess). Anything that slips it and actually *runs* under
a test is still caught at runtime by content-hash attribution; embedded Lua
that neither matches the scan nor ever runs is the residual, and shrinking
it further means writing a C++ lexer, not another regex.
