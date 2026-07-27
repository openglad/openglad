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

WHAT IS SCANNED
---------------
Whatever scripts/lua_inventory.py calls shipped Lua — the same list the
coverage denominator is built from, so a file cannot be game logic to one tool
and invisible to the other. (It used to be two lists, and they disagreed.)
Enumeration problems from that module (an undeclared blob of embedded Lua, a
stale declaration) fail this lint too: they are the list being wrong, which is
strictly worse than an entry on the list being wrong.

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

Run: python3 scripts/check_lua_statement_lines.py [--tracked-only] [paths...]
Exit 0 when clean, 1 with a listing otherwise. Explicit paths are for
spot-checking files while editing them and skip the inventory entirely.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections import Counter
from typing import Iterable, List, Optional, Tuple

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import lua_inventory  # noqa: E402  (path set up immediately above)
from lua_inventory import LuaLexError, Token, tokenize  # noqa: E402,F401

REPO = lua_inventory.REPO

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
    args = parser.parse_args(argv)
    paths = [p.resolve() for p in args.paths] if args.paths else None

    sources, enumeration_problems = collect_sources(
        paths, include_untracked=not args.tracked_only
    )

    problems: List[str] = []
    scanned = 0
    for label, source in sources:
        scanned += 1
        problems.extend(violations(source, label))

    if enumeration_problems:
        print(
            f"{len(enumeration_problems)} problem(s) enumerating the "
            "shipped-Lua inventory (scripts/lua_inventory.py):"
        )
        for problem in enumeration_problems:
            print(f"  {problem}")
    if problems:
        print(f"{len(problems)} pack-Lua line(s) hide more than one decision:")
        for problem in problems:
            print(f"  {problem}")
        print(
            "\nOne measurable decision per line: anything sharing a line with "
            "something already covered shares its coverage point, so untested "
            "logic reads as covered. Split them "
            "(see scripts/check_lua_statement_lines.py)."
        )
    if enumeration_problems or problems:
        return 1
    mode = ", tracked files only" if args.tracked_only else ""
    print(f"pack Lua: one decision per line ({scanned} source(s) scanned{mode})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
