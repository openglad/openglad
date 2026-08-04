#!/usr/bin/env python3
"""One measurable decision per line in pack Lua.

WHY THIS EXISTS (the hole it plugs)
-----------------------------------
Line coverage counts LINES, not statements and not branches. Anything folded
onto a line that already runs becomes free: the line reads as covered, and the
report cannot tell you the folded part never executed. That is not a reporting
nicety, it is a way to add untested game logic at zero coverage cost, and
audits of this repo did it three separate ways, each time leaving summary.json
byte-identical.

  * A second STATEMENT. `if low then flee() end` on one line is a branch body
    the metric cannot distinguish from dead code. An audit folded a wholly
    untested branch onto an already-covered line of soldier.lua's on_create;
    the report did not move.
  * A second FUNCTION. Two prototypes beginning on the same line used to
    collapse into one coverage entry, so
    `local noop, dead = keep(function() end), keep(function() ... end)`
    got `dead` covered for free by calling `noop`. (The recorder now keys on
    the whole line SPAN, which separates that case; this rule makes even an
    identical span unrepresentable.)
  * A second short-circuit OPERATOR. `cond and A or B` is one statement, so
    `return d < 75 and d > 20` could grow `and not (lvl > 15 and d > 60)` —
    a real, untested condition — without adding a coverage point.

So the fix is not a bigger number, it is a grid fine enough to hold the
question. The line denominator grows by exactly the decisions that were
hiding, and some of them turn out to be uncovered. That is the point.

WHAT IS REJECTED
----------------
  1. a statement after `then`, `do`, `else` or `repeat` on the same line
     (`if x then return end`, `while c do i = i + 1 end`)
  2. a statement after `;` on the same line
  3. a function body on the header line (`local function f() return 1 end`)
  4. two juxtaposed statements (`x = 1 y = 2`, `f() g()`)
  5. two `function` keywords on one line
  6. more than one short-circuit operator (`and` / `or`, counted together) on
     one line

An EMPTY block is fine — `function() end`, `if x then end` hide nothing.

Rule 6 leaves the idiomatic single condition legal: `d < 75 and d > 20` is one
operator and stays on one line. A second forces a split, so each decision gets
its own coverage point. The `x and A or B` ternary is two operators and must
become an if/else — deliberately, because an if/else is measurable and the
ternary is not.

STYLE-CONTRACT RULES (docs/lua-style.md)
---------------------------------------
Rules 1-6 protect the coverage grid. Rules 7-9 enforce the pack-Lua style
contract:

  7. the raw rand guard trio — `if n > 0 then r = og.rand(n) end` in any
     spelling (`0 < n`, `local r = ...`). `og.rand0(n)` IS that guard:
     IRandom::next(0) semantics, `n <= 0` answers 0 without advancing the
     stream (style contract S5).
  8. bound-helper reimplementation and cross-file duplication (S4/S5):
     a) a `local function NAME` whose NAME is a bound helper — a function
        field of `og` or `og.combat` in the generated stubs
        (docs/modding/og-api.d.lua — which this rule READS, so stub drift
        starves it; the api_stub_check gate keeps it fed), or one of the
        fused walker verbs. Never hand-inline an engine helper; call the
        binding.
     b) the same `local function NAME` body (token-identical, >= 4 lines)
        in two or more shipped sources — a shared body belongs in
        `packs/<id>/lib/` behind `og.use`. The 3-line hook-key identity
        wrappers (`local function level_up(self) / return lc.level_up(self)
        / end`) stay legal by design: S1 keeps registration tables reading
        as identity pairs.
  9. headers and obsolete source paths (S2/S3): a shipped pack file (under
     packs/ or the example packs in docs/modding/) opens with the one-line S2
     header ending in the cookbook pointer; multi-line boilerplate and
     obsolete `-- transliterated from <file>.cpp` source-path comments are
     findings anywhere they appear. Signed authorship and historical comments
     are permitted. Embedded R"LUA" chunks and .glad archive members have no
     file header and are exempt from the line-1 shape.

WHAT IS SCANNED
---------------
Whatever scripts/lua_inventory.py calls shipped Lua — the same list the
coverage denominator is built from, so a file cannot be game logic to one tool
and invisible to the other. That list has a deliberate boundary: .lua files under the shipped roots
(packs/, docs/, campaigns/) at any depth, .lua members of .glad campaign
archives, and
declared product-C++ literals. A .lua under tests/ or scripts/ is a fixture,
not shipped logic — never in the denominator, never linted here (spot-check
it by passing the path explicitly if you want the rules applied anyway).
Enumeration problems from that module (an undeclared blob of embedded Lua, a
stale declaration, an UNREADABLE shipped file or corrupt archive) fail this
lint too: they are the list being wrong, which is strictly worse than an
entry on the list being wrong.

TWO MODES, ONE LIST
-------------------
  --tracked-only   git-tracked files only. This is the PER-BUILD mode — the
                   check runs as a build dependency of og_gameplay, and a
                   scratch or junk untracked .lua must not break every ninja
                   build on the machine it sits on.
  (default)        the FULL inventory: tracked + untracked, .glad archive
                   members, embedded literals. This is the coverage-gate mode
                   (the check_lua_statement_lines_full target, which gates
                   coverage_report), where an untracked shipped-Lua candidate
                   MUST fail loudly, by its repository path — that is exactly
                   how un-`git add`-ed game logic gets caught before commit.

In CI checkouts nothing is untracked, so the per-build mode already covers
everything there; the split only changes what can break a developer build.

Run: python3 scripts/check_lua_statement_lines.py [--tracked-only]
     [--git-exe PATH] [paths...]
Exit 0 when clean, 1 with a listing otherwise. Explicit paths are for
spot-checking files while editing them and skip the inventory entirely.
--git-exe (or $OG_GIT_EXECUTABLE, which the CMake targets set) names the
git the inventory enumerates with, for hosts where the build-time PATH
cannot resolve the bare name — see lua_inventory.git_executable().
"""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import sys
from collections import Counter, defaultdict
from typing import Dict, Iterable, List, NamedTuple, Optional, Tuple

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lua_inventory  # noqa: E402  (path set up immediately above)
from lua_inventory import LuaLexError, Token, tokenize  # noqa: E402,F401

