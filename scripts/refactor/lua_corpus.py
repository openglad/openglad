#!/usr/bin/env python3
"""Shared Lua source model for the Stage-2 refactor tools.

Every Stage-2 tool (shim_audit.py, rewrite_*.py, strip_provenance.py) works
from the same three primitives defined here:

  * ``Source`` — a pack script with a per-character code/comment/string mask,
    so pattern matching never fires inside a comment or a string literal.
  * ``find_calls`` — balanced-paren call-site extraction with top-level
    argument splitting (handles multi-line calls).
  * ``Analyzer`` — a conservative abstract interpreter over the restricted
    expression grammar the corpus actually uses.  It answers, per expression:
    provably integer-valued?  provably Lua-integer subtype?  provable
    [lo, hi] bounds?  Anything it cannot prove is ``unknown`` — the audit
    then keeps the shim, which is always sound.

The one-statement-per-line lint (scripts/check_lua_statement_lines.py) is a
build gate on this corpus, and the rewriters lean on it: statement-shaped
patterns (guard trios, clamp ladders) are matched per physical line because
the lint guarantees one statement per line.  Calls may still WRAP across
lines (that is not a statement split), which is why call extraction is
balanced-paren rather than line-based.

Dry-run first: rewriters print a unified diff by default and only touch the
tree under --apply.  A rewrite batch that changes any pinned file must be
followed by the pin re-point + canary flip proof (see s2_partition.json for
the per-lane pin map).
"""

from __future__ import annotations

import argparse
import difflib
import pathlib
import re
import sys
from dataclasses import dataclass, field

REPO = pathlib.Path(__file__).resolve().parents[2]
CORPUS_DIR = REPO / "packs" / "core" / "scripts"
STUBS = REPO / "docs" / "modding" / "og-api.d.lua"

# ---------------------------------------------------------------------------
# Source + mask
# ---------------------------------------------------------------------------

CODE, COMMENT, STRING = "c", "#", "s"


class Source:
    """A Lua file plus a per-character mask (code / comment / string)."""

    def __init__(self, path: pathlib.Path, text: str | None = None):
        self.path = path
        self.text = path.read_text() if text is None else text
        self.mask = self._scan(self.text)
        self.lines = self.text.splitlines()
        # Offset of the first character of each line.
        self.line_start: list[int] = []
        off = 0
        for ln in self.lines:
            self.line_start.append(off)
            off += len(ln) + 1

    @staticmethod
    def _scan(text: str) -> str:
        """Classify every character as code, comment or string.

        Handles: -- line comments, --[[ ]] / --[==[ ]==] long comments,
        '...' / "..." strings with backslash escapes, [[...]] long strings.
        """
        n = len(text)
        mask = [CODE] * n
        i = 0
        while i < n:
            ch = text[i]
            if ch in "'\"":
                q = ch
                mask[i] = STRING
                i += 1
                while i < n and text[i] != q:
                    mask[i] = STRING
                    if text[i] == "\\" and i + 1 < n:
                        mask[i + 1] = STRING
                        i += 1
                    i += 1
                if i < n:
                    mask[i] = STRING
                    i += 1
                continue
            long_open = re.match(r"\[(=*)\[", text[i:])
            if ch == "[" and long_open:
                close = "]" + long_open.group(1) + "]"
                end = text.find(close, i)
                end = n if end == -1 else end + len(close)
                for j in range(i, end):
                    mask[j] = STRING
                i = end
                continue
            if ch == "-" and text[i : i + 2] == "--":
                rest = text[i + 2 :]
                long_c = re.match(r"\[(=*)\[", rest)
                if long_c:
                    close = "]" + long_c.group(1) + "]"
                    end = text.find(close, i + 2)
                    end = n if end == -1 else end + len(close)
                else:
                    end = text.find("\n", i)
                    end = n if end == -1 else end
                for j in range(i, end):
                    mask[j] = COMMENT
                i = end
                continue
            i += 1
        return "".join(mask)

    def code_text(self) -> str:
        """The file text with comments and strings blanked to spaces
        (newlines preserved), so offsets/line numbers survive."""
        return "".join(
            ch if (m == CODE or ch == "\n") else " "
            for ch, m in zip(self.text, self.mask)
        )

    def line_of(self, offset: int) -> int:
        """1-based line number of a character offset."""
        return self.text.count("\n", 0, offset) + 1

    def code_line(self, idx: int) -> str:
        """Line ``idx`` (0-based) with comments/strings blanked."""
        start = self.line_start[idx]
        end = start + len(self.lines[idx])
        return "".join(
            ch if m == CODE else " "
            for ch, m in zip(self.text[start:end], self.mask[start:end])
        )

    def is_comment_line(self, idx: int) -> bool:
        stripped = self.lines[idx].strip()
        return stripped.startswith("--")

    def is_blank_line(self, idx: int) -> bool:
        return not self.lines[idx].strip()

    def paren_depth_at_line_start(self, idx: int) -> int:
        """() [] {} nesting depth at the start of line ``idx`` — 0 for every
        statement-starting line; >0 means the line continues a wrapped call
        or table constructor."""
        depth = 0
        end = self.line_start[idx]
        for ch, m in zip(self.text[:end], self.mask[:end]):
            if m != CODE:
                continue
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
        return depth

    def statement_first_line(self, idx: int) -> int:
        """First physical line (0-based) of the statement containing line
        ``idx`` — walks back over call-wrap continuation lines."""
        while idx > 0 and self.paren_depth_at_line_start(idx) > 0:
            idx -= 1
        return idx


