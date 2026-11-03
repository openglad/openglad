#!/usr/bin/env python3
"""Check that every branch of the simulation says what it decides.

Every candidate branch line of the simulation (the C++ in SCOPE_CPP and every
class-pack Lua file under packs/) carries a semantics comment: a block of
comment lines right above the statement, or, for an arm of a chain that is a
rule of its own, a trailing comment on the arm's line.  docs/sim-semantics.md
is the reader's page; this docstring is the contract.

The grammar (C++ with //, Lua with --, the same words):

    // rule: <what the branch decides>
    //   <continuation: two or more spaces after the comment marker>
    // tests: Suite.name, Other.name, parity:<row id>
    // deferred: #<issue> <why the current behaviour is not pinned>
    // unreachable: <why no test can reach the arm>

    // plumbing: <why there is no rule here>

A rule block is `rule:` (and its continuation lines), then one or more
`tests:` lines, then at most one `deferred:` or `unreachable:` line.  A
`plumbing:` block stands alone (it may have continuation lines).  A block
sits on the lines immediately above the statement it covers, at that
statement's indentation, with no blank line between.

Scope.  A block above a statement covers its PARAGRAPH: that statement and
every following statement that starts at the same indentation, until a blank
line, a line indented less (a closing `}` or `end` of the enclosing block
included), the first line of another block at that indentation or less, or an
arm of a statement outside the paragraph (the next `case` of an enclosing
switch).  Plain comments and deeper lines are stepped over.  Of every
statement in the paragraph the block covers the head (the first line and
every continuation line: a condition, an argument list or an expression
wrapped over several lines) and, for a compound statement, its arms:

  * an `if`: every `else if` / `else` of the chain at the same indentation
    (`} else if` and a `}` line followed by `else` included, also past a blank
    line inside a body); Lua: every `elseif` / `else` before the chain's `end`;
  * a `switch`: every `case` / `default` label of that switch (not of a
    switch nested inside it);
  * a `do`: its closing `} while (...)`; Lua `repeat`: its `until`;
  * a `while` / `for` loop: its head only;
  * a `#define`: every backslash-continued line of the macro.

The bodies are not covered: a branch inside a body has its own block, whose
paragraph is its own (a nested block takes over the statements below it in
the body, and the outer paragraph runs on after the body).  A block above a
`case` / `default` label covers that label alone, and the switch's block then
no longer covers it.  A trailing block (`} else if (x) {  // rule: ...`, on
the arm's first or last line, its further lines on the comment lines right
after it, indented deeper than the arm) covers the arm it sits on, and that
arm then leaves the scope of the block above the chain.  A trailing block
anywhere but on the first or last line of an `else` / `else if` / `elseif`
arm is stray.

A `rule:` block that names tests may sit above any statement, a branch or
not (a gate function whose whole body is one comparison, a family-table
field), so that its tests are checked; a plumbing block, or a rule naming no
test, above a paragraph with no branch line is stray.

Candidate lines are the lines the review's grep matches, CPP_BRANCH and
LUA_BRANCH below, applied to the code with every comment and every string
and character literal blanked out: an `and` inside a Lua string or a `?:`
inside a format string is not a branch.  In C++ any `?` left in the blanked
code is a ternary, so a ternary whose `?` and `:` sit on different lines is
a candidate on its `?` line.

The check passes (exit 0) only when:

  R1  every candidate line is covered by exactly one block ("uncovered",
      "double-covered"), and every block covers at least one candidate line
      ("stray": a plumbing block or a rule block naming no test above a
      paragraph with no branch line, a block separated from its statement by
      a blank line or another comment, a block above an arm of a chain or
      inside a continuation, a trailing block off an else arm's first or
      last line);
  R2  every rule block cites a test or says why it cannot ("no tests"), every
      gtest it cites (Suite.name) is defined by a TEST / TEST_F / TEST_P in a
      file under tests/, outside comments and `#if 0`, and is not disabled
      ("missing test", "disabled test"), every parity row it cites is in
      tests/parity/scenario_table.h ("missing parity row"), and every block
      follows the grammar ("malformed": an unknown keyword, a keyword out of
      order, a deferred line without #<issue>, an unreachable line without a
      reason, a plumbing block with tests, a misindented line);
  R3  the summary line counts it all; deferred and unreachable blocks are
      printed as notices, not errors.

Exit codes: 0 clean, 1 violations found, 2 the check cannot run (an in-scope
file or the parity table is missing).  The check only reads.  Standard
library only; Python 3.10+.
"""

from __future__ import annotations

import argparse
import bisect
import contextlib
import glob
import io
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

# Any `?` left in the blanked code is a ternary, also when its `:` sits on a
# later line (a `?:` inside a string or comment is blanked away first).
CPP_BRANCH = re.compile(
    r"^\s*(\}\s*)?(if|else if|else|switch|case|default|while|for|do)\b|\?")
LUA_BRANCH = re.compile(
    r"^\s*(if|elseif|else|while|for|repeat|until)\b|\b(and|or)\b")

TESTS_DIR = "tests"
SCENARIO_TABLE = "tests/parity/scenario_table.h"

KEYWORDS = ("rule", "tests", "deferred", "unreachable", "plumbing")
TAB_WIDTH = 4


def in_scope_files(repo: str, cpp: tuple[str, ...] = SCOPE_CPP,
                   lua_glob: str = SCOPE_LUA_GLOB) -> list[str]:
    """Every in-scope source file, repo-relative, sorted."""
    lua = [os.path.relpath(p, repo).replace(os.sep, "/")
           for p in glob.glob(os.path.join(repo, lua_glob), recursive=True)]
    return sorted(set(cpp) | set(lua))


# --------------------------------------------------------------------------
# Lexing: the code with comments and literals blanked, and the line comments
# --------------------------------------------------------------------------

RAW_PREFIX_RX = re.compile(r'(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')


def _blank(text: str) -> str:
    return "".join(c if c == "\n" else " " for c in text)


def lex_cpp(text: str) -> tuple[str, list[tuple[int, int]]]:
    """(code, comments): `code` is `text` with every comment and the inside of
    every string, character and raw string literal replaced by spaces (newlines
    and the quotes kept); `comments` lists the (start, end) offsets of every
    `//` comment, the marker included."""
    out: list[str] = []
    comments: list[tuple[int, int]] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if text.startswith("//", i):
            j = i
            while True:          # a backslash-newline splices the next line in
                k = text.find("\n", j)
                if k < 0:
                    k = n
                    break
                if k > 0 and text[k - 1] == "\\":
                    j = k + 1
                    continue
                break
            comments.append((i, k))
            out.append(_blank(text[i:k]))
            i = k
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(_blank(text[i:j]))
            i = j
            continue
        prev = text[i - 1] if i else ""
        if ch in "uULR" and not (prev.isalnum() or prev == "_"):
            m = RAW_PREFIX_RX.match(text, i)
            if m:
                close = ")" + m.group(1) + '"'
                j = text.find(close, m.end())
                j = n if j < 0 else j + len(close)
                out.append(text[i:m.end()] + _blank(text[m.end():j - len(close)]) + close)
                i = j
                continue
        if ch == "'" and (prev.isalnum() or prev == "_"):
            # a digit separator (1'000), unless the word before is a prefix
            word = re.search(r"\w*$", text[max(0, i - 3):i]).group(0)
            if word not in ("L", "u", "U", "u8"):
                out.append(ch)
                i += 1
                continue
        if ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j, n)
            out.append(ch + _blank(text[i + 1:j]) + (ch if j < n and text[j] == ch else ""))
            i = j + 1 if j < n and text[j] == ch else j
            continue
        out.append(ch)
        i += 1
    return "".join(out), comments


