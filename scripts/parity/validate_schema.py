#!/usr/bin/env python3
"""Validate a parity state dump against schema v1.

Usage: validate_schema.py DUMP.json [DUMP.json ...]

Reads each dump and asserts the shape promised by schema v1 in
`.plan/parity-harness-design.md`:

- top-level keys: effects, events, level_done, level_tick_count, rng_state,
  schema_version, score_per_team, tick, walkers, weapons (lexicographically
  sorted in the emitted JSON, but Python's json.load() ignores order so we
  only check presence here)
- schema_version == "v1"
- tick is a non-negative integer
- rng_state is a string matching ^0x[0-9A-F]{8}$ or the literal "unobservable"
- score_per_team is a list of exactly 4 non-negative integers
- walkers[] entries have alive(bool), family(str), hp(float), id(int),
  max_hp(float), team(int), weapons_left(int), xpos(int), ypos(int)
- effects[] entries have family(str), id(int), lifetime(int), xpos(int), ypos(int)
- events[] entries have a(int), b(int), kind(str), sequence(int), text(str),
  tick(int)
- weapons[] entries have family(str), id(int), lifetime(int), team(int),
  xpos(int), ypos(int)

Exit code 0 on success; 1 on any validation failure (with the offending
file:field reported on stderr). Useful as a fail-fast guard before invoking
diff_dumps.py.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any, List


SCHEMA_VERSION = "v1"
_HEX32 = re.compile(r"^0x[0-9A-F]{8}$")

_TOP_LEVEL_KEYS = {
    "effects", "events", "level_done", "level_tick_count",
    "rng_state", "schema_version", "score_per_team", "tick", "walkers",
    "weapons",
}

_WALKER_KEYS = {
    "alive", "family", "hp", "id", "max_hp",
    "team", "weapons_left", "xpos", "ypos",
}

_EFFECT_KEYS = {"family", "id", "lifetime", "xpos", "ypos"}

_EVENT_KEYS = {"a", "b", "kind", "sequence", "text", "tick"}

_WEAPON_KEYS = {"family", "id", "lifetime", "team", "xpos", "ypos"}


class ValidationError(Exception):
    pass


def _need(d: dict, key: str, where: str) -> Any:
    if key not in d:
        raise ValidationError(f"{where}: missing key '{key}'")
    return d[key]


def _is_uint(v: Any) -> bool:
    return isinstance(v, int) and not isinstance(v, bool) and v >= 0


def _is_int(v: Any) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


def _is_float(v: Any) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _check_walker(idx: int, w: dict) -> None:
    where = f"walkers[{idx}]"
    if set(w.keys()) != _WALKER_KEYS:
        extra = set(w.keys()) - _WALKER_KEYS
        missing = _WALKER_KEYS - set(w.keys())
        raise ValidationError(
            f"{where}: key set mismatch (extra={sorted(extra)}, "
            f"missing={sorted(missing)})")
    if not isinstance(w["alive"], bool):
        raise ValidationError(f"{where}.alive: expected bool")
    if not isinstance(w["family"], str):
        raise ValidationError(f"{where}.family: expected string")
    if not _is_float(w["hp"]):
        raise ValidationError(f"{where}.hp: expected number")
    if not _is_uint(w["id"]):
        raise ValidationError(f"{where}.id: expected non-negative int")
    if not _is_float(w["max_hp"]):
        raise ValidationError(f"{where}.max_hp: expected number")
    if not _is_uint(w["team"]):
        raise ValidationError(f"{where}.team: expected non-negative int")
    if not _is_int(w["weapons_left"]):
        raise ValidationError(f"{where}.weapons_left: expected int")
    if not _is_int(w["xpos"]):
        raise ValidationError(f"{where}.xpos: expected int")
    if not _is_int(w["ypos"]):
        raise ValidationError(f"{where}.ypos: expected int")


def _check_effect(idx: int, e: dict) -> None:
    where = f"effects[{idx}]"
    if set(e.keys()) != _EFFECT_KEYS:
        extra = set(e.keys()) - _EFFECT_KEYS
        missing = _EFFECT_KEYS - set(e.keys())
        raise ValidationError(
            f"{where}: key set mismatch (extra={sorted(extra)}, "
            f"missing={sorted(missing)})")
    if not isinstance(e["family"], str):
        raise ValidationError(f"{where}.family: expected string")
    if not _is_uint(e["id"]):
        raise ValidationError(f"{where}.id: expected non-negative int")
    if not _is_int(e["lifetime"]):
        raise ValidationError(f"{where}.lifetime: expected int")
    if not _is_int(e["xpos"]):
        raise ValidationError(f"{where}.xpos: expected int")
    if not _is_int(e["ypos"]):
        raise ValidationError(f"{where}.ypos: expected int")


def _check_weapon(idx: int, w: dict) -> None:
    where = f"weapons[{idx}]"
    if set(w.keys()) != _WEAPON_KEYS:
        extra = set(w.keys()) - _WEAPON_KEYS
        missing = _WEAPON_KEYS - set(w.keys())
        raise ValidationError(
            f"{where}: key set mismatch (extra={sorted(extra)}, "
            f"missing={sorted(missing)})")
    if not isinstance(w["family"], str):
        raise ValidationError(f"{where}.family: expected string")
    if not _is_uint(w["id"]):
        raise ValidationError(f"{where}.id: expected non-negative int")
    if not _is_int(w["lifetime"]):
        raise ValidationError(f"{where}.lifetime: expected int")
    if not _is_uint(w["team"]):
        raise ValidationError(f"{where}.team: expected non-negative int")
    if not _is_int(w["xpos"]):
        raise ValidationError(f"{where}.xpos: expected int")
    if not _is_int(w["ypos"]):
        raise ValidationError(f"{where}.ypos: expected int")


def _check_event(idx: int, ev: dict) -> None:
    where = f"events[{idx}]"
    if set(ev.keys()) != _EVENT_KEYS:
        extra = set(ev.keys()) - _EVENT_KEYS
        missing = _EVENT_KEYS - set(ev.keys())
        raise ValidationError(
            f"{where}: key set mismatch (extra={sorted(extra)}, "
            f"missing={sorted(missing)})")
    if not _is_uint(ev["a"]):
        raise ValidationError(f"{where}.a: expected non-negative int")
    if not _is_uint(ev["b"]):
        raise ValidationError(f"{where}.b: expected non-negative int")
    if not isinstance(ev["kind"], str):
        raise ValidationError(f"{where}.kind: expected string")
    if not _is_uint(ev["sequence"]):
        raise ValidationError(f"{where}.sequence: expected non-negative int")
    if not isinstance(ev["text"], str):
        raise ValidationError(f"{where}.text: expected string")
    if not _is_uint(ev["tick"]):
        raise ValidationError(f"{where}.tick: expected non-negative int")


def validate(path: Path) -> None:
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValidationError(f"cannot read: {exc}") from exc

    if not raw.endswith("\n"):
        raise ValidationError(
            "dump must terminate with a single LF newline (schema v1)")
    if raw.endswith("\n\n"):
        raise ValidationError(
            "dump has multiple trailing newlines (schema v1 forbids)")

    try:
        doc = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ValidationError(f"invalid JSON: {exc}") from exc

    if not isinstance(doc, dict):
        raise ValidationError("top-level value must be a JSON object")

    actual_keys = set(doc.keys())
    if actual_keys != _TOP_LEVEL_KEYS:
        extra = actual_keys - _TOP_LEVEL_KEYS
        missing = _TOP_LEVEL_KEYS - actual_keys
        raise ValidationError(
            f"top-level key set mismatch (extra={sorted(extra)}, "
            f"missing={sorted(missing)})")

    if doc["schema_version"] != SCHEMA_VERSION:
        raise ValidationError(
            f"schema_version: expected '{SCHEMA_VERSION}', "
            f"got {doc['schema_version']!r}")

    if not _is_uint(doc["tick"]):
        raise ValidationError("tick: expected non-negative int")

    if not _is_int(doc["level_done"]):
        raise ValidationError("level_done: expected int")

    if not _is_uint(doc["level_tick_count"]):
        raise ValidationError("level_tick_count: expected non-negative int")

    rng_state = doc["rng_state"]
    if not isinstance(rng_state, str):
        raise ValidationError("rng_state: expected string")
    if rng_state != "unobservable" and not _HEX32.match(rng_state):
        raise ValidationError(
            f"rng_state: expected 0xXXXXXXXX (upper hex) or 'unobservable', "
            f"got {rng_state!r}")

    spt = doc["score_per_team"]
    if not isinstance(spt, list) or len(spt) != 4:
        raise ValidationError("score_per_team: expected list of length 4")
    for i, v in enumerate(spt):
        if not _is_uint(v):
            raise ValidationError(
                f"score_per_team[{i}]: expected non-negative int")

    walkers = doc["walkers"]
    if not isinstance(walkers, list):
        raise ValidationError("walkers: expected list")
    for i, w in enumerate(walkers):
        if not isinstance(w, dict):
            raise ValidationError(f"walkers[{i}]: expected object")
        _check_walker(i, w)

    effects = doc["effects"]
    if not isinstance(effects, list):
        raise ValidationError("effects: expected list")
    for i, e in enumerate(effects):
        if not isinstance(e, dict):
            raise ValidationError(f"effects[{i}]: expected object")
        _check_effect(i, e)

    events = doc["events"]
    if not isinstance(events, list):
        raise ValidationError("events: expected list")
    for i, ev in enumerate(events):
        if not isinstance(ev, dict):
            raise ValidationError(f"events[{i}]: expected object")
        _check_event(i, ev)

    weapons = doc["weapons"]
    if not isinstance(weapons, list):
        raise ValidationError("weapons: expected list")
    for i, w in enumerate(weapons):
        if not isinstance(w, dict):
            raise ValidationError(f"weapons[{i}]: expected object")
        _check_weapon(i, w)


def main(argv: List[str]) -> int:
    if len(argv) < 2:
        print("usage: validate_schema.py DUMP.json [DUMP.json ...]",
              file=sys.stderr)
        return 2

    failures = 0
    for raw in argv[1:]:
        path = Path(raw)
        try:
            validate(path)
        except ValidationError as exc:
            failures += 1
            print(f"INVALID {path}: {exc}", file=sys.stderr)
        else:
            print(f"OK {path}")

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
