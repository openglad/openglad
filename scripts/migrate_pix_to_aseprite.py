#!/usr/bin/env python3
"""Regenerate the GIMP palette artifact (``pix/openglad.gpl``) from the engine
palette literal in ``src/resources/our_palette.cpp``.

Byte-deterministic across reruns, so the artifact artists load in Aseprite/GIMP
stays in sync with the palette the engine actually renders with.

    python3 scripts/migrate_pix_to_aseprite.py --emit-gpl pix/openglad.gpl
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PALETTE_SRC = REPO_ROOT / "src" / "resources" / "our_palette.cpp"


# ---------------------------------------------------------------------------
# Palette loading
# ---------------------------------------------------------------------------

def load_palette_6bit() -> bytes:
    """Parse the 768-byte 6-bit VGA palette literal out of our_palette.cpp."""
    text = PALETTE_SRC.read_text(encoding="utf-8")
    # The literal has been both a C array and a std::array over the years;
    # accept either spelling so a modernization sweep cannot silently break
    # palette regeneration.
    m = re.search(
        r"static\s+const\s+"
        r"(?:unsigned\s+char\s+data\s*\[\s*\]|std::array\s*<[^>]*>\s+data)"
        r"\s*=\s*\{",
        text)
    if not m:
        raise RuntimeError(f"could not locate palette literal in {PALETTE_SRC}")
    start = m.end()
    end = text.find("};", start)
    if end < 0:
        raise RuntimeError(f"could not locate end of palette literal in {PALETTE_SRC}")
    body = text[start:end]
    nums = [int(t) for t in re.findall(r"-?\d+", body)]
    if len(nums) != 768:
        raise RuntimeError(
            f"expected 768 palette bytes in {PALETTE_SRC}, got {len(nums)}")
    for n in nums:
        if n < 0 or n > 63:
            raise RuntimeError(f"palette byte out of 6-bit range: {n}")
    return bytes(nums)


def palette_8bit_rgb(pal_6bit: bytes) -> bytes:
    """Convert 256x3 6-bit values to 256x3 8-bit values via (v*255)//63."""
    out = bytearray(768)
    for i, v in enumerate(pal_6bit):
        out[i] = (v * 255) // 63
    return bytes(out)


# ---------------------------------------------------------------------------
# GIMP palette (.gpl) emission
# ---------------------------------------------------------------------------

def format_gpl(palette_rgb: bytes) -> str:
    """Render a 256-entry GIMP palette from an 8-bit RGB triplet stream.

    Format: ``GIMP Palette`` header, ``Name:`` and ``Columns:`` lines, a
    ``#`` comment marker, then 256 lines of ``%3d %3d %3d\\tcolor_NNN``. The
    layout is fixed so the artifact is byte-deterministic across reruns.
    """
    if len(palette_rgb) != 768:
        raise RuntimeError("palette must be 256*3 bytes")
    lines = ["GIMP Palette", "Name: OpenGlad", "Columns: 16", "#"]
    for i in range(256):
        r = palette_rgb[i * 3 + 0]
        g = palette_rgb[i * 3 + 1]
        b = palette_rgb[i * 3 + 2]
        lines.append(f"{r:3d} {g:3d} {b:3d}\tcolor_{i:03d}")
    return "\n".join(lines) + "\n"


def emit_gpl(out_path: Path) -> None:
    palette_6 = load_palette_6bit()
    palette_rgb = palette_8bit_rgb(palette_6)
    out_path.write_text(format_gpl(palette_rgb), encoding="utf-8")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--emit-gpl",
        metavar="PATH",
        type=Path,
        default=None,
        help="Regenerate the GIMP palette from our_palette.cpp and exit.",
    )
    args = parser.parse_args(argv)

    if args.emit_gpl is None:
        parser.error("--emit-gpl PATH is required")

    emit_gpl(args.emit_gpl)
    print(f"[gpl] wrote {args.emit_gpl}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
