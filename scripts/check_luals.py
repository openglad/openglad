#!/usr/bin/env python3
"""Enforce a clean lua-language-server --check over the pack-Lua corpus.

WHAT THIS GATES
---------------
The workspace is checked with the generated og.* stubs
(docs/modding/og-api.d.lua + .luarc.json), and ANY diagnostic at or above
the check level under an ENFORCED root fails the run:

    packs/           the shipped pack corpus (scripts AND lib)
    docs/modding/    the stub file itself and the teaching examples — a
                     type-dirty example teaches the dirt, and a stub file
                     that contradicts itself is a generator bug
    campaigns/       campaign-embedded pack Lua (campaigns/<id>/packs/),
                     shipped inside the composed .glad archives

Diagnostics elsewhere (test fixtures, scratch trees) are PRINTED as
advisory so drift stays visible, but only the enforced roots gate. Fix
findings by correcting the code or the generated types — regenerate via
scripts/modding/gen_api_stubs.py after binding changes — or, for a spot
LuaLS genuinely cannot type, a targeted ---@cast / --[[@as T]] /
---@diagnostic disable-next-line WITH a why-comment. Never blanket-disable
a diagnostic class in .luarc.json: that turns the gate off for every
future finding of that class.

HOW IT RUNS
-----------
lua-language-server --check exits 0 with no findings, non-zero with them,
and in BOTH cases writes a JSON report to --check_out_path. This wrapper
requires that report to exist afterwards — a run that produced no report
did not check anything and must not read as a pass — then splits findings
into enforced/advisory by repository path.

Wired as the cmake target `check_luals`, a dependency of coverage_report
(the same gating point as the full statement-lint and the stub drift
check), NOT an og_gameplay build dependency: per-build cost stays flat,
and the binary comes from the dev shell (flake.nix) or CI's pinned
lua-language-server installation.

Exit codes: 0 clean, 1 enforced findings (or a report the runner failed to
produce), 2 the checker itself could not run.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
from typing import Dict, List, Tuple

ENFORCED_ROOTS = ("packs/", "docs/modding/", "campaigns/")

SEVERITY = {1: "error", 2: "warning", 3: "information", 4: "hint"}


def uri_to_rel(uri: str, repo_root: pathlib.Path) -> str:
    path = urllib.parse.unquote(urllib.parse.urlparse(uri).path)
    try:
        return pathlib.Path(path).resolve().relative_to(repo_root).as_posix()
    except ValueError:
        return path


def run_check(luals: str, repo_root: pathlib.Path, log_dir: pathlib.Path,
              checklevel: str) -> pathlib.Path:
    """Run LuaLS over the workspace; answer the JSON report path."""
    log_dir.mkdir(parents=True, exist_ok=True)
    report = log_dir / "check.json"
    if report.exists():
        report.unlink()  # a stale report must never stand in for this run
    cmd = [
        luals,
        "--check", str(repo_root),
        f"--checklevel={checklevel}",
        f"--logpath={log_dir}",
        f"--check_out_path={report}",
    ]
    try:
        proc = subprocess.run(
            cmd, cwd=repo_root, capture_output=True, text=True)
    except OSError as exc:
        print(f"check_luals: cannot run {luals}: {exc}", file=sys.stderr)
        print("check_luals: lua-language-server comes from the dev shell "
              "(flake.nix); enter it or pass --luals-exe.", file=sys.stderr)
        sys.exit(2)
    # Non-zero is HOW LuaLS reports findings; only a missing report means
    # the check itself did not happen.
    if not report.exists():
        print(f"check_luals: {luals} exited {proc.returncode} without "
              f"writing {report} — the check did not run.", file=sys.stderr)
        tail = (proc.stdout + proc.stderr).strip().splitlines()[-12:]
        for line in tail:
            print(f"  {line}", file=sys.stderr)
        sys.exit(1)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="lua-language-server --check with the og.* stubs; "
        "zero diagnostics under the enforced roots.")
    parser.add_argument("--repo-root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--luals-exe", default=None,
                        help="lua-language-server binary (default: PATH)")
    parser.add_argument("--log-dir", type=pathlib.Path, default=None,
                        help="where LuaLS writes its log + check.json "
                        "(default: a fresh temp dir)")
    parser.add_argument("--checklevel", default="Warning",
                        choices=["Error", "Warning", "Information", "Hint"],
                        help="minimum severity LuaLS reports (default "
                        "Warning; Error would WEAKEN the gate — don't)")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    luals = args.luals_exe or shutil.which("lua-language-server")
    if not luals:
        print("check_luals: no lua-language-server on PATH and no "
              "--luals-exe given. The dev shell (flake.nix) provides it.",
              file=sys.stderr)
        return 2

    version = subprocess.run([luals, "--version"], capture_output=True,
                             text=True).stdout.strip()
    log_dir = args.log_dir or pathlib.Path(
        tempfile.mkdtemp(prefix="og-luals-check-"))
    report_path = run_check(luals, repo_root, log_dir, args.checklevel)

    with open(report_path, "r", encoding="utf-8") as f:
        report = json.load(f)

    enforced: List[Tuple[str, int, str, str, str]] = []
    advisory: List[Tuple[str, int, str, str, str]] = []
    # 3.18.1 writes a dict keyed by file URI, or a bare [] when clean. Any
    # OTHER shape is a format change this parser does not understand, and
    # an ununderstood report must fail, not pass: findings could be inside.
    if isinstance(report, dict):
        findings: Dict[str, List[dict]] = report
    elif isinstance(report, list) and not report:
        findings = {}
    else:
        print(f"check_luals: unrecognized report shape in {report_path} "
              f"({type(report).__name__}, LuaLS {version}) — update this "
              "parser before trusting the result.", file=sys.stderr)
        return 1
    for uri, diags in findings.items():
        rel = uri_to_rel(uri, repo_root)
        for diag in diags:
            row = (
                rel,
                diag.get("range", {}).get("start", {}).get("line", -1) + 1,
                SEVERITY.get(diag.get("severity"), "?"),
                str(diag.get("code", "?")),
                diag.get("message", "").split("\n")[0],
            )
            if rel.startswith(ENFORCED_ROOTS):
                enforced.append(row)
            else:
                advisory.append(row)

    for title, rows in (("ENFORCED", sorted(enforced)),
                        ("advisory (outside the enforced roots)",
                         sorted(advisory))):
        if rows:
            print(f"{len(rows)} {title} LuaLS finding(s):")
            for rel, line, sev, code, msg in rows:
                print(f"  {rel}:{line}: [{sev}] {code}: {msg}")

    scope = " + ".join(ENFORCED_ROOTS)
    if enforced:
        print(f"\ncheck_luals: FAIL — {len(enforced)} finding(s) under "
              f"{scope} (LuaLS {version}, level {args.checklevel}).")
        print("Fix the code or the generated types "
              "(scripts/modding/gen_api_stubs.py); annotate a genuinely "
              "dynamic spot with a targeted cast + why-comment; never "
              "blanket-disable.")
        return 1
    print(f"check_luals: clean under {scope} "
          f"(LuaLS {version}, level {args.checklevel}"
          + (f"; {len(advisory)} advisory finding(s) elsewhere"
             if advisory else "")
          + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