REPO = lua_inventory.REPO

# ---- rule 7-9 machinery (style contract) ---------------------------------

# Rule 8b: identical bodies at or above this many lines are duplication that
# belongs in packs/<id>/lib/. Chosen one above the 3-line hook-key identity
# wrappers (header / `return lc.hook(self)` / `end`) that S1 keeps legal.
DUP_MIN_SPAN = 4

# Rule 8a: fused walker verbs are methods, not og.* fields, so the stub
# parse cannot name them; the pair is small and changes with the bindings.
FUSED_VERBS = {
    "heal_clamped": "walker:heal_clamped",
    "add_frozen_stun": "walker:add_frozen_stun",
}

# Rule 8a reads the GENERATED stubs — the same single source of truth the
# editor tooling uses; api_stub_check keeps the file in sync with the C++
# registration tables.
STUB_REL = "docs/modding/og-api.d.lua"

# Rule 9: label prefixes whose file-backed sources must carry the S2 header.
# og-api.d.lua sits under docs/modding/ but not under examples/ — it is a
# generated annotation file, not pack code, and starts with `---@meta`.
S2_PREFIXES = ("packs/", "docs/modding/examples/")
S2_HEADER_RE = re.compile(
    r"^-- \S.*\(cookbook: docs/lua-classpacks-design\.md §3\)\.\s*$"
)
LEGACY_BOILERPLATE = "-- Cookbook (docs/lua-classpacks-design.md §3) applies"
OBSOLETE_SOURCE_PROVENANCE_RE = re.compile(
    r"--.*\btransliterated from \S+\.(?:cpp|h)\b"
)


class LocalFn(NamedTuple):
    label: str
    name: str
    line: int
    span: int
    body_hash: str

