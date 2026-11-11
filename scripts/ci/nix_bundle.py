#!/usr/bin/env python3
r"""Build artifacts, not test results: transport and activate native Nix bundles.

Producer without a compile-cache seed (inside the Nix shell):
  nix build .#ci-coverage --out-link build/bundle-coverage
  python3 scripts/ci/nix_bundle.py export --bundle build/bundle-coverage \
      --cache build/nix-cache
For a local dirty checkout, preserve the real Git version metadata with:
  python3 scripts/ci/nix_bundle.py build --lane coverage \
      --repo "$PWD" --out-link build/bundle-coverage
This still builds the exact flake source through Nix; it passes the checkout's
history count and HEAD hash as explicit derivation attributes. ASan and coverage
may additionally receive a content-addressed `--sccache-seed`. Compile workers
select a disjoint object plan with `--compile-shards` / `--compile-shard` and
request only the derivation's `--cache-only` output. The final producer uses the
merged seed but still builds the complete ordinary target graph.

Upload the ENTIRE nix-cache directory as an artifact. Consumers download it,
then restore their fresh CTest tree:
  python3 scripts/ci/nix_bundle.py restore --cache build/nix-cache --repo "$PWD"

Restore verifies the compressed archive and NAR hashes, imports and roots the
runtime/compiler dependencies, and streams the product into an owned tree.
It moves that tree into place without importing a second product copy into
the Nix store. Producers use plan-tree for read-only CTest metadata queries;
their cache-hit import command can attach an output link to that exact product.
Neither operation configures, compiles, or runs tests. Each lane reserves one
/tmp/openglad-ci-<lane>/source alias on its runner. The alias resolves embedded
source macros, gcov source/profile paths, and canary incremental dependencies to
the real checkout (including its .git). Use separate runners for simultaneous
consumers of the same lane. CTest paths are also rewritten for readable reports.
Coverage .gcda files land beside their .gcno under build/ci-coverage, without
GCOV_PREFIX and without modifying the read-only Nix store.

Only the canary bundle retains incremental objects and Ninja's dependency DB.
Its original nanosecond timestamps are restored after Nix's timestamp
normalization, so a mutation rebuild compiles the mutated translation unit
instead of rebuilding the entire game. CMake's launchers retain the producer's
NIX_CFLAGS_COMPILE, NIX_LDFLAGS and NIX_HARDENING_ENABLE; these preserve
dependency paths that compiler detection classified as implicit. ASan and
coverage compiler launchers then chain the pinned `sccache`; link launchers and
all other lanes use only the pinned coreutils env. `pack` is the derivation's
internal step.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import re
import subprocess
import sys
import tempfile
import time


LANES = ("coverage", "asan", "tsan", "canary", "fuzz")
FORMAT = 1


def run(*args: str, cwd: Path | None = None) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def read_manifest(bundle: Path) -> dict:
    manifest = json.loads((bundle / "bundle.json").read_text())
    if manifest["format"] != FORMAT or manifest["lane"] not in LANES:
        raise ValueError("unsupported native CI bundle format or lane")
    preset = "ci-test" if manifest["lane"] == "canary" else f"ci-{manifest['lane']}"
    if manifest["preset"] != preset:
        raise ValueError("bundle preset does not match its lane")
    for collection in ("source_timestamps_ns", "build_timestamps_ns"):
        for relative, stamp in manifest[collection].items():
            if Path(relative).is_absolute() or ".." in Path(relative).parts or not isinstance(stamp, int) or stamp < 0:
                raise ValueError("invalid bundle timestamp path or value")
    return manifest


def validate_coverage_flags(build: Path) -> None:
    records = json.loads((build / "compile_commands.json").read_text())
    measured = 0
    for record in records:
        arguments = record.get("arguments") or shlex.split(record["command"])
        if "--coverage" not in arguments:
            continue
        measured += 1
        optimizations = [argument for argument in arguments
                         if re.fullmatch(r"-O(?:[0-9]+|g|s|z|fast)?", argument)]
        if not optimizations or optimizations[-1] != "-O0":
            raise ValueError(f"coverage object is not explicitly O0: {record['file']}")
        profile_updates = [argument for argument in arguments
                           if argument.startswith("-fprofile-update=")]
        if not profile_updates or profile_updates[-1] != "-fprofile-update=atomic":
            raise ValueError(f"coverage object does not use atomic counters: {record['file']}")
    if measured == 0:
        raise ValueError("coverage bundle contains no instrumented compilation records")


def pack(args: argparse.Namespace) -> None:
    source = args.source.resolve()
    build = args.build.resolve()
    if not build.is_relative_to(source):
        raise ValueError("build must be inside source")
    if args.output.exists():
        raise ValueError(f"output already exists: {args.output}")
    canary = args.lane == "canary"
    if args.lane == "coverage":
        validate_coverage_flags(build)

    def ignored(directory: str, names: list[str]) -> list[str]:
        return [name for name in names if
                name in ("Testing", "coverage", "__pycache__")
                or name.endswith(".gcda")
                or (not canary and (name.endswith((".o", ".a"))
                                    or name in (".ninja_deps", ".ninja_log")))]

    args.output.mkdir(parents=True)
    # The derivation owns this build tree and is finished compiling. Prune
    # transport-only waste, then move it into the store output: copying every
    # debug executable first would double producer disk use during install.
    for directory, directories, files in os.walk(build):
        for name in ignored(directory, directories + files):
            path = Path(directory) / name
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
                directories.remove(name)
            else:
                path.unlink()
    shutil.move(str(build), str(args.output / "build"))
    timestamps = {}
    if canary:
        # Record copied files' pre-store timestamps, including .ninja_deps and
        # generated headers. Ninja compares these against its binary dep log.
        for path in (args.output / "build").rglob("*"):
            if path.is_file() and not path.is_symlink():
                timestamps[str(path.relative_to(args.output / "build"))] = path.stat().st_mtime_ns
    source_times = {}
    if canary:
        tracked = subprocess.check_output(
            ["git", "ls-files", "-z"], cwd=source).decode().split("\0")
        for relative in filter(None, tracked):
            path = source / relative
            if path.is_file() and not path.is_symlink():
                source_times[relative] = path.stat().st_mtime_ns
    write_json(args.output / "bundle.json", {
        "format": FORMAT,
        "lane": args.lane,
        "preset": "ci-test" if canary else f"ci-{args.lane}",
        "source": str(source),
        "build_relative": str(build.relative_to(source)),
        "revision": args.revision,
        "build_timestamps_ns": timestamps,
        "source_timestamps_ns": source_times,
    })
    print(f"Packed {args.lane} compiled bundle at {args.output}")


def export_bundle(args: argparse.Namespace) -> None:
    bundle = args.bundle.resolve(strict=True)
    read_manifest(bundle)
    if not str(bundle).startswith("/nix/store/"):
        raise ValueError("export requires a realized Nix store bundle")
    cache = args.cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    # xz's default preset spends minutes compressing debug objects. A local
    # artifact cache favors zstd throughput; upload it without another ZIP pass.
    uri = cache.as_uri() + "?compression=zstd&compression-level=3"
    started = time.monotonic()
    subprocess.run(["nix", "copy", "--to", uri, str(bundle)], check=True)
    elapsed = time.monotonic() - started
    (cache / "bundle-path.txt").write_text(str(bundle) + "\n")
    cache_files = [path for path in cache.rglob("*") if path.is_file()]
    bundle_files = [path for path in bundle.rglob("*") if path.is_file()]
    closure = run("nix", "path-info", "--recursive", str(bundle)).splitlines()
    metrics = {
        "format": 1,
        "bundle": str(bundle),
        "copy_seconds": elapsed,
        "cache_bytes": sum(path.stat().st_size for path in cache_files),
        "cache_files": len(cache_files),
        "bundle_file_bytes": sum(path.stat().st_size for path in bundle_files),
        "closure_paths": len(closure),
    }
    metrics_path = cache.parent / "nar-export-metrics.json"
    write_json(metrics_path, metrics)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write(
                f"Native bundle export: {elapsed:.2f}s; "
                f"{metrics['cache_bytes']:,} cache bytes in {metrics['cache_files']} files; "
                f"{metrics['bundle_file_bytes']:,} bundle bytes; "
                f"{metrics['closure_paths']} closure paths.\n")
    print(f"Exported bundle and dependency closure to {cache}")
    print(f"Export metrics: {metrics_path}")


def import_bundle(args: argparse.Namespace) -> None:
    cache = args.cache.resolve(strict=True)
    bundle = (cache / "bundle-path.txt").read_text().strip()
    if not bundle.startswith("/nix/store/") or "\n" in bundle:
        raise ValueError("invalid bundle store path")
    # GitHub artifacts are authenticated by their run/job identity. This local
    # binary cache has no signing key; never use an untrusted run's artifact.
    subprocess.run(["nix", "copy", "--no-check-sigs", "--from", cache.as_uri(), bundle],
                   check=True)
    read_manifest(Path(bundle))
    out_link = getattr(args, "out_link", None)
    if out_link is not None:
        out_link = out_link.absolute()
        if out_link.exists() or out_link.is_symlink():
            raise ValueError(f"refusing to replace bundle output link: {out_link}")
        out_link.parent.mkdir(parents=True, exist_ok=True)
        out_link.symlink_to(bundle, target_is_directory=True)
    print(bundle)


def materialize_build(source: Path, destination: Path, *, immutable: bool) -> dict:
    """Share immutable code/notes on one filesystem; assets remain private."""
    linked = set()
    shared_bytes = 0
    copied_bytes = 0
    copied_files = 0

    def copy_file(src: str, dst: str) -> str:
        nonlocal shared_bytes, copied_bytes, copied_files
        original, target = Path(src), Path(dst)
        shareable = immutable and original.suffix == ".gcno"
        if immutable and not shareable and original.stat().st_mode & 0o111:
            with original.open("rb") as stream:
                shareable = stream.read(4) == b"\x7fELF"
        if shareable and original.stat().st_mode & 0o222 == 0:
            try:
                os.link(original, target)
            except OSError:
                # Different filesystems and filesystems without hardlinks use
                # ordinary copies. Never chmod a store inode after linking it.
                pass
            else:
                linked.add(target)
                shared_bytes += original.stat().st_size
                return str(target)
        copied_bytes += original.stat().st_size
        copied_files += 1
        return shutil.copy2(original, target)

    shutil.copytree(source, destination, symlinks=True, copy_function=copy_file)
    for path in [destination, *destination.rglob("*")]:
        if not path.is_symlink() and path not in linked:
            path.chmod(path.stat().st_mode | 0o200)
    return {"hardlinked_files": len(linked), "hardlinked_bytes": shared_bytes,
            "copied_files": copied_files, "copied_bytes": copied_bytes}


def activation_paths(manifest: dict, repo: Path) -> tuple[Path, Path]:
    checkout = Path(run("git", "rev-parse", "--show-toplevel", cwd=repo)).resolve()
    if checkout != repo:
        raise ValueError("--repo must be the root of a real git checkout")
    revision = manifest["revision"]
    if revision != "dirty":
        if run("git", "rev-parse", "HEAD", cwd=repo) != revision:
            raise ValueError("bundle revision does not match consumer checkout")
        if subprocess.run(["git", "diff", "--quiet", "HEAD", "--"], cwd=repo).returncode:
            raise ValueError("consumer has tracked edits that do not match its compiled bundle")
    canonical = Path(manifest["source"])
    expected = Path(f"/tmp/openglad-ci-{manifest['lane']}/source")
    if canonical != expected:
        raise ValueError(f"unexpected bundle source path: {canonical}")
    relative = Path(manifest["build_relative"])
    if relative != Path("build") / manifest["preset"]:
        raise ValueError(f"unexpected bundle build path: {relative}")
    destination = repo / relative
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"refusing to overwrite existing build tree: {destination}")
    if canonical.is_symlink():
        if canonical.resolve() != repo:
            raise ValueError(f"{canonical} is in use by another checkout")
    elif canonical.exists():
        raise ValueError(f"canonical source path already exists: {canonical}")
    return canonical, destination


def finish_activation(manifest: dict, repo: Path, canonical: Path, destination: Path) -> None:
    # This includes CTestTestfile.cmake in subdirectories. Shell/script CTest
    # entries must execute the checked-out sources, with their real history.
    for path in destination.rglob("CTestTestfile.cmake"):
        text = path.read_text()
        path.write_text(text.replace(str(canonical), str(repo)))
    for relative, mtime in manifest["source_timestamps_ns"].items():
        path = repo / relative
        if path.is_file() and not path.is_symlink():
            os.utime(path, ns=(mtime, mtime))
    for relative, mtime in manifest["build_timestamps_ns"].items():
        path = destination / relative
        os.utime(path, ns=(mtime, mtime), follow_symlinks=False)


def source_alias(canonical: Path, repo: Path) -> None:
    if canonical.is_symlink():
        if canonical.resolve() != repo:
            raise ValueError(f"{canonical} is in use by another checkout")
    else:
        canonical.parent.mkdir(parents=True, exist_ok=True)
        canonical.symlink_to(repo, target_is_directory=True)


def activate(args: argparse.Namespace) -> None:
    bundle = args.bundle.resolve(strict=True)
    manifest = read_manifest(bundle)
    repo = args.repo.resolve(strict=True)
    canonical, destination = activation_paths(manifest, repo)
    source_alias(canonical, repo)
    # Canary mutates and relinks outputs, so it must own every inode. Other
    # lanes only execute the compiled binaries and write profiles beside
    # their readonly notes. Assets always remain writable private copies.
    activation_started = time.monotonic()
    materialization = materialize_build(
        bundle / "build", destination, immutable=manifest["lane"] != "canary")
    materialization["seconds"] = time.monotonic() - activation_started
    write_json(destination / "bundle-activation-metrics.json", materialization)
    finish_activation(manifest, repo, canonical, destination)
    # Keep the realized bundle (and all ELF/compiler dependencies) reachable
    # during subsequent Nix garbage collection in long consumer jobs.
    subprocess.run(["nix-store", "--add-root", str(destination / ".nix-bundle"),
                    "--indirect", "--realise", str(bundle)], check=True)
    print(f"Activated {manifest['lane']}: {destination}")
    print(f"Shared immutable store bytes: {materialization['hardlinked_bytes']:,}")
    print(f"Run fresh tests with: ctest --test-dir {destination}")


STORE_BASENAME = r"[0-9abcdfghijklmnpqrsvwxyz]{32}-[A-Za-z0-9+._?=-]+"


def checked_store_path(value: str) -> str:
    if not re.fullmatch(r"/nix/store/" + STORE_BASENAME, value):
        raise ValueError("invalid bundle/dependency store path")
    return value


def cache_record(cache: Path, store_path: str) -> dict[str, str]:
    checked_store_path(store_path)
    info = cache / (Path(store_path).name[:32] + ".narinfo")
    if not info.resolve(strict=True).is_relative_to(cache):
        raise ValueError("cache metadata escapes its directory")
    record = {}
    for line in info.read_text().splitlines():
        key, separator, value = line.partition(": ")
        if not separator or key in record:
            raise ValueError("invalid or duplicate cache metadata field")
        record[key] = value
    if record.get("StorePath") != store_path:
        raise ValueError("cache metadata names a different store path")
    for reference in record.get("References", "").split():
        checked_store_path("/nix/store/" + reference)
    return record


def cache_dependencies(cache: Path, bundle: str, product: dict) -> list[str]:
    direct = sorted({"/nix/store/" + name for name in product.get("References", "").split()}
                    - {bundle})
    pending = list(direct)
    visited = set()
    while pending:
        dependency = pending.pop()
        if dependency in visited:
            continue
        visited.add(dependency)
        record = cache_record(cache, dependency)
        references = {"/nix/store/" + name for name in record.get("References", "").split()}
        if bundle in references:
            raise ValueError("dependency requires the product store output")
        pending.extend(references - visited)
    return direct


def expected_hash(value: str) -> str:
    if not value.startswith("sha256:"):
        raise ValueError("bundle cache requires SHA-256 hashes")
    return run("nix", "hash", "convert", "--hash-algo", "sha256", "--to", "base16", value)


def verified_archive(cache: Path, product: dict) -> Path:
    url = product.get("URL", "")
    if product.get("Compression") != "zstd" or not re.fullmatch(
            r"nar/[0-9abcdfghijklmnpqrsvwxyz]{52}\.nar\.zst", url):
        raise ValueError("bundle requires a cache-contained zstd NAR")
    archive = (cache / url).resolve(strict=True)
    if not archive.is_relative_to(cache):
        raise ValueError("bundle archive escapes its cache directory")
    if archive.stat().st_size != int(product["FileSize"]):
        raise ValueError("compressed bundle size mismatch")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != expected_hash(product["FileHash"]):
        raise ValueError("compressed bundle hash mismatch")
    return archive


def restore_nar(archive: Path, product: dict, destination: Path) -> dict:
    """Stream an owned NAR without an unpacked temporary archive or store copy."""
    digest = hashlib.sha256()
    size = 0
    started = time.monotonic()
    with subprocess.Popen(["zstd", "--decompress", "--stdout", str(archive)],
                          stdout=subprocess.PIPE) as decompressor:
        with subprocess.Popen(["nix-store", "--restore", str(destination)],
                              stdin=subprocess.PIPE) as restore:
            try:
                while chunk := decompressor.stdout.read(1024 * 1024):
                    digest.update(chunk)
                    size += len(chunk)
                    if size > int(product["NarSize"]):
                        raise ValueError("uncompressed bundle exceeds its recorded size")
                    restore.stdin.write(chunk)
                restore.stdin.close()
                if decompressor.wait() != 0 or restore.wait() != 0:
                    raise ValueError("bundle decompression or NAR restore failed")
            except BaseException:
                decompressor.terminate()
                restore.terminate()
                raise
    if size != int(product["NarSize"]) or digest.hexdigest() != expected_hash(product["NarHash"]):
        raise ValueError("uncompressed bundle hash/size mismatch")
    return {"restored_nar_bytes": size, "restore_seconds": time.monotonic() - started}


def restore_bundle(args: argparse.Namespace) -> None:
    cache = args.cache.resolve(strict=True)
    repo = args.repo.resolve(strict=True)
    bundle = checked_store_path((cache / "bundle-path.txt").read_text().strip())
    match = re.fullmatch(r"[0-9abcdfghijklmnpqrsvwxyz]{32}-openglad-ci-(" +
                         "|".join(LANES) + r")-.+", Path(bundle).name)
    if not match:
        raise ValueError("cache output is not a native lane bundle")
    lane = match.group(1)
    preset = "ci-test" if lane == "canary" else f"ci-{lane}"
    # Reject occupied destinations before allocating the large restored tree.
    expected = {"lane": lane, "preset": preset, "revision": "dirty",
                "source": f"/tmp/openglad-ci-{lane}/source", "build_relative": f"build/{preset}"}
    activation_paths(expected, repo)
    product = cache_record(cache, bundle)
    dependencies = cache_dependencies(cache, bundle, product)
    archive = verified_archive(cache, product)
    (repo / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".native-bundle-", dir=repo / "build") as temporary:
        restored = Path(temporary) / "bundle"
        metrics = restore_nar(archive, product, restored)
        manifest = read_manifest(restored)
        if any(manifest[key] != expected[key] for key in ("lane", "preset", "source", "build_relative")):
            raise ValueError("restored bundle manifest does not match cache identity")
        canonical, destination = activation_paths(manifest, repo)
        if dependencies:
            subprocess.run(["nix", "copy", "--no-check-sigs", "--from", cache.as_uri(),
                            *dependencies], check=True)
            closure = set(run("nix", "path-info", "--recursive", *dependencies).splitlines())
            if bundle in closure:
                raise ValueError("imported dependency closure requires the product output")
        else:
            closure = set()
        # The restored tree is already owned by this consumer. Rename it on
        # the checkout filesystem instead of allocating another full copy.
        tree = restored / "build"
        if not tree.is_dir() or tree.is_symlink():
            raise ValueError("restored bundle has no owned build directory")
        files = [path for path in tree.rglob("*") if path.is_file() and not path.is_symlink()]
        metrics.update({"bundle": bundle, "restored_file_bytes": sum(path.stat().st_size for path in files),
                        "restored_files": len(files), "copied_bytes": 0, "copied_files": 0,
                        "hardlinked_bytes": 0, "hardlinked_files": 0,
                        "dependency_closure_paths": len(closure), "product_imported_by_restore": False})
        for path in [tree, *tree.rglob("*")]:
            if not path.is_symlink():
                path.chmod(path.stat().st_mode | 0o200)
        new_alias = not canonical.is_symlink()
        tree.rename(destination)
        try:
            # Roots must use their final location: indirect roots cannot be moved.
            roots = destination / ".nix-dependencies"
            roots.mkdir()
            for dependency in dependencies:
                subprocess.run(["nix-store", "--add-root", str(roots / Path(dependency).name),
                                "--indirect", "--realise", dependency], check=True,
                               stdout=subprocess.DEVNULL)
            source_alias(canonical, repo)
            finish_activation(manifest, repo, canonical, destination)
            write_json(destination / "bundle-activation-metrics.json", metrics)
            if os.environ.get("GITHUB_STEP_SUMMARY"):
                with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
                    summary.write(
                        f"Native bundle restore: {metrics['restore_seconds']:.2f}s; "
                        f"{metrics['restored_file_bytes']:,} owned build bytes; "
                        f"{metrics['dependency_closure_paths']} dependency paths; "
                        "zero product store imports and zero build-tree copy bytes.\n")
            print(f"Restored {lane} directly into {destination}; no product store import or build-tree copy")
        except BaseException:
            if new_alias and canonical.is_symlink() and canonical.resolve() == repo:
                canonical.unlink()
            shutil.rmtree(destination)
            raise



def plan_tree(args: argparse.Namespace) -> None:
    """Expose a realized bundle for metadata queries only, without copying it."""
    bundle = args.bundle.resolve(strict=True)
    checked_store_path(str(bundle))
    manifest = read_manifest(bundle)
    repo = args.repo.resolve(strict=True)
    canonical, destination = activation_paths(manifest, repo)
    destination.parent.mkdir(exist_ok=True)
    new_alias = not canonical.is_symlink()
    destination.symlink_to(bundle / "build", target_is_directory=True)
    try:
        source_alias(canonical, repo)
        subprocess.run(["nix-store", "--add-root", str(destination.parent / (".plan-" + manifest["preset"])),
                        "--indirect", "--realise", str(bundle)], check=True)
    except BaseException:
        destination.unlink()
        if new_alias and canonical.is_symlink() and canonical.resolve() == repo:
            canonical.unlink()
        raise
    print(f"Read-only metadata tree: {destination}; run fresh tests only in a restored consumer tree")


def deactivate(args: argparse.Namespace) -> None:
    canonical = Path(f"/tmp/openglad-ci-{args.lane}/source")
    if not canonical.is_symlink() or canonical.resolve() != args.repo.resolve():
        raise ValueError(f"{canonical} is not this checkout's alias")
    canonical.unlink()
    print(f"Released {canonical}")


COMPILER_ENVIRONMENT_KEYS = ("NIX_CFLAGS_COMPILE", "NIX_LDFLAGS", "NIX_HARDENING_ENABLE")


def compiler_launcher(env_program: Path, cache_program: Path | None = None,
                      cache_dir: Path | None = None) -> str:
    """Preserve producer flags and optionally chain a pinned compiler cache."""
    executable = str(env_program)
    if not executable.startswith("/nix/store/") or env_program.name != "env":
        raise ValueError("compiler launcher requires the pinned store env executable")
    arguments = [executable]
    for key in COMPILER_ENVIRONMENT_KEYS:
        value = os.environ.get(key, "")
        if any(character in value for character in (";", "\n", "\r")):
            raise ValueError(f"compiler environment cannot be represented as a CMake list: {key}")
        arguments.append(f"{key}={value}")
    if (cache_program is None) != (cache_dir is None):
        raise ValueError("compiler cache program and directory must be supplied together")
    if cache_program is not None:
        cache_executable = str(cache_program)
        if not cache_executable.startswith("/nix/store/") or cache_program.name != "sccache":
            raise ValueError("compiler cache requires the pinned store sccache executable")
        cache_directory = str(cache_dir)
        if not cache_dir.is_absolute() or any(character in cache_directory for character in (";", "\n", "\r")):
            raise ValueError("compiler cache directory must be an absolute CMake-list-safe path")
        arguments.extend((f"SCCACHE_DIR={cache_directory}", cache_executable))
    return ";".join(arguments)


def print_compiler_launcher(args: argparse.Namespace) -> None:
    print(compiler_launcher(args.env, args.cache, args.cache_dir))


def local_build_expression(repo: Path, lane: str, *, sccache_seed: Path | None = None,
                           compile_shards: int = 0, compile_shard: int = -1) -> str:
    """Attach actual Git history to a dirty flake without changing its source."""
    if lane not in LANES:
        raise ValueError("unknown native CI lane")
    if run("git", "rev-parse", "--is-shallow-repository", cwd=repo) != "false":
        raise ValueError("bundle version metadata requires complete Git history")
    count = run("git", "rev-list", "--count", "HEAD", cwd=repo)
    revision = run("git", "rev-parse", "HEAD", cwd=repo)
    if not re.fullmatch(r"[1-9][0-9]*", count) or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Git returned invalid bundle history metadata")
    # URI quoting also removes Nix string interpolation characters from paths.
    uri = "git+" + repo.resolve().as_uri()
    if compile_shards < 0 or (compile_shards == 0 and compile_shard != -1) or (
            compile_shards > 0 and not 0 <= compile_shard < compile_shards):
        raise ValueError("compile shard index/count are inconsistent")
    seed_binding = ""
    seed_attribute = ""
    if sccache_seed is not None:
        seed = sccache_seed.resolve(strict=True)
        if not (seed / "cache").is_dir():
            raise ValueError("sccache seed has no cache directory")
        seed_binding = (
            f'seed = builtins.path {{ path = builtins.toPath {json.dumps(str(seed))}; '
            'name = "openglad-sccache-seed"; }; '
        )
        seed_attribute = "sccacheSeed = seed; "
    return (
        f"let flake = builtins.getFlake {json.dumps(uri)}; {seed_binding}"
        f"in flake.packages.${{builtins.currentSystem}}.ci-{lane}.overrideAttrs (_: {{ "
        f'ciCommitCount = "{count}"; ciGitHash = "{revision[:8]}"; version = "2.{count}"; '
        f'{seed_attribute}ciCompileShards = "{compile_shards}"; ciCompileShard = "{compile_shard}"; '
        '__intentionallyOverridingVersion = true; })'
    )


def normalize_output_link(out_link: Path, cache_only: bool) -> None:
    """Give a selected secondary Nix output the caller's requested link name."""
    if not cache_only:
        return
    requested = out_link.absolute()
    secondary = Path(str(requested) + "-sccache")
    if requested.exists() or requested.is_symlink():
        raise ValueError(f"refusing to replace output link: {requested}")
    if not secondary.is_symlink():
        raise ValueError(f"Nix did not create the selected sccache output link: {secondary}")
    secondary.rename(requested)


