#!/usr/bin/env python3
"""Merge a class pack's families from YAML + script into one v3 Lua file.

WHAT IT DOES
------------
Schema v2 shipped every family twice: the numbers in
`packs/<id>/families/<order>-NN-<slug>.yaml` and the behavior in
`packs/<id>/scripts/<slug>.lua`, joined by nothing but a naming habit.
Schema v3 puts both in one file — the script's own text, with its
`og.register_hooks(...)` trailer replaced by an `og.family(...)`
declaration carrying the descriptor:

    packs/core/families/living-00-soldier.yaml  \\
                                                 >  living-00-soldier.lua
    packs/core/scripts/soldier.lua              /

THE LAYOUT LAW (docs/lua-classpacks-design.md §3, spec §3)
----------------------------------------------------------
  * Lines 1..(trailer-1) of the script are copied BYTE-IDENTICAL. The
    functional header stays line 1, the FSGames copyright stays line 2, and
    every body comment keeps its exact line number — which is also what
    keeps ~70 parity mutation pins at their existing line and text.
  * The declaration replaces the trailer, in the YAML's key order, with
    every scalar carried AS TEXT. `4.5` moves as the three characters
    `4.5`; nothing here can turn `120` into `120.0`.
  * A YAML comment lands immediately above the same key it annotated
    (tuning formulas, one-off nostalgia). A comment inside the
    `register_hooks` table lands next to the same hook or special.
  * The YAML's line-1 header is the one line this tool CLAIMS: in a merged
    living file it is dropped, because the script's header already occupies
    line 1 and names the family, and a second banner would be archaeology
    about a file that no longer exists.
  * A block whose keys carry annotations (the example pack's do) is written
    one key per line instead of three to a row: a grid has nowhere to put a
    comment, and the comment is the reason that file exists.
  * Every other input line, from both sources, survives verbatim and in
    order. --check re-proves it; so does the per-source comment
    supersequence, run once against each stream.

A WHOLE-PACK classpack.yaml (a pack that never split its families) merges
the same way, with the pack banner, og.pack and og.anims ahead of the
script's body: `scripts/migrate_classpack_v3.py <pack>/classpack.yaml`.

A NON-LIVING family has no script of its own — its behavior lives in a
script shared with its siblings — so `--data-only` writes one declaration
per descriptor and moves each shared script into `lib/` as a module (body
verbatim, the `og.register_hooks` trailer replaced by the export table)
that the declarations reference inline:

    local wp = og.use("weapon_projectiles")
    og.family("weapon", { ..., on_death = wp.explode_on_death })

WHAT IT REFUSES
---------------
  * A YAML shape it was not taught (an anchor or alias, a folded `>` block,
    a literal block with uneven indent): the migration would have to guess
    what the text meant.
  * A script whose trailer it cannot read as `og.register_hooks(order, id,
    { key = value, ... })` with one-line values.
  * A living family no script speaks for, or a script whose id resolves to
    no family in the set it was given (both passes of the engine's own
    resolution, declared id then display name, are tried).
  * A comment stream that came out short, or a carried line that changed.

WHY A SCRIPT AND NOT 19 HAND EDITS
----------------------------------
21 families times ~40 scalars is the exact shape that produces the
transcription slip nobody sees — the same argument migrate_classpack_v2.py
was written under, and the same discipline: values move as text, unclaimed
lines are proven identical, and the tool refuses rather than guesses.

USAGE
-----
    scripts/migrate_classpack_v3.py packs/core/families/living-*.yaml
    scripts/migrate_classpack_v3.py --dry-run <files>   # report, write nothing
    scripts/migrate_classpack_v3.py --check <files>     # re-prove a merged tree
    scripts/migrate_classpack_v3.py --linemap out.json  # old->new line map,
                                                        # for the pin re-anchor

Exit 0 when every file merged, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]

KEY_RE = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_]*):(?:\s(.*))?$")
ITEM_RE = re.compile(r"^(\s*)-\s(.*)$")
NUMBER_RE = re.compile(r"^-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?$")
LUA_ID_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
REGISTER_RE = re.compile(
    r'^og\.register_hooks\("([a-z]+)",\s*"([^"]+)",\s*\{(.*)$')

# The YAML `~` is a PRESENT null, and the installer copies-and-patches, so
# it is not the same as leaving the key out. og.NIL is its Lua spelling.
NIL = "og.NIL"

# v2's `init_bit_flags:` is `flags =` in a declaration; every other key
# keeps its name.
RENAMED_KEYS = {"init_bit_flags": "flags"}

# `~` is a PRESENT null only on a field the interchange can hold one in (a
# NullableString). Everywhere else the YAML reader IGNORED it — `animation:
# ~` set nothing at all, exactly as leaving the key out does — so its Lua
# spelling is the absent key, not og.NIL, and og.family says so by refusing
# the value (format spec V6).
NULLABLE_KEYS = ("short_name", "sprite", "promotes_to", "death_message",
                 "description")
NULL_IS_OMISSION_KEYS = ("animation",)

# The three blocks the canonical declaration lays out three-to-a-row with
# their `= {` columns aligned.
ROW_BLOCKS = ("stats", "combat", "costs")
ROW_WIDTH = 3


class MigrationError(Exception):
    """A refusal, reported against one file."""


# --- comment streams -----------------------------------------------------


def yaml_comments(text: str) -> List[str]:
    """Every YAML comment, in order, transliterated to Lua's `--`.

    A `#` opens a comment only at line start or after whitespace, and only
    outside quotes: `id: core:#8` is an id, and six living files ship one.
    """
    out: List[str] = []
    for line in text.splitlines():
        found = scan_comment(line, "#")
        if found is not None:
            out.append("--" + found[len("#"):].rstrip())
    return out


def lua_comments(text: str) -> List[str]:
    out: List[str] = []
    for line in text.splitlines():
        found = scan_comment(line, "--")
        if found is not None:
            out.append(found.rstrip())
    return out


def scan_comment(line: str, opener: str) -> Optional[str]:
    """The comment starting in `line`, or None. Quote-aware."""
    quote = ""
    index = 0
    while index < len(line):
        char = line[index]
        if quote:
            if char == "\\" and quote == '"':
                index += 2
                continue
            if char == quote:
                quote = ""
        elif char in "\"'":
            quote = char
        elif line.startswith(opener, index) and (
                index == 0 or line[index - 1].isspace()):
            return line[index:]
        index += 1
    return None


def strip_comment(line: str) -> Tuple[str, str]:
    """(code, comment) for a Lua line; comment includes its `--`."""
    found = scan_comment(line, "--")
    if found is None:
        return line, ""
    return line[:len(line) - len(found)], found.rstrip()


def is_subsequence(needle: Sequence[str], haystack: Sequence[str]) -> bool:
    it = iter(haystack)
    return all(any(item == candidate for candidate in it) for item in needle)


# --- the YAML side -------------------------------------------------------


class Value:
    """A YAML value, carried as text.

    `kind` is one of: scalar (token), flow_seq (element tokens), flow_map
    (key/token pairs), map (nested Items), list (Items per entry).
    """

    def __init__(self, kind: str, payload) -> None:
        self.kind = kind
        self.payload = payload


class Item:
    def __init__(self, comments: List[str], key: str, value: Value,
                 trailer: Optional[List[str]] = None) -> None:
        self.comments = comments        # verbatim `# ...` lines above the key
        self.key = key
        self.value = value
        self.trailer = trailer or []    # the `# ...` that sat on the value
                                        # line, plus the lines continuing it


def split_flow(body: str, where: str) -> List[str]:
    """Split a flow collection's interior into verbatim element substrings."""
    tokens: List[str] = []
    start = 0
    quote = ""
    index = 0
    depth = 0
    while index < len(body):
        char = body[index]
        if quote:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = ""
        elif char in "\"'":
            quote = char
        elif char in "[{":
            depth += 1
        elif char in "]}":
            depth -= 1
        elif char == "," and depth == 0:
            tokens.append(body[start:index].strip())
            start = index + 1
        index += 1
    if quote:
        raise MigrationError(f"{where}: unterminated quote in {body!r}")
    tail = body[start:].strip()
    if tail or tokens:
        tokens.append(tail)
    return tokens


def parse_value(text: str, where: str) -> Value:
    body = text.strip()
    if body.startswith("[") and body.endswith("]"):
        return Value("flow_seq", split_flow(body[1:-1], where))
    if body.startswith("{") and body.endswith("}"):
        pairs: List[Tuple[str, str]] = []
        for token in split_flow(body[1:-1], where):
            if ":" not in token:
                raise MigrationError(f"{where}: {token!r} is not `key: value`")
            key, _, value = token.partition(":")
            pairs.append((key.strip(), value.strip()))
        return Value("flow_map", pairs)
    if body.startswith(("|", ">", "&", "*", "!")):
        raise MigrationError(f"{where}: unsupported YAML scalar {body!r}")
    return Value("scalar", body)


