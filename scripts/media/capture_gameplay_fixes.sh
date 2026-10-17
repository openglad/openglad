#!/usr/bin/env bash
# Gameplay Fixes proof media: one GIF per fix (fixes A to M of
# docs/gameplay-fixes.md), each the same scene played twice from the same
# seed and the same keys, the setting OFF on the left and ON on the right.
#
# Runs GameLoop.zz_capture_gameplay_fixes (tests/integration/test_game_loop.cpp)
# from the repository root with SDL's dummy drivers and a throwaway config
# directory, so a run can never rewrite cfg/openglad.yaml. The test plays each
# scene in the real game twice, with the Gameplay Fixes setting off and on
# (New Specials off in both, so every Shift is the classic one), presses the
# keys a player would, and fails if the classic bug does not show on the left,
# if the fix does not show on the right, or if the two runs differ on screen
# before the fix parts them. It writes one P6 frame per game tick into
# <clip>-off/ and <clip>-on/ plus key.txt (the frame of the still).
#
# For every clip this script writes, into the output directory:
#   <clip>.gif            both runs at 2x nearest-neighbour side by side with
#                         a 4 px gutter and a label band (SETTING OFF /
#                         SETTING ON, a frame counter), 12.25 frames a second
#                         (game speed), no dithering, a 1.5 s hold at the end
#   <clip>-off-strip.png  every fourth frame of that run at 1x, ten to a row
#   <clip>-on-strip.png   (for review)
#   <clip>-key.png        the still, both runs side by side at 2x
# A GIF above 3 MB, or shorter than 8 s or longer than 12.5 s, fails the run,
# as does a pair of runs with different frame counts.
#
# Output lands in build/media/gameplay-fixes/ (gitignored); finished media go
# to the openglad/openglad-screenshots repo, never to this one (AGENTS.md,
# "PR screenshots and proof media").
#
# Usage: scripts/media/capture_gameplay_fixes.sh [output-dir]
#   OPENGLAD_BUILD=<dir>  build tree (default build/ci-test)
#   ONLY=<clip>           record and encode one clip (both runs)
#   OG_OVERLAY_FONT=<ttf> the label font (default: DejaVuSansMono.ttf found
#                         under XDG_DATA_DIRS)
#
# Build first:  cmake --build --preset ci-test --target og_test_game_core
# ffmpeg, ffprobe and magick come from the dev shell (`nix develop`).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${OPENGLAD_BUILD:-$REPO_ROOT/build/ci-test}"
TEST_BIN="$BUILD/og_test_game_core"
OUT_DIR="${1:-$REPO_ROOT/build/media/gameplay-fixes}"
RAW_DIR="$OUT_DIR/raw"

FPS=12.25
SCALE=2
BAND=28          # the label band, 14 px at 1x
GUTTER=4         # between the halves, 2 px at 1x
MAX_BYTES=3000000
MIN_SECONDS=8
MAX_SECONDS=12.5
BG='#202020'
BG_FF='0x202020'

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

# montage and drawtext measure text, and the dev shell has no fontconfig
# default: name the DejaVu file the dev shell ships.
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
        "$TEST_BIN" --gtest_filter='GameLoop.zz_capture_gameplay_fixes'
) || die "the recording failed (a fix did not show, or the halves parted early); nothing encoded"

count_frames() {
    find "$1" -maxdepth 1 -name '[0-9][0-9][0-9].ppm' | wc -l | tr -d ' '
}

# The first frame index whose two PPMs differ, or -1.
first_diff() {
    local off="$1" on="$2" n="$3" i
    for ((i = 0; i < n; i++)); do
        local name
        name="$(printf '%03d.ppm' "$i")"
        if ! cmp -s "$off/$name" "$on/$name"; then
            printf '%d\n' "$i"
            return 0
        fi
    done
    printf '%d\n' -1
}

# One run's review strip: every fourth frame, ten to a row, labelled.
strip() {
    local dir="$1" out="$2" frames="" n=0 frame
    for frame in "$dir"/[0-9][0-9][0-9].ppm; do
        if [ $((n % 4)) -eq 0 ]; then
            frames="$frames $frame"
        fi
        n=$((n + 1))
    done
    # shellcheck disable=SC2086 # one word per frame path
    magick montage -background "$BG" -fill '#d8dee9' -font "$FONT" \
        -pointsize 11 -label '%t' $frames -tile 10x -geometry +2+2 "$out"
}

