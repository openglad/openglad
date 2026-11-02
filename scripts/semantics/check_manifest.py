#!/usr/bin/env python3
"""Check the simulation semantics manifest against the source tree.

The manifest (tests/semantics/manifest/<basename>.json, one file per
simulation source file) holds one row per branch point of the simulation:
a rule row for every game semantic, with the condition it is anchored on,
its arms and the tests that assert them, and an "other" row (plumbing,
defensive guard, loop, dead hit, test-only path) with a note for every
branch line that carries no game rule. docs/sim-semantics.md is the
reader's page; this docstring is the contract.

The check passes (exit 0) only when all of these hold:

  R1  every candidate branch line of every in-scope file is covered by
      exactly one row of that file's manifest, after anchoring, and
      every line a row claims is still the kind of branch its `kinds`
      records (a deleted or rewritten arm is a "stale line").  A
      candidate line is a line the review's grep matches (CPP_BRANCH /
      LUA_BRANCH below, comment lines skipped); the in-scope files are
      SCOPE_CPP plus every Lua file under packs/.
  R2  every rule row cites at least one test, every gtest citation names
      a TEST / TEST_F / TEST_P that exists in the cited file outside
      comments and `#if 0` regions, and every
      parity citation names a row id in tests/parity/scenario_table.h.
  R3  every rule row's status is "asserted", or "deferred" with an issue
      number (deferred rows are printed as notices, not errors).

Anchoring.  Each row carries `anchor`, the trimmed source text of its
anchor line, lines[anchor_index] (anchor_index defaults to 0).  When that
line no longer contains the anchor (whitespace collapsed), or contains it
only above the row before it, the check searches the file, ranking the
hits: inside the row's function first, then not above the row before it,
then nearest the line the row would sit on had it moved as far as the row
before it.  When that line is inside the row's function and no hit is, the
anchor was rewritten in place and the row is lost, not moved to a twin.  A row whose anchor is generic (a bare `else`, `default:`,
`end`, ...) is located only through its lines: it moves with the row before
it in its function, else in the file, and must land on a line that reads
exactly its anchor.  The located anchors then cut the file into stretches;
inside each, the old branch lines the rows claim are paired in order with
the branch lines there now when the counts agree, a pair standing only
between two branches of the same kind; every other line moves with the
anchor above it.  Without --fix every row that moved is a drift error
naming where its anchor now sits; with --fix the new lines are written to
the manifest file.  Whatever --fix cannot place, R1 reports.

Exit codes: 0 clean, 1 violations found, 2 the check cannot run (no
manifest directory, unreadable JSON, a malformed row).

Only manifest files are ever written, and only with --fix.  Standard
library only; Python 3.10+.
"""

from __future__ import annotations

import argparse
import bisect
import contextlib
import glob
import io
import json
import os
import re
import shutil
import sys
import tempfile
import time
from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# Scope and the candidate grep (the ONE place both live)
# --------------------------------------------------------------------------

# The C++ simulation files the review covered: src/gameplay and its public
# headers, networking, snapshot serialization, mapgen and the lobby left out.
SCOPE_CPP: tuple[str, ...] = (
    "include/openglad/gameplay/game_world.h",
    "include/openglad/gameplay/statistics.h",
    "include/openglad/gameplay/walker.h",
    "src/gameplay/astar.cpp",
    "src/gameplay/belt.cpp",
    "src/gameplay/belt_input.cpp",
    "src/gameplay/damage_number_event.cpp",
    "src/gameplay/effect.cpp",
    "src/gameplay/fearless.cpp",
    "src/gameplay/game_world.cpp",
    "src/gameplay/game_world_weather.cpp",
    "src/gameplay/gameplay_context.cpp",
    "src/gameplay/guy.cpp",
    "src/gameplay/living.cpp",
    "src/gameplay/mode/mode_tick.cpp",
    "src/gameplay/obmap.cpp",
    "src/gameplay/placement.cpp",
    "src/gameplay/possession.cpp",
    "src/gameplay/respawn/respawn.cpp",
    "src/gameplay/scenario_strip.cpp",
    "src/gameplay/script/bindings_entity.cpp",
    "src/gameplay/script/bindings_kit.cpp",
    "src/gameplay/script/campaign_hooks.cpp",
    "src/gameplay/script/family_decl.cpp",
    "src/gameplay/script/pack_scripts.cpp",
    "src/gameplay/script/script_host.cpp",
    "src/gameplay/script/world_scripts.cpp",
    "src/gameplay/sim_control_policy.cpp",
    "src/gameplay/sim_difficulty.h",
    "src/gameplay/sim_entity.cpp",
    "src/gameplay/sim_event_log.cpp",
    "src/gameplay/sim_input_handler.cpp",
    "src/gameplay/smooth.cpp",
    "src/gameplay/specials_view.cpp",
    "src/gameplay/stats.cpp",
    "src/gameplay/timed_effects.cpp",
    "src/gameplay/treasure.cpp",
    "src/gameplay/walker.cpp",
    "src/gameplay/walker_combat.cpp",
    "src/gameplay/walker_movement.cpp",
    "src/gameplay/walker_pathing.cpp",
    "src/gameplay/walker_specials.cpp",
    "src/gameplay/weap.cpp",
)
SCOPE_LUA_GLOB = "packs/**/*.lua"

CPP_BRANCH = re.compile(
    r"^\s*(\}\s*)?(if|else if|else|switch|case|default|while|for|do)\b|\?[^?:]*:")
LUA_BRANCH = re.compile(
    r"^\s*(if|elseif|else|while|for|repeat|until)\b|\b(and|or)\b")
COMMENT_PREFIXES = ("//", "--", "*")

MANIFEST_DIR = "tests/semantics/manifest"
SCENARIO_TABLE = "tests/parity/scenario_table.h"

RULE_STATUSES = ("asserted", "partial", "executed-only", "none", "deferred")
OTHER_KINDS = ("plumbing", "defensive", "dead", "test-only", "loop")

# Anchors with no identifying text of their own: after removing braces,
# parentheses, semicolons and whitespace, these say nothing about where
# they belong, so such rows are located through their lines only.
GENERIC_ANCHORS = frozenset({
    "", "else", "default:", "do", "repeat", "end", "then", "elseend",
    "endelse", "break", "return", "}else", "else{",
})


def in_scope_files(repo: str) -> list[str]:
    """Every in-scope source file, repo-relative, sorted."""
    lua = [os.path.relpath(p, repo).replace(os.sep, "/")
           for p in glob.glob(os.path.join(repo, SCOPE_LUA_GLOB), recursive=True)]
    return sorted(set(SCOPE_CPP) | set(lua))


def is_candidate(path: str, text: str) -> bool:
    """The review's grep: does this source line count as a branch line?"""
    stripped = text.strip()
    if stripped.startswith(COMMENT_PREFIXES):
        return False
    rx = LUA_BRANCH if path.endswith(".lua") else CPP_BRANCH
    return rx.search(text) is not None


def branch_kind(path: str, text: str) -> str:
    """The keyword the grep matched on a candidate line ("if", "else if",
    "case", "ternary", Lua's "elseif", "and", ...), "" for any other line."""
    if not is_candidate(path, text):
        return ""
    if path.endswith(".lua"):
        m = LUA_BRANCH.search(text)
        return m.group(1) or m.group(2)
    m = CPP_BRANCH.search(text)
    return m.group(2) or "ternary"


def read_lines(repo: str, path: str) -> list[str] | None:
    """The file's lines, without the empty one after a final newline."""
    full = os.path.join(repo, path)
    if not os.path.isfile(full):
        return None
    with open(full, encoding="utf-8", errors="replace", newline="") as fh:
        text = fh.read()
    lines = text.split("\n") if text else []
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def candidates_of(path: str, lines: list[str]) -> dict[int, str]:
    return {i: t for i, t in enumerate(lines, 1) if is_candidate(path, t)}