# Tokens after which a NAME/literal starts a new statement rather than
# continuing the current expression.
VALUE_ENDERS = {"end", "break", "true", "false", "nil"}
CLOSERS = {")", "]", "}"}
# `do` and `repeat` are deliberately absent: they close a `for`/`while`
# header rather than starting a statement, and what follows them is already
# rule 1's business.
STATEMENT_STARTERS = {
    "local", "return", "if", "while", "for", "break", "goto", "function",
}
# Openers whose block may legitimately be closed on the same line when empty.
BLOCK_OPENERS = {"then", "do", "else", "repeat"}
BLOCK_CLOSERS = {"end", "until"}


def starts_statement(token: Token) -> bool:
    if token.kind in ("name", "number", "string"):
        return True
    return token.kind == "keyword" and token.text in STATEMENT_STARTERS


def ends_value(token: Token) -> bool:
    if token.kind in ("name", "number", "string"):
        return True
    if token.kind == "op":
        return token.text in CLOSERS or token.text == "..."
    return token.kind == "keyword" and token.text in VALUE_ENDERS


def find_header_end(tokens: List[Token], index: int) -> Optional[int]:
    """Index of the `)` closing the parameter list of `function` at `index`."""
    j = index + 1
    # Optional name: NAME ('.' NAME)* (':' NAME)?
    while j < len(tokens) and (
        tokens[j].kind == "name"
        or (tokens[j].kind == "op" and tokens[j].text in (".", ":"))
    ):
        j += 1
    if j >= len(tokens) or tokens[j].text != "(":
        return None
    depth = 0
    while j < len(tokens):
        if tokens[j].text == "(":
            depth += 1
        elif tokens[j].text == ")":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return None


def load_bound_helper_names() -> Tuple[Dict[str, str], List[str]]:
    """{local name: qualified display name} rule 8a flags, or a problem.

    Parsed from the generated stubs: every `---@field NAME fun...` under
    `---@class og` or `---@class og.Combat`, plus the fused walker verbs.
    A missing stub file is an enumeration-grade problem, not a silent
    shrink of the rule to nothing.
    """
    stub = REPO / STUB_REL
    if not stub.is_file():
        return {}, [
            f"{STUB_REL}: missing — rule 8a reads the generated stubs for "
            "the bound-helper names (regenerate: "
            "python3 scripts/modding/gen_api_stubs.py)"
        ]
    names: Dict[str, str] = dict(FUSED_VERBS)
    current: Optional[str] = None
    for line in stub.read_text(encoding="utf-8").splitlines():
        cls = re.match(r"---@class (\S+)", line)
        if cls:
            current = cls.group(1)
            continue
        field = re.match(r"---@field (\w+) fun\b", line)
        if field and current in ("og", "og.Combat"):
            prefix = "og.combat." if current == "og.Combat" else "og."
            names[field.group(1)] = prefix + field.group(1)
    return names, []


def block_end_index(tokens: List[Token], fn_index: int) -> Optional[int]:
    """Index of the `end` closing the `function` keyword at fn_index.

    Full block matching: for/while push expecting their own `do` (recorded
    per stack slot, so a `do` inside a nested function cannot satisfy an
    outer for-header), repeat pops at `until`, everything else at `end`.
    """
    stack: List[bool] = [False]  # per-open-block: still awaiting its `do`?
    j = fn_index + 1
    while j < len(tokens):
        token = tokens[j]
        if token.kind == "keyword":
            text = token.text
            if text in ("function", "if"):
                stack.append(False)
            elif text in ("for", "while"):
                stack.append(True)
            elif text == "do":
                if stack and stack[-1]:
                    stack[-1] = False  # the for/while header's own `do`
                else:
                    stack.append(False)  # standalone do-block
            elif text == "repeat":
                stack.append(False)
            elif text in ("end", "until"):
                stack.pop()
                if not stack:
                    return j
        j += 1
    return None