LONG_OPEN_RX = re.compile(r"\[(=*)\[")


def lex_lua(text: str) -> tuple[str, list[tuple[int, int]]]:
    """lex_cpp for Lua: `--` line comments (listed), `--[[ ]]` long comments,
    `"` / `'` strings and `[[ ]]` long strings blanked."""
    out: list[str] = []
    comments: list[tuple[int, int]] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if text.startswith("--", i):
            m = LONG_OPEN_RX.match(text, i + 2)
            if m:
                close = "]" + m.group(1) + "]"
                j = text.find(close, m.end())
                j = n if j < 0 else j + len(close)
                out.append(_blank(text[i:j]))
                i = j
                continue
            k = text.find("\n", i)
            k = n if k < 0 else k
            comments.append((i, k))
            out.append(_blank(text[i:k]))
            i = k
            continue
        if ch == "[":
            m = LONG_OPEN_RX.match(text, i)
            if m:
                close = "]" + m.group(1) + "]"
                j = text.find(close, m.end())
                j = n if j < 0 else j + len(close)
                out.append(m.group(0) + _blank(text[m.end():max(m.end(), j - len(close))])
                           + (close if j <= n and text[j - len(close):j] == close else ""))
                i = j
                continue
        if ch in "\"'":
            j = i + 1
            while j < n and text[j] != ch and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j, n)
            out.append(ch + _blank(text[i + 1:j]) + (ch if j < n and text[j] == ch else ""))
            i = j + 1 if j < n and text[j] == ch else j
            continue
        out.append(ch)
        i += 1
    return "".join(out), comments


# --------------------------------------------------------------------------
# One source file: its lines, candidates and statements
# --------------------------------------------------------------------------

def width(ws: str) -> int:
    return len(ws.expandtabs(TAB_WIDTH))


@dataclass
class Grammar:
    """One comment parsed against the grammar."""
    kind: str        # a KEYWORDS entry, "cont", "unknown" (a `word:` line), "text"
    text: str        # what follows the keyword (or the continuation's text)
    word: str = ""   # the unknown keyword


GRAMMAR_RX = re.compile(r"^ ([A-Za-z][\w-]*):(?:\s(.*)|)$")
CONT_RX = re.compile(r"^  +(\S.*)$")


def parse_grammar(text: str) -> Grammar:
    """`text` is a comment's body, after its `//` or `--` marker."""
    text = text.rstrip()
    m = GRAMMAR_RX.match(text)
    if m:
        word = m.group(1)
        body = (m.group(2) or "").strip()
        if word in KEYWORDS:
            return Grammar(word, body)
        return Grammar("unknown", body, word)
    m = CONT_RX.match(text)
    if m:
        return Grammar("cont", m.group(1).strip())
    return Grammar("text", text.strip())


