#!/usr/bin/env python3
"""Warm and merge exact-revision sccache entries for native CI builds.

Each compile worker configures the complete lane, derives the same plan from
``compile_commands.json``, and executes one disjoint set of exact compiler
commands. The workers transport cache entries, never object files. The
authoritative producer then configures and builds the ordinary complete graph
against the merged cache, so Ninja still owns every dependency, archive, link,
and gate.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import shlex
import subprocess


FORMAT = 1


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def cmake_compiler_launcher(build: Path) -> list[str]:
    cache = build / "CMakeCache.txt"
    prefix = "CMAKE_CXX_COMPILER_LAUNCHER:"
    matches = [line.split("=", 1)[1] for line in cache.read_text().splitlines()
               if line.startswith(prefix) and "=" in line]
    if len(matches) != 1:
        raise ValueError("CMake cache must contain exactly one C++ compiler launcher")
    launcher = matches[0].split(";")
    if not launcher or any(not argument for argument in launcher):
        raise ValueError("CMake C++ compiler launcher is empty or malformed")
    return launcher


def normalized_launcher(launcher: list[str]) -> list[str]:
    return [
        "SCCACHE_DIR=<sandbox-cache>" if argument.startswith("SCCACHE_DIR=") else argument
        for argument in launcher
    ]


def compile_plan(build: Path, shard_count: int) -> dict:
    if shard_count < 1:
        raise ValueError("compile shard count must be positive")
    build = build.resolve(strict=True)
    database_path = build / "compile_commands.json"
    database = json.loads(database_path.read_text())
    launcher = cmake_compiler_launcher(build)
    records = []
    seen = set()
    for entry in database:
        output_value = entry.get("output")
        if not output_value:
            raise ValueError("compile database entry has no output")
        output = Path(output_value)
        if not output.is_absolute():
            output = Path(entry["directory"]) / output
        output = output.resolve()
        if not output.is_relative_to(build):
            raise ValueError(f"compile output escapes build tree: {output}")
        relative = output.relative_to(build).as_posix()
        if relative in seen:
            raise ValueError(f"duplicate compile output: {relative}")
        seen.add(relative)
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        source = source.resolve()
        try:
            weight = max(source.stat().st_size, 1)
        except FileNotFoundError as error:
            raise ValueError(f"compile source does not exist: {source}") from error
        arguments = entry.get("arguments")
        if arguments is None:
            arguments = shlex.split(entry["command"])
        # Nix gives every sandbox a unique NIX_BUILD_TOP.  It is carried only
        # as sccache's storage location, not as a compiler argument or cache
        # key, so erase it from the cross-worker plan identity.
        normalized_arguments = [
            "SCCACHE_DIR=<sandbox-cache>" if argument.startswith("SCCACHE_DIR=") else argument
            for argument in arguments
        ]
        records.append({
            "output": relative,
            "source": str(source),
            "weight": weight,
            "command": normalized_arguments,
            "arguments": arguments,
            "directory": str(Path(entry["directory"]).resolve()),
            "absolute_output": str(output),
            "coverage": "--coverage" in normalized_arguments,
        })
    if not records:
        raise ValueError("compile database is empty")

    # Largest-first bin packing is deterministic and gives source-heavy test
    # TUs a much better cold-build balance than a name hash or round robin.
    assignments = [[] for _ in range(shard_count)]
    weights = [0] * shard_count
    for record in sorted(records, key=lambda item: (-item["weight"], item["output"])):
        shard = min(range(shard_count), key=lambda index: (weights[index], index))
        assignments[shard].append(record["output"])
        weights[shard] += record["weight"]
    for assignment in assignments:
        assignment.sort()

    fingerprint_records = [
        {"output": item["output"], "source": item["source"], "command": item["command"]}
        for item in sorted(records, key=lambda item: item["output"])
    ]
    fingerprint = hashlib.sha256(json.dumps(
        {"launcher": normalized_launcher(launcher), "records": fingerprint_records},
        sort_keys=True, separators=(",", ":")
    ).encode()).hexdigest()
    return {
        "format": FORMAT,
        "fingerprint": fingerprint,
        "record_count": len(records),
        "shard_count": shard_count,
        "assignments": assignments,
        "weights": weights,
        "coverage_outputs": sorted(item["output"] for item in records if item["coverage"]),
        "compile_records": {item["output"]: item for item in records},
        "launcher": launcher,
    }


def warm(args: argparse.Namespace) -> None:
    plan = compile_plan(args.build, args.shards)
    if args.shard < 0 or args.shard >= args.shards:
        raise ValueError(f"compile shard index must be in [0, {args.shards})")
    outputs = plan["assignments"][args.shard]
    if not outputs:
        raise ValueError(f"compile shard {args.shard} is empty")
    # CMake's per-object Ninja edges carry target-order dependencies. Asking
    # Ninja for a test object can therefore compile hundreds of shared library
    # objects, duplicating them in every worker. Generate the one build-time
    # header first, then run exactly the selected compile-database commands.
    subprocess.run(
        ["ninja", "-C", str(args.build.resolve()), "og_git_hash_gen"], check=True)
    cache_program = getattr(args, "cache_program", None)
    if cache_program is not None:
        cache_program = cache_program.resolve(strict=True)
        if not cache_program.is_absolute() or cache_program.name != "sccache":
            raise ValueError("compile cache program must be an absolute sccache executable")
        if plan["launcher"].count(str(cache_program)) != 1:
            raise ValueError("CMake compiler launcher must invoke the pinned sccache exactly once")

    def compile_output(output: str) -> None:
        record = plan["compile_records"][output]
        Path(record["absolute_output"]).parent.mkdir(parents=True, exist_ok=True)
        # compile_commands.json deliberately omits CMake's launcher. Prefix
        # the exact cached launcher so direct workers preserve the producer's
        # captured Nix compiler environment as well as its sccache invocation.
        command = [*plan["launcher"], *record["arguments"]]
        subprocess.run(command, cwd=record["directory"], check=True)

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        list(executor.map(compile_output, outputs))
    cache_requests = 0
    if cache_program is not None:
        cache_stats = json.loads(subprocess.check_output(
            [str(cache_program), "--show-stats", "--stats-format", "json"], text=True))
        cache_requests = cache_stats["stats"]["compile_requests"]
        executed = cache_stats["stats"]["requests_executed"]
        if cache_requests != len(outputs) or executed != len(outputs):
            raise ValueError(
                f"sccache observed {cache_requests} requests ({executed} executed), "
                f"expected exactly {len(outputs)}"
            )
    coverage_notes = 0
    if args.lane == "coverage":
        missing = []
        measured_outputs = set(plan["coverage_outputs"])
        for output in outputs:
            if output not in measured_outputs:
                continue
            note = args.build.resolve() / (output[:-2] + ".gcno")
            if not output.endswith(".o") or not note.is_file():
                missing.append(str(note))
            else:
                coverage_notes += 1
        if missing:
            raise ValueError(f"coverage compile cache did not materialize notes: {missing[:3]}")
    write_json(args.out, {
        "format": FORMAT,
        "lane": args.lane,
        "revision": args.revision,
        "fingerprint": plan["fingerprint"],
        "record_count": plan["record_count"],
        "shard_count": args.shards,
        "shard": args.shard,
        "assigned_count": len(outputs),
        "assigned_weight": plan["weights"][args.shard],
        "cache_requests": cache_requests,
        "coverage_notes": coverage_notes,
    })
    print(f"Warmed compile shard {args.shard + 1}/{args.shards}: {len(outputs)} objects")


def cache_root(root: Path, *, required: bool) -> Path | None:
    root = root.resolve()
    cache = root / "cache"
    if cache.is_dir():
        return cache
    if required:
        raise ValueError(f"sccache root has no cache directory: {root}")
    return None


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def same_file(left: Path, right: Path) -> bool:
    return left.stat().st_size == right.stat().st_size and file_digest(left) == file_digest(right)


def immutable_cache_files(source: Path) -> list[Path]:
    # sccache result entries are addressed by their content key and immutable.
    # cache/preprocessor is a mutable source-to-result index: two independent
    # workers legitimately update the same index with different result sets.
    # Do not transport those indexes. The producer will preprocess, derive the
    # immutable result key, and then retrieve the cached object.
    return [path for path in source.rglob("*")
            if path.is_file() and path.relative_to(source).parts[0] != "preprocessor"]


def copy_cache_entries(source: Path, destination: Path, *, baseline: Path | None = None,
                       max_bytes: int | None = None) -> tuple[int, int]:
    copied_files = 0
    copied_bytes = 0
    paths = immutable_cache_files(source)
    if max_bytes is not None:
        if max_bytes < 1:
            raise ValueError("maximum cache publication size must be positive")
        # A merged seed can exceed sccache's limit because workers report
        # additions, not independent LRU deletions. Retain the newest cache
        # files explicitly so the GitHub cache never grows without bound.
        newest = sorted(paths, key=lambda path: (-path.stat().st_mtime_ns,
                                                 path.relative_to(source).as_posix()))
        retained = []
        retained_bytes = 0
        for path in newest:
            size = path.stat().st_size
            if retained_bytes + size <= max_bytes:
                retained.append(path)
                retained_bytes += size
        paths = retained
    for path in sorted(paths):
        relative = path.relative_to(source)
        if baseline is not None:
            old = baseline / relative
            if old.exists():
                if not old.is_file() or not same_file(path, old):
                    raise ValueError(f"sccache result changed at content key: {relative}")
                continue
        target = destination / relative
        if target.exists():
            if not target.is_file() or not same_file(path, target):
                raise ValueError(f"conflicting sccache result entry: {relative}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        copied_files += 1
        copied_bytes += path.stat().st_size
    return copied_files, copied_bytes


def publish(args: argparse.Namespace) -> None:
    live_cache = args.cache.resolve(strict=True)
    if args.output.exists():
        raise ValueError(f"cache output already exists: {args.output}")
    destination = args.output / "cache"
    destination.mkdir(parents=True)
    baseline = cache_root(args.seed, required=True) if args.seed else None
    if baseline is not None and args.max_bytes is not None:
        raise ValueError("cache-size limits apply only to full publications")
    copied_files, copied_bytes = copy_cache_entries(
        live_cache, destination, baseline=baseline, max_bytes=args.max_bytes)
    metadata = {
        "format": FORMAT,
        "lane": args.lane,
        "revision": args.revision,
        "cache_files": copied_files,
        "cache_bytes": copied_bytes,
        "delta": baseline is not None,
    }
    if args.shard_manifest:
        shard = json.loads(args.shard_manifest.read_text())
        for field in ("format", "lane", "revision"):
            if shard[field] != metadata[field]:
                raise ValueError(f"shard manifest {field} does not match cache publication")
        metadata.update(shard)
        write_json(args.output / "shard.json", metadata)
    else:
        write_json(args.output / "cache.json", metadata)
    print(f"Published {copied_files} sccache files ({copied_bytes} bytes)")


def merge(args: argparse.Namespace) -> None:
    if args.output.exists():
        raise ValueError(f"merged cache output already exists: {args.output}")
    destination = args.output / "cache"
    destination.mkdir(parents=True)
    if args.seed:
        seed_cache = cache_root(args.seed, required=True)
        copy_cache_entries(seed_cache, destination)

    manifests = []
    for root in args.input:
        manifest_path = root / "shard.json"
        if not manifest_path.is_file():
            raise ValueError(f"compile-cache shard manifest missing: {manifest_path}")
        manifest = json.loads(manifest_path.read_text())
        manifests.append(manifest)
        delta = cache_root(root, required=False)
        if delta is not None:
            copy_cache_entries(delta, destination)
    if len(manifests) != args.shards:
        raise ValueError(f"expected {args.shards} compile-cache shards, found {len(manifests)}")
    expected_indexes = list(range(args.shards))
    actual_indexes = sorted(manifest["shard"] for manifest in manifests)
    if actual_indexes != expected_indexes:
        raise ValueError(f"compile-cache shard set is not complete: {actual_indexes}")
    first = manifests[0]
    for manifest in manifests:
        for field, expected in (("format", FORMAT), ("lane", args.lane),
                                ("revision", args.revision), ("shard_count", args.shards),
                                ("fingerprint", first["fingerprint"]),
                                ("record_count", first["record_count"])):
            if manifest.get(field) != expected:
                raise ValueError(f"compile-cache shard {manifest.get('shard')} has mismatched {field}")
    assigned_count = sum(manifest.get("assigned_count", -1) for manifest in manifests)
    if assigned_count != first["record_count"]:
        raise ValueError(
            f"compile-cache shards assign {assigned_count} objects, "
            f"expected {first['record_count']}"
        )
    merged_files = [path for path in destination.rglob("*") if path.is_file()]
    write_json(args.output / "cache.json", {
        "format": FORMAT,
        "lane": args.lane,
        "revision": args.revision,
        "fingerprint": first["fingerprint"],
        "record_count": first["record_count"],
        "shard_count": args.shards,
        "cache_files": len(merged_files),
        "cache_bytes": sum(path.stat().st_size for path in merged_files),
    })
    print(f"Merged all {args.shards} compile-cache shards into {args.output}")


def statistic_total(stats: dict, name: str) -> int:
    value = stats[name]
    if isinstance(value, int):
        return value
    return sum(value["counts"].values())


def verify_producer(args: argparse.Namespace) -> None:
    cache_program = args.cache_program.resolve(strict=True)
    stats = json.loads(subprocess.check_output(
        [str(cache_program), "--show-stats", "--stats-format", "json"], text=True))["stats"]
    requests = stats["compile_requests"]
    executed = stats["requests_executed"]
    hits = statistic_total(stats, "cache_hits")
    misses = statistic_total(stats, "cache_misses")
    errors = statistic_total(stats, "cache_errors")
    compilations = stats["compilations"]
    if requests < 1 or (requests, executed, hits, misses, errors, compilations) != (
            requests, requests, requests, 0, 0, 0):
        raise ValueError(
            "authoritative producer did not reuse every warmed compilation: "
            f"requests={requests}, executed={executed}, hits={hits}, misses={misses}, "
            f"errors={errors}, compilations={compilations}"
        )
    print(f"Authoritative producer reused all {requests} warmed compilations")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    warming = commands.add_parser("warm", help="compile one disjoint object shard")
    warming.add_argument("--build", type=Path, required=True)
    warming.add_argument("--lane", choices=("asan", "coverage"), required=True)
    warming.add_argument("--revision", required=True)
    warming.add_argument("--shards", type=int, required=True)
    warming.add_argument("--shard", type=int, required=True)
    warming.add_argument("--jobs", type=int, required=True)
    warming.add_argument("--out", type=Path, required=True)
    warming.add_argument("--cache-program", type=Path)
    warming.set_defaults(function=warm)

    publishing = commands.add_parser("publish", help="publish a full cache or shard delta")
    publishing.add_argument("--cache", type=Path, required=True)
    publishing.add_argument("--seed", type=Path)
    publishing.add_argument("--output", type=Path, required=True)
    publishing.add_argument("--lane", choices=("asan", "coverage"), required=True)
    publishing.add_argument("--revision", required=True)
    publishing.add_argument("--shard-manifest", type=Path)
    publishing.add_argument("--max-bytes", type=int)
    publishing.set_defaults(function=publish)

    merging = commands.add_parser("merge", help="verify and merge every shard delta")
    merging.add_argument("--seed", type=Path)
    merging.add_argument("--input", type=Path, action="append", required=True)
    merging.add_argument("--output", type=Path, required=True)
    merging.add_argument("--lane", choices=("asan", "coverage"), required=True)
    merging.add_argument("--revision", required=True)
    merging.add_argument("--shards", type=int, required=True)
    merging.set_defaults(function=merge)

    verifying = commands.add_parser(
        "verify-producer", help="require the authoritative build to compile no objects")
    verifying.add_argument("--cache-program", type=Path, required=True)
    verifying.set_defaults(function=verify_producer)

    args = parser.parse_args()
    args.function(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
