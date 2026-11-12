#!/usr/bin/env python3
"""Partition the complete CTest inventory, run once, and require every result.

Timings affect placement only. New entries always join the plan. Fixture
components stay together so CTest never silently repeats setup on another shard.
Only emscripten_build_test is delegated to the separately required WASM graph.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

DELEGATED = {"emscripten_build_test": "Release / WASM build and browser matrix"}
DEFAULT_PARALLEL = 4

# Existing opt-in tools and external/display capabilities are reported by name.
# Semantic, sanitizer-budget, unarmed-version and missing-golden skips fail.
SCHEDULED_GTEST_SKIPS = {
    "PausePlayerDraw.menushots_player_screen_keyboard_and_joystick": "opt-in MENUSHOTS_DIR proof capture",
    "PausePlayerDraw.menushots_remap_prompt_spec2": "opt-in MENUSHOTS_DIR proof capture",
    "PausePlayerDraw.menushots_key_cell_and_wrapped_prompts": "opt-in MENUSHOTS_DIR proof capture",
    "RenderEffects.zz_capture_effect_scenes": "opt-in OG_FX_CAPTURE_DIR recording",
    "RenderEffects.zz_bench_large_canvas_redraw": "opt-in OG_BENCH performance benchmark",
    "StairOverlay.zz_capture_stair_and_lava_scene": "opt-in OG_FX_CAPTURE_DIR recording",
    "DecorRender.zz_capture_decor_sampler": "opt-in OG_FX_CAPTURE_DIR recording",
    "KeymapScreen.zz_capture_four_seats_one_on_a_pad": "opt-in OG_KEYMAP_SHOT_DIR proof capture",
    "NetTransportRelayWs.live_cloudflare_relay_end_to_end": "opt-in deployed production relay; local transports remain required",
    "VideoModesMore.display_selector_reports_sdls_modes_the_desktop_and_the_usable_bounds": "dummy driver has no comparable fullscreen modes; real X11 lane covers display behavior",
}


def read(path):
    return json.loads(Path(path).read_text())


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def scheduling_slots(test, parallel):
    """Return how many of one runner's CTest slots this entry reserves."""
    processors = 1
    run_serial = False
    for prop in test.get("properties", []):
        if prop["name"] == "PROCESSORS":
            try:
                processors = int(prop["value"])
            except (TypeError, ValueError) as error:
                raise ValueError(f'invalid PROCESSORS for {test["name"]}') from error
        elif prop["name"] == "RUN_SERIAL":
            value = prop["value"]
            run_serial = value is True or (isinstance(value, str)
                                            and value.upper() in ("1", "ON", "TRUE", "YES"))
    if processors < 1:
        raise ValueError(f'invalid PROCESSORS for {test["name"]}')
    return parallel if run_serial else min(processors, parallel)


def partition(inventory, timings, count, lane, parallel=DEFAULT_PARALLEL):
    tests = inventory["tests"]
    names = [t["name"] for t in tests]
    if len(names) != len(set(names)) or not names or count < 1 or parallel < 1:
        raise ValueError("invalid or empty CTest inventory")
    by_name = {test["name"]: test for test in tests}
    selected = set(names) - DELEGATED.keys()
    if lane == "tsan":
        selected = {"og_unit_network"}
        if not selected <= set(names):
            raise ValueError("focused TSan network test is absent")
    parents = {name: name for name in selected}

    def root(name):
        while parents[name] != name:
            name = parents[name]
        return name

    fixtures = {}
    for test in tests:
        if test["name"] not in selected:
            continue
        for prop in test.get("properties", []):
            if prop["name"] == "DISABLED" and prop["value"]:
                raise ValueError(f'disabled test: {test["name"]}')
            if prop["name"] in ("FIXTURES_SETUP", "FIXTURES_REQUIRED", "FIXTURES_CLEANUP"):
                values = prop["value"] if isinstance(prop["value"], list) else [prop["value"]]
                for fixture in values:
                    previous = fixtures.setdefault(fixture, test["name"])
                    parents[root(test["name"])] = root(previous)
    components = {}
    for name in sorted(selected):
        components.setdefault(root(name), []).append(name)
    weights = {**timings.get("seconds", {}),
               **timings.get("lane_seconds", {}).get(lane, {})}
    shards = [[] for _ in range(count)]
    elapsed_loads = [0.0] * count
    runner_loads = [0.0] * count

    def elapsed(group):
        return sum(weights.get(name, 30) for name in group)

    def runner_cost(group):
        return sum(weights.get(name, 30) * scheduling_slots(by_name[name], parallel) / parallel
                   for name in group)

    groups = sorted(components.values(), key=lambda group: (-runner_cost(group), group))
    for group in groups:
        shard = min(range(count), key=lambda i: (runner_loads[i], i))
        shards[shard].extend(group)
        elapsed_loads[shard] += elapsed(group)
        runner_loads[shard] += runner_cost(group)
    if any(not shard for shard in shards):
        raise ValueError("shard count exceeds independent test components")
    delegated = {name: DELEGATED[name] for name in names if name in DELEGATED}
    focused = sorted(set(names) - selected - delegated.keys()) if lane == "tsan" else []
    return {"format": 1, "lane": lane, "parallel": parallel,
            "inventory": sorted(names), "delegated": delegated,
            "focused_lane_omissions": focused,
            "shards": [{"id": i + 1, "tests": sorted(shard),
                        "estimated_seconds": elapsed_loads[i],
                        "estimated_runner_seconds": runner_loads[i]}
                       for i, shard in enumerate(shards)]}


