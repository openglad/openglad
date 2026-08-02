#!/usr/bin/env python3
"""Rewrite a class pack's living entries from schema v1 to schema v2.

WHAT IT DOES
------------
A v1 living entry keeps its numbers in six positional arrays plus two loose
scalars; v2 keeps them in named blocks (docs/lua-classpacks-design.md §4):

    base_stats: [12, 6, 12, 8, 9, 1]        stats:
    hiring_cost: 250                          strength: 12   ... level: 1
    derived_bonuses: [120, 0, 20, 0, ...]   combat:
    stat_costs: [6, 10, 6, 25, 50, 200]       hp: 120  melee_damage: 20
    special_costs: [5000, 25, ...]            stepsize: 4  fire_delay: 6
    weapon_cost: 2                            fire_mp_cost: 2
    special_names: ["NONE", "CHARGE", ...]  costs:
    alternate_names: ["NONE", ...]            hire: 250
                                              train: {6 axes}
                                            specials:
                                              - id/name/mp_cost/alternate

This script performs that rewrite mechanically, and re-anchors the parity
mutation pins that named the lines it moved.

The engine no longer reads the v1 spellings: each of the eight keys is
refused by name with a line saying where its value moved, so an unmigrated
pack fails to load rather than installing a family of registry defaults.
This script is what that error points at.

WHY IT IS A SCRIPT AND NOT A HAND EDIT
--------------------------------------
21 near-identical files times ~40 scalars each is the exact shape that
produces a transcription slip nobody sees: the design strawman this schema
came from misread `weapon_cost: 2` as `25` (the adjacent line's special
cost) while writing with the file open. So:

  * SCALARS ARE CARRIED AS TEXT, never parsed and re-printed. `7.5` moves as
    the three characters `7.5`; nothing in this script can turn `120` into
    `120.0` or `1` into `1.0`.
  * EVERY INPUT LINE THE REWRITE DOES NOT CLAIM survives verbatim and in
    order (--check re-proves this after writing). Since the claimed lines
    are exactly the eight v1 key lines, that carry-proof is also the comment
    proof: the output's comment stream is a supersequence of the input's.
  * The value-preservation preconditions for dropping slot 0 and the trailing
    "NONE" slots are ASSERTED per file, not assumed: a slot's name is "NONE"
    if and only if its cost is the 5000 sentinel, and no disabled slot
    carries an alternate name.

WHAT IT REFUSES
---------------
  * A non-zero value in one of the four dead derived_bonuses columns
    (MP/ranged_damage/range/defense). Those columns have no reader anywhere,
    so v2 has no key for them and the value would silently vanish. Pass
    --drop-dead-axis to drop it loudly instead (the emberwisp doc example
    ships `40` in the MP column next to the live init_max_magicpoints: 40,
    which is the cargo-cult this schema exists to end).
  * A special name that does not reduce to a legal id ([a-z0-9_]+).
  * A pin it cannot re-anchor, or a re-anchored pin whose from-text does not
    occur exactly once on its new line (that is the applier's rule —
    scripts/parity/_apply_mutation.py refuses zero and refuses two).

USAGE
-----
    scripts/migrate_classpack_v2.py packs/core/families/living-*.yaml
    scripts/migrate_classpack_v2.py --dry-run <files>      # report only
    scripts/migrate_classpack_v2.py --check <files>        # re-prove, no write
    scripts/migrate_classpack_v2.py --pin-table <hdr> ...  # extra pin tables
                                                           # (repeatable)

Exit 0 when every file migrated and every pin re-anchored, 1 otherwise.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PIN_TABLES = ("tests/parity/scenario_table.h",)

AXES = ("strength", "dexterity", "constitution", "intelligence", "armor",
        "level")

# v1 keys whose value is a flow sequence, and the element count each must
# carry. The installer clamped longer ones silently in v1; here a wrong
# length is a refusal, because the migration has to know which column is
# which.
V1_SEQ_KEYS = {
    "base_stats": 6,
    "derived_bonuses": 8,
    "stat_costs": 6,
    "special_costs": 6,
    "special_names": 6,
    "alternate_names": 6,
}
V1_SCALAR_KEYS = ("hiring_cost", "weapon_cost")
V1_KEYS = tuple(V1_SEQ_KEYS) + V1_SCALAR_KEYS

# derived_bonuses column -> combat: member. The other four columns had no
# reader in any build of this engine.
LIVE_DERIVED = ((0, "hp"), (2, "melee_damage"), (6, "stepsize"),
                (7, "fire_delay"))
DEAD_DERIVED = {1: "mp", 3: "ranged_damage", 4: "range", 5: "defense"}

SPECIAL_NAME_NONE = "NONE"
SPECIAL_COST_DISABLED = "5000"

ID_RE = re.compile(r"^[a-z0-9_]+$")
ENTRY_START_RE = re.compile(r"^(\s*)- id:\s")
KEY_LINE_RE = re.compile(r"^(\s*)([A-Za-z_][A-Za-z0-9_]*):(.*)$")


class MigrationError(Exception):
    """A refusal, reported against one file."""


# --- scalar handling -----------------------------------------------------


def unquote(token: str) -> str:
    """The text of a YAML scalar token, for comparisons only.

    The token itself is what gets written back out; this is used to ask
    "is this slot NONE" without deciding how NONE was spelled.
    """
    if len(token) >= 2 and token[0] == token[-1] and token[0] in "\"'":
        return token[1:-1]
    return token


def flow_tokens(text: str, where: str) -> List[str]:
    """Split a flow sequence's interior into verbatim element substrings.

    Quote-aware so `["NONE", "WARP SPACE"]` splits into two elements, and
    substring-exact so nothing is normalised on the way through.
    """
    body = text.strip()
    if not (body.startswith("[") and body.endswith("]")):
        raise MigrationError(f"{where}: expected a flow sequence, got {text!r}")
    body = body[1:-1]
    if not body.strip():
        return []
    tokens: List[str] = []
    start = 0
    quote = ""
    index = 0
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
        elif char == ",":
            tokens.append(body[start:index].strip())
            start = index + 1
        index += 1
    if quote:
        raise MigrationError(f"{where}: unterminated quote in {text!r}")
    tokens.append(body[start:].strip())
    return tokens


def scalar_token(text: str, where: str) -> str:
    """The verbatim scalar after `key:`, refusing anything with a comment."""
    value = text.strip()
    if not value:
        raise MigrationError(f"{where}: expected a scalar value")
    if "#" in value:
        raise MigrationError(
            f"{where}: an inline comment on a rewritten key would be lost: "
            f"{text!r}")
    return value


def comment_stream(text: str) -> List[str]:
    """Every comment in a YAML text, in order, as `line:text` is irrelevant.

    A `#` counts as a comment start only outside quotes and only at the
    start of a line or after whitespace — `id: core:#8` is an id, not a
    comment, and six living files ship one.
    """
    out: List[str] = []
    for line in text.splitlines():
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
            elif char == "#" and (index == 0 or line[index - 1].isspace()):
                out.append(line[index:].rstrip())
                break
            index += 1
    return out


def is_subsequence(needle: Sequence[str], haystack: Sequence[str]) -> bool:
    it = iter(haystack)
    return all(any(item == candidate for candidate in it) for item in needle)


# --- the rewrite ---------------------------------------------------------


class Field:
    """One emitted v2 line, addressable by the pin translator.

    `path` is the block path a v1 column maps to ("combat.hp",
    "costs.train.armor", "specials.1.mp_cost"); `key` is the leaf key as
    written; `offset` is the line's index inside the emitted block.
    """

    def __init__(self, path: str, key: str, offset: int) -> None:
        self.path = path
        self.key = key
        self.offset = offset


class Entry:
    """One living entry's v1 key lines and the v2 block that replaces them."""

    def __init__(self, indent: str, entry_id: str) -> None:
        self.indent = indent            # the key indent ("      ")
        self.entry_id = entry_id
        self.lines: Dict[str, int] = {}     # v1 key -> 0-based line index
        self.tokens: Dict[str, List[str]] = {}
        self.scalars: Dict[str, str] = {}
        self.block: List[str] = []
        self.fields: Dict[str, Field] = {}
        self.block_start = -1               # 0-based line index in the output
        self.special_ids: List[Tuple[int, str, str]] = []  # slot, id, name