def norm(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def is_generic(anchor: str) -> bool:
    return re.sub(r"[\s{}();]", "", anchor) in GENERIC_ANCHORS


def function_tokens(function: str | None) -> frozenset[str]:
    """Every identifier a row's function label names: the review writes
    "statistics::walk_to_foe", "M.ai_rekindle", "seat_timer / label_for_role",
    "stage_level (placer closure)"; any of them may be the definition the
    row's branch sits in."""
    if not function:
        return frozenset()
    return frozenset(re.findall(r"[A-Za-z_]\w+", function))


# --------------------------------------------------------------------------
# Manifest I/O
# --------------------------------------------------------------------------

def dump_manifest(data: dict) -> str:
    """The canonical text of a manifest file (gen_manifest.py writes the same)."""
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def manifest_name(path: str, clashes: set[str]) -> str:
    """tests/semantics/manifest/<name>: the basename, or parent-basename when two
    in-scope files share a basename."""
    base = os.path.basename(path)
    if base in clashes:
        return os.path.basename(os.path.dirname(path)) + "-" + base + ".json"
    return base + ".json"


class CannotRun(Exception):
    pass


@dataclass
class Row:
    file: str             # manifest file (absolute)
    path: str             # source path
    section: str          # "rules" or "other"
    data: dict
    delta: int = 0
    located: bool = True
    new_lines: list[int] = field(default_factory=list)

    @property
    def id(self) -> str:
        return str(self.data["id"])

    @property
    def lines(self) -> list[int]:
        return self.data["lines"]

    @property
    def kinds(self) -> list[str] | None:
        """The branch kind of each line when the row was last written."""
        return self.data.get("kinds")

    @property
    def anchor(self) -> str:
        return str(self.data.get("anchor", ""))

    @property
    def function(self) -> str | None:
        return self.data.get("function")

    @property
    def anchor_line(self) -> int:
        """The line the anchor sits on: lines[anchor_index] (default the first)."""
        return self.lines[int(self.data.get("anchor_index", 0))]


def load_manifests(repo: str) -> tuple[dict[str, tuple[str, dict]], list[str]]:
    """{source path: (manifest file, data)} plus structural errors."""
    mdir = os.path.join(repo, MANIFEST_DIR)
    if not os.path.isdir(mdir):
        raise CannotRun(f"no manifest directory at {MANIFEST_DIR}")
    out: dict[str, tuple[str, dict]] = {}
    errors: list[str] = []
    for name in sorted(os.listdir(mdir)):
        if not name.endswith(".json"):
            continue
        full = os.path.join(mdir, name)
        rel = f"{MANIFEST_DIR}/{name}"
        try:
            with open(full, encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, ValueError) as exc:
            raise CannotRun(f"{rel}: not readable JSON ({exc})") from exc
        problem = validate_manifest(data)
        if problem:
            raise CannotRun(f"{rel}: {problem}")
        path = data["path"]
        if path in out:
            errors.append(f"{path} is described by both "
                          f"{os.path.basename(out[path][0])} and {name}")
            continue
        out[path] = (full, data)
    return out, errors


def validate_manifest(data: object) -> str | None:
    if not isinstance(data, dict) or not isinstance(data.get("path"), str):
        return 'not a manifest object (needs "path", "rules", "other")'
    for section in ("rules", "other"):
        if not isinstance(data.get(section), list):
            return f'"{section}" is not a list'
        for row in data[section]:
            if not isinstance(row, dict) or not isinstance(row.get("id"), str):
                return f'a row in "{section}" has no string id'
            lines = row.get("lines")
            if (not isinstance(lines, list) or not lines
                    or not all(isinstance(n, int) and n > 0 for n in lines)):
                return f'row {row["id"]}: "lines" must be a non-empty list of line numbers'
            if not isinstance(row.get("anchor", ""), str):
                return f'row {row["id"]}: "anchor" is not a string'
            kinds = row.get("kinds")
            if kinds is not None and (not isinstance(kinds, list) or len(kinds) != len(lines)
                                      or not all(isinstance(k, str) for k in kinds)):
                return f'row {row["id"]}: "kinds" must be a list of strings, one per line'
            k = row.get("anchor_index", 0)
            if isinstance(k, bool) or not isinstance(k, int) or not 0 <= k < len(lines):
                return f'row {row["id"]}: "anchor_index" must index "lines"'
    for row in data["rules"]:
        if row.get("status") not in RULE_STATUSES:
            return f'rule row {row["id"]}: status {row.get("status")!r} is not one of {", ".join(RULE_STATUSES)}'
        if not isinstance(row.get("tests"), list) or not all(isinstance(t, dict) for t in row["tests"]):
            return f'rule row {row["id"]}: "tests" is not a list of objects'
    for row in data["other"]:
        if row.get("kind") not in OTHER_KINDS:
            return f'other row {row["id"]}: kind {row.get("kind")!r} is not one of {", ".join(OTHER_KINDS)}'
    return None


# --------------------------------------------------------------------------
# Anchoring
# --------------------------------------------------------------------------

CPP_HEADER_RX = re.compile(r"^(?![\s#/{}*])(?!(?:if|else|for|while|switch|return|do|case)\b)"
                           r"[^;(]*?\b(\w+)\s*\(")
LUA_HEADER_RX = re.compile(r"^\s*(?:local\s+)?function\s+([\w.:]+)\s*\("
                           r"|^\s*(?:local\s+)?([\w.]+)\s*=\s*function\b"
                           r"|^\s*\[?\"?(\w+)\"?\]?\s*=\s*function\b")


def header_token(path: str, text: str) -> str | None:
    """The function name a definition line opens, or None when the line is
    not one.  C++: a line at column 0 (not a comment, preprocessor line,
    brace or statement keyword) whose first parenthesis follows a name and
    which does not end in ';'.  Lua: `function a.b(`, `name = function`."""
    if path.endswith(".lua"):
        m = LUA_HEADER_RX.match(text)
        if not m:
            return None
        name = next(g for g in m.groups() if g)
        return re.split(r"[.:]", name)[-1]
    if text.rstrip().endswith(";"):
        return None
    m = CPP_HEADER_RX.match(text)
    return m.group(1) if m else None


def enclosing_functions(path: str, src: list[str]) -> list[frozenset[str]]:
    """For every line (1-based; index 0 unused) the names of the function
    definitions it may sit in: the open definitions above it, where a
    definition closes every earlier one indented as deep or deeper (a Lua
    `local function` inside another keeps its parent open)."""
    out: list[frozenset[str]] = [frozenset()]
    stack: list[tuple[int, str]] = []
    current: frozenset[str] = frozenset()
    for text in src:
        tok = header_token(path, text)
        if tok:
            indent = len(text) - len(text.lstrip())
            while stack and stack[-1][0] >= indent:
                stack.pop()
            stack.append((indent, tok))
            current = frozenset(name for _, name in stack)
        out.append(current)
    return out


def find_anchor(row: Row, src: list[str], hint: int | None = None, floor: int = 0,
                enclosing: list[frozenset[str]] | None = None) -> int | None:
    """Line where the row's anchor sits now, or None.

    None also when the line the row would sit on is inside the row's
    function and none of the hits is: the anchor was rewritten in place,
    and a twin in another function is not this row's branch.

    An anchor text can repeat in a file (two `if (target)` a few lines
    apart), so the hits are ranked, best first:
      1. inside the row's function (the nearest definition above the hit
         names it);
      2. not above `floor`, the line below the preceding located row's
         anchor (an edit never reorders lines, so rows keep their order);
      3. nearest the line the row would sit on had it moved by `hint`, the
         shift of the preceding located row (an insert above both moves
         both by the same amount)."""
    want = norm(row.anchor)
    hits = [i for i, t in enumerate(src, 1) if want in norm(t)]
    if not hits:
        return None
    first = row.anchor_line
    predicted = first + (hint or 0)
    toks = function_tokens(row.function)
    if enclosing is None:
        enclosing = enclosing_functions(row.path, src)

    def owned(i: int) -> bool:
        return bool(toks & enclosing[i])

    # The line the row would sit on lies inside its function, yet no hit
    # does: the anchor was rewritten there.  (A row whose label names no
    # definition around it, "family table (check_special_ai wiring)", is
    # ranked as usual.)
    if 1 <= predicted < len(enclosing) and owned(predicted) and not any(owned(i) for i in hits):
        return None
    return min(hits, key=lambda i: (not owned(i), i < floor, abs(i - predicted), i))


def line_reads(src: list[str], line: int, anchor: str) -> bool:
    """Does `line` still read `anchor`?  A generic anchor (`else`, `{`, an
    empty line) must be the whole line; any other anchor a substring."""
    if not 1 <= line <= len(src):
        return False
    if is_generic(anchor):
        return norm(src[line - 1]) == norm(anchor)
    return norm(anchor) in norm(src[line - 1])


def anchor_file(rows: list[Row], src: list[str]) -> list[tuple[str, Row, int | None]]:
    """Locate every row of one file and work out where its lines sit now.

    Pass 1 finds each row's anchor line, in order of the old anchor lines
    (row.delta, row.located).  Pass 2 re-points every line of every located
    row: a line at or above the row's anchor moves with the anchor; a line
    below it moves with the nearest located anchor at or above it, of any
    row, so an insert between two arms of one row moves the lower arm only
    (row.new_lines).  Returns the findings: ("drift", row, new anchor line)
    for a row whose lines changed, ("lost", row, None), ("unanchored", row,
    None)."""
    findings: list[tuple[str, Row, int | None]] = []
    ordered = sorted(rows, key=lambda r: (r.anchor_line, r.lines[0], r.id))
    enclosing = enclosing_functions(rows[0].path, src) if rows else []
    last_by_function: dict[str | None, Row] = {}
    last_any: Row | None = None
    for row in ordered:
        # The lowest line this row's anchor may sit on now: below the row
        # before it (on the same line only when it shared that row's line).
        floor = 0
        if last_any is not None:
            floor = last_any.anchor_line + last_any.delta
            floor += 1 if row.anchor_line > last_any.anchor_line else 0
        if is_generic(row.anchor):
            # Carried by the preceding located row of its function, else by
            # the preceding located row of the file; it must land on its
            # own text and not above the row before it.
            carriers = [c for c in (last_by_function.get(row.function) if row.function else None,
                                    last_any) if c is not None]
            for carrier in carriers or [None]:
                row.delta = carrier.delta if carrier is not None else 0
                at = row.anchor_line + row.delta
                if line_reads(src, at, row.anchor) and at >= floor:
                    break
            else:
                row.delta = 0
                row.located = False
                findings.append(("unanchored", row, None))
                continue
        elif line_reads(src, row.anchor_line, row.anchor) and row.anchor_line >= floor:
            row.delta = 0
        else:
            # Not on its line, or on its line only by the chance of a twin
            # that now sits above the row before it.
            hint = last_any.delta if last_any is not None else None
            found = find_anchor(row, src, hint, floor, enclosing)
            if found is None:
                row.located = False
                findings.append(("lost", row, None))
                continue
            row.delta = found - row.anchor_line
        last_by_function[row.function] = row
        last_any = row

    # Pass 2: the located anchors cut the file into stretches.  Between two
    # neighbouring anchors (old a..a', new b..b') the old branch lines the
    # rows claim and the branch lines the file has now are paired in order
    # when there are as many of each: an edit there added or removed lines
    # that are not branches, so the k-th branch line is still the k-th.  A
    # pair stands only when both lines are the same kind of branch (the
    # row's `kinds`): a branch removed and another added in one stretch
    # keeps the count, and the newcomer must not take the old line's place.
    # A line left unpaired moves with its own row's anchor if it sits above
    # it, else with the anchor that opens its stretch; the coverage and
    # stale-line checks then report whatever does not fit.
    path = rows[0].path if rows else ""
    points = sorted({(r.anchor_line, r.anchor_line + r.delta) for r in rows if r.located})
    controls = dict(points)
    olds = [o for o, _ in points]
    cands = sorted(candidates_of(path, src)) if rows else []
    old_kind: dict[int, str | None] = {}
    for r in rows:
        if r.located:
            for i, n in enumerate(r.lines):
                old_kind.setdefault(n, r.kinds[i] if r.kinds is not None else None)
    old_lines = sorted(set(old_kind) - set(controls))
    paired: dict[int, int] = {}
    bounds = [(0, 0)] + points + [(None, len(src) + 1)]
    for (a0, b0), (a1, b1) in zip(bounds, bounds[1:]):
        old_seg = [n for n in old_lines if n > a0 and (a1 is None or n < a1)]
        new_seg = [c for c in cands if b0 < c < b1 and c not in controls.values()]
        if old_seg and len(old_seg) == len(new_seg):
            paired.update((o, c) for o, c in zip(old_seg, new_seg)
                          if old_kind[o] is not None and old_kind[o] == branch_kind(path, src[c - 1]))
    for row in rows:
        if not row.located:
            row.new_lines = list(row.lines)
            continue
        new = []
        for n in row.lines:
            if n in controls:
                new.append(controls[n])
            elif n in paired:
                new.append(paired[n])
            elif n <= row.anchor_line:
                new.append(n + row.delta)
            else:
                k = bisect.bisect_right(olds, n) - 1
                new.append(n + (points[k][1] - points[k][0]))
        row.new_lines = new

    for row in rows:
        if row.located and row.new_lines != row.lines:
            findings.append(("drift", row, row.anchor_line + row.delta))
    findings.sort(key=lambda f: (f[1].lines[0], f[1].id))
    return findings


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------

TEST_DEF_RX = re.compile(r"\bTEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
MACRO_DEF_RX = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)",
                          re.MULTILINE)


