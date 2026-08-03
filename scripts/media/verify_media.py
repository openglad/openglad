#!/usr/bin/env python3
"""Structurally verify the generated lua-classpacks showcase media
(build/media/lua-classpacks by default; pass a directory to check a
checkout of the openglad/openglad-screenshots repo instead).

Runs ffprobe (from the dev shell: `nix develop`) over every shipped file with
frame counting on, which decodes each file end to end: a GIF whose frames all
decode, at the right size, count and total duration, is a GIF that plays. The
PNGs and the raw BMP are additionally pinned to pal8 — the captures are
indexed-colour and must stay that way through the pipeline.

Run: python3 scripts/media/verify_media.py [dir]
Exits non-zero if a file is missing, malformed, or the wrong shape.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys

# name -> (codec, width, height, frame count, duration seconds or None,
#          pixel format or None). One frame means a still.
EXPECTED = {
    "ninefold-court.gif": ("gif", 640, 400, 60, 8.04, None),
    "ninefold-court-judgment.gif": ("gif", 640, 400, 43, 4.00, None),
    "ninefold-court-pillars.png": ("png", 640, 400, 1, None, "pal8"),
    "ninefold-court-wards-fail.png": ("png", 640, 400, 1, None, "pal8"),
    "ninefold-court-judgment.png": ("png", 640, 400, 1, None, "pal8"),
    # The raw capture, at the game's own 320x200 canvas (everything else is
    # the 2x nearest-neighbour upscale that makes the 4x6 font legible).
    "ninefold-court-judgment.bmp": ("bmp", 320, 200, 1, None, "pal8"),
    "demo-grid.png": ("png", 640, 400, 1, None, "pal8"),
}


def probe(path: str) -> dict:
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
         "-show_entries",
         "stream=codec_name,width,height,pix_fmt,nb_read_frames,duration",
         "-of", "json", path],
        check=True, capture_output=True, text=True).stdout
    streams = json.loads(out).get("streams", [])
    if len(streams) != 1:
        raise AssertionError(f"{path}: {len(streams)} video streams")
    return streams[0]


def check(path: str, codec: str, want_w: int, want_h: int, want_frames: int,
          want_duration: float | None, want_pix_fmt: str | None) -> str:
    stream = probe(path)
    if stream.get("codec_name") != codec:
        raise AssertionError(f"{path}: codec {stream.get('codec_name')}, "
                             f"expected {codec}")
    size = (stream.get("width"), stream.get("height"))
    if size != (want_w, want_h):
        raise AssertionError(f"{path}: {size[0]}x{size[1]}, "
                             f"expected {want_w}x{want_h}")
    frames = int(stream.get("nb_read_frames", "0"))
    if frames != want_frames:
        raise AssertionError(f"{path}: {frames} frames, expected {want_frames}")
    if want_duration is not None:
        duration = float(stream.get("duration", "0"))
        if abs(duration - want_duration) > 0.005:
            raise AssertionError(f"{path}: runs {duration}s, "
                                 f"expected {want_duration}s")
    if want_pix_fmt is not None and stream.get("pix_fmt") != want_pix_fmt:
        raise AssertionError(f"{path}: pix_fmt {stream.get('pix_fmt')}, "
                             f"expected {want_pix_fmt}")
    if want_frames > 1:
        return f"{frames} frames {want_w}x{want_h}, {want_duration}s"
    return f"still {want_w}x{want_h} {stream.get('pix_fmt')}"


def main(argv: list[str]) -> int:
    if shutil.which("ffprobe") is None:
        print("ffprobe not found; enter the dev shell first: nix develop",
              file=sys.stderr)
        return 1
    root = argv[0] if argv else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..",
        "build", "media", "lua-classpacks")
    root = os.path.abspath(root)
    print(f"verifying media in {root}")

    failures = 0
    total = 0
    for name, expectation in EXPECTED.items():
        path = os.path.join(root, name)
        try:
            if not os.path.exists(path):
                raise AssertionError(f"{path}: missing")
            detail = check(path, *expectation)
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