class Source:
    """A source file read for the check."""

    def __init__(self, path: str, text: str) -> None:
        self.path = path
        self.lua = path.endswith(".lua")
        self.text = text
        lines = text.split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        self.lines = lines
        code, comments = (lex_lua if self.lua else lex_cpp)(text)
        self.code = code.split("\n")[:len(lines)]
        while len(self.code) < len(lines):
            self.code.append("")
        # Line comments by line: (column of the marker, the body after it).
        starts = [0]
        for ln in lines:
            starts.append(starts[-1] + len(ln) + 1)
        self.comment: dict[int, tuple[int, str]] = {}
        for a, b in comments:
            li = bisect.bisect_right(starts, a) - 1
            if li >= len(lines) or li in self.comment:
                continue
            col = a - starts[li]
            body = text[a + 2:b].split("\n")[0]
            self.comment[li] = (col, body)
        self.n = len(lines)
        self.cands: dict[int, str] = {}
        for i in range(self.n):
            k = self.branch_kind(i)
            if k:
                self.cands[i] = k
        self._analyse()

    # -- line classes -------------------------------------------------------

    def is_blank(self, i: int) -> bool:
        return not self.lines[i].strip()

    def has_code(self, i: int) -> bool:
        return bool(self.code[i].strip())

    def slash_only(self, i: int) -> bool:
        """A line holding nothing but one line comment."""
        if self.has_code(i) or i not in self.comment:
            return False
        col, _ = self.comment[i]
        return not self.lines[i][:col].strip()

    def indent(self, i: int) -> str:
        ln = self.lines[i]
        return ln[:len(ln) - len(ln.lstrip())]

    def branch_kind(self, i: int) -> str:
        """The keyword the grep matched on line i ("if", "else if", "case",
        "ternary", Lua's "elseif", "and", ...), "" when it is no candidate."""
        code = self.code[i]
        if not code.strip():
            return ""
        if self.lua:
            m = LUA_BRANCH.search(code)
            return (m.group(1) or m.group(2)) if m else ""
        m = CPP_BRANCH.search(code)
        return (m.group(2) or "ternary") if m else ""

    def lead(self, i: int) -> str:
        """The statement keyword line i starts with, "" for none."""
        code = self.code[i].strip()
        if self.lua:
            m = re.match(r"(if|elseif|else|while|for|repeat|until|end)\b", code)
            return m.group(1) if m else ""
        if code.startswith("#"):
            return "#"
        m = re.match(r"(\}\s*)?(else\s+if|if|else|switch|case|default|while|for|do)\b", code)
        if not m:
            return ""
        word = re.sub(r"\s+", " ", m.group(2))
        if m.group(1):
            return "} " + word
        return word

    # -- statements ---------------------------------------------------------

    def _skip(self, i: int) -> bool:
        """A line a statement scan steps over: blank or comment-only."""
        return not self.has_code(i)

    def _depth(self, code: str) -> int:
        opens = "([{" if self.lua else "(["
        closes = ")]}" if self.lua else ")]"
        return sum(code.count(c) for c in opens) - sum(code.count(c) for c in closes)

    def _next_code(self, j: int) -> int | None:
        j += 1
        while j < self.n and self._skip(j):
            j += 1
        return j if j < self.n else None

    def head_lines(self, i: int) -> list[int]:
        """Line i and the continuation lines of the statement it starts; for a
        C++ control head whose body is one plain statement without braces
        (`if (x)` then `y = a ? b : c;` on the next line), that body too."""
        if self.lua:
            return self._lua_head(i)
        return self._with_body(self._cpp_head(i))

    def _with_body(self, head: list[int]) -> list[int]:
        word = self.lead(head[0]).replace("} ", "")
        if word not in ("if", "else if", "else", "while", "for", "do"):
            return head
        last = self.code[head[-1]].strip()
        if word in ("else", "do"):
            if not re.search(r"\b(else|do)$", last):
                return head
        elif not last.endswith(")"):
            return head
        nxt = self._next_code(head[-1])
        if nxt is None or self.code[nxt].strip().startswith(("{", "#")) or self.lead(nxt):
            return head
        if width(self.indent(nxt)) <= width(self.indent(head[0])):
            return head
        return head + self._cpp_head(nxt)

    def _cpp_head(self, i: int) -> list[int]:
        lead = self.lead(i)
        out: list[int] = []
        if lead == "#":
            j = i
            while j < self.n:
                out.append(j)
                if not self.lines[j].rstrip().endswith("\\"):
                    break
                j += 1
            return out
        word = lead.replace("} ", "")
        control = word in ("if", "else if", "while", "for", "switch")
        depth = 0
        opened = False
        j = i
        while j < self.n and len(out) < 200:
            if j > i and self._skip(j):
                j += 1
                continue
            if j > i and self.code[j].strip().startswith("#"):
                j += 1
                continue
            code = self.code[j].strip()
            out.append(j)
            if "(" in code:
                opened = True
            depth += self._depth(code)
            if depth <= 0:
                end = code[-1:] if code else ""
                if control:
                    if opened and end in ("{", ")", ";", "}"):
                        break
                elif word in ("else", "do"):
                    if end in ("{", ";", "}") or code.endswith(("else", "do")):
                        break
                elif word in ("case", "default"):
                    if end in (":", "{", ";", "}"):
                        break
                else:
                    if end in (";", "{", "}", ","):
                        break
                    if re.fullmatch(r"(public|private|protected|\w+)\s*:", code):
                        break
            j += 1
        return out

    LUA_OP_END = re.compile(r"(\band|\bor|\bnot|==|~=|<=|>=|[<>=+\-*/%^#,(\[{]|\.\.)\s*$")
    LUA_OP_START = re.compile(r"^(and\b|or\b|\.\.|==|~=|<=|>=|[<>+*/%^]|-(?!-))")

    def _lua_head(self, i: int) -> list[int]:
        word = self.lead(i)
        out: list[int] = []
        depth = 0
        j = i
        while j < self.n and len(out) < 200:
            if j > i and self._skip(j):
                j += 1
                continue
            code = self.code[j].strip()
            out.append(j)
            depth += self._depth(code)
            if word in ("if", "elseif"):
                if depth <= 0 and re.search(r"\bthen\b", code):
                    break
            elif word in ("while", "for"):
                if depth <= 0 and re.search(r"\bdo\b", code):
                    break
            elif word in ("else", "repeat", "end"):
                break
            else:
                if re.search(r"\bfunction\b", code) or code.endswith("{"):
                    break
                if depth <= 0:
                    if code.endswith(","):
                        break
                    if not self.LUA_OP_END.search(code):
                        nxt = self._next_code(j)
                        if nxt is None or not self.LUA_OP_START.match(self.code[nxt].strip()):
                            break
            j += 1
        return out

    def _arms(self, s: int, head: list[int]) -> list[list[int]]:
        """The arms of the compound statement starting at s."""
        word = self.lead(s)
        if self.lua:
            if word == "if":
                return self._lua_chain(s, head, ("elseif", "else"))
            if word == "repeat":
                return self._lua_chain(s, head, ("until",))
            return []
        if word == "if":
            return self._cpp_chain(s, head)
        if word == "switch":
            return self._cpp_switch(s, head)
        if word == "do":
            return self._cpp_do(s, head)
        return []

    def _same_indent_scan(self, s: int, start: int):
        """Yield the lines from `start` on that sit at s's indentation, stepping
        over deeper and empty lines, until a shallower line."""
        ind = width(self.indent(s))
        j = start
        while j < self.n:
            if self._skip(j) or self.code[j].strip().startswith("#") and not self.lua:
                j += 1
                continue
            w = width(self.indent(j))
            if w > ind:
                j += 1
                continue
            if w < ind:
                return
            yield j
            j += 1

    def _lua_chain(self, s: int, head: list[int], arm_words: tuple[str, ...]) -> list[list[int]]:
        # A chain closed on its own head line (`if x then y end`) has no arms.
        if self.lead(s) == "if" and self._closes_on_line(head[-1]):
            return []
        arms: list[list[int]] = []
        start = head[-1] + 1
        while True:
            found = None
            for j in self._same_indent_scan(s, start):
                if self.lead(j) in arm_words:
                    found = j
                break
            if found is None:
                return arms
            h = self._lua_head(found)
            arms.append(h)
            if self.lead(found) == "until":
                return arms
            start = h[-1] + 1

    def _closes_on_line(self, j: int) -> bool:
        """Does the Lua line j close every block it opens (`if x then y end`)?"""
        code = self.code[j]
        opens = len(re.findall(r"\b(if|function|do)\b", code)) - len(
            re.findall(r"\b(while|for)\b[^\n]*?\bdo\b", code))
        ends = len(re.findall(r"\bend\b", code))
        return ends >= opens and ends > 0

    def _cpp_chain(self, s: int, head: list[int]) -> list[list[int]]:
        arms: list[list[int]] = []
        start = head[-1] + 1
        while True:
            found = None
            for j in self._same_indent_scan(s, start):
                code = self.code[j].strip()
                if code == "{":
                    continue
                if code.startswith("}"):
                    rest = code[1:].strip()
                    if re.match(r"else\b", rest):
                        found = j
                        break
                    if rest in ("", ";"):
                        continue
                    break
                if re.match(r"else\b", code):
                    found = j
                break
            if found is None:
                return arms
            h = self._with_body(self._cpp_head(found))
            arms.append(h)
            start = h[-1] + 1

    def _cpp_do(self, s: int, head: list[int]) -> list[list[int]]:
        for j in self._same_indent_scan(s, head[-1] + 1):
            code = self.code[j].strip()
            if re.match(r"\}\s*while\b", code) or re.match(r"while\b", code):
                return [self._cpp_head(j)]
            if code in ("}", "{"):
                continue
            return []
        return []

    def _cpp_switch(self, s: int, head: list[int]) -> list[list[int]]:
        # From the switch's `{`, the labels at depth 1 of its body.
        depth = 0
        started = False
        arms: list[list[int]] = []
        for j in range(head[-1], self.n):
            code = self.code[j]
            if j == head[-1]:
                # only the part after the condition's closing paren matters
                code = code[code.rfind(")") + 1:] if ")" in code else code
            if started and depth == 1 and re.match(r"\s*(case|default)\b", code):
                arms.append([j])
            for c in code:
                if c == "{":
                    depth += 1
                    started = True
                elif c == "}":
                    depth -= 1
            if started and depth <= 0:
                break
        return arms

    def _analyse(self) -> None:
        """Walk the file once: every statement start, its head and its arms.
        `role[i]` is ("head", s) or ("arm", s, k) for every line a statement's
        head or arm holds."""
        self.role: dict[int, tuple] = {}
        self.heads: dict[int, list[int]] = {}
        self.arms: dict[int, list[list[int]]] = {}
        for i in range(self.n):
            if i in self.role or not self.has_code(i):
                continue
            code = self.code[i].strip()
            if not self.lua and code.startswith("#") and not re.match(r"#\s*define\b", code):
                continue
            if re.fullmatch(r"[{}()\[\];,\s]*", code):
                continue
            if self.lua and re.fullmatch(r"end\b[\s)},;]*", code):
                continue
            head = self.head_lines(i)
            arms = self._arms(i, head)
            self.heads[i] = head
            self.arms[i] = arms
            for j in head:
                self.role.setdefault(j, ("head", i))
            for k, arm in enumerate(arms):
                for j in arm:
                    self.role.setdefault(j, ("arm", i, k))

    def is_arm_start(self, i: int) -> bool:
        """Does line i open an arm of a chain (an else, case, until, ...)?"""
        w = self.lead(i)
        if self.lua:
            return w in ("elseif", "else", "until")
        return w.startswith("} ") or w in ("else", "else if", "case", "default")

    def unit_of(self, i: int) -> tuple[int, int | None]:
        """(statement, arm index or None) of the head or arm holding line i."""
        r = self.role.get(i)
        if r is None:
            return i, None
        if r[0] == "head":
            return r[1], None
        return r[1], r[2]


# --------------------------------------------------------------------------
# Comment blocks
# --------------------------------------------------------------------------

TRAILING_START_RX = re.compile(r"(?:^|\s(?://|--))( (?:rule|plumbing):(?:\s.*|))$")