def parse_block(lines: List[str], index: int, indent: int,
                path: str) -> Tuple[List[Item], int]:
    """Read a mapping block at `indent`, returning its items."""
    items: List[Item] = []
    pending: List[str] = []
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            # A blank line ends this block when what follows is outdented;
            # inside one it would be whitespace with nowhere to land.
            nxt = next_content(lines, index)
            if nxt is None:
                break
            if len(lines[nxt]) - len(lines[nxt].lstrip()) < indent:
                break
            if indent > 0:
                raise MigrationError(
                    f"{path}:{index + 1}: a blank line inside a family entry "
                    f"has no place to land in the declaration")
            # A manifest's top level breathes between sections; the merged
            # layout puts its own blank lines between the emitted calls.
            index += 1
            continue
        stripped = line.lstrip()
        line_indent = len(line) - len(stripped)
        if stripped.startswith("#"):
            if line_indent < indent:
                break
            if line_indent > indent:
                raise MigrationError(
                    f"{path}:{index + 1}: a comment indented past its mapping "
                    f"continues nothing: {stripped.rstrip()!r}")
            pending.append(stripped.rstrip())
            index += 1
            continue
        if line_indent < indent:
            break
        if line_indent > indent:
            raise MigrationError(
                f"{path}:{index + 1}: unexpected indent inside a mapping")
        match = KEY_RE.match(line.rstrip("\n"))
        if not match:
            raise MigrationError(
                f"{path}:{index + 1}: not a `key: value` line: {line!r}")
        key, rest = match.group(2), match.group(3)
        where = f"{path}:{index + 1} {key}"
        index += 1
        if rest is not None and rest.strip() == "|":
            block, index = parse_literal_block(lines, index, indent, where)
            items.append(Item(pending, key, Value("block", block)))
        elif rest is None or not rest.strip():
            nxt = next_content(lines, index)
            if nxt is None:
                raise MigrationError(f"{where}: key with no value")
            child_indent = len(lines[nxt]) - len(lines[nxt].lstrip())
            if child_indent <= indent:
                raise MigrationError(f"{where}: key with no value")
            if lines[nxt].lstrip().startswith("- "):
                entries, index = parse_sequence(lines, index, child_indent,
                                                path)
                items.append(Item(pending, key, entries))
            else:
                children, index = parse_block(lines, index, child_indent, path)
                items.append(Item(pending, key, Value("map", children)))
        else:
            found = scan_comment(rest, "#")
            trailer: List[str] = []
            if found is not None:
                rest = rest[:len(rest) - len(found)]
                trailer.append("--" + found[1:].rstrip())
                index = read_continuation(lines, index, indent, trailer)
            items.append(Item(pending, key, parse_value(rest, where), trailer))
        pending = []
    if pending:
        raise MigrationError(
            f"{path}: a trailing comment block annotates nothing: "
            f"{pending[0]!r}")
    return items, index


def read_continuation(lines: List[str], index: int, indent: int,
                      trailer: List[str]) -> int:
    """Fold the comment lines that continue a value line's trailer into it."""
    while index < len(lines):
        stripped = lines[index].lstrip()
        line_indent = len(lines[index]) - len(stripped)
        if not stripped.startswith("#") or line_indent <= indent:
            break
        trailer.append("--" + stripped[1:].rstrip())
        index += 1
    return index


def parse_literal_block(lines: List[str], index: int, indent: int,
                        where: str) -> Tuple[List[str], int]:
    """A `|` literal block: its lines, common indent removed.

    Clip chomping (plain `|`) keeps exactly one trailing newline, which is
    why the emitted string ends in `\\n`.
    """
    body: List[str] = []
    block_indent: Optional[int] = None
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            raise MigrationError(
                f"{where}: a blank line inside a literal block would change "
                f"the string this tool is copying")
        line_indent = len(line) - len(line.lstrip())
        if line_indent <= indent:
            break
        if block_indent is None:
            block_indent = line_indent
        if line_indent != block_indent:
            raise MigrationError(
                f"{where}: a literal block whose lines are indented unevenly "
                f"carries whitespace this tool would have to guess about")
        body.append(line[block_indent:].rstrip("\n"))
        index += 1
    if not body:
        raise MigrationError(f"{where}: an empty literal block")
    return body, index


def parse_sequence(lines: List[str], index: int, indent: int,
                   path: str) -> Tuple[Value, int]:
    """A block sequence at `indent`: mappings, or bare scalars with trailers."""
    first = lines[next_content(lines, index) or index]
    head = ITEM_RE.match(first.rstrip("\n"))
    if head is not None and KEY_RE.match(" " * (indent + 2) +
                                         head.group(2)) is None:
        return parse_scalar_sequence(lines, index, indent, path)
    entries, index = parse_mapping_sequence(lines, index, indent, path)
    return Value("list", entries), index


def parse_scalar_sequence(lines: List[str], index: int, indent: int,
                          path: str) -> Tuple[Value, int]:
    """`- <scalar>` entries, each keeping the comment on its own line."""
    rows: List[Item] = []
    while index < len(lines):
        line = lines[index]
        stripped = line.lstrip()
        line_indent = len(line) - len(stripped)
        if not line.strip() or line_indent != indent or \
                not stripped.startswith("- "):
            break
        rest = stripped[2:]
        where = f"{path}:{index + 1}"
        trailer: List[str] = []
        found = scan_comment(rest, "#")
        if found is not None:
            rest = rest[:len(rest) - len(found)]
            trailer.append("--" + found[1:].rstrip())
        index += 1
        index = read_continuation(lines, index, indent, trailer)
        rows.append(Item([], "", parse_value(rest, where), trailer))
    return Value("seq", rows), index


def parse_mapping_sequence(lines: List[str], index: int, indent: int,
                           path: str) -> Tuple[List[List[Item]], int]:
    """Read a block sequence of mappings at `indent`."""
    entries: List[List[Item]] = []
    pending: List[str] = []
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            break
        stripped = line.lstrip()
        line_indent = len(line) - len(stripped)
        if stripped.startswith("#") and line_indent == indent:
            # A comment block above `- id:` annotates the ENTRY (one shipped
            # descriptor has one). It rides down onto the entry's first key,
            # which is the id: the same "above what it annotates" rule.
            pending.append(stripped.rstrip())
            index += 1
            continue
        if line_indent < indent or not stripped.startswith("- "):
            break
        head = ITEM_RE.match(line.rstrip("\n"))
        if head is None:
            raise MigrationError(f"{path}:{index + 1}: unreadable list item")
        rest = head.group(2)
        key_indent = indent + 2
        first = KEY_RE.match(" " * key_indent + rest)
        if first is None:
            raise MigrationError(
                f"{path}:{index + 1}: a list item must open with `- key: "
                f"value`: {line!r}")
        where = f"{path}:{index + 1} {first.group(2)}"
        text = first.group(3)
        trailer: List[str] = []
        found = scan_comment(text, "#")
        if found is not None:
            text = text[:len(text) - len(found)]
            trailer.append("--" + found[1:].rstrip())
        item = Item(pending, first.group(2), parse_value(text, where),
                    trailer)
        pending = []
        index += 1
        index = read_continuation(lines, index, key_indent, trailer)
        rest_items, index = parse_block(lines, index, key_indent, path)
        entries.append([item] + rest_items)
    if pending:
        raise MigrationError(
            f"{path}: a comment block after the last entry of a sequence "
            f"annotates nothing: {pending[0]!r}")
    return entries, index


def next_content(lines: List[str], index: int) -> Optional[int]:
    while index < len(lines):
        stripped = lines[index].strip()
        if stripped and not stripped.startswith("#"):
            return index
        index += 1
    return None


def read_family_doc(text: str, path: str) -> Tuple[str, List[Item]]:
    """(order, the one family entry's items) from a families/*.yaml."""
    lines = text.splitlines()
    items, index = parse_block(lines, 0, 0, path)
    if index != len(lines):
        raise MigrationError(f"{path}:{index + 1}: trailing content")
    top = {item.key: item for item in items}
    if set(top) != {"families"}:
        raise MigrationError(
            f"{path}: expected exactly a `families:` mapping, got "
            f"{', '.join(sorted(top)) or 'nothing'}")
    orders = top["families"].value
    if orders.kind != "map" or len(orders.payload) != 1:
        raise MigrationError(f"{path}: expected one order under `families:`")
    order_item = orders.payload[0]
    if order_item.value.kind != "list" or len(order_item.value.payload) != 1:
        raise MigrationError(
            f"{path}: expected exactly one family entry under "
            f"`{order_item.key}:`")
    return order_item.key, order_item.value.payload[0]


class Manifest:
    """A v1-style classpack.yaml: the pack header, its anims, one family."""

    def __init__(self, header: List[str], pack: List[Item],
                 anims: List[Item], order: str, entry: List[Item]) -> None:
        self.header = header        # the file's leading comment block
        self.pack = pack            # pack/version/title/authors
        self.anims = anims          # one Item per declared animation set
        self.order = order
        self.entry = entry