# The scale, the band and the label of one half (k = SCALE).
half_filter() {
    local label="$1" left_pad="$2" extra="$3"
    printf 'scale=iw*%d:ih*%d:flags=neighbor,pad=iw+%d:ih+%d:%d:%d:color=%s,drawtext=fontfile=%s:text=%s:x=%d:y=4:fontsize=18:fontcolor=0xd8dee9%s' \
        "$SCALE" "$SCALE" "$left_pad" "$BAND" "$left_pad" "$BAND" "$BG_FF" \
        "$FONT" "'$label'" $((left_pad + 6)) "$extra"
}

encoded=0
for off_dir in "$RAW_DIR"/*-off/; do
    off_dir="${off_dir%/}"
    clip="$(basename "$off_dir")"
    clip="${clip%-off}"
    on_dir="$RAW_DIR/$clip-on"
    [ -d "$on_dir" ] || die "$clip has an off run but no on run"

    n_off="$(count_frames "$off_dir")"
    n_on="$(count_frames "$on_dir")"
    [ "$n_off" -eq "$n_on" ] \
        || die "$clip: $n_off frames with the setting off, $n_on with it on"
    diff_at="$(first_diff "$off_dir" "$on_dir" "$n_off")"

    gif="$OUT_DIR/$clip.gif"
    counter="drawtext=fontfile=$FONT:text='frame %{eif\\:n\\:d\\:3}':x=w-tw-6:y=4:fontsize=18:fontcolor=0x8a93a2"
    left="$(half_filter 'SETTING OFF' 0 '')"
    right="$(half_filter 'SETTING ON' "$GUTTER" ",$counter")"
    ffmpeg -hide_banner -loglevel error -y \
        -framerate "$FPS" -i "$off_dir/%03d.ppm" \
        -framerate "$FPS" -i "$on_dir/%03d.ppm" \
        -filter_complex "[0:v]$left[a];[1:v]$right[b];[a][b]hstack=inputs=2,split[s0][s1];[s0]palettegen=stats_mode=diff[p];[s1][p]paletteuse=dither=none" \
        -final_delay 150 -loop 0 "$gif"

    bytes="$(wc -c < "$gif" | tr -d ' ')"
    seconds="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$gif")"
    [ "$bytes" -le "$MAX_BYTES" ] \
        || die "$gif is $bytes bytes, above $MAX_BYTES"
    awk -v d="$seconds" -v lo="$MIN_SECONDS" -v hi="$MAX_SECONDS" \
        'BEGIN { exit !(d >= lo && d <= hi) }' \
        || die "$gif runs ${seconds}s, outside ${MIN_SECONDS}-${MAX_SECONDS}s"

    strip "$off_dir" "$OUT_DIR/$clip-off-strip.png"
    strip "$on_dir" "$OUT_DIR/$clip-on-strip.png"

    key="$(tr -d ' \n' < "$on_dir/key.txt")"
    [ "$key" -ge 0 ] 2> /dev/null || die "$clip has no key frame"
    key_name="$(printf '%03d.ppm' "$key")"
    magick \
        \( "$off_dir/$key_name" -filter point -resize "$((SCALE * 100))%" \
           -background "$BG" -gravity north -splice "0x$BAND" \
           -gravity northwest -fill '#d8dee9' -font "$FONT" -pointsize 18 \
           -annotate +6+4 'SETTING OFF' \) \
        \( "$on_dir/$key_name" -filter point -resize "$((SCALE * 100))%" \
           -background "$BG" -gravity north -splice "0x$BAND" \
           -gravity west -splice "${GUTTER}x0" \
           -gravity northwest -fill '#d8dee9' -font "$FONT" -pointsize 18 \
           -annotate "+$((GUTTER + 6))+4" 'SETTING ON' \
           -gravity northeast -fill '#8a93a2' -annotate +6+4 "frame $key" \) \
        -background "$BG" +append "$OUT_DIR/$clip-key.png"

    printf '%s  %s bytes  %ss  key frame %s  first differing frame %s\n' \
        "$gif" "$bytes" "$seconds" "$key" "$diff_at"
    encoded=$((encoded + 1))
done

[ "$encoded" -gt 0 ] || die "no clip was recorded"
printf 'encoded %d clip(s) into %s\n' "$encoded" "$OUT_DIR"
