#!/usr/bin/env python3
"""Replay, mutate, and minimize a corpus against an instrumented game loader."""
from __future__ import annotations

import argparse
from collections import deque
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import threading
import time


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_corpus(source: Path, destination: Path) -> None:
    for path in sorted(source.iterdir()):
        if path.is_file():
            shutil.copyfile(path, destination / digest(path))


def instrumentation(build: Path) -> dict:
    commands = json.loads((build / "compile_commands.json").read_text())
    required = {"src/resources/level_file_io.cpp", "src/resources/save_data.cpp"}
    seen: dict[str, str] = {}
    for entry in commands:
        source = entry["file"]
        relative = next((name for name in required if source.endswith("/" + name)), None)
        if relative is None:
            continue
        flags = entry.get("arguments") or shlex.split(entry["command"])
        enabled = set()
        for flag in flags:
            if flag.startswith("-fsanitize="):
                enabled.update(flag.split("=", 1)[1].split(","))
        if not {"fuzzer-no-link", "address", "undefined", "float-cast-overflow"} <= enabled:
            raise RuntimeError(f"production parser is not fully fuzz-instrumented: {relative}")
        if any(flag == "-DTESTING" or flag.startswith("-DTESTING=") for flag in flags):
            raise RuntimeError(f"fuzzer uses TESTING implementation: {relative}")
        seen[relative] = digest(Path(source))
    if set(seen) != required:
        raise RuntimeError("production parser compilation records are missing")
    return seen


def run_phase(binary: Path, arguments: list[str], output: Path,
              phase: str, timeout: int) -> dict:
    log = output / f"{phase}.log.gz"
    tail: deque[str] = deque(maxlen=80)
    counters: list[dict] = []
    read_errors: list[Exception] = []
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    command = [str(binary), *arguments]
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=environment,
                               start_new_session=True)
    assert process.stdout is not None

    def collect() -> None:
        try:
            with gzip.open(log, "wb") as stream:
                for raw in process.stdout:
                    stream.write(raw)
                    line = raw.decode("utf-8", errors="replace").rstrip()
                    tail.append(line)
                    match = re.search(r"#(\d+)\s+(INITED|NEW|REDUCE|DONE).*cov: (\d+) ft: (\d+)", line)
                    if match:
                        counters.append(dict(executions=int(match[1]), event=match[2],
                                             edges=int(match[3]), features=int(match[4])))
        except Exception as error:
            read_errors.append(error)
            os.killpg(process.pid, signal.SIGKILL)

    reader = threading.Thread(target=collect)
    reader.start()
    timed_out = False
    try:
        status = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGKILL)
        status = process.wait()
    reader.join()
    process.stdout.close()
    result = dict(phase=phase, command=command, exit_code=status,
                  timeout=timed_out, seconds=round(time.monotonic() - started, 3),
                  first=counters[0] if counters else None,
                  last=counters[-1] if counters else None, log=log.name)
    (output / f"{phase}.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    if status != 0 or timed_out or read_errors:
        print("\n".join(tail), flush=True)
        raise RuntimeError(f"{phase} failed; retained full log and reproducer")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("scenario", "savefile"), required=True)
    parser.add_argument("--build", type=Path, default=Path("build/ci-fuzz"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--minimize", action="store_true")
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error("--seconds must be positive")
    binary = (args.build / f"fuzz_{args.target}").resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    corpus = args.corpus.resolve()
    corpus.mkdir(parents=True, exist_ok=True)
    generated = output / "generated-seeds"
    generated.mkdir(exist_ok=True)
    crashes = output / "crashes"
    crashes.mkdir(exist_ok=True)
    summary = dict(target=args.target, binary_sha256=digest(binary),
                   revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                   seed=args.seed, seconds=args.seconds, phases=[], complete=False)
    try:
        summary["production_sources"] = instrumentation(args.build)
        summary["phases"].append(run_phase(binary, ["--generate-seeds", str(generated)],
                                            output, "seeds", 120))
        copy_corpus(Path("tests/fuzz/corpus") / args.target, corpus)
        copy_corpus(generated, corpus)
        summary["inputs_before"] = len(list(corpus.iterdir()))
        common = ["-timeout=15", "-rss_limit_mb=2048", "-max_len=1048576",
                  "-print_final_stats=1", f"-artifact_prefix={crashes}/"]
        # -runs=0 still executes every initial corpus entry; no mutation.
        summary["phases"].append(run_phase(binary, [str(corpus), "-runs=0", *common],
                                            output, "replay", 300))
        summary["phases"].append(run_phase(binary, [str(corpus), f"-max_total_time={args.seconds}",
                                            f"-seed={args.seed}", "-use_value_profile=1",
                                            f"-dict=tests/fuzz/{args.target}.dict", *common],
                                            output, "fuzz", args.seconds + 90))
        if args.minimize:
            minimized = output / "minimized"
            minimized.mkdir(exist_ok=True)
            summary["phases"].append(run_phase(binary, ["-merge=1", str(minimized), str(corpus), *common],
                                                output, "minimize", 600))
            # Keep a nonempty replayable corpus even if all interesting edges
            # move into initialization in a later binary.
            if not any(minimized.iterdir()):
                raise RuntimeError("minimization produced an empty corpus")
            for path in corpus.iterdir():
                if path.is_file():
                    path.unlink()
            copy_corpus(minimized, corpus)
        summary["inputs_after"] = len(list(corpus.iterdir()))
        summary["complete"] = True
    finally:
        (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