def compiler_notes(build):
    notes = {str(path.relative_to(build)): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in build.rglob("*.gcno")}
    if not notes:
        raise ValueError("coverage build has no compiler notes")
    return notes


def trace_grid(trace):
    """Counts may differ between shards; the emitted grid must be identical."""
    grid = {}
    for file in trace["files"]:
        name = file["file"]
        if name in grid:
            raise ValueError(f"duplicate source in C++ trace: {name}")
        lines = sorted({line["line_number"] for line in file["lines"]})
        functions = sorted((function.get("name", function.get("demangled_name", "")),
                            function["lineno"])
                           for function in file["functions"])
        grid[name] = {"lines": lines, "functions": functions}
    if not grid:
        raise ValueError("empty C++ trace inventory")
    return grid


def validate_product_inventory(inventory, lane):
    names = {test["name"] for test in inventory["tests"]}
    required = {"og_unit_network"}
    if lane != "tsan":
        required.update({"og_unit_core", "og_test_curses", "openglad_curses_link_no_sdl",
                         "openglad_server_link_no_sdl", "semantics_check",
                         "og_test_parity", "og_test_parity_a_m", "og_test_parity_gates",
                         "scenario99_setup", "scenario99_cleanup",
                         "parity_census_setup", "parity_census_cleanup"})
        required.update(f"og_test_parity_census_{part}" for part in range(4))
    missing = required - names
    if missing:
        raise ValueError("required product test groups absent: " + ", ".join(sorted(missing)))


def gtest_partitions(inventory, lane):
    """Validate the upstream shard protocol before any case runs."""
    groups = {}
    for test in inventory["tests"]:
        if lane == "tsan" and test["name"] != "og_unit_network":
            continue
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        settings = properties.get("ENVIRONMENT", [])
        if isinstance(settings, str):
            settings = settings.split(";")
        environment = dict(item.split("=", 1) for item in settings)
        if environment.get("GTEST_REPEAT", "1") != "1":
            raise ValueError(f'repeated or disabled GoogleTest execution: {test["name"]}')
        total = environment.get("GTEST_TOTAL_SHARDS")
        index = environment.get("GTEST_SHARD_INDEX")
        if total is None and index is None:
            continue
        if total is None or index is None or not total.isdecimal() or not index.isdecimal():
            raise ValueError(f'incomplete GoogleTest shard protocol: {test["name"]}')
        total, index = int(total), int(index)
        if total < 2 or not 0 <= index < total:
            raise ValueError(f'invalid GoogleTest shard index: {test["name"]}')
        command = test.get("command", [])
        if len(command) < 2 or not command[-1].startswith("--gtest_filter="):
            raise ValueError(f'missing explicit GoogleTest filter: {test["name"]}')
        key = (command[-2], command[-1], total)
        groups.setdefault(key, []).append((index, test["name"]))
    result = []
    for (binary, selection, total), entries in sorted(groups.items()):
        if Counter(index for index, name in entries) != Counter(range(total)):
            raise ValueError(f'missing/duplicate GoogleTest partitions: {binary} {selection}')
        result.append({"binary": Path(binary).name, "filter": selection.split("=", 1)[1],
                       "partitions": total, "entries": [name for index, name in sorted(entries)]})
    return result


