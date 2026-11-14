#!/usr/bin/env python3
"""Partition the complete CTest inventory, run once, and require every result.

Timings affect placement only. New entries always join the plan. Fixture
components stay together so CTest never silently repeats setup on another shard.
Only emscripten_build_test is delegated to the separately required WASM graph.
"""
from __future__ import annotations

import argparse
from array import array
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import xml.etree.ElementTree as ET

DELEGATED = {"emscripten_build_test": "Release / WASM build and browser matrix"}
DEFAULT_PARALLEL = 4
LINES_TOOL = "og_lua_lines"
NOTES_ARCHIVE = "compiler-notes.tar.zst"

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


# The live-transport unit binary is cut into several CTest entries
# (cmake/OpenGladTests.cmake, OG_NETWORK_TESTS). Every lane must carry every
# one of them, and the focused TSan lane is exactly this set: an entry that
# is missing is transport code no lane ran.
NETWORK_ENTRIES = frozenset({"og_unit_network", "og_unit_network_ws_server",
                             "og_unit_network_ws_client"})


def is_network_entry(name):
    return name == "og_unit_network" or name.startswith("og_unit_network_")


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
        selected = {name for name in names if is_network_entry(name)}
        if not NETWORK_ENTRIES <= selected:
            raise ValueError("focused TSan network test is absent: "
                             + ", ".join(sorted(NETWORK_ENTRIES - selected)))
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


def cmake_home_directory(build):
    """The source directory the build was configured from, as the notes name it."""
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
            return line.split("=", 1)[1]
    raise ValueError(f"build tree does not name its source directory: {build}")


def source_links(build, source_root):
    """Symlinks inside the build tree that lead back into the source tree.

    The game's components compile against per-component include roots that
    CMake lays out as links (component_includes/<component>/openglad/<name>
    -> include/openglad/<name>), and a note names a header by the path it was
    included through. gcov can only report such a header under its real name
    where the link exists, so the gate recreates every one of them: without
    them the hits of 62 header functions were filed under a build-tree path
    no report counts (GitHub run 43544998297, 2026-11-14).
    """
    root = Path(source_root)
    build = Path(os.path.abspath(build))
    links = {}
    for directory, directories, files in os.walk(build):
        for name in directories + files:
            path = Path(directory) / name
            if not path.is_symlink():
                continue
            target = Path(os.path.normpath(path.parent / os.readlink(path)))
            if target.is_relative_to(build):
                continue  # a link within the build tree names no source
            try:
                relative = target.relative_to(root)
            except ValueError:
                continue  # neither does a link out of the source tree
            links[str(path.relative_to(build))] = str(relative)
    return dict(sorted(links.items()))


def restore_source_links(tree, links, repo):
    """Recreate the planned links in a tree that holds only notes."""
    for link, target in links.items():
        for name in (link, target):
            if Path(name).is_absolute() or ".." in Path(name).parts or not Path(name).parts:
                raise ValueError(f"planned source link leaves its tree: {link} -> {target}")
        destination = repo / target
        if not destination.exists():
            raise ValueError(f"planned source link has no target in this checkout: {target}")
        location = tree / link
        if location.exists() or location.is_symlink():
            raise ValueError(f"planned source link is already occupied: {location}")
        location.parent.mkdir(parents=True, exist_ok=True)
        location.symlink_to(destination, target_is_directory=destination.is_dir())


def compiler_notes(build):
    notes = {str(path.relative_to(build)): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in build.rglob("*.gcno")}
    if not notes:
        raise ValueError("coverage build has no compiler notes")
    return notes