# ---------------------------------------------------------------------------
# Call-site extraction
# ---------------------------------------------------------------------------


@dataclass
class Call:
    name: str          # e.g. "og.fadd" or "stun_total" or ":s_set_frozen_delay"
    start: int         # offset of the first char of the name (not receiver)
    open_paren: int
    close_paren: int   # offset of the matching ')'
    args: list[tuple[int, int]]  # [start, end) offsets per argument
    receiver: str | None = None  # simple receiver name for ':' methods

    def arg_text(self, src: Source, i: int) -> str:
        s, e = self.args[i]
        return src.text[s:e].strip()

    def arg_code_text(self, src: Source, i: int) -> str:
        """Argument text with comments/strings blanked — what the analyzer
        should evaluate (a comment inside a wrapped call must not be parsed
        as subtraction)."""
        s, e = self.args[i]
        return src.code_text()[s:e].strip()

    def arg_has_comment(self, src: Source, i: int) -> bool:
        s, e = self.args[i]
        return "#" in src.mask[s:e]

    def full_text(self, src: Source) -> str:
        return src.text[self.start : self.close_paren + 1]


def find_calls(src: Source, name: str) -> list[Call]:
    """All call sites of ``name`` in code (never comments/strings).

    ``name`` forms:
      "og.fadd"              — dotted global call
      "stun_total"           — bare local-function call
      ":s_set_frozen_delay"  — method call; receiver captured when it is a
                               single identifier (the corpus never chains
                               receivers in front of the rewritten methods).
    """
    code = src.code_text()
    out: list[Call] = []
    if name.startswith(":"):
        pat = re.compile(r":\s*(" + re.escape(name[1:]) + r")\s*\(")
    else:
        pat = re.compile(
            r"(?<![\w.:])(" + re.escape(name).replace(r"\.", r"\s*\.\s*") + r")\s*\("
        )
    for m in pat.finditer(code):
        open_paren = m.end() - 1
        depth = 0
        close = -1
        for i in range(open_paren, len(code)):
            if code[i] == "(":
                depth += 1
            elif code[i] == ")":
                depth -= 1
                if depth == 0:
                    close = i
                    break
        if close == -1:
            continue
        args: list[tuple[int, int]] = []
        depth = 0
        arg_start = open_paren + 1
        for i in range(open_paren + 1, close + 1):
            ch = code[i]
            if ch in "([{":
                depth += 1
            elif ch in ")]}" and i != close:
                depth -= 1
            if (ch == "," and depth == 0) or i == close:
                seg = src.text[arg_start:i]
                if seg.strip():
                    args.append((arg_start, i))
                arg_start = i + 1
        receiver = None
        if name.startswith(":"):
            rm = re.match(r".*?([A-Za-z_][A-Za-z0-9_]*)\s*$", code[: m.start()], re.S)
            if rm:
                receiver = rm.group(1)
        out.append(Call(name, m.start(1) if not name.startswith(":") else m.start(),
                        open_paren, close, args, receiver))
    return out


