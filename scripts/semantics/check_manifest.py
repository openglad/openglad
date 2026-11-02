#!/usr/bin/env python3
"""Check the simulation semantics manifest against the source tree.

The manifest (tests/semantics/manifest/<basename>.json, one file per
simulation source file) holds one row per branch point of the simulation:
a rule row for every game semantic, with the condition it is anchored on,
its arms and the tests that assert them, and an "other" row (plumbing,
defensive guard, loop, dead hit, test-only path) with a note for every
branch line that carries no game rule. docs/sim-semantics.md is the
reader's page; this docstring is the contract.

The check passes (exit 0) only when all of these hold:

  R1  every candidate branch line of every in-scope file is covered by
      exactly one row of that file's manifest, after anchoring.  A
      candidate line is a line the review's grep matches (CPP_BRANCH /
      LUA_BRANCH below, comment lines skipped); the in-scope files are
      SCOPE_CPP plus every Lua file under packs/.
  R2  every rule row cites at least one test, every gtest citation names
      a TEST / TEST_F / TEST_P that exists in the cited file, and every
      parity citation names a row id in tests/parity/scenario_table.h.
  R3  every rule row's status is "asserted", or "deferred" with an issue
      number (deferred rows are printed as notices, not errors).

Anchoring.  Each row carries `anchor`, the trimmed source text of its first
line.  When the first line no longer contains the anchor (whitespace
collapsed), the check searches the file for it, preferring an occurrence
below a line that names the row's function, then the occurrence nearest
the old line.  Without --fix that is a drift error naming the line the
anchor now sits on; with --fix every line of the row moves by the same
delta and the manifest file is rewritten.  A row whose anchor is generic
(a bare `else`, `default:`, `end`, ...) is located only through its lines:
it moves with the preceding anchored row of the same function, and is
reported when that does not put it back on its text.

Exit codes: 0 clean, 1 violations found, 2 the check cannot run (no
manifest directory, unreadable JSON, a malformed row).

Only manifest files are ever written, and only with --fix.  Standard
library only; Python 3.10+.
"""

from __future__ import annotations

import argparse
import contextlib
import glob
import io
import json
import os
import re
import shutil
import sys
import tempfile
import time
from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# Scope and the candidate grep (the ONE place both live)
# --------------------------------------------------------------------------

# The C++ simulation files the review covered: src/gameplay and its public
# headers, networking, snapshot serialization, mapgen and the lobby left out.
SCOPE_CPP: tuple[str, ...] = (
    "include/openglad/gameplay/game_world.h",
    "include/openglad/gameplay/statistics.h",
    "include/openglad/gameplay/walker.h",
    "src/gameplay/astar.cpp",
    "src/gameplay/belt.cpp",
    "src/gameplay/belt_input.cpp",
    "src/gameplay/damage_number_event.cpp",
    "src/gameplay/effect.cpp",
    "src/gameplay/fearless.cpp",
    "src/gameplay/game_world.cpp",
    "src/gameplay/game_world_weather.cpp",
    "src/gameplay/gameplay_context.cpp",
    "src/gameplay/guy.cpp",
    "src/gameplay/living.cpp",
    "src/gameplay/mode/mode_tick.cpp",
    "src/gameplay/obmap.cpp",
    "src/gameplay/placement.cpp",
    "src/gameplay/possession.cpp",
    "src/gameplay/respawn/respawn.cpp",
    "src/gameplay/scenario_strip.cpp",
    "src/gameplay/script/bindings_entity.cpp",
    "src/gameplay/script/bindings_kit.cpp",
    "src/gameplay/script/campaign_hooks.cpp",
    "src/gameplay/script/family_decl.cpp",
    "src/gameplay/script/pack_scripts.cpp",
    "src/gameplay/script/script_host.cpp",
    "src/gameplay/script/world_scripts.cpp",
    "src/gameplay/sim_control_policy.cpp",
    "src/gameplay/sim_difficulty.h",
    "src/gameplay/sim_entity.cpp",
    "src/gameplay/sim_event_log.cpp",
    "src/gameplay/sim_input_handler.cpp",
    "src/gameplay/smooth.cpp",
    "src/gameplay/specials_view.cpp",
    "src/gameplay/stats.cpp",
    "src/gameplay/timed_effects.cpp",
    "src/gameplay/treasure.cpp",
    "src/gameplay/walker.cpp",
    "src/gameplay/walker_combat.cpp",
    "src/gameplay/walker_movement.cpp",
    "src/gameplay/walker_pathing.cpp",
    "src/gameplay/walker_specials.cpp",
    "src/gameplay/weap.cpp",
)
SCOPE_LUA_GLOB = "packs/**/*.lua"

CPP_BRANCH = re.compile(
    r"^\s*(\}\s*)?(if|else if|else|switch|case|default|while|for|do)\b|\?[^?:]*:")
LUA_BRANCH = re.compile(
    r"^\s*(if|elseif|else|while|for|repeat|until)\b|\b(and|or)\b")
COMMENT_PREFIXES = ("//", "--", "*")

MANIFEST_DIR = "tests/semantics/manifest"
SCENARIO_TABLE = "tests/parity/scenario_table.h"

RULE_STATUSES = ("asserted", "partial", "executed-only", "none", "deferred")
OTHER_KINDS = ("plumbing", "defensive", "dead", "test-only", "loop")