def local_function_records(tokens: List[Token], label: str) -> List[LocalFn]:
    """Every `local function NAME ... end` in one tokenized source."""
    records: List[LocalFn] = []
    for index, token in enumerate(tokens):
        if not (token.kind == "keyword" and token.text == "function"):
            continue
        if index == 0 or tokens[index - 1].text != "local":
            continue
        if index + 1 >= len(tokens) or tokens[index + 1].kind != "name":
            continue
        close = block_end_index(tokens, index)
        if close is None:
            continue  # unclosed function: the lexer/loader will complain
        body = tokens[index + 2:close]
        digest = hashlib.sha256(
            "\x00".join(f"{t.kind}:{t.text}" for t in body).encode("utf-8")
        ).hexdigest()[:16]
        records.append(
            LocalFn(
                label=label,
                name=tokens[index + 1].text,
                line=token.line,
                span=tokens[close].line - token.line + 1,
                body_hash=digest,
            )
        )
    return records


def guard_trio_violations(tokens: List[Token], label: str) -> List[str]:
    """Rule 7 — the raw rand guard trio in either comparison spelling."""
    found: List[str] = []
    for index, token in enumerate(tokens):
        if not (token.kind == "keyword" and token.text == "if"):
            continue
        cond = tokens[index + 1:index + 5]
        if len(cond) < 4 or cond[3].text != "then":
            continue
        if cond[0].kind == "name" and cond[1].text == ">" and cond[2].text == "0":
            bound = cond[0].text
        elif cond[0].text == "0" and cond[1].text == "<" and cond[2].kind == "name":
            bound = cond[2].text
        else:
            continue
        k = index + 5
        if k < len(tokens) and tokens[k].kind == "keyword" and tokens[k].text == "local":
            k += 1
        tail = tokens[k:k + 8]
        if len(tail) < 8:
            continue
        shape = (
            tail[0].kind == "name"
            and tail[1].text == "="
            and tail[2].text == "og"
            and tail[3].text == "."
            and tail[4].text == "rand"
            and tail[5].text == "("
            and tail[6].kind == "name"
            and tail[6].text == bound
            and tail[7].text == ")"
        )
        if shape:
            found.append(
                f"{label}:{token.line}: raw rand guard trio around "
                f"`og.rand({bound})` — og.rand0({bound}) IS this guard "
                "(IRandom::next(0): n <= 0 answers 0 without advancing the "
                "stream; style contract S5)"
            )
    return found


def header_violations(source: str, label: str) -> List[str]:
    """Rule 9 — require S2 headers; reject boilerplate/deleted-source paths."""
    found: List[str] = []
    is_chunk = ':R"LUA"@' in label or "!" in label
    lines = source.splitlines()
    if not is_chunk and label.startswith(S2_PREFIXES):
        first = lines[0] if lines else ""
        if not S2_HEADER_RE.match(first):
            found.append(
                f"{label}:1: missing the S2 one-line header — `-- <scope> — "
                "<what the hooks do> (cookbook: "
                "docs/lua-classpacks-design.md §3).` (style contract S2)"
            )
    for number, line in enumerate(lines, start=1):
        if LEGACY_BOILERPLATE in line:
            found.append(
                f"{label}:{number}: disallowed multi-line boilerplate header — "
                "use the S2 one-line header (style contract S2)"
            )
        if OBSOLETE_SOURCE_PROVENANCE_RE.search(line):
            found.append(
                f"{label}:{number}: obsolete deleted-source provenance "
                "comment — preserve authorship/history, but name the "
                "reproduced operation sequence instead (style contract S3)"
            )
    return found


def bound_helper_violations(
    records: List[LocalFn], bound: Dict[str, str]
) -> List[str]:
    """Rule 8a — a local function shadowing/reimplementing a bound helper."""
    found: List[str] = []
    for record in records:
        if record.name in bound:
            found.append(
                f"{record.label}:{record.line}: `local function "
                f"{record.name}` reimplements or shadows the bound helper "
                f"`{bound[record.name]}` — call the binding (style "
                "contract S5: never hand-inline an engine helper)"
            )
    return found


