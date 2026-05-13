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
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
CONSTANTS_H = REPO_ROOT / "include" / "openglad" / "core" / "constants.h"
EVENT_H = REPO_ROOT / "include" / "openglad" / "gameplay" / "event.h"
COVERAGE_TARGETS_H = REPO_ROOT / "tests" / "parity" / "coverage_targets.h"

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


def main() -> int:
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


if __name__ == "__main__":
    sys.exit(main())
