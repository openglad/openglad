#!/usr/bin/env python3
"""Apply a single discriminating mutation to a source file.

Phase 02 mutation canary helper. Given a target ${file}, 1-indexed
${line}, literal ${from} and ${to}, performs a single
str.replace(${from}, ${to}, 1) on that exact line and writes the file
back. Validates:
  - ${file} is a repository-relative path that resolves inside this
    checkout.
  - ${from} appears literally on ${line} (exactly once — ambiguous
    multi-occurrences abort).
  - the optional ${context_before}, when given, appears VERBATIM on one
    of the CONTEXT_WINDOW lines above ${line}. A pin's from-text is
    matched within a single line, which for a repeated statement cannot
    say WHICH occurrence the pin means; the context line above it can.
  - The realpath of ${file} resolved against the repo root does NOT live
    under ../openglad-master/ or tests/parity/. Master is pinned at
    master_companion_sha; tests/parity/* headers are consumed via
    inline constexpr by every parity TU, so mutating them triggers a
    full parity-group rebuild that defeats incremental canary runs.

Usage:
  _apply_mutation.py <file> <line> <from_text> <to_text> [<context_before>]

Exit codes:
  0  applied
  2  argv parse error
  3  refused: forbidden path
  4  file not present
  5  line out of range
  6  from-text not on that line
  7  from-text appears multiple times on that line (ambiguous)
  8  context-before text is not on the lines above (pin is on the wrong
     occurrence, or the block it names has moved)
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
REPO_ROOT_REAL = os.path.realpath(REPO_ROOT)
FORBIDDEN_PREFIXES = [
    os.path.realpath(REPO_ROOT / ".." / "openglad-master"),
    os.path.realpath(REPO_ROOT / "tests" / "parity"),
]


# How far above the pinned line a context anchor may sit. THE definition:
# check_mutation_pins.py imports it (and context_ok below) rather than
# re-deriving the rule, so the acceptance check cannot drift from the applier
# it is supposed to predict, and tests/parity/scenario_table.h mirrors the
# number as kMutationContextWindow for the in-suite C++ gate.
#
# Sixteen lines is the enclosing `case`, `if` or `local function` of a
# statement in this codebase without reaching the one before it.
CONTEXT_WINDOW = 16


def _is_under(path_real: str, prefix_real: str) -> bool:
    try:
        return os.path.commonpath([path_real, prefix_real]) == prefix_real
    except ValueError:
        return False


def context_ok(lines: list[str], line_no: int, context: str) -> bool:
    """Is `context` one of the CONTEXT_WINDOW lines above 1-indexed line_no?

    An empty context asserts nothing — that is every pin written before the
    field existed, and it stays exactly as applicable as it was.

    The comparison is EXACT once the line terminator is off: indentation is
    part of the anchor. A re-indented block is a block that moved into or out
    of something, which is precisely when a pin's claim to know which
    occurrence it means stops being true.
    """
    if not context:
        return True
    lo = max(0, line_no - 1 - CONTEXT_WINDOW)
    return any(line.rstrip("\r\n") == context
               for line in lines[lo:max(lo, line_no - 1)])


def main() -> int:
    if len(sys.argv) not in (5, 6):
        sys.stderr.write(
            "usage: _apply_mutation.py <file> <line> <from_text> <to_text> "
            "[<context_before>]\n"
        )
        return 2

    file_arg = sys.argv[1]
    try:
        line_no = int(sys.argv[2])
    except ValueError:
        sys.stderr.write(f"_apply_mutation: line must be an integer, got {sys.argv[2]!r}\n")
        return 2
    from_text = sys.argv[3]
    to_text = sys.argv[4]
    context_before = sys.argv[5] if len(sys.argv) == 6 else ""

    if not from_text:
        sys.stderr.write("_apply_mutation: from-text must not be empty\n")
        return 2

    file_path = Path(file_arg)
    if file_path.is_absolute() or ".." in file_path.parts:
        sys.stderr.write(
            f"_apply_mutation: refusing non-repository-relative path: {file_arg}\n"
        )
        return 3

    target_path = REPO_ROOT / file_path
    target_real = os.path.realpath(target_path)
    if not _is_under(target_real, REPO_ROOT_REAL):
        sys.stderr.write(
            f"_apply_mutation: refusing path outside repository: {target_real}\n"
        )
        return 3

    for prefix in FORBIDDEN_PREFIXES:
        if _is_under(target_real, prefix):
            sys.stderr.write(
                f"_apply_mutation: refusing to mutate {target_real}: under {prefix}\n"
            )
            return 3

    if not Path(target_real).is_file():
        sys.stderr.write(f"_apply_mutation: file not present: {target_real}\n")
        return 4

    with open(target_real, "r", encoding="utf-8", newline="") as f:
        text = f.read()

    lines = text.splitlines(keepends=True)
    if line_no < 1 or line_no > len(lines):
        sys.stderr.write(
            f"_apply_mutation: line {line_no} out of range (1..{len(lines)}) "
            f"in {file_arg}\n"
        )
        return 5

    line = lines[line_no - 1]
    occ = line.count(from_text)
    if occ == 0:
        sys.stderr.write(
            f"_apply_mutation: from-text not on line {line_no} of {file_arg}\n"
            f"  expected: {from_text!r}\n"
            f"  line:     {line.rstrip()!r}\n"
        )
        return 6
    if occ > 1:
        sys.stderr.write(
            f"_apply_mutation: from-text appears {occ}x on line {line_no} "
            f"(ambiguous) in {file_arg}\n"
        )
        return 7

    if not context_ok(lines, line_no, context_before):
        sys.stderr.write(
            f"_apply_mutation: context-before text is not on the "
            f"{CONTEXT_WINDOW} lines above line {line_no} of {file_arg}; "
            f"refusing to mutate what may be the wrong occurrence\n"
            f"  expected above: {context_before!r}\n"
        )
        return 8

    lines[line_no - 1] = line.replace(from_text, to_text, 1)
    new_text = "".join(lines)

    with open(target_real, "w", encoding="utf-8", newline="") as f:
        f.write(new_text)

    sys.stdout.write(f"_apply_mutation: applied at {file_arg}:{line_no}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
