#!/usr/bin/env python3
"""Round-trip test for bmp2png.py: synthesize indexed BMPs, convert to PNG,
then decode the PNG back with an independent reader and compare every pixel.

Run: python3 scripts/media/test_bmp2png.py
Exits non-zero on any mismatch. No third-party dependencies.
"""

from __future__ import annotations

import os
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmp2png  # noqa: E402
from test_bmp2gif import make_palette, write_indexed_bmp  # noqa: E402


def decode_indexed_png(path: str):
    """Minimal PNG reader for colour-type 3, bit depth 8, filter 0 only.

    Written from the spec rather than sharing code with the writer, so a
    writer bug cannot cancel itself out.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise AssertionError("bad PNG signature")

    pos = 8
    width = height = 0
    palette: list[tuple[int, int, int]] = []
    idat = bytearray()
    seen_iend = False
    while pos < len(data):
        length, tag = struct.unpack_from(">I4s", data, pos)
        payload = data[pos + 8:pos + 8 + length]
        stored_crc = struct.unpack_from(">I", data, pos + 8 + length)[0]
        if zlib.crc32(tag + payload) & 0xFFFFFFFF != stored_crc:
            raise AssertionError(f"CRC mismatch on {tag!r} chunk")
        pos += 12 + length

        if tag == b"IHDR":
            width, height, depth, colour, comp, filt, interlace = \
                struct.unpack(">IIBBBBB", payload)
            if (depth, colour, comp, filt, interlace) != (8, 3, 0, 0, 0):
                raise AssertionError(
                    f"unexpected IHDR {(depth, colour, comp, filt, interlace)}")
        elif tag == b"PLTE":
            palette = [tuple(payload[i:i + 3]) for i in range(0, len(payload), 3)]
        elif tag == b"IDAT":
            idat.extend(payload)
        elif tag == b"IEND":
            seen_iend = True
            break
    if not seen_iend:
        raise AssertionError("no IEND chunk")

    raw = zlib.decompress(bytes(idat))
    stride = width + 1
    if len(raw) != stride * height:
        raise AssertionError(f"decompressed {len(raw)} bytes, expected {stride * height}")
    rows = []
    for y in range(height):
        line = raw[y * stride:(y + 1) * stride]
        if line[0] != 0:
            raise AssertionError(f"row {y} uses filter {line[0]}, expected 0")
        rows.append(bytes(line[1:]))
    return width, height, palette, rows


def case_roundtrip(tmp: str, width: int, height: int, bottom_up: bool,
                   scale: int, label: str) -> None:
    palette = make_palette()
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            row.append(1 if y == x % height else (x * 7 + y * 13) % 256)
        rows.append(bytes(row))

    bmp_path = os.path.join(tmp, f"{label}.bmp")
    png_path = os.path.join(tmp, f"{label}.png")
    write_indexed_bmp(bmp_path, width, height, rows, palette, bottom_up=bottom_up)
    assert bmp2png.main([bmp_path, png_path, "--scale", str(scale)]) == 0

    pw, ph, ppal, prows = decode_indexed_png(png_path)
    assert (pw, ph) == (width * scale, height * scale), \
        f"{label}: png is {pw}x{ph}, expected {width * scale}x{height * scale}"
    assert ppal[1] == (255, 0, 0) and ppal[3] == (0, 0, 255), \
        f"{label}: palette not preserved: {ppal[1]} {ppal[3]}"
    for y in range(ph):
        want = rows[y // scale]
        if scale > 1:
            want = bytes(v for v in want for _ in range(scale))
        assert prows[y] == want, f"{label}: row {y} mismatch"
    print(f"  ok {label}: {pw}x{ph}, pixels identical")


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        print("bmp2png round-trip tests")
        case_roundtrip(tmp, 37, 21, True, 1, "odd_bottom_up")
        case_roundtrip(tmp, 32, 16, False, 1, "top_down")
        case_roundtrip(tmp, 20, 12, True, 3, "scaled")
        case_roundtrip(tmp, 320, 200, True, 1, "full_screen")
        print("all bmp2png round-trip tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