def collect_entries(lines: List[str], path: str) -> List[Entry]:
    entries: List[Entry] = []
    current: Optional[Entry] = None
    current_key_indent = ""
    for index, line in enumerate(lines):
        start = ENTRY_START_RE.match(line)
        if start:
            entry_id = line.split(":", 1)[1].strip()
            current = Entry(start.group(1) + "  ", entry_id)
            current_key_indent = current.indent
            entries.append(current)
            continue
        match = KEY_LINE_RE.match(line)
        if not match:
            continue
        indent, key, rest = match.group(1), match.group(2), match.group(3)
        if key not in V1_KEYS:
            continue
        if current is None or indent != current_key_indent:
            raise MigrationError(
                f"{path}:{index + 1}: `{key}` outside a living entry")
        if key in current.lines:
            raise MigrationError(
                f"{path}:{index + 1}: `{key}` declared twice in "
                f"{current.entry_id}")
        current.lines[key] = index
        where = f"{path}:{index + 1} {key}"
        if key in V1_SEQ_KEYS:
            tokens = flow_tokens(rest, where)
            if len(tokens) != V1_SEQ_KEYS[key]:
                raise MigrationError(
                    f"{where}: expected {V1_SEQ_KEYS[key]} elements, "
                    f"found {len(tokens)}")
            current.tokens[key] = tokens
        else:
            current.scalars[key] = scalar_token(rest, where)
    return [e for e in entries if e.lines]


