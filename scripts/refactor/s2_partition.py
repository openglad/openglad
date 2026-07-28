#!/usr/bin/env python3
"""Generate scripts/refactor/s2_partition.json — the Stage-2 lane split.

Splits the 36 corpus files into 3 lanes balanced by line count (greedy
longest-first), and attaches to each lane the subset of mutation-canary
pins (tests/parity/scenario_table.h) that anchor into that lane's files —
so a lane knows exactly which pins every line-shifting batch it applies
must re-point and canary-flip.  Pins are recorded as {mutation, file, line,
from}: the canary anchors on {file, line, text}, so after a reflow the lane
re-points `line` wherever `from` moved (scripts/parity/check_mutation_pins.py
--fix does the mechanical part; the FLIP PROOF — >= 1 scenario flipped per
moved pin — is the lane's evidence the pin kept its teeth).

Regenerate after any corpus reflow or pin retarget:

    python3 scripts/refactor/s2_partition.py          # rewrite the JSON
    python3 scripts/refactor/s2_partition.py --check  # verify it is current
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
CORPUS = REPO / "packs" / "core" / "scripts"
TABLE = REPO / "tests" / "parity" / "scenario_table.h"
OUT = pathlib.Path(__file__).resolve().parent / "s2_partition.json"

# Same pin shape check_mutation_pins.py parses, plus the kMut_ name.
PIN = re.compile(
    r'inline constexpr Mutation (kMut_\w+) = \{\s*'
    r'"((?:src|packs|tools)/[^"]+)"\s*,\s*(\d+)\s*,\s*'
    r'"((?:[^"\\]|\\.)*)"',
    re.S,
)


def unescape(text: str) -> str:
    return text.encode().decode("unicode_escape")


def build() -> dict:
    counts = {p.name: sum(1 for _ in p.open()) for p in sorted(CORPUS.glob("*.lua"))}

    pins: dict[str, list[dict]] = {}
    for m in PIN.finditer(TABLE.read_text()):
        name, file_, line, from_text = m.groups()
        if not file_.startswith("packs/core/scripts/"):
            continue
        pins.setdefault(pathlib.Path(file_).name, []).append({
            "mutation": name,
            "file": file_,
            "line": int(line),
            "from": unescape(from_text),
        })

    lanes: list[dict] = [
        {"lane": lane, "lines": 0, "files": [], "pins": []}
        for lane in ("A", "B", "C")
    ]
    for fname, lines in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        lane = min(lanes, key=lambda l: (l["lines"], l["lane"]))
        lane["files"].append(fname)
        lane["lines"] += lines
        lane["pins"].extend(pins.get(fname, []))
    for lane in lanes:
        lane["files"].sort()
        lane["pins"].sort(key=lambda p: (p["file"], p["line"], p["mutation"]))

    unassigned = sorted(set(pins) - set(counts))
    if unassigned:
        sys.exit(f"error: pins anchor into unknown corpus files: {unassigned}")

    return {
        "_comment": (
            "Stage-2 lane partition: 3 lanes balanced by line count; each "
            "lane's `pins` are the scenario_table.h mutation-canary anchors "
            "into its files. Every line-shifting batch a lane applies must "
            "re-point these pins ({file,line,text} anchors) and prove >= 1 "
            "canary scenario flip per moved pin — anchors are not teeth. "
            "Regenerate with scripts/refactor/s2_partition.py."
        ),
        "corpus": {"files": len(counts), "lines": sum(counts.values())},
        "lanes": lanes,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="verify the committed JSON matches a fresh build")
    args = ap.parse_args()
    data = build()
    text = json.dumps(data, indent=1) + "\n"
    if args.check:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"s2_partition: {OUT} is stale — regenerate", file=sys.stderr)
            return 1
        print("s2_partition: current")
        return 0
    OUT.write_text(text)
    for lane in data["lanes"]:
        print(f"lane {lane['lane']}: {len(lane['files'])} files, "
              f"{lane['lines']} lines, {len(lane['pins'])} pins")
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
