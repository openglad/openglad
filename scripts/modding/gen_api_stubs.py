#!/usr/bin/env python3
"""Generate docs/modding/og-api.d.lua from the C++ binding registration tables.

WHAT THIS IS
------------
The pack-Lua API surface is registered in exactly three C++ files:

  src/gameplay/script/bindings_entity.cpp   kWalkerMethods / kGuyMethods /
                                            kOgWorldFuncs (luaL_Reg tables)
                                            + kConstants (og.C.*)
                                            + kWalkerProperties (the
                                            property layer: plain fields,
                                            and value|method unions where
                                            a method shadows the name)
  src/gameplay/script/script_host.cpp       kOgFuncs (sandbox og.* arithmetic)
  src/gameplay/script/world_scripts.cpp     og.register_hooks and friends
                                            (lua_pushcfunction/lua_setfield
                                            pairs in install_vm_scaffolding)
                                            + the hook-name tables
  src/gameplay/script/family_decl.cpp       og.family / og.anims / og.pack

Those tables are the single source of truth. This script PARSES them — it
never carries its own list of functions — and emits EmmyLua (LuaCATS) type
stubs for lua-language-server. Parameter names and types are inferred from
the binding shims themselves: `luaL_checkinteger(L, 2)` is an integer arg 2,
`resolve_walker(L, 1, ...)` is a walker-handle arg 1, `lua_pushboolean`
before `return 1` is a boolean result, a `std::strcmp(s, "...")` chain over
a checked string arg is a string-literal union. Anything the inference
cannot classify is emitted as `any` with a TODO(stubgen) marker rather than
guessed.

THE OUTPUT FILE IS COMMENT-ONLY BY DESIGN
-----------------------------------------
docs/modding/og-api.d.lua lives under a shipped-Lua root, so
scripts/lua_inventory.py puts it in the coverage denominator (deliberately:
any .lua under packs/ or docs/ is measured, no exceptions — see that
module's header for why a narrower pattern is a hole). A stub file written
as `function og.rand(n) end` declarations would therefore mint hundreds of
function records nothing ever executes — an unfixable coverage red. So the
stubs are pure annotations: `---@field rand fun(n: integer): integer` lines
on `---@class` blocks, with a single executable statement (`og = {}`) that
binds the class to the global. The whole file's coverage grid is one
function (the main chunk) plus one line, and the loader test in
tests/unit/test_script_host.cpp runs the chunk and pins that shape.

APPEND CONTRACT FOR NEW BINDINGS
--------------------------------
The parser reads what already exists; keep new entries in the same shapes
and regeneration keeps working:

  * new walker/stats/guy accessors: the W_/S_/G_ GET/SET macro invocations;
  * new walker properties: a `{"name", getter, setter}` row in
    kWalkerProperties (setter nullptr = read-only);
  * new hand-written bindings: `int og_name(lua_State* L) { ... }` with
    `luaL_check*` / `resolve_walker` / `lua_push*` argument plumbing, and a
    `{"name", og_name},` line in the matching luaL_Reg table;
  * new constants: `{"NAME", VALUE},` in kConstants;
  * new world entry points: a `lua_pushcfunction(L, fn);` +
    `lua_setfield(L, -2, "name");` pair in install_vm_scaffolding();
  * new og.* VALUES (not functions) in install_vm_scaffolding(): the
    generator recognises exactly two shapes — a `push_og_nil(L);` sentinel
    and a `lua_newtable` … `push_frozen_view_of_top(L);` frozen table whose
    members are `lua_push<kind>(L, x); lua_setfield(L, -2, "k");` pairs —
    each followed by its own `lua_setfield(L, -2, "name");`. A value
    registration in any other shape is a hard error rather than a silent
    omission, because a field missing from the stub is a field LuaLS calls
    undefined in every pack that touches it;
  * new hooks: a `{"name", FamilyHook::X},` row in the order's hook table
    and a try_script_hook call site in a hooks:: wrapper.

After adding any of the above:  python3 scripts/modding/gen_api_stubs.py
Drift is caught by scripts/modding/check_api_stubs.py (the enforced
api_stub_check CMake target).

Output is deterministic: fields are sorted within each class, section order
is fixed, nothing depends on hashing or timestamps.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

BINDINGS = "src/gameplay/script/bindings_entity.cpp"
HOST = "src/gameplay/script/script_host.cpp"
WORLD = "src/gameplay/script/world_scripts.cpp"
FAMILY_DECL = "src/gameplay/script/family_decl.cpp"
OUTPUT = "docs/modding/og-api.d.lua"

TODO_ANY = "any"  # inference failure marker; paired with a TODO comment


# ---------------------------------------------------------------------------
# C++ source scanning
# ---------------------------------------------------------------------------

def read_source(repo_root: Path, rel: str) -> str:
    path = repo_root / rel
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"gen_api_stubs: cannot read {path}: {exc}")


def balanced_span(src: str, open_pos: int, open_ch: str, close_ch: str) -> int:
    """Index just past the delimiter matching src[open_pos]."""
    depth = 0
    for i in range(open_pos, len(src)):
        c = src[i]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i + 1
    raise SystemExit("gen_api_stubs: unbalanced delimiters in C++ source")


def balanced_body(src: str, open_brace: int) -> str:
    return src[open_brace + 1 : balanced_span(src, open_brace, "{", "}") - 1]


@dataclass
class CppFunction:
    name: str
    body: str
    comment: str  # first sentence of the preceding //-comment block


def leading_comment(src: str, def_start: int) -> str:
    """First sentence of the contiguous // block right above a definition."""
    lines: List[str] = []
    pos = def_start
    while pos > 0:
        line_start = src.rfind("\n", 0, pos - 1) + 1
        line = src[line_start : pos - 1] if pos > line_start else ""
        stripped = line.strip()
        if stripped.startswith("//"):
            lines.append(stripped.lstrip("/").strip())
            pos = line_start
            continue
        # `template <typename T>` sits between comment and definition.
        if stripped.startswith("template"):
            pos = line_start
            continue
        break
    if not lines:
        return ""
    lines.reverse()
    # Drop separator rules ("-----") and blank comment lines; keep the full
    # joined text — emission trims it per registered name (summarize).
    return " ".join(l for l in lines if l and not set(l) <= {"-"})


# A definition's first signature line: starts at column 0 with an
# identifier-ish return type and mentions a lua_State* L parameter. The body
# opens at the next top-level '{'; a ';' first means it was a declaration.
FN_SIG = re.compile(r"^[A-Za-z_][^\n;{}]*?\b(\w+)\(lua_State\* L", re.M)