# Anchors with no identifying text of their own: after removing braces,
# parentheses, semicolons and whitespace, these say nothing about where
# they belong, so such rows are located through their lines only.
GENERIC_ANCHORS = frozenset({
    "", "else", "default:", "do", "repeat", "end", "then", "elseend",
    "endelse", "break", "return", "}else", "else{",
})


def in_scope_files(repo: str) -> list[str]:
    """Every in-scope source file, repo-relative, sorted."""
    lua = [os.path.relpath(p, repo).replace(os.sep, "/")
           for p in glob.glob(os.path.join(repo, SCOPE_LUA_GLOB), recursive=True)]
    return sorted(set(SCOPE_CPP) | set(lua))


def is_candidate(path: str, text: str) -> bool:
    """The review's grep: does this source line count as a branch line?"""
    stripped = text.strip()
    if stripped.startswith(COMMENT_PREFIXES):
        return False
    rx = LUA_BRANCH if path.endswith(".lua") else CPP_BRANCH
    return rx.search(text) is not None


def read_lines(repo: str, path: str) -> list[str] | None:
    full = os.path.join(repo, path)
    if not os.path.isfile(full):
        return None
    with open(full, encoding="utf-8", errors="replace", newline="") as fh:
        text = fh.read()
    return text.split("\n") if text else []


def candidates_of(path: str, lines: list[str]) -> dict[int, str]:
    return {i: t for i, t in enumerate(lines, 1) if is_candidate(path, t)}


def norm(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def is_generic(anchor: str) -> bool:
    return re.sub(r"[\s{}();]", "", anchor) in GENERIC_ANCHORS


def function_token(function: str | None) -> str | None:
    """The bare identifier of a row's function label ("statistics::walk_to_foe",
    "M.ai_rekindle", "on_act (core:door_open)") or None."""
    if not function:
        return None
    head = re.split(r"[\s(/]", function.strip(), maxsplit=1)[0]
    head = re.split(r"::|[.:]", head)[-1]
    return head if re.fullmatch(r"[A-Za-z_]\w+", head) else None


# --------------------------------------------------------------------------
# Manifest I/O
# --------------------------------------------------------------------------

def dump_manifest(data: dict) -> str:
    """The canonical text of a manifest file (gen_manifest.py writes the same)."""
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def manifest_name(path: str, clashes: set[str]) -> str:
    """tests/semantics/manifest/<name>: the basename, or parent-basename when two
    in-scope files share a basename."""
    base = os.path.basename(path)
    if base in clashes:
        return os.path.basename(os.path.dirname(path)) + "-" + base + ".json"
    return base + ".json"


class CannotRun(Exception):
    pass


@dataclass
class Row:
    file: str             # manifest file (absolute)
    path: str             # source path
    section: str          # "rules" or "other"
    data: dict
    delta: int = 0
    located: bool = True

    @property
    def id(self) -> str:
        return str(self.data["id"])

    @property
    def lines(self) -> list[int]:
        return self.data["lines"]

    @property
    def anchor(self) -> str:
        return str(self.data.get("anchor", ""))

    @property
    def function(self) -> str | None:
        return self.data.get("function")


def load_manifests(repo: str) -> tuple[dict[str, tuple[str, dict]], list[str]]:
    """{source path: (manifest file, data)} plus structural errors."""
    mdir = os.path.join(repo, MANIFEST_DIR)
    if not os.path.isdir(mdir):
        raise CannotRun(f"no manifest directory at {MANIFEST_DIR}")
    out: dict[str, tuple[str, dict]] = {}
    errors: list[str] = []
    for name in sorted(os.listdir(mdir)):
        if not name.endswith(".json"):
            continue
        full = os.path.join(mdir, name)
        rel = f"{MANIFEST_DIR}/{name}"
        try:
            with open(full, encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, ValueError) as exc:
            raise CannotRun(f"{rel}: not readable JSON ({exc})") from exc
        problem = validate_manifest(data)
        if problem:
            raise CannotRun(f"{rel}: {problem}")
        path = data["path"]
        if path in out:
            errors.append(f"{path} is described by both "
                          f"{os.path.basename(out[path][0])} and {name}")
            continue
        out[path] = (full, data)
    return out, errors


def validate_manifest(data: object) -> str | None:
    if not isinstance(data, dict) or not isinstance(data.get("path"), str):
        return 'not a manifest object (needs "path", "rules", "other")'
    for section in ("rules", "other"):
        if not isinstance(data.get(section), list):
            return f'"{section}" is not a list'
        for row in data[section]:
            if not isinstance(row, dict) or not isinstance(row.get("id"), str):
                return f'a row in "{section}" has no string id'
            lines = row.get("lines")
            if (not isinstance(lines, list) or not lines
                    or not all(isinstance(n, int) and n > 0 for n in lines)):
                return f'row {row["id"]}: "lines" must be a non-empty list of line numbers'
            if not isinstance(row.get("anchor", ""), str):
                return f'row {row["id"]}: "anchor" is not a string'
    for row in data["rules"]:
        if row.get("status") not in RULE_STATUSES:
            return f'rule row {row["id"]}: status {row.get("status")!r} is not one of {", ".join(RULE_STATUSES)}'
        if not isinstance(row.get("tests"), list) or not all(isinstance(t, dict) for t in row["tests"]):
            return f'rule row {row["id"]}: "tests" is not a list of objects'
    for row in data["other"]:
        if row.get("kind") not in OTHER_KINDS:
            return f'other row {row["id"]}: kind {row.get("kind")!r} is not one of {", ".join(OTHER_KINDS)}'
    return None


# --------------------------------------------------------------------------
# Anchoring
# --------------------------------------------------------------------------

def find_anchor(row: Row, src: list[str]) -> int | None:
    """Line where the row's anchor sits now, or None."""
    want = norm(row.anchor)
    hits = [i for i, t in enumerate(src, 1) if want in norm(t)]
    if not hits:
        return None
    first = row.lines[0]
    tok = function_token(row.function)
    named = set()
    if tok:
        rx = re.compile(r"\b" + re.escape(tok) + r"\b")
        named = {i for i, t in enumerate(src, 1) if rx.search(t)}

    def owned(i: int) -> bool:
        return any(n <= i for n in named)

    return min(hits, key=lambda i: (not owned(i), abs(i - first), i))


def line_reads(src: list[str], line: int, anchor: str) -> bool:
    return 1 <= line <= len(src) and norm(anchor) in norm(src[line - 1])


def anchor_file(rows: list[Row], src: list[str]) -> list[tuple[str, Row, int | None]]:
    """Locate every row of one file.  Sets row.delta / row.located and returns
    the findings: ("drift", row, new_line), ("lost", row, None),
    ("unanchored", row, None)."""
    findings: list[tuple[str, Row, int | None]] = []
    ordered = sorted(rows, key=lambda r: (r.lines[0], r.id))
    last_by_function: dict[str | None, Row] = {}
    last_any: Row | None = None
    for row in ordered:
        if is_generic(row.anchor):
            if row.function:
                carrier = last_by_function.get(row.function)
            else:
                carrier = last_any
            row.delta = carrier.delta if carrier is not None else 0
            if not line_reads(src, row.lines[0] + row.delta, row.anchor):
                row.delta = 0
                row.located = False
                findings.append(("unanchored", row, None))
            elif row.delta:
                findings.append(("drift", row, row.lines[0] + row.delta))
            continue
        if line_reads(src, row.lines[0], row.anchor):
            row.delta = 0
        else:
            found = find_anchor(row, src)
            if found is None:
                row.located = False
                findings.append(("lost", row, None))
            else:
                row.delta = found - row.lines[0]
                findings.append(("drift", row, found))
        if row.located:
            last_by_function[row.function] = row
            last_any = row
    return findings


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------

TEST_DEF_RX = re.compile(r"\bTEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
MACRO_DEF_RX = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\n)*[^\n]*)",
                          re.MULTILINE)