# ---------------------------------------------------------------------------
# Stub-derived method return types (single source of truth: og-api.d.lua)
# ---------------------------------------------------------------------------


def load_stub_types() -> dict[str, str]:
    """method/function name -> declared return type ('integer', 'number', ...).

    Parsed from the generated EmmyLua stubs so the audit's integer/float
    knowledge auto-drifts with the bindings (the stub drift check keeps the
    stubs honest against the registration table).
    Only zero-extra-arg walker getters and og.* functions are useful to the
    analyzer; everything else can stay in the map harmlessly.
    """
    types: dict[str, str] = {}
    if not STUBS.exists():
        return types
    field_re = re.compile(
        r"^---@field\s+(\w+)\s+fun\((?:[^)]*)\)\s*:\s*([\w.\[\]|]+)"
    )
    for line in STUBS.read_text().splitlines():
        m = field_re.match(line)
        if m and m.group(1) not in types:
            types[m.group(1)] = m.group(2)
    return types


# ---------------------------------------------------------------------------
# Conservative expression analysis
# ---------------------------------------------------------------------------

BIG = 1 << 62


@dataclass
class Val:
    """What we can prove about an expression's value.

    int_valued  — provably integer-valued (a mathematical integer)
    int_subtype — provably Lua *integer* subtype (never a float 3.0)
    lo, hi      — provable inclusive bounds (None = unbounded that side)
    why         — human trace of the proof, for the manifest
    """

    int_valued: bool = False
    int_subtype: bool = False
    lo: int | None = None
    hi: int | None = None
    why: str = ""

    @staticmethod
    def unknown(why: str = "unprovable") -> "Val":
        return Val(False, False, None, None, why)

    def nonneg(self) -> bool:
        return self.lo is not None and self.lo >= 0

    def positive(self) -> bool:
        return self.lo is not None and self.lo >= 1

    def bounded_by(self, mag: int) -> bool:
        return (
            self.lo is not None
            and self.hi is not None
            and -mag < self.lo
            and self.hi < mag
        )