RAW_STRING_RX = re.compile(r'R"([^()\\\s]{0,16})\(')


def strip_comments(text: str) -> str:
    """C++ text with every comment blanked (newlines kept, so lines stay
    lines) and every `#if 0` region dropped: a test that is commented out or
    compiled out defines nothing.  String, character and raw string literals
    are skipped whole, so a "//" inside one is not a comment."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:j]))
            i = j
            continue
        m = RAW_STRING_RX.match(text, i) if ch == "R" and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")) else None
        if m:
            close = ")" + m.group(1) + '"'
            j = text.find(close, m.end())
            j = n if j < 0 else j + len(close)
            out.append(text[i:j])
            i = j
            continue
        if ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j])
            i = j
            continue
        out.append(ch)
        i += 1
    kept: list[str] = []
    dead = 0          # depth inside an `#if 0` region (0: live)
    for line in "".join(out).split("\n"):
        d = re.match(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", line)
        if dead:
            if d and d.group(1) in ("if", "ifdef", "ifndef"):
                dead += 1
            elif d and d.group(1) == "endif":
                dead -= 1
            elif d and d.group(1) in ("else", "elif") and dead == 1:
                dead = 0
            kept.append("")
            continue
        if d and d.group(1) == "if" and d.group(2).strip() == "0":
            dead = 1
            kept.append("")
            continue
        kept.append(line)
    return "\n".join(kept)


def tests_defined(text: str) -> set[str]:
    """Every Suite.name a test file defines: TEST/TEST_F/TEST_P written out,
    plus the ones a file-local macro stamps out (#define SCENE(test_name, ...)
    whose body holds TEST_F(Suite, test_name), invoked as SCENE(name, ...)).
    Comments and `#if 0` regions are stripped first."""
    text = strip_comments(text)
    names = {f"{a}.{b}" for a, b in TEST_DEF_RX.findall(text)}
    for m in MACRO_DEF_RX.finditer(text):
        macro, params = m.group(1), [p.strip() for p in m.group(2).split(",")]
        for suite, pname in TEST_DEF_RX.findall(m.group(3)):
            if pname not in params:
                continue
            k = params.index(pname)
            for call in re.finditer(r"^[ \t]*" + re.escape(macro) + r"\s*\(([^)]*)\)", text, re.MULTILINE):
                args = [a.strip() for a in call.group(1).split(",")]
                if len(args) > k and re.fullmatch(r"\w+", args[k]):
                    names.add(f"{suite}.{args[k]}")
    return names


def parity_row_ids(repo: str) -> set[str]:
    full = os.path.join(repo, SCENARIO_TABLE)
    if not os.path.isfile(full):
        return set()
    with open(full, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    return set(re.findall(r'^\s*\{\s*"(\w+)",\s*"[^"]*",', text, re.MULTILINE))


def test_exists(repo: str, file: str, test: str, cache: dict[str, set[str] | None]) -> str | None:
    """None when `file` defines the gtest Suite.name, else the reason."""
    if file not in cache:
        full = os.path.join(repo, file)
        if os.path.isfile(full):
            with open(full, encoding="utf-8", errors="replace") as fh:
                cache[file] = tests_defined(fh.read())
        else:
            cache[file] = None
    defined = cache[file]
    if defined is None:
        return f"{file} does not exist"
    suite, _, name = test.partition(".")
    if not suite or not name:
        return f'"{test}" is not Suite.name'
    return None if test in defined else f"{file} has no TEST {test}"


@dataclass
class Report:
    errors: dict[str, list[str]] = field(default_factory=dict)
    notices: list[str] = field(default_factory=list)
    fixed: list[str] = field(default_factory=list)
    counts: dict[str, int] = field(default_factory=dict)

    def add(self, kind: str, message: str) -> None:
        self.errors.setdefault(kind, []).append(f"{kind}: {message}")


ERROR_KINDS = (
    "missing file", "stray manifest", "duplicate manifest", "out of range",
    "uncovered", "double-claimed", "stale line", "missing kinds", "drift", "lost anchor",
    "unanchored",
    "no test", "missing test", "missing parity row", "not asserted",
    "deferred without issue",
)


def run_check(repo: str, fix: bool) -> Report:
    rep = Report()
    manifests, structural = load_manifests(repo)
    for msg in structural:
        rep.add("duplicate manifest", msg)
    scope = in_scope_files(repo)
    scope_set = set(scope)
    for path in sorted(manifests):
        if path not in scope_set:
            rep.add("stray manifest", f"{os.path.basename(manifests[path][0])} describes "
                    f"{path}, which is not an in-scope simulation file")

    parity_ids = parity_row_ids(repo)
    test_cache: dict[str, set[str] | None] = {}
    n_rules = n_other = n_cands = 0
    status_counts = {s: 0 for s in RULE_STATUSES}

    for path in scope:
        src = read_lines(repo, path)
        if src is None:
            rep.add("missing file", f"{path} is in scope but does not exist")
            continue
        cands = candidates_of(path, src)
        n_cands += len(cands)
        mfile, data = manifests.get(path, (None, {"rules": [], "other": []}))
        rows = ([Row(mfile, path, "rules", r) for r in data["rules"]]
                + [Row(mfile, path, "other", r) for r in data["other"]])
        n_rules += len(data["rules"])
        n_other += len(data["other"])

        # Anchor.
        for kind, row, where in anchor_file(rows, src):
            if kind == "drift":
                if row.delta:
                    what = f"anchor \"{norm(row.anchor)}\" is not on line {row.anchor_line}; it now sits on line {where}"
                    done = f"moved {row.delta:+d} (line {row.anchor_line} -> {where})"
                else:
                    moved = [(a, b) for a, b in zip(row.lines, row.new_lines) if a != b]
                    span = ", ".join(f"{a} -> {b}" for a, b in moved)
                    what = f"anchor stays on line {row.anchor_line}, but lines below it moved ({span})"
                    done = f"lines below the anchor moved ({span})"
                if fix:
                    rep.fixed.append(f"fixed: {path}: row {row.id} {done}")
                else:
                    rep.add("drift", f"{path}: row {row.id}: {what} (--fix re-points the row)")
            elif kind == "lost":
                twins = [i for i, t in enumerate(src, 1) if norm(row.anchor) in norm(t)]
                where_else = ("nowhere else in the file" if not twins else
                              f"not inside {row.function} (line{'s' if len(twins) > 1 else ''} "
                              f"{', '.join(map(str, twins))} in another function read{'' if len(twins) > 1 else 's'} the same)")
                rep.add("lost anchor", f"{path}: row {row.id}: anchor \"{norm(row.anchor)}\" is not on "
                        f"line {row.anchor_line} and {where_else}")
            else:
                rep.add("unanchored", f"{path}: row {row.id}: line {row.anchor_line} no longer reads "
                        f"\"{norm(row.anchor)}\" and no anchored row of the same function "
                        f"moved to carry it")
        # Kinds: a row written by hand may leave them out; --fix fills them in
        # from the lines the row now claims.
        kinds_written = False
        for row in rows:
            if row.kinds is not None:
                continue
            if fix and row.located:
                kinds = [branch_kind(path, src[n - 1]) if n <= len(src) else ""
                         for n in row.new_lines]
                items = list(row.data.items())
                at = next(i for i, (k, _) in enumerate(items) if k == "lines") + 1
                row.data.clear()
                row.data.update(items[:at] + [("kinds", kinds)] + items[at:])
                rep.fixed.append(f"fixed: {path}: row {row.id} kinds written")
                kinds_written = True
            else:
                rep.add("missing kinds", f"{path}: row {row.id}: no \"kinds\" (--fix writes them)")
        if fix and mfile is not None and (kinds_written or any(r.new_lines != r.lines for r in rows)):
            for row in rows:
                if row.new_lines != row.lines:
                    row.data["lines"] = list(row.new_lines)
            write_fixed(repo, mfile, data)

        # R1: every line a row claims is still the branch it was.
        for row in rows:
            if row.kinds is None or not row.located:
                continue
            for line, want in zip(row.new_lines, row.kinds):
                if line > len(src):
                    continue
                got = branch_kind(path, src[line - 1])
                if got == want:
                    continue
                if not got:
                    what = f"is no longer a branch line (the row says `{want}`)"
                elif not want:
                    what = f"is now a branch line (`{got}`), the row says it is none"
                else:
                    what = f"is now `{got}`, the row says `{want}`"
                rep.add("stale line", f"{path}: row {row.id}: line {line} {what}: {src[line - 1].strip()}")

        # R1: coverage.
        claims: dict[int, list[str]] = {}
        for row in rows:
            for line in row.new_lines:
                if line > len(src):
                    rep.add("out of range", f"{path}: row {row.id}: line {line} is past the "
                            f"end of the file ({len(src)} lines)")
                    continue
                claims.setdefault(line, []).append(row.id)
        for line in sorted(cands):
            owners = claims.get(line, [])
            if not owners:
                rep.add("uncovered", f"{path}:{line}: {cands[line].strip()}")
            elif len(owners) > 1:
                rep.add("double-claimed", f"{path}:{line}: claimed by rows {', '.join(owners)}")

        # R2 and R3.
        for row in (r for r in rows if r.section == "rules"):
            d = row.data
            status = d["status"]
            status_counts[status] += 1
            if not d["tests"]:
                rep.add("no test", f"{path}: row {row.id}: a rule row cites no test")
            for t in d["tests"]:
                if "parity" in t:
                    if str(t["parity"]) not in parity_ids:
                        rep.add("missing parity row", f"{path}: row {row.id}: parity row "
                                f"\"{t['parity']}\" is not in {SCENARIO_TABLE}")
                elif "file" in t and "test" in t:
                    why = test_exists(repo, str(t["file"]), str(t["test"]), test_cache)
                    if why:
                        rep.add("missing test", f"{path}: row {row.id}: {why}")
                else:
                    rep.add("missing test", f"{path}: row {row.id}: a test entry names "
                            f"neither file+test nor parity: {json.dumps(t, sort_keys=True)}")
            if status == "asserted":
                continue
            if status == "deferred":
                issue = d.get("issue")
                if issue_number(issue) is None:
                    rep.add("deferred without issue", f"{path}: row {row.id}: status deferred "
                            f"needs an issue number")
                else:
                    rep.notices.append(f"deferred: {path}: row {row.id}: issue #{issue_number(issue)}")
                continue
            rep.add("not asserted", f"{path}: row {row.id}: status {status}")

    rep.counts = {
        "files": len(scope), "candidates": n_cands, "rules": n_rules, "other": n_other,
        **{f"status {s}": status_counts[s] for s in RULE_STATUSES},
    }
    return rep


def issue_number(issue: object) -> int | None:
    if isinstance(issue, bool):
        return None
    if isinstance(issue, int) and issue > 0:
        return issue
    if isinstance(issue, str):
        m = re.fullmatch(r"\s*#?(\d+)\s*", issue)
        if m and int(m.group(1)) > 0:
            return int(m.group(1))
    return None


def write_fixed(repo: str, mfile: str, data: dict) -> None:
    mdir = os.path.realpath(os.path.join(repo, MANIFEST_DIR))
    if os.path.dirname(os.path.realpath(mfile)) != mdir:
        raise CannotRun(f"refusing to write {mfile}: not in {MANIFEST_DIR}")
    for section in ("rules", "other"):
        data[section].sort(key=lambda r: (r["lines"][0], r["id"]))
    with open(mfile, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(dump_manifest(data))


def render(rep: Report) -> tuple[str, int]:
    out: list[str] = []
    out.extend(rep.fixed)
    for kind in ERROR_KINDS:
        out.extend(rep.errors.get(kind, []))
    out.extend(rep.notices)
    c = rep.counts
    n = {k: len(rep.errors.get(k, [])) for k in ERROR_KINDS}
    not_asserted = c["rules"] - c["status asserted"] - c["status deferred"]
    verdict = "FAIL" if any(n.values()) else "OK"
    out.append(
        f"check_manifest: {verdict}: {c['files']} files, {c['candidates']} candidate lines, "
        f"{c['rules']} rule rows, {c['other']} other rows")
    out.append(
        f"  R1 uncovered {n['uncovered']}, double-claimed {n['double-claimed']}, "
        f"stale line {n['stale line']}, missing kinds {n['missing kinds']}, "
        f"drift {n['drift']}, lost anchor {n['lost anchor']}, unanchored {n['unanchored']}, "
        f"out of range {n['out of range']}, missing file {n['missing file']}, "
        f"stray manifest {n['stray manifest']}, duplicate manifest {n['duplicate manifest']}")
    out.append(
        f"  R2 no test {n['no test']}, missing test {n['missing test']}, "
        f"missing parity row {n['missing parity row']}")
    out.append(
        f"  R3 not asserted {not_asserted} (partial {c['status partial']}, "
        f"executed-only {c['status executed-only']}, none {c['status none']}), "
        f"deferred {c['status deferred']} (without issue {n['deferred without issue']})")
    if rep.fixed:
        out.append(f"  fixed {len(rep.fixed)} rows")
    return "\n".join(out) + "\n", (1 if verdict == "FAIL" else 0)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--fix", action="store_true", help="re-point drifted rows and rewrite their manifest files")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--report", metavar="PATH", help="also write the full output to PATH")
    ap.add_argument("--compare-candidates", metavar="DIR",
                    help="compare this tree's candidate lines with the review's DIR/*.txt "
                         "(lines 'path:line: text') and exit")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    repo = os.path.abspath(args.repo)
    if args.compare_candidates:
        return compare_candidates(repo, args.compare_candidates)
    try:
        rep = run_check(repo, args.fix)
    except CannotRun as exc:
        print(f"check_manifest: cannot run: {exc}")
        return 2
    text, rc = render(rep)
    sys.stdout.write(text)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as fh:
            fh.write(text)
    return rc


def compare_candidates(repo: str, cands_dir: str) -> int:
    review: set[tuple[str, int]] = set()
    for name in sorted(glob.glob(os.path.join(cands_dir, "*.txt"))):
        with open(name, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = re.match(r"^([^:]+):(\d+):", line)
                if m:
                    review.add((m.group(1), int(m.group(2))))
    mine: set[tuple[str, int]] = set()
    for path in in_scope_files(repo):
        src = read_lines(repo, path) or []
        mine |= {(path, n) for n in candidates_of(path, src)}
    only_mine = sorted(mine - review)
    only_review = sorted(review - mine)
    for p, n in only_mine:
        print(f"only the check: {p}:{n}")
    for p, n in only_review:
        print(f"only the review: {p}:{n}")
    same = not only_mine and not only_review
    print(f"compare_candidates: {'EQUAL' if same else 'DIFFERENT'}: the check {len(mine)}, "
          f"the review {len(review)}, only the check {len(only_mine)}, "
          f"only the review {len(only_review)}")
    return 0 if same else 1


# --------------------------------------------------------------------------
# Self-test
# --------------------------------------------------------------------------

_FIXTURE_SRC = """\
// fearless fixture
int pick(int a)
{
    if (a > 3)
        return 1;
    else
        return 2;
}

