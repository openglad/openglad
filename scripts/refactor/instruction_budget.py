#!/usr/bin/env python3
"""Aggregate and gate the parity harness's per-scenario Lua instruction totals.

The raw input is the file OPENGLAD_LUA_INSTRUCTION_REPORT points og_test_parity
at: one "<scenario-id>\t<total>" line per scenario RUN, where <total> is the
exact cumulative VM instruction count the world-host budget hook observed for
that run (ScriptHost drops to a per-instruction budget-hook cadence under the
same variable — see budget_check_cadence in src/gameplay/script/script_host.cpp
and append_instruction_report in tests/parity/parity_runner.cpp). Scenarios
that run more than once in a suite pass (the Invariant dual-captures, the
smoke-divergence re-runs) appear once per run; the sim is deterministic, so
every run of one scenario must report the same total, and `aggregate` refuses
the file when any two disagree rather than average away a determinism bug.

Subcommands:

  aggregate --raw R.tsv --tree-sha SHA --out baseline.json
      Fold the raw runs into {scenario-id: total} and write the baseline JSON
      (sorted keys, stable formatting) with the capture tree's SHA inside.
      This is how scripts/refactor/baseline/instruction_baseline.json is
      (re)generated; see scripts/refactor/README.md for the full recipe.

  check --raw R.tsv --baseline baseline.json [--max-regression 0.10]
      Aggregate a candidate run the same way and fail (exit 1) when any
      scenario's total exceeds its baseline by more than --max-regression
      (default 10%, the budget the refactor plan holds helper indirection
      to). Improvements and sub-threshold growth pass. A scenario with a
      zero baseline fails on ANY growth (any increase from zero is more
      than 10%). Scenarios present only in the baseline are reported as
      warnings, not failures — scenario presence is og_test_parity's
      coverage gate's job, not this tool's; scenarios present only in the
      candidate (new scenarios) are reported and pass.

Exit status: 0 clean, 1 gate failure or malformed input.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

SCHEMA = "og-lua-instruction-baseline/1"


def read_raw(path: pathlib.Path) -> dict[str, int]:
    """Parse a raw report into {scenario: total}, refusing indeterminism."""
    totals: dict[str, int] = {}
    problems: list[str] = []
    lines = path.read_text().splitlines()
    for lineno, line in enumerate(lines, 1):
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 2 or not parts[1].isdigit():
            problems.append(f"{path}:{lineno}: malformed line: {line!r}")
            continue
        scenario, total = parts[0], int(parts[1])
        if scenario in totals and totals[scenario] != total:
            problems.append(
                f"{path}:{lineno}: scenario '{scenario}' reported {total} "
                f"but an earlier run reported {totals[scenario]} — "
                f"per-scenario instruction counts must be deterministic")
            continue
        totals[scenario] = total
    if not totals:
        problems.append(f"{path}: no scenario totals found — was "
                        "og_test_parity run with "
                        "OPENGLAD_LUA_INSTRUCTION_REPORT set to this path?")
    if problems:
        raise SystemExit("\n".join(problems))
    return totals


def cmd_aggregate(args: argparse.Namespace) -> int:
    totals = read_raw(pathlib.Path(args.raw))
    doc = {
        "schema": SCHEMA,
        "tree_sha": args.tree_sha,
        "cadence": "per-instruction",
        "scenarios": dict(sorted(totals.items())),
    }
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=2, sort_keys=False) + "\n")
    print(f"instruction baseline: {len(totals)} scenarios -> {out} "
          f"(tree {args.tree_sha[:12]})")
    return 0


def load_baseline(path: pathlib.Path) -> dict[str, int]:
    doc = json.loads(path.read_text())
    if doc.get("schema") != SCHEMA:
        raise SystemExit(
            f"{path}: schema {doc.get('schema')!r} is not {SCHEMA!r}; "
            "refusing to compare across formats")
    return {str(k): int(v) for k, v in doc["scenarios"].items()}


def cmd_check(args: argparse.Namespace) -> int:
    base = load_baseline(pathlib.Path(args.baseline))
    cur = read_raw(pathlib.Path(args.raw))

    regressions: list[str] = []
    improvements = 0
    for scenario in sorted(set(base) & set(cur)):
        b, c = base[scenario], cur[scenario]
        limit = b * (1.0 + args.max_regression)
        if c > limit:
            pct = (c - b) / b * 100.0 if b else float("inf")
            regressions.append(
                f"  {scenario}: {b} -> {c} (+{pct:.1f}%, limit "
                f"+{args.max_regression * 100:.0f}%)")
        elif c < b:
            improvements += 1

    only_base = sorted(set(base) - set(cur))
    only_cur = sorted(set(cur) - set(base))
    for scenario in only_base:
        print(f"warning: baseline scenario '{scenario}' missing from this "
              "run (og_test_parity's coverage gate owns scenario presence)")
    for scenario in only_cur:
        print(f"note: scenario '{scenario}' has no baseline yet "
              f"(current total {cur[scenario]})")

    compared = len(set(base) & set(cur))
    print(f"instruction budget: {compared} scenarios compared, "
          f"{improvements} improved, {len(regressions)} regressed beyond "
          f"{args.max_regression * 100:.0f}%")
    if regressions:
        print("instruction-budget REGRESSIONS:")
        print("\n".join(regressions))
        print("(rebaseline deliberately via scripts/refactor/README.md if "
              "the growth is accepted)")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    agg = sub.add_parser("aggregate", help="fold a raw report into baseline "
                                           "JSON")
    agg.add_argument("--raw", required=True,
                     help="raw TSV written by og_test_parity")
    agg.add_argument("--tree-sha", required=True,
                     help="git SHA of the tree the report was captured on")
    agg.add_argument("--out", required=True, help="baseline JSON to write")
    agg.set_defaults(func=cmd_aggregate)

    chk = sub.add_parser("check", help="gate a raw report against a baseline")
    chk.add_argument("--raw", required=True,
                     help="raw TSV written by og_test_parity")
    chk.add_argument("--baseline", required=True, help="baseline JSON")
    chk.add_argument("--max-regression", type=float, default=0.10,
                     help="max allowed per-scenario growth (default 0.10)")
    chk.set_defaults(func=cmd_check)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