def parse_cpp_functions(src: str) -> Dict[str, CppFunction]:
    out: Dict[str, CppFunction] = {}
    for m in FN_SIG.finditer(src):
        name = m.group(1)
        i = m.end()
        while i < len(src) and src[i] not in "{;":
            i += 1
        if i >= len(src) or src[i] == ";":
            continue  # declaration, not a definition
        body = balanced_body(src, i)
        out[name] = CppFunction(name, body, leading_comment(src, m.start()))
    return out


def parse_luareg_table(src: str, table: str) -> List[Tuple[str, str]]:
    """(lua_name, c_function) pairs of one luaL_Reg table, in table order."""
    m = re.search(r"const luaL_Reg " + table + r"\[\] = \{", src)
    if m is None:
        raise SystemExit(f"gen_api_stubs: table {table} not found")
    body = balanced_body(src, m.end() - 1)
    entries = re.findall(r'\{\s*"([^"]+)"\s*,\s*([\w:]+(?:<[^>]*>)?)\s*\}',
                         body)
    if not entries:
        raise SystemExit(f"gen_api_stubs: table {table} parsed empty")
    return entries


def parse_constants(src: str) -> List[str]:
    m = re.search(r"const NamedConst kConstants\[\] = \{", src)
    if m is None:
        raise SystemExit("gen_api_stubs: kConstants not found")
    body = balanced_body(src, m.end() - 1)
    names = re.findall(r'\{\s*"(\w+)"\s*,', body)
    if not names:
        raise SystemExit("gen_api_stubs: kConstants parsed empty")
    return names


