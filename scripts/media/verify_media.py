#!/usr/bin/env python3
"""Structurally verify a set of generated media.

Sets:
  lua-classpacks  (the default) the showcase media
                  scripts/media/capture_showcase.sh writes, in
                  build/media/lua-classpacks by default
  ability-select  the New Ability Select stills
                  scripts/media/capture_ability_select.sh writes, in
                  build/media/ability-select by default

Pass a directory to check a checkout of the openglad/openglad-screenshots
repo instead.

Runs ffprobe (from the dev shell: `nix develop`) over every shipped file with
frame counting on, which decodes each file end to end: a GIF whose frames all
decode, at the right size, count and total duration, is a GIF that plays. The
PNGs and the raw BMP are additionally pinned to pal8 — the captures are
indexed-colour and must stay that way through the pipeline.

Run: python3 scripts/media/verify_media.py [--set NAME]
         [--allow-missing GROUP]... [dir]
--allow-missing lets one group of a set be absent (the capture script
passes the steps it did not run); a file that IS there is still checked.
Exits non-zero if a file is missing, malformed, or the wrong shape.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys

# name -> (codec, width, height, frame count, duration seconds or None,
#          pixel format or None). One frame means a still.
LUA_CLASSPACKS = {
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

# The New Ability Select stills. Every canvas shot is the 320x200 game canvas
# doubled; the pixel format is whatever the lossless PPM -> PNG conversion
# picked, so it is not pinned.
CANVAS_STILL = ("png", 640, 400, 1, None, None)
ABILITY_SELECT = {
    "hud": {f"hud-{scene}.png": CANVAS_STILL for scene in (
        "1p-on", "1p-off", "2p-on", "2p-off", "4p-on", "4p-off",
        "1p-on-shift", "4p-on-shift")},
    "hud-radar": {f"hud-radar-{scene}.png": CANVAS_STILL for scene in (
        "2p-on", "2p-on-shift", "4p-on", "4p-on-shift")},
    "radar": {"hud-2p-radar-on.png": CANVAS_STILL},
    "abilities": {f"abilities-{shot}.png": CANVAS_STILL for shot in (
        "archmage", "archmage-selected", "archmage-placed", "mage",
        "train-door-on", "train-door-off")},
    # The key panels (Base Camp seat, PAUSED player screen) on and off, the
    # REMAP prompt, and a dimmed button elsewhere (the main menu's note).
    "panels": {f"{shot}.png": CANVAS_STILL for shot in (
        "basecamp-keys-on", "basecamp-keys-off", "pause-keys-on",
        "pause-keys-off", "remap-p1-spec2", "dimmed-no-company")},
    # The committed keyboard pictures, copied as they are.
    "keyboard": {f"{name}.png": ("png", 1560, 550, 1, None, None)
                 for name in ("keyboard_belt", "keyboard_belt_web")},
    "ui-fx": {"ui-fx.png": CANVAS_STILL},
    # The CHANGELOG! page at the window's real size, never the canvas.
    "changelog": {f"changelog-{w}x{h}.png": ("png", w, h, 1, None, None)
                  for w, h in ((640, 400), (1280, 800), (1920, 1080))},
}
# set name -> (default directory under build/media, {group: {name: spec}})
SETS = {
    "lua-classpacks": ("lua-classpacks", {"": LUA_CLASSPACKS}),
    "ability-select": ("ability-select", ABILITY_SELECT),
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
    parser = argparse.ArgumentParser(
        description="Structurally verify a set of generated media.")
    parser.add_argument("--set", dest="media_set", default="lua-classpacks",
                        choices=sorted(SETS))
    parser.add_argument("--allow-missing", action="append", default=[],
                        metavar="GROUP",
                        help="a group of the set that may be absent")
    parser.add_argument("dir", nargs="?")
    args = parser.parse_args(argv)

    default_dir, groups = SETS[args.media_set]
    unknown = [g for g in args.allow_missing if g not in groups]
    if unknown:
        print(f"no such group in {args.media_set}: {', '.join(unknown)}",
              file=sys.stderr)
        return 2
    if shutil.which("ffprobe") is None:
        print("ffprobe not found; enter the dev shell first: nix develop",
              file=sys.stderr)
        return 1
    root = args.dir or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..",
        "build", "media", default_dir)
    root = os.path.abspath(root)
    print(f"verifying {args.media_set} media in {root}")
    if not os.path.isdir(root):
        print(f"  FAIL: {root} is not a directory")
        return 1

    failures = 0
    checked = 0
    expected_total = 0
    total = 0
    known: set[str] = set()
    present = sorted(os.listdir(root))
    for group, expected in groups.items():
        may_miss = group in args.allow_missing
        for name, expectation in expected.items():
            known.add(name)
            path = os.path.join(root, name)
            if not os.path.exists(path) and may_miss:
                print(f"  skip {name}: group {group} allowed missing")
                continue
            expected_total += 1
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
            checked += 1
            print(f"  ok {name}: {detail}, {size} bytes")

    extra = [f for f in present if f not in known and f != "README.md"]
    if extra:
        print(f"  note: unlisted files present: {', '.join(extra)}")

    print(f"{checked}/{expected_total} files verified, {total} bytes total")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
