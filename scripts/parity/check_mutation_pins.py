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

# {"<path>", <line>, "<from>", "<to>", ... — path prefixes match the canary's
# own allow-list (repo-relative source under src/, packs/ or tools/).
# Both texts are captured because a pin is equally well anchored whether the
# tree is clean (line holds `from`) or mid-mutation (line holds `to`) — see
# accepts() below.
PIN = re.compile(
    r'(\{\s*"((?:src|packs|tools)/[^"]+)"\s*,\s*)(\d+)'
    r'(\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)")',
    re.S,
)


def unescape(text: str) -> str:
    return text.encode().decode("unicode_escape")


def accepts(line: str, from_text: str, to_text: str) -> bool:
    """A pin anchors if the line holds either side of its substitution.

    This check is a build dependency of og_test_parity, and the canary
    rebuilds that target WITH THE MUTATION APPLIED — at which point the pinned
    line holds `to`, not `from`. Insisting on `from` alone would fail the
    mutated build and abort the canary before it could measure anything,
    breaking the very tool this check exists to protect. Accepting either side
    keeps that honest: a line matching neither really has drifted.
    """
    return from_text in line or (bool(to_text) and to_text in line)


def check(fix: bool) -> int:
    source = TABLE.read_text()
    problems: list[str] = []
    repaired: list[str] = []
    total = 0

    def visit(match: re.Match) -> str:
        nonlocal total
        total += 1
        head, path, line_text, tail, from_text, to_text = match.groups()
        line = int(line_text)
        wanted = unescape(from_text)
        mutated = unescape(to_text)
        target = REPO / path

        if not target.exists():
            problems.append(f"{path}:{line} — file does not exist")
            return match.group(0)

        lines = target.read_text().splitlines()
        if 1 <= line <= len(lines) and accepts(lines[line - 1], wanted, mutated):
            return match.group(0)

        hits = [i + 1 for i, text in enumerate(lines)
                if accepts(text, wanted, mutated)]
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
