#!/usr/bin/env python3
"""Combined C++ / Lua coverage report and gate for OpenGlad.

Since the class-pack conversion, family behaviour lives in Lua under packs/.
The repo's original gate ran gcovr over src/ only, which would go green while
the half of the codebase that actually decides what a soldier does went
untested.  This script produces ONE report over BOTH languages, in lcov
tracefile format (the lingua franca gcovr, lcov and genhtml all read), and
enforces the line/function bar on each half AND on the union — the C++ half
alone, the Lua half alone, and the combination must each meet it. A single
combined bar let the slack in one language absorb a shortfall in the other:
keeping only the 10 largest of 61 dumps put the Lua half at 94.50% line while
the union still read 96.18% PASS.

Inputs
------
C++   an lcov tracefile, either supplied directly (--cpp-tracefile) or
      generated here from a gcov build directory with gcovr (--cpp-build-dir).
Lua   DENOMINATOR from scripts/lua_inventory.py — a purely static enumeration
      of the repository's Lua (every .lua file, every .lua inside every
      committed .glad, every R"LUA(...)LUA" literal), deduplicated by content
      hash. NUMERATOR from the raw dumps the in-process recorder writes
      (og::script::coverage, armed with OPENGLAD_LUA_COVERAGE=<dir>).

THE DENOMINATOR DOES NOT DEPEND ON WHICH TESTS RAN. That is the single
property this file exists to hold, and it used to be false: a script entered
the denominator only by being loaded at runtime. The consequences were all
demonstrated, not theorised —

  * a new example pack with a dozen uncovered lines left the report
    byte-identical, because "never loaded" meant ABSENT rather than 0%;
  * deleting a test deleted its scripts from the denominator, so dropping the
    109 lines of court.lua boss logic moved the combined number UP;
  * the same bytes mounted under N chunk names counted N times in BOTH halves,
    so copying an already-covered pack and mounting it manufactured headroom.

Content-hash identity is what closes the third: identical bytes are ONE entry
however many paths, archives or mounts expose them. The dumps then only say
which lines and prototypes of those entries ran.

HITS BIND TO THE BYTES THE SAME PROCESS DECLARED. A dump's L/F records are
scored only against source declarations (S records) in that same dump, keyed
by (chunk name, sha256 of the source). When the key was the chunk name alone,
merged across dumps last-writer-wins, one process's hits could be scored
against a source a DIFFERENT process declared under the same name: a dump
declaring a 2-line stub for packs/core/scripts/archmage.lua while recording
hits on the real file's uncovered lines took archmage from 362/382 to 382/382
without a line of it running. Now those hits bind to the stub's digest, the
stub's bytes are not repository content, and the run stops.

AND TO THE GENERATION THAT WAS EXECUTING — resolved from the code itself,
not from declaration order. One chunk name can carry two different sources
within one process (a regenerated pack cache mounts both generations under
one path; a campaign .glad overrides a core script while a world compiled
from the old bytes is still alive). The recorder maps every compiled
prototype to the (chunk, digest) it was compiled from, at compile time, and
credits each hit to the EXECUTING prototype's generation — so a stale
closure that keeps running after a re-declaration still credits its own
bytes, and each L/F record carries that generation's digest. A hit therefore
belongs to exactly ONE (chunk, digest). Two alternatives died on the way
here. Crediting every declared generation whose grid contains the line was a
demonstrated dishonest pass: a never-loaded byte-variant of core druid.lua
scored 86/101 off the real file's execution. Crediting the most recently
DECLARED digest was both a dishonest pass (a stale closure's hits marked the
new generation's overlapping lines covered) and a false failure (where the
grids diverged, the same hits landed off-grid and stopped the run).

Outputs (into --output-dir)
---------------------------
    lua.info        lcov tracefile for every Lua source the repository ships
    cpp.info        lcov tracefile for src/            (when C++ was measured)
    combined.info   the two concatenated — feed to genhtml/gcovr
    summary.json    machine-readable totals and per-half gate verdicts

Exit status is non-zero when any measured half — or the union — misses the
thresholds, or when any structural error (see the ERROR lines it prints)
makes the numbers untrustworthy.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import lua_inventory  # noqa: E402  (path set up immediately above)

RAW_SUFFIX = ".luacov"

# The raw dump format this reader understands, pinned exactly. Version skew
# is a HARD error, never a silent skip: an earlier reader ignored the records
# of a newer writer and the Lua numerator quietly collapsed to zero while the
# report still printed a table. Version 5 added the generation digest to L
# and F records — a version-4 dump's hits are ambiguous by construction (the
# recorder pooled them per chunk name) and are refused, not reinterpreted.
DUMP_HEADER = "# openglad-lua-coverage 5"

# L/F digest field meaning "the executing code was compiled from bytes no
# declaration covers" — test Lua compiled from a string literal. A mounted
# pack script is always declared before the engine can compile it, so this
# marker on a packs/ chunk is a hard error.
NO_GENERATION = "-"

# Chunk names the engine loads pack scripts under always start here (see
# og::resources::register_mounted_pack_scripts). A recorded chunk under this
# prefix is a mounted pack script and MUST resolve to a denominator entry;
# anything else is Lua a test compiled from a string literal inside its own
# source file, which is test code, not the product, and is not measured.
PACK_CHUNK_PREFIX = "packs/"

# Digests of Lua that exists only while a test runs. See the file's own
# header; every entry is a hole in the metric, which is why they are spelled
# out one by one in a committed file instead of inferred from where the bytes
# were loaded from.
FIXTURE_DIGESTS_FILE = Path(__file__).resolve().parent / "runtime_only_lua.txt"

# Tracked src/**/*.cpp translation units the coverage build legitimately
# cannot measure (compiled only under Emscripten, fuzz-only drivers, TUs
# with no coverable lines). The C++ denominator is whatever the build
# emitted .gcno for, so an absent TU is otherwise invisible; this committed
# ledger is what turns "invisible" into "on the record, with a reason".
CPP_EXCLUDED_FILE = Path(__file__).resolve().parent / "cpp_excluded.txt"

# The processes a full suite run is expected to have collected dumps from.
# A suite that quietly loses processes does not look broken, it looks like
# slightly lower coverage — with enough slack above the bar, like nothing at
# all. Every recorder process stamps its executable name into its dump (the P
# record) and the population is checked against this committed manifest.
PROCESS_MANIFEST_FILE = (
    Path(__file__).resolve().parent / "recorder_processes.txt"
)

# The writer builds sidecar names from [A-Za-z0-9._-] only and always appends
# ".lua"; the reader enforces the same alphabet so a dump cannot smuggle in a
# path. The ".lua" suffix requirement additionally rules out "." and "..".
SIDECAR_NAME = re.compile(r"^[A-Za-z0-9._-]+\.lua$")
SHA256_HEX = re.compile(r"^[0-9a-f]{64}$")


# ---------------------------------------------------------------------------
# lcov tracefile model
# ---------------------------------------------------------------------------


@dataclass
class FileCoverage:
    path: str
    # line number -> execution count
    lines: Dict[int, int] = field(default_factory=dict)
    # function name -> (definition line, call count)
    functions: Dict[str, Tuple[int, int]] = field(default_factory=dict)

    @property
    def lines_found(self) -> int:
        return len(self.lines)

    @property
    def lines_hit(self) -> int:
        return sum(1 for c in self.lines.values() if c > 0)

    @property
    def functions_found(self) -> int:
        return len(self.functions)

    @property
    def functions_hit(self) -> int:
        return sum(1 for _, c in self.functions.values() if c > 0)


@dataclass
class Totals:
    lines_hit: int = 0
    lines_found: int = 0
    functions_hit: int = 0
    functions_found: int = 0

    def add(self, other: "Totals") -> "Totals":
        return Totals(
            self.lines_hit + other.lines_hit,
            self.lines_found + other.lines_found,
            self.functions_hit + other.functions_hit,
            self.functions_found + other.functions_found,
        )

    @property
    def line_pct(self) -> float:
        return 100.0 * self.lines_hit / self.lines_found if self.lines_found else 100.0

    @property
    def function_pct(self) -> float:
        return (
            100.0 * self.functions_hit / self.functions_found
            if self.functions_found
            else 100.0
        )


def totals_of(files: Iterable[FileCoverage]) -> Totals:
    t = Totals()
    for f in files:
        t.lines_hit += f.lines_hit
        t.lines_found += f.lines_found
        t.functions_hit += f.functions_hit
        t.functions_found += f.functions_found
    return t


def write_tracefile(files: List[FileCoverage], path: Path, test_name: str) -> None:
    out: List[str] = []
    for f in sorted(files, key=lambda x: x.path):
        out.append(f"TN:{test_name}")
        out.append(f"SF:{f.path}")
        for name, (line, _) in sorted(f.functions.items(), key=lambda kv: (kv[1][0], kv[0])):
            out.append(f"FN:{line},{name}")
        for name, (_, count) in sorted(f.functions.items()):
            out.append(f"FNDA:{count},{name}")
        out.append(f"FNF:{f.functions_found}")
        out.append(f"FNH:{f.functions_hit}")
        for line in sorted(f.lines):
            out.append(f"DA:{line},{f.lines[line]}")
        out.append(f"LF:{f.lines_found}")
        out.append(f"LH:{f.lines_hit}")
        out.append("end_of_record")
    path.write_text("\n".join(out) + ("\n" if out else ""), encoding="utf-8")


def repo_relative(path_text: str, repo_root: Path) -> str:
    """A tracefile SF: path as a repo-relative string (gcovr emits absolute
    paths, a hand-fed tracefile may not). Paths outside the repository come
    back unchanged; nothing here treats them as repository content."""
    p = Path(path_text)
    if p.is_absolute():
        try:
            return p.resolve().relative_to(repo_root.resolve()).as_posix()
        except ValueError:
            return path_text
    return p.as_posix()


def git_tracked_src(repo_root: Path, errors: List[str]) -> Optional[Set[str]]:
    """Git-tracked paths under src/, repo-relative. None (plus an error) when
    git cannot answer — the same no-fallback stance as the Lua inventory: a
    denominator check that silently ran against a guess is worse than one
    that refused. The executable comes from the inventory's resolver
    ($OG_GIT_EXECUTABLE / --git-exe, then PATH), so the CMake-plumbed git
    reaches this spawn too; every failure to resolve or run it is COLLECTED
    like any other completeness error, never a traceback."""
    try:
        git = lua_inventory.git_executable()
        proc = subprocess.run(
            [git, "-C", str(repo_root), "ls-files", "--", "src"],
            capture_output=True,
            text=True,
            check=False,
        )
    except (SystemExit, OSError) as exc:
        errors.append(
            f"cannot run git ls-files under {repo_root} ({exc}) — the C++ "
            "completeness checks cannot run, and running without them is "
            "not an option"
        )
        return None
    if proc.returncode != 0:
        errors.append(
            "git ls-files failed under "
            f"{repo_root} ({proc.returncode}): {proc.stderr.strip()} — the "
            "C++ completeness checks cannot run, and running without them "
            "is not an option"
        )
        return None
    return {line.strip() for line in proc.stdout.splitlines() if line.strip()}


def drop_deleted_sources(
    files: List[FileCoverage],
    repo_root: Path,
    tracked_src: Optional[Set[str]],
    strict: bool,
    errors: List[str],
    warnings: List[str],
) -> List[FileCoverage]:
    """Discard records for source files that no longer exist — loudly.

    An incremental gcov build dir keeps .gcno for sources the tree has since
    deleted — on this branch, the 30-odd C++ family implementations the class
    packs replaced. Nothing ever runs them, so they land in the report as
    thousands of lines at 0% and drag the number well below what a clean CI
    checkout measures. They are build litter, not untested code — but a
    silent drop is also how deleting a BADLY covered src/ file without a
    clean rebuild inflates the number, so every drop is accounted for:

      * a dropped path that git still TRACKS is an error in every mode — a
        tracked source missing from disk is a broken checkout, and scoring
        around it would be fiction;
      * under --strict-cpp (the CI mode, where the build directory is always
        freshly configured) ANY drop is an error: there is nothing stale a
        fresh build could have left behind, so a vanished record means the
        tree and the build disagree;
      * otherwise (a developer's incremental build) untracked-and-nonexistent
        records are dropped with a warning that says how many lines left the
        denominator and recommends a clean build.
    """
    kept: List[FileCoverage] = []
    stale: List[str] = []
    stale_lines = 0
    tracked_missing: List[str] = []
    for f in files:
        p = Path(f.path)
        if not p.is_absolute():
            p = repo_root / p
        if p.exists():
            kept.append(f)
            continue
        rel = repo_relative(f.path, repo_root)
        if tracked_src is not None and rel in tracked_src:
            tracked_missing.append(rel)
        else:
            stale.append(rel)
            stale_lines += f.lines_found
    if tracked_missing:
        errors.append(
            f"{len(tracked_missing)} C++ record(s) are for git-TRACKED "
            "sources that do not exist on disk "
            f"(e.g. {tracked_missing[0]}): the checkout and the index "
            "disagree; restore the file(s) or commit their deletion before "
            "trusting any number measured here"
        )
    if stale:
        if strict:
            errors.append(
                f"{len(stale)} C++ record(s) ({stale_lines} lines) are for "
                f"files that no longer exist (e.g. {stale[0]}): under "
                "--strict-cpp every record must correspond to a source on "
                "disk — a fresh CI build directory has no stale .gcno, so a "
                "drop means the build and the tree disagree"
            )
        else:
            warnings.append(
                f"dropped {len(stale)} C++ record(s) ({stale_lines} lines) "
                "for source files that no longer exist — stale .gcno in an "
                "incremental build dir, not untested code. A clean build "
                "directory removes them for good "
                "(cmake --preset ci-coverage after wiping build/ci-coverage)"
            )
    return kept


def check_cpp_completeness(
    files: List[FileCoverage],
    repo_root: Path,
    tracked_src: Optional[Set[str]],
    excluded_file: Path,
    errors: List[str],
) -> None:
    """Every git-tracked src/**/*.cpp is measured, or excluded ON THE RECORD.

    The C++ denominator is whatever the build emitted .gcno for. A TU that is
    only compiled in some other configuration (an Emscripten-only bridge, a
    fuzz driver) — or that a build regression silently stopped compiling —
    simply is not there, and "not there" looks exactly like nothing. So the
    tracefile's population is checked against the repository: a tracked
    src/**/*.cpp that is absent from the gcov data must be listed in the
    committed exclusion file with a stated reason, and the exclusion list
    itself must not rot (a listed file that IS measured, or that git no
    longer tracks, is an error too — the mirror of the stale-fixture rule
    on the Lua side).
    """
    if tracked_src is None:
        return  # git already failed with its own error
    tracked_cpp = {p for p in tracked_src if p.endswith(".cpp")}
    measured = {repo_relative(f.path, repo_root) for f in files}

    excluded: Dict[str, str] = {}
    if not excluded_file.exists():
        errors.append(
            f"C++ exclusion list not found: {excluded_file}. The "
            "completeness check needs the committed ledger of TUs the "
            "coverage build cannot see (see scripts/coverage/README.md)"
        )
    else:
        for lineno, raw in enumerate(
            excluded_file.read_text(encoding="utf-8").splitlines(), start=1
        ):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            path_text, _, reason = line.partition(" ")
            if not reason.strip():
                errors.append(
                    f"{excluded_file.name}:{lineno}: exclusion without a "
                    f"reason: {path_text!r}. Every entry states why the "
                    "coverage build cannot measure the TU"
                )
                continue
            excluded[path_text] = reason.strip()

    unaccounted = sorted(tracked_cpp - measured - set(excluded))
    if unaccounted:
        shown = ", ".join(unaccounted[:5])
        more = "" if len(unaccounted) <= 5 else f" (+{len(unaccounted) - 5} more)"
        errors.append(
            f"{len(unaccounted)} tracked src/ translation unit(s) are "
            f"absent from the gcov data and not listed in "
            f"{excluded_file.name}: {shown}{more}. Either the coverage "
            "build stopped compiling them — fix the build — or they "
            "genuinely cannot be measured here, in which case list each "
            "with its reason so the hole is on the record"
        )
    stale_present = sorted(set(excluded) & measured)
    if stale_present:
        errors.append(
            f"{len(stale_present)} entr(y/ies) in {excluded_file.name} are "
            f"for TUs the gcov data DOES measure: {', '.join(stale_present)}. "
            "Delete the stale exclusion(s); an exclusion list that overstates "
            "its holes hides the day one of them becomes real"
        )
    stale_untracked = sorted(set(excluded) - tracked_cpp)
    if stale_untracked:
        errors.append(
            f"{len(stale_untracked)} entr(y/ies) in {excluded_file.name} "
            "are not git-tracked src/ .cpp files: "
            f"{', '.join(stale_untracked)}. An exclusion cannot outlive the "
            "file it excused"
        )


def parse_tracefile(path: Path) -> List[FileCoverage]:
    """Read an lcov tracefile.

    Handles both the classic FN:/FNDA: records that gcovr emits and the
    lcov 2.x FNL:/FNA: pair, so either producer can feed the gate.
    """
    files: Dict[str, FileCoverage] = {}
    current: Optional[FileCoverage] = None
    # lcov 2.x: FNL:<index>,<start>[,<end>] then FNA:<index>,<count>,<name>
    pending_fnl: Dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("SF:"):
            name = line[3:]
            current = files.setdefault(name, FileCoverage(name))
            pending_fnl = {}
        elif current is None:
            continue
        elif line == "end_of_record":
            current = None
        elif line.startswith("DA:"):
            parts = line[3:].split(",")
            if len(parts) >= 2:
                ln, count = int(parts[0]), int(parts[1])
                current.lines[ln] = current.lines.get(ln, 0) + count
        elif line.startswith("FN:"):
            body = line[3:]
            first, _, name = body.partition(",")
            if name:
                start = int(first.split(",")[0])
                prev = current.functions.get(name)
                current.functions[name] = (start, prev[1] if prev else 0)
        elif line.startswith("FNDA:"):
            count_s, _, name = line[5:].partition(",")
            if name:
                prev = current.functions.get(name, (0, 0))
                current.functions[name] = (prev[0], prev[1] + int(count_s))
        elif line.startswith("FNL:"):
            parts = line[4:].split(",")
            if len(parts) >= 2:
                pending_fnl[parts[0]] = int(parts[1])
        elif line.startswith("FNA:"):
            parts = line[4:].split(",", 2)
            if len(parts) == 3:
                index, count_s, name = parts
                start = pending_fnl.get(index, 0)
                prev = current.functions.get(name, (start, 0))
                current.functions[name] = (start or prev[0], prev[1] + int(count_s))
    return list(files.values())


# ---------------------------------------------------------------------------
# Lua side
# ---------------------------------------------------------------------------


@dataclass
class SourceFacts:
    """What Lua says a source contains. Both totals, one prototype walk."""

    lines: List[int] = field(default_factory=list)
    # (linedefined, lastlinedefined) spans. The SPAN is the prototype's
    # identity: two functions can begin on one line, and keying on the start
    # alone merged them into a single entry that either one could cover.
    functions: List[Tuple[int, int]] = field(default_factory=list)


def source_facts(
    lines_tool: Path,
    cwd: Path,
    paths: List[str],
    display: Optional[Dict[str, str]] = None,
    errors: Optional[List[str]] = None,
) -> Dict[str, SourceFacts]:
    """Run the oracle over `paths`, keyed by the path as passed in.

    `display` maps an on-disk path to the name a human should see in a
    failure. The inventory is staged into a temp directory before the oracle
    sees it, and an error naming `/tmp/og-luacov-XXXX/source-0037.lua` sends
    the reader to a directory that no longer exists; the repository-side
    entry path is the actionable name.

    `errors`, when given, COLLECTS a source Lua will not compile as a gate
    error (the source simply yields no facts) instead of aborting on the
    first one. Aborting was a false-failure amplifier: one broken file
    inside a shipped root killed the whole report mid-flight — no table, no
    summary.json, and every OTHER problem in the tree suppressed behind it.
    The run still fails; it fails with the complete list. A crashed oracle
    (exit outside 0/1) still aborts either way — that is the tool breaking,
    not a source.
    """
    if not paths:
        return {}
    names = display or {}
    result: Dict[str, SourceFacts] = {}
    # Chunk the argument list so a very large source set cannot blow ARG_MAX.
    for start in range(0, len(paths), 200):
        batch = paths[start : start + 200]
        proc = subprocess.run(
            [str(lines_tool), *batch],
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode not in (0, 1):
            raise SystemExit(
                f"{lines_tool} failed ({proc.returncode}): {proc.stderr.strip()}"
            )
        for row in proc.stdout.splitlines():
            parts = row.split("\t")
            if len(parts) >= 3 and parts[1] == "-1":
                shown = names.get(parts[0], parts[0])
                message = f"{shown}: Lua will not compile: {parts[2]}"
                if errors is None:
                    raise SystemExit(message)
                errors.append(message)
                continue
            if len(parts) != 5:
                continue
            path, _, line_payload, _, fn_payload = parts
            spans = []
            for token in fn_payload.split(","):
                if not token:
                    continue
                start_s, _, end_s = token.partition(":")
                spans.append((int(start_s), int(end_s)))
            result[path] = SourceFacts(
                [int(v) for v in line_payload.split(",") if v], spans
            )
    return result


@dataclass
class Dump:
    """One process's raw dump, with its OWN source declarations.

    Hits are attributed per dump, never from a pool merged across dumps: the
    question the gate asks is "did the process that recorded these hits
    declare this chunk, and with which bytes" — a chunk name is not an
    identity, (chunk, sha256 of the source) is. Every L/F record carries the
    digest of the generation whose COMPILED PROTOTYPE executed (the recorder
    binds each prototype to its source at compile time and resolves the
    executing one at record time), so a hit belongs to exactly one generation
    and this report never guesses.
    """

    path: Path
    program: str = ""
    # chunk -> digest -> (sidecar path, origin the engine loaded it from)
    declared: Dict[str, Dict[str, Tuple[Path, str]]] = field(
        default_factory=dict
    )
    # (chunk, generation digest or "" for none, line) -> hit count
    line_hits: Dict[Tuple[str, str, int], int] = field(default_factory=dict)
    # (chunk, digest, linedefined, lastlinedefined) -> [label, body events]
    fn_hits: Dict[Tuple[str, str, int, int], List] = field(
        default_factory=dict
    )


def _parse_s_record(
    dump: Dump,
    parts: List[str],
    raw_dir: Path,
    where: str,
    errors: List[str],
) -> None:
    """Validate and store one S record.

    The sidecar field is a bare file name the reader joins under
    <raw dir>/sources/ itself. It used to be a dump-supplied path joined
    unvalidated — and pathlib's "/" DISCARDS the base when the right-hand
    side is absolute, so a dump could name any file on the machine as the
    source its hits should be scored against. Every check here is a hard
    error: a dump that fails one is evidence, not noise.
    """
    chunk, name, digest, origin = parts[1], parts[2], parts[3], parts[4]
    if not SHA256_HEX.fullmatch(digest):
        errors.append(f"{where}: S record digest is not sha256 hex: {digest!r}")
        return
    if "/" in name or "\\" in name or Path(name).is_absolute():
        errors.append(
            f"{where}: S record sidecar {name!r} is a path, not a bare file "
            "name; refusing to read outside the dump directory"
        )
        return
    if not SIDECAR_NAME.fullmatch(name):
        errors.append(f"{where}: S record sidecar name is malformed: {name!r}")
        return
    sidecar = raw_dir / "sources" / name
    sources_root = (raw_dir / "sources").resolve()
    if sidecar.resolve().parent != sources_root:
        errors.append(
            f"{where}: S record sidecar {name!r} escapes the dump directory"
        )
        return
    try:
        data = sidecar.read_bytes()
    except OSError as exc:
        errors.append(f"{where}: declared source unreadable: {exc}")
        return
    if hashlib.sha256(data).hexdigest() != digest:
        errors.append(
            f"{where}: sidecar {name} does not hash to the digest the dump "
            f"declared ({digest}); a dump cannot claim one script's bytes "
            "and ship another's"
        )
        return
    dump.declared.setdefault(chunk, {})[digest] = (sidecar, origin)


def _parse_generation(
    field_text: str, where: str, errors: List[str]
) -> Optional[str]:
    """The L/F generation field: 64 hex digits, or the no-generation marker.

    Returns "" for the marker, the digest otherwise, None (plus an error) for
    anything else — an unparseable generation must not quietly become "no
    generation", because "no generation" is itself load-bearing (it is what
    makes a pack chunk that ran undeclared a hard failure).
    """
    if field_text == NO_GENERATION:
        return ""
    if SHA256_HEX.fullmatch(field_text):
        return field_text
    errors.append(
        f"{where}: generation field is neither '{NO_GENERATION}' nor sha256 "
        f"hex: {field_text!r}"
    )
    return None


def read_raw_dumps(raw_dir: Path, errors: List[str]) -> List[Dump]:
    """Parse every per-process .luacov dump, strictly.

    A record this parser does not recognise is an ERROR, not a skip. The
    silent-skip alternative was live once: a version-4 writer against a
    version-3 reader dropped every S record on the floor, the Lua numerator
    collapsed to zero, and nothing said why.
    """
    out: List[Dump] = []
    if not raw_dir.is_dir():
        return out
    for path in sorted(raw_dir.glob(f"*{RAW_SUFFIX}")):
        text = path.read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        if not lines or lines[0] != DUMP_HEADER:
            found = lines[0] if lines else "<empty file>"
            errors.append(
                f"{path.name}: not a '{DUMP_HEADER}' dump (first line: "
                f"{found!r}); the recorder and this report disagree about "
                "the format, and skipping records it half-understands is "
                "how a numerator silently vanishes"
            )
            continue
        dump = Dump(path)
        programs: List[str] = []
        for lineno, raw in enumerate(lines[1:], start=2):
            if not raw or raw.startswith("#"):
                continue
            where = f"{path.name}:{lineno}"
            parts = raw.split("\t")
            try:
                if parts[0] == "P" and len(parts) == 2:
                    programs.append(parts[1])
                elif parts[0] == "S" and len(parts) == 5:
                    _parse_s_record(dump, parts, raw_dir, where, errors)
                elif parts[0] == "L" and len(parts) == 5:
                    digest = _parse_generation(parts[2], where, errors)
                    if digest is None:
                        continue
                    key = (parts[1], digest, int(parts[3]))
                    dump.line_hits[key] = (
                        dump.line_hits.get(key, 0) + int(parts[4])
                    )
                elif parts[0] == "F" and len(parts) == 7:
                    digest = _parse_generation(parts[2], where, errors)
                    if digest is None:
                        continue
                    key = (parts[1], digest, int(parts[3]), int(parts[4]))
                    calls = int(parts[5])
                    label = parts[6]
                    entry = dump.fn_hits.get(key)
                    if entry is None:
                        dump.fn_hits[key] = [label, calls]
                    else:
                        if label and (not entry[0] or label < entry[0]):
                            entry[0] = label
                        entry[1] += calls
                else:
                    errors.append(f"{where}: malformed record: {raw!r}")
            except ValueError:
                errors.append(f"{where}: malformed record: {raw!r}")
        if len(programs) != 1 or not programs[0]:
            errors.append(
                f"{path.name}: expected exactly one P record naming the "
                f"recorder process, found {programs!r}; an unattributable "
                "dump defeats the process-population check"
            )
        else:
            dump.program = programs[0]
        out.append(dump)
    return out


def read_fixture_digests(path: Path) -> Dict[str, str]:
    """digest -> note, for Lua that only exists while a test runs.

    Every entry is an acknowledged hole: bytes the engine compiled that the
    repository does not contain, so nothing can measure them. They are listed
    explicitly, in a committed file, precisely because the alternative — the
    report deciding for itself which recorded scripts to disregard — is how
    unmeasured game logic hides. A reviewer sees an added digest in the diff.
    """
    out: Dict[str, str] = {}
    if not path.exists():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        digest, _, note = line.partition(" ")
        out[digest.strip().lower()] = note.strip()
    return out


def read_process_manifest(path: Path, errors: List[str]) -> Set[str]:
    """The committed set of processes a full collection run produces.

    Loss of a process is invisible in a percentage — it reads as slightly
    lower coverage, absorbed by any slack above the bar. The manifest makes
    it structural: a missing process fails the run whatever the numbers say,
    and a new test binary has to be added here, in a diff a reviewer reads.
    """
    if not path.exists():
        errors.append(
            f"recorder process manifest not found: {path}. The population "
            "check cannot run; regenerate the manifest from a full "
            "collection run (see scripts/coverage/README.md)"
        )
        return set()
    out: Set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        out.add(line)
    if not out:
        errors.append(f"recorder process manifest is empty: {path}")
    return out


def function_name(label: str, span: Tuple[int, int]) -> str:
    """lcov FN name for a prototype.

    The span is always in it. Two prototypes in one file can share a
    registration label (one closure registered as two hooks) AND can share a
    start line, and lcov keys function records by name, so the name has to
    carry the whole identity or two prototypes silently become one record.
    """
    start, end = span
    if label:
        return f"{label}@{start}-{end}"
    if start == 0 and end == 0:
        return "main chunk@0-0"
    return f"function@{start}-{end}"


@dataclass
class Attribution:
    """Every dump hit bound to a denominator digest — or accounted for."""

    # (digest, line) -> count, for digests in the static inventory
    line_hits: Dict[Tuple[str, int], int] = field(default_factory=dict)
    # (digest, (start, end)) -> body ran at least once
    fn_hits: Dict[Tuple[str, Tuple[int, int]], bool] = field(
        default_factory=dict
    )
    # (digest, (start, end)) -> smallest registration label
    labels: Dict[Tuple[str, Tuple[int, int]], str] = field(
        default_factory=dict
    )
    # digest -> chunk names that carried those bytes (>1 == the same source
    # mounted several ways; it is still ONE denominator entry)
    aliased: Dict[str, Set[str]] = field(default_factory=dict)
    # "<chunk> (<note>)" for observed runtime-only fixtures
    fixtures: Set[str] = field(default_factory=set)
    # the digests behind those observations — what a stale-entry check needs
    fixture_digests: Set[str] = field(default_factory=set)


def attribute_dumps(
    dumps: List[Dump],
    known_digests: Set[str],
    fixtures: Dict[str, str],
    line_grid: Dict[str, Set[int]],
    fn_grid: Dict[str, Set[Tuple[int, int]]],
    errors: List[str],
) -> Attribution:
    """Bind every recorded hit to the bytes its own process declared.

    Attribution is by CONTENT, per process. Not by chunk name pooled across
    dumps — that let one process's hits be scored against another process's
    declaration — and not by where PhysFS says a file came from — a campaign
    .glad is staged into a temp directory before mounting, so shipped and
    synthetic packs look identical from that angle. The bytes do not.

    A hit belongs to exactly ONE (chunk, digest): the recorder binds every
    compiled prototype to the source it was compiled from and credits each
    hit to the EXECUTING prototype's generation, and the L/F records carry
    that digest. One chunk name can still carry several sources in one
    process (a regenerated pack cache mounts two generations under one path;
    a campaign override re-declares a chunk while closures of the old bytes
    are still alive) — each generation's hits arrive under its own digest
    and are scored only against its own grid, including hits a stale closure
    records AFTER the re-declaration. There is no "credit every declared
    generation whose grid contains the line" fallback any more: that guess
    let execution of one file's bytes mark a never-loaded byte-variant of
    another file as 85% covered, turning a gate FAIL into a PASS with only a
    warning.

    A declared source whose bytes are nowhere in the repository is a HARD
    FAILURE unless it is a listed runtime-only fixture. So is a hit under a
    generation its own dump never declared, a pack chunk that executed while
    no declaration was active, and a hit that lands off its generation's
    grid. Hits with nowhere to go mean the enumeration is wrong, and
    discarding them quietly is how the previous version of this report lost
    109 lines of live boss logic.
    """
    out = Attribution()
    unknown: Dict[str, str] = {}
    # (chunk, claimed digest or "") -> printable description
    undeclared: Dict[Tuple[str, str], str] = {}
    off_grid: List[str] = []
    off_grid_fns: List[str] = []

    for dump in dumps:
        for chunk in sorted(dump.declared):
            generations = dump.declared[chunk]
            for digest in sorted(generations):
                _, origin = generations[digest]
                if digest in known_digests:
                    out.aliased.setdefault(digest, set()).add(chunk)
                elif digest in fixtures:
                    out.fixtures.add(f"{chunk} ({fixtures[digest]})")
                    out.fixture_digests.add(digest)
                else:
                    unknown.setdefault(
                        digest,
                        f"  {digest}  {chunk} <- {origin or 'unknown'} "
                        f"[{dump.path.name}]",
                    )

        def bind(chunk: str, digest: str) -> Optional[str]:
            """The inventory digest a hit is scored against, or None.

            None is only ever "this hit is not measured", and every
            unmeasured case is either fine (test Lua) or already a recorded
            error — nothing falls between:
              * digest "" on a non-pack chunk: test Lua, unmeasured;
              * digest "" on a packs/ chunk: the engine ran pack code whose
                compiled bytes no declaration covers — error;
              * a digest this dump never declared for this chunk: hits bound
                to a source their own process never declared — error;
              * a declared fixture digest: acknowledged hole, observed via
                its S record, hits unmeasured;
              * a declared digest outside the inventory: already an
                "unknown content" error from the declaration pass above.
            """
            if not digest:
                if chunk.startswith(PACK_CHUNK_PREFIX):
                    undeclared.setdefault(
                        (chunk, ""),
                        f"{chunk} [{dump.path.name}, {dump.program}] "
                        "(executed while no declared source was active)",
                    )
                return None
            generations = dump.declared.get(chunk, {})
            if digest not in generations:
                undeclared.setdefault(
                    (chunk, digest),
                    f"{chunk} [{dump.path.name}, {dump.program}] "
                    f"(hits bound to undeclared generation {digest[:12]}…)",
                )
                return None
            if digest in fixtures or digest not in known_digests:
                return None
            return digest

        for (chunk, gen, line), count in sorted(dump.line_hits.items()):
            digest = bind(chunk, gen)
            if digest is None:
                continue
            if line not in line_grid.get(digest, set()):
                # A line this generation's source has no code on. Either the
                # loaded source and the measured source disagree, or the
                # oracle and the debug hook disagree about the grid; both
                # make the report fiction.
                off_grid.append(f"{chunk}:{line}")
                continue
            key = (digest, line)
            out.line_hits[key] = out.line_hits.get(key, 0) + count

        for (chunk, gen, start, end), (label, calls) in sorted(
            dump.fn_hits.items()
        ):
            digest = bind(chunk, gen)
            if digest is None:
                continue
            span = (start, end)
            if span not in fn_grid.get(digest, set()):
                off_grid_fns.append(f"{chunk}:{start}-{end}")
                continue
            key = (digest, span)
            if calls > 0:
                out.fn_hits[key] = True
            else:
                out.fn_hits.setdefault(key, False)
            # One canonical label per (entry, prototype), smallest wins,
            # so merges are order-free and two mounts that registered
            # different names cannot split one prototype into two lcov
            # records.
            if label and (
                key not in out.labels or label < out.labels[key]
            ):
                out.labels[key] = label

    if unknown:
        errors.append(
            f"{len(unknown)} recorded pack script(s) are not repository "
            "content:\n" + "\n".join(sorted(unknown.values())) + "\n"
            "    The engine compiled Lua the static inventory does not know "
            "about. Either it is shipped game logic that "
            "scripts/lua_inventory.py fails to enumerate — fix the "
            "enumeration — or it is Lua a test generates, in which case add "
            f"the digest to {FIXTURE_DIGESTS_FILE.name} with a note saying "
            "what makes it. Do not discard the hits."
        )
    if undeclared:
        listed = ", ".join(sorted(undeclared.values())[:5])
        errors.append(
            f"{len(undeclared)} chunk(s) recorded hits bound to a source "
            f"their own process never declared: {listed}. "
            "og::resources::register_mounted_pack_scripts must call "
            "declare_pack_source for every script it registers, BEFORE the "
            "engine can compile it (the recorder binds prototypes to their "
            "generation at compile time) — a declaration from some OTHER "
            "process does not say what THIS process compiled, and a "
            "generation the dump never declared has no bytes to score "
            "against"
        )
    if off_grid:
        errors.append(
            f"{len(off_grid)} hit(s) on lines the declared source has no "
            f"code on (e.g. {off_grid[0]}): numerator and denominator "
            "disagree"
        )
    if off_grid_fns:
        errors.append(
            f"{len(off_grid_fns)} function hit(s) at a span no prototype "
            f"of the declared source occupies (e.g. {off_grid_fns[0]})"
        )
    return out


def build_lua_coverage(
    repo_root: Path,
    raw_dir: Path,
    lines_tool: Path,
    process_manifest: Path,
    fixture_digests_file: Path,
) -> Tuple[List[FileCoverage], List[str], List[str], Dict]:
    """Returns (per-file coverage, hard errors, warnings, info)."""
    errors: List[str] = []
    warnings: List[str] = []
    dumps = read_raw_dumps(raw_dir, errors)

    # An unmeasured Lua half is a broken run, not a perfect score. Left as a
    # warning this reads as 100% of nothing and sails through a 100% function
    # bar — the exact shape of a gate that cannot fail.
    if not dumps:
        errors.append(
            f"no {RAW_SUFFIX} dumps in {raw_dir}: the pack-Lua recorder never "
            "ran, so the Lua half of this report is not evidence of anything. "
            "Collect it first: cmake --build <build-dir> --target coverage_run"
        )
    elif not any(d.line_hits for d in dumps):
        errors.append(
            f"{len(dumps)} dump(s) in {raw_dir} but not one recorded "
            "line: the recorder was armed and saw no pack Lua execute at all"
        )

    # Which processes contributed. Set-of-names equality against the
    # committed manifest: reruns of a flaky binary add dumps but no names,
    # while a lost process — or a new binary nobody added — is structural.
    observed = {d.program for d in dumps if d.program}
    expected = read_process_manifest(process_manifest, errors)
    if expected:
        missing = sorted(expected - observed)
        unexpected = sorted(observed - expected)
        if missing:
            errors.append(
                f"{len(missing)} recorder process(es) named in "
                f"{process_manifest.name} wrote no dump: "
                f"{', '.join(missing)}. A partial suite is not a lower "
                "number, it is a broken collection run"
            )
        if unexpected:
            errors.append(
                f"{len(unexpected)} process(es) wrote dumps but are not in "
                f"{process_manifest.name}: {', '.join(unexpected)}. Add "
                "them so their absence in a future run is detectable"
            )

    # THE DENOMINATOR. Statically enumerated, deduplicated by content hash,
    # and computed before a single dump is consulted — run zero tests and it
    # is the same set. scan() rather than inventory(): an enumeration problem
    # (undeclared embedded Lua, a stale declaration) makes the denominator
    # itself untrustworthy, so it fails the gate here exactly as it fails
    # the build lint.
    scanned = lua_inventory.scan(repo_root)
    for problem in scanned.problems:
        errors.append(f"lua_inventory: {problem}")
    sources = scanned.sources
    by_digest = lua_inventory.by_digest(sources)

    # One oracle pass over the inventory. Every entry is staged under a flat
    # generated name because an entry may have no file at all (it can live
    # inside a .glad or in a C++ string literal), and because a chunk name is
    # engine-supplied text with no business becoming a filesystem path.
    facts: Dict[str, SourceFacts] = {}
    with tempfile.TemporaryDirectory(prefix="og-luacov-") as tmp:
        tmp_dir = Path(tmp)
        staged: Dict[str, str] = {}
        display: Dict[str, str] = {}
        for index, entry in enumerate(sources):
            staged_path = tmp_dir / f"source-{index:04d}.lua"
            staged_path.write_bytes(entry.data)
            staged[str(staged_path)] = entry.digest
            display[str(staged_path)] = entry.path
        staged_facts = source_facts(
            lines_tool, tmp_dir, sorted(staged), display, errors
        )
        for staged_path, digest in staged.items():
            if staged_path in staged_facts:
                facts[digest] = staged_facts[staged_path]

    line_grid = {d: set(f.lines) for d, f in facts.items()}
    fn_grid = {d: set(f.functions) for d, f in facts.items()}

    fixtures = read_fixture_digests(fixture_digests_file)
    resolution = attribute_dumps(
        dumps, set(by_digest), fixtures, line_grid, fn_grid, errors,
    )

    files: Dict[str, FileCoverage] = {}
    for entry in sources:
        fact = facts.get(entry.digest, SourceFacts())
        cov = FileCoverage(entry.path, {ln: 0 for ln in fact.lines})
        # The function denominator is STATIC, exactly like the line
        # denominator: every prototype the file defines, hook or local helper
        # or anonymous callback, whether or not anything ever loaded the file.
        # Taking it from the runtime dumps instead would mean an unloaded
        # script contributed no functions at all — the fraud the line side
        # already defends against.
        for span in fact.functions:
            label = resolution.labels.get((entry.digest, span), "")
            cov.functions[function_name(label, span)] = (span[0], 0)
        files[entry.digest] = cov

    for (digest, line), count in resolution.line_hits.items():
        files[digest].lines[line] += count
    for (digest, span), ran in resolution.fn_hits.items():
        # FNDA is executed-or-not, deliberately. The recorder counts line
        # events inside the body, which is evidence the body ran but is not
        # a call count, and writing that number into a field every lcov
        # reader labels "call count" would be a lie with a decimal point.
        if ran:
            name = function_name(
                resolution.labels.get((digest, span), ""), span
            )
            files[digest].functions[name] = (span[0], 1)

    never_loaded = sorted(
        entry.path for entry in sources if entry.digest not in resolution.aliased
    )
    if never_loaded:
        warnings.append(
            f"{len(never_loaded)} shipped Lua source(s) no test loaded, "
            f"counted at 0%: {', '.join(never_loaded)}"
        )
    multi = {
        digest: chunks
        for digest, chunks in resolution.aliased.items()
        if len(chunks) > 1
    }
    if multi:
        warnings.append(
            f"{len(multi)} source(s) were mounted under more than one chunk "
            "name and counted once each: "
            + ", ".join(
                f"{by_digest[d].path} ({len(c)}x)" for d, c in sorted(multi.items())
            )
        )
    if resolution.fixtures:
        warnings.append(
            f"{len(resolution.fixtures)} declared runtime-only fixture(s) not "
            f"measured: {', '.join(sorted(resolution.fixtures))}"
        )
    # A listed fixture digest nothing declared all run is the reviewed-holes
    # list going stale — either its test is gone (drop the line) or, worse,
    # something stopped making one of the generations observable. Both have
    # happened: while sidecars were named by chunk and last-writer-wins, the
    # first org.test.regen generation's entry was unreachable — a dead line
    # in a file that exists to be an honest ledger. An ERROR, not a warning:
    # every entry in that file is a reviewed hole in the metric, and a hole
    # nothing exercises any more is either dead weight to delete or a broken
    # test to fix. A ledger that tolerates rot stops being evidence.
    stale_fixtures = sorted(set(fixtures) - resolution.fixture_digests)
    if stale_fixtures:
        errors.append(
            f"{len(stale_fixtures)} digest(s) in {fixture_digests_file.name} "
            "were never observed in this run: "
            + ", ".join(f"{d[:12]} ({fixtures[d]})" for d in stale_fixtures)
            + ". Delete the stale line(s), or fix whatever stopped the "
            "test that generates those bytes from running"
        )
    empty = [e.path for e in sources if not facts.get(e.digest, SourceFacts()).lines]
    if empty:
        warnings.append(f"{len(empty)} Lua source(s) contain no executable lines")

    info = {
        "sources": len(sources),
        "sources_never_loaded": never_loaded,
        "runtime_only_fixtures": sorted(resolution.fixtures),
        "recorder_processes": sorted(observed),
        "aliases": {
            by_digest[d].path: sorted(set(c) | set(by_digest[d].aliases))
            for d, c in sorted(resolution.aliased.items())
            if len(set(c) | set(by_digest[d].aliases)) > 1
        },
    }
    return list(files.values()), errors, warnings, info


# ---------------------------------------------------------------------------
# C++ side
# ---------------------------------------------------------------------------


def gcovr_tracefile(build_dir: Path, repo_root: Path, out: Path) -> None:
    gcovr = shutil.which("gcovr")
    base = [gcovr] if gcovr else [sys.executable, "-m", "gcovr"]
    base += [
        "--root",
        str(repo_root),
        "--filter",
        "src/",
        "--gcov-ignore-parse-errors=suspicious_hits.warn_once_per_file",
        "--gcov-ignore-parse-errors=negative_hits.warn_once_per_file",
    ]
    tail = ["--lcov", str(out), str(build_dir)]
    # Classic FN:/FNDA: records. Besides matching what the Lua side emits,
    # 1.x skips the per-file VER: checksum — and computing that checksum means
    # opening every source, which hard-fails on the stale .gcno an incremental
    # build keeps for sources the branch deleted.
    attempts = [base + ["--lcov-format-version=1.x"] + tail, base + tail]
    for cmd in attempts:
        if out.exists():
            out.unlink()
        proc = subprocess.run(cmd, cwd=repo_root, check=False)
        if proc.returncode == 0 and out.exists():
            return
    raise SystemExit(
        "gcovr failed to produce a C++ tracefile; install gcovr "
        "(python3 -m pip install gcovr) or pass --cpp-tracefile"
    )


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def fmt_row(label: str, t: Totals) -> str:
    # An empty denominator is not 100% — it is "nothing was measured", which
    # is exactly the state a misconfigured run lands in.
    lines = f"{t.lines_hit}/{t.lines_found}"
    funcs = f"{t.functions_hit}/{t.functions_found}"
    line_pct = f"{t.line_pct:.2f}%" if t.lines_found else "n/a"
    func_pct = f"{t.function_pct:.2f}%" if t.functions_found else "n/a"
    return f"  {label:<14}{lines:>16}{line_pct:>10}{funcs:>14}{func_pct:>10}"


def bar_verdicts(
    t: Totals, line_threshold: float, function_threshold: float
) -> Tuple[bool, bool]:
    """(line bar met, function bar met) for one half or the union.

    Same exact-integer rule the repo's C++ gate already uses: percentages
    are formatting, the gate is on counts, so 94.96% cannot round into 95.
    An empty denominator meets nothing.
    """
    line_ok = t.lines_found > 0 and (
        t.lines_hit * 100.0 >= line_threshold * t.lines_found
    )
    if function_threshold >= 100.0:
        func_ok = t.functions_found > 0 and (
            t.functions_hit == t.functions_found
        )
    else:
        func_ok = t.functions_found > 0 and (
            t.functions_hit * 100.0
            >= function_threshold * t.functions_found
        )
    return line_ok, func_ok


def main() -> int:
    repo_root_default = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", type=Path, default=repo_root_default)
    ap.add_argument("--lua-raw-dir", type=Path, required=True,
                    help="directory of *.luacov dumps (OPENGLAD_LUA_COVERAGE)")
    ap.add_argument("--lines-tool", type=Path, required=True,
                    help="path to the og_lua_lines executable")
    ap.add_argument("--cpp-build-dir", type=Path, default=None,
                    help="gcov build directory; runs gcovr over it")
    ap.add_argument("--cpp-tracefile", type=Path, default=None,
                    help="pre-made lcov tracefile for the C++ side")
    ap.add_argument("--output-dir", type=Path, required=True)
    ap.add_argument("--line-threshold", type=float, default=95.0)
    ap.add_argument("--function-threshold", type=float, default=100.0)
    ap.add_argument("--processes-manifest", type=Path,
                    default=PROCESS_MANIFEST_FILE,
                    help="expected recorder process names (population check)")
    ap.add_argument("--fixture-digests", type=Path,
                    default=FIXTURE_DIGESTS_FILE,
                    help="committed ledger of runtime-only Lua digests")
    ap.add_argument("--cpp-excluded", type=Path,
                    default=CPP_EXCLUDED_FILE,
                    help="committed ledger of tracked src/ TUs the coverage "
                         "build cannot measure (completeness check)")
    ap.add_argument("--strict-cpp", action="store_true",
                    help="treat ANY dropped C++ record as an error — for "
                         "fresh build directories (CI), where nothing stale "
                         "can legitimately be dropped")
    ap.add_argument("--no-gate", action="store_true",
                    help="report only; always exit 0")
    ap.add_argument("--git-exe", metavar="PATH",
                    help="git executable for the inventory enumeration and "
                         f"completeness checks; sets {lua_inventory.GIT_ENV_VAR} "
                         "(which the CMake coverage_report target already "
                         "passes through the environment). Default: that "
                         "variable if set, else PATH.")
    args = ap.parse_args()
    if args.git_exe:
        os.environ[lua_inventory.GIT_ENV_VAR] = args.git_exe

    repo_root = args.repo_root.resolve()
    out_dir = args.output_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if not args.lines_tool.exists():
        raise SystemExit(
            f"executable-line oracle not found: {args.lines_tool}\n"
            "Build it first:  cmake --build <build-dir> --target og_lua_lines"
        )

    lua_files, errors, warnings, lua_info = build_lua_coverage(
        repo_root, args.lua_raw_dir.resolve(), args.lines_tool.resolve(),
        args.processes_manifest, args.fixture_digests,
    )
    write_tracefile(lua_files, out_dir / "lua.info", "lua")
    lua_totals = totals_of(lua_files)

    # A Lua half that measured nothing is not a pass. Without this the gate
    # is satisfied by an empty denominator (0/0 reads as 100%) — and with C++
    # measured alongside, an entirely absent Lua half would not even move the
    # combined percentage enough to notice.
    if lua_totals.lines_found == 0:
        errors.append(
            "the Lua denominator is empty: no pack script was measured at all"
        )
    if lua_totals.functions_found == 0:
        errors.append(
            "the Lua function denominator is empty: no pack prototype was "
            "measured at all"
        )

    cpp_files: List[FileCoverage] = []
    cpp_measured = False
    cpp_info = out_dir / "cpp.info"
    if args.cpp_tracefile is not None:
        src = args.cpp_tracefile.resolve()
        if src != cpp_info:
            shutil.copyfile(src, cpp_info)
        cpp_files = parse_tracefile(cpp_info)
        cpp_measured = True
    elif args.cpp_build_dir is not None:
        gcovr_tracefile(args.cpp_build_dir.resolve(), repo_root, cpp_info)
        cpp_files = parse_tracefile(cpp_info)
        cpp_measured = True
    if cpp_measured:
        tracked_src = git_tracked_src(repo_root, errors)
        cpp_files = drop_deleted_sources(
            cpp_files, repo_root, tracked_src, args.strict_cpp, errors,
            warnings,
        )
        check_cpp_completeness(
            cpp_files, repo_root, tracked_src, args.cpp_excluded, errors,
        )
    cpp_totals = totals_of(cpp_files)

    # The mirror of the empty-Lua guard above: a REQUESTED C++ half that
    # found nothing to measure is a broken run, not a smaller union.
    # --cpp-tracefile /dev/null (or a build dir whose .gcno gcovr cannot see)
    # used to collapse the combined bar onto the Lua half and pass.
    if cpp_measured and cpp_totals.lines_found == 0:
        errors.append(
            "the C++ half was requested but measured nothing: the tracefile "
            "contains no line records for sources that exist. Point "
            "--cpp-build-dir at a gcov build that has run the suite, or fix "
            "the tracefile passed via --cpp-tracefile"
        )
    if cpp_measured and cpp_totals.functions_found == 0:
        errors.append(
            "the C++ half was requested but contains no function records"
        )

    # Re-emit both halves through the same writer rather than concatenating
    # the producers' output: gcovr and lcov 2.x disagree about how function
    # records are spelled (FN/FNDA vs FNL/FNA), and a file mixing the two
    # confuses the readers this artifact exists to feed.
    combined_info = out_dir / "combined.info"
    write_tracefile(cpp_files + lua_files, combined_info, "openglad")
    combined = cpp_totals.add(lua_totals)

    # EVERY measured half must meet the bar, and so must the union. A single
    # combined bar is gameable from either side: with C++ at 96%+ the union
    # tolerated a Lua half at 94.50%, which is exactly the shape of "the
    # directive says Lua is in the 95/100 measurement" being technically
    # true and practically false.
    halves: List[Tuple[str, str, Totals]] = []
    if cpp_measured:
        halves.append(("C++ (src/)", "cpp", cpp_totals))
    halves.append(("Lua", "lua", lua_totals))
    halves.append(("COMBINED", "combined", combined))
    verdicts = {
        key: bar_verdicts(t, args.line_threshold, args.function_threshold)
        for _, key, t in halves
    }
    line_ok = all(v[0] for v in verdicts.values())
    func_ok = all(v[1] for v in verdicts.values())

    print("Coverage (line / function), the same bar for each language and the union")
    print(f"  {'Component':<14}{'Lines':>16}{'Line%':>10}{'Functions':>14}{'Func%':>10}")
    if cpp_measured:
        print(fmt_row("C++ (src/)", cpp_totals))
    else:
        print("  C++ (src/)      not measured — pass --cpp-build-dir or --cpp-tracefile")
    print(fmt_row("Lua (packs/)", lua_totals))
    print(fmt_row("COMBINED", combined))
    print(
        f"  thresholds (per half AND combined): line >= "
        f"{args.line_threshold:g}%  function >= {args.function_threshold:g}%"
    )
    for label, key, _ in halves:
        l_ok, f_ok = verdicts[key]
        print(
            f"    {label:<14}line {'PASS' if l_ok else 'FAIL'}   "
            f"function {'PASS' if f_ok else 'FAIL'}"
        )
    for w in warnings:
        print(f"  warning: {w}")
    for e in errors:
        print(f"  ERROR: {e}")
    print(f"  tracefile: {combined_info}")

    summary = {
        "cpp_measured": cpp_measured,
        "cpp": vars(cpp_totals),
        "lua": vars(lua_totals),
        "combined": vars(combined),
        "line_threshold": args.line_threshold,
        "function_threshold": args.function_threshold,
        # PASS only when every measured half (and the union) meets the bar.
        "line_status": "PASS" if line_ok else "FAIL",
        "function_status": "PASS" if func_ok else "FAIL",
        "gate": {
            key: {
                "line": "PASS" if verdicts[key][0] else "FAIL",
                "function": "PASS" if verdicts[key][1] else "FAIL",
            }
            for _, key, _ in halves
        },
        "errors": errors,
        "warnings": warnings,
        "lua_inventory": lua_info,
        "lua_files": [
            {
                "path": f.path,
                "lines_hit": f.lines_hit,
                "lines_found": f.lines_found,
                "functions_hit": f.functions_hit,
                "functions_found": f.functions_found,
            }
            for f in sorted(lua_files, key=lambda x: x.path)
        ],
    }

    status = "PASS" if (line_ok and func_ok and not errors) else "FAIL"
    summary["status"] = status
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )

    print(f"  RESULT: {status}")
    if not cpp_measured:
        print("  (the C++ half was not measured; its bar was not applied — "
              "the CI gate always measures both)")
    if args.no_gate:
        return 0
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
