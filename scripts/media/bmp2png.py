#!/usr/bin/env python3
"""Convert indexed BMP frames to indexed PNG stills -- no third-party deps.

Companion to bmp2gif.py. PNG's colour-type 3 is the same palette-plus-indices
shape the game already renders in, so the conversion is a re-container: reuse
the BMP palette as PLTE and deflate the index rows. zlib is stdlib, which is
the whole reason this exists (there is no PIL or ffmpeg in this environment).

Usage:
    bmp2png.py in.bmp out.png
    bmp2png.py --scale 2 in.bmp out.png
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bmp2gif import read_indexed_bmp, scale_rows  # noqa: E402


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_indexed_png(path: str, width: int, height: int,
                      palette: list[tuple[int, int, int]],
                      rows: list[bytes]) -> None:
    # Filter type 0 (None) per scanline: the rows are palette indices, where
    # the usual predictors mostly hurt. Deflate at max level instead.
    raw = b"".join(b"\x00" + row for row in rows)
    body = b"".join((
        b"\x89PNG\r\n\x1a\n",
        _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)),
        _chunk(b"PLTE", b"".join(bytes(c) for c in palette[:256])),
        _chunk(b"IDAT", zlib.compress(raw, 9)),
        _chunk(b"IEND", b""),
    ))
    with open(path, "wb") as out:
        out.write(body)


def convert(src: str, dst: str, scale: int = 1) -> tuple[int, int]:
    frame = read_indexed_bmp(src)
    rows = scale_rows(frame.rows, scale)
    width = frame.width * scale
    height = frame.height * scale
    write_indexed_png(dst, width, height, frame.palette, rows)
    return width, height


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("output")
    parser.add_argument("--scale", type=int, default=1)
    args = parser.parse_args(argv)

    width, height = convert(args.source, args.output, max(1, args.scale))
    print(f"{args.output}: {width}x{height}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
