#!/usr/bin/env python3
"""Static check that tests/parity/coverage_targets.h is a superset of every
FAMILY_* defined in include/openglad/core/constants.h and every EventKind
in include/openglad/gameplay/event.h.

Used as a pre-build gate. Exits 0 on success; on failure exits 1 with a
named list of missing entries on stderr.

The check is purely textual — it parses the C++ headers with regexes
sufficient for OpenGlad's `inline constexpr int FAMILY_X = N;` and
`EnumName = N,` conventions. The intent is to catch the easy mistake of
adding a family or event kind without also wiring it into the parity
manifest.

Phase 01 adds two stdout-only subcommands:
  --emit-gap-table     pipe-table per-target coverage inventory
  --emit-scenario-list tab-separated <id>\\t<compare_mode>\\t<is_branch_internal>
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
CONSTANTS_H = REPO_ROOT / "include" / "openglad" / "core" / "constants.h"
EVENT_H = REPO_ROOT / "include" / "openglad" / "gameplay" / "event.h"
COVERAGE_TARGETS_H = REPO_ROOT / "tests" / "parity" / "coverage_targets.h"
SCENARIO_TABLE_H = REPO_ROOT / "tests" / "parity" / "scenario_table.h"
GOLDEN_DIR = REPO_ROOT / "tests" / "parity" / "golden"

# Family ID ranges that the coverage manifest must enumerate. Comment
# headings in constants.h delimit the groups; we use those as anchors
# rather than re-implementing the C++ scope.
CATEGORY_ORDER = [
    ("Living families",   "kRequiredWalkerFamilies"),
    ("Weapon families",   "kRequiredWeaponFamilies"),
    ("Treasure families", "kRequiredTreasureFamilies"),
    ("Generator families","kRequiredGeneratorFamilies"),
    ("FX families",       "kRequiredEffectFamilies"),
]

# Families to exclude from the manifest gate even though they live in
# constants.h. These are infrastructure constants, not gameplay entities.
EXCLUDED_FAMILIES = {
    "FAMILY_RESERVED_TEAM",  # "Special families" group — not a gameplay family
    "FAMILY_NORMAL1",        # "Button graphic families" — UI sprite IDs
    "FAMILY_PLUS",
    "FAMILY_MINUS",
    "FAMILY_WRENCH",
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_constants_h(text: str) -> dict[str, list[str]]:
    """Return a mapping `category_heading -> [FAMILY_*, ...]`.

    Splits the file on lines like `// Living families` (case-insensitive
    in `families`/`Families`), then collects every
    `inline constexpr int FAMILY_*` declared until the next category
    heading.
    """
    headings = [(c[0], c[0]) for c in CATEGORY_ORDER]
    # Precompile regexes
    heading_re = re.compile(
        r"^\s*//\s*(Living families|Weapon families|Treasure families|"
        r"Generator families|FX families|Special families|"
        r"Button graphic families)\s*$",
        re.MULTILINE,
    )
    family_re = re.compile(
        r"^inline\s+constexpr\s+int\s+(FAMILY_[A-Z0-9_]+)\s*=", re.MULTILINE
    )

    # Walk: find each heading marker and the slice of text up to the
    # next heading; collect family names from that slice.
    matches = list(heading_re.finditer(text))
    out: dict[str, list[str]] = {h[0]: [] for h in headings}
    if not matches:
        return out
    for i, m in enumerate(matches):
        category = m.group(1)
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        chunk = text[start:end]
        names = family_re.findall(chunk)
        if category in out:
            out[category].extend(n for n in names if n not in EXCLUDED_FAMILIES)
    return out


def parse_event_kinds(text: str) -> list[str]:
    """Return the list of `EventKind` enumerators except `None`."""
    # Match `EnumName = N,` between `enum class EventKind ...` and `}`.
    start = text.find("enum class EventKind")
    if start < 0:
        return []
    end = text.find("};", start)
    block = text[start:end]
    pairs = re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", block, re.MULTILINE)
    return [p for p in pairs if p != "None"]


def event_kind_to_symbol(name: str) -> str:
    """Mirror tests/parity/state_dump.cpp::event_kind_symbol.

    "PlaySound"     -> "play_sound"
    "EndGame"       -> "end_game"
    "RequestRedraw" -> "request_redraw"
    """
    out: list[str] = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0:
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def parse_coverage_targets(text: str) -> dict[str, set[str]]:
    """Return a dict from array name -> set of token strings present in it."""
    arrays: dict[str, set[str]] = {}
    # We capture array initializer bodies for the known names. The C++
    # source layout is `inline constexpr ... kName[] = { ... };`.
    targets = [
        "kRequiredWalkerFamilies",
        "kRequiredEffectFamilies",
        "kRequiredWeaponFamilies",
        "kRequiredTreasureFamilies",
        "kRequiredGeneratorFamilies",
        "kRequiredEventKinds",
    ]
    for name in targets:
        pattern = re.compile(
            rf"{re.escape(name)}\s*\[\s*\]\s*=\s*\{{(.*?)\}}\s*;",
            re.DOTALL,
        )
        m = pattern.search(text)
        if not m:
            arrays[name] = set()
            continue
        body = m.group(1)
        # FAMILY_* tokens or quoted strings, whichever applies.
        tokens = set(re.findall(r"FAMILY_[A-Z0-9_]+", body))
        tokens |= set(re.findall(r'"([^"]+)"', body))
        arrays[name] = tokens
    return arrays


def parse_coverage_target_ordered(text: str) -> dict[str, list[str]]:
    """Like parse_coverage_targets but preserves declaration order."""
    arrays: dict[str, list[str]] = {}
    targets = [
        "kRequiredWalkerFamilies",
        "kRequiredEffectFamilies",
        "kRequiredWeaponFamilies",
        "kRequiredTreasureFamilies",
        "kRequiredGeneratorFamilies",
        "kRequiredEventKinds",
    ]
    for name in targets:
        pattern = re.compile(
            rf"{re.escape(name)}\s*\[\s*\]\s*=\s*\{{(.*?)\}}\s*;",
            re.DOTALL,
        )
        m = pattern.search(text)
        if not m:
            arrays[name] = []
            continue
        body = m.group(1)
        if name == "kRequiredEventKinds":
            arrays[name] = re.findall(r'"([^"]+)"', body)
        else:
            arrays[name] = re.findall(r"FAMILY_[A-Z0-9_]+", body)
    return arrays


def parse_required_specials(text: str) -> list[tuple[str, int]]:
    """Return the ordered list of (FAMILY_*, slot) pairs in kRequiredSpecials."""
    m = re.search(
        r"kRequiredSpecials\[\]\s*=\s*\{(.*?)\}\s*;",
        text,
        re.DOTALL,
    )
    if not m:
        return []
    body = m.group(1)
    return [
        (fam, int(slot))
        for fam, slot in re.findall(r"\{\s*(FAMILY_[A-Z0-9_]+)\s*,\s*(\d+)\s*\}", body)
    ]


# ---------------------------------------------------------------------------
# Scenario-table parsing (used by --emit-gap-table and --emit-scenario-list).
# ---------------------------------------------------------------------------


def _strip_line_comments(text: str) -> str:
    """Strip C++ // comments line by line (preserves /* */ block comments
    since those carry the FAMILY_*/event-kind hints we want to keep)."""
    out_lines = []
    for line in text.splitlines():
        i = line.find("//")
        out_lines.append(line if i < 0 else line[:i])
    return "\n".join(out_lines)


def _split_top_level_block(body: str) -> list[str]:
    """Return the brace-balanced top-level row strings inside an array body."""
    rows = []
    depth = 0
    start = -1
    in_str = False
    i = 0
    while i < len(body):
        c = body[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == "{":
                if depth == 0:
                    start = i
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0 and start >= 0:
                    rows.append(body[start : i + 1])
                    start = -1
        i += 1
    return rows


def _find_array_body(text: str, name: str) -> str | None:
    """Find `<name>[] = { ... };` and return the inner body, brace-balanced."""
    m = re.search(rf"\b{re.escape(name)}\s*\[\s*\]\s*=\s*\{{", text)
    if not m:
        return None
    start = m.end()
    depth = 1
    i = start
    in_str = False
    while i < len(text) and depth > 0:
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return text[start:i]
        i += 1
    return None


def parse_scenarios(text: str) -> list[dict]:
    """Parse kScenarios[] rows. Returns a list of dicts with keys:
    id, compare_mode, is_branch_internal, exercises, facts_name, spawns_name, row_text
    """
    body = _find_array_body(text, "kScenarios")
    if body is None:
        return []
    rows = []
    for row_text in _split_top_level_block(body):
        idm = re.search(r'"([a-z0-9_]+)"', row_text)
        if not idm:
            continue
        rid = idm.group(1)
        cm = re.search(r"CompareMode::(\w+)", row_text)
        compare_mode = cm.group(1) if cm else ""
        # is_branch_internal is the bool following CompareMode::X,
        bi = re.search(r"CompareMode::\w+\s*,\s*(true|false)", row_text)
        is_branch_internal = bi.group(1) if bi else "false"
        # facts_name: kFacts_<rest>, std::size(kFacts_<rest>)
        fm = re.search(r"\b(kFacts_[A-Za-z0-9_]+)\b", row_text)
        facts_name = fm.group(1) if fm else ""
        sm = re.search(r"\b(kFamilySpawns_[A-Za-z0-9_]+|kSmokeArenaSpawns)\b", row_text)
        spawns_name = sm.group(1) if sm else ""
        # exercises: pick everything between Exercises::... and the following comma
        em = re.search(r"Exercises::([A-Za-z0-9_|\s]*?)(?:,|\Z)", row_text)
        exercises = em.group(1).strip() if em else ""
        rows.append(
            {
                "id": rid,
                "compare_mode": compare_mode,
                "is_branch_internal": is_branch_internal,
                "exercises": exercises,
                "facts_name": facts_name,
                "spawns_name": spawns_name,
                "row_text": row_text,
            }
        )
    return rows


def find_array_bodies(text: str, prefix: str) -> dict[str, str]:
    """Return a dict {name: body_text} for every `<prefix><suffix>[] = { ... };`."""
    out = {}
    for m in re.finditer(rf"\b({re.escape(prefix)}[A-Za-z0-9_]+)\s*\[\s*\]\s*=\s*\{{", text):
        name = m.group(1)
        body = _find_array_body(text, name)
        if body is not None:
            out[name] = body
    return out


# ---------------------------------------------------------------------------
# Coverage attribution: scan scenario row + spawns + facts and find first
# scenario whose textual bag references each target token.
# ---------------------------------------------------------------------------


def parse_exercises_family_map(scen_text: str) -> dict[str, str]:
    """Parse the Exercises enum body in scenario_table.h to build a map
    FAMILY_<NAME> -> Special_<PascalCaseName>; the enum body groups
    enumerators under `// FAMILY_<NAME> (N): ...` headings followed by
    `Special_<CamelName>_<slot> = ...` lines. We pick the first such pair
    per family heading."""
    m = re.search(
        r"enum\s+class\s+Exercises\s*:\s*std::uint64_t\s*\{(.*?)\};",
        scen_text,
        re.DOTALL,
    )
    if not m:
        return {}
    body = m.group(1)
    out: dict[str, str] = {}
    current_family: str | None = None
    for line in body.splitlines():
        head = re.match(r"\s*//\s*(FAMILY_[A-Z0-9_]+)\s*\(", line)
        if head:
            current_family = head.group(1)
            continue
        enum_m = re.match(r"\s*Special_([A-Za-z0-9]+)_(\d+)\s*=", line)
        if enum_m and current_family and current_family not in out:
            out[current_family] = enum_m.group(1)
    return out


# Map FAMILY_<NAME> -> Special_<PascalCaseName>_<slot>. The PascalCase
# part is irregular (FAMILY_FIREELEMENTAL -> FireElemental,
# FAMILY_SMALL_SLIME -> SmallSlime), so we read it from the Exercises
# enum body rather than try to derive it from the family token.
_SPECIAL_FAMILY_MAP: dict[str, str] | None = None


def family_to_special_camel(family: str, scen_text: str | None = None) -> str:
    global _SPECIAL_FAMILY_MAP
    if _SPECIAL_FAMILY_MAP is None:
        if scen_text is None:
            scen_text = read(SCENARIO_TABLE_H)
        _SPECIAL_FAMILY_MAP = parse_exercises_family_map(scen_text)
    if family in _SPECIAL_FAMILY_MAP:
        return _SPECIAL_FAMILY_MAP[family]
    parts = family.removeprefix("FAMILY_").lower().split("_")
    return "".join(p.capitalize() for p in parts)


def _scenario_bag(scn: dict, spawns: dict[str, str], facts: dict[str, str]) -> str:
    parts = [scn["row_text"]]
    if scn["spawns_name"] in spawns:
        parts.append(spawns[scn["spawns_name"]])
    if scn["facts_name"] in facts:
        parts.append(facts[scn["facts_name"]])
    return "\n".join(parts)


def emit_gap_table_main() -> int:
    coverage_text = read(COVERAGE_TARGETS_H)
    scen_text = read(SCENARIO_TABLE_H)

    ordered = parse_coverage_target_ordered(coverage_text)
    specials = parse_required_specials(coverage_text)
    scenarios = parse_scenarios(scen_text)
    spawns = find_array_bodies(scen_text, "kFamilySpawns_")
    # kSmokeArenaSpawns + others without the kFamilySpawns_ prefix
    other_spawn_names = set(s["spawns_name"] for s in scenarios) - set(spawns)
    for name in other_spawn_names:
        if not name:
            continue
        body = _find_array_body(scen_text, name)
        if body is not None:
            spawns[name] = body
    facts = find_array_bodies(scen_text, "kFacts_")

    bags = [(scn["id"], _scenario_bag(scn, spawns, facts)) for scn in scenarios]

    def first_covering(predicate) -> str:
        for sid, bag in bags:
            if predicate(bag):
                return sid
        return "(none yet)"

    def golden_present(sid: str) -> str:
        return "yes" if (GOLDEN_DIR / f"{sid}.json").exists() else "no"

    def family_predicate(family_token: str):
        pat = re.compile(r"\b" + re.escape(family_token) + r"\b")
        return lambda bag: bool(pat.search(bag))

    def event_predicate(event_token: str):
        pat = re.compile(r"/\*\s*" + re.escape(event_token) + r"\s*\*/")
        return lambda bag: bool(pat.search(bag))

    def special_predicate(enum_token: str):
        pat = re.compile(r"\bSpecial_" + re.escape(enum_token) + r"\b")
        return lambda bag: bool(pat.search(bag))

    def emit_table(heading: str, rows: list[tuple[str, str, str, str]]) -> str:
        out = [
            f"### {heading}",
            "| target | observed_in_any_row | covering_scenario_id | golden_present |",
            "|---|---|---|---|",
        ]
        for r in rows:
            out.append(f"| `{r[0]}` | {r[1]} | `{r[2]}` | {r[3]} |")
        return "\n".join(out)

    sections: list[str] = []

    # Walker families
    rows: list[tuple[str, str, str, str]] = []
    for fam in ordered["kRequiredWalkerFamilies"]:
        sid = first_covering(family_predicate(fam))
        rows.append(
            (fam, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Walker families (21)", rows))

    # Weapon families
    rows = []
    for fam in ordered["kRequiredWeaponFamilies"]:
        sid = first_covering(family_predicate(fam))
        rows.append(
            (fam, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Weapon families (20)", rows))

    # Treasure families
    rows = []
    for fam in ordered["kRequiredTreasureFamilies"]:
        sid = first_covering(family_predicate(fam))
        rows.append(
            (fam, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Treasure families (13)", rows))

    # Generator families
    rows = []
    for fam in ordered["kRequiredGeneratorFamilies"]:
        sid = first_covering(family_predicate(fam))
        rows.append(
            (fam, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Generator families (4)", rows))

    # Effect (FX) families
    rows = []
    for fam in ordered["kRequiredEffectFamilies"]:
        sid = first_covering(family_predicate(fam))
        rows.append(
            (fam, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Effect (FX) families (13)", rows))

    # Specials (42 (family, slot) pairs)
    rows = []
    for fam, slot in specials:
        token = f"{family_to_special_camel(fam, scen_text)}_{slot}"
        sid = first_covering(special_predicate(token))
        target_token = f"{fam}:slot{slot}"
        rows.append(
            (target_token, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Specials (42 (family, slot) pairs)", rows))

    # Event kinds
    rows = []
    for ek in ordered["kRequiredEventKinds"]:
        sid = first_covering(event_predicate(ek))
        rows.append(
            (ek, "yes" if sid != "(none yet)" else "no", sid, golden_present(sid))
        )
    sections.append(emit_table("Event kinds (9)", rows))

    sys.stdout.write("\n".join(sections))
    if not sections[-1].endswith("\n"):
        sys.stdout.write("\n")
    return 0


def emit_scenario_list_main() -> int:
    scen_text = read(SCENARIO_TABLE_H)
    scenarios = parse_scenarios(scen_text)
    for scn in scenarios:
        sys.stdout.write(
            f"{scn['id']}\t{scn['compare_mode']}\t{scn['is_branch_internal']}\n"
        )
    return 0


def default_check_main() -> int:
    constants_text = read(CONSTANTS_H)
    event_text = read(EVENT_H)
    coverage_text = read(COVERAGE_TARGETS_H)

    declared = parse_constants_h(constants_text)
    declared_event_kinds = [event_kind_to_symbol(k) for k in parse_event_kinds(event_text)]
    arrays = parse_coverage_targets(coverage_text)

    missing: list[str] = []

    for heading, array_name in CATEGORY_ORDER:
        for fam in declared.get(heading, []):
            if fam not in arrays.get(array_name, set()):
                missing.append(f"{array_name} is missing {fam} ({heading})")

    for kind_symbol in declared_event_kinds:
        if kind_symbol not in arrays.get("kRequiredEventKinds", set()):
            missing.append(
                f"kRequiredEventKinds is missing \"{kind_symbol}\" "
                "(declared in include/openglad/gameplay/event.h)"
            )

    if missing:
        sys.stderr.write(
            "check_coverage_manifest.py: tests/parity/coverage_targets.h is "
            "out of sync with header declarations:\n"
        )
        for line in missing:
            sys.stderr.write(f"  - {line}\n")
        sys.stderr.write(
            "Update tests/parity/coverage_targets.h and "
            ".plan/parity-coverage-manifest.md, then re-run this check.\n"
        )
        return 1

    print("check_coverage_manifest.py: OK")
    print(f"  walker_families   = {len(arrays['kRequiredWalkerFamilies'])}")
    print(f"  weapon_families   = {len(arrays['kRequiredWeaponFamilies'])}")
    print(f"  treasure_families = {len(arrays['kRequiredTreasureFamilies'])}")
    print(f"  generator_families= {len(arrays['kRequiredGeneratorFamilies'])}")
    print(f"  effect_families   = {len(arrays['kRequiredEffectFamilies'])}")
    print(f"  event_kinds       = {len(arrays['kRequiredEventKinds'])}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Parity coverage manifest gate (Phase 01 adds gap-table / scenario-list emitters)",
    )
    parser.add_argument(
        "--emit-gap-table",
        action="store_true",
        help="Emit seven pipe tables (walker/weapon/treasure/generator/effect/specials/event) to stdout.",
    )
    parser.add_argument(
        "--emit-scenario-list",
        action="store_true",
        help="Emit one tab-separated <id>\\t<compare_mode>\\t<is_branch_internal> per kScenarios row.",
    )
    args = parser.parse_args()

    if args.emit_gap_table and args.emit_scenario_list:
        sys.stderr.write("--emit-gap-table and --emit-scenario-list are mutually exclusive\n")
        return 2
    if args.emit_gap_table:
        return emit_gap_table_main()
    if args.emit_scenario_list:
        return emit_scenario_list_main()
    return default_check_main()


if __name__ == "__main__":
    sys.exit(main())