def duplicate_body_violations(records: List[LocalFn]) -> List[str]:
    """Rule 8b — token-identical same-name bodies >= DUP_MIN_SPAN lines in
    two or more sources."""
    found: List[str] = []
    grouped: Dict[Tuple[str, str], List[LocalFn]] = defaultdict(list)
    for record in records:
        grouped[(record.name, record.body_hash)].append(record)
    for (name, _digest), group in sorted(grouped.items()):
        labels = {record.label for record in group}
        span = max(record.span for record in group)
        if len(labels) < 2 or span < DUP_MIN_SPAN:
            continue
        sites = ", ".join(
            f"{record.label}:{record.line}"
            for record in sorted(group, key=lambda r: (r.label, r.line))
        )
        found.append(
            f"{group[0].label}:{group[0].line}: `local function {name}` "
            f"({span} lines) is token-identical in {len(labels)} files "
            f"({sites}) — move the body to packs/<id>/lib/ behind og.use "
            "(style contract S4)"
        )
    return found


def violations(source: str, label: str) -> List[str]:
    try:
        tokens = tokenize(source)
    except LuaLexError as exc:
        return [f"{label}: {exc}"]

    found: List[str] = []
    depth = 0
    for index, token in enumerate(tokens):
        nxt = tokens[index + 1] if index + 1 < len(tokens) else None
        if token.kind == "op" and token.text in "([{":
            depth += 1
        elif token.kind == "op" and token.text in ")]}":
            depth -= 1

        if nxt is None or nxt.line != token.line:
            continue

        # 1 — a block opener with its body on the same line.
        if token.kind == "keyword" and token.text in BLOCK_OPENERS:
            if not (nxt.kind == "keyword" and nxt.text in BLOCK_CLOSERS):
                found.append(
                    f"{label}:{token.line}: `{token.text}` opens a block and "
                    f"its body starts on the same line (`{nxt.text}`); the "
                    "branch cannot be measured separately"
                )
            continue

        # 2 — `;` as a separator rather than a terminator.
        if token.kind == "op" and token.text == ";":
            found.append(
                f"{label}:{token.line}: `;` with another statement after it "
                f"(`{nxt.text}`)"
            )
            continue

        # 3 — a function body on its header line.
        if token.kind == "keyword" and token.text == "function":
            close = find_header_end(tokens, index)
            if close is not None and close + 1 < len(tokens):
                body = tokens[close + 1]
                if body.line == tokens[close].line and not (
                    body.kind == "keyword" and body.text == "end"
                ):
                    found.append(
                        f"{label}:{tokens[close].line}: function body starts "
                        f"on the header line (`{body.text}`)"
                    )
            continue

        # 4 — two statements simply run together.
        if depth == 0 and ends_value(token) and starts_statement(nxt):
            # `f"literal"` and `f{...}` are call syntax, not juxtaposition.
            if token.kind == "name" and nxt.kind == "string":
                continue
            found.append(
                f"{label}:{token.line}: two statements on one line "
                f"(`{token.text}` then `{nxt.text}`)"
            )

    # 5 — two prototypes on one line. The recorder keys a function on its
    # (linedefined, lastlinedefined) span, which already separates two
    # functions that merely START together; this closes the remaining case
    # where the spans are identical too (two empty bodies on one line), for
    # which no line-based identity can exist.
    per_line: "Counter[int]" = Counter()
    for token in tokens:
        if token.kind == "keyword" and token.text == "function":
            per_line[token.line] += 1
    for line, count in sorted(per_line.items()):
        if count > 1:
            found.append(
                f"{label}:{line}: {count} `function` keywords on one line; "
                "two prototypes that share a line span are one coverage entry"
            )

    # 6 — more than one short-circuit operator. `and`/`or` are the branches
    # a per-statement rule cannot see: they are one statement, so a second
    # condition folds onto an already-covered line for free.
    per_line = Counter()
    for token in tokens:
        if token.kind == "keyword" and token.text in ("and", "or"):
            per_line[token.line] += 1
    for line, count in sorted(per_line.items()):
        if count > 1:
            found.append(
                f"{label}:{line}: {count} short-circuit operators "
                "(`and`/`or`) on one line; each decision needs its own "
                "coverage point — split the condition, or turn an "
                "`x and A or B` ternary into an if/else"
            )
    return found


