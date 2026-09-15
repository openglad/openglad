#!/usr/bin/env python3
"""Selection and grouping for the parity mutation canary.

`scripts/parity/run_mutation_canary.sh` used to walk scenario rows one at a
time and pay two rebuilds per row, whatever the row's pin actually touched.
Most pins are pack Lua, which no binary embeds, and 63 of the C++ rows share
only 32 distinct pins between them. This module turns a selection request into
the plan the script executes:

  * which rows are IN the run (``--all`` / ``--filter`` / ``--scenario`` /
    ``--touched``),
  * which GROUP each row belongs to — the group key is the pin's full tuple
    ``(file, line, from, to, context_before)``, so two rows that name the same
    constant are mutated, measured and restored ONCE, while two sibling pins
    that happen to sit on one line stay separate groups (their from/to differ,
    and applying one does not apply the other),
  * and how the group is applied: ``staged-lua`` mutates the staged copy under
    ``build/<preset>/packs`` and costs no rebuild, ``rebuild-cpp`` mutates the
    source and costs two.

The plan is data, not action: nothing here writes to a source file, and
``--plan`` on the shell side is exactly "compute this and print it".

Selection rules
---------------
``all``       every master-comparable (non-branch-internal) row. Branch-internal
              rows are excluded here exactly as they are in
              ``lint.parse_scenarios`` and in the companion's ``list_scenarios``:
              an Invariant row compares two dumps of the SAME mutated binary, so
              a mutation cannot flip its compare.
``scenario``  one named row.
``filter``    ``fnmatch.fnmatchcase`` against the id, as the script has always
              done.
``touched``   the on-PR set:
                (a) every row whose pin file is under ``packs/`` — the whole Lua
                    arm is free, so it always runs;
                (b) every row whose pin file is in ``changed_files``;
                (c) every row whose parsed spec differs from the base table —
                    its raw row text, its mutation record, or its ``kFacts_``
                    array text;
                (d) every row absent from the base table.
              and the escalation flag ``all_required``, set when the change
              touches the harness itself (``scripts/parity/``, ``tests/parity``
              sources other than the table — (c) already covers that one —,
              ``cmake/OpenGladTests.cmake`` or the canary workflow). A change to
              the machinery can detooth a pin without editing any pin, so the
              subset stops being an argument and the run escalates to ``--all``.

Usage:
  canary_plan.py --mode all [--emit text|records] [--out <dir>]
  canary_plan.py --mode filter --pattern 'family_*'
  canary_plan.py --mode scenario --pattern combat_attack_scen99
  canary_plan.py --mode touched [--changed-files-from <file>] [--base-table <path>]
  canary_plan.py --self-test

Table path: ``--table``, else ``$OG_CANARY_TABLE``, else
tests/parity/scenario_table.h.

Exit codes:
  0  plan produced
  2  table/JSON could not be parsed, or the generated JSON disagrees with the
     table (rebuild og_test_parity to regenerate it)
  3  the selection is empty
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# The table parser the lint and the pin check already share; the canary must
# read the table the same way they do or it would guard a different corpus.
import lint_scenario_facts as lint  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TABLE = REPO_ROOT / "tests" / "parity" / "scenario_table.h"
DEFAULT_FACTS = REPO_ROOT / "tests" / "parity" / "scenario_facts_generated.json"

STAGED_LUA = "staged-lua"
REBUILD_CPP = "rebuild-cpp"

# A pin under this prefix names a pack asset, which is staged verbatim into the
# build tree by stage_runtime_assets and read exe-adjacent at runtime. Mutating
# the staged copy changes behaviour with no compiler involved.
PACKS_PREFIX = "packs/"


class PlanError(Exception):
    """The table could not be turned into a plan."""


@dataclass(frozen=True)
class Group:
    """One pin, and every selected row that rides on it."""

    key: tuple          # (file, line, from, to, context_before)
    mode: str           # STAGED_LUA | REBUILD_CPP
    rows: tuple         # row ids, table order
    declaration: str    # JSON for OPENGLAD_MUTATION_IN_FLIGHT

    @property
    def file(self) -> str:
        return self.key[0]

    @property
    def line(self) -> int:
        return self.key[1]


@dataclass(frozen=True)
class Plan:
    groups: tuple       # Group, staged-lua arm first
    rows: tuple         # selected row ids, table order
    all_required: bool
    escalation: tuple   # human-readable reasons, empty unless all_required
    unresolved: tuple   # (row id, reason) for rows with no usable pin
    fixtures: tuple     # distinct scenario_file paths the selected rows load


# --- table parsing ----------------------------------------------------------

_FACTS_ARRAY_RX = re.compile(
    r"inline\s+constexpr\s+FactPredicate\s+(kFacts_\w+)\s*\[\]\s*=\s*\{([^}]*)\};",
    re.DOTALL,
)
# The row's scenario file is the second quoted token of the row body.
_ROW_FILE_RX = re.compile(r'"[^"]*"\s*,\s*"([^"]*)"')
_WS_RX = re.compile(r"\s+")


def facts_bodies(text: str) -> dict:
    """{kFacts_name: array body as written}. Same shape the lint parses."""
    return {m.group(1): m.group(2) for m in _FACTS_ARRAY_RX.finditer(text)}


def _normalise(s: str) -> str:
    """Collapse whitespace so a reindent is not read as a spec change.

    Everything else — a changed argument, a changed comment, a new predicate —
    still differs, and differing selects the row. Selection errs towards
    running more rows: a row that runs when it did not need to costs time, a
    row that silently does not run costs the teeth this whole thing is for.
    """
    return _WS_RX.sub(" ", s).strip()


def parse_rows(table_text: str) -> list:
    """Master-comparable rows with their pin and their spec fingerprint."""
    muts = lint.parse_mutation_constants(table_text)
    facts = facts_bodies(table_text)
    rows = []
    for r in lint.parse_scenarios(table_text):
        tok = r.get("mutation_token", "")
        pin = None
        reason = ""
        if not tok or tok.startswith("{"):
            reason = "default-constructed discriminating_mutation"
        elif tok not in muts:
            reason = f"references unknown mutation constant {tok}"
        else:
            m = muts[tok]
            try:
                line_no = int(m["line"])
            except (TypeError, ValueError):
                line_no = 0
            if line_no <= 0 or not m["file"] or not m["from"]:
                reason = f"mutation constant {tok} is incomplete"
            else:
                pin = {
                    "file": m["file"],
                    "line": line_no,
                    "from": m["from"],
                    "to": m["to"],
                    "context_before": m.get("context_before", ""),
                }
        fm = _ROW_FILE_RX.search(r["raw"].strip())
        facts_name = r.get("expected_facts", "nullptr")
        rows.append({
            "id": r["id"],
            "pin": pin,
            "pin_error": reason,
            "mutation_token": tok,
            "scenario_file": fm.group(1) if fm else "",
            "fingerprint": (
                _normalise(r["raw"]),
                _normalise(json.dumps(muts.get(tok, {}), sort_keys=True)),
                _normalise(facts.get(facts_name, "")),
            ),
        })
    return rows


def _load_facts_ids(facts_json) -> list:
    if facts_json is None:
        return []
    if isinstance(facts_json, dict):
        data = facts_json
    else:
        try:
            with open(facts_json, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError) as exc:
            raise PlanError(f"cannot read {facts_json}: {exc}") from exc
    return [s["id"] for s in data.get("scenarios", [])
            if not s.get("is_branch_internal", False)]


# --- escalation -------------------------------------------------------------

def escalation_reasons(changed_files) -> list:
    """Changed paths that make a SUBSET run meaningless.

    The pins are only as good as the machinery that applies and evaluates
    them; a change there can detooth every row without touching one pin, and
    no per-file selection can see that. So those changes escalate to --all.
    tests/parity/scenario_table.h is deliberately NOT here: rule (c) already
    selects exactly the rows whose spec the table edit moved.
    """
    out = []
    for raw in changed_files or ():
        f = raw.strip().replace("\\", "/")
        if not f:
            continue
        if f.startswith("scripts/parity/"):
            out.append(f"{f} (canary machinery)")
        elif (f.startswith("tests/parity/")
              and f.endswith((".cpp", ".h"))
              and f != "tests/parity/scenario_table.h"):
            out.append(f"{f} (parity harness source)")
        elif f == "cmake/OpenGladTests.cmake":
            out.append(f"{f} (test wiring)")
        elif f == ".github/workflows/parity-canary.yml":
            out.append(f"{f} (canary workflow)")
    return out


# --- the plan ---------------------------------------------------------------

def plan(table_text, facts_json=None, mode="all", changed_files=None,
         base_table_text=None, pattern=None) -> Plan:
    """Build the canary's execution plan.

    `mode` is "all" | "scenario" | "filter" | "touched"; "scenario" and
    "filter" take their argument in `pattern`. `facts_json` (a path or an
    already-parsed dict) is cross-checked against the table when given:
    scenario_facts_generated.json is a BUILD OUTPUT written back into the
    source tree, so a disagreement means the tree was edited without a
    rebuild and the canary would be measuring a different corpus than the
    gtest it reports beside.
    """
    rows = parse_rows(table_text)
    if not rows:
        raise PlanError("no master-comparable rows found in the table")

    json_ids = _load_facts_ids(facts_json)
    if json_ids and json_ids != [r["id"] for r in rows]:
        missing = sorted(set(r["id"] for r in rows) - set(json_ids))
        extra = sorted(set(json_ids) - set(r["id"] for r in rows))
        raise PlanError(
            "scenario_facts_generated.json disagrees with scenario_table.h "
            "(rebuild og_test_parity to regenerate it); "
            f"only in table: {missing or '-'}; only in json: {extra or '-'}")

    changed = [c.strip().replace("\\", "/") for c in (changed_files or ())
               if c.strip()]
    escalation = escalation_reasons(changed) if mode == "touched" else []
    all_required = bool(escalation)

    selected: list = []
    if mode == "all" or (mode == "touched" and all_required):
        selected = [r["id"] for r in rows]
    elif mode == "scenario":
        known = {r["id"] for r in rows}
        if pattern not in known:
            raise PlanError(f"unknown scenario {pattern!r}")
        selected = [pattern]
    elif mode == "filter":
        selected = [r["id"] for r in rows
                    if fnmatch.fnmatchcase(r["id"], pattern or "")]
    elif mode == "touched":
        base_rows = {}
        if base_table_text:
            try:
                base_rows = {r["id"]: r["fingerprint"]
                             for r in parse_rows(base_table_text)}
            except Exception:
                base_rows = {}
        changed_set = set(changed)
        for r in rows:
            pin = r["pin"]
            take = False
            if pin is None:
                take = True                                   # unresolved: report it
            elif pin["file"].startswith(PACKS_PREFIX):
                take = True                                   # (a) free arm
            elif pin["file"] in changed_set:
                take = True                                   # (b) touched pin file
            if not take and base_table_text:
                base_fp = base_rows.get(r["id"])
                if base_fp is None or base_fp != r["fingerprint"]:
                    take = True                               # (c)/(d) spec moved
            elif not take and not base_table_text:
                take = True            # no base to compare: cannot rule it out
            if take:
                selected.append(r["id"])
    else:
        raise PlanError(f"unknown mode {mode!r}")

    if not selected:
        raise PlanError("selection is empty")

    by_id = {r["id"]: r for r in rows}
    unresolved = tuple((sid, by_id[sid]["pin_error"])
                       for sid in selected if by_id[sid]["pin"] is None)

    groups_by_key = {}
    for sid in selected:
        pin = by_id[sid]["pin"]
        if pin is None:
            continue
        key = (pin["file"], pin["line"], pin["from"], pin["to"],
               pin["context_before"])
        groups_by_key.setdefault(key, []).append(sid)

    def group_mode(key):
        return STAGED_LUA if key[0].startswith(PACKS_PREFIX) else REBUILD_CPP

    ordered_keys = sorted(
        groups_by_key,
        key=lambda k: (0 if group_mode(k) == STAGED_LUA else 1,
                       selected.index(groups_by_key[k][0])),
    )
    groups = tuple(
        Group(
            key=k,
            mode=group_mode(k),
            rows=tuple(groups_by_key[k]),
            # The declaration check_mutation_pins.py accepts while this pin is
            # in flight. It is built from the same record the applier is handed,
            # so the two cannot disagree about what was applied.
            declaration=json.dumps({"file": k[0], "line": k[1],
                                    "from": k[2], "to": k[3]}),
        )
        for k in ordered_keys
    )

    fixtures = []
    for sid in selected:
        sf = by_id[sid]["scenario_file"]
        if sf and sf not in fixtures:
            fixtures.append(sf)

    return Plan(groups=groups, rows=tuple(selected), all_required=all_required,
                escalation=tuple(escalation), unresolved=unresolved,
                fixtures=tuple(fixtures))


# --- emitters ---------------------------------------------------------------

def format_text(p: Plan) -> list:
    """The stable `--plan` rendering. One line per group, then a summary."""
    lines = []
    for reason in p.escalation:
        lines.append(f"escalation: {reason} — every row is in the run")
    total = len(p.groups)
    for i, g in enumerate(p.groups, start=1):
        lines.append(
            f"group {i}/{total} {g.mode} {g.file}:{g.line} "
            f"rows={len(g.rows)}: {','.join(g.rows)}")
    for sid, why in p.unresolved:
        lines.append(f"unresolved {sid}: {why}")
    staged = sum(1 for g in p.groups if g.mode == STAGED_LUA)
    lines.append(
        f"plan: rows={len(p.rows)} groups={total} "
        f"staged-lua={staged} rebuild-cpp={total - staged} "
        f"unresolved={len(p.unresolved)} all_required={int(p.all_required)}")
    return lines


def emit_records(p: Plan, out_dir: Path) -> None:
    """Write the plan for the shell driver.

    Pin texts carry literal tabs and quotes, so the pin fields travel
    NUL-terminated — the same reason lookup_mutation used NUL rather than tab.
    Everything else (ids, counts, modes) is plain ASCII and goes in line files.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    index = []
    for i, g in enumerate(p.groups, start=1):
        fields = [g.file, str(g.line), g.key[2], g.key[3], g.key[4],
                  g.declaration]
        (out_dir / f"{i}.pin").write_bytes(
            b"".join(f.encode("utf-8") + b"\0" for f in fields))
        (out_dir / f"{i}.rows").write_text(
            "".join(f"{r}\n" for r in g.rows), encoding="utf-8")
        index.append(f"{i} {g.mode} {len(g.rows)}")
    (out_dir / "index").write_text("".join(f"{l}\n" for l in index),
                                   encoding="utf-8")
    (out_dir / "rows").write_text("".join(f"{r}\n" for r in p.rows),
                                  encoding="utf-8")
    (out_dir / "escalation").write_text(
        "".join(f"{e}\n" for e in p.escalation), encoding="utf-8")
    (out_dir / "fixtures").write_text(
        "".join(f"{f}\n" for f in p.fixtures), encoding="utf-8")
    (out_dir / "unresolved").write_text(
        "".join(f"{sid}\t{why}\n" for sid, why in p.unresolved),
        encoding="utf-8")
    staged = sum(1 for g in p.groups if g.mode == STAGED_LUA)
    (out_dir / "summary").write_text(
        f"rows={len(p.rows)}\ngroups={len(p.groups)}\n"
        f"staged_lua={staged}\nrebuild_cpp={len(p.groups) - staged}\n"
        f"unresolved={len(p.unresolved)}\n"
        f"all_required={int(p.all_required)}\n", encoding="utf-8")
    (out_dir / "text").write_text(
        "".join(f"{l}\n" for l in format_text(p)), encoding="utf-8")


