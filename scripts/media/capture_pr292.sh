#!/usr/bin/env bash
# Capture the proof media for PR #292 (the pre-existing-defect sweep). Output
# lands in build/media/pr-292/<phase>/ (gitignored); the finished PNGs and
# GIFs get pushed to the openglad/openglad-screenshots repo under pr-292/ and
# the PR body links them by raw.githubusercontent URL pinned to THAT repo's
# commit -- never to this one (AGENTS.md, "PR screenshots and proof media").
#
# Every recipe is captured twice: once with --phase before, on a tree that
# still has the defect, and once with --phase after, on the fixed tree. The
# two runs write the same file names into different phase directories so the
# side-by-side composition is a plain pair.
#
# Usage:
#   scripts/media/capture_pr292.sh [--phase before|after] [--only <recipe>]...
#     --list         print the recipe names and exit
#     --phase        which side of the fix this tree is (default: before)
#     --only         capture just this recipe (repeatable); default is all
#   OPENGLAD_BUILD_DIR=<dir>          build tree holding the binaries
#                                     (default build/ci-test)
#   OPENGLAD_PR292_MEDIA_DIR=<dir>    output directory
#                                     (default build/media/pr-292/<phase>)
#
# Recipes
# -------
# p1-damage-numbers
#   P1 is the 2013 defect in sdl_video::walkputbuffertext_alpha: it computes
#   the team-ramped `curcolor` for every source byte above 247 and then plots
#   the flat `teamcolor` anyway, so everything drawn through the alpha text
#   path comes out as a single-shade silhouette. Its only production consumer
#   is the floating damage/heal number (draw_damage_number ->
#   text::write_xy_center_alpha), and nothing replicates those numbers over
#   the wire, so no recorded session can show the difference -- the proof is
#   a scripted render scene instead. og_test_rendering's
#   RenderEffects.damage_number_glyphs_paint_in_the_requested_band draws five
#   numbers (heal green 56, attacker orange 235, target RED, then the same
#   "99" opaque and at half alpha) on flat backing strips and, with
#   OG_FX_CAPTURE_DIR set, dumps the 320x200 viewport as a P6 PPM.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"

RECIPES=(p1-damage-numbers)

usage() {
    sed -n '/^# Usage:/,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

# --- arguments --------------------------------------------------------------
SELECTED=()
PHASE=before
while [ $# -gt 0 ]; do
    case "$1" in
        --list)
            printf '%s\n' "${RECIPES[@]}"
            exit 0
            ;;
        --phase)
            shift
            [ $# -gt 0 ] || die 'error: --phase needs before or after'
            PHASE="$1"
            ;;
        --phase=*)
            PHASE="${1#--phase=}"
            ;;
        --before)
            PHASE=before
            ;;
        --after)
            PHASE=after
            ;;
        --only)
            shift
            [ $# -gt 0 ] || die 'error: --only needs a recipe name'
            SELECTED+=("$1")
            ;;
        --only=*)
            SELECTED+=("${1#--only=}")
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "error: unknown argument '$1'"
            ;;
    esac
    shift
done

case "$PHASE" in
    before | after) ;;
    *) die "error: --phase must be before or after, not '$PHASE'" ;;
esac

if [ "${#SELECTED[@]}" -eq 0 ]; then
    SELECTED=("${RECIPES[@]}")
fi
for want in "${SELECTED[@]}"; do
    found=0
    for known in "${RECIPES[@]}"; do
        [ "$want" = "$known" ] && found=1
    done
    [ "$found" -eq 1 ] || die "error: no such recipe '$want' (see --list)"
done

wanted() { # wanted <recipe>
    local name
    for name in "${SELECTED[@]}"; do
        [ "$name" = "$1" ] && return 0
    done
    return 1
}

OUT_DIR="${OPENGLAD_PR292_MEDIA_DIR:-$REPO_ROOT/build/media/pr-292/$PHASE}"

# --- prerequisites ----------------------------------------------------------
need_bin() { # need_bin <path>
    [ -x "$1" ] && return 0
    printf '%s not found; build it first:\n' "$1" >&2
    printf '  cmake --build --preset ci-test --target %s\n' "$(basename "$1")" >&2
    exit 1
}

RENDERING_BIN="$BUILD_DIR/og_test_rendering"
if wanted p1-damage-numbers; then
    need_bin "$RENDERING_BIN"
fi
for tool in ffmpeg ffprobe python3; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "$OUT_DIR"

FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# --- capture seams ----------------------------------------------------------
# One test binary, one --gtest_filter, never --gtest_shuffle, and a scratch
# config directory: a headless run that cannot reach its config directory
# falls back to the working directory and rewrites the tracked
# cfg/openglad.yaml, which the check at the bottom of this script catches.
run_fx_scene() { # run_fx_scene <name> <binary> <gtest_filter> -> $SCENE_DIR
    local name="$1" binary="$2" filter="$3"
    SCENE_DIR="$WORK_DIR/fx-$name"
    rm -rf -- "$SCENE_DIR" "$WORK_DIR/config-$name"
    mkdir -p "$SCENE_DIR"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OG_FX_CAPTURE_DIR="$SCENE_DIR" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$binary" --gtest_filter="$filter" \
        > "$WORK_DIR/$name.log" 2>&1 || {
        printf 'capture run %s failed; see %s\n' "$name" "$WORK_DIR/$name.log" >&2
        exit 1
    }
}