def build_bundle(args: argparse.Namespace) -> None:
    repo = args.repo.resolve()
    expression = local_build_expression(
        repo, args.lane,
        sccache_seed=getattr(args, "sccache_seed", None),
        compile_shards=getattr(args, "compile_shards", 0),
        compile_shard=getattr(args, "compile_shard", -1),
    )
    if getattr(args, "cache_only", False):
        if args.lane not in ("asan", "coverage"):
            raise ValueError("only ASan and coverage producers expose an sccache output")
        expression = f"({expression}).sccache"
    subprocess.run(["nix", "build", "--impure", "--expr", expression,
                    "--cores", str(getattr(args, "cores", 3)), "--max-jobs", "1", "-L",
                    "--out-link", os.path.abspath(args.out_link)], cwd=repo, check=True)
    normalize_output_link(args.out_link, getattr(args, "cache_only", False))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    launcher = commands.add_parser("compiler-launcher", help="internal producer compiler environment capture")
    launcher.add_argument("--env", type=Path, required=True)
    launcher.add_argument("--cache", type=Path)
    launcher.add_argument("--cache-dir", type=Path)
    launcher.set_defaults(function=print_compiler_launcher)
    building = commands.add_parser("build", help="build with the checkout's real Git history metadata")
    building.add_argument("--lane", choices=LANES, required=True)
    building.add_argument("--repo", type=Path, default=Path.cwd())
    building.add_argument("--out-link", type=Path, required=True)
    building.add_argument("--sccache-seed", type=Path)
    building.add_argument("--compile-shards", type=int, default=0)
    building.add_argument("--compile-shard", type=int, default=-1)
    building.add_argument("--cache-only", action="store_true")
    building.add_argument("--cores", type=int, default=3)
    building.set_defaults(function=build_bundle)
    packing = commands.add_parser("pack", help="internal derivation install step")
    packing.add_argument("--source", type=Path, required=True)
    packing.add_argument("--build", type=Path, required=True)
    packing.add_argument("--output", type=Path, required=True)
    packing.add_argument("--lane", choices=LANES, required=True)
    packing.add_argument("--revision", required=True)
    packing.set_defaults(function=pack)
    exporting = commands.add_parser("export", help="export a complete closure to a local binary cache")
    exporting.add_argument("--bundle", type=Path, required=True)
    exporting.add_argument("--cache", type=Path, required=True)
    exporting.set_defaults(function=export_bundle)
    importing = commands.add_parser("import", help="import a downloaded closure")
    importing.add_argument("--cache", type=Path, required=True)
    importing.add_argument("--out-link", type=Path)
    importing.set_defaults(function=import_bundle)
    restoring = commands.add_parser("restore", help="verify and restore an owned consumer tree without importing the product")
    restoring.add_argument("--cache", type=Path, required=True)
    restoring.add_argument("--repo", type=Path, default=Path.cwd())
    restoring.set_defaults(function=restore_bundle)
    planning = commands.add_parser("plan-tree", help="expose a read-only producer tree for metadata queries only")
    planning.add_argument("--bundle", type=Path, required=True)
    planning.add_argument("--repo", type=Path, default=Path.cwd())
    planning.set_defaults(function=plan_tree)
    activation = commands.add_parser("activate", help="materialize a writable tree without rebuilding")
    activation.add_argument("--bundle", type=Path, required=True)
    activation.add_argument("--repo", type=Path, default=Path.cwd())
    activation.set_defaults(function=activate)
    deactivation = commands.add_parser("deactivate", help="release the canonical checkout alias")
    deactivation.add_argument("--lane", choices=LANES, required=True)
    deactivation.add_argument("--repo", type=Path, default=Path.cwd())
    deactivation.set_defaults(function=deactivate)
    args = parser.parse_args()
    try:
        args.function(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"nix_bundle: {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