PACK_KEYS = ("pack", "version", "title", "authors")


def read_pack_doc(text: str, path: str) -> Manifest:
    """A whole classpack.yaml, for a pack that never split its families."""
    lines = text.splitlines()
    items, index = parse_block(lines, 0, 0, path)
    if index != len(lines):
        raise MigrationError(f"{path}:{index + 1}: trailing content")
    top = {item.key: item for item in items}
    unknown = set(top) - set(PACK_KEYS) - {"anims", "families"}
    if unknown:
        raise MigrationError(
            f"{path}: unknown top-level key(s) {', '.join(sorted(unknown))}")
    if "families" not in top:
        raise MigrationError(f"{path}: no families")
    header = items[0].comments if items else []
    pack = [item for item in items if item.key in PACK_KEYS]
    if pack:
        pack[0] = Item([], pack[0].key, pack[0].value, pack[0].trailer)
    anims: List[Item] = []
    if "anims" in top:
        if top["anims"].value.kind != "map":
            raise MigrationError(f"{path}: anims must be a mapping")
        anims = top["anims"].value.payload
        anims[0] = Item(top["anims"].comments + anims[0].comments,
                        anims[0].key, anims[0].value, anims[0].trailer)
    order_item = single_order(top["families"], path)
    return Manifest(header, pack, anims, order_item[0], order_item[1])


def single_order(families: Item, path: str) -> Tuple[str, List[Item]]:
    orders = families.value
    if orders.kind != "map" or len(orders.payload) != 1:
        raise MigrationError(f"{path}: expected one order under `families:`")
    order_item = orders.payload[0]
    if order_item.value.kind != "list" or len(order_item.value.payload) != 1:
        raise MigrationError(
            f"{path}: expected exactly one family entry under "
            f"`{order_item.key}:`")
    return order_item.key, order_item.value.payload[0]


def item_scalar(items: List[Item], key: str) -> Optional[str]:
    for item in items:
        if item.key == key and item.value.kind == "scalar":
            return item.value.payload
    return None


# --- scalar transliteration ----------------------------------------------


def lua_scalar(token: str, where: str) -> str:
    """A YAML scalar token as Lua source, preserving its subtype."""
    if token == "~" or token == "null":
        return NIL
    if token in ("true", "false"):
        return token
    if NUMBER_RE.match(token):
        return token
    if token.startswith('"') and token.endswith('"') and len(token) >= 2:
        check_escapes(token, where)
        return token
    if token.startswith("'") or '"' in token or "\\" in token:
        raise MigrationError(
            f"{where}: {token!r} has no unambiguous Lua spelling — quote it "
            f"with double quotes in the YAML first")
    return '"' + token + '"'


def check_escapes(literal: str, where: str) -> None:
    """Refuse a quoted scalar whose escapes differ between YAML and Lua."""
    index = 1
    body = literal[:-1]
    while index < len(body):
        if body[index] == "\\":
            if index + 1 >= len(body) or body[index + 1] not in 'nt"\\':
                raise MigrationError(
                    f"{where}: escape {body[index:index + 2]!r} does not mean "
                    f"the same thing in YAML and Lua")
            index += 2
            continue
        index += 1


# --- the script side -----------------------------------------------------


class Registration:
    """One `og.register_hooks(order, id, { ... })` call."""

    def __init__(self, order: str, family_id: str, trailer: str) -> None:
        self.order = order
        self.family_id = family_id
        self.trailer = trailer          # comment after the opening `{`
        self.hooks: List[Item] = []     # hook name -> Lua expression text
        self.hook_trailers: Dict[str, str] = {}
        self.specials: List[Item] = []  # special id (or `default`) -> text
        self.special_trailers: Dict[str, str] = {}
        self.specials_comments: List[str] = []
        self.specials_trailer = ""


def parse_table_body(lines: List[str], index: int, path: str,
                     owner: str) -> Tuple[List[Item], Dict[str, str], int]:
    """Read `key = value,` lines up to the closing brace of a Lua table."""
    items: List[Item] = []
    trailers: Dict[str, str] = {}
    pending: List[str] = []
    while index < len(lines):
        raw = lines[index]
        code, comment = strip_comment(raw)
        stripped = code.strip()
        if not stripped and comment:
            pending.append(comment)
            index += 1
            continue
        if stripped in ("}", "},", "})"):
            if pending:
                raise MigrationError(
                    f"{path}:{index + 1}: a comment block at the end of "
                    f"{owner} annotates nothing")
            return items, trailers, index
        if "=" not in stripped:
            raise MigrationError(
                f"{path}:{index + 1}: not a `key = value,` line: {raw!r}")
        key, _, value = stripped.partition("=")
        key = key.strip()
        value = value.strip()
        if not LUA_ID_RE.match(key):
            raise MigrationError(
                f"{path}:{index + 1}: {key!r} is not a plain key")
        if value.endswith("{"):
            raise MigrationError(
                f"{path}:{index + 1}: a nested table under {owner} is not "
                f"something this tool was taught to move")
        if not value.endswith(","):
            raise MigrationError(
                f"{path}:{index + 1}: {owner}.{key} must end in a comma")
        items.append(Item(pending, key, Value("lua", value[:-1].strip())))
        if comment:
            trailers[key] = comment
        pending = []
        index += 1
    raise MigrationError(f"{path}: {owner} is never closed")


def parse_registration(lines: List[str], index: int,
                       path: str) -> Tuple[Registration, int]:
    match = REGISTER_RE.match(lines[index].rstrip("\n"))
    if match is None:
        raise MigrationError(
            f"{path}:{index + 1}: unreadable og.register_hooks call")
    tail = match.group(3).strip()
    trailer = ""
    if tail:
        found = scan_comment(" " + tail, "--")
        if found is None or (" " + tail)[:len(" " + tail) - len(found)].strip():
            raise MigrationError(
                f"{path}:{index + 1}: code after the opening brace: {tail!r}")
        trailer = found.rstrip()
    reg = Registration(match.group(1), match.group(2), trailer)
    index += 1
    owner = f"og.register_hooks({reg.family_id})"
    pending: List[str] = []
    while index < len(lines):
        raw = lines[index]
        code, comment = strip_comment(raw)
        stripped = code.strip()
        if not stripped and comment:
            pending.append(comment)
            index += 1
            continue
        if stripped == "})":
            if pending:
                raise MigrationError(
                    f"{path}:{index + 1}: a comment block at the end of "
                    f"{owner} annotates nothing")
            return reg, index + 1
        if stripped == "specials = {":
            reg.specials_comments = pending
            reg.specials_trailer = comment
            pending = []
            reg.specials, reg.special_trailers, index = parse_table_body(
                lines, index + 1, path, f"{owner}.specials")
            closing = lines[index].strip()
            if strip_comment(closing)[0].strip() != "},":
                raise MigrationError(
                    f"{path}:{index + 1}: the specials table must close with "
                    f"`}},`")
            if strip_comment(closing)[1]:
                raise MigrationError(
                    f"{path}:{index + 1}: a comment on the specials table's "
                    f"closing brace has nowhere to land")
            index += 1
            continue
        if "=" not in stripped or not stripped.endswith(","):
            raise MigrationError(
                f"{path}:{index + 1}: not a `hook = value,` line: {raw!r}")
        key, _, value = stripped.partition("=")
        key = key.strip()
        if not LUA_ID_RE.match(key):
            raise MigrationError(
                f"{path}:{index + 1}: {key!r} is not a hook name")
        reg.hooks.append(Item(pending, key, Value("lua",
                                                  value.strip()[:-1].strip())))
        if comment:
            reg.hook_trailers[key] = comment
        pending = []
        index += 1
    raise MigrationError(f"{path}: {owner} is never closed")


def split_script(text: str, path: str) -> Tuple[List[str], List[Registration],
                                                List[List[str]]]:
    """(verbatim prefix lines, registrations, between-call filler lines)."""
    lines = text.splitlines()
    prefix: List[str] = []
    index = 0
    while index < len(lines) and not lines[index].startswith(
            "og.register_hooks("):
        prefix.append(lines[index])
        index += 1
    registrations: List[Registration] = []
    fillers: List[List[str]] = []
    filler: List[str] = []
    while index < len(lines):
        if lines[index].startswith("og.register_hooks("):
            reg, index = parse_registration(lines, index, path)
            registrations.append(reg)
            fillers.append(filler)
            filler = []
            continue
        if lines[index].strip():
            raise MigrationError(
                f"{path}:{index + 1}: code between registrations: "
                f"{lines[index]!r}")
        filler.append(lines[index])
        index += 1
    if filler and any(line.strip() for line in filler):
        raise MigrationError(f"{path}: content after the last registration")
    return prefix, registrations, fillers