# --- conversion + verification ---------------------------------------------
verify_png() { # verify_png <file> <width> <height> [pix_fmt]
    local file="$1" want_w="$2" want_h="$3" want_fmt="${4:-}" probed
    [ -s "$file" ] || die "FAIL: $file was never produced"
    probed="$(ffprobe -loglevel error -select_streams v:0 \
        -show_entries stream=codec_name,width,height,pix_fmt \
        -of csv=p=0 "$file")"
    local codec width height fmt
    IFS=, read -r codec width height fmt <<< "$probed"
    [ "$codec" = "png" ] || die "FAIL: $file decoded as $codec, expected png"
    { [ "$width" = "$want_w" ] && [ "$height" = "$want_h" ]; } \
        || die "FAIL: $file is ${width}x${height}, expected ${want_w}x${want_h}"
    if [ -n "$want_fmt" ] && [ "$fmt" != "$want_fmt" ]; then
        die "FAIL: $file is $fmt, expected $want_fmt"
    fi
    printf 'wrote %s (%sx%s %s)\n' "$file" "$width" "$height" "$fmt"
}

# PPM captures are true colour already; a nearest-neighbour double is all
# they need (the recipe scripts/media/capture_readme.sh uses).
ppm_still() { # ppm_still <in.ppm> <out.png> [scale]
    local in="$1" out="$2" scale="${3:-iw*2:ih*2}"
    [ -s "$in" ] || die "FAIL: expected capture $in was never written"
    "${FFMPEG[@]}" -i "$in" -vf "scale=$scale:flags=neighbor" \
        -frames:v 1 -update 1 -compression_level 9 "$out"
}

ppm_crop4x() { # ppm_crop4x <in.ppm> <out.png> <crop_h> <crop_y>
    local in="$1" out="$2" ch="$3" cy="$4"
    [ -s "$in" ] || die "FAIL: expected capture $in was never written"
    "${FFMPEG[@]}" -i "$in" \
        -vf "crop=iw:$ch:0:$cy,scale=iw*4:ih*4:flags=neighbor" \
        -frames:v 1 -update 1 -compression_level 9 "$out"
}

# The quantified claim for a glyph-colour change: every distinct colour inside
# a horizontal band of the capture, with its pixel count. A flat plot shows
# one colour per number; a ramped one shows several inside the same band.
ppm_band_census() { # ppm_band_census <in.ppm> <y0> <y1>
    python3 - "$1" "$2" "$3" <<'PY'
import collections, re, sys
data = open(sys.argv[1], 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
w, h = int(m.group(1)), int(m.group(2))
off = m.end()
y0, y1 = int(sys.argv[2]), int(sys.argv[3])
count = collections.Counter()
for y in range(y0, y1):
    row = off + 3 * y * w
    for x in range(w):
        i = row + 3 * x
        count[(data[i], data[i + 1], data[i + 2])] += 1
total = sum(count.values())
print(f'  band y={y0}..{y1 - 1}: {total} px, {len(count)} distinct colours')
for rgb, n in sorted(count.items(), key=lambda kv: -kv[1]):
    print('    rgb%-16s %5d' % (str(rgb), n))
PY
}

# --- recipes ----------------------------------------------------------------
if wanted p1-damage-numbers; then
    run_fx_scene p1 "$RENDERING_BIN" \
        'RenderEffects.damage_number_glyphs_paint_in_the_requested_band'
    P1_PPM="$SCENE_DIR/damage_numbers_alpha/000.ppm"
    ppm_still "$P1_PPM" "$OUT_DIR/p1-damage-numbers-$PHASE.png"
    verify_png "$OUT_DIR/p1-damage-numbers-$PHASE.png" 640 400
    # The numbers sit on backing strips spanning rows 10..19 of the viewport;
    # rows 6..21 give them a little air at 4x.
    ppm_crop4x "$P1_PPM" "$OUT_DIR/p1-damage-numbers-$PHASE-crop-4x.png" 16 6
    verify_png "$OUT_DIR/p1-damage-numbers-$PHASE-crop-4x.png" 1280 64
    printf 'p1-damage-numbers (%s): glyph colours\n' "$PHASE"
    ppm_band_census "$P1_PPM" 10 20
fi

# A headless run that cannot reach its scratch config directory falls back to
# the working directory and rewrites the tracked cfg/openglad.yaml. That has
# cost enough debugging time to be worth failing over.
if [ -n "$(git -C "$REPO_ROOT" status --porcelain cfg/ 2>/dev/null)" ]; then
    die 'FAIL: a capture run dirtied cfg/ -- check OPENGLAD_CONFIG_DIR'
fi

printf 'pr-292 %s media in %s\n' "$PHASE" "$OUT_DIR"
