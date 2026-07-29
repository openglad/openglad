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

A pinned line is accepted in any state this table can explain: clean (`from`
on the line), this pin mid-mutation (`to` on the line), or a shared-anchor
SIBLING pin mid-mutation (a sibling's `to` on the line whose reversal
restores this pin's `from`) — see accepts(). The sibling rule is what lets
the canary rebuild og_test_parity while one of several same-line pins is
applied; without it, teeth for shared-anchor C++ pins were unprovable.
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


def accepts(line: str, from_text: str, to_text: str,
            siblings: list[tuple[str, str]] = []) -> bool:
    """A pin anchors if the line holds either side of its substitution,
    or a SIBLING pin's mutated state whose reversal restores this anchor.

    This check is a build dependency of og_test_parity, and the canary
    rebuilds that target WITH THE MUTATION APPLIED — at which point the pinned
    line holds `to`, not `from`. Insisting on `from` alone would fail the
    mutated build and abort the canary before it could measure anything,
    breaking the very tool this check exists to protect. Accepting either side
    keeps that honest: a line matching neither really has drifted.

    Shared anchors need one more state. Several pins may anchor the same
    line with the same `from` but different `to`s (walker.cpp:1189 carries
    eight — one 362.0f multiplier, eight discriminating rewrites). Mid-canary
    for pin P, the line holds P's `to`; a sibling pin Q then matches neither
    of Q's own sides, which used to red the build-dep check and abort the
    canary — making teeth for this whole pin class unprovable in-harness.
    So a pin also anchors when some sibling's `to` occurs in the line AND
    reversing that one substitution (`to` -> `from`, first occurrence)
    restores this pin's own `from`. That accepts exactly the recognized
    single-mutation states of the shared line: an unrelated edit contains no
    sibling's `to`, and a wrong reversal does not reproduce `from`, so
    genuine drift still fails.
    """
    if from_text in line or (bool(to_text) and to_text in line):
        return True
    for sib_from, sib_to in siblings:
        if not sib_to or sib_to == sib_from or sib_to not in line:
            continue
        if from_text in line.replace(sib_to, sib_from, 1):
            return True
    return False


def collect_siblings(source: str) -> dict[tuple[str, int],
                                          list[tuple[str, str]]]:
    """All pins keyed by (path, line): the shared-anchor groups accepts()
    consults to recognize a sibling's mid-mutation state."""
    groups: dict[tuple[str, int], list[tuple[str, str]]] = {}
    for match in PIN.finditer(source):
        _, path, line_text, _, from_text, to_text = match.groups()
        groups.setdefault((path, int(line_text)), []).append(
            (unescape(from_text), unescape(to_text)))
    return groups


def check(fix: bool) -> int:
    source = TABLE.read_text()
    siblings = collect_siblings(source)
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
        if 1 <= line <= len(lines) and accepts(
                lines[line - 1], wanted, mutated,
                siblings.get((path, line), [])):
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