@dataclass
class Block:
    path: str
    first: int                 # the block's first line (the arm's line when trailing)
    last: int                  # its last line
    trailing: bool
    entries: list[tuple[int, Grammar]] = field(default_factory=list)
    target: int | None = None  # the statement line an above block sits on
    covers: list[int] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)

    @property
    def kind(self) -> str:
        return self.entries[0][1].kind if self.entries else ""

    def texts(self, kw: str) -> list[str]:
        """The text of every `kw` entry, its continuation lines joined on."""
        out: list[str] = []
        cur: list[str] | None = None
        for _, g in self.entries:
            if g.kind == "cont":
                if cur is not None:
                    cur.append(g.text)
                continue
            if cur is not None:
                out.append(" ".join(t for t in cur if t))
                cur = None
            if g.kind == kw:
                cur = [g.text]
        if cur is not None:
            out.append(" ".join(t for t in cur if t))
        return out


def comment_body(src: Source, i: int) -> str | None:
    c = src.comment.get(i)
    return None if c is None else c[1]


def parse_blocks(src: Source) -> tuple[list[Block], list[str]]:
    """Every block of the file and the "outside a block" problems."""
    blocks: list[Block] = []
    loose: list[str] = []
    used: set[int] = set()
    i = 0
    while i < src.n:
        if i in used:
            i += 1
            continue
        body = comment_body(src, i)
        if body is None:
            i += 1
            continue
        if src.slash_only(i):
            g = parse_grammar(body)
            if g.kind in ("rule", "plumbing"):
                b = Block(src.path, i, i, False, [(i, g)])
                j = i + 1
                while j < src.n and src.slash_only(j):
                    g2 = parse_grammar(comment_body(src, j))
                    if g2.kind in ("rule", "plumbing"):
                        break
                    b.entries.append((j, g2))
                    j += 1
                b.last = j - 1
                used.update(range(i, j))
                blocks.append(b)
                i = j
                continue
            if g.kind in ("tests", "deferred", "unreachable"):
                loose.append(f"{src.path}:{i + 1}: `{g.kind}:` outside a block "
                             f"(a block starts with `rule:` or `plumbing:`)")
            i += 1
            continue
        if src.has_code(i):
            m = TRAILING_START_RX.search(body)
            if m:
                g = parse_grammar(m.group(1))
                b = Block(src.path, i, i, True, [(i, g)])
                j = i + 1
                while j < src.n and src.slash_only(j):
                    g2 = parse_grammar(comment_body(src, j))
                    if g2.kind in ("rule", "plumbing", "text"):
                        break
                    b.entries.append((j, g2))
                    j += 1
                b.last = j - 1
                used.update(range(i + 1, j))
                blocks.append(b)
                i = j
                continue
        i += 1
    return blocks, loose


TEST_NAME_RX = re.compile(r"^[A-Za-z_]\w*\.[A-Za-z_]\w*$")
PARITY_RX = re.compile(r"^parity:(\w+)$")


def validate_grammar(src: Source, b: Block) -> None:
    """Order and form of the block's lines; problems land on b.problems."""
    where = f"{src.path}:{b.first + 1}"
    # keyword order: rule, tests..., one deferred/unreachable; or plumbing alone
    seen: list[str] = []
    for line, g in b.entries:
        if g.kind == "unknown":
            b.problems.append(f"{src.path}:{line + 1}: unknown keyword `{g.word}:` "
                              f"(the grammar knows {', '.join(k + ':' for k in KEYWORDS)})")
            continue
        if g.kind == "text":
            b.problems.append(f"{src.path}:{line + 1}: a comment inside a block is neither a "
                              f"keyword line nor a continuation (indent continuation lines by "
                              f"two or more spaces after the marker)")
            continue
        if g.kind == "cont":
            continue
        seen.append(g.kind)
    if b.kind == "plumbing":
        if any(k != "plumbing" for k in seen):
            b.problems.append(f"{where}: a plumbing block stands alone "
                              f"(it has {', '.join(k + ':' for k in seen if k != 'plumbing')})")
        if not " ".join(b.texts("plumbing")).strip():
            b.problems.append(f"{where}: plumbing: says nothing")
        return
    # a rule block
    order = {"rule": 0, "tests": 1, "deferred": 2, "unreachable": 2}
    last = -1
    for k in seen[1:]:
        if k == "rule":
            continue
        if order[k] < last or (order[k] == 2 and last == 2):
            b.problems.append(f"{where}: keywords out of order (a rule block is rule:, "
                              f"then tests:, then at most one deferred: or unreachable:)")
            break
        last = order[k]
    if not " ".join(b.texts("rule")).strip():
        b.problems.append(f"{where}: rule: says nothing")
    for text in b.texts("deferred"):
        if not re.match(r"#\d+\b", text):
            b.problems.append(f"{where}: deferred: needs an issue number (#123) first")
    for text in b.texts("unreachable"):
        if not text.strip():
            b.problems.append(f"{where}: unreachable: needs a reason")


def test_entries(b: Block) -> list[str]:
    out: list[str] = []
    for text in b.texts("tests"):
        out.extend(t.strip() for t in text.split(",") if t.strip())
    return out


# --------------------------------------------------------------------------
# The tests the blocks cite
# --------------------------------------------------------------------------

TEST_DEF_RX = re.compile(r"\bTEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
MACRO_DEF_RX = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)",
                          re.MULTILINE)


def strip_comments(text: str) -> str:
    """C++ text with every comment and literal blanked and every `#if 0`
    region dropped: a test that is commented out or compiled out defines
    nothing (a TEST name never sits inside a literal)."""
    code, _ = lex_cpp(text)
    kept: list[str] = []
    dead = 0
    for line in code.split("\n"):
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
    """Every Suite.name a test file defines, disabled ones included: TEST /
    TEST_F / TEST_P written out, plus the ones a file-local macro stamps out
    (#define SCENE(test_name, ...) whose body holds TEST_F(Suite, test_name),
    invoked as SCENE(name, ...))."""
    text = strip_comments(text)
    names = {f"{a}.{b}" for a, b in TEST_DEF_RX.findall(text)}
    for m in MACRO_DEF_RX.finditer(text):
        macro, params = m.group(1), [p.strip() for p in m.group(2).split(",")]
        for suite, pname in TEST_DEF_RX.findall(m.group(3)):
            if pname not in params:
                continue
            k = params.index(pname)
            for call in re.finditer(r"^[ \t]*" + re.escape(macro) + r"\s*\(([^)]*)\)", text,
                                    re.MULTILINE):
                args = [a.strip() for a in call.group(1).split(",")]
                if len(args) > k and re.fullmatch(r"\w+", args[k]):
                    names.add(f"{suite}.{args[k]}")
    return names


def is_disabled(test: str) -> bool:
    suite, _, name = test.partition(".")
    return suite.startswith("DISABLED_") or name.startswith("DISABLED_")


def all_tests(repo: str) -> set[str]:
    names: set[str] = set()
    root = os.path.join(repo, TESTS_DIR)
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for fn in sorted(filenames):
            if not fn.endswith((".cpp", ".cc", ".h", ".hpp", ".inc")):
                continue
            with open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            if "TEST" in text:
                names |= tests_defined(text)
    return names