TOKEN = re.compile(
    r"\s*(?:(?P<num>\d+\.\d+|\.\d+|\d+)|(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"|(?P<op>==|~=|<=|>=|\.\.|[-+*/%#(),:.<>]))"
)


def tokenize(expr: str) -> list[tuple[str, str]] | None:
    toks: list[tuple[str, str]] = []
    i = 0
    while i < len(expr):
        if expr[i].isspace():
            i += 1
            continue
        m = TOKEN.match(expr, i)
        if not m:
            return None
        if m.group("num"):
            toks.append(("num", m.group("num")))
        elif m.group("name"):
            toks.append(("name", m.group("name")))
        else:
            toks.append(("op", m.group("op")))
        i = m.end()
    return toks


class Analyzer:
    """Interval/type analysis over the corpus' restricted expression forms.

    METHOD_RANGES entries are curated engine facts; each carries its
    justification.  A method absent from both the stub map and the curated
    map makes the whole expression unknown — which is always sound (the
    site keeps its shim).
    """

    # Curated value ranges for getters the corpus feeds into shims.
    # (lo, hi, justification).  Width-only entries (full type range) prove
    # magnitude for float-op exactness; sign-tightened entries additionally
    # unlock og.div/og.mod -> // and %.
    METHOD_RANGES: dict[str, tuple[int, int, str]] = {
        # statistics fields
        "s_level": (0, 32767, "int32 field, but engine levels are small "
                    "non-negative ints (descriptor data; guy::upgrade_to_level "
                    "takes short; difficulty scaling never writes negatives)"),
        "s_special_cost": (0, 65535, "unsigned short field"),
        "s_weapon_cost": (-32768, 32767, "short field (width only)"),
        "s_frozen_delay": (0, 32767, "masked getter never returns negatives"),
        "s_frozen_delay_raw": (-32768, 32767, "short field (thaw immunity "
                               "phase is negative by design)"),
        # guy stats
        "g_intelligence": (0, 32767, "guy stat short; build points >= 0"),
        "g_constitution": (0, 32767, "guy stat short; build points >= 0"),
        "g_strength": (0, 32767, "guy stat short; build points >= 0"),
        "g_dexterity": (0, 32767, "guy stat short; build points >= 0"),
        "g_exp": (0, (1 << 32) - 1, "uint32 field"),
        # walker fields
        "sizex": (0, 32767, "sprite dimension: positive descriptor data"),
        "sizey": (0, 32767, "sprite dimension: positive descriptor data"),
        "drawcycle": (0, 255, "unsigned char field"),
        "current_special": (-128, 127, "char field (width only)"),
        "lineofsight": (0, (1 << 31) - 1, "int32 sight range; engine and "
                        "core descriptors never write it negative"),
        "keys": (0, (1 << 32) - 1, "uint32 bitmask"),
        "xpos": (-32768, 32767, "short (width only — edge coords can go "
                 "negative, so NOT sign-curated)"),
        "ypos": (-32768, 32767, "short (width only)"),
        "charm_left": (-32768, 32767, "short (width only)"),
        "weapons_left": (-32768, 32767, "short (width only)"),
        "view_all": (-32768, 32767, "short (width only)"),
        "skip_exit": (-32768, 32767, "short (width only)"),
        "team_num": (0, 255, "unsigned char team index"),
    }

    # og.* helpers whose integer-ness the analyzer models.
    OG_INT_FUNCS = {
        "div", "mod", "trunc", "i8", "i16", "i32", "u8", "u16", "u32",
        "rand", "rand0", "soften", "my_team", "living_count", "entity_id",
        "scare_radius", "scare_duration", "freeze_duration", "charm_duration",
        "elemental_lifetime", "image_lifetime", "exp_from_action",
    }

    def __init__(self, stub_types: dict[str, str]):
        self.stub_types = stub_types
        # single-assignment local environment for the current file
        self.locals: dict[str, Val] = {}

    # -- local environment ---------------------------------------------------

    def load_locals(self, src: Source) -> None:
        """Single-assignment locals: ``local NAME = <expr>`` where NAME is
        declared exactly once in the file and never re-assigned.  Anything
        else is unknown.  (Same-named locals in different functions — the
        corpus' `generic`, `newob` — correctly fall out as unknown.)"""
        self.locals = {}
        code_lines = [src.code_line(i) for i in range(len(src.lines))]
        decl = re.compile(r"^\s*local\s+([A-Za-z_]\w*)\s*=\s*(.+?)\s*$")
        decls: dict[str, list[str]] = {}
        assigns: dict[str, int] = {}
        for cl in code_lines:
            m = decl.match(cl)
            if m and "," not in m.group(1):
                decls.setdefault(m.group(1), []).append(m.group(2))
            for am in re.finditer(r"(?<![\w.:])([A-Za-z_]\w*)\s*=[^=]", cl):
                if not re.match(r"^\s*local\b", cl):
                    assigns[am.group(1)] = assigns.get(am.group(1), 0) + 1
            for fm in re.finditer(r"\bfor\s+([A-Za-z_]\w*)", cl):
                assigns[fm.group(1)] = assigns.get(fm.group(1), 0) + 1
            for fn in re.finditer(r"\bfunction\s*\(([^)]*)\)|\blocal\s+function\s+\w+\s*\(([^)]*)\)", cl):
                params = (fn.group(1) or fn.group(2) or "")
                for p in params.split(","):
                    p = p.strip()
                    if p:
                        assigns[p] = assigns.get(p, 0) + 1
        for name_, exprs in decls.items():
            if len(exprs) == 1 and assigns.get(name_, 0) == 0:
                # two-pass so a local defined from an earlier local resolves
                self.locals[name_] = Val.unknown("pending")
        for _ in range(2):
            for name_, exprs in decls.items():
                if name_ in self.locals:
                    self.locals[name_] = self.eval(exprs[0])
                    self.locals[name_].why = (
                        f"local {name_} = {exprs[0]} [{self.locals[name_].why}]"
                    )

    # -- evaluation ----------------------------------------------------------

    def eval(self, expr: str) -> Val:
        toks = tokenize(expr)
        if toks is None:
            return Val.unknown("untokenizable")
        try:
            val, pos = self._sum(toks, 0)
        except _Bail as b:
            return Val.unknown(str(b))
        if pos != len(toks):
            return Val.unknown("trailing tokens")
        return val

    def _sum(self, t, i):
        val, i = self._prod(t, i)
        while i < len(t) and t[i] == ("op", "+") or i < len(t) and t[i] == ("op", "-"):
            op = t[i][1]
            rhs, i = self._prod(t, i + 1)
            val = self._arith(val, rhs, op)
        return val, i

    def _prod(self, t, i):
        val, i = self._unary(t, i)
        while i < len(t) and t[i][0] == "op" and t[i][1] in "*/%":
            op = t[i][1]
            rhs, i = self._unary(t, i + 1)
            if op == "*":
                val = self._arith(val, rhs, "*")
            else:
                # plain / or % inside an analyzed expression: give up
                val = Val.unknown("plain / or % in operand")
        return val, i

    def _unary(self, t, i):
        if i < len(t) and t[i] == ("op", "-"):
            v, i = self._unary(t, i + 1)
            lo = None if v.hi is None else -v.hi
            hi = None if v.lo is None else -v.lo
            return Val(v.int_valued, v.int_subtype, lo, hi, f"-({v.why})"), i
        if i < len(t) and t[i] == ("op", "#"):
            _, i = self._unary(t, i + 1)
            return Val(True, True, 0, BIG, "# length"), i
        return self._primary(t, i)

    def _primary(self, t, i):
        if i >= len(t):
            raise _Bail("truncated expression")
        kind, text = t[i]
        if kind == "num":
            if "." in text:
                f = float(text)
                if f.is_integer():
                    n = int(f)
                    return Val(True, False, n, n, f"float literal {text}"), i + 1
                return Val(False, False, None, None, f"non-integer literal {text}"), i + 1
            n = int(text)
            return Val(True, True, n, n, f"literal {n}"), i + 1
        if kind == "op" and text == "(":
            v, i = self._sum(t, i + 1)
            if i >= len(t) or t[i] != ("op", ")"):
                raise _Bail("unbalanced paren")
            return v, i + 1
        if kind == "name":
            return self._name(t, i)
        raise _Bail(f"unexpected token {text!r}")

    def _name(self, t, i):
        name = t[i][1]
        j = i + 1
        # dotted / method chains: a.b, a:b(...), a.b(...), chains thereof
        last_method = None
        base = name
        while j < len(t) and t[j][0] == "op" and t[j][1] in ".:":
            sep = t[j][1]
            if j + 1 >= len(t) or t[j + 1][0] != "name":
                raise _Bail("dangling . or :")
            member = t[j + 1][1]
            j += 2
            if j < len(t) and t[j] == ("op", "("):
                arg_start = j
                j = self._skip_args(t, j)
                last_method = member
                if base == "og" or base == "math":
                    return self._call_val(base, member,
                                          self._split_call_args(t, arg_start, j)), j
                base = f"{base}:{member}()"
            else:
                base = f"{base}.{member}"
                if name == "C":
                    return Val(True, True, None, None, f"C.{member} constant"), j
        if last_method is not None:
            return self._method_val(last_method), j
        if j < len(t) and t[j] == ("op", "("):
            # bare local function call — unknown
            j = self._skip_args(t, j)
            return Val.unknown(f"call {name}()"), j
        if name in self.locals:
            v = self.locals[name]
            return Val(v.int_valued, v.int_subtype, v.lo, v.hi, v.why), j
        return Val.unknown(f"name {name} not single-assignment"), j

    def _skip_args(self, t, j):
        depth = 0
        while j < len(t):
            if t[j] == ("op", "("):
                depth += 1
            elif t[j] == ("op", ")"):
                depth -= 1
                if depth == 0:
                    return j + 1
            j += 1
        raise _Bail("unbalanced call")

    def _split_call_args(self, t, open_idx, end_idx):
        """Token slices of each top-level argument of the call whose '(' is
        at ``open_idx`` and whose tokens end at ``end_idx`` (exclusive)."""
        args: list[list] = []
        depth = 0
        cur: list = []
        for k in range(open_idx, end_idx):
            tok = t[k]
            if tok == ("op", "("):
                depth += 1
                if depth == 1:
                    continue
            elif tok == ("op", ")"):
                depth -= 1
                if depth == 0:
                    if cur:
                        args.append(cur)
                    break
            if depth >= 1 and tok == ("op", ",") and depth == 1:
                args.append(cur)
                cur = []
                continue
            if depth >= 1:
                cur.append(tok)
        return args

    def _eval_tokens(self, toks) -> Val:
        try:
            v, pos = self._sum(toks, 0)
        except _Bail as b:
            return Val.unknown(str(b))
        if pos != len(toks):
            return Val.unknown("trailing tokens")
        return v

    def _method_val(self, method: str) -> Val:
        cur = self.METHOD_RANGES.get(method)
        stub = self.stub_types.get(method)
        if cur:
            lo, hi, why = cur
            if stub == "integer" or stub is None and False:
                pass
            if stub is not None and stub != "integer":
                # stub disagrees (float field) — trust the stub, drop range
                return Val(False, False, None, None,
                           f"{method}(): stub says {stub}")
            return Val(True, True, lo, hi, f"{method}(): {why}")
        if stub == "integer":
            return Val(True, True, None, None,
                       f"{method}(): integer per stubs, range uncurated")
        if stub is not None:
            return Val(False, False, None, None, f"{method}(): {stub} per stubs")
        return Val.unknown(f"{method}(): not in stubs")

    def _call_val(self, base: str, func: str, arg_toks: list | None = None) -> Val:
        argvals = [self._eval_tokens(a) for a in (arg_toks or [])]
        if base == "math" and func == "abs":
            hi = None
            if argvals and argvals[0].lo is not None and argvals[0].hi is not None:
                hi = max(abs(argvals[0].lo), abs(argvals[0].hi))
            iv = bool(argvals and argvals[0].int_valued)
            ist = bool(argvals and argvals[0].int_subtype)
            return Val(iv, ist, 0, hi, "math.abs >= 0")
        if base == "og":
            if func in ("rand", "rand0"):
                hi = None
                if argvals and argvals[0].hi is not None:
                    hi = max(argvals[0].hi - 1, 0)
                return Val(True, True, 0, hi, f"og.{func} in [0, n)")
            narrow = {
                "i8": (-128, 127), "u8": (0, 255),
                "i16": (-32768, 32767), "u16": (0, 65535),
                "i32": (-(1 << 31), (1 << 31) - 1), "u32": (0, (1 << 32) - 1),
            }
            if func in narrow:
                lo, hi = narrow[func]
                return Val(True, True, lo, hi, f"og.{func} range")
            if func in ("div", "mod") and len(argvals) == 2:
                a, b = argvals
                if a.nonneg() and b.positive():
                    if func == "div":
                        hi = None if a.hi is None or b.lo is None else a.hi // b.lo
                        return Val(True, True, 0, hi, "og.div nonneg/positive")
                    hi = None if b.hi is None else b.hi - 1
                    return Val(True, True, 0, hi, "og.mod nonneg/positive")
                return Val(True, True, None, None, f"og.{func} -> integer")
            if func == "soften":
                # soften(raw, knee, ceiling) <= ceiling; >= 0 for raw >= 0
                hi = argvals[2].hi if len(argvals) == 3 else None
                lo = 0 if (argvals and argvals[0].nonneg()) else None
                return Val(True, True, lo, hi, "og.soften")
            if func in self.OG_INT_FUNCS:
                return Val(True, True, None, None, f"og.{func} -> integer")
            stub = self.stub_types.get(func)
            if stub == "integer":
                return Val(True, True, None, None, f"og.{func}: integer per stubs")
            return Val.unknown(f"og.{func}: unmodeled")
        return Val.unknown(f"{base}.{func}: unmodeled")

    @staticmethod
    def _arith(a: Val, b: Val, op: str) -> Val:
        int_valued = a.int_valued and b.int_valued
        int_subtype = a.int_subtype and b.int_subtype
        lo = hi = None
        if a.lo is not None and a.hi is not None and b.lo is not None and b.hi is not None:
            if op == "+":
                lo, hi = a.lo + b.lo, a.hi + b.hi
            elif op == "-":
                lo, hi = a.lo - b.hi, a.hi - b.lo
            elif op == "*":
                prods = [a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi]
                lo, hi = min(prods), max(prods)
        why = f"({a.why}) {op} ({b.why})"
        return Val(int_valued, int_subtype, lo, hi, why)


class _Bail(Exception):
    pass


# ---------------------------------------------------------------------------
# Rewriter driver (shared CLI)
# ---------------------------------------------------------------------------


def corpus_files(names: list[str] | None) -> list[pathlib.Path]:
    files = sorted(CORPUS_DIR.glob("*.lua"))
    if names:
        wanted = {pathlib.Path(n).name for n in names}
        files = [f for f in files if f.name in wanted]
        missing = wanted - {f.name for f in files}
        if missing:
            sys.exit(f"error: no such corpus file(s): {', '.join(sorted(missing))}")
    return files


def run_rewriter(tool_name: str, description: str, transform) -> None:
    """Shared CLI: dry-run unified diff by default; --apply writes.

    ``transform(src: Source) -> tuple[list[str] | None, list[str]]`` returns
    (new_lines or None if unchanged, human notes).
    """
    ap = argparse.ArgumentParser(prog=tool_name, description=description)
    ap.add_argument("--apply", action="store_true",
                    help="write changes to the tree (default: print diff)")
    ap.add_argument("--files", nargs="*", metavar="NAME",
                    help="restrict to these corpus file names")
    ap.add_argument("--quiet", action="store_true", help="suppress notes")
    args = ap.parse_args()

    changed = 0
    total_notes: list[str] = []
    for path in corpus_files(args.files):
        src = Source(path)
        new_lines, notes = transform(src)
        for note in notes:
            total_notes.append(f"{path.name}: {note}")
        if new_lines is None:
            continue
        new_text = "\n".join(new_lines) + "\n"
        if new_text == src.text:
            continue
        changed += 1
        if args.apply:
            path.write_text(new_text)
        else:
            rel = path.relative_to(REPO)
            diff = difflib.unified_diff(
                src.text.splitlines(keepends=True),
                new_text.splitlines(True),
                fromfile=f"a/{rel}", tofile=f"b/{rel}",
            )
            sys.stdout.write("".join(diff))
    if not args.quiet:
        for note in total_notes:
            print(f"note: {note}", file=sys.stderr)
    mode = "applied to" if args.apply else "would change (dry-run)"
    print(f"{tool_name}: {changed} file(s) {mode}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Purity — expressions safe to duplicate/merge in rewrites
# ---------------------------------------------------------------------------

# Zero-arg getters that read sim state without drawing RNG or mutating.
# (attack/fire/special/death/animate/s_do_command and the known drawing
# calls — og.charm_duration etc. — are deliberately absent.)
PURE_GETTERS = {
    "xpos", "ypos", "sizex", "sizey", "stepsize", "s_level", "damage",
    "lineofsight", "skip_exit", "drawcycle", "lifetime", "g_intelligence",
    "g_constitution", "g_strength", "invisibility_left", "weapons_left",
    "s_hitpoints", "s_max_hitpoints", "busy", "current_special",
}

def is_pure_expr(expr: str) -> bool:
    """True when the expression only combines literals, plain names, C.*
    constants and known-pure zero-arg getters with + - * and grouping
    parens.  Anything else (unknown calls, og.rand, indexing, string
    content) is impure.  Sound to be wrong in the strict direction only.
    """
    expr = expr.strip()
    if not expr:
        return False
    # Blank out the allowed compound forms, then inspect what is left.
    getter_alt = "|".join(sorted(PURE_GETTERS))
    reduced = re.sub(r"[A-Za-z_]\w*\s*:\s*(?:%s)\(\)" % getter_alt, " 1 ", expr)
    reduced = re.sub(r"\bmath\.abs\(", " abs__(", reduced)
    reduced = re.sub(r"\bC\.\w+", " 1 ", reduced)
    # Any remaining name directly followed by '(' (or ':'/'.'/'[') is a call,
    # method, index or field we do not allow — except our abs__ marker.
    for m in re.finditer(r"([A-Za-z_]\w*)\s*([(\[.:])", reduced):
        if m.group(1) != "abs__":
            return False
    # abs__ itself must wrap a pure body; strip markers and re-check chars.
    reduced = reduced.replace("abs__(", "(")
    return re.fullmatch(r"[\w\s+\-*().]*", reduced) is not None and \
        reduced.count("(") == reduced.count(")")
