#!/usr/bin/env python3
"""Verify every mutation-canary pin still anchors to the line it names.

Each Mutation in tests/parity/scenario_table.h is {file, line, from, to, why}.
The canary applies it by replacing `from` on that exact line, so a pin whose
line has drifted — or whose text has changed — silently stops mutating
anything. The canary then reports a clean run and the scenario looks guarded
when it is not. That failure mode is invisible without this check, and it is
easy to cause: any insertion earlier in a pinned file shifts every pin below it.

Run: python3 scripts/parity/check_mutation_pins.py [--fix]
Exit 0 when every pin anchors, 1 otherwise. With --fix, pins whose text still
occurs elsewhere in the file are re-pointed at the nearest occurrence.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO / "tests" / "parity" / "scenario_table.h"

# {"<path>", <line>, "<from>", ... — path prefixes match the canary's own
# allow-list (repo-relative source under src/, packs/ or tools/).
PIN = re.compile(
    r'(\{\s*"((?:src|packs|tools)/[^"]+)"\s*,\s*)(\d+)(\s*,\s*"((?:[^"\\]|\\.)*)")',
    re.S,
)


def unescape(text: str) -> str:
    return text.encode().decode("unicode_escape")


def check(fix: bool) -> int:
    source = TABLE.read_text()
    problems: list[str] = []
    repaired: list[str] = []
    total = 0

    def visit(match: re.Match) -> str:
        nonlocal total
        total += 1
        head, path, line_text, tail, from_text = match.groups()
        line = int(line_text)
        wanted = unescape(from_text)
        target = REPO / path

        if not target.exists():
            problems.append(f"{path}:{line} — file does not exist")
            return match.group(0)

        lines = target.read_text().splitlines()
        if 1 <= line <= len(lines) and wanted in lines[line - 1]:
            return match.group(0)

        hits = [i + 1 for i, text in enumerate(lines) if wanted in text]
        if not hits:
            problems.append(
                f"{path}:{line} — anchor text no longer present anywhere\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        nearest = min(hits, key=lambda i: abs(i - line))
        if not fix:
            problems.append(
                f"{path}:{line} — drifted, text now at line {nearest}"
                f"{' (%d occurrences)' % len(hits) if len(hits) > 1 else ''}\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        repaired.append(f"{path}: {line} -> {nearest}")
        return head + str(nearest) + tail

    updated = PIN.sub(visit, source)

    if fix and repaired:
        TABLE.write_text(updated)
        print(f"repaired {len(repaired)} pin(s):")
        for entry in repaired:
            print("  ", entry)

    if problems:
        print(f"{len(problems)} of {total} mutation pins are broken:",
              file=sys.stderr)
        for entry in problems:
            print("  -", entry, file=sys.stderr)
        print("\nRe-run with --fix to re-point drifted pins, then re-read the"
              "\ndiff: a pin whose surrounding code changed meaning needs a"
              "\nnew anchor, not just a new line number.", file=sys.stderr)
        return 1

    print(f"mutation pins: {total} anchors valid")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true",
                        help="re-point drifted pins at the nearest occurrence")
    args = parser.parse_args()
    return check(args.fix)


if __name__ == "__main__":
    sys.exit(main())