def parity_row_ids(repo: str) -> set[str]:
    with open(os.path.join(repo, SCENARIO_TABLE), encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    return set(re.findall(r'^\s*\{\s*"(\w+)",\s*"[^"]*",', text, re.MULTILINE))


# --------------------------------------------------------------------------
# The check
# --------------------------------------------------------------------------

class CannotRun(Exception):
    pass


ERROR_KINDS = ("uncovered", "double-covered", "stray", "no tests", "missing test",
               "disabled test", "missing parity row", "malformed")


@dataclass
class Report:
    errors: dict[str, list[str]] = field(default_factory=dict)
    notices: list[str] = field(default_factory=list)
    counts: dict[str, int] = field(default_factory=dict)

    def add(self, kind: str, message: str) -> None:
        self.errors.setdefault(kind, []).append(f"{kind}: {message}")


def show(src: Source, i: int) -> str:
    return f"{src.path}:{i + 1}: {src.lines[i].strip()}"


NEUTRAL_CPP_RX = re.compile(r"[{}()\[\];,\s]*")
NEUTRAL_LUA_RX = re.compile(r"(end\b|[{}()\[\];,\s])*")


def paragraph(src: Source, t: int, block_starts: set[int]) -> list[int]:
    """The statements a block above line t covers: t itself and every statement
    after it that starts at t's indentation, until a blank line, a line
    indented less than t (a closing brace or `end` included), the first line
    of another block at t's indentation or less, or an arm of a statement
    outside the paragraph (the next `case` of an enclosing switch).  Lines
    indented deeper (bodies, continuations) and plain comments are stepped
    over; a block among them takes over the statements of its own
    paragraph, and the bodies are never the outer block's."""
    w = width(src.indent(t))
    stmts = [t]
    neutral = NEUTRAL_LUA_RX if src.lua else NEUTRAL_CPP_RX
    j = t + 1
    while j < src.n:
        if src.is_blank(j):
            break
        if j in block_starts and width(src.indent(j)) <= w:
            break
        if not src.has_code(j):
            j += 1
            continue
        code = src.code[j].strip()
        if not src.lua and code.startswith("#"):
            j += 1
            continue
        ind = width(src.indent(j))
        if ind < w:
            break
        if ind > w or neutral.fullmatch(code):
            j += 1
            continue
        role = src.role.get(j)
        if role is None:
            j += 1
            continue
        if role[1] in stmts:
            j += 1
            continue
        if role[0] == "head" and role[1] == j:
            stmts.append(j)
            j += 1
            continue
        break
    return stmts


def place_blocks(src: Source, blocks: list[Block], rep: Report) -> None:
    """Attach every block to what it covers; stray and misindented blocks are
    reported here."""
    carved: set[tuple[int, int]] = set()
    above: list[Block] = []
    for b in blocks:
        if b.trailing:
            s, k = src.unit_of(b.first)
            if k is None or not src.is_arm_start(src.arms[s][k][0]) or \
                    src.lead(src.arms[s][k][0]).replace("} ", "") not in ("else", "else if", "elseif"):
                rep.add("stray", f"{show(src, b.first)} -- a trailing block belongs on an "
                        f"`else` / `else if` / `elseif` arm; anything else takes its block "
                        f"on the lines above it")
                continue
            arm0 = src.arms[s][k][0]
            ctl = (src._lua_head(arm0) if src.lua else src._cpp_head(arm0))
            if b.first not in (ctl[0], ctl[-1]):
                rep.add("stray", f"{show(src, b.first)} -- a trailing block sits on the arm's "
                        f"first or last line, not inside its condition")
                continue
            arm_w = width(src.indent(src.arms[s][k][0]))
            for line, _ in b.entries[1:]:
                if width(src.indent(line)) <= arm_w:
                    b.problems.append(f"{src.path}:{line + 1}: a trailing block's further "
                                      f"lines are indented deeper than its arm")
                    break
            b.covers = list(src.arms[s][k])
            carved.add((s, k))
            continue
        nxt = b.last + 1
        if nxt >= src.n or src.is_blank(nxt):
            rep.add("stray", f"{src.path}:{b.first + 1}: the block is not attached to a "
                    f"statement (a blank line or the end of the file follows it)")
            continue
        if not src.has_code(nxt):
            rep.add("stray", f"{src.path}:{b.first + 1}: the block is not directly above a "
                    f"statement (another comment follows it at line {nxt + 1})")
            continue
        b.target = nxt
        ws = src.indent(nxt)
        for line, _ in b.entries:
            if src.indent(line) != ws:
                b.problems.append(f"{src.path}:{line + 1}: the block is not at the "
                                  f"indentation of the statement it covers (line {nxt + 1})")
                break
        role = src.role.get(nxt)
        if role and role[0] == "arm":
            s, k = role[1], role[2]
            if src.lead(nxt) in ("case", "default") and src.arms[s][k][0] == nxt:
                b.covers = list(src.arms[s][k])
                carved.add((s, k))
                continue
            rep.add("stray", f"{src.path}:{b.first + 1}: the block sits above an arm "
                    f"({show(src, nxt)}); an `else` arm takes its rule as a trailing "
                    f"comment on its own line")
            continue
        if role and role[1] != nxt:
            rep.add("stray", f"{src.path}:{b.first + 1}: the block sits inside a statement "
                    f"(line {nxt + 1} continues the one at line {role[1] + 1})")
            continue
        above.append(b)
    starts = {b.first for b in blocks if not b.trailing}
    for b in above:
        lines: list[int] = []
        for s in paragraph(src, b.target, starts):
            lines.extend(src.heads.get(s, [s]))
            for k, arm in enumerate(src.arms.get(s, [])):
                if (s, k) not in carved:
                    lines.extend(arm)
        b.covers = lines
    for b in blocks:
        if b.kind == "rule" and test_entries(b) and not b.trailing and b.target is not None:
            continue   # a rule that names its tests may sit above any statement
        if b.covers and not any(i in src.cands for i in b.covers):
            b.covers = []
            what = show(src, b.target) if b.target is not None else show(src, b.first)
            rep.add("stray", f"{src.path}:{b.first + 1}: the block covers no branch line "
                    f"(it sits above {what.split(': ', 1)[1]})")


def run_check(repo: str, cpp: tuple[str, ...] = SCOPE_CPP,
              lua_glob: str = SCOPE_LUA_GLOB) -> Report:
    rep = Report()
    if not os.path.isfile(os.path.join(repo, SCENARIO_TABLE)):
        raise CannotRun(f"{SCENARIO_TABLE} does not exist")
    files = in_scope_files(repo, cpp, lua_glob)
    for path in files:
        if not os.path.isfile(os.path.join(repo, path)):
            raise CannotRun(f"the in-scope file {path} does not exist (update SCOPE_CPP)")
    tests = all_tests(repo)
    parity = parity_row_ids(repo)
    c = {"files": len(files), "candidates": 0, "rules": 0, "plumbing": 0,
         "deferred": 0, "unreachable": 0}
    for path in files:
        with open(os.path.join(repo, path), encoding="utf-8", errors="replace",
                  newline="") as fh:
            src = Source(path, fh.read())
        c["candidates"] += len(src.cands)
        blocks, loose = parse_blocks(src)
        for msg in loose:
            rep.add("malformed", msg)
        place_blocks(src, blocks, rep)
        owners: dict[int, list[Block]] = {}
        for b in blocks:
            validate_grammar(src, b)
            for msg in b.problems:
                rep.add("malformed", msg)
            if b.kind == "plumbing":
                c["plumbing"] += 1
            else:
                c["rules"] += 1
            for i in b.covers:
                if i in src.cands:
                    owners.setdefault(i, []).append(b)
            if b.kind != "rule":
                continue
            entries = test_entries(b)
            deferred = b.texts("deferred")
            unreach = b.texts("unreachable")
            if not entries and not deferred and not unreach:
                rep.add("no tests", f"{src.path}:{b.first + 1}: the rule cites no test "
                        f"(add `tests:`, or `deferred: #<issue> ...` / `unreachable: ...`)")
            for t in entries:
                m = PARITY_RX.match(t)
                if m:
                    if m.group(1) not in parity:
                        rep.add("missing parity row", f"{src.path}:{b.first + 1}: {t} is not "
                                f"a row of {SCENARIO_TABLE}")
                    continue
                if not TEST_NAME_RX.match(t):
                    rep.add("malformed", f"{src.path}:{b.first + 1}: `{t}` is neither "
                            f"Suite.name nor parity:<row id>")
                    continue
                suite, _, name = t.partition(".")
                if t in tests and not is_disabled(t):
                    continue
                if t in tests or f"DISABLED_{suite}.{name}" in tests or \
                        f"{suite}.DISABLED_{name}" in tests:
                    rep.add("disabled test", f"{src.path}:{b.first + 1}: {t} is disabled")
                    continue
                rep.add("missing test", f"{src.path}:{b.first + 1}: no TEST defines {t}")
            for text in deferred:
                c["deferred"] += 1
                rep.notices.append(f"deferred: {src.path}:{b.first + 1}: {text}")
            for text in unreach:
                c["unreachable"] += 1
                rep.notices.append(f"unreachable: {src.path}:{b.first + 1}: {text}")
        for i in sorted(src.cands):
            got = owners.get(i, [])
            if not got:
                rep.add("uncovered", show(src, i))
            elif len(got) > 1:
                at = " and ".join(str(b.first + 1) for b in got)
                rep.add("double-covered", f"{show(src, i)} (the blocks at lines {at})")
    rep.counts = c
    return rep


def render(rep: Report) -> tuple[str, int]:
    out: list[str] = []
    for kind in ERROR_KINDS:
        out.extend(rep.errors.get(kind, []))
    out.extend(rep.notices)
    c = rep.counts
    n = {k: len(rep.errors.get(k, [])) for k in ERROR_KINDS}
    verdict = "FAIL" if any(n.values()) else "OK"
    out.append(
        f"check_semantics: {verdict}: {c['files']} files, {c['candidates']} candidate lines, "
        f"{c['rules']} rule comments, {c['plumbing']} plumbing comments | "
        + ", ".join(f"{k} {n[k]}" for k in ERROR_KINDS)
        + f" | deferred {c['deferred']}, unreachable {c['unreachable']}")
    return "\n".join(out) + "\n", (1 if verdict == "FAIL" else 0)


def main(argv: list[str] | None = None, cpp: tuple[str, ...] = SCOPE_CPP,
         lua_glob: str = SCOPE_LUA_GLOB) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--report", metavar="PATH", help="also write the full output to PATH")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    repo = os.path.abspath(args.repo)
    try:
        rep = run_check(repo, cpp, lua_glob)
    except CannotRun as exc:
        print(f"check_semantics: cannot run: {exc}")
        return 2
    text, rc = render(rep)
    sys.stdout.write(text)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as fh:
            fh.write(text)
    return rc


# --------------------------------------------------------------------------
# Self-test
# --------------------------------------------------------------------------

_ST_CPP = ("src/a.cpp",)
_ST_LUA = "packs/**/*.lua"

_ST_TESTS = """\
#include <gtest/gtest.h>
TEST(S, t) {}
TEST(S, u) {}
TEST_F(F, f) {}
TEST(S, DISABLED_off) {}
// TEST(S, commented) {}
#if 0
TEST(S, compiled_out) {}
#endif
#define SCENE(test_name, x) TEST_F(Scene, test_name) {}
SCENE(stamped, 1)
"""

_ST_TABLE = """\
static const Scenario kScenarios[] = {
    { "row_one", "scen99", nullptr },
};
"""


def _st_repo(root: str, files: dict[str, str]) -> None:
    base = {"src/a.cpp": "", "tests/unit/test_x.cpp": _ST_TESTS,
            "tests/parity/scenario_table.h": _ST_TABLE}
    base.update(files)
    for path, text in base.items():
        if text is None:
            continue
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)