def validate_product_inventory(inventory, lane):
    names = {test["name"] for test in inventory["tests"]}
    required = set(NETWORK_ENTRIES)
    unlisted = sorted(name for name in names
                      if is_network_entry(name) and name not in NETWORK_ENTRIES)
    if unlisted:
        raise ValueError("network test entries missing from NETWORK_ENTRIES: "
                         + ", ".join(unlisted))
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
        if lane == "tsan" and not is_network_entry(test["name"]):
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
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "coverage"))
        import coverage_report

        result["compiler_notes"] = compiler_notes(args.build)
        # What the gate needs to scan those notes without the compiled tree:
        # where the compiler stood (the notes name their sources from there),
        # and which object directories are the TESTING variant.
        result["source_root"] = cmake_home_directory(args.build)
        result["build_relative"] = str(args.build)
        result["source_links"] = source_links(args.build, result["source_root"])
        result["production_exclusions"] = coverage_report.production_variant_exclusions(args.build)
        # The gate needs this one built tool and nothing else from the
        # compiled tree. It travels with the plan, pinned by digest, so the
        # gate job does not download the whole compiled bundle to reach it.
        tool = args.build / LINES_TOOL
        result["lines_tool_sha256"] = hashlib.sha256(tool.read_bytes()).hexdigest()
    result["revision"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    write(args.out / "inventory.json", inventory)
    write(args.out / "plan.json", result)
    if args.lane == "coverage":
        shutil.copyfile(args.build / LINES_TOOL, args.out / LINES_TOOL)
        if getattr(args, "notes_out", None) is not None:
            pack_notes(args.build, args.notes_out / NOTES_ARCHIVE)
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


def object_partitions(notes, exclusions, workers):
    """Split the CMake target directories into directory-disjoint gcov jobs.

    gcovr runs gcov under a lock on its working directory, and without an
    object directory that directory is --root for every object: -j then runs
    one gcov at a time (136 s of one worker's 219 s collection on
    2026-11-12). Separate gcovr processes over disjoint target directories,
    each running gcov beside its object, are parallel and never share an
    output directory.

    Production and TESTING object variants stay in different jobs, so the
    production trace is the merge of its own jobs instead of a second scan
    of the same objects. Every note belongs to exactly one job.
    """
    if workers < 1:
        raise ValueError("coverage collection needs at least one worker")
    weights = {}
    for note in notes:
        match = re.match(r"((?:.*/)?CMakeFiles/[^/]+\.dir)/", note)
        if match is None:
            raise ValueError(f"coverage note outside a CMake target directory: {note}")
        weights[match.group(1)] = weights.get(match.group(1), 0) + 1
    testing = {directory for directory in weights
               if any(re.search(pattern, directory + "/") for pattern in exclusions)}
    production = set(weights) - testing
    if not testing or not production:
        raise ValueError("coverage build lacks a production or a TESTING object population")

    def pack(directories, count):
        jobs = [[] for _ in range(count)]
        loads = [0] * count
        for directory in sorted(directories, key=lambda name: (-weights[name], name)):
            job = min(range(count), key=lambda index: (loads[index], index))
            jobs[job].append(directory)
            loads[job] += weights[directory]
        return [sorted(job) for job in jobs if job]

    production_weight = sum(weights[directory] for directory in production)
    share = round(workers * production_weight / sum(weights.values()))
    production_jobs = min(max(share, 1), max(workers - 1, 1))
    testing_jobs = max(workers - production_jobs, 1)
    return ([{"variant": "production", "directories": job}
             for job in pack(production, production_jobs)]
            + [{"variant": "testing", "directories": job}
               for job in pack(testing, testing_jobs)])


# A GCC counter file (.gcda), as libgcov writes it and gcov-io.h describes it:
# four header words, then records of (tag, length in bytes, payload). A
# --coverage build writes one summary, and per function an announcement
# followed by its arc counters; a counter record whose length is negative
# stands for that many bytes of zero counters and has no payload.
COUNTER_FILE_MAGIC = 0x67636461  # "gcda"
COUNTER_TAG_FUNCTION = 0x01000000
COUNTER_TAG_ARCS = 0x01A10000
COUNTER_TAG_SUMMARY = 0xA1000000


def counter_file_totals(data, name):
    """Return (counters, their sum) for one counter file, or refuse it.

    Collection rejects a negative counter here, at its source, instead of
    reading it back out of a formatted report: a lost update shows as a
    count below zero in the worker that lost it, and summing workers first
    could hide it. Anything this does not recognise is refused too, so a
    toolchain that changes the format fails loudly instead of being skipped.
    """
    if len(data) < 16 or struct.unpack_from("=I", data, 0)[0] != COUNTER_FILE_MAGIC:
        raise ValueError(f"not a GCC counter file: {name}")
    offset = 16
    counters = 0
    total = 0
    summaries = 0
    in_function = False
    while offset < len(data):
        if len(data) - offset == 4 and data[offset:] == b"\0\0\0\0":
            break  # libgcov ends the file with one zero word
        if len(data) - offset < 8:
            raise ValueError(f"truncated counter file: {name}")
        tag, length = struct.unpack_from("=Ii", data, offset)
        offset += 8
        if tag == COUNTER_TAG_ARCS:
            if not in_function or length % 8:
                raise ValueError(f"misplaced or misaligned arc counters: {name}")
            if length < 0:
                counters += -length // 8
                continue
            if offset + length > len(data):
                raise ValueError(f"truncated counter file: {name}")
            values = array("q")
            values.frombytes(data[offset:offset + length])
            offset += length
            if values and min(values) < 0:
                raise ValueError(
                    f"negative or overflowed coverage counter ({min(values)}): {name}")
            counters += len(values)
            total += sum(values)
            continue
        if length < 0 or offset + length > len(data):
            raise ValueError(f"truncated counter file: {name}")
        if tag == COUNTER_TAG_FUNCTION and length in (0, 12):
            # Length 0 announces a function another object emitted.
            in_function = length == 12
        elif tag == COUNTER_TAG_SUMMARY and length == 8:
            summaries += 1
        else:
            raise ValueError(f"unknown record {tag:#010x} ({length} bytes) in counter file: {name}")
        offset += length
    if summaries != 1:
        raise ValueError(f"counter file without exactly one summary: {name}")
    return counters, total


def counter_tree_totals(root, notes):
    """Totals of every counter file under root, each beside a planned note."""
    totals = {}
    for path in sorted(root.rglob("*.gcda")):
        relative = str(path.relative_to(root))
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"counter file is not a regular file: {path}")
        if str(Path(relative).with_suffix(".gcno")) not in notes:
            raise ValueError(f"counter file without a compiler note: {relative}")
        totals[relative] = counter_file_totals(path.read_bytes(), relative)
    if not totals:
        raise ValueError(f"no coverage counter files under {root}")
    return totals


