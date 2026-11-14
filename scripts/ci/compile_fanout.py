#!/usr/bin/env python3
"""Decide whether a push needs the eight compile-cache warmers.

The ASan and coverage lanes compile through a cross-commit sccache seed. A
push used to fan its objects out over eight warm-up jobs before the producer
built anything, whatever it had changed: 1m34s of the coverage lane and 2m47s
of the ASan lane on 2026-11-13, in runs where every one of the sixteen jobs
reported a 100 % hit rate and compiled nothing.

This reads what changed since the commit the newest seed was saved for and
answers one question: can the producer compile the difference itself, on its
own four cores, in less time than the fan-out costs? The answer changes only
how long the lane takes. A producer that follows no fan-out compiles whatever
its seed lacks (`nix_bundle.py build --unwarmed`), so a wrong "no" is a slower
build and never a different one.

    compile_fanout.py --prefix <cache key prefix> --matched-key <restored key>

writes `warm=true|false` and `reason=...` to $GITHUB_OUTPUT.
"""

from __future__ import annotations

import argparse
import os
from pathlib import PurePosixPath
import re
import subprocess
import sys

# Translation-unit compilations the producer may absorb. One instrumented
# object costs a few core-seconds; the fan-out it replaces costs more than a
# minute of wall time before the producer starts.
BUDGET = 24

# Compiled once per test binary that links them, not once.
SHARED_TEST_SOURCES = frozenset({
    "tests/unit/unit_main.cpp",
    "tests/unit/headless_unit_main.cpp",
    "tests/unit/family_registry_dump.cpp",
    "tests/integration/integration_main.cpp",
})

# Read at run time, by checks, or by other workflows: never by a compiler.
UNCOMPILED_PREFIXES = (
    ".claude/", ".github/", "campaigns/", "cfg/", "docs/", "packs/", "pix/",
    "relay/", "scen/", "scripts/coverage/", "scripts/parity/", "sound/",
    "tests/e2e/", "tests/parity/golden/", "web/",
)
UNCOMPILED_FILES = frozenset({
    "scripts/analyze_jitter_metrics.mjs",
    "scripts/ci/browser_matrix.py",
    "scripts/ci/canary_matrix.py",
    "scripts/ci/compile_fanout.py",
    "scripts/ci/test_matrix.py",
    "scripts/ci/test_timings.json",
})
SOURCE_SUFFIXES = (".c", ".cc", ".cpp")


def compilations(path: str) -> int | None:
    """Objects one changed path recompiles, or None when nobody can say.

    None is every header, every build description and everything this file
    does not name: the fan-out stays for those.
    """
    pure = PurePosixPath(path)
    if pure.is_absolute() or ".." in pure.parts:
        raise ValueError(f"not a repository path: {path}")
    if path in UNCOMPILED_FILES or path.endswith(".md") or path.startswith(UNCOMPILED_PREFIXES):
        return 0
    if re.fullmatch(r"scripts/ci/(test_[^/]+|[^/]+_test)\.py", path):
        return 0
    if path in SHARED_TEST_SOURCES or not path.endswith(SOURCE_SUFFIXES):
        return None
    if path.startswith("src/"):
        # The product, TESTING and headless variants of one game source.
        return 3
    if path.startswith("tests/"):
        return 1
    return None


def decide(changed: list[str], budget: int = BUDGET) -> tuple[bool, str]:
    """Return (warm, reason) for the paths changed since the seed's commit."""
    total = 0
    for path in sorted(set(changed)):
        cost = compilations(path)
        if cost is None:
            return True, f"{path} can change any number of objects"
        total += cost
    if total > budget:
        return True, f"about {total} objects changed, over the producer's budget of {budget}"
    return False, f"about {total} objects changed, within the producer's budget of {budget}"


def seed_revision(prefix: str, matched_key: str) -> str | None:
    if not prefix or not matched_key.startswith(prefix):
        return None
    revision = matched_key[len(prefix):]
    return revision if re.fullmatch(r"[0-9a-f]{40}", revision) else None


def changed_since(revision: str) -> list[str] | None:
    """Paths that differ between that commit's tree and HEAD's, or None."""
    known = subprocess.run(["git", "cat-file", "-e", f"{revision}^{{commit}}"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if known.returncode:
        return None
    listing = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", revision, "HEAD"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if listing.returncode:
        return None
    return [path for path in listing.stdout.split("\0") if path]


def plan(prefix: str, matched_key: str) -> tuple[bool, str]:
    revision = seed_revision(prefix, matched_key)
    if revision is None:
        return True, "no compiler-cache seed from a known commit"
    changed = changed_since(revision)
    if changed is None:
        return True, f"the seed's commit {revision[:8]} is not in this checkout"
    warm, reason = decide(changed)
    return warm, f"{reason} since the seed at {revision[:8]}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--matched-key", default="")
    args = parser.parse_args()
    warm, reason = plan(args.prefix, args.matched_key)
    verdict = "true" if warm else "false"
    print(f"warm={verdict}: {reason}")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a") as out:
            out.write(f"warm={verdict}\nreason={reason}\n")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as out:
            out.write(f"Compile fan-out: **{'eight warmers' if warm else 'skipped'}** — {reason}.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