def _st_run(files: dict[str, str], extra: list[str] | None = None) -> tuple[int, str]:
    root = tempfile.mkdtemp(prefix="check_semantics_")
    try:
        _st_repo(root, files)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--repo", root] + (extra or []), _ST_CPP, _ST_LUA)
        return rc, buf.getvalue().replace(root, "<repo>")
    finally:
        shutil.rmtree(root, ignore_errors=True)


def _sum(files: int = 1, cands: int = 0, rules: int = 0, plumbing: int = 0,
         deferred: int = 0, unreachable: int = 0, **errs: int) -> str:
    n = {k: errs.get(k.replace("-", "_").replace(" ", "_"), 0) for k in ERROR_KINDS}
    verdict = "FAIL" if any(n.values()) else "OK"
    return (f"check_semantics: {verdict}: {files} files, {cands} candidate lines, "
            f"{rules} rule comments, {plumbing} plumbing comments | "
            + ", ".join(f"{k} {n[k]}" for k in ERROR_KINDS)
            + f" | deferred {deferred}, unreachable {unreachable}\n")


# (name, files, rc, expected output)
_ST_CASES: list[tuple[str, dict[str, str], int, str]] = []


def _case(name: str, files: dict[str, str], rc: int, out: str) -> None:
    _ST_CASES.append((name, files, rc, out))


_case("if chain with } else if / } else, all covered", {"src/a.cpp": """\
void f(int a)
{
    // rule: A positive a doubles,
    //   a negative one halves.
    // tests: S.t, S.u
    // tests: parity:row_one
    if (a > 0) {
        g();
    } else if (a < 0) {
        h();
    } else {
        k();
    }
}
"""}, 0, _sum(cands=3, rules=1))

_case("Allman chain: a } line, then else, then {", {"src/a.cpp": """\
void f(int a)
{
    // plumbing: picks the branch for the log only.
    if (a)
    {
        g();
    }
    else
    {
        h();
    }
}
"""}, 0, _sum(cands=2, plumbing=1))

_case("a branch with no block is uncovered", {"src/a.cpp": """\
void f(int a)
{
    if (a)
        g();
}
"""}, 1, "uncovered: src/a.cpp:3: if (a)\n" + _sum(cands=1, uncovered=1))

_case("two trailing blocks on one arm double-cover it", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a) {
        g();
    } else if (b &&  // rule: first
        // tests: S.t
        b > 1) {  // rule: second
        // tests: S.u
        h();
    }
}
"""}, 1, "double-covered: src/a.cpp:7: } else if (b &&  // rule: first "
   "(the blocks at lines 7 and 9)\n" + _sum(cands=2, rules=3, double_covered=1))

_case("switch: the block covers its labels, a label's own block carves it out, "
      "a nested switch's labels are not the outer's", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: a picks the action
    // tests: S.t
    switch (a)
    {
    case 0:
        g();
        break;
    // rule: one has its own rule
    // tests: S.u
    case 1: {
        // plumbing: the inner switch only logs
        switch (b) {
        case 2:
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}
"""}, 0, _sum(cands=7, rules=2, plumbing=1))

_case("a loop covers its head only: the if in its body needs its own block", {"src/a.cpp": """\
void f(int n)
{
    // plumbing: walks the list.
    for (int i = 0; i < n; ++i)
    {
        if (i == 3)
            g();
    }
    // plumbing: drains.
    while (n--)
        g();
}
"""}, 1, "uncovered: src/a.cpp:6: if (i == 3)\n" + _sum(cands=3, plumbing=2, uncovered=1))

_case("continuation lines: a wrapped condition and a wrapped ternary", {"src/a.cpp": """\
int f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a > 0 &&
        (b ? a : b) > 2)
        return 1;
    // rule: q
    // tests: S.t
    const int x = a
        + (b > 0 ? b : -b);
    return x;
}
"""}, 0, _sum(cands=3, rules=2))

_case("a braceless body of one plain statement belongs to its head", {"src/a.cpp": """\
void f(int d)
{
    // rule: a non-zero d becomes its sign
    // tests: S.t
    if (d)
        d = (d > 0) ? 1 : -1;
}
"""}, 0, _sum(cands=2, rules=1))