def collect(args):
    """One worker's evidence: the raw counter files its tests wrote.

    Nothing is scanned or formatted here. Each of eight workers used to run
    gcov over the complete note inventory and write two reports of it (71 to
    108 s of every worker on 2026-11-13), and the gate then spent 82 s
    reading those sixteen reports back in. The gate sums the counter files
    instead and scans the inventory once.
    """
    notes = compiler_notes(args.build)
    write(args.out / "compiler-notes.json", notes)
    totals = counter_tree_totals(args.build, notes)
    destination = args.out / "counters"
    if destination.exists():
        raise ValueError(f"counter output already exists: {destination}")
    for relative in totals:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.build / relative, target)
    write(args.out / "counters.json",
          {relative: {"counters": counters, "total": total}
           for relative, (counters, total) in totals.items()})
    print(f"Collected {len(totals)} counter files, "
          f"{sum(counters for counters, _ in totals.values())} counters")


def pack_notes(build, archive):
    """Ship the complete compiler-note inventory to the gate."""
    names = sorted(str(path.relative_to(build)) for path in build.rglob("*.gcno"))
    if not names:
        raise ValueError("coverage build has no compiler notes")
    archive.parent.mkdir(parents=True, exist_ok=True)
    with archive.open("wb") as output:
        compressor = subprocess.Popen(["zstd", "-3", "-T0", "--quiet", "--stdout"],
                                      stdin=subprocess.PIPE, stdout=output)
        with tarfile.open(fileobj=compressor.stdin, mode="w|") as tar:
            for name in names:
                tar.add(build / name, arcname=name, recursive=False)
        compressor.stdin.close()
        if compressor.wait():
            raise ValueError("compressing the compiler notes failed")