def parse_walker_properties(src: str) -> List[Tuple[str, str, Optional[str]]]:
    """(name, getter_cname, setter_cname|None) rows from kWalkerProperties.

    The property layer routes `self.hp` / `self.busy = v` through the same
    lua_CFunctions the method table registers (bindings_entity.cpp keeps
    kWalkerProperties as the single source of truth), so the stub types are
    derived from those functions' already-inferred signatures rather than
    re-inferred here.
    """
    m = re.search(r"kWalkerProperties\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if m is None:
        raise SystemExit(
            "gen_api_stubs: kWalkerProperties[] not found in "
            f"{BINDINGS} (property layer moved; update the stub generator)")
    rows: List[Tuple[str, str, Optional[str]]] = []
    for name, getter, setter in re.findall(
            r'\{\s*"(\w+)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\}', m.group(1)):
        rows.append((name, getter, None if setter == "nullptr" else setter))
    if not rows:
        raise SystemExit(
            "gen_api_stubs: kWalkerProperties[] parsed empty "
            "(initializer shape changed; update the stub generator)")
    return rows


def parse_macro_accessors(src: str) -> Dict[str, "Signature"]:
    """Signatures for the W_/S_/G_ macro-generated accessors."""
    out: Dict[str, Signature] = {}
    pat = re.compile(
        r"^([WSG])_(GET|SET)_(INT|FLT|BOOL|REF)\((\w+)(?:,\s*[^)]+)?\)\s*$",
        re.M,
    )
    self_type = {"W": "og.Walker", "S": "og.Walker", "G": "og.Guy"}
    value_type = {
        "INT": "integer",
        "FLT": "number",
        "BOOL": "boolean",
        "REF": "og.Walker?",
    }
    for fam, kind, vtype, name in pat.findall(src):
        # The macros paste the prefix into the C symbol: m_ for walker
        # fields, s_ for stats, g_ for guy.
        prefix = {"W": "m_", "S": "s_", "G": "g_"}[fam]
        cname = prefix + ("set_" if kind == "SET" else "") + name
        sig = Signature(self_type=self_type[fam])
        if kind == "GET":
            sig.returns = [value_type[vtype]]
        else:
            # W_SET_BOOL runs the value through lua_toboolean (any truthy
            # value is legal); the rest luaL_check the exact type.
            ptype = "any" if vtype == "BOOL" else value_type[vtype]
            sig.params = [("value", ptype)]
        out[cname] = sig
    return out


# ---------------------------------------------------------------------------
# Signature inference from hand-written binding bodies
# ---------------------------------------------------------------------------

@dataclass
class Signature:
    self_type: Optional[str] = None  # method receiver ("og.Walker"/"og.Guy")
    params: List[Tuple[str, str]] = field(default_factory=list)
    returns: List[str] = field(default_factory=list)
    todo: bool = False  # inference gave up somewhere


# Extractor call → lua type; group 1 is the argument index. `weak` entries
# never override a type another extractor established for the same index.
ARG_EXTRACTORS: List[Tuple[re.Pattern, str, bool]] = [
    (re.compile(r"resolve_walker\(L,\s*(\d+),"), "og.Walker", False),
    (re.compile(r"resolve_walker_or_nil\(L,\s*(\d+)\)"), "og.Walker?", False),
    (re.compile(r"resolve_guy\(L,\s*(\d+),"), "og.Guy", False),
    (re.compile(r"guy_arg\(L,\s*(\d+)\)"), "og.Guy|og.Walker", False),
    (re.compile(r"living_arg\(L,\s*(\d+)\)"), "og.Walker", False),
    (re.compile(r"luaL_checkinteger\(L,\s*(\d+)\)"), "integer", False),
    (re.compile(r"luaL_optinteger\(L,\s*(\d+),"), "integer?", False),
    (re.compile(r"luaL_checknumber\(L,\s*(\d+)\)"), "number", False),
    (re.compile(r"luaL_checkstring\(L,\s*(\d+)\)"), "string", False),
    (re.compile(r"luaL_checklstring\(L,\s*(\d+),"), "string", False),
    (re.compile(r"luaL_optlstring\(L,\s*(\d+),"), "string?", False),
    (re.compile(r"luaL_checktype\(L,\s*(\d+),\s*LUA_TTABLE\)"), "table",
     False),
    (re.compile(r"luaL_checkudata\(L,\s*(\d+),\s*kWalkerMeta\)"), "og.Walker",
     False),
    (re.compile(r"luaL_checkudata\(L,\s*(\d+),\s*kGuyMeta\)"), "og.Guy",
     False),
    (re.compile(r"order_from_string\(L,\s*(\d+)\)"), "og.OrderName", False),
    (re.compile(r"list_from_selector\(L,\s*(\d+)\)"), "og.ListSelector",
     False),
    (re.compile(r"check_number_arg\(L,\s*(\d+)\)"), "number", False),
    (re.compile(r"luaL_checkany\(L,\s*(\d+)\)"), "any", True),
    (re.compile(r"lua_toboolean\(L,\s*(\d+)\)"), "any", True),
]

# Body markers that pin argument 1 as the method receiver.
SELF_MARKERS: List[Tuple[str, str]] = [
    ("self_arg(L)", "og.Walker"),
    ("stats_arg(L)", "og.Walker"),
    ("living_arg(L)", "og.Walker"),
    ("guy_arg(L, 1)", "og.Guy|og.Walker"),
]

PUSH_RESULTS: List[Tuple[re.Pattern, str]] = [
    (re.compile(r"lua_pushinteger\("), "integer"),
    (re.compile(r"lua_pushnumber\("), "number"),
    (re.compile(r"lua_pushboolean\("), "boolean"),
    (re.compile(r"lua_pushlstring\(|lua_pushliteral\(|lua_pushstring\(|"
                r"lua_pushfstring\("), "string"),
    (re.compile(r"push_walker_here\(|push_walker_handle\("), "og.Walker?"),
    (re.compile(r"push_walker_list\("), "og.Walker[]"),
    (re.compile(r"lua_pushnil\("), "nil"),
    (re.compile(r"lua_createtable\(|lua_newtable\("), "table"),
    # Returns one of its own arguments (og.max/min/clamp); resolved to the
    # common parameter type in a post-pass when all params agree.
    (re.compile(r"lua_pushvalue\("), TODO_ANY),
]

# Cosmetic fallback names for single-letter C++ locals, keyed by lua type.
NAME_BY_TYPE = {
    "og.Walker": "entity",
    "og.Walker?": "entity",
    "og.Guy": "guy",
    "og.Guy|og.Walker": "guy",
}


def statement_around(body: str, pos: int) -> str:
    start = body.rfind(";", 0, pos)
    start = 0 if start == -1 else start + 1
    end = body.find(";", pos)
    end = len(body) if end == -1 else end
    return body[start:end], start


def name_for_arg(body: str, match_start: int, ltype: str,
                 fn_name: str, idx: int, max_idx: int,
                 n_params: int) -> str:
    """Best-effort parameter name from the assignment the extractor feeds."""
    stmt, stmt_start = statement_around(body, match_start)
    rel = match_start - stmt_start
    m = re.search(r"\b(\w+)\s*=[^=]", stmt)
    name = None
    if m and m.end() <= rel + 1:
        # Accept only a direct `name = <casts...>(extractor...)` shape: the
        # text between '=' and the extractor must be casts/parens alone,
        # otherwise the extractor was buried in some larger call and the
        # assigned variable is not this argument's name.
        between = stmt[m.end() - 1 : rel]
        if re.fullmatch(r"(?:\s|\(|static_cast<[^>]*>|const_cast<[^>]*>)*",
                        between):
            name = m.group(1)
    if name in {"L", None}:
        name = None
    if name is not None and len(name) == 1 and ltype in NAME_BY_TYPE:
        return NAME_BY_TYPE[ltype]
    if name is not None:
        return name
    if "set_" in fn_name and idx == max_idx and n_params == 1:
        return "value"
    return f"arg{idx}"


def literal_union(body: str, var: str) -> Optional[str]:
    """A strcmp chain over `var` → sorted string-literal union type."""
    lits = re.findall(
        r"std::strcmp\(" + re.escape(var) + r",\s*\"([^\"]+)\"\)\s*==\s*0",
        body,
    )
    if len(lits) < 2:
        return None
    return "|".join(f'"{v}"' for v in sorted(set(lits)))


def infer_signature(fn: CppFunction) -> Signature:
    body = fn.body
    sig = Signature()

    for marker, stype in SELF_MARKERS:
        if marker in body:
            sig.self_type = stype
            break

    # Positional arguments.
    hits: Dict[int, Tuple[int, str, bool]] = {}  # idx → (pos, type, weak)
    for pat, ltype, weak in ARG_EXTRACTORS:
        for m in pat.finditer(body):
            idx = int(m.group(1))
            if idx < 1:
                continue
            if idx in hits and (weak or not hits[idx][2]):
                continue  # keep the existing strong hit
            hits[idx] = (m.start(), ltype, weak)
    optional = {int(m.group(1))
                for m in re.finditer(r"lua_isnoneornil\(L,\s*(\d+)\)", body)}
    find_order_vars = set(re.findall(r"find_order\((\w+)\)", body))

    start_idx = 2 if sig.self_type is not None else 1
    indices = sorted(i for i in hits if i >= start_idx)
    if indices != list(range(start_idx, start_idx + len(indices))):
        sig.todo = True  # a positional gap: something was not inferred
    max_idx = max(indices, default=0)
    for i in indices:
        pos, ltype, _weak = hits[i]
        name = name_for_arg(body, pos, ltype, fn.name, i, max_idx,
                            len(indices))
        if ltype == "string":
            union = literal_union(body, name)
            if union:
                ltype = union
            elif name in find_order_vars:
                ltype = "og.OrderName"
        if i in optional and not ltype.endswith("?"):
            ltype += "?"
        sig.params.append((name, ltype))
    # Vararg bindings (og.log) walk the stack with lua_gettop instead of
    # extracting fixed positions.
    if not sig.params and sig.self_type is None and "lua_gettop(L)" in body:
        sig.params.append(("...", "any"))

    # Results: the C return count is the number of Lua results; the pushes
    # give the types, in source order.
    ret_counts = [int(v) for v in re.findall(r"^\s*return (\d+);", body, re.M)]
    nresults = max(ret_counts, default=0)
    if nresults == 0:
        return sig
    pushes: List[Tuple[int, str]] = []
    for pat, ltype in PUSH_RESULTS:
        for m in pat.finditer(body):
            pushes.append((m.start(), ltype))
    pushes.sort()
    if not pushes:
        sig.returns = [TODO_ANY] * nresults
        sig.todo = True
        return sig
    rawseti_pos = [m.start() for m in re.finditer(r"lua_rawseti\(", body)]
    if nresults == 1:
        if rawseti_pos and any(t == "table" for _, t in pushes):
            # Array building: pushes consumed by a nearby rawseti are the
            # element type; anything else (early-bail pushnil) unions in.
            def consumed(pos: int) -> bool:
                return any(pos < r <= pos + 100 for r in rawseti_pos)

            elems = sorted({t for p, t in pushes
                            if consumed(p) and t not in ("table", "nil")})
            others = sorted({t for p, t in pushes
                             if not consumed(p) and t != "table"})
            elem = "|".join(e.rstrip("?") for e in elems) or TODO_ANY
            text = elem + "[]"
            if "nil" in others:
                text += "?"
                others = [t for t in others if t != "nil"]
            sig.returns = ["|".join([text] + others) if others else text]
            sig.todo = sig.todo or elem == TODO_ANY
        else:
            uniq = sorted({t for _, t in pushes})
            non_nil = [t for t in uniq if t != "nil"]
            if not non_nil:
                sig.returns = ["nil"]
            else:
                joined = "|".join(non_nil)
                if "nil" in uniq and not joined.endswith("?"):
                    joined += "?"
                sig.returns = [joined]
    else:
        if len(pushes) < nresults:
            sig.returns = [TODO_ANY] * nresults
            sig.todo = True
        else:
            sig.returns = [t for _, t in pushes[-nresults:]]
    # The returns-one-of-its-arguments idiom (lua_pushvalue in og.max/min/
    # clamp): when every parameter shares one type, the result has it too.
    if sig.returns and TODO_ANY in sig.returns:
        ptypes = {t for _, t in sig.params}
        if len(ptypes) == 1:
            only = next(iter(ptypes))
            sig.returns = [only if t == TODO_ANY else t for t in sig.returns]
        else:
            sig.todo = True
    return sig


# ---------------------------------------------------------------------------
# world_scripts.cpp: entry points, hook tables, hook signatures
# ---------------------------------------------------------------------------

def scaffolding_body(src: str) -> str:
    """The body of install_vm_scaffolding.

    The hand-registered half of the og table. It moved out of the
    WorldScripts constructor when the declaration VM started sharing the
    scaffolding: both VM kinds must offer the same og.*, so there is one
    place that builds it and this is that place.
    """
    m = re.search(r"void install_vm_scaffolding\(", src)
    if m is None:
        raise SystemExit("gen_api_stubs: install_vm_scaffolding not found")
    return balanced_body(src, src.index("{", m.end()))


def parse_world_entry_points(body: str) -> List[Tuple[str, str]]:
    """(lua_name, c_function) pairs registered in install_vm_scaffolding."""
    pairs = re.findall(
        r'lua_pushcfunction\(L,\s*(\w+)\);\s*'
        r'lua_setfield\(L,\s*-2,\s*"(\w+)"\);',
        body,
    )
    if not pairs:
        raise SystemExit(
            "gen_api_stubs: no install_vm_scaffolding og registrations found")
    return [(lua, c) for c, lua in pairs]


# og.* entries that are VALUES rather than functions. Two shapes exist and
# the generator knows both by sight; a third would be a silent hole in the
# stub, so it stops the run instead (see the append contract above).
SENTINEL_VALUE_RE = re.compile(
    r'push_og_nil\(L\);\s*lua_setfield\(L,\s*-2,\s*"(\w+)"\);')
FROZEN_TABLE_RE = re.compile(
    r'lua_newtable\(L\);\s*((?:\s*lua_push\w+\(L,[^;]*\);\s*'
    r'lua_setfield\(L,\s*-2,\s*"\w+"\);)+)\s*'
    r'push_frozen_view_of_top\(L\);\s*lua_setfield\(L,\s*-2,\s*"(\w+)"\);',
    re.S)
FROZEN_MEMBER_RE = re.compile(
    r'lua_push(\w+)\(L,[^;]*\);\s*lua_setfield\(L,\s*-2,\s*"(\w+)"\);')
PUSH_KIND_TYPES = {
    "integer": "integer",
    "number": "number",
    "string": "string",
    "boolean": "boolean",
}


@dataclass
class ValueEntry:
    """One og.<name> that holds a value: a sentinel, or a frozen table."""
    name: str
    cls: str
    members: List[Tuple[str, str]]  # (member, lua type); empty for sentinels


def parse_world_value_entry_points(body: str) -> List[ValueEntry]:
    """og.* value registrations, and proof that none was missed.

    Names come from the source, not from a list here. Anything registered
    on the og table that this cannot classify stops the generator: a field
    absent from the stub is a field LuaLS calls undefined in every pack that
    spells it, which is a worse failure than a noisy one here.
    """
    out: List[ValueEntry] = []
    consumed = set()
    for m in SENTINEL_VALUE_RE.finditer(body):
        out.append(ValueEntry(m.group(1), "og." + m.group(1).capitalize(), []))
        consumed.add(m.group(1))
    for m in FROZEN_TABLE_RE.finditer(body):
        members = []
        for kind, member in FROZEN_MEMBER_RE.findall(m.group(1)):
            lua_type = PUSH_KIND_TYPES.get(kind)
            if lua_type is None:
                raise SystemExit(
                    f"gen_api_stubs: og.{m.group(2)}.{member} pushed with an "
                    f"unhandled lua_push{kind}")
            members.append((member, lua_type))
            consumed.add(member)
        out.append(ValueEntry(m.group(2), "og." + m.group(2).capitalize(),
                              members))
        consumed.add(m.group(2))

    functions = {lua for lua, _c in parse_world_entry_points(body)}
    every = {m.group(1)
             for m in re.finditer(r'lua_setfield\(L,\s*-2,\s*"(\w+)"\);',
                                  body)}
    unclassified = sorted(every - functions - consumed)
    if unclassified:
        raise SystemExit(
            "gen_api_stubs: unrecognised og registration(s) in "
            "install_vm_scaffolding: " + ", ".join(unclassified) +
            " (see the APPEND CONTRACT in this file's docstring)")
    return out


def parse_hook_tables(src: str) -> Dict[str, List[Tuple[str, str]]]:
    """kLivingHooks-style tables: table name → [(hook_name, enum_name)]."""
    out: Dict[str, List[Tuple[str, str]]] = {}
    for m in re.finditer(r"constexpr HookName (k\w+Hooks)\[\] = \{", src):
        body = balanced_body(src, m.end() - 1)
        out[m.group(1)] = re.findall(
            r'\{\s*"(\w+)"\s*,\s*FamilyHook::(\w+)\s*\}', body
        )
    if not out:
        raise SystemExit("gen_api_stubs: no HookName tables found")
    return out


def parse_orders(src: str) -> List[Tuple[str, str, str]]:
    """kOrders rows: (order_name, order_enum, hook_table_name)."""
    m = re.search(r"constexpr OrderInfo kOrders\[\] = \{", src)
    if m is None:
        raise SystemExit("gen_api_stubs: kOrders not found")
    body = balanced_body(src, m.end() - 1)
    rows = re.findall(
        r'\{\s*"(\w+)"\s*,\s*Order::(\w+)\s*,\s*(k\w+Hooks)\s*,', body
    )
    if not rows:
        raise SystemExit("gen_api_stubs: kOrders parsed empty")
    return rows


def parse_level_hook_names(src: str) -> List[str]:
    m = re.search(r"constexpr LevelHookName kLevelHookNames\[\] = \{", src)
    if m is None:
        raise SystemExit("gen_api_stubs: kLevelHookNames not found")
    body = balanced_body(src, m.end() - 1)
    return re.findall(r'\{\s*"(\w+)"\s*,\s*LevelHook::\w+\s*\}', body)


CPP_PARAM_TYPES = {
    "walker": "og.Walker",
    "living": "og.Walker",
    "weap": "og.Walker",
    "effect": "og.Walker",
    "treasure": "og.Walker",
    "guy": "og.Guy",
    "std::int32_t": "integer",
    "std::uint32_t": "integer",
    "short": "integer",
    "int": "integer",
    "lua_Integer": "integer",
}


def split_top_level(text: str) -> List[str]:
    parts: List[str] = []
    depth = 0
    cur: List[str] = []
    for c in text:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    if cur:
        parts.append("".join(cur))
    return parts


def hook_arg_of(expr: str, ptypes: Dict[str, str]) -> Tuple[str, str]:
    """One try_script_hook argument expression → (lua name, lua type)."""
    inner = expr.strip()
    cast_type = None
    m = re.match(r"static_cast<([\w:*<>\s]+?)\s*>\((.*)\)$", inner, re.S)
    if m:
        cast_type = m.group(1).replace(" ", "")
        inner = m.group(2).strip()
    if inner == "stats->controller()":
        # hit_response: the Lua self is the stats' owner.
        return ("self", "og.Walker")
    if cast_type == "lua_Integer":
        return (inner if inner.isidentifier() else "value", "integer")
    if inner.isidentifier():
        cpp = ptypes.get(inner, "")
        lua = CPP_PARAM_TYPES.get(cpp)
        return (inner, lua or TODO_ANY)
    return ("value", TODO_ANY)


def parse_family_hook_signatures(
    src: str,
) -> Dict[str, Tuple[List[Tuple[str, str]], bool]]:
    """FamilyHook enum name → (hook args, wants_result).

    Read from the try_script_hook call sites inside the hooks:: wrappers;
    argument types come from the enclosing wrapper's C++ parameter list.
    """
    out: Dict[str, Tuple[List[Tuple[str, str]], bool]] = {}
    for m in re.finditer(
        r"^(?:std::optional<bool>|bool|void)\s+\w+\(([^)]*)\)\s*\n\{",
        src,
        re.M,
    ):
        params_text = m.group(1)
        body = balanced_body(src, src.index("{", m.end() - 1))
        ptypes: Dict[str, str] = {}
        for ptype, pname in re.findall(
            r"(?:const\s+)?([\w:]+)\s*[*&]?\s*(\w+)\s*(?:,|$)", params_text
        ):
            ptypes[pname] = ptype
        search = 0
        while True:
            call = body.find("try_script_hook(", search)
            if call == -1:
                break
            open_paren = call + len("try_script_hook")
            end = balanced_span(body, open_paren, "(", ")")
            search = end
            inner = body[open_paren + 1 : end - 1]
            parts = [p.strip() for p in split_top_level(inner)]
            if len(parts) < 4:
                continue
            em = re.match(r"FamilyHook::(\w+)", parts[2])
            if em is None:
                continue
            wants_result = parts[3] == "true"
            hook_args = [hook_arg_of(p, ptypes) for p in parts[4:]]
            out[em.group(1)] = (hook_args, wants_result)
    if not out:
        raise SystemExit("gen_api_stubs: no try_script_hook sites found")
    # do_special dispatches through its own funnel — the specials-table
    # dispatcher try_script_do_special — not the generic try_script_hook
    # template, so its signature is read from that site. Fail loudly if the
    # dispatch pattern ever moves again.
    m = re.search(
        r"std::optional<bool>\s+try_script_do_special\(([^)]*)\)\s*\n\{",
        src,
    )
    if m is None:
        raise SystemExit(
            "gen_api_stubs: try_script_do_special not found (the do_special "
            "dispatch moved; update the stub generator)")
    params_text = m.group(1)
    body = balanced_body(src, src.index("{", m.end() - 1))
    ptypes = {}
    for ptype, pname in re.findall(
        r"(?:const\s+)?([\w:]+)\s*[*&]?\s*(\w+)\s*(?:,|$)", params_text
    ):
        ptypes[pname] = ptype
    arg_names = re.findall(r"f\.arg\((\w+)\);", body)
    call = re.search(
        r"f\.call\(hook_where\(FamilyHook::DoSpecial\),\s*(true|false)\)",
        body,
    )
    if not arg_names or call is None:
        raise SystemExit(
            "gen_api_stubs: try_script_do_special dispatch pattern not "
            "recognized (update the stub generator)")
    out["DoSpecial"] = (
        [hook_arg_of(n, ptypes) for n in arg_names],
        call.group(1) == "true",
    )
    return out


def parse_level_dispatch_signatures(
    src: str,
) -> Dict[str, List[Tuple[str, str]]]:
    """protected_call label → pushed-argument (name, type) list.

    Reads the level_* dispatchers: every lua push between the frame's
    push_dispatch_gen and its protected_call is a hook argument.
    """
    out: Dict[str, List[Tuple[str, str]]] = {}
    for call in re.finditer(
        r'protected_call\("((?:level|entity):\w+)",\s*(\d+),', src
    ):
        label = call.group(1)
        nargs = int(call.group(2))
        window_start = src.rfind("push_dispatch_gen", 0, call.start())
        window = src[window_start : call.start()]
        pushes: List[Tuple[int, str, str]] = []
        for m in re.finditer(r"lua_pushinteger\([\w.]+,\s*([^;]*)\);", window):
            expr = m.group(1)
            name = "level" if "->id" in expr else (
                "tick" if "tick_count" in expr else "value")
            pushes.append((m.start(), name, "integer"))
        for m in re.finditer(r"push_walker_handle\([\w.]+,", window):
            pushes.append((m.start(), "entity", "og.Walker"))
        pushes.sort()
        args = [(n, t) for _, n, t in pushes]
        if len(args) != nargs:
            args = [(f"arg{i + 1}", TODO_ANY) for i in range(nargs)]
        out[label] = args
    return out


def parse_alias(functions: Dict[str, CppFunction], fn_name: str) -> str:
    """Sorted literal union out of a shared helper's strcmp chain."""
    fn = functions.get(fn_name)
    if fn is None:
        raise SystemExit(f"gen_api_stubs: helper {fn_name} not found")
    lits = re.findall(r'std::strcmp\(s,\s*"([^"]+)"\)\s*==\s*0', fn.body)
    if not lits:
        raise SystemExit(f"gen_api_stubs: no strcmp chain in {fn_name}")
    return "|".join(f'"{v}"' for v in sorted(set(lits)))


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------

def summarize(comment: str, lua_name: str) -> str:
    """One-line description for a @field, trimmed from the C++ comment.

    When the block documents several bindings (adjacent comment paragraphs
    with no blank line), anchor on the sentence that names this one —
    "og.family_flag(" — so a shared block cannot mislabel its neighbours.
    """
    if not comment:
        return ""
    text = comment
    for anchor in (f"og.{lua_name}(", f"walker:{lua_name}(",
                   f"ob:{lua_name}(", f"self:{lua_name}("):
        at = text.find(anchor)
        if at != -1:
            text = text[at:]
            break
    period = re.search(r"(?<!\.)\. ", text)
    if period is not None:
        text = text[: period.start() + 1]
    text = text.strip()
    if len(text) > 160:
        text = text[:157].rstrip() + "..."
    return text


def comment_param_names(comment: str, lua_name: str) -> List[str]:
    """Parameter names a doc comment declares: "og.clamp(v, lo, hi)"."""
    if not comment:
        return []
    text = comment.replace("[", "").replace("]", "")
    m = re.search(
        r"(?:og\.|walker:|ob:|self:)" + re.escape(lua_name)
        + r"\(\s*(\w+(?:\s*,\s*\w+)*)\s*\)",
        text,
    )
    if m is None:
        return []
    return [n.strip() for n in m.group(1).split(",")]


def apply_comment_names(sig: Signature, comment: str, lua_name: str,
                        skip_first: bool) -> None:
    """Fill fallback argN names from the binding's own doc comment.

    Only fallback names are replaced — a real C++ local always wins — and
    only when the comment's arity matches, so a stale comment cannot
    mislabel positions.
    """
    names = comment_param_names(comment, lua_name)
    if skip_first and names:
        names = names[1:]
    if len(names) != len(sig.params):
        return
    sig.params = [
        (names[i], t) if re.fullmatch(r"arg\d+|value", n) else (n, t)
        for i, (n, t) in enumerate(sig.params)
    ]


def fun_type(sig: Signature, self_type: Optional[str]) -> str:
    parts = []
    if self_type is not None:
        parts.append(f"self: {self_type}")
    for name, ltype in sig.params:
        parts.append(f"{name}: {ltype}")
    text = "fun(" + ", ".join(parts) + ")"
    if sig.returns:
        text += ": " + ", ".join(sig.returns)
    return text


def field_line(name: str, type_text: str, desc: str, todo: bool,
               optional: bool = False) -> str:
    line = f"---@field {name}{'?' if optional else ''} {type_text}"
    notes = []
    if todo:
        notes.append("TODO(stubgen): signature not fully inferred")
    if desc:
        notes.append(desc)
    if notes:
        line += " # " + " — ".join(notes)
    return line


def method_fields(entries: List[Tuple[str, str]],
                  functions: Dict[str, CppFunction],
                  macro_sigs: Dict[str, Signature],
                  self_type: str,
                  writable_props: Optional[Dict[str, str]] = None) -> List[str]:
    lines = []
    writable_props = writable_props or {}
    for lua_name, c_name in sorted(set(entries)):
        c_base = re.sub(r"<.*>$", "", c_name)
        if c_base in macro_sigs:
            sig = macro_sigs[c_base]
            desc = ""
        elif c_base in functions:
            sig = infer_signature(functions[c_base])
            comment = functions[c_base].comment
            # Method comments name the receiver implicitly ("walker:f(a)"),
            # so the declared names map onto the non-self parameters.
            apply_comment_names(sig, comment, lua_name, skip_first=False)
            desc = summarize(comment, lua_name)
        else:
            sig = Signature(todo=True)
            desc = ""
        # The receiver type comes from the TABLE the name is registered in
        # (guy_arg accepts either handle, but on og.Guy the receiver is a
        # guy record).
        type_text = fun_type(sig, self_type)
        if lua_name in writable_props and len(sig.returns) == 1:
            # A method name shadowing a WRITABLE walker property
            # (kWalkerProperties): reads stay the method (method-first
            # __index contract), but `self.name = v` is a legal
            # write-through, so the declared type is the union of the two.
            type_text = f"{sig.returns[0]}|{type_text}"
            note = (f"write-through property: `self.{lua_name} = v` runs "
                    f"{writable_props[lua_name]}; reads answer the method "
                    "(method-first)")
            desc = f"{note} — {desc}" if desc else note
        lines.append(field_line(lua_name, type_text, desc, sig.todo))
    return sorted(lines)


def property_fields(props: List[Tuple[str, str, Optional[str]]],
                    method_names: set,
                    functions: Dict[str, CppFunction],
                    macro_sigs: Dict[str, Signature]) -> List[str]:
    """Plain value fields for walker properties NO method name shadows.

    These are the names that read as values (`self.hp`); the shadowed ones
    are typed as unions on their method field by method_fields. The value
    type comes from the paired getter's signature — the same C function the
    method table registers, so the types cannot drift apart.
    """
    lines = []
    for name, getter, setter in props:
        if name in method_names:
            continue
        c_base = re.sub(r"<.*>$", "", getter)
        if c_base in macro_sigs:
            sig = macro_sigs[c_base]
        elif c_base in functions:
            sig = infer_signature(functions[c_base])
        else:
            sig = Signature(todo=True)
        vtype = sig.returns[0] if len(sig.returns) == 1 else TODO_ANY
        mode = "read/write" if setter else "read-only"
        lines.append(field_line(
            name, vtype,
            f"{mode} property over {getter}"
            + (f"/{setter} (write-through, same narrowing)" if setter else ""),
            sig.todo or vtype == TODO_ANY))
    return sorted(lines)


def og_function_fields(entries: List[Tuple[str, str]],
                       functions: Dict[str, CppFunction],
                       overrides: Dict[str, str],
                       return_overrides: Optional[Dict[str, str]] = None
                       ) -> List[str]:
    lines = []
    return_overrides = return_overrides or {}
    for lua_name, c_name in sorted(set(entries)):
        c_base = re.sub(r"<.*>$", "", c_name)
        if c_base in functions:
            sig = infer_signature(functions[c_base])
            comment = functions[c_base].comment
        else:
            sig = Signature(todo=True)
            comment = ""
        # og.* functions are free functions: a walker extracted at index 1
        # is an ordinary first parameter, not a method receiver.
        if sig.self_type is not None:
            sig.params.insert(0, ("self", sig.self_type))
            sig.self_type = None
        apply_comment_names(sig, comment, lua_name, skip_first=False)
        if lua_name in return_overrides:
            # A return shape the body-walking inference cannot see (the
            # og.tuning table is built from typed TuningValue pushes,
            # which read as a scalar union): declared here, kept in ONE
            # place so a real signature change still regenerates cleanly.
            sig.returns = [return_overrides[lua_name]]
            sig.todo = False
        if lua_name in overrides:
            # Hook-table parameters carry class types the C++ body cannot
            # express; patch the luaL_checktype(TABLE) param.
            sig.params = [
                ("hooks" if re.fullmatch(r"arg\d+|value", n) else n,
                 overrides[lua_name]) if t == "table" else (n, t)
                for n, t in sig.params
            ]
        lines.append(field_line(lua_name, fun_type(sig, None),
                                summarize(comment, lua_name), sig.todo))
    return lines


HEADER = """---@meta
-- docs/modding/og-api.d.lua — GENERATED FILE, do not edit by hand.
--
-- EmmyLua (LuaCATS) type stubs for the OpenGlad pack-scripting API, for
-- lua-language-server and any editor speaking LSP. Parsed from the C++
-- registration tables (the single source of truth) by
-- scripts/modding/gen_api_stubs.py:
--
--   src/gameplay/script/bindings_entity.cpp
--   src/gameplay/script/script_host.cpp
--   src/gameplay/script/world_scripts.cpp
--
-- Regenerate after any binding change:
--
--   python3 scripts/modding/gen_api_stubs.py
--
-- Drift is caught by scripts/modding/check_api_stubs.py (the enforced
-- api_stub_check CMake target).
--
-- This file is annotation-only apart from the final `og = {}` line. That
-- is load-bearing, not style: it lives under a shipped-Lua root, so it is
-- in the coverage denominator (scripts/lua_inventory.py), and comment-only
-- stubs keep its coverage grid at exactly one function (the main chunk),
-- which the loader test in tests/unit/test_script_host.cpp executes and
-- pins. Never add `function ... end` stubs here — every one would be a
-- permanently uncovered function record.
--
-- Semantics the type system cannot carry (see docs/modding/api-reference.md
-- and docs/lua-classpacks-design.md §3): handles are dispatch-scoped
-- (stashing one across dispatches is a script error on next use); og.rand
-- errors on n <= 0; every integer division goes through og.div/og.mod; one
-- og.f* call per float operation; no pairs in the sandbox."""


def generate(repo_root: Path) -> str:
    bindings_src = read_source(repo_root, BINDINGS)
    host_src = read_source(repo_root, HOST)
    world_src = read_source(repo_root, WORLD)
    # og.family / og.anims / og.pack are registered beside the rest of the
    # og table but DEFINED next to the declaration pass they serve, so their
    # doc comments and argument plumbing live here.
    family_decl_src = read_source(repo_root, FAMILY_DECL)

    functions: Dict[str, CppFunction] = {}
    functions.update(parse_cpp_functions(host_src))
    functions.update(parse_cpp_functions(world_src))
    functions.update(parse_cpp_functions(family_decl_src))
    functions.update(parse_cpp_functions(bindings_src))
    macro_sigs = parse_macro_accessors(bindings_src)

    walker_methods = parse_luareg_table(bindings_src, "kWalkerMethods")
    guy_methods = parse_luareg_table(bindings_src, "kGuyMethods")
    og_world = parse_luareg_table(bindings_src, "kOgWorldFuncs")
    og_combat = parse_luareg_table(bindings_src, "kOgCombatFuncs")
    og_host = parse_luareg_table(host_src, "kOgFuncs")
    constants = parse_constants(bindings_src)
    scaffolding = scaffolding_body(world_src)
    world_entry = parse_world_entry_points(scaffolding)
    world_values = parse_world_value_entry_points(scaffolding)

    hook_tables = parse_hook_tables(world_src)
    orders = parse_orders(world_src)
    hook_sigs = parse_family_hook_signatures(world_src)
    level_names = parse_level_hook_names(world_src)
    level_sigs = parse_level_dispatch_signatures(world_src)

    out: List[str] = [HEADER, ""]

    out.append("---@alias og.OrderName "
               + parse_alias(functions, "order_from_string"))
    out.append("---@alias og.ListSelector "
               + parse_alias(functions, "list_from_selector"))
    out.append("")

    out.append("-- Entity (walker) handle. Stats accessors are flattened on")
    out.append("-- with the s_ prefix; the guy record uses g_ (and requires")
    out.append("-- the walker to have one). Handles compare by entity id and")
    out.append("-- are only valid within the dispatch that minted them.")
    out.append("--")
    out.append("-- Property layer (kWalkerProperties):")
    out.append("-- names no method shadows read AND write as plain values")
    out.append("-- (self.hp); method-shadowed names keep reading as the")
    out.append("-- method (method-first __index) and are typed as the union")
    out.append("-- of value and method, because `self.busy = v` is a legal")
    out.append("-- write-through to the same setter the method uses.")
    walker_props = parse_walker_properties(bindings_src)
    walker_method_names = {n for n, _c in walker_methods}
    writable_shadowed = {
        name: setter for name, _getter, setter in walker_props
        if name in walker_method_names and setter is not None
    }
    out.append("---@class og.Walker")
    walker_rows = method_fields(walker_methods, functions, macro_sigs,
                                "og.Walker", writable_props=writable_shadowed)
    walker_rows += property_fields(walker_props, walker_method_names,
                                   functions, macro_sigs)
    out.extend(sorted(walker_rows))
    out.append("")

    out.append("-- Guy-record handle (level_up's first argument; picker-side,")
    out.append("-- dispatch-scoped, no world).")
    out.append("---@class og.Guy")
    out.extend(method_fields(guy_methods, functions, macro_sigs, "og.Guy"))
    out.append("")

    # Hook tables: one class per hook-name table, wired via kOrders.
    order_class: Dict[str, str] = {}
    class_of_table: Dict[str, str] = {}
    for order_name, _enum, table in orders:
        if table in class_of_table:
            order_class[order_name] = class_of_table[table]
            continue
        cls = "og." + order_name.capitalize() + "Hooks"
        class_of_table[table] = cls
        order_class[order_name] = cls
        aliases = sorted(o for o, _e, t in orders if t == table)
        quoted = " / ".join(f'"{a}"' for a in aliases)
        hook_names = [h for h, _e in hook_tables[table]]
        if "do_special" in hook_names:
            # The living order also accepts the table form of do_special
            # (og.register_hooks key 'specials'); assert the registration
            # really exists in the source before declaring it.
            if 'lua_getfield(L, 3, "specials")' not in world_src:
                raise SystemExit(
                    "gen_api_stubs: 'specials' registration not found in "
                    "world_scripts.cpp (surface moved; update the stub "
                    "generator)")
            ds_args, ds_wants = hook_sigs["DoSpecial"]
            ds_sig = Signature(params=ds_args,
                               returns=["boolean"] if ds_wants else [])
            branch_fun = fun_type(ds_sig, None)
            out.append("-- Table form of the do_special hook (the "
                       "og.register_hooks key 'specials'):")
            out.append("-- current_special() selects the entry, a missing "
                       "index falls to `default`,")
            out.append("-- and a table with neither is a successful no-op. "
                       "Entries return")
            out.append("-- like do_special itself.")
            out.append(f"---@class {cls[:-5]}Specials")
            out.append("-- Keys are the special ids the family declared;")
            out.append("-- registration resolves them to slot ints (an")
            out.append("-- unknown id, or a bare slot number, is a load")
            out.append("-- error).")
            out.append(f"---@field [string] {branch_fun}")
            out.append(f"---@field default? {branch_fun}")
            out.append("")
        out.append(f"-- Hook table for og.register_hooks(order = {quoted}).")
        out.append(f"---@class {cls}")
        rows = []
        for hook_name, enum_name in hook_tables[table]:
            args, wants_result = hook_sigs.get(enum_name, ([], False))
            sig = Signature(params=args,
                            returns=["boolean"] if wants_result else [],
                            todo=enum_name not in hook_sigs)
            rows.append(field_line(hook_name, fun_type(sig, None), "",
                                   sig.todo, optional=True))
        if "do_special" in hook_names:
            rows.append(field_line("specials", f"{cls[:-5]}Specials", "",
                                   False, optional=True))
        out.extend(sorted(rows))
        out.append("")

    out.append("-- Hook table for og.register_level_hooks.")
    out.append("---@class og.LevelHooks")
    rows = []
    for name in level_names:
        args = level_sigs.get("level:" + name)
        sig = Signature(params=args or [], todo=args is None)
        rows.append(field_line(name, fun_type(sig, None), "", sig.todo,
                               optional=True))
    out.extend(sorted(rows))
    out.append("")

    out.append("-- Per-entity hook table for og.set_entity_hooks (one-shot:")
    out.append("-- consumed when it fires).")
    out.append("---@class og.EntityHooks")
    death_args = level_sigs.get("entity:on_death")
    sig = Signature(params=death_args or [], todo=death_args is None)
    out.append(field_line("on_death", fun_type(sig, None), "", sig.todo,
                          optional=True))
    out.append("")

    out.append("-- Engine constants (og.C.*), names from the kConstants")
    out.append("-- table in bindings_entity.cpp; every value is an integer.")
    out.append("---@class og.Constants")
    for name in sorted(set(constants)):
        out.append(f"---@field {name} integer")
    out.append("")

    hook_union = "|".join(sorted(set(order_class.values())))
    overrides = {
        "register_hooks": hook_union,
        "register_level_hooks": "og.LevelHooks",
        "set_entity_hooks": "og.EntityHooks",
    }
    out.append("-- Draw-free bindings over the og::combat constexpr helpers")
    out.append("-- (core/combat_math.h); entries from kOgCombatFuncs in")
    out.append("-- bindings_entity.cpp.")
    out.append("---@class og.Combat")
    out.extend(og_function_fields(list(og_combat), functions, {}))
    out.append("")

    # og.* entries that hold a value rather than a function. A sentinel is
    # an empty class (it exists to be compared, never indexed); a frozen
    # table gets its members typed.
    for value in sorted(world_values, key=lambda v: v.name):
        if value.members:
            out.append(f"-- Frozen og.{value.name} table: read-only, and a "
                       "write to it errors.")
        else:
            out.append(f"-- The og.{value.name} sentinel. Compared, never "
                       "indexed.")
        out.append(f"---@class {value.cls}")
        for member, lua_type in sorted(value.members):
            out.append(f"---@field {member} {lua_type}")
        out.append("")

    out.append("-- The og namespace: deterministic arithmetic, world queries,")
    out.append("-- sim events, and the registration entry points.")
    out.append("---@class og")
    out.append("---@field C og.Constants")
    out.append("---@field combat og.Combat")
    for value in sorted(world_values, key=lambda v: v.name):
        out.append(f"---@field {value.name} {value.cls}")
    og_entries = list(og_world) + list(og_host) + list(world_entry)
    out.extend(og_function_fields(
        og_entries, functions, overrides,
        return_overrides={"tuning": "table<string, any>"}))
    # No blank line: the @class block must attach to the statement below.
    out.append("og = {}")
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate docs/modding/og-api.d.lua from the C++ "
                    "binding registration tables.")
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--stdout", action="store_true",
                        help="print instead of writing the stub file")
    args = parser.parse_args()

    text = generate(args.repo_root)
    if args.stdout:
        sys.stdout.write(text)
        return 0
    out_path = args.repo_root / OUTPUT
    out_path.parent.mkdir(parents=True, exist_ok=True)
    old = out_path.read_text(encoding="utf-8") if out_path.exists() else None
    out_path.write_text(text, encoding="utf-8")
    state = "unchanged" if old == text else "written"
    count = len(re.findall(r"^---@field ", text, re.M))
    print(f"gen_api_stubs: {OUTPUT} {state} ({count} fields)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
