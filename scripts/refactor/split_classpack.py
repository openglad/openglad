#!/usr/bin/env python3
"""Split packs/core/classpack.yaml into per-family files (quality plan Stage 4).

One descriptor entry per output file, entry text carried over VERBATIM (byte
identical lines), so the split set parses field-for-field identically to the
monolith. Emits:
  packs/core/families/<order>-<NN>-<slug>.yaml
  a rewritten header-only classpack.yaml
  a pin map (old monolith line -> new file:line) on stdout for
  tests/parity/scenario_table.h re-pointing.

Usage: split_classpack.py <classpack.yaml> <out_families_dir> [--apply]
Dry-run by default: prints the plan + pin map, writes nothing.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ORDERS = ["living", "weapon", "effect", "treasure", "generator"]


def slugify(name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    return slug or "family"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("monolith")
    ap.add_argument("out_dir")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    src_path = pathlib.Path(args.monolith)
    out_dir = pathlib.Path(args.out_dir)
    lines = src_path.read_text().splitlines(keepends=True)

    # Locate structure: root keys at col 0, orders at 2 spaces under
    # families:, entries starting "    - id:" at 4 spaces.
    files: list[tuple[str, list[str], int]] = []  # (filename, lines, src_start_line_1idx)
    header_lines: list[str] = []
    current_order: str | None = None
    entry: list[str] | None = None
    entry_start = 0
    entry_meta: dict[str, str] = {}
    in_families = False

    def flush_entry() -> None:
        nonlocal entry
        if entry is None:
            return
        wire = entry_meta.get("wire_id", "")
        name = entry_meta.get("name", entry_meta.get("id", "x"))
        if not wire.isdigit():
            sys.exit(f"entry at line {entry_start}: non-numeric wire_id {wire!r}")
        fname = f"{current_order}-{int(wire):02d}-{slugify(name)}.yaml"
        files.append((fname, entry.copy(), entry_start))
        entry = None
        entry_meta.clear()

    for idx, line in enumerate(lines, start=1):
        stripped = line.rstrip("\n")
        if stripped.startswith("families:"):
            in_families = True
            flush_entry()
            continue
        if in_families and re.match(r"^[a-z_]+:", stripped):
            # A new root key after families: none exists in the monolith
            # (families is last), but stay correct if one appears.
            flush_entry()
            in_families = False
        if not in_families:
            header_lines.append(line)
            continue
        m = re.match(r"^  ([a-z_]+):\s*$", stripped)
        if m and m.group(1) in ORDERS:
            flush_entry()
            current_order = m.group(1)
            continue
        if re.match(r"^    - id:", stripped):
            flush_entry()
            entry = []
            entry_start = idx
        if entry is not None:
            entry.append(line)
            m = re.match(r"^      wire_id:\s*(\S+)\s*$", stripped)
            if m:
                entry_meta["wire_id"] = m.group(1)
            m = re.match(r'^      name:\s*"?([^"]*)"?\s*$', stripped)
            if m and "name" not in entry_meta:
                entry_meta["name"] = m.group(1)
            m = re.match(r"^    - id:\s*(\S+)\s*$", stripped)
            if m:
                entry_meta["id"] = m.group(1)
        else:
            sys.exit(f"line {idx}: content outside any entry inside families: {stripped!r}")
    flush_entry()

    seen: set[str] = set()
    for fname, _, _ in files:
        if fname in seen:
            sys.exit(f"duplicate output filename {fname}")
        seen.add(fname)

    # Pin map: old line -> (file, new line). New line = position inside the
    # emitted file: 2 preamble lines (families: + order:) + offset, +1 for
    # the per-file header comment line.
    pin_map: dict[int, tuple[str, int]] = {}
    for fname, entry_lines, start in files:
        for off in range(len(entry_lines)):
            pin_map[start + off] = (fname, 3 + off + 1)

    print(f"{len(files)} family files from {src_path}")
    for fname, entry_lines, start in files:
        print(f"  {fname:44s} {len(entry_lines):3d} lines  (monolith {start})")
    print("PINMAP-BEGIN")
    for old, (fname, new) in sorted(pin_map.items()):
        print(f"{old} packs/core/families/{fname} {new}")
    print("PINMAP-END")

    if not args.apply:
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    order_of: dict[str, str] = {}
    for fname, entry_lines, _ in files:
        order = fname.split("-", 1)[0]
        fam_id = ""
        for ln in entry_lines:
            m = re.match(r"^    - id:\s*(\S+)\s*$", ln.rstrip("\n"))
            if m:
                fam_id = m.group(1)
                break
        text = (
            f"# {fam_id} — split from classpack.yaml (families load in sorted filename order)\n"
            + "families:\n"
            + f"  {order}:\n"
            + "".join(entry_lines)
        )
        (out_dir / fname).write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
