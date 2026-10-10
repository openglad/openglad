#!/usr/bin/env bash
# New Specials proof media: one GIF per new special, plus three of a bot
# using one.
#
# Runs GameLoop.zz_capture_new_specials (tests/integration/test_game_loop.cpp)
# from the repository root with SDL's dummy drivers and a throwaway config
# directory, so a run can never rewrite cfg/openglad.yaml. The test plays each
# scene in the real game with the New Specials setting on, presses the keys a
# player would, and fails if the special does not visibly happen. It writes
# one P6 frame per game tick plus key.txt (the frame that shows the special).
#
# For every scene this script writes, into the output directory:
#   <scene>.gif        2x nearest-neighbour, 12.25 frames a second (game
#                      speed), no dithering, a 1.5 s hold on the last frame
#   <scene>-strip.png  every fourth frame at 1x, ten to a row (for review)
#   <scene>-key.png    the frame that shows the special, at 2x
# A GIF above 3 MB, or shorter than 8 s or longer than 12.5 s, fails the run.
#
# Output lands in build/media/new-specials/ (gitignored); finished media go
# to the openglad/openglad-screenshots repo, never to this one (AGENTS.md,
# "PR screenshots and proof media").
#
# Usage: scripts/media/capture_new_specials.sh [output-dir]
#   OPENGLAD_BUILD=<dir>  build tree (default build/ci-test)
#   ONLY=<scene>          record and encode one scene
#
# Build first:  cmake --build --preset ci-test --target og_test_game_core
# ffmpeg, ffprobe and magick come from the dev shell (`nix develop`).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${OPENGLAD_BUILD:-$REPO_ROOT/build/ci-test}"
TEST_BIN="$BUILD/og_test_game_core"
OUT_DIR="${1:-$REPO_ROOT/build/media/new-specials}"
RAW_DIR="$OUT_DIR/raw"

FPS=12.25
MAX_BYTES=3000000
MIN_SECONDS=8
MAX_SECONDS=12.5

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

# montage measures text even for its frame-number labels, and the dev shell
# has no fontconfig default: name the DejaVu file the dev shell ships.
find_font() {
    if [ -n "${OG_OVERLAY_FONT:-}" ] && [ -f "$OG_OVERLAY_FONT" ]; then
        printf '%s\n' "$OG_OVERLAY_FONT"
        return 0
    fi
    local root hit
    for root in $(printf '%s' "${XDG_DATA_DIRS:-}:$HOME/.nix-profile/share:/usr/local/share:/usr/share" | tr ':' ' '); do
        hit="$(find "$root/fonts" -name 'DejaVuSansMono.ttf' 2> /dev/null | head -1)"
        if [ -n "$hit" ]; then
            printf '%s\n' "$hit"
            return 0
        fi
    done
    return 1
}

[ -x "$TEST_BIN" ] \
    || die "$TEST_BIN not found; build it first:
  cmake --build --preset ci-test --target og_test_game_core"
for tool in ffmpeg ffprobe magick; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done
FONT="$(find_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
mkdir -p "$WORK_DIR/config"

rm -rf "$RAW_DIR"
mkdir -p "$RAW_DIR"

# The test binary reads its data relative to the working directory: run it
# from the repository root.
(
    cd "$REPO_ROOT"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config" \
        OG_FX_CAPTURE_DIR="$RAW_DIR" \
        ${ONLY:+OG_FX_CAPTURE_ONLY="$ONLY"} \
        "$TEST_BIN" --gtest_filter='GameLoop.zz_capture_new_specials'
) || die "the recording failed (a special did not show); nothing encoded"

VF='scale=iw*2:ih*2:flags=neighbor'
encoded=0
for scene_dir in "$RAW_DIR"/*/; do
    scene="$(basename "$scene_dir")"
    gif="$OUT_DIR/$scene.gif"
    palette="$WORK_DIR/$scene-palette.png"

    ffmpeg -hide_banner -loglevel error -y -framerate "$FPS" \
        -i "$scene_dir/%03d.ppm" -vf "$VF,palettegen=stats_mode=diff" \
        "$palette"
    ffmpeg -hide_banner -loglevel error -y -framerate "$FPS" \
        -i "$scene_dir/%03d.ppm" -i "$palette" \
        -lavfi "[0:v]$VF[x];[x][1:v]paletteuse=dither=none" \
        -final_delay 150 -loop 0 "$gif"

    bytes="$(wc -c < "$gif" | tr -d ' ')"
    seconds="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$gif")"
    [ "$bytes" -le "$MAX_BYTES" ] \
        || die "$gif is $bytes bytes, above $MAX_BYTES"
    awk -v d="$seconds" -v lo="$MIN_SECONDS" -v hi="$MAX_SECONDS" \
        'BEGIN { exit !(d >= lo && d <= hi) }' \
        || die "$gif runs ${seconds}s, outside ${MIN_SECONDS}-${MAX_SECONDS}s"

    # Every fourth frame, ten to a row, at the game's own size, each tile
    # labelled with its frame number.
    strip_frames=""
    n=0
    for frame in "$scene_dir"/[0-9][0-9][0-9].ppm; do
        if [ $((n % 4)) -eq 0 ]; then
            strip_frames="$strip_frames $frame"
        fi
        n=$((n + 1))
    done
    # shellcheck disable=SC2086 # one word per frame path
    magick montage -background '#202020' -fill '#d8dee9' -font "$FONT" \
        -pointsize 11 -label '%t' $strip_frames -tile 10x -geometry +2+2 \
        "$OUT_DIR/$scene-strip.png"

    key="$(tr -d ' \n' < "$scene_dir/key.txt")"
    [ "$key" -ge 0 ] 2> /dev/null || die "$scene has no key frame"
    key_ppm="$(printf '%s/%03d.ppm' "$scene_dir" "$key")"
    magick "$key_ppm" -filter point -resize 200% "$OUT_DIR/$scene-key.png"

    printf '%s  %s bytes  %ss  key frame %s\n' "$gif" "$bytes" "$seconds" "$key"
    encoded=$((encoded + 1))
done

[ "$encoded" -gt 0 ] || die "no scene was recorded"
printf 'encoded %d scene(s) into %s\n' "$encoded" "$OUT_DIR"