_case("a braceless body that is itself a branch needs its own block", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a)
        if (b)
            g();
}
"""}, 1, "uncovered: src/a.cpp:6: if (b)\n" + _sum(cands=2, rules=1, uncovered=1))

_case("the trailing form covers its else-if arm, out of the chain's block", {"src/a.cpp": """\
void f(int a)
{
    // rule: r
    // tests: S.t
    if (a > 0) {
        g();
    } else if (a < 0) {  // rule: a negative a has its own rule,
        //   told here.
        // tests: S.u
        h();
    } else  // existing note // rule: and so has the last arm
        // deferred: #12 not pinned yet
        k();
}
"""}, 0, "deferred: src/a.cpp:11: #12 not pinned yet\n" + _sum(cands=3, rules=3, deferred=1))

_case("a trailing block anywhere but an else arm is stray", {"src/a.cpp": """\
void f(int a)
{
    if (a)  // rule: r
        // tests: S.t
        g();
}
"""}, 1, "uncovered: src/a.cpp:3: if (a)  // rule: r\n"
   "stray: src/a.cpp:3: if (a)  // rule: r -- a trailing block belongs on an `else` / "
   "`else if` / `elseif` arm; anything else takes its block on the lines above it\n"
   + _sum(cands=1, rules=1, uncovered=1, stray=1))

_case("a trailing block's further lines sit deeper than the arm", {"src/a.cpp": """\
void f(int a)
{
    // rule: r
    // tests: S.t
    if (a) {
        g();
    } else {  // rule: q
    // tests: S.t
        h();
    }
}
"""}, 1, "malformed: src/a.cpp:8: a trailing block's further lines are indented deeper "
   "than its arm\n" + _sum(cands=2, rules=2, malformed=1))

_case("a block separated from its statement by a blank line is not attached", {"src/a.cpp": """\
void f(int a)
{
    // rule: r
    // tests: S.t

    if (a)
        g();
}
"""}, 1, "uncovered: src/a.cpp:6: if (a)\n"
   "stray: src/a.cpp:3: the block is not attached to a statement (a blank line or the end "
   "of the file follows it)\n" + _sum(cands=1, rules=1, uncovered=1, stray=1))

_case("a block above a statement that is no branch is stray", {"src/a.cpp": """\
void f(int a)
{
    // plumbing: nothing to decide
    g(a);
}
"""}, 1, "stray: src/a.cpp:3: the block covers no branch line (it sits above g(a);)\n"
   + _sum(plumbing=1, stray=1))

_case("a block above an else arm, or inside a statement, or above another comment, "
      "is stray", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a) {
        g();
    // plumbing: misplaced
    } else {
        h();
    }
    // plumbing: p
    g(a,
      // plumbing: inside
      b ? 1 : 2);
    // plumbing: q
    /* a block comment */
    if (b)
        g();
    // plumbing: q
    // a plain line comment
    if (b)
        g();
}
"""}, 1, "uncovered: src/a.cpp:17: if (b)\n"
   "stray: src/a.cpp:7: the block sits above an arm (src/a.cpp:8: } else {); an `else` arm "
   "takes its rule as a trailing comment on its own line\n"
   "stray: src/a.cpp:13: the block sits inside a statement (line 14 continues the one at "
   "line 12)\n"
   "stray: src/a.cpp:15: the block is not directly above a statement (another comment "
   "follows it at line 16)\n"
   "malformed: src/a.cpp:20: a comment inside a block is neither a keyword line nor a "
   "continuation (indent continuation lines by two or more spaces after the marker)\n"
   + _sum(cands=5, rules=1, plumbing=5, uncovered=1, stray=3, malformed=1))

_case("strings, characters and comments are blanked before the grep", {"src/a.cpp": """\
void f()
{
    log("is it ? yes : no");  // if (x) ? a : b
    char c = '?'; int k = 1'000; const char* r = R"x(a ? b : c)x";
    /* if (a) ? b : c */
}
"""}, 0, _sum())

_case("a do block covers its closing } while", {"src/a.cpp": """\
void f(int n)
{
    // plumbing: counts down.
    do {
        --n;
    } while (n > 0);
}
"""}, 0, _sum(cands=2, plumbing=1))

_case("a #define covers the lines its backslashes continue", {"src/a.cpp": """\
// rule: the getter answers 1 for true
// tests: S.t
#define GET(N) \\
    int get_##N() { \\
        return N() ? 1 : 0; \\
    }
"""}, 0, _sum(cands=1, rules=1))

_case("cited tests: missing, disabled, commented out, compiled out, stamped by a macro, "
      "parity rows", {"src/a.cpp": """\
void f(int a)
{
    // rule: r
    // tests: S.t, F.f, Scene.stamped, parity:row_one, S.nope, S.off, S.DISABLED_off
    // tests: S.commented, S.compiled_out, parity:row_two, not-a-name
    if (a)
        g();
}
"""}, 1, "missing test: src/a.cpp:3: no TEST defines S.nope\n"
   "missing test: src/a.cpp:3: no TEST defines S.commented\n"
   "missing test: src/a.cpp:3: no TEST defines S.compiled_out\n"
   "disabled test: src/a.cpp:3: S.off is disabled\n"
   "disabled test: src/a.cpp:3: S.DISABLED_off is disabled\n"
   "missing parity row: src/a.cpp:3: parity:row_two is not a row of "
   "tests/parity/scenario_table.h\n"
   "malformed: src/a.cpp:3: `not-a-name` is neither Suite.name nor parity:<row id>\n"
   + _sum(cands=1, rules=1, missing_test=3, disabled_test=2, missing_parity_row=1,
          malformed=1))

_case("a rule with no tests is red; deferred and unreachable need their issue and reason",
      {"src/a.cpp": """\
void f(int a)
{
    // rule: no tests at all
    if (a)
        g();
    // rule: deferred, no issue
    // deferred: later
    if (a)
        g();
    // rule: deferred to an issue
    // deferred: #42 the old game differs
    if (a)
        g();
    // rule: unreachable, no reason
    // unreachable:
    if (a)
        g();
    // rule: unreachable with a reason
    // tests: S.t
    // unreachable: no caller passes null
    if (a)
        g();
}
"""}, 1, "no tests: src/a.cpp:3: the rule cites no test (add `tests:`, or "
   "`deferred: #<issue> ...` / `unreachable: ...`)\n"
   "malformed: src/a.cpp:6: deferred: needs an issue number (#123) first\n"
   "malformed: src/a.cpp:14: unreachable: needs a reason\n"
   "deferred: src/a.cpp:6: later\n"
   "deferred: src/a.cpp:10: #42 the old game differs\n"
   "unreachable: src/a.cpp:14: \n"
   "unreachable: src/a.cpp:18: no caller passes null\n"
   + _sum(cands=5, rules=5, deferred=2, unreachable=2, no_tests=1, malformed=2))

_case("the grammar: unknown keywords, order, plumbing alone, indentation, stray text, "
      "keyword lines outside a block", {"src/a.cpp": """\
void f(int a)
{
    // rule: r
    // test: S.t
    // tests: S.t
    if (a)
        g();
    // rule: r
    // deferred: #1 x
    // tests: S.t
    if (a)
        g();
    // plumbing: p
    // tests: S.t
    if (a)
        g();
    // rule: r
      // tests: S.t
    if (a)
        g();
    // rule: r
    // tests: S.t
    // and a plain sentence
    if (a)
        g();
    // tests: S.t
    g();
    // plumbing:
    if (a)
        g();
}
"""}, 1, "malformed: src/a.cpp:26: `tests:` outside a block (a block starts with `rule:` "
   "or `plumbing:`)\n"
   "malformed: src/a.cpp:4: unknown keyword `test:` (the grammar knows rule:, tests:, "
   "deferred:, unreachable:, plumbing:)\n"
   "malformed: src/a.cpp:8: keywords out of order (a rule block is rule:, then tests:, "
   "then at most one deferred: or unreachable:)\n"
   "malformed: src/a.cpp:13: a plumbing block stands alone (it has tests:)\n"
   "malformed: src/a.cpp:18: the block is not at the indentation of the statement it "
   "covers (line 19)\n"
   "malformed: src/a.cpp:23: a comment inside a block is neither a keyword line nor a "
   "continuation (indent continuation lines by two or more spaces after the marker)\n"
   "malformed: src/a.cpp:28: plumbing: says nothing\n"
   "deferred: src/a.cpp:8: #1 x\n"
   + _sum(cands=6, rules=4, plumbing=2, deferred=1, malformed=7))