# --- emitting the declaration --------------------------------------------


def fill_rows(parts: List[str], indent: str, first: str) -> List[str]:
    """`first` then `parts`, three to a row, wrapped under the open brace."""
    rows: List[str] = []
    for start in range(0, len(parts), ROW_WIDTH):
        rows.append(" ".join(parts[start:start + ROW_WIDTH]))
    out = [first + rows[0]]
    for row in rows[1:]:
        out.append(indent + row)
    return out


def wrap_list(head: str, elements: List[str], width: int = 78) -> List[str]:
    """`head` plus a Lua list, wrapped under the open brace when it is long."""
    if not elements:
        return [head + "{},"]
    single = head + "{ " + ", ".join(elements) + " },"
    if len(single) <= width:
        return [single]
    indent = " " * (len(head) + 2)
    rows: List[str] = []
    current = head + "{ "
    for position, element in enumerate(elements):
        piece = element + ("," if position + 1 < len(elements) else " },")
        if current.strip() and len(current) + len(piece) > width:
            rows.append(current.rstrip())
            current = indent
        current += piece + (" " if position + 1 < len(elements) else "")
    rows.append(current.rstrip())
    return rows


def with_trailer(text: str, trailer: List[str]) -> List[str]:
    """`text` with its comment beside it, continuations lined up under it."""
    if not trailer:
        return [text]
    out = [text + "  " + trailer[0]]
    column = " " * (len(text) + 2)
    return out + [column + line for line in trailer[1:]]


def lua_string(text: str, where: str) -> str:
    """A raw string as a double-quoted Lua literal.

    Only the four escapes v2 descriptions already used are produced, so a
    round trip through this is the identity on every shipped string.
    """
    for char in text:
        if char in "\r\t" or ord(char) < 0x20:
            raise MigrationError(
                f"{where}: a control character has no plain Lua spelling")
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_literal_block(head: str, body: List[str],
                         where: str) -> List[str]:
    """A YAML `|` block as concatenated per-line Lua strings.

    One source line per output line, each ending in the `\\n` the literal
    block kept, so the string is readable and the line breaks are where the
    author put them.
    """
    pieces = [lua_string(line, where)[:-1] + '\\n"' for line in body]
    out = [head + pieces[0] + (" .." if len(pieces) > 1 else ",")]
    column = " " * len(head)
    for position, piece in enumerate(pieces[1:], start=2):
        out.append(column + piece + (" .." if position < len(pieces) else ","))
    return out


def annotated(items: List[Item]) -> bool:
    return any(item.comments or item.trailer for item in items)


def render_annotated_block(key: str, items: List[Item], where: str,
                           indent: str) -> Tuple[List[str], Dict[str, int]]:
    """A mapping block laid out one key per line.

    The three-to-a-row grid has nowhere to put an annotation, so a block
    that carries one is written out long-hand and every comment keeps the
    key it was written against.
    """
    lines = [f"{indent}{key} = {{"]
    anchors: Dict[str, int] = {}
    inner = indent + "  "
    for item in items:
        for comment in item.comments:
            lines.append(inner + "--" + comment[1:])
        if item.value.kind == "map":
            nested, sub = render_annotated_block(
                item.key, item.value.payload, f"{where}.{item.key}", inner)
            for name, offset in sub.items():
                anchors[f"{item.key}.{name}"] = len(lines) + offset
            lines.extend(nested)
            continue
        if item.value.kind != "scalar":
            raise MigrationError(f"{where}.{item.key}: expected a scalar")
        text = (f"{inner}{item.key} = "
                f"{lua_scalar(item.value.payload, where + '.' + item.key)},")
        anchors[item.key] = len(lines)
        lines.extend(with_trailer(text, item.trailer))
    lines.append(indent + "},")
    return lines, anchors


def block_parts(items: List[Item], where: str, prefix: str,
                anchors: Dict[str, int], line_base: int,
                rows: List[str]) -> List[str]:
    """`key = value,` fragments for a flat mapping block."""
    parts: List[str] = []
    for item in items:
        if item.comments or item.trailer:
            raise MigrationError(
                f"{where}.{item.key}: a comment inside a stats/combat/costs "
                f"block has no row to land on")
        if item.value.kind != "scalar":
            raise MigrationError(f"{where}.{item.key}: expected a scalar")
        parts.append(f"{item.key} = "
                     f"{lua_scalar(item.value.payload, where + '.' + item.key)},")
    return parts


def render_specials(entries: List[List[Item]], reg: Optional[Registration],
                    where: str) -> Tuple[List[str], List[str]]:
    """(the specials table's lines, the special ids it declared)."""
    casts: Dict[str, Item] = {}
    if reg is not None:
        for item in reg.specials:
            casts[item.key] = item
    rendered: List[Tuple[List[str], str, str, str, str, str]] = []
    ids: List[str] = []
    for entry in entries:
        fields = {item.key: item for item in entry}
        for item in entry:
            if item.comments:
                raise MigrationError(
                    f"{where}: a comment inside a YAML special entry has "
                    f"nowhere to land")
        special_id = fields["id"].value.payload
        ids.append(special_id)
        head = f'id = "{special_id}",'
        name = f'name = {lua_scalar(fields["name"].value.payload, where)},'
        cost = f'mp_cost = {fields["mp_cost"].value.payload},'
        extra = ""
        if "alternate" in fields:
            alt = fields["alternate"].value
            if alt.kind != "flow_map" or [k for k, _ in alt.payload] != ["name"]:
                raise MigrationError(
                    f"{where} '{special_id}': alternate takes exactly a name")
            alt_name = lua_scalar(alt.payload[0][1], where)
            extra = "alternate = { name = " + alt_name + " },"
        cast = casts.pop(special_id, None)
        comments = list(cast.comments) if cast is not None else []
        trailer = reg.special_trailers.get(special_id, "") if reg else ""
        cast_text = f"cast = {cast.value.payload}," if cast is not None else ""
        rendered.append((comments, head, name, cost, extra,
                         cast_text + ("  " + trailer if trailer else "")))
    default = casts.pop("default", None)
    if casts:
        raise MigrationError(
            f"{where}: the script casts {', '.join(sorted(casts))}, which the "
            f"descriptor does not declare as a special")
    if not rendered and default is None:
        return ["  specials = {},"], ids

    id_w = max((len(r[1]) for r in rendered), default=0)
    name_w = max((len(r[2]) for r in rendered), default=0)
    cost_w = max((len(r[3]) for r in rendered), default=0)
    lines = ["  " + comment for comment in (reg.specials_comments if reg
                                            else [])]
    trailer = reg.specials_trailer if reg else ""
    lines.append("  specials = {" + ("  " + trailer if trailer else ""))
    for comments, head, name, cost, extra, tail in rendered:
        for comment in comments:
            lines.append("    " + comment)
        body = f"{{ {head.ljust(id_w)} {name.ljust(name_w)} "
        body += cost.ljust(cost_w) if extra or tail else cost
        for piece in (extra, tail):
            if piece:
                body += " " + piece
        body = body.rstrip()
        if body.endswith(","):
            body = body[:-1]
        lines.append(f"    {body} }},")
    if default is not None:
        for comment in default.comments:
            lines.append("    " + comment)
        trailer = reg.special_trailers.get("default", "") if reg else ""
        line = f"    default_cast = {default.value.payload},"
        lines.append(line + ("  " + trailer if trailer else ""))
    lines.append("  },")
    return lines, ids


def render_pack(items: List[Item], where: str) -> List[str]:
    """og.pack{...} from classpack.yaml's top-level scalars.

    Every field of the pack header is a STRING in the interchange (the YAML
    reader stored `value.text` for all four), so `version: 1` is written
    `version = "1"` — the same string the manifest has always carried, not a
    number the harvester would refuse.
    """
    if not items:
        return []
    lines = ["og.pack{"]
    for item in items:
        for comment in item.comments:
            lines.append("  --" + comment[1:])
        if item.value.kind != "scalar":
            raise MigrationError(f"{where}.{item.key}: expected a scalar")
        token = item.value.payload
        text = token if token.startswith('"') else '"' + token + '"'
        check_escapes(text, f"{where}.{item.key}")
        key = "id" if item.key == "pack" else item.key
        if key == "authors":
            text = "{ " + text + " }"
        lines.extend(with_trailer(f"  {key} = {text},", item.trailer))
    lines.append("}")
    return lines