def collect_sources(
    paths: Optional[Iterable[pathlib.Path]] = None,
    include_untracked: bool = True,
) -> Tuple[List[Tuple[str, str]], List[str]]:
    """((label, source) pairs, enumeration problems) for the shipped Lua.

    With no paths, this is the coverage denominator's own inventory — one
    list, one source of truth — via scan(), so the enumeration problems ride
    along instead of being dropped (inventory() would refuse outright, but a
    lint should list everything wrong in one run, not one thing per run).
    Explicit paths are for spot-checking a file while editing it.
    """
    if paths is None:
        result = lua_inventory.scan(include_untracked=include_untracked)
        return (
            [(entry.path, entry.text) for entry in result.sources],
            list(result.problems),
        )

    out: List[Tuple[str, str]] = []
    for path in paths:
        try:
            rel = path.relative_to(REPO).as_posix()
        except ValueError:
            rel = path.as_posix()
        if path.suffix == ".lua":
            out.append((rel, path.read_text(encoding="utf-8", errors="replace")))
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in lua_inventory.LUA_LITERAL.finditer(text):
            offset = text.count("\n", 0, match.start(1))
            out.append((f'{rel}:R"LUA"@{offset + 1}', match.group(1)))
    return out, []


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Lint shipped pack Lua: one measurable decision per line."
    )
    parser.add_argument(
        "paths", nargs="*", type=pathlib.Path,
        help="spot-check these files instead of the inventory",
    )
    parser.add_argument(
        "--tracked-only", action="store_true",
        help="lint git-tracked files only (the per-build mode, used by the "
        "og_gameplay build dependency, so a junk untracked file cannot break "
        "every build); the coverage-gate path runs without this flag and "
        "lints the full inventory, untracked files included",
    )
    parser.add_argument(
        "--git-exe", metavar="PATH",
        help="git executable for the inventory's enumeration; sets "
        f"{lua_inventory.GIT_ENV_VAR} (which the CMake check targets already "
        "pass through the environment). Unused when spot-checking explicit "
        "paths, which skip the inventory.",
    )
    args = parser.parse_args(argv)
    if args.git_exe:
        os.environ[lua_inventory.GIT_ENV_VAR] = args.git_exe
    paths = [p.resolve() for p in args.paths] if args.paths else None

    sources, enumeration_problems = collect_sources(
        paths, include_untracked=not args.tracked_only
    )

    bound_names, stub_problems = load_bound_helper_names()
    enumeration_problems.extend(stub_problems)

    problems: List[str] = []
    records: List[LocalFn] = []
    scanned = 0
    for label, source in sources:
        scanned += 1
        problems.extend(violations(source, label))
        try:
            tokens = tokenize(source)
        except LuaLexError:
            continue  # already reported by violations() above
        problems.extend(guard_trio_violations(tokens, label))
        problems.extend(header_violations(source, label))
        records.extend(local_function_records(tokens, label))
    problems.extend(bound_helper_violations(records, bound_names))
    problems.extend(duplicate_body_violations(records))

    if enumeration_problems:
        print(
            f"{len(enumeration_problems)} problem(s) enumerating the "
            "shipped-Lua inventory (scripts/lua_inventory.py):"
        )
        for problem in enumeration_problems:
            print(f"  {problem}")
    if problems:
        print(
            f"{len(problems)} pack-Lua lint finding(s) "
            "(one-decision-per-line rules 1-6, style-contract rules 7-9):"
        )
        for problem in problems:
            print(f"  {problem}")
        print(
            "\nOne measurable decision per line, and the style contract "
            "(docs/lua-style.md) on top: anything sharing a line with "
            "something already covered shares its coverage point, and "
            "every shipped source must satisfy the style rules. Split or fix "
            "the offending code "
            "(see scripts/check_lua_statement_lines.py)."
        )
    if enumeration_problems or problems:
        return 1
    mode = ", tracked files only" if args.tracked_only else ""
    print(
        f"pack Lua: one decision per line + style contract "
        f"({scanned} source(s) scanned{mode})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