# --- self-test --------------------------------------------------------------

_SELF_TEST_HEAD = '''
inline constexpr Mutation kMut_lua_a = {
    "packs/core/families/living-00-soldier.lua", 10,
    "local a = 1", "local a = nil",
    "rationale a"
};
inline constexpr Mutation kMut_cpp_b = {
    "src/gameplay/weap.cpp", 20,
    "int x = 1;", "int x = 0;",
    "rationale b"
};
inline constexpr FactPredicate kFacts_row_one[] = {
    pred::TickReached(10),
};
inline constexpr FactPredicate kFacts_row_two[] = {
    pred::TickReached(10),
};
inline constexpr FactPredicate kFacts_row_three[] = {
    pred::TickReached(20),
};
'''

_SELF_TEST_ROWS = '''
inline constexpr ScenarioSpec kScenarios[] = {
    { "row_one", "scen/scen1.fss", 0x1u,
      nullptr, 0, 10,
      CompareMode::SemanticParity, false,
      nullptr, 0, 0, false, true,
      Exercises::None,
      kFacts_row_one, std::size(kFacts_row_one),
      kMut_lua_a },

    { "row_two", "scen/scen1.fss", 0x1u,
      nullptr, 0, 10,
      CompareMode::SemanticParity, false,
      nullptr, 0, 0, false, true,
      Exercises::None,
      kFacts_row_two, std::size(kFacts_row_two),
      kMut_lua_a },

    { "row_three", "scen/scen1.fss", 0x1u,
      nullptr, 0, 20,
      CompareMode::SemanticParity, false,
      nullptr, 0, 0, false, true,
      Exercises::None,
      kFacts_row_three, std::size(kFacts_row_three),
      kMut_cpp_b },

    { "row_internal", "scen/scen1.fss", 0x1u,
      nullptr, 0, 20,
      CompareMode::Invariant, true,
      nullptr, 0, 0, false, true,
      Exercises::None,
      kFacts_row_three, std::size(kFacts_row_three),
      kMut_cpp_b },
};
'''