def render_anims(items: List[Item], where: str) -> List[str]:
    """og.anims(name, { rows = N, frames = { ... } }) per declared set."""
    lines: List[str] = []
    for item in items:
        for comment in item.comments:
            lines.append("--" + comment[1:])
        if item.value.kind != "map":
            raise MigrationError(f"{where}.{item.key}: expected a mapping")
        fields = {sub.key: sub for sub in item.value.payload}
        if set(fields) - {"rows", "frames"}:
            raise MigrationError(f"{where}.{item.key}: unknown key")
        lines.append(f'og.anims("{item.key}", {{')
        rows = fields.get("rows")
        if rows is not None:
            for comment in rows.comments:
                lines.append("  --" + comment[1:])
            lines.extend(with_trailer(
                f"  rows = {lua_scalar(rows.value.payload, where)},",
                rows.trailer))
        frames = fields.get("frames")
        if frames is None:
            raise MigrationError(f"{where}.{item.key}: no frames")
        if frames.value.kind != "seq":
            raise MigrationError(
                f"{where}.{item.key}.frames: expected a list of rows")
        lines.append("  frames = {")
        bodies = []
        for row in frames.value.payload:
            if row.value.kind == "scalar" and row.value.payload in ("~",
                                                                    "null"):
                bodies.append("false,")   # the null row; nil punches a hole
            elif row.value.kind == "flow_seq":
                bodies.append("{ " + ", ".join(row.value.payload) + " },")
            else:
                raise MigrationError(
                    f"{where}.{item.key}.frames: a row is a frame list or ~")
        width = max(len(body) for body in bodies)
        for body, row in zip(bodies, frames.value.payload):
            lines.extend(with_trailer("    " + body.ljust(width)
                                      if row.trailer else "    " + body,
                                      row.trailer))
        lines.append("  },")
        lines.append("})")
    return lines


def render_family(entry: List[Item], order: str, reg: Optional[Registration],
                  path: str) -> Tuple[List[str], Dict[str, int], List[str]]:
    """The og.family block for one family. Returns (lines, anchors, ids)."""
    family_id = item_scalar(entry, "id")
    if family_id is None:
        raise MigrationError(f"{path}: the family entry has no id")
    where = f"{path} {family_id}"
    keys = [item.key for item in entry]
    if len(set(keys)) != len(keys):
        raise MigrationError(f"{where}: a key is declared twice")

    lines: List[str] = []
    anchors: Dict[str, int] = {}
    trailer = reg.trailer if reg is not None else ""
    lines.append(f'og.family("{order}", {{' + ("   " + trailer if trailer
                                               else ""))
    special_ids: List[str] = []
    hook_specials_seen = False
    for item in entry:
        if (item.value.kind == "scalar" and item.value.payload in ("~", "null")
                and item.key not in NULLABLE_KEYS):
            if item.key not in NULL_IS_OMISSION_KEYS:
                raise MigrationError(
                    f"{where}.{item.key}: a `~` on a field with no null in "
                    f"the interchange — this tool was not taught whether it "
                    f"means og.NIL or nothing at all")
            if item.comments or item.trailer:
                raise MigrationError(
                    f"{where}.{item.key}: `~` here means the key is left "
                    f"out, which would drop the comment beside it")
            continue
        if item.key == "tuning" and lines and lines[-1].strip():
            lines.append("")        # ahead of the comments, not between them
        for comment in item.comments:
            lines.append("  --" + comment[1:])
        key = RENAMED_KEYS.get(item.key, item.key)
        value = item.value
        if item.key == "specials":
            if value.kind == "list":
                entries = value.payload
            elif value.kind == "flow_seq" and value.payload == []:
                entries = []
            else:
                raise MigrationError(f"{where}.specials: expected a list")
            block, special_ids = render_specials(entries, reg, where)
            for line in block:
                lines.append(line)
            hook_specials_seen = True
            continue
        if key in ROW_BLOCKS:
            if value.kind != "map":
                raise MigrationError(f"{where}.{key}: expected a block")
            if annotated(value.payload) or any(
                    annotated(sub.value.payload) for sub in value.payload
                    if sub.value.kind == "map"):
                block, sub_anchors = render_annotated_block(
                    key, value.payload, f"{where}.{key}", "  ")
                base = len(lines)
                for name, offset in sub_anchors.items():
                    anchors[f"{family_id}/{key}.{name}"] = base + offset + 1
                lines.extend(block)
                continue
            head = f"  {key.ljust(6)} = {{ "
            if key == "costs":
                pairs = {sub.key: sub for sub in value.payload}
                if set(pairs) - {"hire", "train"}:
                    raise MigrationError(f"{where}.costs: unknown key")
                lines.append(head + f"hire = "
                             f"{lua_scalar(pairs['hire'].value.payload, where)},")
                anchors[f"{family_id}/costs.hire"] = len(lines)
                inner = "             train = { "
                parts = block_parts(pairs["train"].value.payload,
                                    where + ".costs.train", "", anchors, 0, [])
                rows = fill_rows(parts, " " * len(inner), inner)
                rows[-1] = rows[-1][:-1] + " } },"
                for offset, row in enumerate(rows):
                    lines.append(row)
                    for axis in parts:
                        if axis in row:
                            anchors[f"{family_id}/costs.train."
                                    f"{axis.split(' ')[0]}"] = len(lines)
                continue
            parts = block_parts(value.payload, where + "." + key, "", anchors,
                                0, [])
            rows = fill_rows(parts, " " * len(head), head)
            rows[-1] = rows[-1][:-1] + " },"
            for row in rows:
                lines.append(row)
                for part in parts:
                    if part in row:
                        anchors[f"{family_id}/{key}."
                                f"{part.split(' ')[0]}"] = len(lines)
            continue
        if key == "tuning":
            if value.kind != "map":
                raise MigrationError(f"{where}.tuning: expected a block")
            lines.append("  tuning = {")
            for sub in value.payload:
                for comment in sub.comments:
                    lines.append("    --" + comment[1:])
                if sub.value.kind != "scalar":
                    raise MigrationError(
                        f"{where}.tuning.{sub.key}: expected a scalar")
                text = (f"    {sub.key} = "
                        f"{lua_scalar(sub.value.payload, where)},")
                anchors[f"{family_id}/tuning.{sub.key}"] = len(lines) + 1
                lines.extend(with_trailer(text, sub.trailer))
            lines.append("  },")
            continue
        if value.kind == "flow_seq":
            elements = [lua_scalar(token, where + "." + key)
                        for token in value.payload]
            for row in wrap_list(f"  {key} = ", elements):
                lines.append(row)
        elif value.kind == "scalar":
            lines.extend(with_trailer(
                f"  {key} = "
                f"{lua_scalar(value.payload, where + '.' + key)},",
                item.trailer))
        elif value.kind == "block":
            lines.extend(render_literal_block(f"  {key} = ", value.payload,
                                              where + "." + key))
        else:
            raise MigrationError(f"{where}.{key}: unsupported value shape")
        anchors[f"{family_id}/{key}"] = len(lines)
    if not hook_specials_seen and reg is not None and reg.specials:
        raise MigrationError(
            f"{where}: the script registers specials casts but the descriptor "
            f"declares no specials")

    if reg is not None and reg.hooks:
        lines.append("")
        for hook in reg.hooks:
            if hook.key == "do_special":
                raise MigrationError(
                    f"{where}: a function-form do_special is a default_cast "
                    f"in v3, and this family declares no specials to hang it "
                    f"under")
            for comment in hook.comments:
                lines.append("  " + comment)
            text = f"  {hook.key} = {hook.value.payload},"
            trailer = reg.hook_trailers.get(hook.key, "")
            lines.append(text + ("  " + trailer if trailer else ""))
    lines.append("})")
    return lines, anchors, special_ids


def lower_do_special(reg: Registration, path: str) -> None:
    """v2's function-form `do_special` is v3's specials-level default_cast.

    Same dispatch: the bind pass stores it under the specials table's
    `default` key, which is the key begin_special falls through to for every
    slot the table does not name — exactly what a bare function did.
    """
    for position, hook in enumerate(reg.hooks):
        if hook.key != "do_special":
            continue
        if any(item.key == "default" for item in reg.specials):
            raise MigrationError(
                f"{path}: {reg.family_id} registers both a function-form "
                f"do_special and a specials `default`")
        reg.hooks.pop(position)
        reg.specials.append(Item(hook.comments, "default", hook.value))
        trailer = reg.hook_trailers.pop("do_special", "")
        if trailer:
            reg.special_trailers["default"] = trailer
        return


# --- one merged file -----------------------------------------------------


class Merge:
    def __init__(self, out_path: Path, script_path: Path,
                 yaml_paths: List[Path]) -> None:
        self.out_path = out_path
        self.script_path = script_path
        self.yaml_paths = yaml_paths
        self.text = ""
        self.anchors: Dict[str, int] = {}
        self.line_map: Dict[int, int] = {}
        self.script_refs: Dict[Path, str] = {}   # descriptor -> what the
                                                 # script called the family


