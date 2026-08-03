#!/usr/bin/env bash
# Regenerate the lua-classpacks showcase media from a headless openglad_demo
# run. Output lands in build/media/ (gitignored); finished artifacts get
# committed to the openglad/openglad-screenshots repo, never to this one.
#
# Two capture runs, both fully seeded, so the frame numbers picked below stay
# on the moments they were picked for:
#   1. one session on "The Ninefold Court" (concept scenario 605), the
#      campaign-embedded Lua level script, camera pinned to the arena;
#   2. a 2x2 grid of stock campaign levels, dumped as one composited image.
#
# The demo runs on SDL's dummy video driver (no display needed) and writes
# 8-bit indexed BMPs. ffmpeg — provided by the dev shell (`nix develop`, see
# flake.nix) — turns those into the shipped media: GIFs via the two-pass
# palettegen/paletteuse chain, stills as indexed PNGs.
#
# Usage: scripts/media/capture_showcase.sh [output-dir]
#        OPENGLAD_DEMO=/path/to/openglad_demo scripts/media/capture_showcase.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO_BIN="${OPENGLAD_DEMO:-$REPO_ROOT/build/ci-test/openglad_demo}"
OUT_DIR="${1:-$REPO_ROOT/build/media/lua-classpacks}"
MEDIA="$REPO_ROOT/scripts/media"

if [ ! -x "$DEMO_BIN" ]; then
    printf 'openglad_demo not found at %s\n' "$DEMO_BIN" >&2
    printf 'build it with: cmake --build --preset ci-test --target openglad_demo\n' >&2
    exit 1
fi
for tool in ffmpeg ffprobe; do
    if ! command -v "$tool" > /dev/null; then
        printf '%s not found; enter the dev shell first: nix develop\n' "$tool" >&2
        exit 1
    fi
done

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

FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# The GIFs are cut from the BMP dumps with the concat demuxer: one list line
# per animation frame, each with an explicit on-screen duration. A moment that
# should linger is not a repeated frame, just a longer duration on one line —
# GIF stores per-frame delays natively.
emit() { # emit <list> <dir> <duration-secs> <first> <last> <step>
    local list=$1 dir=$2 dur=$3 first=$4 last=$5 step=$6 i
    for ((i = first; i <= last; i += step)); do
        printf "file '%s/frame%05d.bmp'\nduration %s\n" "$dir" "$i" "$dur" >> "$list"
    done
}

hold() { # hold <list> <dir> <frame> <duration-secs>
    printf "file '%s/frame%05d.bmp'\nduration %s\n" "$2" "$3" "$4" >> "$1"
}

# The judgment explosions are only on screen for three simulation frames
# (299-301), a quarter of a second. Holding those frames keeps the ring on
# screen long enough to read.
pulse() { # pulse <list> <dir> <dur-x2> <dur-x3> <dur-x5>
    hold "$1" "$2" 299 "$3"
    hold "$1" "$2" 300 "$4"
    hold "$1" "$2" 301 "$5"
}

# Two passes: palettegen collects the exact colours the frames use (indexed
# game frames have at most 256, so nothing is quantized), paletteuse maps every
# frame through that palette with no dithering. The 2x nearest-neighbour
# upscale is what makes the 4x6 font legible. ffmpeg's GIF encoder then stores
# only each frame's changed region (offsetting + transparency), which is what
# keeps a 640x400 animation inside a few hundred KB.
encode_gif() { # encode_gif <list> <out.gif> <final-delay-cs>
    local list=$1 out=$2 final=$3 palette="$WORK_DIR/palette.png"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,palettegen' "$palette"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" -i "$palette" \
        -lavfi '[0:v]scale=iw*2:ih*2:flags=neighbor[x];[x][1:v]paletteuse=dither=none' \
        -final_delay "$final" "$out"
    rm -f -- "$palette"
}

# Stills stay indexed too: palettegen/paletteuse on the single frame hands the
# PNG encoder a pal8 picture instead of 32-bit RGB.
still2x() { # still2x <in.bmp> <out.png>
    "${FFMPEG[@]}" -i "$1" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse=dither=none' \
        -frames:v 1 -update 1 -compression_level 9 "$2"
}

# --- Animations -------------------------------------------------------------
# The fight from the opening bell to the first judgment, at roughly 4x speed
# (every 6th tick for 12 centiseconds against a 12.25 tick/s simulation).
list="$WORK_DIR/court.ffconcat"
: > "$list"
emit "$list" "$court" 0.12 0 288 6
emit "$list" "$court" 0.12 292 298 3
pulse "$list" "$court" 0.24 0.36 0.60
emit "$list" "$court" 0.12 304 328 6
encode_gif "$list" "$OUT_DIR/ninefold-court.gif" 12

# The judgment pulse itself, tick for tick.
list="$WORK_DIR/judgment.ffconcat"
: > "$list"
emit "$list" "$court" 0.08 288 298 1
pulse "$list" "$court" 0.16 0.24 0.40
emit "$list" "$court" 0.08 302 330 1
encode_gif "$list" "$OUT_DIR/ninefold-court-judgment.gif" 8

# --- Stills -----------------------------------------------------------------
still2x "$court/frame00024.bmp" "$OUT_DIR/ninefold-court-pillars.png"
still2x "$court/frame00121.bmp" "$OUT_DIR/ninefold-court-wards-fail.png"
still2x "$court/frame00301.bmp" "$OUT_DIR/ninefold-court-judgment.png"

# The grid composite is dumped at its final size, and the BMP is already
# indexed, so it converts to PNG directly.
"${FFMPEG[@]}" -i "$grid/frame00008.bmp" \
    -frames:v 1 -update 1 -compression_level 9 "$OUT_DIR/demo-grid.png"

# One raw frame ships as-is so the BMP the game actually writes is in the
# record next to everything derived from it.
cp "$court/frame00301.bmp" "$OUT_DIR/ninefold-court-judgment.bmp"

python3 "$MEDIA/verify_media.py" "$OUT_DIR"
ls -l "$OUT_DIR"