def derive_special_id(name: str, taken: Sequence[str], where: str,
                      warnings: List[str]) -> str:
    base = name.lower().replace(" ", "_")
    if not ID_RE.match(base):
        raise MigrationError(
            f"{where}: special name {name!r} does not reduce to an id "
            f"([a-z0-9_]+); rename it or add the id by hand")
    candidate = base
    suffix = 2
    while candidate in taken:
        candidate = f"{base}_{suffix}"
        suffix += 1
        warnings.append(
            f"{where}: two specials reduce to the id {base!r}; the later one "
            f"is {candidate!r}")
    return candidate


def render_entry(entry: Entry, path: str, drop_dead_axis: bool,
                 warnings: List[str]) -> None:
    """Fill entry.block / entry.fields / entry.special_ids."""
    missing = [k for k in V1_KEYS if k not in entry.lines]
    if missing:
        raise MigrationError(
            f"{path}: living entry {entry.entry_id} is missing "
            f"{', '.join(missing)} — a partial v1 entry has no v2 spelling")

    ind = entry.indent
    ind2 = ind + "  "
    ind3 = ind2 + "  "
    lines: List[str] = []
    fields: Dict[str, Field] = {}

    def emit(text: str, path_name: Optional[str] = None,
             key: Optional[str] = None) -> None:
        if path_name is not None:
            assert key is not None
            fields[path_name] = Field(path_name, key, len(lines))
        lines.append(text)

    # stats:
    emit(f"{ind}stats:")
    for axis, token in zip(AXES, entry.tokens["base_stats"]):
        emit(f"{ind2}{axis}: {token}", f"stats.{axis}", axis)

    # combat:
    derived = entry.tokens["derived_bonuses"]
    for column, name in DEAD_DERIVED.items():
        if derived[column] == "0":
            continue
        message = (f"{path}: {entry.entry_id} derived_bonuses[{column}] "
                   f"({name}) is {derived[column]}, and that column has no "
                   f"reader in the engine")
        if not drop_dead_axis:
            raise MigrationError(message + " — pass --drop-dead-axis to drop "
                                           "it")
        warnings.append(message + " — DROPPED")
    emit(f"{ind}combat:")
    for column, key in LIVE_DERIVED:
        emit(f"{ind2}{key}: {derived[column]}", f"combat.{key}", key)
    emit(f"{ind2}fire_mp_cost: {entry.scalars['weapon_cost']}",
         "combat.fire_mp_cost", "fire_mp_cost")

    # costs:
    emit(f"{ind}costs:")
    emit(f"{ind2}hire: {entry.scalars['hiring_cost']}", "costs.hire", "hire")
    emit(f"{ind2}train:")
    for axis, token in zip(AXES, entry.tokens["stat_costs"]):
        emit(f"{ind3}{axis}: {token}", f"costs.train.{axis}", axis)

    # specials:
    names = entry.tokens["special_names"]
    costs = entry.tokens["special_costs"]
    alternates = entry.tokens["alternate_names"]
    where = f"{path}: {entry.entry_id}"
    if unquote(names[0]) != SPECIAL_NAME_NONE or \
            costs[0] != SPECIAL_COST_DISABLED:
        raise MigrationError(
            f"{where}: slot 0 is an engine artifact the loader zeroes, so v2 "
            f"has no spelling for it, but this entry gives it "
            f"{names[0]}/{costs[0]}")
    for slot in range(6):
        disabled_name = unquote(names[slot]) == SPECIAL_NAME_NONE
        disabled_cost = costs[slot] == SPECIAL_COST_DISABLED
        if disabled_name != disabled_cost:
            raise MigrationError(
                f"{where}: slot {slot} is {names[slot]}/{costs[slot]}; "
                f"omitting a slot only preserves values when 'NONE' and the "
                f"5000 sentinel agree")
        if disabled_name and unquote(alternates[slot]) != SPECIAL_NAME_NONE:
            raise MigrationError(
                f"{where}: slot {slot} is disabled but carries the alternate "
                f"{alternates[slot]}")
    live = [slot for slot in range(1, 6)
            if unquote(names[slot]) != SPECIAL_NAME_NONE]
    if not live:
        emit(f"{ind}specials: []")
    else:
        emit(f"{ind}specials:")
        expected_slot = 1
        taken: List[str] = []
        for slot in live:
            special_id = derive_special_id(
                unquote(names[slot]), taken, f"{where} slot {slot}", warnings)
            taken.append(special_id)
            entry.special_ids.append((slot, special_id, unquote(names[slot])))
            emit(f"{ind2}- id: {special_id}",
                 f"specials.{slot}.id", "id")
            emit(f"{ind3}name: {names[slot]}", f"specials.{slot}.name", "name")
            emit(f"{ind3}mp_cost: {costs[slot]}",
                 f"specials.{slot}.mp_cost", "mp_cost")
            if slot != expected_slot:
                emit(f"{ind3}slot: {slot}", f"specials.{slot}.slot", "slot")
            if unquote(alternates[slot]) != SPECIAL_NAME_NONE:
                emit(f"{ind3}alternate: {{name: {alternates[slot]}}}",
                     f"specials.{slot}.alternate", "alternate")
            expected_slot = slot + 1

    entry.block = lines
    entry.fields = fields