def merge_one(merge: Merge, docs: Dict[Path, Tuple[str, List[Item]]],
              script_text: str, registry: Registry) -> None:
    rel_script = rel(merge.script_path)
    prefix, registrations, fillers = split_script(script_text, rel_script)
    # Keyed by descriptor, not by the string the script wrote: golem.lua's
    # "core:beast" reaches wire id 18 through the display-name pass.
    by_path = {registry.resolve(reg.family_id): reg for reg in registrations}
    out: List[str] = list(prefix)
    for index, line in enumerate(prefix):
        merge.line_map[index + 1] = index + 1

    order_of_yaml = []
    for yaml_path in merge.yaml_paths:
        order, entry = docs[yaml_path]
        family_id = item_scalar(entry, "id")
        order_of_yaml.append((yaml_path, order, entry, family_id))

    used: List[str] = []
    for position, (yaml_path, order, entry, family_id) in \
            enumerate(order_of_yaml):
        reg = by_path.get(yaml_path)
        if reg is not None:
            if reg.order != order:
                raise MigrationError(
                    f"{rel_script}: {family_id} is registered as order "
                    f"'{reg.order}' but declared as '{order}'")
            lower_do_special(reg, rel_script)
            used.append(reg.family_id)
            merge.script_refs[yaml_path] = reg.family_id
        if position == 0:
            if out and out[-1].strip():
                out.append("")
        else:
            filler = fillers[position] if position < len(fillers) else [""]
            out.extend(filler if filler else [""])
        lines, anchors, _ = render_family(entry, order, reg, rel(yaml_path))
        base = len(out)
        for key, offset in anchors.items():
            merge.anchors[key] = base + offset
        out.extend(lines)
    unused = [reg.family_id for reg in registrations
              if reg.family_id not in used]
    if unused:
        raise MigrationError(
            f"{rel_script}: registers {', '.join(unused)}, which none of the "
            f"given descriptors declare")
    merge.text = "\n".join(out) + "\n"


def merge_manifest(merge: Merge, manifest: Manifest, script_text: str) -> None:
    """A whole-pack classpack.yaml plus its one script, into one v3 file.

    Layout: the script's own leading comment block stays at line 1 (it names
    the family), then the pack banner, og.pack and og.anims — a reader meets
    the pack before its behavior — then the rest of the script verbatim, then
    og.family.
    """
    rel_script = rel(merge.script_path)
    prefix, registrations, _ = split_script(script_text, rel_script)
    if len(registrations) > 1:
        raise MigrationError(
            f"{rel_script}: a manifest merge takes one registration")
    reg = registrations[0] if registrations else None
    where = rel(merge.yaml_paths[0])
    if reg is not None:
        lower_do_special(reg, rel_script)
        merge.script_refs[merge.yaml_paths[0]] = reg.family_id

    head = 0
    while head < len(prefix) and prefix[head].lstrip().startswith("--"):
        head += 1
    out: List[str] = list(prefix[:head])
    for line in manifest.header:
        out.append("--" + line[1:])
    section(out, render_pack(manifest.pack, where))
    section(out, render_anims(manifest.anims, where))
    for offset, line in enumerate(prefix[:head]):
        merge.line_map[offset + 1] = offset + 1
    for offset, line in enumerate(prefix[head:]):
        if not out or out[-1].strip() or line.strip():
            out.append(line)
            merge.line_map[head + offset + 1] = len(out)
    lines, anchors, _ = render_family(manifest.entry, manifest.order, reg,
                                      where)
    base = len(out)
    for key, offset in anchors.items():
        merge.anchors[key] = base + offset
    if out and out[-1].strip():
        out.append("")
    out.extend(lines)
    merge.text = "\n".join(out) + "\n"


def section(out: List[str], lines: List[str]) -> None:
    if not lines:
        return
    if out and out[-1].strip():
        out.append("")
    out.extend(lines)


def rel(path: Path) -> str:
    resolved = path.resolve()
    if resolved.is_relative_to(REPO_ROOT):
        return str(resolved.relative_to(REPO_ROOT))
    return str(path)


def prove(merge: Merge, script_text: str, yaml_texts: Dict[Path, str],
          claim_headers: bool = True) -> List[str]:
    """Carry proof + one comment-supersequence check per SOURCE."""
    new_lines = merge.text.splitlines()
    script_lines = script_text.splitlines()
    for old, new in merge.line_map.items():
        if script_lines[old - 1] != new_lines[new - 1]:
            raise MigrationError(
                f"{rel(merge.script_path)}: carried line {old} changed: "
                f"{script_lines[old - 1]!r} -> {new_lines[new - 1]!r}")
    new_comments = lua_comments(merge.text)
    report: List[str] = []
    script_comments = lua_comments(script_text)
    if not is_subsequence(script_comments, new_comments):
        raise MigrationError(
            f"{rel(merge.script_path)}: the merged file's comment stream is "
            f"not a supersequence of the script's")
    for yaml_path, text in yaml_texts.items():
        comments = yaml_comments(text)
        if not claim_headers:
            if not is_subsequence(comments, new_comments):
                raise MigrationError(
                    f"{rel(yaml_path)}: the merged file's comment stream is "
                    f"not a supersequence of this manifest's")
            continue
        if not comments:
            raise MigrationError(f"{rel(yaml_path)}: no header comment")
        claimed = comments[0]          # the line-1 `split from classpack.yaml`
        if "split from classpack.yaml" not in claimed:
            raise MigrationError(
                f"{rel(yaml_path)}: line 1 is not the v2 split header, so "
                f"claiming it would drop a comment this tool did not write: "
                f"{claimed!r}")
        rest = comments[1:]
        if not is_subsequence(rest, new_comments):
            raise MigrationError(
                f"{rel(yaml_path)}: the merged file's comment stream is not a "
                f"supersequence of this descriptor's")
        report.append(f"    claimed {rel(yaml_path)}:1 {claimed!r} -> dropped "
                      f"(the script's header names the family)")
    return report


def check_ids_survive(merge: Merge, ids: Dict[Path, str]) -> None:
    """Every merged family is still named by a comment in the output.

    The name that counts is any the family answers to: its declared id, the
    local part of it, or the reference the script itself used to reach it
    (golem.lua says "core:beast" and always has).
    """
    comments = " ".join(lua_comments(merge.text))
    for yaml_path, family_id in ids.items():
        spellings = [family_id, family_id.split(":", 1)[-1]]
        alias = merge.script_refs.get(yaml_path)
        if alias:
            spellings += [alias, alias.split(":", 1)[-1]]
        if not any(spelling in comments for spelling in spellings):
            raise MigrationError(
                f"{rel(merge.out_path)}: nothing in the merged file's comments "
                f"names {family_id}; the claimed YAML header was the only "
                f"place it was written")


# --- driver --------------------------------------------------------------


def normalize_family_name(raw: str) -> str:
    """og::families::normalize_family_name — lowercase, spaces to underscores."""
    return "".join("_" if char == " " else char.lower() for char in raw)


class Registry:
    """The declared families, keyed the way the engine keys them.

    `resolve` is og::families::resolve_family_string_id
    (src/gameplay/families/family_string_ids.cpp) restricted to the
    descriptors this run was given: an exact match on the DECLARED id first,
    then — the back-compat pass — the local part against the registry
    DISPLAY name, lowest wire id winning.
    """

    def __init__(self) -> None:
        self.by_id: Dict[str, Path] = {}
        self.by_name: List[Tuple[int, str, Path]] = []

    def add(self, family_id: str, name: str, wire_id: str, path: Path) -> None:
        self.by_id[normalize_family_name(family_id)] = path
        slot = int(wire_id) if wire_id.isdigit() else 1 << 30
        self.by_name.append((slot, normalize_family_name(name.strip('"')),
                             path))

    def resolve(self, ref: str) -> Optional[Path]:
        found = self.by_id.get(normalize_family_name(ref))
        if found is not None:
            return found
        local = ref.split(":", 1)[1] if ":" in ref else ref
        want = normalize_family_name(local)
        for _, name, path in sorted(self.by_name, key=lambda row: row[0]):
            if name == want:
                return path
        return None


def script_for(script_dir: Path, registry: Registry) -> Dict[
        Path, List[Path]]:
    """script path -> the descriptors it speaks for.

    A script speaks for the families it registers hooks for; a script with
    no registration (the two descriptor-only beasts) speaks for the id in
    its line-1 header, which the style contract puts there. golem.lua is the
    one shipped script that reaches its family through the display-name
    pass: it says "core:beast" and lands on the lowest BEAST, wire id 18,
    whose declared id is "core:#18".
    """
    out: Dict[Path, List[Path]] = {}
    for path in sorted(script_dir.glob("*.lua")):
        text = path.read_text()
        refs = [match.group(2)
                for match in (REGISTER_RE.match(line)
                              for line in text.splitlines())
                if match is not None]
        if not refs:
            head = text.splitlines()[0] if text else ""
            found = re.search(r"--\s+([a-z0-9_]+:[^\s]+)", head)
            if found is None:
                continue
            refs = [found.group(1)]
        seen: List[Path] = []
        for ref in refs:
            target = registry.resolve(ref)
            if target is not None and target not in seen:
                seen.append(target)
        if seen:
            out[path] = seen
    return out