_SELF_TEST_TABLE = _SELF_TEST_HEAD + _SELF_TEST_ROWS


def _self_test() -> int:
    """Synthetic tables exercise every selection and grouping rule.

    These snippets ARE the fixtures: grouping, the two modes, per-file
    selection, spec-diff selection and escalation each fail here before they
    can fail on the real table.
    """
    failures = []

    def check(name, got, want):
        if got != want:
            failures.append(f"{name}: expected {want!r}, got {got!r}")

    # Branch-internal rows never enter a plan.
    p = plan(_SELF_TEST_TABLE, mode="all")
    check("all.rows", list(p.rows), ["row_one", "row_two", "row_three"])
    # Two rows on one pin share one group; the Lua arm sorts first.
    check("all.groups", [(g.mode, g.rows) for g in p.groups],
          [(STAGED_LUA, ("row_one", "row_two")),
           (REBUILD_CPP, ("row_three",))])
    check("all.escalation", p.all_required, False)

    # Sibling pins on ONE line stay separate groups: same file and line, but a
    # different from/to, so applying one does not apply the other. This is the
    # walker.cpp:1189 shape, and it is why the group key is the whole tuple.
    twin_head = _SELF_TEST_HEAD + """
inline constexpr Mutation kMut_cpp_c = {
    "src/gameplay/weap.cpp", 20,
    "int y = 1;", "int y = 0;",
    "rationale c"
};
inline constexpr Mutation kMut_cpp_d = {
    "src/gameplay/weap.cpp", 20,
    "int x = 1;", "int x = 2;",
    "rationale d — same line AND same from-text as kMut_cpp_b, different to"
};
"""
    twin_rows = _SELF_TEST_ROWS.replace(
        "      kMut_cpp_b },\n\n    { \"row_internal\"",
        "      kMut_cpp_b },\n\n"
        "    { \"row_twin\", \"scen/scen1.fss\", 0x1u,\n"
        "      nullptr, 0, 20,\n"
        "      CompareMode::SemanticParity, false,\n"
        "      nullptr, 0, 0, false, true,\n"
        "      Exercises::None,\n"
        "      kFacts_row_three, std::size(kFacts_row_three),\n"
        "      kMut_cpp_c },\n\n    { \"row_internal\"")
    twin_rows = twin_rows.replace(
        "      kMut_cpp_c },\n\n    { \"row_internal\"",
        "      kMut_cpp_c },\n\n"
        "    { \"row_twin_to\", \"scen/scen1.fss\", 0x1u,\n"
        "      nullptr, 0, 20,\n"
        "      CompareMode::SemanticParity, false,\n"
        "      nullptr, 0, 0, false, true,\n"
        "      Exercises::None,\n"
        "      kFacts_row_three, std::size(kFacts_row_three),\n"
        "      kMut_cpp_d },\n\n    { \"row_internal\"")
    pt = plan(twin_head + twin_rows, mode="all")
    # Three C++ pins on ONE line: b and c differ in from-text, b and d differ
    # ONLY in to-text. A key that drops any field merges a pair and leaves one
    # mutation never applied while its row is reported as measured.
    check("twin.groups", len(pt.groups), 4)
    check("twin.lines",
          sorted(g.line for g in pt.groups if g.mode == REBUILD_CPP),
          [20, 20, 20])
    check("twin.to_texts",
          sorted(g.key[3] for g in pt.groups if g.mode == REBUILD_CPP),
          ["int x = 0;", "int x = 2;", "int y = 0;"])

    # A changed C++ file selects its rows; the Lua arm always rides along.
    p = plan(_SELF_TEST_TABLE, mode="touched",
             changed_files=["src/gameplay/weap.cpp"],
             base_table_text=_SELF_TEST_TABLE)
    check("touched.weap.rows", list(p.rows),
          ["row_one", "row_two", "row_three"])
    check("touched.weap.groups", len(p.groups), 2)

    # An unrelated changed file leaves only the free arm.
    p = plan(_SELF_TEST_TABLE, mode="touched",
             changed_files=["src/interface/screen.cpp"],
             base_table_text=_SELF_TEST_TABLE)
    check("touched.unrelated.rows", list(p.rows), ["row_one", "row_two"])
    check("touched.unrelated.rebuilds",
          [g for g in p.groups if g.mode == REBUILD_CPP], [])

    # A changed FACTS array selects that row even though no file it pins moved.
    edited = _SELF_TEST_TABLE.replace("pred::TickReached(20),",
                                      "pred::TickReached(21),")
    p = plan(edited, mode="touched", changed_files=[],
             base_table_text=_SELF_TEST_TABLE)
    check("touched.facts.rows", list(p.rows),
          ["row_one", "row_two", "row_three"])

    # A changed ROW (new pin token) selects it too.
    repinned = _SELF_TEST_TABLE.replace(
        "      kMut_cpp_b },\n\n    { \"row_internal\"",
        "      kMut_lua_a },\n\n    { \"row_internal\"")
    p = plan(repinned, mode="touched", changed_files=[],
             base_table_text=_SELF_TEST_TABLE)
    check("touched.repin.rows", list(p.rows),
          ["row_one", "row_two", "row_three"])

    # A brand-new row is selected on sight.
    added = _SELF_TEST_TABLE.replace(
        "    { \"row_internal\"",
        "    { \"row_four\", \"scen/scen1.fss\", 0x1u,\n"
        "      nullptr, 0, 20,\n"
        "      CompareMode::SemanticParity, false,\n"
        "      nullptr, 0, 0, false, true,\n"
        "      Exercises::None,\n"
        "      kFacts_row_three, std::size(kFacts_row_three),\n"
        "      kMut_cpp_b },\n\n    { \"row_internal\"")
    p = plan(added, mode="touched", changed_files=[],
             base_table_text=_SELF_TEST_TABLE)
    check("touched.added.rows", sorted(p.rows),
          ["row_four", "row_one", "row_two"])

    # Harness changes escalate to the full set.
    p = plan(_SELF_TEST_TABLE, mode="touched",
             changed_files=["scripts/parity/foo.py"],
             base_table_text=_SELF_TEST_TABLE)
    check("escalate.flag", p.all_required, True)
    check("escalate.rows", list(p.rows), ["row_one", "row_two", "row_three"])
    check("escalate.reason", len(p.escalation), 1)

    # The table itself does NOT escalate — rule (c) covers it exactly.
    p = plan(_SELF_TEST_TABLE, mode="touched",
             changed_files=["tests/parity/scenario_table.h"],
             base_table_text=_SELF_TEST_TABLE)
    check("table.no_escalate", p.all_required, False)

    # filter / scenario modes.
    p = plan(_SELF_TEST_TABLE, mode="filter", pattern="row_t*")
    check("filter.rows", list(p.rows), ["row_two", "row_three"])
    p = plan(_SELF_TEST_TABLE, mode="scenario", pattern="row_three")
    check("scenario.rows", list(p.rows), ["row_three"])
    for bad, why in (("row_internal", "branch-internal row"),
                     ("row_nope", "unknown row")):
        try:
            plan(_SELF_TEST_TABLE, mode="scenario", pattern=bad)
            failures.append(f"scenario.{bad}: {why} was accepted")
        except PlanError:
            pass
    try:
        plan(_SELF_TEST_TABLE, mode="filter", pattern="nothing_*")
        failures.append("filter.empty: empty selection was accepted")
    except PlanError:
        pass

    # A row with no usable pin is reported, never silently dropped.
    orphan = _SELF_TEST_TABLE.replace("      kMut_cpp_b },\n\n    { \"row_internal\"",
                                      "      {} },\n\n    { \"row_internal\"")
    p = plan(orphan, mode="all")
    check("orphan.unresolved", [sid for sid, _ in p.unresolved], ["row_three"])
    check("orphan.groups", len(p.groups), 1)

    # The generated-JSON cross-check fires on drift.
    try:
        plan(_SELF_TEST_TABLE, facts_json={"scenarios": [{"id": "row_one"}]},
             mode="all")
        failures.append("facts.drift: a disagreeing JSON was accepted")
    except PlanError:
        pass

    for f in failures:
        sys.stderr.write(f"canary_plan self-test: {f}\n")
    if failures:
        sys.stderr.write(f"canary_plan self-test: {len(failures)} failure(s)\n")
        return 1
    print("canary_plan self-test: OK")
    return 0