def plan(args):
    inventory = json.loads(subprocess.check_output(
        ["ctest", "--test-dir", str(args.build), "--show-only=json-v1"], text=True))
    validate_product_inventory(inventory, args.lane)
    result = partition(inventory, read(args.timings), args.shards, args.lane, args.parallel)
    result["gtest_partitions"] = gtest_partitions(inventory, args.lane)
    if args.lane == "coverage":
        result["compiler_notes"] = compiler_notes(args.build)
    result["revision"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    write(args.out / "inventory.json", inventory)
    write(args.out / "plan.json", result)
    for shard in result["shards"]:
        (args.out / f'tests-{shard["id"]}.txt').write_text("\n".join(shard["tests"]) + "\n")
    print(json.dumps(result, indent=2))


def junit_results(path):
    result = {}
    for case in ET.parse(path).iter("testcase"):
        name = case.attrib["name"]
        if name in result:
            raise ValueError(f"duplicate result: {name}")
        failure = case.find("failure")
        error = case.find("error")
        skipped = case.find("skipped")
        result[name] = {"passed": failure is None and error is None and skipped is None
                        and case.get("status") in (None, "run"),
                        "seconds": float(case.get("time", "0"))}
        # CTest can pass a binary containing skipped GoogleTest cases. Its
        # JUnit system-out retains successful process output too, unlike the
        # console's --output-on-failure stream.
        output = case.findtext("system-out", "")
        if "[This part of the test output was removed since it exceeds the threshold" in output:
            raise ValueError(f"truncated native test evidence: {name}")
        internal_skips = sorted(set(re.findall(
            r"^\[\s+SKIPPED\s+\]\s+(\S+\.\S+)", output, re.MULTILINE)))
        if internal_skips:
            scheduled = [{"test": test, "reason": SCHEDULED_GTEST_SKIPS[test]}
                         for test in internal_skips if test in SCHEDULED_GTEST_SKIPS]
            unapproved = [test for test in internal_skips if test not in SCHEDULED_GTEST_SKIPS]
            result[name]["scheduled_skips"] = scheduled
            result[name]["unapproved_skips"] = unapproved
            if unapproved:
                result[name]["passed"] = False
    return result


def run(args):
    manifest = read(args.plan)
    if manifest.get("parallel", DEFAULT_PARALLEL) != args.parallel:
        raise ValueError("run parallelism does not match plan")
    if args.shard < 1 or args.shard > len(manifest["shards"]):
        raise ValueError("shard id outside manifest")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    if manifest["revision"] != revision:
        raise ValueError("plan revision does not match consumer checkout")
    shard = manifest["shards"][args.shard - 1]
    if shard["id"] != args.shard:
        raise ValueError("bad shard id")
    args.out.mkdir(parents=True, exist_ok=True)
    selection = args.out / "tests.txt"
    selection.write_text("\n".join(shard["tests"]) + "\n")
    env = dict(os.environ)
    # CTest owns case selection and bounded one-pass execution. Its per-entry
    # shard properties still reach the child; developer shell filters/repeats do not.
    for flag in tuple(env):
        if flag.startswith("GTEST_"):
            env.pop(flag)
    env["GTEST_COLOR"] = "no"
    env["GTEST_FAIL_IF_NO_TEST_SELECTED"] = "1"
    env["OG_EXPECT_VERSION_MINOR"] = subprocess.check_output(["git", "rev-list", "--count", "HEAD"], text=True).strip()
    env["OG_EXPECT_GIT_HASH"] = subprocess.check_output(["git", "rev-parse", "--short=8", "HEAD"], text=True).strip()
    if manifest["lane"] == "coverage":
        raw = args.out.resolve() / "lua-raw"
        raw.mkdir()
        env["OPENGLAD_LUA_COVERAGE"] = str(raw)
    junit = args.out.resolve() / "ctest.xml"
    command = ["ctest", "--test-dir", str(args.build), "--tests-from-file", str(selection.resolve()),
               "--output-junit", str(junit), "--parallel", str(args.parallel),
               "--test-output-size-passed", "0", "--test-output-size-failed", "0",
               "--output-on-failure", "--timeout", "420", "--no-tests=error"]
    with (args.out / "ctest.log").open("w") as log:
        process = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
        status = process.wait()
    results = junit_results(junit) if junit.exists() else {}
    write(args.out / "result.json", {"format": 1, "shard": args.shard,
          "plan_sha256": fingerprint(manifest), "exit_code": status, "results": results})
    mandatory_skips = [f"{name}: {test}" for name, result in results.items()
                       for test in result.get("unapproved_skips", [])]
    for result in results.values():
        for skip in result.get("scheduled_skips", []):
            print(f'Scheduled native skip: {skip["test"]} — {skip["reason"]}', flush=True)
    if mandatory_skips:
        raise ValueError(f"shard {args.shard} skipped mandatory GoogleTest cases: "
                         + ", ".join(mandatory_skips) + f"; see {args.out}")
    if status or set(results) != set(shard["tests"]) or not all(r["passed"] for r in results.values()):
        raise ValueError(f"shard {args.shard} failed or omitted results; see {args.out}")


def aggregate(plan_data, reports):
    expected = {s["id"]: s for s in plan_data["shards"]}
    counts = Counter(report["shard"] for report in reports)
    if counts != Counter({i: 1 for i in expected}):
        raise ValueError(f"missing/duplicate/unexpected shards: expected {sorted(expected)}, got {dict(counts)}")
    union = []
    failures = []
    scheduled_skips = []
    for report in reports:
        shard = expected[report["shard"]]
        if report["plan_sha256"] != fingerprint(plan_data):
            raise ValueError("shard used a different plan")
        if set(report["results"]) != set(shard["tests"]):
            raise ValueError(f'incomplete results for shard {report["shard"]}')
        union.extend(report["results"])
        if report["exit_code"]:
            failures.append(f'shard {report["shard"]} exit {report["exit_code"]}')
        failures.extend(name for name, result in report["results"].items() if not result["passed"])
        for name, result in report["results"].items():
            failures.extend(f'{name}: skipped mandatory GoogleTest {test}'
                            for test in result.get("unapproved_skips", []))
            scheduled_skips.extend(dict(ctest=name, **skip)
                                   for skip in result.get("scheduled_skips", []))
    if len(union) != len(set(union)):
        raise ValueError("test executed by multiple shards")
    omitted = set(plan_data["delegated"]) | set(plan_data["focused_lane_omissions"])
    if set(union) | omitted != set(plan_data["inventory"]) or set(union) & omitted:
        raise ValueError("shard union does not equal inventory")
    if failures:
        raise ValueError("failed tests: " + ", ".join(failures))
    return {"tests": len(union), "shards": len(reports), "delegated": plan_data["delegated"],
            "scheduled_skips": scheduled_skips}


def verify(args):
    reports = [read(p) for p in args.results.glob("*/result.json")]
    summary = aggregate(read(args.plan), reports)
    print(json.dumps(summary, indent=2))
    if os.getenv("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as out:
            out.write(f'Fresh CTest execution: **{summary["tests"]} entries across {summary["shards"]} shards passed**.\n')
            for skip in summary["scheduled_skips"]:
                out.write(f'Named GoogleTest skip: `{skip["test"]}` — {skip["reason"]}.\n')


def collect(args):
    # Every shard scans the complete .gcno inventory: uncovered translation
    # units remain zero-hit data. Profiles are never copied between workers.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "coverage"))
    import coverage_report

    exclusions = coverage_report.production_variant_exclusions(args.build)
    write(args.out / "compiler-notes.json", compiler_notes(args.build))
    base = ["gcovr", "--root", ".", "--filter", "src/", "--filter", "include/openglad/",
            "-j", "3",
            "--gcov-suspicious-hits-threshold", str(coverage_report.GCOVR_SUSPICIOUS_HITS_THRESHOLD)]
    subprocess.run([*base, "--json", str(args.out / "cpp.json"), str(args.build)], check=True)
    coverage_report.canonicalize_cpp_json(args.out / "cpp.json", Path.cwd())
    # This diagnostic separates compiler object variants, without narrowing
    # source roots or applying another percentage floor to production alone.
    production = [flag for pattern in exclusions
                  for flag in ("--gcov-exclude-directory", pattern)]
    subprocess.run([*base, *production, "--json", str(args.out / "production.json"),
                    str(args.build)], check=True)
    coverage_report.canonicalize_cpp_json(args.out / "production.json", Path.cwd())


def merge_lua_evidence(dumps, raw, shard):
    # The reader is deliberately nonrecursive; flatten process dumps while
    # preserving its sources/ sidecar lookup. Digest-addressed shared sources
    # may coincide only when their bytes agree. Dump names are worker-private.
    for source in dumps.iterdir():
        if source.name == "sources" and source.is_dir() and not source.is_symlink():
            (raw / "sources").mkdir(exist_ok=True)
            for sidecar in source.iterdir():
                destination = raw / "sources" / sidecar.name
                if not sidecar.is_file() or sidecar.is_symlink():
                    raise ValueError(f"invalid source sidecar: {sidecar}")
                if destination.exists():
                    if destination.read_bytes() != sidecar.read_bytes():
                        raise ValueError(f"conflicting source sidecar: {sidecar}")
                else:
                    shutil.copy2(sidecar, destination)
            continue
        destination = raw / f"shard-{shard}-{source.name}"
        if not source.is_file() or source.is_symlink() or destination.exists():
            raise ValueError(f"invalid/duplicate Lua evidence: {source}")
        shutil.copy2(source, destination)


def merge(args):
    verify(args)  # Completeness is independent of the percentage gate.
    manifest = read(args.plan)
    traces = []
    production_traces = []
    grid = None
    production_grid = None
    raw = args.out / "lua-raw"
    raw.mkdir(parents=True, exist_ok=True)
    for shard in manifest["shards"]:
        directory = args.results / f'shard-{shard["id"]}'
        trace = directory / "cpp.json"
        if not trace.is_file():
            raise ValueError(f"missing C++ trace: {trace}")
        if read(directory / "compiler-notes.json") != manifest["compiler_notes"]:
            raise ValueError(f"compiler notes differ from built inventory: {directory}")
        shard_grid = trace_grid(read(trace))
        if grid is None:
            grid = shard_grid
        elif grid != shard_grid:
            raise ValueError(f"C++ zero-hit inventory differs between shards: {directory}")
        traces.extend(["--add-tracefile", str(trace)])
        production_trace = directory / "production.json"
        if not production_trace.is_file():
            raise ValueError(f"missing production variant trace: {production_trace}")
        shard_production_grid = trace_grid(read(production_trace))
        if production_grid is None:
            production_grid = shard_production_grid
        elif production_grid != shard_production_grid:
            raise ValueError(f"production zero-hit inventory differs between shards: {directory}")
        production_traces.extend(["--add-tracefile", str(production_trace)])
        dumps = directory / "lua-raw"
        if not dumps.is_dir() or not list(dumps.glob("*.luacov")):
            raise ValueError(f"missing Lua dumps: {dumps}")
        merge_lua_evidence(dumps, raw, shard["id"])
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "coverage"))
    import coverage_report

    lcov_format = coverage_report.gcovr_lcov_1x_flag(["gcovr"], Path.cwd())
    subprocess.run(["gcovr", "--root", ".", *traces, lcov_format,
                    "--lcov", str(args.out / "cpp.info"),
                    "--json", str(args.out / "cpp.json"),
                    "--html-details", str(args.out / "coverage.html"),
                    "--txt", str(args.out / "coverage-summary.txt")], check=True)
    subprocess.run(["gcovr", "--root", ".", *production_traces, lcov_format,
                    "--lcov", str(args.out / "production.info"),
                    "--json", str(args.out / "production.json"),
                    "--txt", str(args.out / "production-summary.txt")], check=True)
    subprocess.run([sys.executable, "scripts/coverage/coverage_report.py", "--repo-root", ".",
                    "--lua-raw-dir", str(raw), "--lines-tool", str(args.build / "og_lua_lines"),
                    "--cpp-tracefile", str(args.out / "cpp.info"),
                    "--cpp-production-tracefile", str(args.out / "production.info"),
                    "--output-dir", str(args.out),
                    "--line-threshold", "98", "--function-threshold", "100", "--strict-cpp"], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--lane", choices=["coverage", "asan", "tsan"], required=True)
    p.add_argument("--shards", type=int, required=True)
    p.add_argument("--parallel", type=int, default=DEFAULT_PARALLEL)
    p.add_argument("--timings", type=Path, default=Path("scripts/ci/test_timings.json"))
    p.add_argument("--out", type=Path, required=True)
    p.set_defaults(func=plan)
    p = sub.add_parser("run")
    p.add_argument("--shard", type=int, required=True)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--plan", type=Path, required=True)
    p.add_argument("--parallel", type=int, default=DEFAULT_PARALLEL)
    p.set_defaults(func=run)
    for name, function in (("verify", verify), ("merge-coverage", merge)):
        p = sub.add_parser(name)
        p.add_argument("--plan", type=Path, required=True)
        p.add_argument("--results", type=Path, required=True)
        if function is merge:
            p.add_argument("--out", type=Path, required=True)
            p.add_argument("--build", type=Path, required=True)
        p.set_defaults(func=function)
    p = sub.add_parser("collect-coverage")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--build", type=Path, required=True)
    p.set_defaults(func=collect)
    args = parser.parse_args()
    try:
        args.func(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"test_matrix: {error}\n")


if __name__ == "__main__":
    main()