def rewrite(text: str, path: str, drop_dead_axis: bool,
            warnings: List[str]) -> Tuple[str, List[Entry], Dict[int, int]]:
    """Return (new text, entries, old->new line map for carried lines).

    Line numbers in the map are 0-based indices into the line lists.
    """
    lines = text.splitlines(keepends=True)
    entries = collect_entries(lines, path)
    if not entries:
        raise MigrationError(f"{path}: no v1 living entry found")
    for entry in entries:
        render_entry(entry, path, drop_dead_axis, warnings)

    replaced: Dict[int, Entry] = {}
    for entry in entries:
        for index in entry.lines.values():
            replaced[index] = entry
    block_at = {min(entry.lines.values()): entry for entry in entries}

    out: List[str] = []
    carried: Dict[int, int] = {}
    for index, line in enumerate(lines):
        entry = block_at.get(index)
        if entry is not None:
            entry.block_start = len(out)
            out.extend(block_line + "\n" for block_line in entry.block)
        if index in replaced:
            continue
        carried[index] = len(out)
        out.append(line)
    return "".join(out), entries, carried


# --- proofs --------------------------------------------------------------


def prove(old_text: str, new_text: str, entries: List[Entry],
          carried: Dict[int, int], path: str) -> str:
    old_lines = old_text.splitlines()
    new_lines = new_text.splitlines()
    replaced = {index for entry in entries for index in entry.lines.values()}

    kept = 0
    for index, line in enumerate(old_lines):
        if index in replaced:
            continue
        target = carried[index]
        if new_lines[target] != line:
            raise MigrationError(
                f"{path}: carried line {index + 1} changed: {line!r} -> "
                f"{new_lines[target]!r}")
        kept += 1
    order = [carried[i] for i in sorted(carried)]
    if order != sorted(order):
        raise MigrationError(f"{path}: carried lines were reordered")

    old_comments = comment_stream(old_text)
    new_comments = comment_stream(new_text)
    if not is_subsequence(old_comments, new_comments):
        raise MigrationError(
            f"{path}: the output's comment stream is not a supersequence of "
            f"the input's")
    specials = [name for entry in entries for _, name, _ in entry.special_ids]
    return (f"{path}: {len(old_lines)} -> {len(new_lines)} lines; "
            f"carried {kept}/{kept} verbatim; "
            f"comments {len(old_comments)} -> {len(new_comments)} "
            f"(supersequence OK); "
            f"specials [{', '.join(specials) if specials else 'none'}]")


