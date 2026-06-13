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
  - The realpath of ${file} resolved against the repo root does NOT live
    under ../openglad-master/ or tests/parity/. Master is pinned at
    master_companion_sha; tests/parity/* headers are consumed via
    inline constexpr by every parity TU, so mutating them triggers a
    full parity-group rebuild that defeats incremental canary runs.

Usage:
  _apply_mutation.py <file> <line> <from_text> <to_text>

Exit codes:
  0  applied
  2  argv parse error
  3  refused: forbidden path
  4  file not present
  5  line out of range
  6  from-text not on that line
  7  from-text appears multiple times on that line (ambiguous)
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


def _is_under(path_real: str, prefix_real: str) -> bool:
    try:
        return os.path.commonpath([path_real, prefix_real]) == prefix_real
    except ValueError:
        return False


def main() -> int:
    if len(sys.argv) != 5:
        sys.stderr.write(
            "usage: _apply_mutation.py <file> <line> <from_text> <to_text>\n"
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

    lines[line_no - 1] = line.replace(from_text, to_text, 1)
    new_text = "".join(lines)

    with open(target_real, "w", encoding="utf-8", newline="") as f:
        f.write(new_text)

    sys.stdout.write(f"_apply_mutation: applied at {file_arg}:{line_no}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