def migrate_manifest(yaml_path: Path, dry_run: bool) -> int:
    """A pack that kept everything in classpack.yaml: one merged family file."""
    pack_root = yaml_path.resolve().parent
    try:
        text = yaml_path.read_text()
        manifest = read_pack_doc(text, rel(yaml_path))
        family_id = item_scalar(manifest.entry, "id")
        if family_id is None:
            raise MigrationError(f"{rel(yaml_path)}: no id")
        registry = Registry()
        registry.add(family_id, item_scalar(manifest.entry, "name") or "",
                     item_scalar(manifest.entry, "wire_id") or "", yaml_path)
        speaks_for = script_for(pack_root / "scripts", registry)
        if len(speaks_for) != 1:
            raise MigrationError(
                f"{rel(yaml_path)}: expected exactly one script under "
                f"scripts/, found {len(speaks_for)}")
        script_path = next(iter(speaks_for))
        out_path = pack_root / "families" / script_path.name
        merge = Merge(out_path, script_path, [yaml_path])
        script_text = script_path.read_text()
        merge_manifest(merge, manifest, script_text)
        prove(merge, script_text, {yaml_path: text}, claim_headers=False)
        check_ids_survive(merge, {yaml_path: family_id})
    except MigrationError as error:
        print(f"REFUSED {error}", file=sys.stderr)
        return 1
    print(f"{rel(merge.out_path)}: {len(script_text.splitlines())} script "
          f"lines + the pack manifest -> {len(merge.text.splitlines())} lines")
    if dry_run:
        print("dry run: 1 file would be written")
        return 0
    out_path.parent.mkdir(exist_ok=True)
    out_path.write_text(merge.text)
    yaml_path.unlink()
    script_path.unlink()
    try:
        script_path.parent.rmdir()
    except OSError:
        pass
    print(f"wrote {rel(out_path)}; removed {rel(yaml_path)} and "
          f"{rel(script_path)}")
    return 0


# --- the non-living half: data-only declarations + lib/ modules ----------
#
# A weapon, effect, treasure or generator has no per-family script: its
# behavior arrives in a SHARED one (weapon_projectiles.lua speaks for the
# fire arrow and the boulder; treasure_consumables.lua for six potions). So
# the living merge's one-file-per-script shape does not apply. Instead each
# descriptor becomes its own declaration and every shared script becomes a
# lib/ module — body verbatim, `og.register_hooks` trailer replaced by the
# export table — which the declarations reference inline:
#
#     local projectiles = og.use("weapon_projectiles")
#     og.family("weapon", { ..., on_death = projectiles.explode_on_death })


def lib_aliases(modules: Sequence[str]) -> Dict[str, str]:
    """The local name each module gets at its `og.use` sites.

    A module is named for its order and its subject (`treasure_consumables`),
    and inside a declaration the order is already in the file name, so the
    subject alone reads best: `on_eat = consumables.drumstick_on_eat`.
    Initials do not: `effect_cloud` and `effect_chain` are both `ec`, and an
    alias that is ambiguous ACROSS files is exactly what makes a
    line-anchored mutation pin land on the wrong declaration. Two subjects
    could collide too (a pack with `weapon_wave` and `effect_wave`), so that
    case falls back to the whole module name.
    """
    subject = {name: name.split("_", 1)[-1] for name in modules}
    taken: Dict[str, int] = {}
    for short in subject.values():
        taken[short] = taken.get(short, 0) + 1
    return {name: short if taken[short] == 1 else name
            for name, short in subject.items()}


def data_header(family_id: str, order: str,
                module: Optional["LibModule"]) -> str:
    """Line 1 of a data-only declaration.

    The v2 header said "split from classpack.yaml", which stopped being true
    the moment the file it was split from was deleted, and its "(families
    load in sorted filename order)" tail is now stated once in the pack
    header instead of 52 times. What replaces both is the style contract's
    S2 line: this file's scope, what it does, and the cookbook.
    """
    behavior = (f", behavior in lib/{module.name}.lua"
                if module is not None else "")
    return (f"-- {family_id} — the {order} declaration{behavior} "
            f"(cookbook: docs/lua-classpacks-design.md §3).")


class LibModule:
    """A shared behavior script on its way to lib/."""

    def __init__(self, path: Path, text: str, pack_id: str,
                 alias: str) -> None:
        self.path = path
        self.text = text
        self.pack_id = pack_id
        self.name = path.stem
        self.alias = alias
        self.prefix, self.registrations, _ = split_script(text, rel(path))
        self.exports: List[str] = []
        self.uses: List[Tuple[str, str, str]] = []   # family, hook, function
        for reg in self.registrations:
            if reg.specials:
                raise MigrationError(
                    f"{rel(path)}: {reg.family_id} registers specials casts, "
                    f"which only a living family has")
            if not reg.hooks:
                raise MigrationError(
                    f"{rel(path)}: {reg.family_id} registers no hooks")
            for hook in reg.hooks:
                function = hook.value.payload
                if not LUA_ID_RE.match(function):
                    raise MigrationError(
                        f"{rel(path)}: {reg.family_id}.{hook.key} is "
                        f"{function!r}, not a plain function name a module "
                        f"can export")
                if function not in self.exports:
                    self.exports.append(function)
                self.uses.append((reg.family_id, hook.key, function))
        self.module_text = self.render()

    def render(self) -> str:
        body = list(self.prefix)
        while body and not body[-1].strip():
            body.pop()
        out = body + [""]
        # What the register_hooks trailer said — which families this file
        # speaks for, and through which hook — kept as the export block's
        # own comment, because the declarations are now somewhere else.
        out.append(f"-- The declarations that reference this module "
                   f"(packs/{self.pack_id}/families/):")
        width = max(len(family_id) for family_id, _, _ in self.uses)
        for family_id, hook, function in self.uses:
            out.append(f"--   {family_id.ljust(width)}  {hook} = "
                       f"{self.alias}.{function}")
        out.append("return {")
        for name in self.exports:
            out.append(f"  {name} = {name},")
        out.append("}")
        return "\n".join(out) + "\n"

    def prove(self) -> None:
        out = self.module_text.splitlines()
        body = list(self.prefix)
        while body and not body[-1].strip():
            body.pop()
        for index, line in enumerate(body):
            if out[index] != line:
                raise MigrationError(
                    f"{rel(self.path)}: carried line {index + 1} changed: "
                    f"{line!r} -> {out[index]!r}")
        if not is_subsequence(lua_comments(self.text),
                              lua_comments(self.module_text)):
            raise MigrationError(
                f"{rel(self.path)}: the module's comment stream is not a "
                f"supersequence of the script's")

    def namespace_hooks(self) -> None:
        """Point every hook value at the module: `f` -> `wp.f`. Once."""
        for reg in self.registrations:
            for hook in reg.hooks:
                hook.value = Value("lua", f"{self.alias}.{hook.value.payload}")


def render_data_family(entry: List[Item], order: str,
                       module: Optional[LibModule],
                       reg: Optional[Registration], path: str) -> List[str]:
    """One non-living declaration: header, the og.use it needs, og.family."""
    family_id = item_scalar(entry, "id")
    if family_id is None:
        raise MigrationError(f"{path}: the family entry has no id")
    lines = [data_header(family_id, order, module), ""]
    if module is not None:
        lines.append(f'local {module.alias} = og.use("{module.name}")')
        lines.append("")
    lines.extend(render_family(entry, order, reg, path)[0])
    return lines


def prove_data_family(text: str, yaml_text: str, yaml_path: Path,
                      header: str) -> str:
    """The comment supersequence for one declaration, with its one claim."""
    comments = yaml_comments(yaml_text)
    if not comments:
        raise MigrationError(f"{rel(yaml_path)}: no header comment")
    if "split from classpack.yaml" not in comments[0]:
        raise MigrationError(
            f"{rel(yaml_path)}: line 1 is not the v2 split header, so "
            f"rewriting it would change a comment this tool did not write: "
            f"{comments[0]!r}")
    new_comments = lua_comments(text)
    if header not in new_comments:
        raise MigrationError(
            f"{rel(yaml_path)}: the rewritten header is not in the output")
    if not is_subsequence(comments[1:], new_comments):
        raise MigrationError(
            f"{rel(yaml_path)}: the declaration's comment stream is not a "
            f"supersequence of the descriptor's")
    return (f"    claimed {rel(yaml_path)}:1 {comments[0]!r}\n"
            f"            -> {header!r}")


