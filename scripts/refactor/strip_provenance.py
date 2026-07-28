#!/usr/bin/env python3
"""Provenance / stale-guard comment strip (Stage 2, style S3).

Two comment populations die here; genuine RNG-order records stay.

1.  DEAD PROVENANCE — comments citing C++ files the design-doc §9a
    retirement deleted (family_*.cpp, weapon_family_*.cpp,
    effect_family_*.cpp, treasure_family_*.cpp).  The header collapse
    (rewrite_headers.py) removes the 17 header-line cites; this tool
    handles the stragglers and then VERIFIES none remain: every comment
    line citing a *.cpp/*.h path is checked against the tree, a dead cite
    matching a curated surgery is rewritten, a dead cite that is the whole
    line is deleted, and any other dead cite is reported as a finding (the
    tool exits 1 so the batch cannot silently keep it).  Cites of LIVE
    files (src/gameplay/effect.cpp, guy.cpp, combat_math.h, ...) are
    legitimate references and are kept.

2.  STALE GUARD WRAPPERS — comment blocks whose only content is the
    og.rand-errors-on-n<=0 / next(0)-does-not-advance rationale that the
    og.rand0 contract now states once, centrally.  After rewrite_rand0.py
    those blocks describe guards that no longer exist.  The corpus' guard
    commentary is FUSED with load-bearing eval-order records in two places
    (orc's FLAGGED block, the drumstick formula record), so this is curated
    per site: each surgery names its exact original lines and the trimmed
    replacement that keeps the record and drops the guard rationale.
    A curated block that no longer matches is skipped with a note (running
    before rewrite_rand0, or on an already-stripped tree, is harmless).

The FLAGGED convention itself is untouched: eval-order adjudications are
determinism records and survive every refactor (S3 kind 1).
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_corpus import REPO, Source, run_rewriter  # noqa: E402

# --------------------------------------------------------------------------
# Curated surgeries: file -> [(exact stripped original lines, replacement
# stripped lines)] — re-indented to the original block's indent.
# --------------------------------------------------------------------------

SURGERIES: dict[str, list[tuple[list[str], list[str]]]] = {
    "orc.lua": [(
        ["-- FLAGGED (two rng calls in one C++ expression, operand order",
         "-- unspecified): tempy = 10 + rng(level*10) - rng(con*10) is",
         "-- written LEFT-FIRST here. rng_.next(0) returns 0 WITHOUT",
         "-- advancing the rng state (irandom.h) while og.rand errors on",
         "-- n <= 0, hence the guards; level/tempx_clamped are never",
         "-- negative here so the C++ uint32 casts are value-preserving."],
        ["-- FLAGGED (two rng calls in one C++ expression, operand order",
         "-- unspecified): tempy = 10 + rng(level*10) - rng(con*10) is",
         "-- written LEFT-FIRST here; level/tempx_clamped are never negative,",
         "-- so the C++ uint32 casts are value-preserving."],
    )],
    "treasure_consumables.lua": [(
        ["-- C++: 10*level + rng_.next((uint32)(10*level)). rng_.next(0) yields 0",
         "-- WITHOUT advancing the stream while og.rand(0) raises, so the bound is",
         "-- guarded to keep the draw count identical at level 0."],
        ["-- C++: 10*level + rng_.next((uint32)(10*level)); og.rand0 keeps the",
         "-- level-0 draw count at zero."],
    )],
    "effect_ghost_scare.lua": [(
        ["-- rng_.next(0) returns 0 without advancing the stream; og.rand(0)",
         "-- raises, so the zero bound is guarded."],
        [],
    )],
    "elf.lua": [(
        ["-- next_spread_multiplier (family_elf.cpp): the C++ picks its RNG source as"],
        ["-- next_spread_multiplier: the legacy code picks its RNG source as"],
    )],
    "cleric.lua": [(
        ["-- Shared turn-undead block: the case-2 and case-3 shifter_down bodies in",
         "-- family_cleric.cpp are token-identical. Returns false where the C++",
         "-- returned false out of do_special; true = fall through to `return true`."],
        ["-- Shared turn-undead block: the case-2 and case-3 shifter_down bodies",
         "-- were token-identical. Returns false where the legacy code returned",
         "-- false out of do_special; true = fall through to `return true`."],
    )],
}

CITE = re.compile(r"\b([A-Za-z_][\w/]*\.(?:cpp|h))\b")

# Live-reference prefixes that are always fine even when the bare basename
# search below is ambiguous.
FINDINGS: list[str] = []


def _file_exists_in_tree(cite: str) -> bool:
    p = REPO / cite
    if p.exists():
        return True
    base = pathlib.Path(cite).name
    for root in (REPO / "src", REPO / "include", REPO / "tools", REPO / "tests"):
        if root.exists() and any(root.rglob(base)):
            return True
    return False


def transform(src: Source):
    notes: list[str] = []
    lines = list(src.lines)

    # -- curated surgeries -------------------------------------------------
    for original, replacement in SURGERIES.get(src.path.name, []):
        stripped = [ln.strip() for ln in lines]
        hits = [k for k in range(len(lines) - len(original) + 1)
                if stripped[k:k + len(original)] == original]
        if len(hits) > 1:
            print(f"strip_provenance: ABORT: ambiguous surgery in "
                  f"{src.path.name} at {original[0]!r}", file=sys.stderr)
            sys.exit(2)
        if not hits:
            notes.append(f"surgery not found (already applied?): {original[0]!r}")
            continue
        k = hits[0]
        indent = re.match(r"\s*", lines[k]).group(0)
        lines[k:k + len(original)] = [indent + r for r in replacement]
        notes.append(f"surgery at line {k + 1}: "
                     f"{len(original)} -> {len(replacement)} line(s)")

    # -- dead-cite scan ----------------------------------------------------
    out: list[str] = []
    for idx, ln in enumerate(lines):
        if not ln.lstrip().startswith("--"):
            out.append(ln)
            continue
        dead = [c for c in CITE.findall(ln) if not _file_exists_in_tree(c)]
        if not dead:
            out.append(ln)
            continue
        body = ln.lstrip().lstrip("-").strip()
        only_cite = re.fullmatch(
            r"(?:behavior hooks )?(?:transliterated from )?"
            + "|".join(re.escape(c) for c in dead) + r"[.,]?", body)
        if only_cite:
            notes.append(f"line {idx + 1}: deleted whole-line dead cite {dead}")
            continue
        FINDINGS.append(f"{src.path.name}:{idx + 1}: dead cite {dead} "
                        f"embedded in comment — needs a curated surgery")
        out.append(ln)

    if out == list(src.lines):
        return None, notes
    return out, notes


if __name__ == "__main__":
    run_rewriter("strip_provenance",
                 "dead provenance + stale guard-wrapper comments (S3)",
                 transform)
    if FINDINGS:
        print("strip_provenance: FINDINGS (unhandled dead cites):",
              file=sys.stderr)
        for f in FINDINGS:
            print(f"  {f}", file=sys.stderr)
        sys.exit(1)