_case("Lua: an if / elseif / else chain; and / or inside strings and comments are no "
      "branch", {"packs/p/a.lua": """\
local function f(a)
  local s = "this and that" .. [[ or this ]]  -- and a comment or two
  --[[ if a and b then ]]
  -- rule: a picks the action
  -- tests: S.t
  if a > 0 then
    g()
  elseif a < 0 then
    h()
  else
    k()
  end
end
"""}, 0, _sum(files=2, cands=3, rules=1))

_case("Lua: continuation lines, table fields, repeat / until, a function inside a call",
      {"packs/p/a.lua": """\
local function f(a, b)
  -- rule: r
  -- tests: S.t
  local x = a
    and b
  local t = {
    -- plumbing: p
    y = a or b,
    z = 1,
  }
  -- plumbing: p
  repeat
    a = a - 1
  until a < 0 or b
  -- plumbing: p
  og.each(t, function(v)
    if v then
      g()
    end
  end)
end
"""}, 1, "uncovered: packs/p/a.lua:17: if v then\n"
   "stray: packs/p/a.lua:15: the block covers no branch line (it sits above "
   "og.each(t, function(v))\n"
   + _sum(files=2, cands=5, rules=1, plumbing=3, uncovered=1, stray=1))

_case("Lua: the trailing form on elseif, after an existing comment", {"packs/p/a.lua": """\
local function f(a)
  -- rule: r
  -- tests: S.t
  if a > 0 then
    g()
  elseif a < 0 then  -- negative -- rule: q
    -- tests: S.u
    h()
  end
end
"""}, 0, _sum(files=2, cands=2, rules=2))


_case("a block covers its paragraph: the statement below it and its siblings at the same "
      "indentation", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a)
        g();
    // a plain comment does not end the paragraph
    int c = b ? 1 : 2;
    if (b)
        h();
}
"""}, 0, _sum(cands=3, rules=1))

_case("a blank line ends the paragraph", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a)
        g();

    if (b)
        h();
}
"""}, 1, "uncovered: src/a.cpp:8: if (b)\n" + _sum(cands=2, rules=1, uncovered=1))

_case("a dedent ends the paragraph", {"src/a.cpp": """\
void f(int a, int b)
{
    {
        // rule: r
        // tests: S.t
        if (a)
            g();
    }
    if (b)
        h();
}
"""}, 1, "uncovered: src/a.cpp:9: if (b)\n" + _sum(cands=2, rules=1, uncovered=1))

_case("a nested block takes over the statements of its own paragraph; the outer block's "
      "paragraph runs on past the body", {"src/a.cpp": """\
void f(int a, int b, int c)
{
    // rule: outer
    // tests: S.t
    if (a) {
        // rule: inner
        // tests: S.u
        if (b)
            g();
        if (c)
            h();
    } else if (c) {
        k();
    }
    while (a--)
        g();
}
"""}, 0, _sum(cands=5, rules=2))

_case("the next block at the same indentation ends the paragraph", {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a)
        g();
    // plumbing: p
    if (b)
        h();
    for (;;)
        g();
}
"""}, 0, _sum(cands=3, rules=1, plumbing=1))

_case("a rule that names its tests may sit above any statement; one without tests, or "
      "plumbing, may not", {"src/a.cpp": """\
bool f(int a, int b)
{
    // rule: a gate whose whole body is a comparison: both must hold
    // tests: S.t
    return a > 0 && b > 0;
}
bool g(int a, int b)
{
    // rule: no tests here
    return a > 0 && b > 0;
}
"""}, 1, "stray: src/a.cpp:9: the block covers no branch line (it sits above return a > 0 "
   "&& b > 0;)\n"
   "no tests: src/a.cpp:9: the rule cites no test (add `tests:`, or `deferred: #<issue> ...` "
   "/ `unreachable: ...`)\n" + _sum(rules=2, stray=1, no_tests=1))

_case("a ternary wrapped so its ? and : sit on different lines is a candidate",
      {"src/a.cpp": """\
int f(int a)
{
    // rule: a non-zero a picks one
    // tests: S.t
    const int x = a
        ? 1
        : 2;
    const int y = a
        ? 3
        : 4;
    return x + y;
}
"""}, 0, _sum(cands=2, rules=1))

_case("a wrapped ternary with no block is uncovered", {"src/a.cpp": """\
int f(int a)
{
    return a
        ? 1
        : 2;
}
"""}, 1, "uncovered: src/a.cpp:4: ? 1\n" + _sum(cands=1, uncovered=1))

_case("a trailing block sits on the arm's first or last line, not inside its condition",
      {"src/a.cpp": """\
void f(int a, int b)
{
    // rule: r
    // tests: S.t
    if (a) {
        g();
    } else if (b &&
               b > 1 &&  // rule: q
               // tests: S.u
               b < 9) {
        h();
    }
}
"""}, 1, "stray: src/a.cpp:8: b > 1 &&  // rule: q -- a trailing block sits on the arm's "
   "first or last line, not inside its condition\n" + _sum(cands=2, rules=2, stray=1))

def self_test() -> int:
    t0 = time.monotonic()
    failed = 0
    for name, files, rc_want, out_want in _ST_CASES:
        rc, out = _st_run(files)
        if rc != rc_want or out != out_want:
            failed += 1
            print(f"FAIL {name}: rc {rc} (want {rc_want})")
            print("--- got\n" + out + "--- want\n" + out_want + "---")
    # exit 2: the parity table or an in-scope file is missing
    rc, out = _st_run({"tests/parity/scenario_table.h": None})
    want = "check_semantics: cannot run: tests/parity/scenario_table.h does not exist\n"
    if rc != 2 or out != want:
        failed += 1
        print(f"FAIL exit 2 on a missing parity table: rc {rc}\n{out}")
    rc, out = _st_run({"src/a.cpp": None})
    want = ("check_semantics: cannot run: the in-scope file src/a.cpp does not exist "
            "(update SCOPE_CPP)\n")
    if rc != 2 or out != want:
        failed += 1
        print(f"FAIL exit 2 on a missing in-scope file: rc {rc}\n{out}")
    # --report writes what stdout shows
    root = tempfile.mkdtemp(prefix="check_semantics_")
    try:
        _st_repo(root, {"src/a.cpp": "void f(int a)\n{\n    if (a)\n        g();\n}\n"})
        report = os.path.join(root, "out.txt")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--repo", root, "--report", report], _ST_CPP, _ST_LUA)
        with open(report, encoding="utf-8") as fh:
            text = fh.read()
        if rc != 1 or text != buf.getvalue() or "uncovered: src/a.cpp:3" not in text:
            failed += 1
            print(f"FAIL --report: rc {rc}\n{text}")
    finally:
        shutil.rmtree(root, ignore_errors=True)
    total = len(_ST_CASES) + 3
    dt = time.monotonic() - t0
    if failed:
        print(f"check_semantics: self-test: FAIL: {failed} of {total} cases ({dt:.1f} s)")
        return 1
    print(f"check_semantics: self-test: OK: {total} cases ({dt:.1f} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
