#!/usr/bin/env python3
"""Require real sanitizer failures and exact source lines from a bundled fixture."""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess


def diagnostic_cases(lane: str) -> dict[str, tuple[str, str]]:
    if lane == "tsan":
        return {"race": ("ThreadSanitizer: data race", "shared_value = value;")}
    return {
        "global": ("global-buffer-overflow", "return global_pointer()[index];"),
        "heap": ("heap-buffer-overflow", "const int result = values[index];"),
        "integer": ("signed integer overflow", "return maximum + index;"),
        "float": ("outside the range of representable values", "return static_cast<int>(oversized);"),
    }


def verify_diagnostic(output: str, status: int, diagnostic: str, filename: str, line: int) -> None:
    if status == 0:
        raise ValueError(f"sanitizer failed to reject {diagnostic}")
    if diagnostic not in output or re.search(re.escape(filename) + rf":{line}(?=[:\s]|$)", output) is None:
        raise ValueError(f"missing {diagnostic} with exact source line {filename}:{line}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lane", required=True, choices=("asan", "tsan", "fuzz"))
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).resolve().parents[2] / "tests/fixtures/sanitizer_diagnostics.cpp"
    source = fixture.read_text().splitlines()
    symbolizer = shutil.which("llvm-symbolizer")
    if symbolizer is None or not symbolizer.startswith("/nix/store/"):
        raise ValueError("the pinned LLVM symbolizer is required for diagnostic proof")
    environment = dict(os.environ, ASAN_SYMBOLIZER_PATH=symbolizer,
                       ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                       UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                       TSAN_OPTIONS="halt_on_error=1")
    results = {}
    for mode, (diagnostic, statement) in diagnostic_cases(args.lane).items():
        lines = [number for number, text in enumerate(source, 1) if statement in text]
        if len(lines) != 1:
            raise ValueError(f"diagnostic fixture statement is not unique: {mode}")
        result = subprocess.run([str(args.binary.resolve(strict=True)), mode], env=environment,
                                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=30)
        (args.out / f"{mode}.log").write_text(result.stdout)
        verify_diagnostic(result.stdout, result.returncode, diagnostic, fixture.name, lines[0])
        results[mode] = {"exit_code": result.returncode, "diagnostic": diagnostic,
                         "source_line": lines[0]}
    report = {"lane": args.lane, "binary": str(args.binary), "symbolizer": symbolizer,
              "test_retries": 0, "diagnostics": results}
    (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
