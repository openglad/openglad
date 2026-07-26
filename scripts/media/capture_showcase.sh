#!/usr/bin/env bash
# Regenerate docs/media/lua-classpacks from a headless openglad_demo run.
#
# Two capture runs, both fully seeded, so the frame numbers picked below stay
# on the moments they were picked for:
#   1. one session on "The Ninefold Court" (concept scenario 605), the
#      campaign-embedded Lua level script, camera pinned to the arena;
#   2. a 2x2 grid of stock campaign levels, dumped as one composited image.
#
# Nothing here needs ffmpeg, PIL or a display: the demo runs on SDL's dummy
# video driver and writes indexed BMPs, which bmp2gif.py/bmp2png.py turn into
# the GIFs and PNGs with nothing but the Python standard library.
#
# Usage: scripts/media/capture_showcase.sh [output-dir]
#        OPENGLAD_DEMO=/path/to/openglad_demo scripts/media/capture_showcase.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO_BIN="${OPENGLAD_DEMO:-$REPO_ROOT/build/ci-test/openglad_demo}"
OUT_DIR="${1:-$REPO_ROOT/docs/media/lua-classpacks}"
MEDIA="$REPO_ROOT/scripts/media"

if [ ! -x "$DEMO_BIN" ]; then
    printf 'openglad_demo not found at %s\n' "$DEMO_BIN" >&2
    printf 'build it with: cmake --build --preset ci-test --target openglad_demo\n' >&2
    exit 1
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "$OUT_DIR"

court="$WORK_DIR/court"
grid="$WORK_DIR/grid"

# --- Run 1: The Ninefold Court, one session, static arena camera -----------
# 700 frames at one dump per rendered frame == the first 700 simulation ticks,
# which covers the wards holding, all four pillars falling, and the first two
# ninefold judgment pulses (the script fires one every 300 ticks).
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_SEED=7 \
    OPENGLAD_DEMO_CAMPAIGN=org.openglad.concept \
    OPENGLAD_DEMO_SCENARIOS=605 \
    OPENGLAD_DEMO_TEAM_SIZE=6 \
    OPENGLAD_DEMO_CAPTURE_DIR="$court" \
    OPENGLAD_DEMO_CAPTURE_FOCUS=center \
    OPENGLAD_DEMO_CAPTURE_LIMIT=700 \
    OPENGLAD_CONFIG_DIR="$WORK_DIR/config-court" \
    "$DEMO_BIN" > "$WORK_DIR/court.log" 2>&1

# --- Run 2: four stock campaign levels at once, composited ------------------
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    OPENGLAD_DEMO_GRID=2x2 \
    OPENGLAD_DEMO_SEED=5 \
    OPENGLAD_DEMO_SCENARIOS=2,5,8,13 \
    OPENGLAD_DEMO_CAPTURE_DIR="$grid" \
    OPENGLAD_DEMO_CAPTURE_SESSION=-1 \
    OPENGLAD_DEMO_CAPTURE_EVERY=25 \
    OPENGLAD_DEMO_CAPTURE_LIMIT=12 \
    OPENGLAD_CONFIG_DIR="$WORK_DIR/config-grid" \
    "$DEMO_BIN" > "$WORK_DIR/grid.log" 2>&1

frames() { # frames <dir> <first> <last> <step>
    local dir=$1 first=$2 last=$3 step=$4 i
    for ((i = first; i <= last; i += step)); do
        printf '%s/frame%05d.bmp ' "$dir" "$i"
    done
}

hold() { # hold <dir> <frame> <repeats>
    local dir=$1 frame=$2 repeats=$3 i
    for ((i = 0; i < repeats; i++)); do
        printf '%s/frame%05d.bmp ' "$dir" "$frame"
    done
}

# The judgment explosions are only on screen for three simulation frames
# (299-301), a quarter of a second. Repeating those frames holds the ring long
# enough to read; a repeated frame costs a few bytes under --diff, because its
# dirty rectangle is empty.
pulse() { # pulse <dir>
    local dir=$1
    hold "$dir" 299 2
    hold "$dir" 300 3
    hold "$dir" 301 5
}

# --- Animations -------------------------------------------------------------
# --diff stores only the changed rectangle of each frame, which is what keeps
# these 640x400 animations inside a few hundred KB each.
#
# The fight from the opening bell to the first judgment, at roughly 4x speed
# (every 6th tick held for 12 centiseconds against a 12.25 tick/s simulation).
python3 "$MEDIA/bmp2gif.py" "$OUT_DIR/ninefold-court.gif" \
    --delay 12 --scale 2 --diff \
    $(frames "$court" 0 288 6) $(frames "$court" 292 298 3) \
    $(pulse "$court") $(frames "$court" 304 328 6)

# The judgment pulse itself, tick for tick.
python3 "$MEDIA/bmp2gif.py" "$OUT_DIR/ninefold-court-judgment.gif" \
    --delay 8 --scale 2 --diff \
    $(frames "$court" 288 298 1) $(pulse "$court") \
    $(frames "$court" 302 330 1)

# --- Stills -----------------------------------------------------------------
python3 "$MEDIA/bmp2png.py" --scale 2 \
    "$court/frame00024.bmp" "$OUT_DIR/ninefold-court-pillars.png"
python3 "$MEDIA/bmp2png.py" --scale 2 \
    "$court/frame00121.bmp" "$OUT_DIR/ninefold-court-wards-fail.png"
python3 "$MEDIA/bmp2png.py" --scale 2 \
    "$court/frame00301.bmp" "$OUT_DIR/ninefold-court-judgment.png"
python3 "$MEDIA/bmp2png.py" \
    "$grid/frame00008.bmp" "$OUT_DIR/demo-grid.png"

# One raw frame ships as-is so the BMP the game actually writes is in the
# record next to everything derived from it.
cp "$court/frame00301.bmp" "$OUT_DIR/ninefold-court-judgment.bmp"

python3 "$MEDIA/verify_media.py" "$OUT_DIR"
ls -l "$OUT_DIR"