def migrate_data_only(files: Sequence[Path], dry_run: bool) -> int:
    """Every given descriptor becomes its own declaration; scripts to lib/."""
    docs: Dict[Path, Tuple[str, List[Item]]] = {}
    texts: Dict[Path, str] = {}
    registry = Registry()
    failures = 0
    for yaml_path in files:
        try:
            text = yaml_path.read_text()
            order, entry = read_family_doc(text, rel(yaml_path))
            if order == "living":
                raise MigrationError(
                    f"{rel(yaml_path)}: a living family merges with its own "
                    f"script; --data-only is the non-living shape")
            family_id = item_scalar(entry, "id")
            if family_id is None:
                raise MigrationError(f"{rel(yaml_path)}: no id")
            docs[yaml_path] = (order, entry)
            texts[yaml_path] = text
            registry.add(family_id, item_scalar(entry, "name") or "",
                         item_scalar(entry, "wire_id") or "", yaml_path)
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1
    if failures:
        return 1

    pack_root = next(iter(docs)).resolve().parent.parent
    pack_id = pack_root.name
    speaks_for = script_for(pack_root / "scripts", registry)
    aliases = lib_aliases([path.stem for path in speaks_for])
    modules: Dict[Path, LibModule] = {}
    yaml_to_reg: Dict[Path, Tuple[LibModule, Registration]] = {}
    for script_path in sorted(speaks_for):
        try:
            module = LibModule(script_path, script_path.read_text(), pack_id,
                               aliases[script_path.stem])
            module.prove()
            target = pack_root / "lib" / script_path.name
            if target.exists():
                raise MigrationError(
                    f"{rel(script_path)}: {rel(target)} already exists")
            for reg in module.registrations:
                yaml_path = registry.resolve(reg.family_id)
                if yaml_path is None or yaml_path not in docs:
                    raise MigrationError(
                        f"{rel(script_path)}: registers {reg.family_id}, "
                        f"whose descriptor is not in this run — a partial "
                        f"move would leave the hook behind")
                yaml_to_reg[yaml_path] = (module, reg)
            module.namespace_hooks()
            modules[script_path] = module
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1
    if failures:
        return 1

    written: Dict[Path, str] = {}
    claims: List[str] = []
    for yaml_path in sorted(docs):
        order, entry = docs[yaml_path]
        module, reg = yaml_to_reg.get(yaml_path, (None, None))
        try:
            lines = render_data_family(entry, order, module, reg,
                                       rel(yaml_path))
            text = "\n".join(lines) + "\n"
            claims.append(prove_data_family(text, texts[yaml_path], yaml_path,
                                            lines[0]))
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1
            continue
        written[yaml_path.with_suffix(".lua")] = text
    if failures:
        return 1

    for out_path in sorted(written):
        print(f"{rel(out_path)}: {len(written[out_path].splitlines())} lines")
    for module in modules.values():
        print(f"{rel(pack_root / 'lib' / module.path.name)}: "
              f"{len(module.module_text.splitlines())} lines, exports "
              f"{', '.join(module.exports)}")
    for claim in claims:
        print(claim)
    if dry_run:
        print(f"dry run: {len(written)} declarations and {len(modules)} "
              f"modules would be written")
        return 0
    for out_path, text in written.items():
        out_path.write_text(text)
    for yaml_path in docs:
        if yaml_path.with_suffix(".lua") != yaml_path:
            yaml_path.unlink()
    for script_path, module in modules.items():
        (pack_root / "lib" / script_path.name).write_text(module.module_text)
        script_path.unlink()
    try:
        (pack_root / "scripts").rmdir()
    except OSError:
        pass
    print(f"wrote {len(written)} declarations and {len(modules)} lib "
          f"modules; removed {len(docs)} descriptors and {len(modules)} "
          f"scripts")
    return 0


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Merge class-pack families from YAML+script into v3 Lua.")
    parser.add_argument("files", nargs="+", type=Path,
                        help="the families/*.yaml files to merge")
    parser.add_argument("--dry-run", action="store_true",
                        help="report only; write and delete nothing")
    parser.add_argument("--check", action="store_true",
                        help="re-prove an already-merged tree (no YAML left "
                             "and a families/*.lua in its place)")
    parser.add_argument("--data-only", action="store_true",
                        help="the non-living shape: one declaration per "
                             "descriptor, shared scripts moved to lib/")
    parser.add_argument("--linemap", type=Path, default=None,
                        help="write the old->new line map and key anchors as "
                             "JSON (input for the pin re-anchor)")
    args = parser.parse_args(list(argv))

    if args.check:
        return run_check(args.files)

    if args.data_only:
        return migrate_data_only(args.files, args.dry_run)

    manifests = [path for path in args.files if path.name == "classpack.yaml"]
    if manifests:
        if len(args.files) != 1:
            print("REFUSED a classpack.yaml is a whole pack: migrate it on "
                  "its own", file=sys.stderr)
            return 1
        return migrate_manifest(manifests[0], args.dry_run)

    docs: Dict[Path, Tuple[str, List[Item]]] = {}
    texts: Dict[Path, str] = {}
    registry = Registry()
    failures = 0
    for yaml_path in args.files:
        try:
            text = yaml_path.read_text()
            order, entry = read_family_doc(text, rel(yaml_path))
            family_id = item_scalar(entry, "id")
            if family_id is None:
                raise MigrationError(f"{rel(yaml_path)}: no id")
            docs[yaml_path] = (order, entry)
            texts[yaml_path] = text
            registry.add(family_id, item_scalar(entry, "name") or "",
                         item_scalar(entry, "wire_id") or "", yaml_path)
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1
    if failures:
        return 1

    pack_root = next(iter(docs)).resolve().parent.parent
    speaks_for = script_for(pack_root / "scripts", registry)
    yaml_to_script: Dict[Path, Path] = {}
    for script_path, yaml_paths in speaks_for.items():
        for yaml_path in yaml_paths:
            yaml_to_script[yaml_path] = script_path

    groups: Dict[Optional[Path], List[Path]] = {}
    for yaml_path in docs:
        groups.setdefault(yaml_to_script.get(yaml_path), []).append(yaml_path)
    if None in groups:
        for orphan in sorted(groups.pop(None)):
            print(f"REFUSED {rel(orphan)}: no script speaks for this family",
                  file=sys.stderr)
            failures += 1
        if failures:
            return 1

    merges: List[Merge] = []
    for script_path, yaml_paths in sorted(groups.items(),
                                          key=lambda kv: kv[0].name):
        yaml_paths.sort()
        merge = Merge(yaml_paths[0].with_suffix(".lua"), script_path,
                      yaml_paths)
        try:
            merge_one(merge, docs, script_path.read_text(), registry)
            claims = prove(merge, script_path.read_text(),
                           {p: texts[p] for p in yaml_paths})
            check_ids_survive(
                merge, {p: item_scalar(docs[p][1], "id") or ""
                        for p in yaml_paths})
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1
            continue
        merges.append(merge)
        print(f"{rel(merge.out_path)}: {len(merge.script_path.read_text().splitlines())}"
              f" script lines + {len(yaml_paths)} descriptor(s) -> "
              f"{len(merge.text.splitlines())} lines")
        for claim in claims:
            print(claim)
    if failures:
        return 1

    if args.dry_run:
        print(f"dry run: {len(merges)} files would be written")
        return 0

    for merge in merges:
        merge.out_path.write_text(merge.text)
        for yaml_path in merge.yaml_paths:
            if yaml_path != merge.out_path:
                yaml_path.unlink()
        merge.script_path.unlink()
    print(f"wrote {len(merges)} merged files; removed "
          f"{sum(len(m.yaml_paths) for m in merges)} descriptors and "
          f"{len(merges)} scripts")

    if args.linemap is not None:
        payload = {
            rel(merge.out_path): {
                "script": rel(merge.script_path),
                "descriptors": [rel(p) for p in merge.yaml_paths],
                "lines": {str(old): new
                          for old, new in sorted(merge.line_map.items())},
                "anchors": merge.anchors,
            }
            for merge in merges
        }
        args.linemap.write_text(json.dumps(payload, indent=1, sort_keys=True)
                                + "\n")
        print(f"line map: {rel(args.linemap)}")
    return 0


def run_check(files: Sequence[Path]) -> int:
    """A merged tree re-proves itself: no YAML, and a declaration in its place."""
    failures = 0
    for path in files:
        target = path.with_suffix(".lua") if path.suffix == ".yaml" else path
        if path.suffix == ".yaml" and path.exists():
            print(f"REFUSED {rel(path)}: still ships a v2 descriptor",
                  file=sys.stderr)
            failures += 1
            continue
        if not target.exists():
            print(f"REFUSED {rel(target)}: missing", file=sys.stderr)
            failures += 1
            continue
        text = target.read_text()
        if "og.pack{" in text and "og.family(" not in text:
            # A pack that keeps its header in its own families/ chunk: legal,
            # og.pack is a family-chunk call and the last one wins.
            print(f"{rel(target)}: schema v3 (the pack header)")
            continue
        if "og.family(" not in text:
            print(f"REFUSED {rel(target)}: declares no family",
                  file=sys.stderr)
            failures += 1
            continue
        print(f"{rel(target)}: schema v3 (declaration and behavior in one "
              f"file)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