def unpack_notes(archive, tree, expected):
    """Lay the planned note inventory out where the compiler wrote it."""
    if tree.exists():
        raise ValueError(f"coverage tree already exists: {tree}")
    tree.mkdir(parents=True)
    decompressor = subprocess.Popen(["zstd", "--decompress", "--quiet", "--stdout", str(archive)],
                                    stdout=subprocess.PIPE)
    with tarfile.open(fileobj=decompressor.stdout, mode="r|") as tar:
        for member in tar:
            name = Path(member.name)
            if (not member.isfile() or name.is_absolute() or ".." in name.parts
                    or member.name not in expected):
                raise ValueError(f"unplanned entry in the compiler notes: {member.name}")
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
    if decompressor.wait():
        raise ValueError("decompressing the compiler notes failed")
    if compiler_notes(tree) != expected:
        raise ValueError("shipped compiler notes are not the planned build's")


def merge_counters(shards, tree, notes):
    """Sum every worker's counter files into the note tree, and prove the sum.

    gcov-tool is the compiler's own merge, the one libgcov applies when two
    processes write one file. The proof does not take its word: every merged
    file must hold the counters of its inputs, and their total must be the
    inputs' totals added up.
    """
    expected = {}
    for directory in shards:
        for relative, (counters, total) in counter_tree_totals(directory, notes).items():
            previous = expected.get(relative)
            if previous is not None and previous[0] != counters:
                raise ValueError(f"workers disagree about the counters of {relative}")
            expected[relative] = (counters, total + (previous[1] if previous else 0))
    with tempfile.TemporaryDirectory(prefix="counter-merge-", dir=tree.parent) as scratch:
        current = shards[0]
        for index, addition in enumerate(shards[1:]):
            target = Path(scratch) / f"sum-{index}"
            result = subprocess.run(["gcov-tool", "merge", "-o", str(target),
                                     str(current), str(addition)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if result.returncode or result.stdout.strip():
                raise ValueError("gcov-tool could not merge worker counters: "
                                 + result.stdout.strip()[-2000:])
            current = target
        merged = counter_tree_totals(current, notes)
        if merged != expected:
            wrong = sorted(set(merged) ^ set(expected)) or sorted(
                name for name in expected if merged[name] != expected[name])
            raise ValueError("merged counters are not the sum of the workers': "
                             + ", ".join(wrong[:5]))
        for relative in merged:
            if (tree / relative).exists():
                raise ValueError(f"coverage tree already holds counters: {relative}")
            if not (tree / relative).with_suffix(".gcno").is_file():
                raise ValueError(f"coverage tree lacks the note of {relative}")
        for relative in merged:
            shutil.copyfile(current / relative, tree / relative)
    return merged


def scan_counters(tree, exclusions, out, workers=DEFAULT_PARALLEL):
    """Run gcov over the complete note inventory once and write every report.

    Both object populations leave their merged trace, the tracefile the gate
    reads and a text summary; the complete one also leaves the HTML detail.
    Each population is formatted in the pass that merges its partitions.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "coverage"))
    import coverage_report

    # Every note is scanned, with or without a counter file: an uncovered
    # translation unit stays in the report as zero-hit lines and functions.
    notes = compiler_notes(tree)
    jobs = object_partitions(notes, exclusions, workers)
    base = ["gcovr", "--root", ".", "--filter", "src/", "--filter", "include/openglad/",
            "-j", "1", "--gcov-object-directory", ".",
            "--gcov-suspicious-hits-threshold", str(coverage_report.GCOVR_SUSPICIOUS_HITS_THRESHOLD)]
    with tempfile.TemporaryDirectory(prefix="cpp-partitions-", dir=out) as temporary:
        def scan(item):
            index, job = item
            trace = Path(temporary) / f'{job["variant"]}-{index}.json'
            subprocess.run([*base, "--json", str(trace),
                            *[str(tree / directory) for directory in job["directories"]]],
                           check=True)
            return job["variant"], trace

        with ThreadPoolExecutor(max_workers=workers) as pool:
            traces = list(pool.map(scan, enumerate(jobs)))
        populations = [
            (out / "cpp.json", [trace for _, trace in traces],
             {"lcov_output": out / "cpp.info", "html_output": out / "coverage.html",
              "text_output": out / "coverage-summary.txt"}),
            (out / "production.json",
             [trace for variant, trace in traces if variant == "production"],
             {"lcov_output": out / "production.info",
              "text_output": out / "production-summary.txt"}),
        ]
        with ThreadPoolExecutor(max_workers=len(populations)) as pool:
            merging = [pool.submit(coverage_report.canonicalize_cpp_json, target, Path.cwd(),
                                   sources=sources, **reports)
                       for target, sources, reports in populations]
            for job in merging:
                job.result()


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


def source_alias(source_root, repo):
    """Make the producer's source directory resolve to this checkout.

    The notes name every source by the path the compiler saw. A restored
    bundle brings that alias with it; the gate, which restores no bundle,
    makes it here and returns it so the caller can take it away again.
    """
    canonical = Path(source_root)
    if canonical.resolve() == repo:
        return None
    if canonical.exists() or canonical.is_symlink():
        raise ValueError(f"{canonical} is in use by another checkout")
    canonical.parent.mkdir(parents=True, exist_ok=True)
    canonical.symlink_to(repo, target_is_directory=True)
    return canonical


def merge(args):
    verify(args)  # Completeness is independent of the percentage gate.
    manifest = read(args.plan)
    notes = manifest["compiler_notes"]
    raw = args.out / "lua-raw"
    raw.mkdir(parents=True, exist_ok=True)
    counters = []
    for shard in manifest["shards"]:
        directory = args.results / f'shard-{shard["id"]}'
        if read(directory / "compiler-notes.json") != notes:
            raise ValueError(f"compiler notes differ from built inventory: {directory}")
        if not (directory / "counters").is_dir():
            raise ValueError(f"missing C++ counter files: {directory / 'counters'}")
        counters.append(directory / "counters")
        dumps = directory / "lua-raw"
        if not dumps.is_dir() or not list(dumps.glob("*.luacov")):
            raise ValueError(f"missing Lua dumps: {dumps}")
        merge_lua_evidence(dumps, raw, shard["id"])
    if args.build is not None:
        # A restored bundle: its notes are already where the compiler wrote
        # them, and they must be the planned build's.
        tree = args.build
        tool = tree / LINES_TOOL
        if compiler_notes(tree) != notes:
            raise ValueError(f"compiler notes differ from built inventory: {tree}")
    else:
        # The gate job: the notes arrive in one archive beside the plan, and
        # so does the one built tool the gate runs (the Lua executable-line
        # oracle). Each defines a denominator, so each must be the planned
        # build's own, byte for byte.
        tree = Path(manifest["build_relative"])
        tool = args.lines_tool
        if tree.is_absolute() or ".." in tree.parts:
            raise ValueError(f"planned build tree is not inside the checkout: {tree}")
        if not tool.is_file():
            raise ValueError(f"executable-line oracle missing: {tool}")
        if hashlib.sha256(tool.read_bytes()).hexdigest() != manifest.get("lines_tool_sha256"):
            raise ValueError(f"executable-line oracle is not the planned build's: {tool}")
        # Artifact transport does not carry the executable bit.
        tool.chmod(tool.stat().st_mode | 0o111)
        unpack_notes(args.notes, tree, notes)
        restore_source_links(tree, manifest["source_links"], Path.cwd().resolve())
    alias = source_alias(manifest["source_root"], Path.cwd().resolve())
    try:
        merge_counters(counters, tree, notes)
        scan_counters(tree, manifest["production_exclusions"], args.out)
    finally:
        if alias is not None:
            alias.unlink()

    subprocess.run([sys.executable, "scripts/coverage/coverage_report.py", "--repo-root", ".",
                    "--lua-raw-dir", str(raw), "--lines-tool", str(tool),
                    "--cpp-tracefile", str(args.out / "cpp.info"),
                    "--cpp-production-tracefile", str(args.out / "production.info"),
                    "--output-dir", str(args.out),
                    "--line-threshold", "98", "--function-threshold", "100", "--strict-cpp"],
                   check=True)


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
    p.add_argument("--notes-out", type=Path,
                   help="coverage lane: also archive the compiler notes here for the gate")
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
            location = p.add_mutually_exclusive_group(required=True)
            location.add_argument("--build", type=Path)
            location.add_argument("--lines-tool", type=Path)
            p.add_argument("--notes", type=Path,
                           help="with --lines-tool: the planned build's compiler-note archive")
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
