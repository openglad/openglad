#!/usr/bin/env python3
"""Structurally verify the generated media in docs/media/lua-classpacks.

Decodes every shipped file with the independent decoders the media tests use
(no PIL, no ffmpeg): each GIF is LZW-decoded frame by frame and played back
through the same compositing a viewer does, each PNG is inflated and its
scanline filters checked, each BMP is parsed as an 8-bit indexed image. A file
that decodes is a file that opens.

Run: python3 scripts/media/verify_media.py [dir]
Exits non-zero if a file is missing, malformed, or the wrong shape.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bmp2gif import read_indexed_bmp  # noqa: E402
from test_bmp2gif import composite, decode_gif  # noqa: E402
from test_bmp2png import decode_indexed_png  # noqa: E402

# name -> (width, height, frame count). One frame means a still.
EXPECTED = {
    "ninefold-court.gif": (640, 400, 67),
    "ninefold-court-judgment.gif": (640, 400, 50),
    "ninefold-court-pillars.png": (640, 400, 1),
    "ninefold-court-wards-fail.png": (640, 400, 1),
    "ninefold-court-judgment.png": (640, 400, 1),
    # The raw capture, at the game's own 320x200 canvas (everything else is
    # the 2x nearest-neighbour upscale that makes the 4x6 font legible).
    "ninefold-court-judgment.bmp": (320, 200, 1),
    "demo-grid.png": (640, 400, 1),
}


def check_gif(path: str, want_w: int, want_h: int, want_frames: int) -> str:
    width, height, palette, frames = decode_gif(path)
    if (width, height) != (want_w, want_h):
        raise AssertionError(f"{path}: {width}x{height}, expected {want_w}x{want_h}")
    if len(frames) != want_frames:
        raise AssertionError(
            f"{path}: {len(frames)} frames, expected {want_frames}")
    if len(palette) != 256:
        raise AssertionError(f"{path}: {len(palette)} palette entries")
    played = composite(width, height, frames)
    if len(played) != want_frames:
        raise AssertionError(f"{path}: composited {len(played)} frames")
    for index, canvas in enumerate(played):
        if len(canvas) != height or any(len(row) != width for row in canvas):
            raise AssertionError(f"{path}: frame {index} is ragged")
    return f"{want_frames} frames {width}x{height}"


def check_png(path: str, want_w: int, want_h: int) -> str:
    width, height, palette, rows = decode_indexed_png(path)
    if (width, height) != (want_w, want_h):
        raise AssertionError(f"{path}: {width}x{height}, expected {want_w}x{want_h}")
    if len(rows) != height:
        raise AssertionError(f"{path}: {len(rows)} rows, expected {height}")
    if not palette:
        raise AssertionError(f"{path}: no palette")
    return f"still {width}x{height}, {len(palette)} colours"


def check_bmp(path: str, want_w: int, want_h: int) -> str:
    frame = read_indexed_bmp(path)
    if (frame.width, frame.height) != (want_w, want_h):
        raise AssertionError(
            f"{path}: {frame.width}x{frame.height}, expected {want_w}x{want_h}")
    return f"indexed BMP {frame.width}x{frame.height}"


def main(argv: list[str]) -> int:
    root = argv[0] if argv else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..",
        "docs", "media", "lua-classpacks")
    root = os.path.abspath(root)
    print(f"verifying media in {root}")

    failures = 0
    total = 0
    for name, (width, height, frame_count) in EXPECTED.items():
        path = os.path.join(root, name)
        try:
            if not os.path.exists(path):
                raise AssertionError(f"{path}: missing")
            if name.endswith(".gif"):
                detail = check_gif(path, width, height, frame_count)
            elif name.endswith(".png"):
                detail = check_png(path, width, height)
            else:
                detail = check_bmp(path, width, height)
        except AssertionError as error:
            print(f"  FAIL {name}: {error}")
            failures += 1
            continue
        size = os.path.getsize(path)
        total += size
        print(f"  ok {name}: {detail}, {size} bytes")

    extra = sorted(f for f in os.listdir(root)
                   if f not in EXPECTED and f != "README.md")
    if extra:
        print(f"  note: unlisted files present: {', '.join(extra)}")

    print(f"{len(EXPECTED) - failures}/{len(EXPECTED)} files verified, "
          f"{total} bytes total")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