# --- mutation pins -------------------------------------------------------


C_ESCAPES = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\t": "\\t"}


def c_unescape(literal: str) -> str:
    body = literal[1:-1]
    out: List[str] = []
    index = 0
    while index < len(body):
        char = body[index]
        if char == "\\" and index + 1 < len(body):
            nxt = body[index + 1]
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"'}.get(nxt,
                                                                       nxt))
            index += 2
            continue
        out.append(char)
        index += 1
    return "".join(out)


def c_escape(text: str) -> str:
    return "".join(C_ESCAPES.get(char, char) for char in text)


PIN_RE = re.compile(
    r'(?P<head>"(?P<file>packs/core/families/[^"]+)",\s*)'
    r'(?P<line>\d+)'
    r'(?P<mid1>,\s*\n\s*)'
    r'(?P<from>"(?:[^"\\]|\\.)*")'
    r'(?P<mid2>,\s*\n\s*)'
    r'(?P<to>"(?:[^"\\]|\\.)*")')


class Pin:
    def __init__(self, match: "re.Match[str]") -> None:
        self.match = match
        self.file = match.group("file")
        self.line = int(match.group("line"))
        self.from_text = c_unescape(match.group("from"))
        self.to_text = c_unescape(match.group("to"))
        self.new_line = self.line
        self.new_from = self.from_text
        self.new_to = self.to_text
        self.changed = False


def field_for_column(key: str, column: int, entry: Entry,
                     where: str) -> Field:
    if key == "base_stats":
        return entry.fields[f"stats.{AXES[column]}"]
    if key == "stat_costs":
        return entry.fields[f"costs.train.{AXES[column]}"]
    if key == "derived_bonuses":
        for live_column, name in LIVE_DERIVED:
            if live_column == column:
                return entry.fields[f"combat.{name}"]
        raise MigrationError(
            f"{where}: pin mutates derived_bonuses[{column}] "
            f"({DEAD_DERIVED[column]}), a column v2 does not carry")
    leaf = {"special_costs": "mp_cost", "special_names": "name",
            "alternate_names": "alternate"}[key]
    field = entry.fields.get(f"specials.{column}.{leaf}")
    if field is None:
        raise MigrationError(
            f"{where}: pin mutates {key}[{column}], which v2 omits (the slot "
            f"is disabled)")
    return field


def reanchor(pin: Pin, old_lines: List[str], new_lines: List[str],
             entries: List[Entry], carried: Dict[int, int],
             path: str) -> None:
    index = pin.line - 1
    where = f"{path}:{pin.line}"
    if index < 0 or index >= len(old_lines):
        raise MigrationError(f"{where}: pin line out of range")
    old_line = old_lines[index]
    if old_line.count(pin.from_text) != 1:
        raise MigrationError(
            f"{where}: pin from-text {pin.from_text!r} does not occur exactly "
            f"once on its line")

    if index in carried:
        pin.new_line = carried[index] + 1
        pin.changed = pin.new_line != pin.line
    else:
        entry = next(e for e in entries if index in e.lines.values())
        key = next(k for k, v in entry.lines.items() if v == index)
        mutated = old_line.replace(pin.from_text, pin.to_text, 1)
        if key in V1_SEQ_KEYS:
            body = old_line.split(":", 1)[1]
            new_body = mutated.split(":", 1)[1]
            before = flow_tokens(body, where)
            after = flow_tokens(new_body, where)
            if len(before) != len(after):
                raise MigrationError(
                    f"{where}: the mutation changes the element count of "
                    f"{key}")
            differing = [i for i, (a, b) in enumerate(zip(before, after))
                         if a != b]
            if len(differing) != 1:
                raise MigrationError(
                    f"{where}: the mutation touches {len(differing)} elements "
                    f"of {key}; a v2 line carries exactly one")
            column = differing[0]
            field = field_for_column(key, column, entry, where)
            pin.new_from = f"{field.key}: {before[column]}"
            pin.new_to = f"{field.key}: {after[column]}"
        else:
            field = entry.fields[{"hiring_cost": "costs.hire",
                                  "weapon_cost": "combat.fire_mp_cost"}[key]]
            pin.new_from = (f"{field.key}: "
                            f"{scalar_token(old_line.split(':', 1)[1], where)}")
            pin.new_to = (f"{field.key}: "
                          f"{scalar_token(mutated.split(':', 1)[1], where)}")
        pin.new_line = entry.block_start + field.offset + 1
        pin.changed = True

    target = new_lines[pin.new_line - 1]
    if target.count(pin.new_from) != 1:
        raise MigrationError(
            f"{where}: re-anchored from-text {pin.new_from!r} occurs "
            f"{target.count(pin.new_from)} times on the new line "
            f"{pin.new_line} ({target!r}); the canary applier requires "
            f"exactly one")