int other(int b)
{
    int c = b ? 4 : 5;
    while (c > 0)
        c--;
    return c;
}
"""

_FIXTURE_TESTS = """\
TEST(Fearless, picks_one_above_three) { }
TEST_F(FearlessFixture,
       picks_two_otherwise) { }
#define FEARLESS_SCENE(test_name, scene) \\
    TEST_F(FearlessScenes, test_name)    \\
    {                                    \\
        play(scene);                     \\
    }
FEARLESS_SCENE(counts_down_to_zero, "c_count")
TEST_P(FearlessSizes, picks_for_every_size) { }
"""

_FIXTURE_TABLE = """\
static const ScenarioSpec kScenarios[] = {
    { "fearless_pick_scen99", "scen/scen99.fss", 0x1u,
};
"""


def _fixture_manifest() -> dict:
    return {
        "path": "src/gameplay/fearless.cpp",
        "rules": [
            {"id": "fearless.cpp:4", "lines": [4], "kinds": ["if"], "function": "pick",
             "anchor": "if (a > 3)", "semantic": "s", "arms": [], "setting": "none",
             "status": "asserted",
             "tests": [{"file": "tests/unit/test_fearless.cpp", "test": "Fearless.picks_one_above_three"},
                       {"file": "tests/unit/test_fearless.cpp", "test": "FearlessSizes.picks_for_every_size"}],
             "missing_arms": []},
            {"id": "fearless.cpp:6", "lines": [6], "kinds": ["else"], "function": "pick",
             "anchor": "else", "semantic": "s", "arms": [], "setting": "none",
             "status": "asserted",
             "tests": [{"file": "tests/unit/test_fearless.cpp", "test": "FearlessFixture.picks_two_otherwise"}],
             "missing_arms": []},
            {"id": "fearless.cpp:12", "lines": [12], "kinds": ["ternary"], "function": "other",
             "anchor": "int c = b ? 4 : 5;", "semantic": "s", "arms": [], "setting": "none",
             "status": "asserted",
             "tests": [{"parity": "fearless_pick_scen99", "fact": "WalkerFamilyCount"},
                       {"file": "tests/unit/test_fearless.cpp", "test": "FearlessScenes.counts_down_to_zero"}],
             "missing_arms": []},
        ],
        "other": [
            {"id": "fearless.cpp:13", "lines": [13], "kinds": ["while"], "kind": "loop",
             "anchor": "while (c > 0)", "note": "countdown"},
        ],
    }


def _build_repo(root: str, manifest: dict | None, src: str = _FIXTURE_SRC,
                skip: tuple[str, ...] = (), tests: str = _FIXTURE_TESTS) -> None:
    for path in SCOPE_CPP:
        if path in skip:
            continue
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(src if path == "src/gameplay/fearless.cpp" else "")
    for rel, text in (("tests/unit/test_fearless.cpp", tests),
                      (SCENARIO_TABLE, _FIXTURE_TABLE)):
        full = os.path.join(root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(text)
    os.makedirs(os.path.join(root, MANIFEST_DIR), exist_ok=True)
    if manifest is not None:
        with open(os.path.join(root, MANIFEST_DIR, "fearless.cpp.json"), "w", encoding="utf-8") as fh:
            fh.write(dump_manifest(manifest))


def _run(argv: list[str]) -> tuple[int, str]:
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = main(argv)
    return rc, buf.getvalue()


def _r1(**counts: int) -> str:
    """The R1 counts line with every count 0 but the ones named
    (double_claimed=1 for "double-claimed 1")."""
    names = ("uncovered", "double-claimed", "stale line", "missing kinds", "drift", "lost anchor",
             "unanchored", "out of range", "missing file", "stray manifest", "duplicate manifest")
    want = {k.replace("_", "-" if k == "double_claimed" else " "): v for k, v in counts.items()}
    assert set(want) <= set(names), want
    return ", ".join(f"{k} {want.get(k, 0)}" for k in names)


def _summary(verdict: str, files: int = len(SCOPE_CPP), rules: int = 3, other: int = 1, cands: int = 4, r1: str = "", r2: str = "",
             r3: str = "", fixed: int = 0) -> str:
    r1 = r1 or _r1()
    r2 = r2 or "no test 0, missing test 0, missing parity row 0"
    r3 = r3 or "not asserted 0 (partial 0, executed-only 0, none 0), deferred 0 (without issue 0)"
    text = (f"check_manifest: {verdict}: {files} files, {cands} candidate lines, "
            f"{rules} rule rows, {other} other rows\n  R1 {r1}\n  R2 {r2}\n  R3 {r3}\n")
    if fixed:
        text += f"  fixed {fixed} rows\n"
    return text


def self_test() -> int:
    started = time.monotonic()
    failures: list[str] = []
    cases = 0

    def expect(name: str, got: object, want: object) -> None:
        nonlocal cases
        cases += 1
        if got != want:
            failures.append(f"{name}:\n--- want\n{want}\n--- got\n{got}")

    tmp = tempfile.mkdtemp(prefix="semantics_selftest_")
    try:
        def case(name: str, manifest: dict | None, *, src: str = _FIXTURE_SRC,
                 skip: tuple[str, ...] = (), tests: str = _FIXTURE_TESTS) -> str:
            root = os.path.join(tmp, name)
            _build_repo(root, manifest, src, skip, tests)
            return root

        # The grep, clause by clause (the review's exact rules).
        cpp_lines = {
            "if (x)": True, "  } else if (y) {": True, "else": True, "}else{": True,
            "switch (k) {": True, "case 3:": True, "default:": True, "while (n--)": True,
            "for (;;)": True, "do {": True, "int a = b ? c : d;": True,
            "// if (commented)": False, "* else in a block comment": False,
            "// pick = a ? b : c;": False, " * keeps x ? y : z": False,
            "  /* if */ x = 1;": False, "iffy = 2;": False, "elsewhere();": False,
            "return casey;": False, "x = cond ? std::max(a, b) : c;": True,
            "doit();": False, "a ?: b": True, "q = p ? 1 : 0; // ternary": True,
        }
        lua_lines = {
            "if x then": True, "elseif y then": True, "else": True, "while true do": True,
            "for i = 1, 3 do": True, "repeat": True, "until done": True,
            "local v = a and b": True, "return x or y": True, "-- if commented": False,
            "  --[[ and ]]": False, "local android = 1": False, "ordinal = 2": False,
            "endif = 3": False, "local t = {}": False, "s = 'this or that'": True,
        }
        got_cpp = {t: is_candidate("src/gameplay/x.cpp", t) for t in cpp_lines}
        got_lua = {t: is_candidate("packs/core/lib/x.lua", t) for t in lua_lines}
        expect("grep: C++ clauses", got_cpp, cpp_lines)
        expect("grep: Lua clauses", got_lua, lua_lines)
        expect("grep: fixture candidate lines",
               sorted(candidates_of("src/gameplay/fearless.cpp", _FIXTURE_SRC.split("\n"))),
               [4, 6, 12, 13])
        # A .lua file outside SCOPE_CPP joins the scope by the packs glob.
        root = case("scope", _fixture_manifest())
        os.makedirs(os.path.join(root, "packs/core/lib"), exist_ok=True)
        with open(os.path.join(root, "packs/core/lib/kit.lua"), "w", encoding="utf-8") as fh:
            fh.write("local M = {}\nif x then\n  y = 1\nend\nreturn M\n")
        rc, out = _run(["--repo", root])
        expect("scope: an unclaimed pack Lua line is red",
               (rc, out),
               (1, "uncovered: packs/core/lib/kit.lua:2: if x then\n"
                + _summary("FAIL", cands=5, r1="uncovered 1, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0", files=len(SCOPE_CPP) + 1)))

        # Clean tree.
        root = case("clean", _fixture_manifest())
        expect("clean: green", _run(["--repo", root]), (0, _summary("OK")))

        # R1: uncovered line.
        m = _fixture_manifest()
        del m["other"][0]
        root = case("uncovered", m)
        expect("R1: uncovered line is red", _run(["--repo", root]),
               (1, "uncovered: src/gameplay/fearless.cpp:13: while (c > 0)\n"
                + _summary("FAIL", other=0, r1="uncovered 1, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))

        # R1: double claim.
        m = _fixture_manifest()
        m["other"][0]["lines"] = [12, 13]
        m["other"][0]["kinds"] = ["ternary", "while"]
        m["other"][0]["anchor"] = "int c = b ? 4 : 5;"
        root = case("double", m)
        expect("R1: double-claimed line is red", _run(["--repo", root]),
               (1, "double-claimed: src/gameplay/fearless.cpp:12: claimed by rows fearless.cpp:12, "
                "fearless.cpp:13\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 1, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))

        # R1: a missing in-scope file and a stray manifest.
        m = _fixture_manifest()
        root = case("missing_file", m, skip=("src/gameplay/belt.cpp",))
        with open(os.path.join(root, MANIFEST_DIR, "elsewhere.json"), "w", encoding="utf-8") as fh:
            fh.write(dump_manifest({"path": "src/net/lobby.cpp", "rules": [], "other": []}))
        expect("R1: missing in-scope file and stray manifest are red", _run(["--repo", root]),
               (1, "missing file: src/gameplay/belt.cpp is in scope but does not exist\n"
                "stray manifest: elsewhere.json describes src/net/lobby.cpp, which is not an "
                "in-scope simulation file\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 1, stray manifest 1, "
                           "duplicate manifest 0")))

        # R2: missing TEST, missing test file, missing parity row, no test.
        m = _fixture_manifest()
        m["rules"][0]["tests"] = [{"file": "tests/unit/test_fearless.cpp", "test": "Fearless.gone"},
                                  {"file": "tests/unit/test_nowhere.cpp", "test": "A.b"}]
        m["rules"][2]["tests"] = [{"parity": "no_such_scen99", "fact": "x"},
                                  {"file": "tests/unit/test_fearless.cpp", "test": "FearlessScenes.scene"}]
        m["rules"][1]["tests"] = []
        root = case("r2", m)
        expect("R2: missing TEST / file / parity row / no test are red", _run(["--repo", root]),
               (1, "no test: src/gameplay/fearless.cpp: row fearless.cpp:6: a rule row cites no test\n"
                "missing test: src/gameplay/fearless.cpp: row fearless.cpp:4: "
                "tests/unit/test_fearless.cpp has no TEST Fearless.gone\n"
                "missing test: src/gameplay/fearless.cpp: row fearless.cpp:4: "
                "tests/unit/test_nowhere.cpp does not exist\n"
                "missing test: src/gameplay/fearless.cpp: row fearless.cpp:12: "
                "tests/unit/test_fearless.cpp has no TEST FearlessScenes.scene\n"
                "missing parity row: src/gameplay/fearless.cpp: row fearless.cpp:12: parity row "
                "\"no_such_scen99\" is not in tests/parity/scenario_table.h\n"
                + _summary("FAIL", r2="no test 1, missing test 3, missing parity row 1")))

        # R3: partial is red; deferred with an issue is a notice; without one is red.
        m = _fixture_manifest()
        m["rules"][0]["status"] = "partial"
        root = case("partial", m)
        expect("R3: partial is red", _run(["--repo", root]),
               (1, "not asserted: src/gameplay/fearless.cpp: row fearless.cpp:4: status partial\n"
                + _summary("FAIL", r3="not asserted 1 (partial 1, executed-only 0, none 0), "
                           "deferred 0 (without issue 0)")))
        m = _fixture_manifest()
        m["rules"][2]["status"] = "deferred"
        m["rules"][2]["issue"] = 451
        root = case("deferred", m)
        expect("R3: deferred with an issue is a notice only", _run(["--repo", root]),
               (0, "deferred: src/gameplay/fearless.cpp: row fearless.cpp:12: issue #451\n"
                + _summary("OK", r3="not asserted 0 (partial 0, executed-only 0, none 0), "
                           "deferred 1 (without issue 0)")))
        del m["rules"][2]["issue"]
        root = case("deferred_bare", m)
        expect("R3: deferred without an issue is red", _run(["--repo", root]),
               (1, "deferred without issue: src/gameplay/fearless.cpp: row fearless.cpp:12: "
                "status deferred needs an issue number\n"
                + _summary("FAIL", r3="not asserted 0 (partial 0, executed-only 0, none 0), "
                           "deferred 1 (without issue 1)")))

        # Drift: two lines inserted above pick(), one more inside other().
        drifted = _FIXTURE_SRC.replace("int pick(int a)", "#include <x>\n\nint pick(int a)").replace(
            "    int c = b ? 4 : 5;", "    b += 1;\n    int c = b ? 4 : 5;")
        root = case("drift", _fixture_manifest(), src=drifted)
        drift_r1 = ("uncovered 0, double-claimed 0, stale line 0, missing kinds 0, drift 4, lost anchor 0, unanchored 0, "
                    "out of range 0, missing file 0, stray manifest 0, duplicate manifest 0")
        expect("drift without --fix is red", _run(["--repo", root]),
               (1, "drift: src/gameplay/fearless.cpp: row fearless.cpp:4: anchor \"if (a > 3)\" is not "
                "on line 4; it now sits on line 6 (--fix re-points the row)\n"
                "drift: src/gameplay/fearless.cpp: row fearless.cpp:6: anchor \"else\" is not "
                "on line 6; it now sits on line 8 (--fix re-points the row)\n"
                "drift: src/gameplay/fearless.cpp: row fearless.cpp:12: anchor \"int c = b ? 4 : 5;\" "
                "is not on line 12; it now sits on line 15 (--fix re-points the row)\n"
                "drift: src/gameplay/fearless.cpp: row fearless.cpp:13: anchor \"while (c > 0)\" is not "
                "on line 13; it now sits on line 16 (--fix re-points the row)\n"
                + _summary("FAIL", r1=drift_r1)))
        mpath = os.path.join(root, MANIFEST_DIR, "fearless.cpp.json")
        with open(mpath, encoding="utf-8") as fh:
            before = fh.read()
        expect("drift without --fix leaves the manifest alone", before, dump_manifest(_fixture_manifest()))
        expect("drift with --fix is repaired", _run(["--repo", root, "--fix"]),
               (0, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:4 moved +2 (line 4 -> 6)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:6 moved +2 (line 6 -> 8)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:12 moved +3 (line 12 -> 15)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:13 moved +3 (line 13 -> 16)\n"
                + _summary("OK", fixed=4)))
        with open(mpath, encoding="utf-8") as fh:
            after = json.load(fh)
        expect("--fix moves every row, the generic else with its function's anchored row",
               [(r["id"], r["lines"]) for r in after["rules"] + after["other"]],
               [("fearless.cpp:4", [6]), ("fearless.cpp:6", [8]),
                ("fearless.cpp:12", [15]), ("fearless.cpp:13", [16])])
        with open(mpath, encoding="utf-8") as fh:
            fixed_once = fh.read()
        expect("--fix is idempotent", _run(["--repo", root, "--fix"]), (0, _summary("OK")))
        with open(mpath, encoding="utf-8") as fh:
            expect("--fix twice leaves identical bytes", fh.read(), fixed_once)

        # Lost anchor and an unanchored generic row.
        lost = _FIXTURE_SRC.replace("if (a > 3)", "if (a >= 4)")
        root = case("lost", _fixture_manifest(), src=lost)
        expect("a vanished anchor is red, with or without --fix",
               _run(["--repo", root, "--fix"]),
               (1, "lost anchor: src/gameplay/fearless.cpp: row fearless.cpp:4: anchor \"if (a > 3)\" is "
                "not on line 4 and nowhere else in the file\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 1, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))
        moved_else = _FIXTURE_SRC.replace("        return 1;\n    else\n", "        return 1;\n\n    else\n")
        root = case("unanchored", _fixture_manifest(), src=moved_else)
        expect("a generic row that no anchored row carries is red", _run(["--repo", root, "--fix"]),
               (1, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:12 moved +1 (line 12 -> 13)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:13 moved +1 (line 13 -> 14)\n"
                "uncovered: src/gameplay/fearless.cpp:7: else\n"
                "unanchored: src/gameplay/fearless.cpp: row fearless.cpp:6: line 6 no longer reads "
                "\"else\" and no anchored row of the same function moved to carry it\n"
                + _summary("FAIL", r1="uncovered 1, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 1, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0", fixed=2)))

        # R1: a row line past the end of the file.
        m = _fixture_manifest()
        m["other"][0]["lines"] = [13, 40]
        m["other"][0]["kinds"] = ["while", ""]
        root = case("out_of_range", m)
        expect("R1: a line past the end of the file is red", _run(["--repo", root]),
               (1, "out of range: src/gameplay/fearless.cpp: row fearless.cpp:13: line 40 is past "
                "the end of the file (16 lines)\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 1, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))

        # R1: two manifest files describing one source file.
        root = case("duplicate", _fixture_manifest())
        with open(os.path.join(root, MANIFEST_DIR, "fearless-copy.json"), "w", encoding="utf-8") as fh:
            fh.write(dump_manifest(_fixture_manifest()))
        expect("R1: two manifests for one file are red", _run(["--repo", root]),
               (1, "duplicate manifest: src/gameplay/fearless.cpp is described by both "
                "fearless-copy.json and fearless.cpp.json\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 0, stale line 0, missing kinds 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 1")))

        # R2: a test entry that names neither a gtest nor a parity row.
        m = _fixture_manifest()
        m["rules"][1]["tests"] = [{"note": "watched it once"}]
        root = case("note_only", m)
        expect("R2: a note-only test entry is red", _run(["--repo", root]),
               (1, "missing test: src/gameplay/fearless.cpp: row fearless.cpp:6: a test entry names "
                "neither file+test nor parity: {\"note\": \"watched it once\"}\n"
                + _summary("FAIL", r2="no test 0, missing test 1, missing parity row 0")))

        # Anchoring: twin anchors.  Rows on two identical `if (k)` lines, and
        # a third row after a non-branch line that quotes the same text.
        twins_src = ("// twins\nint twin(int k)\n{\n    if (k)\n        k++;\n    if (k)\n"
                     "        k--;\n    if (j)\n        say(\"if (q)\");\n    if (q)\n"
                     "        k = 0;\n    return k;\n}\n")

        def twins_manifest() -> dict:
            def row(line: int, anchor: str) -> dict:
                return {"id": f"fearless.cpp:{line}", "lines": [line], "kinds": ["if"], "function": "twin",
                        "anchor": anchor, "semantic": "s", "arms": [], "setting": "none",
                        "status": "asserted",
                        "tests": [{"file": "tests/unit/test_fearless.cpp",
                                   "test": "Fearless.picks_one_above_three"}],
                        "missing_arms": []}
            return {"path": "src/gameplay/fearless.cpp",
                    "rules": [row(4, "if (k)"), row(6, "if (k)"), row(8, "if (j)"), row(10, "if (q)")],
                    "other": []}

        def fixed_lines(root: str) -> list[tuple[str, list[int]]]:
            with open(os.path.join(root, MANIFEST_DIR, "fearless.cpp.json"), encoding="utf-8") as fh:
                got = json.load(fh)
            return [(r["id"], r["lines"]) for r in got["rules"] + got["other"]]

        moved = twins_src.replace("int twin", "// a\n// b\n// c\n// d\n// e\nint twin")
        root = case("twins_above", twins_manifest(), src=moved)
        rc, _ = _run(["--repo", root, "--fix"])
        expect("twins: an insert above both moves each to its own line (the shift of the row before)",
               (rc, fixed_lines(root)),
               (0, [("fearless.cpp:4", [9]), ("fearless.cpp:6", [11]),
                    ("fearless.cpp:8", [13]), ("fearless.cpp:10", [15])]))
        moved = moved.replace("        k++;\n", "        k++;\n        // f\n        // g\n        // h\n")
        root = case("twins_between", twins_manifest(), src=moved)
        rc, _ = _run(["--repo", root, "--fix"])
        expect("twins: an insert between them never puts the lower row above the upper",
               (rc, fixed_lines(root)),
               (0, [("fearless.cpp:4", [9]), ("fearless.cpp:6", [14]),
                    ("fearless.cpp:8", [16]), ("fearless.cpp:10", [18])]))
        expect("twins: --fix twice changes nothing", _run(["--repo", root, "--fix"]),
               (0, _summary("OK", rules=4, other=0, cands=4)))

        # Anchoring: an anchor repeated in another function goes to the row's own.
        two_funcs = ["int alpha(int k)", "{", "    if (k)", "        k++;", "    return k;", "}",
                     "", "", "", "", "", "", "", "", "",
                     "int beta(int k)", "{", "    if (k)", "        k--;", "    return k;", "}"]
        lone = Row("", "src/gameplay/fearless.cpp", "rules",
                   {"id": "fearless.cpp:8", "lines": [8], "function": "beta", "anchor": "if (k)"})
        expect("find_anchor: the occurrence inside the row's function wins over a nearer one",
               find_anchor(lone, two_funcs), 18)

        # A generic anchor must be the whole line.
        expect("line_reads: a generic anchor matches only a line that is just it",
               [line_reads(["} else {"], 1, "{"), line_reads(["  {"], 1, "{"),
                line_reads(["} else {"], 1, "else"), line_reads(["} else {"], 1, "} else {")],
               [False, True, False, True])

        # An insert between two arms of one row moves the lower arm only.
        m = _fixture_manifest()
        m["rules"][0]["lines"] = [4, 6]
        m["rules"][0]["kinds"] = ["if", "else"]
        del m["rules"][1]
        between = _FIXTURE_SRC.replace("        return 1;\n", "        return 1;\n        // one\n")
        root = case("arms", m, src=between)
        expect("an insert between two arms of one row is repaired arm by arm",
               _run(["--repo", root, "--fix"]),
               (0, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:4 lines below the anchor moved "
                "(6 -> 7)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:12 moved +1 (line 12 -> 13)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:13 moved +1 (line 13 -> 14)\n"
                + _summary("OK", rules=2, fixed=3)))
        expect("arm by arm: the lines", fixed_lines(root),
               [("fearless.cpp:4", [4, 7]), ("fearless.cpp:12", [13]), ("fearless.cpp:13", [14])])

        # A row anchored on its second line (anchor_index) moves by that line.
        m = _fixture_manifest()
        m["other"][0]["lines"] = [11, 13]
        m["other"][0]["kinds"] = ["", "while"]
        m["other"][0]["anchor_index"] = 1
        root = case("anchor_index", m, src=_FIXTURE_SRC.replace("int other", "// x\nint other"))
        rc, _ = _run(["--repo", root, "--fix"])
        expect("anchor_index: the row moves with the line it is anchored on",
               (rc, fixed_lines(root)),
               (0, [("fearless.cpp:4", [4]), ("fearless.cpp:6", [6]),
                    ("fearless.cpp:12", [13]), ("fearless.cpp:13", [12, 14])]))

        # A branch removed and another added in the same stretch: the count
        # of branch lines stays, but an `if` may not take an `else`'s place.
        def two_arms() -> dict:
            m = _fixture_manifest()
            m["rules"][0]["lines"] = [4, 6]
            m["rules"][0]["kinds"] = ["if", "else"]
            del m["rules"][1]
            return m

        swapped = _FIXTURE_SRC.replace("    else\n        return 2;\n",
                                       "        return 2;\n    if (zz_swap) {}\n")
        swap_out = ("uncovered: src/gameplay/fearless.cpp:7: if (zz_swap) {}\n"
                    "stale line: src/gameplay/fearless.cpp: row fearless.cpp:4: line 6 is no longer a "
                    "branch line (the row says `else`): return 2;\n"
                    + _summary("FAIL", rules=2, r1=_r1(uncovered=1, stale_line=1)))
        root = case("swap", two_arms(), src=swapped)
        expect("swap: a new if where an else was is red without --fix", _run(["--repo", root]),
               (1, swap_out))
        expect("swap: and red with --fix, the row left alone", _run(["--repo", root, "--fix"]),
               (1, swap_out))
        expect("swap: --fix wrote nothing", fixed_lines(root),
               [("fearless.cpp:4", [4, 6]), ("fearless.cpp:12", [12]), ("fearless.cpp:13", [13])])

        # An arm deleted: the line the row still claims is no branch now.
        root = case("arm_gone", two_arms(), src=_FIXTURE_SRC.replace("    else\n", ""))
        expect("arm deleted: --fix moves the rows below, the stale arm is red",
               _run(["--repo", root, "--fix"]),
               (1, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:12 moved -1 (line 12 -> 11)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:13 moved -1 (line 13 -> 12)\n"
                "stale line: src/gameplay/fearless.cpp: row fearless.cpp:4: line 6 is no longer a "
                "branch line (the row says `else`): return 2;\n"
                + _summary("FAIL", rules=2, cands=3, r1=_r1(stale_line=1), fixed=2)))

        # An arm rewritten into another kind of branch on its own line.
        root = case("arm_kind", two_arms(), src=_FIXTURE_SRC.replace("    else\n", "    else if (a > 1)\n"))
        expect("arm rewritten: `else` -> `else if` is red, with or without --fix",
               [_run(["--repo", root]), _run(["--repo", root, "--fix"])],
               [(1, "stale line: src/gameplay/fearless.cpp: row fearless.cpp:4: line 6 is now `else if`, "
                 "the row says `else`: else if (a > 1)\n"
                 + _summary("FAIL", rules=2, r1=_r1(stale_line=1)))] * 2)

        # An anchor rewritten in place, with a twin of its old text in another
        # function: lost, not moved onto the twin.
        rewritten = _FIXTURE_SRC.replace("if (a > 3)", "if (a >= 4)").replace(
            "        c--;\n", "        c--;\n    if (a > 3) c = 9;\n")
        root = case("rewritten", _fixture_manifest(), src=rewritten)
        expect("anchor rewritten in place: lost, the twin elsewhere is not taken",
               _run(["--repo", root, "--fix"]),
               (1, "uncovered: src/gameplay/fearless.cpp:15: if (a > 3) c = 9;\n"
                "lost anchor: src/gameplay/fearless.cpp: row fearless.cpp:4: anchor \"if (a > 3)\" is not "
                "on line 4 and not inside pick (line 15 in another function reads the same)\n"
                + _summary("FAIL", cands=5, r1=_r1(uncovered=1, lost_anchor=1))))
        expect("anchor rewritten in place: --fix wrote nothing", fixed_lines(root),
               [("fearless.cpp:4", [4]), ("fearless.cpp:6", [6]),
                ("fearless.cpp:12", [12]), ("fearless.cpp:13", [13])])

        # A row without kinds: red, and --fix writes them.
        m = _fixture_manifest()
        del m["rules"][1]["kinds"]
        root = case("no_kinds", m)
        expect("a row without kinds is red", _run(["--repo", root]),
               (1, "missing kinds: src/gameplay/fearless.cpp: row fearless.cpp:6: no \"kinds\" "
                "(--fix writes them)\n" + _summary("FAIL", r1=_r1(missing_kinds=1))))
        expect("--fix writes the kinds", _run(["--repo", root, "--fix"]),
               (0, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:6 kinds written\n"
                + _summary("OK", fixed=1)))
        with open(os.path.join(root, MANIFEST_DIR, "fearless.cpp.json"), encoding="utf-8") as fh:
            expect("--fix wrote exactly the fixture", fh.read(), dump_manifest(_fixture_manifest()))

        # R2: a test that is commented out or compiled out defines nothing;
        # a "//" inside a string literal is no comment.
        dead_tests = (_FIXTURE_TESTS
                      + "// TEST(Fearless, commented_out) { }\n"
                      + "/* TEST(Fearless, blocked_out) { }\n   still blocked */\n"
                      + "#if 0\nTEST(Fearless, compiled_out) { }\n#endif\n"
                      + "TEST(Fearless, url_in_string) { auto u = \"http://x\"; } TEST(Fearless, after_url) { }\n"
                      + "TEST(Fearless, after_the_dead) { }\n")
        m = _fixture_manifest()
        m["rules"][0]["tests"] = [
            {"file": "tests/unit/test_fearless.cpp", "test": f"Fearless.{n}"}
            for n in ("commented_out", "blocked_out", "compiled_out", "url_in_string", "after_url",
                      "after_the_dead")]
        root = case("dead_tests", m, tests=dead_tests)
        expect("R2: a commented-out or compiled-out TEST is red", _run(["--repo", root]),
               (1, "".join(f"missing test: src/gameplay/fearless.cpp: row fearless.cpp:4: "
                           f"tests/unit/test_fearless.cpp has no TEST Fearless.{n}\n"
                           for n in ("commented_out", "blocked_out", "compiled_out"))
                + _summary("FAIL", r2="no test 0, missing test 3, missing parity row 0")))
        expect("strip_comments: #if 0 nests and ends at its #else",
               strip_comments("a\n#if 0\n#ifdef X\nb\n#endif\nc\n#else\nd\n#endif\ne").split("\n"),
               ["a", "", "", "", "", "", "", "d", "#endif", "e"])

        # --fix never writes outside the manifest directory.
        root = case("guard", _fixture_manifest())
        outside = os.path.join(root, "src", "gameplay", "fearless.json")
        try:
            write_fixed(root, outside, _fixture_manifest())
            guard = "wrote"
        except CannotRun as exc:
            guard = str(exc).replace(root, "<root>")
        expect("write_fixed refuses a path outside the manifest directory",
               (guard, os.path.exists(outside)),
               ("refusing to write <root>/src/gameplay/fearless.json: not in tests/semantics/manifest",
                False))

        # The generator: context lines dropped, anchor on the branch line,
        # citations normalised, byte-identical twice, a row with no line refused.
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gen_manifest  # noqa: PLC0415  (gen_manifest imports this module)
        root = case("gen", None)
        review = os.path.join(root, "review")
        os.makedirs(review)
        rows = [
            {"id": "fearless.cpp:3", "path": "src/gameplay/fearless.cpp", "lines": [3, 4, 5, 6],
             "function": "pick", "kind": "rule", "condition": "a > 3", "semantic": "picks",
             "arms": [{"when": "a > 3", "observable": "1"}], "setting": "none",
             "depends_on": [], "stage": {"how": "x", "sketch": "y"}, "suspect": "z",
             "assertion": {"status": "asserted",
                           "where": ["tests/unit/test_fearless.cpp: TEST(Fearless, picks_one_above_three) (both arms)",
                                     "parity: fearless_pick_scen99 / WalkerFamilyCount",
                                     "GAP: nobody looks at a == 3"]}},
            {"id": "fearless.cpp:12", "path": "src/gameplay/fearless.cpp", "lines": [12, 13],
             "kind": "loop", "note": "countdown"},
            {"id": "fearless.cpp:5", "path": "src/gameplay/fearless.cpp", "lines": [5, 6, 12],
             "kind": "dead", "note": "starts on a statement, then a bare else"},
        ]
        with open(os.path.join(review, "part.json"), "w", encoding="utf-8") as fh:
            json.dump(rows, fh)
        outs = []
        for k in range(2):
            mdir = os.path.join(root, f"out{k}")
            with contextlib.redirect_stdout(io.StringIO()):
                rc = gen_manifest.generate(root, review, mdir)
            with open(os.path.join(mdir, "fearless.cpp.json"), encoding="utf-8") as fh:
                outs.append((rc, fh.read()))
        got = json.loads(outs[0][1])
        expect("gen: twice gives identical bytes", outs[0], outs[1])
        expect("gen: the rule row",
               got["rules"],
               [{"id": "fearless.cpp:3", "lines": [4, 6], "kinds": ["if", "else"], "function": "pick", "anchor": "if (a > 3)",
                 "condition": "a > 3", "semantic": "picks",
                 "arms": [{"when": "a > 3", "observable": "1"}], "setting": "none",
                 "status": "asserted",
                 "tests": [{"file": "tests/unit/test_fearless.cpp", "test": "Fearless.picks_one_above_three",
                            "note": "both arms"},
                           {"parity": "fearless_pick_scen99", "fact": "WalkerFamilyCount"}],
                 "missing_arms": [], "note": ["GAP: nobody looks at a == 3"]}])
        expect("gen: the other row", got["other"],
               [{"id": "fearless.cpp:5", "lines": [6, 12], "kinds": ["else", "ternary"], "kind": "dead",
                 "anchor": "int c = b ? 4 : 5;", "anchor_index": 1,
                 "note": "starts on a statement, then a bare else"},
                {"id": "fearless.cpp:12", "lines": [12, 13], "kinds": ["ternary", "while"], "kind": "loop",
                 "anchor": "int c = b ? 4 : 5;", "note": "countdown"}])
        rows[0]["lines"] = []
        rows[0]["id"] = "fearless.cpp:pick"
        with open(os.path.join(review, "part.json"), "w", encoding="utf-8") as fh:
            json.dump(rows, fh)
        err = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
            rc = gen_manifest.generate(root, review, os.path.join(root, "out2"))
        expect("gen: a row with no line exits 2 with its own message", (rc, err.getvalue()),
               (2, "gen_manifest: row fearless.cpp:pick: no lines and no line number in its id\n"))

        # Exit 2: no manifest directory, malformed row.
        root = case("no_dir", None)
        shutil.rmtree(os.path.join(root, MANIFEST_DIR))
        expect("no manifest directory exits 2", _run(["--repo", root]),
               (2, "check_manifest: cannot run: no manifest directory at tests/semantics/manifest\n"))
        m = _fixture_manifest()
        m["rules"][0]["status"] = "great"
        root = case("malformed", m)
        expect("a malformed row exits 2", _run(["--repo", root]),
               (2, "check_manifest: cannot run: tests/semantics/manifest/fearless.cpp.json: rule row "
                "fearless.cpp:4: status 'great' is not one of asserted, partial, executed-only, none, "
                "deferred\n"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    elapsed = time.monotonic() - started
    for f in failures:
        print(f"FAIL {f}")
    print(f"check_manifest self-test: {cases - len(failures)}/{cases} cases pass "
          f"({'OK' if not failures else 'FAIL'}, {elapsed:.2f} s)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