# --- CLI --------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--table")
    ap.add_argument("--facts")
    ap.add_argument("--base-table")
    ap.add_argument("--mode", choices=["all", "scenario", "filter", "touched"])
    ap.add_argument("--pattern")
    ap.add_argument("--changed-files-from")
    ap.add_argument("--emit", choices=["text", "records"], default="text")
    ap.add_argument("--out")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()
    if not args.mode:
        ap.error("--mode is required unless --self-test is given")

    table_path = Path(args.table or os.environ.get("OG_CANARY_TABLE")
                      or DEFAULT_TABLE)
    if not table_path.is_file():
        sys.stderr.write(f"canary_plan: cannot read table {table_path}\n")
        return 2
    table_text = table_path.read_text(encoding="utf-8")

    facts = args.facts
    if facts is None and table_path == DEFAULT_TABLE and DEFAULT_FACTS.is_file():
        facts = DEFAULT_FACTS

    base_text = None
    if args.base_table:
        base_path = Path(args.base_table)
        if base_path.is_file():
            base_text = base_path.read_text(encoding="utf-8")

    changed = []
    if args.changed_files_from:
        try:
            changed = Path(args.changed_files_from).read_text(
                encoding="utf-8").splitlines()
        except OSError as exc:
            sys.stderr.write(f"canary_plan: {exc}\n")
            return 2

    try:
        p = plan(table_text, facts_json=facts, mode=args.mode,
                 changed_files=changed, base_table_text=base_text,
                 pattern=args.pattern)
    except PlanError as exc:
        sys.stderr.write(f"canary_plan: {exc}\n")
        return 3 if "selection is empty" in str(exc) or "unknown scenario" in str(exc) else 2

    if args.emit == "records":
        if not args.out:
            sys.stderr.write("canary_plan: --emit records needs --out <dir>\n")
            return 2
        emit_records(p, Path(args.out))
    for line in format_text(p):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