def tests_defined(text: str) -> set[str]:
    """Every Suite.name a test file defines: TEST/TEST_F/TEST_P written out,
    plus the ones a file-local macro stamps out (#define SCENE(test_name, ...)
    whose body holds TEST_F(Suite, test_name), invoked as SCENE(name, ...))."""
    names = {f"{a}.{b}" for a, b in TEST_DEF_RX.findall(text)}
    for m in MACRO_DEF_RX.finditer(text):
        macro, params = m.group(1), [p.strip() for p in m.group(2).split(",")]
        for suite, pname in TEST_DEF_RX.findall(m.group(3)):
            if pname not in params:
                continue
            k = params.index(pname)
            for call in re.finditer(r"^[ \t]*" + re.escape(macro) + r"\s*\(([^)]*)\)", text, re.MULTILINE):
                args = [a.strip() for a in call.group(1).split(",")]
                if len(args) > k and re.fullmatch(r"\w+", args[k]):
                    names.add(f"{suite}.{args[k]}")
    return names


def parity_row_ids(repo: str) -> set[str]:
    full = os.path.join(repo, SCENARIO_TABLE)
    if not os.path.isfile(full):
        return set()
    with open(full, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    return set(re.findall(r'^\s*\{\s*"(\w+)",\s*"[^"]*",', text, re.MULTILINE))


def test_exists(repo: str, file: str, test: str, cache: dict[str, set[str] | None]) -> str | None:
    """None when `file` defines the gtest Suite.name, else the reason."""
    if file not in cache:
        full = os.path.join(repo, file)
        if os.path.isfile(full):
            with open(full, encoding="utf-8", errors="replace") as fh:
                cache[file] = tests_defined(fh.read())
        else:
            cache[file] = None
    defined = cache[file]
    if defined is None:
        return f"{file} does not exist"
    suite, _, name = test.partition(".")
    if not suite or not name:
        return f'"{test}" is not Suite.name'
    return None if test in defined else f"{file} has no TEST {test}"


@dataclass
class Report:
    errors: dict[str, list[str]] = field(default_factory=dict)
    notices: list[str] = field(default_factory=list)
    fixed: list[str] = field(default_factory=list)
    counts: dict[str, int] = field(default_factory=dict)

    def add(self, kind: str, message: str) -> None:
        self.errors.setdefault(kind, []).append(f"{kind}: {message}")


ERROR_KINDS = (
    "missing file", "stray manifest", "duplicate manifest", "out of range",
    "uncovered", "double-claimed", "drift", "lost anchor", "unanchored",
    "no test", "missing test", "missing parity row", "not asserted",
    "deferred without issue",
)


def run_check(repo: str, fix: bool) -> Report:
    rep = Report()
    manifests, structural = load_manifests(repo)
    for msg in structural:
        rep.add("duplicate manifest", msg)
    scope = in_scope_files(repo)
    scope_set = set(scope)
    for path in sorted(manifests):
        if path not in scope_set:
            rep.add("stray manifest", f"{os.path.basename(manifests[path][0])} describes "
                    f"{path}, which is not an in-scope simulation file")

    parity_ids = parity_row_ids(repo)
    test_cache: dict[str, set[str] | None] = {}
    n_rules = n_other = n_cands = 0
    status_counts = {s: 0 for s in RULE_STATUSES}

    for path in scope:
        src = read_lines(repo, path)
        if src is None:
            rep.add("missing file", f"{path} is in scope but does not exist")
            continue
        cands = candidates_of(path, src)
        n_cands += len(cands)
        mfile, data = manifests.get(path, (None, {"rules": [], "other": []}))
        rows = ([Row(mfile, path, "rules", r) for r in data["rules"]]
                + [Row(mfile, path, "other", r) for r in data["other"]])
        n_rules += len(data["rules"])
        n_other += len(data["other"])

        # Anchor.
        for kind, row, where in anchor_file(rows, src):
            if kind == "drift":
                if fix:
                    rep.fixed.append(f"fixed: {path}: row {row.id} moved {row.delta:+d} "
                                     f"(line {row.lines[0]} -> {where})")
                else:
                    rep.add("drift", f"{path}: row {row.id}: anchor \"{norm(row.anchor)}\" is not on "
                            f"line {row.lines[0]}; it now sits on line {where} (--fix re-points the row)")
            elif kind == "lost":
                rep.add("lost anchor", f"{path}: row {row.id}: anchor \"{norm(row.anchor)}\" is not on "
                        f"line {row.lines[0]} and nowhere else in the file")
            else:
                rep.add("unanchored", f"{path}: row {row.id}: line {row.lines[0]} no longer reads "
                        f"\"{norm(row.anchor)}\" and no anchored row of the same function "
                        f"moved to carry it")
        if fix and mfile is not None and any(r.delta for r in rows):
            for row in rows:
                if row.delta:
                    row.data["lines"] = [n + row.delta for n in row.lines]
                    row.delta = 0
            write_fixed(repo, mfile, data)

        # R1: coverage.
        claims: dict[int, list[str]] = {}
        for row in rows:
            for n in row.lines:
                line = n + row.delta
                if line > len(src):
                    rep.add("out of range", f"{path}: row {row.id}: line {line} is past the "
                            f"end of the file ({len(src)} lines)")
                    continue
                claims.setdefault(line, []).append(row.id)
        for line in sorted(cands):
            owners = claims.get(line, [])
            if not owners:
                rep.add("uncovered", f"{path}:{line}: {cands[line].strip()}")
            elif len(owners) > 1:
                rep.add("double-claimed", f"{path}:{line}: claimed by rows {', '.join(owners)}")

        # R2 and R3.
        for row in (r for r in rows if r.section == "rules"):
            d = row.data
            status = d["status"]
            status_counts[status] += 1
            if not d["tests"]:
                rep.add("no test", f"{path}: row {row.id}: a rule row cites no test")
            for t in d["tests"]:
                if "parity" in t:
                    if str(t["parity"]) not in parity_ids:
                        rep.add("missing parity row", f"{path}: row {row.id}: parity row "
                                f"\"{t['parity']}\" is not in {SCENARIO_TABLE}")
                elif "file" in t and "test" in t:
                    why = test_exists(repo, str(t["file"]), str(t["test"]), test_cache)
                    if why:
                        rep.add("missing test", f"{path}: row {row.id}: {why}")
                else:
                    rep.add("missing test", f"{path}: row {row.id}: a test entry names "
                            f"neither file+test nor parity: {json.dumps(t, sort_keys=True)}")
            if status == "asserted":
                continue
            if status == "deferred":
                issue = d.get("issue")
                if issue_number(issue) is None:
                    rep.add("deferred without issue", f"{path}: row {row.id}: status deferred "
                            f"needs an issue number")
                else:
                    rep.notices.append(f"deferred: {path}: row {row.id}: issue #{issue_number(issue)}")
                continue
            rep.add("not asserted", f"{path}: row {row.id}: status {status}")

    rep.counts = {
        "files": len(scope), "candidates": n_cands, "rules": n_rules, "other": n_other,
        **{f"status {s}": status_counts[s] for s in RULE_STATUSES},
    }
    return rep


def issue_number(issue: object) -> int | None:
    if isinstance(issue, bool):
        return None
    if isinstance(issue, int) and issue > 0:
        return issue
    if isinstance(issue, str):
        m = re.fullmatch(r"\s*#?(\d+)\s*", issue)
        if m and int(m.group(1)) > 0:
            return int(m.group(1))
    return None


def write_fixed(repo: str, mfile: str, data: dict) -> None:
    mdir = os.path.realpath(os.path.join(repo, MANIFEST_DIR))
    if os.path.dirname(os.path.realpath(mfile)) != mdir:
        raise CannotRun(f"refusing to write {mfile}: not in {MANIFEST_DIR}")
    for section in ("rules", "other"):
        data[section].sort(key=lambda r: (r["lines"][0], r["id"]))
    with open(mfile, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(dump_manifest(data))


def render(rep: Report) -> tuple[str, int]:
    out: list[str] = []
    out.extend(rep.fixed)
    for kind in ERROR_KINDS:
        out.extend(rep.errors.get(kind, []))
    out.extend(rep.notices)
    c = rep.counts
    n = {k: len(rep.errors.get(k, [])) for k in ERROR_KINDS}
    not_asserted = c["rules"] - c["status asserted"] - c["status deferred"]
    verdict = "FAIL" if any(n.values()) else "OK"
    out.append(
        f"check_manifest: {verdict}: {c['files']} files, {c['candidates']} candidate lines, "
        f"{c['rules']} rule rows, {c['other']} other rows")
    out.append(
        f"  R1 uncovered {n['uncovered']}, double-claimed {n['double-claimed']}, "
        f"drift {n['drift']}, lost anchor {n['lost anchor']}, unanchored {n['unanchored']}, "
        f"out of range {n['out of range']}, missing file {n['missing file']}, "
        f"stray manifest {n['stray manifest']}, duplicate manifest {n['duplicate manifest']}")
    out.append(
        f"  R2 no test {n['no test']}, missing test {n['missing test']}, "
        f"missing parity row {n['missing parity row']}")
    out.append(
        f"  R3 not asserted {not_asserted} (partial {c['status partial']}, "
        f"executed-only {c['status executed-only']}, none {c['status none']}), "
        f"deferred {c['status deferred']} (without issue {n['deferred without issue']})")
    if rep.fixed:
        out.append(f"  fixed {len(rep.fixed)} rows")
    return "\n".join(out) + "\n", (1 if verdict == "FAIL" else 0)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--fix", action="store_true", help="re-point drifted rows and rewrite their manifest files")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--report", metavar="PATH", help="also write the full output to PATH")
    ap.add_argument("--compare-candidates", metavar="DIR",
                    help="compare this tree's candidate lines with the review's DIR/*.txt "
                         "(lines 'path:line: text') and exit")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    repo = os.path.abspath(args.repo)
    if args.compare_candidates:
        return compare_candidates(repo, args.compare_candidates)
    try:
        rep = run_check(repo, args.fix)
    except CannotRun as exc:
        print(f"check_manifest: cannot run: {exc}")
        return 2
    text, rc = render(rep)
    sys.stdout.write(text)
    if args.report:
        with open(args.report, "w", encoding="utf-8") as fh:
            fh.write(text)
    return rc


def compare_candidates(repo: str, cands_dir: str) -> int:
    review: set[tuple[str, int]] = set()
    for name in sorted(glob.glob(os.path.join(cands_dir, "*.txt"))):
        with open(name, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = re.match(r"^([^:]+):(\d+):", line)
                if m:
                    review.add((m.group(1), int(m.group(2))))
    mine: set[tuple[str, int]] = set()
    for path in in_scope_files(repo):
        src = read_lines(repo, path) or []
        mine |= {(path, n) for n in candidates_of(path, src)}
    only_mine = sorted(mine - review)
    only_review = sorted(review - mine)
    for p, n in only_mine:
        print(f"only the check: {p}:{n}")
    for p, n in only_review:
        print(f"only the review: {p}:{n}")
    same = not only_mine and not only_review
    print(f"compare_candidates: {'EQUAL' if same else 'DIFFERENT'}: the check {len(mine)}, "
          f"the review {len(review)}, only the check {len(only_mine)}, "
          f"only the review {len(only_review)}")
    return 0 if same else 1


# --------------------------------------------------------------------------
# Self-test
# --------------------------------------------------------------------------

_FIXTURE_SRC = """\
// fearless fixture
int pick(int a)
{
    if (a > 3)
        return 1;
    else
        return 2;
}

int other(int b)
{
    int c = b ? 4 : 5;
    while (c > 0)
        c--;
    return c;
}
"""

_FIXTURE_TESTS = """\
TEST(Fearless, picks_one_above_three) { }
TEST_F(FearlessFixture,
       picks_two_otherwise) { }
#define FEARLESS_SCENE(test_name, scene) \\
    TEST_F(FearlessScenes, test_name)    \\
    {                                    \\
        play(scene);                     \\
    }
FEARLESS_SCENE(counts_down_to_zero, "c_count")
"""

_FIXTURE_TABLE = """\
static const ScenarioSpec kScenarios[] = {
    { "fearless_pick_scen99", "scen/scen99.fss", 0x1u,
};
"""


def _fixture_manifest() -> dict:
    return {
        "path": "src/gameplay/fearless.cpp",
        "rules": [
            {"id": "fearless.cpp:4", "lines": [4], "function": "pick",
             "anchor": "if (a > 3)", "semantic": "s", "arms": [], "setting": "none",
             "status": "asserted",
             "tests": [{"file": "tests/unit/test_fearless.cpp", "test": "Fearless.picks_one_above_three"}],
             "missing_arms": []},
            {"id": "fearless.cpp:6", "lines": [6], "function": "pick",
             "anchor": "else", "semantic": "s", "arms": [], "setting": "none",
             "status": "asserted",
             "tests": [{"file": "tests/unit/test_fearless.cpp", "test": "FearlessFixture.picks_two_otherwise"}],
             "missing_arms": []},
            {"id": "fearless.cpp:12", "lines": [12], "function": "other",
             "anchor": "int c = b ? 4 : 5;", "semantic": "s", "arms": [], "setting": "none",
             "status": "asserted",
             "tests": [{"parity": "fearless_pick_scen99", "fact": "WalkerFamilyCount"},
                       {"file": "tests/unit/test_fearless.cpp", "test": "FearlessScenes.counts_down_to_zero"}],
             "missing_arms": []},
        ],
        "other": [
            {"id": "fearless.cpp:13", "lines": [13], "kind": "loop",
             "anchor": "while (c > 0)", "note": "countdown"},
        ],
    }


def _build_repo(root: str, manifest: dict | None, src: str = _FIXTURE_SRC,
                skip: tuple[str, ...] = ()) -> None:
    for path in SCOPE_CPP:
        if path in skip:
            continue
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(src if path == "src/gameplay/fearless.cpp" else "")
    for rel, text in (("tests/unit/test_fearless.cpp", _FIXTURE_TESTS),
                      (SCENARIO_TABLE, _FIXTURE_TABLE)):
        full = os.path.join(root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(text)
    os.makedirs(os.path.join(root, MANIFEST_DIR), exist_ok=True)
    if manifest is not None:
        with open(os.path.join(root, MANIFEST_DIR, "fearless.cpp.json"), "w", encoding="utf-8") as fh:
            fh.write(dump_manifest(manifest))


def _run(argv: list[str]) -> tuple[int, str]:
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = main(argv)
    return rc, buf.getvalue()


def _summary(verdict: str, files: int = len(SCOPE_CPP), rules: int = 3, other: int = 1, cands: int = 4, r1: str = "", r2: str = "",
             r3: str = "", fixed: int = 0) -> str:
    r1 = r1 or ("uncovered 0, double-claimed 0, drift 0, lost anchor 0, unanchored 0, out of range 0, "
                "missing file 0, stray manifest 0, duplicate manifest 0")
    r2 = r2 or "no test 0, missing test 0, missing parity row 0"
    r3 = r3 or "not asserted 0 (partial 0, executed-only 0, none 0), deferred 0 (without issue 0)"
    text = (f"check_manifest: {verdict}: {files} files, {cands} candidate lines, "
            f"{rules} rule rows, {other} other rows\n  R1 {r1}\n  R2 {r2}\n  R3 {r3}\n")
    if fixed:
        text += f"  fixed {fixed} rows\n"
    return text


def self_test() -> int:
    started = time.monotonic()
    failures: list[str] = []
    cases = 0

    def expect(name: str, got: object, want: object) -> None:
        nonlocal cases
        cases += 1
        if got != want:
            failures.append(f"{name}:\n--- want\n{want}\n--- got\n{got}")

    tmp = tempfile.mkdtemp(prefix="semantics_selftest_")
    try:
        def case(name: str, manifest: dict | None, *, src: str = _FIXTURE_SRC,
                 skip: tuple[str, ...] = ()) -> str:
            root = os.path.join(tmp, name)
            _build_repo(root, manifest, src, skip)
            return root

        # The grep, clause by clause (the review's exact rules).
        cpp_lines = {
            "if (x)": True, "  } else if (y) {": True, "else": True, "}else{": True,
            "switch (k) {": True, "case 3:": True, "default:": True, "while (n--)": True,
            "for (;;)": True, "do {": True, "int a = b ? c : d;": True,
            "// if (commented)": False, "* else in a block comment": False,
            "// pick = a ? b : c;": False, " * keeps x ? y : z": False,
            "  /* if */ x = 1;": False, "iffy = 2;": False, "elsewhere();": False,
            "return casey;": False, "x = cond ? std::max(a, b) : c;": True,
            "doit();": False, "a ?: b": True, "q = p ? 1 : 0; // ternary": True,
        }
        lua_lines = {
            "if x then": True, "elseif y then": True, "else": True, "while true do": True,
            "for i = 1, 3 do": True, "repeat": True, "until done": True,
            "local v = a and b": True, "return x or y": True, "-- if commented": False,
            "  --[[ and ]]": False, "local android = 1": False, "ordinal = 2": False,
            "endif = 3": False, "local t = {}": False, "s = 'this or that'": True,
        }
        got_cpp = {t: is_candidate("src/gameplay/x.cpp", t) for t in cpp_lines}
        got_lua = {t: is_candidate("packs/core/lib/x.lua", t) for t in lua_lines}
        expect("grep: C++ clauses", got_cpp, cpp_lines)
        expect("grep: Lua clauses", got_lua, lua_lines)
        expect("grep: fixture candidate lines",
               sorted(candidates_of("src/gameplay/fearless.cpp", _FIXTURE_SRC.split("\n"))),
               [4, 6, 12, 13])
        # A .lua file outside SCOPE_CPP joins the scope by the packs glob.
        root = case("scope", _fixture_manifest())
        os.makedirs(os.path.join(root, "packs/core/lib"), exist_ok=True)
        with open(os.path.join(root, "packs/core/lib/kit.lua"), "w", encoding="utf-8") as fh:
            fh.write("local M = {}\nif x then\n  y = 1\nend\nreturn M\n")
        rc, out = _run(["--repo", root])
        expect("scope: an unclaimed pack Lua line is red",
               (rc, out),
               (1, "uncovered: packs/core/lib/kit.lua:2: if x then\n"
                + _summary("FAIL", cands=5, r1="uncovered 1, double-claimed 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0", files=len(SCOPE_CPP) + 1)))

        # Clean tree.
        root = case("clean", _fixture_manifest())
        expect("clean: green", _run(["--repo", root]), (0, _summary("OK")))

        # R1: uncovered line.
        m = _fixture_manifest()
        del m["other"][0]
        root = case("uncovered", m)
        expect("R1: uncovered line is red", _run(["--repo", root]),
               (1, "uncovered: src/gameplay/fearless.cpp:13: while (c > 0)\n"
                + _summary("FAIL", other=0, r1="uncovered 1, double-claimed 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))

        # R1: double claim.
        m = _fixture_manifest()
        m["other"][0]["lines"] = [12, 13]
        m["other"][0]["anchor"] = "int c = b ? 4 : 5;"
        root = case("double", m)
        expect("R1: double-claimed line is red", _run(["--repo", root]),
               (1, "double-claimed: src/gameplay/fearless.cpp:12: claimed by rows fearless.cpp:12, "
                "fearless.cpp:13\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 1, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))

        # R1: a missing in-scope file and a stray manifest.
        m = _fixture_manifest()
        root = case("missing_file", m, skip=("src/gameplay/belt.cpp",))
        with open(os.path.join(root, MANIFEST_DIR, "elsewhere.json"), "w", encoding="utf-8") as fh:
            fh.write(dump_manifest({"path": "src/net/lobby.cpp", "rules": [], "other": []}))
        expect("R1: missing in-scope file and stray manifest are red", _run(["--repo", root]),
               (1, "missing file: src/gameplay/belt.cpp is in scope but does not exist\n"
                "stray manifest: elsewhere.json describes src/net/lobby.cpp, which is not an "
                "in-scope simulation file\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 0, drift 0, lost anchor 0, "
                           "unanchored 0, out of range 0, missing file 1, stray manifest 1, "
                           "duplicate manifest 0")))

        # R2: missing TEST, missing test file, missing parity row, no test.
        m = _fixture_manifest()
        m["rules"][0]["tests"] = [{"file": "tests/unit/test_fearless.cpp", "test": "Fearless.gone"},
                                  {"file": "tests/unit/test_nowhere.cpp", "test": "A.b"}]
        m["rules"][2]["tests"] = [{"parity": "no_such_scen99", "fact": "x"},
                                  {"file": "tests/unit/test_fearless.cpp", "test": "FearlessScenes.scene"}]
        m["rules"][1]["tests"] = []
        root = case("r2", m)
        expect("R2: missing TEST / file / parity row / no test are red", _run(["--repo", root]),
               (1, "no test: src/gameplay/fearless.cpp: row fearless.cpp:6: a rule row cites no test\n"
                "missing test: src/gameplay/fearless.cpp: row fearless.cpp:4: "
                "tests/unit/test_fearless.cpp has no TEST Fearless.gone\n"
                "missing test: src/gameplay/fearless.cpp: row fearless.cpp:4: "
                "tests/unit/test_nowhere.cpp does not exist\n"
                "missing test: src/gameplay/fearless.cpp: row fearless.cpp:12: "
                "tests/unit/test_fearless.cpp has no TEST FearlessScenes.scene\n"
                "missing parity row: src/gameplay/fearless.cpp: row fearless.cpp:12: parity row "
                "\"no_such_scen99\" is not in tests/parity/scenario_table.h\n"
                + _summary("FAIL", r2="no test 1, missing test 3, missing parity row 1")))

        # R3: partial is red; deferred with an issue is a notice; without one is red.
        m = _fixture_manifest()
        m["rules"][0]["status"] = "partial"
        root = case("partial", m)
        expect("R3: partial is red", _run(["--repo", root]),
               (1, "not asserted: src/gameplay/fearless.cpp: row fearless.cpp:4: status partial\n"
                + _summary("FAIL", r3="not asserted 1 (partial 1, executed-only 0, none 0), "
                           "deferred 0 (without issue 0)")))
        m = _fixture_manifest()
        m["rules"][2]["status"] = "deferred"
        m["rules"][2]["issue"] = 451
        root = case("deferred", m)
        expect("R3: deferred with an issue is a notice only", _run(["--repo", root]),
               (0, "deferred: src/gameplay/fearless.cpp: row fearless.cpp:12: issue #451\n"
                + _summary("OK", r3="not asserted 0 (partial 0, executed-only 0, none 0), "
                           "deferred 1 (without issue 0)")))
        del m["rules"][2]["issue"]
        root = case("deferred_bare", m)
        expect("R3: deferred without an issue is red", _run(["--repo", root]),
               (1, "deferred without issue: src/gameplay/fearless.cpp: row fearless.cpp:12: "
                "status deferred needs an issue number\n"
                + _summary("FAIL", r3="not asserted 0 (partial 0, executed-only 0, none 0), "
                           "deferred 1 (without issue 1)")))

        # Drift: two lines inserted above pick(), one more inside other().
        drifted = _FIXTURE_SRC.replace("int pick(int a)", "#include <x>\n\nint pick(int a)").replace(
            "    int c = b ? 4 : 5;", "    b += 1;\n    int c = b ? 4 : 5;")
        root = case("drift", _fixture_manifest(), src=drifted)
        drift_r1 = ("uncovered 0, double-claimed 0, drift 4, lost anchor 0, unanchored 0, "
                    "out of range 0, missing file 0, stray manifest 0, duplicate manifest 0")
        expect("drift without --fix is red", _run(["--repo", root]),
               (1, "drift: src/gameplay/fearless.cpp: row fearless.cpp:4: anchor \"if (a > 3)\" is not "
                "on line 4; it now sits on line 6 (--fix re-points the row)\n"
                "drift: src/gameplay/fearless.cpp: row fearless.cpp:6: anchor \"else\" is not "
                "on line 6; it now sits on line 8 (--fix re-points the row)\n"
                "drift: src/gameplay/fearless.cpp: row fearless.cpp:12: anchor \"int c = b ? 4 : 5;\" "
                "is not on line 12; it now sits on line 15 (--fix re-points the row)\n"
                "drift: src/gameplay/fearless.cpp: row fearless.cpp:13: anchor \"while (c > 0)\" is not "
                "on line 13; it now sits on line 16 (--fix re-points the row)\n"
                + _summary("FAIL", r1=drift_r1)))
        mpath = os.path.join(root, MANIFEST_DIR, "fearless.cpp.json")
        with open(mpath, encoding="utf-8") as fh:
            before = fh.read()
        expect("drift without --fix leaves the manifest alone", before, dump_manifest(_fixture_manifest()))
        expect("drift with --fix is repaired", _run(["--repo", root, "--fix"]),
               (0, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:4 moved +2 (line 4 -> 6)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:6 moved +2 (line 6 -> 8)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:12 moved +3 (line 12 -> 15)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:13 moved +3 (line 13 -> 16)\n"
                + _summary("OK", fixed=4)))
        with open(mpath, encoding="utf-8") as fh:
            after = json.load(fh)
        expect("--fix moves every row, the generic else with its function's anchored row",
               [(r["id"], r["lines"]) for r in after["rules"] + after["other"]],
               [("fearless.cpp:4", [6]), ("fearless.cpp:6", [8]),
                ("fearless.cpp:12", [15]), ("fearless.cpp:13", [16])])
        with open(mpath, encoding="utf-8") as fh:
            fixed_once = fh.read()
        expect("--fix is idempotent", _run(["--repo", root, "--fix"]), (0, _summary("OK")))
        with open(mpath, encoding="utf-8") as fh:
            expect("--fix twice leaves identical bytes", fh.read(), fixed_once)

        # Lost anchor and an unanchored generic row.
        lost = _FIXTURE_SRC.replace("if (a > 3)", "if (a >= 4)")
        root = case("lost", _fixture_manifest(), src=lost)
        expect("a vanished anchor is red, with or without --fix",
               _run(["--repo", root, "--fix"]),
               (1, "lost anchor: src/gameplay/fearless.cpp: row fearless.cpp:4: anchor \"if (a > 3)\" is "
                "not on line 4 and nowhere else in the file\n"
                + _summary("FAIL", r1="uncovered 0, double-claimed 0, drift 0, lost anchor 1, "
                           "unanchored 0, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0")))
        moved_else = _FIXTURE_SRC.replace("        return 1;\n    else\n", "        return 1;\n\n    else\n")
        root = case("unanchored", _fixture_manifest(), src=moved_else)
        expect("a generic row that no anchored row carries is red", _run(["--repo", root, "--fix"]),
               (1, "fixed: src/gameplay/fearless.cpp: row fearless.cpp:12 moved +1 (line 12 -> 13)\n"
                "fixed: src/gameplay/fearless.cpp: row fearless.cpp:13 moved +1 (line 13 -> 14)\n"
                "uncovered: src/gameplay/fearless.cpp:7: else\n"
                "unanchored: src/gameplay/fearless.cpp: row fearless.cpp:6: line 6 no longer reads "
                "\"else\" and no anchored row of the same function moved to carry it\n"
                + _summary("FAIL", r1="uncovered 1, double-claimed 0, drift 0, lost anchor 0, "
                           "unanchored 1, out of range 0, missing file 0, stray manifest 0, "
                           "duplicate manifest 0", fixed=2)))

        # Exit 2: no manifest directory, malformed row.
        root = case("no_dir", None)
        shutil.rmtree(os.path.join(root, MANIFEST_DIR))
        expect("no manifest directory exits 2", _run(["--repo", root]),
               (2, "check_manifest: cannot run: no manifest directory at tests/semantics/manifest\n"))
        m = _fixture_manifest()
        m["rules"][0]["status"] = "great"
        root = case("malformed", m)
        expect("a malformed row exits 2", _run(["--repo", root]),
               (2, "check_manifest: cannot run: tests/semantics/manifest/fearless.cpp.json: rule row "
                "fearless.cpp:4: status 'great' is not one of asserted, partial, executed-only, none, "
                "deferred\n"))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    elapsed = time.monotonic() - started
    for f in failures:
        print(f"FAIL {f}")
    print(f"check_manifest self-test: {cases - len(failures)}/{cases} cases pass "
          f"({'OK' if not failures else 'FAIL'}, {elapsed:.2f} s)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