def patch_pin_table(table_path: Path, per_file: Dict[str, Dict[str, object]],
                    dry_run: bool) -> List[str]:
    text = table_path.read_text()
    report: List[str] = []
    pieces: List[str] = []
    last = 0
    for match in PIN_RE.finditer(text):
        pin = Pin(match)
        state = per_file.get(pin.file)
        if state is None:
            continue
        reanchor(pin, state["old_lines"], state["new_lines"],  # type: ignore
                 state["entries"], state["carried"], pin.file)  # type: ignore
        if not pin.changed:
            continue
        pieces.append(text[last:match.start()])
        pieces.append(match.group("head"))
        pieces.append(str(pin.new_line))
        pieces.append(match.group("mid1"))
        pieces.append(f'"{c_escape(pin.new_from)}"')
        pieces.append(match.group("mid2"))
        pieces.append(f'"{c_escape(pin.new_to)}"')
        last = match.end()
        report.append(
            f"  {pin.file}:{pin.line} -> :{pin.new_line}  "
            f"{pin.from_text!r} -> {pin.new_from!r}"
            + ("" if pin.to_text == pin.new_to
               else f"   (to: {pin.to_text!r} -> {pin.new_to!r})"))
    if not report:
        return report
    pieces.append(text[last:])
    if not dry_run:
        table_path.write_text("".join(pieces))
    return report


# --- driver --------------------------------------------------------------


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Rewrite class-pack living entries from schema v1 to v2.")
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--dry-run", action="store_true",
                        help="report only; write nothing")
    parser.add_argument("--check", action="store_true",
                        help="re-prove already-migrated files (no v1 key left "
                             "is success); writes nothing")
    parser.add_argument("--drop-dead-axis", action="store_true",
                        help="drop a non-zero value in a dead derived_bonuses "
                             "column instead of refusing")
    parser.add_argument("--pin-table", action="append", type=Path,
                        default=None,
                        help="a mutation pin table to re-anchor (repeatable); "
                             "default tests/parity/scenario_table.h")
    parser.add_argument("--no-pins", action="store_true",
                        help="skip pin re-anchoring")
    args = parser.parse_args(list(argv))

    warnings: List[str] = []
    per_file: Dict[str, Dict[str, object]] = {}
    failures = 0

    for file_path in args.files:
        rel = str(file_path.resolve().relative_to(REPO_ROOT)) \
            if file_path.resolve().is_relative_to(REPO_ROOT) else str(file_path)
        old_text = file_path.read_text()
        try:
            if args.check:
                stale = [key for key in V1_KEYS
                         if re.search(rf"^\s*{key}:", old_text, re.M)]
                if stale:
                    raise MigrationError(
                        f"{rel}: still declares {', '.join(stale)}")
                print(f"{rel}: schema v2 (no v1 key remains)")
                continue
            new_text, entries, carried = rewrite(
                old_text, rel, args.drop_dead_axis, warnings)
            print(prove(old_text, new_text, entries, carried, rel))
            per_file[rel] = {
                "old_lines": old_text.splitlines(),
                "new_lines": new_text.splitlines(),
                "entries": entries,
                "carried": carried,
            }
            if not args.dry_run:
                file_path.write_text(new_text)
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1

    for warning in warnings:
        print(f"WARNING {warning}", file=sys.stderr)

    if failures:
        return 1
    if args.check or args.no_pins or not per_file:
        return 0

    tables = args.pin_table or [Path(name) for name in DEFAULT_PIN_TABLES]
    for table in tables:
        path = table if table.is_absolute() else REPO_ROOT / table
        if not path.exists():
            print(f"REFUSED pin table {path} does not exist", file=sys.stderr)
            failures += 1
            continue
        try:
            report = patch_pin_table(path, per_file, args.dry_run)
        except MigrationError as error:
            print(f"REFUSED {error}", file=sys.stderr)
            failures += 1
            continue
        print(f"pin patch ({table}): {len(report)} re-anchored")
        for line in report:
            print(line)

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
