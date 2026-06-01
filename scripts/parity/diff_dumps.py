#!/usr/bin/env python3
"""Compare two parity state dumps emitted by the branch and master companions.

Usage: diff_dumps.py BRANCH_JSON MASTER_JSON

Both inputs are canonical JSON files produced by the schema-v1 emitter in
tests/parity/state_dump.cpp (branch) and tools/parity_dump_state.cpp
(master companion). The script reports the first three categorised
differences and writes a machine-readable summary on stdout line 1.

Exit code: 0 on match, 1 on mismatch, 2 on harness/usage error.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Iterable, List, Tuple


SCHEMA_VERSION = "v1"


def load(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"HARNESS_ERROR cannot read {path}: {exc}", file=sys.stderr)
        sys.exit(2)


def categorise(field: str) -> str:
    if field == "tick":
        return "cadence_mismatch"
    if field == "rng_state":
        return "rng_drift"
    if field.startswith("walkers["):
        if field.endswith(".hp") or field.endswith(".max_hp"):
            return "hp_drift"
        if field.endswith(".xpos") or field.endswith(".ypos"):
            return "position_drift"
        return "walker_field"
    if field.startswith("effects["):
        return "effect_field"
    if field.startswith("events["):
        return "event_field"
    if field.startswith("score_per_team"):
        return "scoring"
    return "schema"


def walk(prefix: str, a: Any, b: Any, diffs: List[Tuple[str, Any, Any]]) -> None:
    if isinstance(a, dict) and isinstance(b, dict):
        keys = sorted(set(a) | set(b))
        for k in keys:
            sub = f"{prefix}.{k}" if prefix else k
            if k not in a:
                diffs.append((sub, "<missing>", b[k]))
            elif k not in b:
                diffs.append((sub, a[k], "<missing>"))
            else:
                walk(sub, a[k], b[k], diffs)
        return
    if isinstance(a, list) and isinstance(b, list):
        n = max(len(a), len(b))
        for i in range(n):
            sub = f"{prefix}[{i}]"
            if i >= len(a):
                diffs.append((sub, "<missing>", b[i]))
            elif i >= len(b):
                diffs.append((sub, a[i], "<missing>"))
            else:
                walk(sub, a[i], b[i], diffs)
        return
    if a != b:
        diffs.append((prefix, a, b))


def main(argv: List[str]) -> int:
    if len(argv) != 3:
        print(f"usage: {Path(argv[0]).name} BRANCH_JSON MASTER_JSON",
              file=sys.stderr)
        return 2
    branch = load(Path(argv[1]))
    master = load(Path(argv[2]))

    for label, dump in (("branch", branch), ("master", master)):
        if dump.get("schema_version") != SCHEMA_VERSION:
            print(f"HARNESS_ERROR {label} schema_version="
                  f"{dump.get('schema_version')!r} (expected {SCHEMA_VERSION!r})",
                  file=sys.stderr)
            return 2

    diffs: List[Tuple[str, Any, Any]] = []
    walk("", branch, master, diffs)

    if not diffs:
        print(f"MATCH schema={SCHEMA_VERSION}")
        return 0

    field, br, ms = diffs[0]
    print(f"FAIL {categorise(field)} {field}")
    for i, (f, b, m) in enumerate(diffs[:3]):
        print(f"  [{i+1}/{len(diffs)}] {f}: branch={b!r} master={m!r}",
              file=sys.stderr)
    if len(diffs) > 3:
        print(f"  ...{len(diffs) - 3} more differences", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
