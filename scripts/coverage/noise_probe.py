#!/usr/bin/env python3
"""Run-to-run coverage noise probe for the C++ half (#338).

The coverage gate reads ONE pass of the suite. If identical binaries cover
different lines from run to run, a margin smaller than that spread means
nothing. This tool measures the spread so it can be attributed and recorded
(scripts/coverage/README.md, "Run-to-run noise floor").

Two subcommands:

  capture BUILD_DIR OUT.info
      Runs gcovr over BUILD_DIR exactly as coverage_report.py does (same
      --root/--filter/parse-error flags, same LCOV-1.x spelling probe) and
      writes OUT.info. gcovr's stderr is kept next to it as OUT.info.log so
      negative/suspicious-hit warnings (counter races, root cause F) can be
      counted per run.

  compare RUN.info [RUN.info ...] [--baseline A.info ... --compare B.info ...]
      Without --baseline: reads N tracefiles of the SAME binary set, each
      captured after deleting every .gcda, and reports per source file the
      lines always hit, never hit and NOISY (hit in some runs, not in others),
      the noisy line numbers, and the min/max total of hit lines.
      With --baseline/--compare: reports both sets' noise, then the noisy
      lines that the compare set no longer flips and the ones it newly flips
      (the before/after of a fix).

A line's hit bit is `count > 0`; counts themselves are not compared (a busy
loop that spins 3 or 300 times is not coverage noise). The tool reads only
`SF:`, `DA:` and `end_of_record`; it never edits a tracefile and is never a
CMake COMMAND (R-std-9).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
Line = Tuple[str, int]


def read_hits(path: Path) -> Dict[Line, bool]:
    """{(source, line): hit} for every DA record in one tracefile."""
    hits: Dict[Line, bool] = {}
    source = None
    with path.open(encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if line.startswith("SF:"):
                source = line[3:]
                try:
                    source = str(Path(source).resolve().relative_to(REPO_ROOT))
                except ValueError:
                    pass
            elif line.startswith("DA:") and source is not None:
                fields = line[3:].split(",")
                number = int(fields[0])
                count = int(fields[1]) if fields[1].lstrip("-").isdigit() else 0
                key = (source, number)
                hits[key] = hits.get(key, False) or count > 0
            elif line == "end_of_record":
                source = None
    return hits


def noise(runs: List[Dict[Line, bool]]) -> Tuple[Dict[str, dict], List[int]]:
    """Per-file always/never/noisy census over N runs, plus per-run totals."""
    keys: Set[Line] = set()
    for run in runs:
        keys.update(run)
    files: Dict[str, dict] = {}
    for key in sorted(keys):
        bits = [run.get(key, False) for run in runs]
        entry = files.setdefault(
            key[0], {"always": 0, "never": 0, "noisy": [], "flips": {}})
        if all(bits):
            entry["always"] += 1
        elif not any(bits):
            entry["never"] += 1
        else:
            entry["noisy"].append(key[1])
            entry["flips"][key[1]] = sum(bits)
    totals = [sum(1 for hit in run.values() if hit) for run in runs]
    return files, totals


def noisy_set(files: Dict[str, dict]) -> Set[Line]:
    return {(name, n) for name, entry in files.items() for n in entry["noisy"]}


def compress(numbers: List[int]) -> str:
    """1,2,3,7 -> '1-3, 7'."""
    out: List[str] = []
    start = prev = None
    for n in numbers:
        if start is None:
            start = prev = n
        elif n == prev + 1:
            prev = n
        else:
            out.append(f"{start}" if start == prev else f"{start}-{prev}")
            start = prev = n
    if start is not None:
        out.append(f"{start}" if start == prev else f"{start}-{prev}")
    return ", ".join(out)


def report(label: str, paths: List[Path]) -> Set[Line]:
    runs = [read_hits(p) for p in paths]
    files, totals = noise(runs)
    noisy = noisy_set(files)
    print(f"== {label}: {len(paths)} run(s)")
    for path, total in zip(paths, totals):
        print(f"   {total:7d} lines hit  {path}")
    if totals:
        print(f"   lines_hit min {min(totals)} max {max(totals)} "
              f"spread {max(totals) - min(totals)}")
    print(f"   noisy lines: {len(noisy)} in "
          f"{sum(1 for e in files.values() if e['noisy'])} file(s)")
    for name in sorted(files):
        entry = files[name]
        if not entry["noisy"]:
            continue
        print(f"   {name}: always {entry['always']} never {entry['never']} "
              f"noisy {len(entry['noisy'])}: {compress(entry['noisy'])}")
    return noisy


def print_set(title: str, lines: Set[Line]) -> None:
    print(f"== {title}: {len(lines)}")
    by_file: Dict[str, List[int]] = {}
    for name, number in lines:
        by_file.setdefault(name, []).append(number)
    for name in sorted(by_file):
        print(f"   {name}: {compress(sorted(by_file[name]))}")


def cmd_capture(args: argparse.Namespace) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import coverage_report  # noqa: E402  (same directory, import-safe)

    gcovr = coverage_report.shutil.which("gcovr")
    base = [gcovr] if gcovr else [sys.executable, "-m", "gcovr"]
    flag = coverage_report.gcovr_lcov_1x_flag(base, REPO_ROOT)
    out = Path(args.out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = base + [
        "--root", str(REPO_ROOT),
        "--filter", "src/",
        "--gcov-ignore-parse-errors=suspicious_hits.warn_once_per_file",
        "--gcov-ignore-parse-errors=negative_hits.warn_once_per_file",
        flag, "--lcov", str(out), str(Path(args.build_dir).resolve()),
    ]
    log = out.with_name(out.name + ".log")
    with log.open("w", encoding="utf-8") as fh:
        proc = subprocess.run(cmd, cwd=REPO_ROOT, check=False,
                              stdout=fh, stderr=subprocess.STDOUT)
    text = log.read_text(encoding="utf-8", errors="replace")
    print(f"{out}: gcovr exit {proc.returncode}; "
          f"negative_hits warnings {text.count('negative_hits')}; "
          f"suspicious_hits warnings {text.count('suspicious_hits')}")
    return proc.returncode


def cmd_compare(args: argparse.Namespace) -> int:
    if args.baseline:
        before = report("baseline", [Path(p) for p in args.baseline])
        after = report("compare", [Path(p) for p in args.compare or []])
        print_set("noise removed (noisy in baseline, stable in compare)",
                  before - after)
        print_set("noise added (stable in baseline, noisy in compare)",
                  after - before)
        return 0
    report("runs", [Path(p) for p in args.runs])
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    cap = sub.add_parser("capture", help="gcovr a build dir into one .info")
    cap.add_argument("build_dir")
    cap.add_argument("out")
    cap.set_defaults(func=cmd_capture)
    cmp_ = sub.add_parser("compare", help="noise census over N tracefiles")
    cmp_.add_argument("runs", nargs="*")
    cmp_.add_argument("--baseline", nargs="+")
    cmp_.add_argument("--compare", nargs="+")
    cmp_.set_defaults(func=cmd_compare)
    args = parser.parse_args()
    if args.command == "compare" and not args.baseline and len(args.runs) < 2:
        parser.error("compare needs at least two tracefiles")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
