#!/usr/bin/env python3
"""Fail when docs/modding/og-api.d.lua is stale against the binding sources.

CI-runnable drift check for the generated EmmyLua stubs: regenerates the
stub text in memory (scripts/modding/gen_api_stubs.py, the same module —
one generator, no second implementation) and byte-compares it with the
committed file. Wired as the `api_stub_check` cmake custom target and,
since quality-plan stage 5, ENFORCED as a dependency of coverage_report
(the same gating point as the full statement lint and check_luals) —
still deliberately not an og_gameplay build dependency, so per-build cost
stays flat. Stale stubs also starve check_luals and rule 8 of
check_lua_statement_lines.py, both of which read the committed file.

Exit codes: 0 fresh, 1 stale or missing, 2 generator failure.
"""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_api_stubs  # noqa: E402  (path bootstrap above)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check docs/modding/og-api.d.lua freshness.")
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()

    try:
        expected = gen_api_stubs.generate(args.repo_root)
    except SystemExit as exc:
        print(f"api_stub_check: stub generation FAILED: {exc}",
              file=sys.stderr)
        print("api_stub_check: a binding-source shape the parser cannot "
              "read? See the append contract in "
              "scripts/modding/gen_api_stubs.py.", file=sys.stderr)
        return 2

    stub_path = args.repo_root / gen_api_stubs.OUTPUT
    if not stub_path.exists():
        print(f"api_stub_check: {gen_api_stubs.OUTPUT} is MISSING.",
              file=sys.stderr)
        print("api_stub_check: regenerate it:  "
              "python3 scripts/modding/gen_api_stubs.py", file=sys.stderr)
        return 1
    actual = stub_path.read_text(encoding="utf-8")
    if actual == expected:
        print(f"api_stub_check: {gen_api_stubs.OUTPUT} is up to date.")
        return 0

    diff = list(difflib.unified_diff(
        actual.splitlines(keepends=True),
        expected.splitlines(keepends=True),
        fromfile=f"{gen_api_stubs.OUTPUT} (committed)",
        tofile=f"{gen_api_stubs.OUTPUT} (regenerated)",
    ))
    head = diff[:60]
    sys.stderr.writelines(head)
    if len(diff) > len(head):
        print(f"... ({len(diff) - len(head)} more diff lines)",
              file=sys.stderr)
    print(f"api_stub_check: {gen_api_stubs.OUTPUT} is STALE against the "
          "binding registration tables.", file=sys.stderr)
    print("api_stub_check: regenerate it:  "
          "python3 scripts/modding/gen_api_stubs.py", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
