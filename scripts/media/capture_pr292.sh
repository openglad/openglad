#!/usr/bin/env bash
# Proof media for PR #292 (the test-teeth audit's product fixes).
#
# Every user-visible fix in that PR owes a before/after pair, and both halves
# have to come from the repository's own capture seams — openglad_demo for
# gameplay, the og_test_* capture tests for scripted scenes — never from a
# hand-rolled encoder or a manual screenshot (AGENTS.md, "PR screenshots and
# proof media").  Output lands in build/media/pr-292/{before,after}/, which is
# gitignored; the finished files are pushed to the openglad/openglad-screenshots
# repo under pr-292/ and linked from the PR body by SHA-pinned raw URL.
#
# The two halves are captured at different times on purpose:
#   --before  runs on the phase-2 base tree with only the capture code added,
#             BEFORE any fix merges;
#   --after   runs on the merged tree.
# Anything else (re-deriving a "before" from a half-fixed tree) produces a
# picture that proves nothing.
#
# Usage:
#   scripts/media/capture_pr292.sh --before [--scene <name>]...
#   scripts/media/capture_pr292.sh --after  [--scene <name>]...
#   scripts/media/capture_pr292.sh --list
#
#   OPENGLAD_BUILD_DIR=<dir>   build tree holding the binaries
#                              (default build/ci-test)
#   OPENGLAD_PR292_MEDIA_DIR=<dir>
#                              output root (default build/media/pr-292)
#
# Scenes:
#   selftest  harness self-check: two openglad_demo stills of one stock level
#             through the cell camera.  Proves the whole pipeline (demo run,
#             scratch config, BMP -> pal8 PNG, ffprobe verification) before a
#             recipe blames the game for a missing frame.  In the default set.
#   p2        the smooth.cpp autotiler gaps (stray light-grass columns in the
#             dark-grass bands, unautotiled horizontal wall runs).  Deferred
#             out of PR #292 to issue #301; kept here because the recipe is
#             what issue #301 needs, and not in the default set.
#
# K1-K3 add the p1 (alpha text), p5 (cleric heal) and p8 (lobby denial
# correlation) recipes next to scene_p2 and list them in SCENES/DEFAULT_SCENES.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"
MEDIA_ROOT="${OPENGLAD_PR292_MEDIA_DIR:-$REPO_ROOT/build/media/pr-292}"

SCENES=(selftest p2)
DEFAULT_SCENES=(selftest)

# A GIF that will not load on GitHub is not proof; this is the band every
# shipped animation in the screenshots repo already sits in.
GIF_MAX_BYTES=2000000

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '/^# Usage:/,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

# --- arguments --------------------------------------------------------------
PHASE=""
SELECTED=()
while [ $# -gt 0 ]; do
    case "$1" in
        --before) PHASE="before" ;;
        --after) PHASE="after" ;;
        --list)
            printf '%s\n' "${SCENES[@]}"
            exit 0
            ;;
        --scene)
            shift
            [ $# -gt 0 ] || die 'error: --scene needs a name'
            SELECTED+=("$1")
            ;;
        --scene=*) SELECTED+=("${1#--scene=}") ;;
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

[ -n "$PHASE" ] || { usage >&2; die 'error: one of --before / --after is required'; }
if [ "${#SELECTED[@]}" -eq 0 ]; then
    SELECTED=("${DEFAULT_SCENES[@]}")
fi
for want in "${SELECTED[@]}"; do
    found=0
    for known in "${SCENES[@]}"; do
        [ "$want" = "$known" ] && found=1
    done
    [ "$found" -eq 1 ] || die "error: no such scene '$want' (see --list)"
done

wanted() { # wanted <scene>
    local name
    for name in "${SELECTED[@]}"; do
        [ "$name" = "$1" ] && return 0
    done
    return 1
}

OUT_DIR="$MEDIA_ROOT/$PHASE"
mkdir -p "$OUT_DIR"

# --- prerequisites ----------------------------------------------------------
need_bin() { # need_bin <path>
    [ -x "$1" ] && return 0
    printf '%s not found; build it first:\n' "$1" >&2
    printf '  cmake --build --preset ci-test --target %s\n' "$(basename "$1")" >&2
    exit 1
}

DEMO_BIN="$BUILD_DIR/openglad_demo"

for tool in ffmpeg ffprobe; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done

WORK_DIR="$(mktemp -d)"
FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# The scripted scenes build their own rosters through save/save0.gtl; a real
# save must survive the run (scripts/fx_review/generate.sh precedent).
SAVE_BACKUP=""
if [ -f save/save0.gtl ]; then
    SAVE_BACKUP="$(mktemp)"
    cp save/save0.gtl "$SAVE_BACKUP"
fi
cleanup() {
    if [ -n "$SAVE_BACKUP" ]; then
        cp "$SAVE_BACKUP" save/save0.gtl
        rm -f -- "$SAVE_BACKUP"
    fi
    rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT

# --- expected-output manifest ----------------------------------------------
# Each recipe declares what it owes; the tail below is what actually checks
# it.  scripts/media/verify_media.py has a fixed EXPECTED table and cannot
# know about files that only exist for one PR.
EXPECT_PNG=()   # "<path>|<width>|<height>|<pix_fmt>"
EXPECT_GIF=()   # "<path>"

expect_png() { EXPECT_PNG+=("$1|$2|$3|${4:-}"); }
expect_gif() { EXPECT_GIF+=("$1"); }

# --- capture seams ----------------------------------------------------------
# Headless, and every run writes its config into a scratch directory: a run
# that cannot reach its config directory falls back to the working directory
# and rewrites the tracked cfg/openglad.yaml.  The guard at the bottom of this
# script catches that.
run_demo() { # run_demo <name> <campaign> <scenarios> <team> <focus> <seed> <limit>
    local name="$1" campaign="$2" scenarios="$3" team="$4" focus="$5" seed="$6" limit="$7"
    need_bin "$DEMO_BIN"
    FRAME_DIR="$WORK_DIR/frames-$name"
    rm -rf -- "$FRAME_DIR"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_SEED="$seed" \
        OPENGLAD_DEMO_CAMPAIGN="$campaign" \
        OPENGLAD_DEMO_SCENARIOS="$scenarios" \
        OPENGLAD_DEMO_TEAM_SIZE="$team" \
        OPENGLAD_DEMO_CAPTURE_DIR="$FRAME_DIR" \
        OPENGLAD_DEMO_CAPTURE_FOCUS="$focus" \
        OPENGLAD_DEMO_CAPTURE_LIMIT="$limit" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$DEMO_BIN" > "$WORK_DIR/$name.log" 2>&1 || {
        printf 'demo run %s failed; see %s\n' "$name" "$WORK_DIR/$name.log" >&2
        exit 1
    }
}

# One test binary, one --gtest_filter, per call: the shuffle and multi-suite
# forms are what make og_test_basecamp hang on seeds 7 and 29.
run_scene_test() { # run_scene_test <name> <binary> <gtest_filter> [VAR=VALUE ...]
    local name="$1" binary="$2" filter="$3"
    shift 3
    need_bin "$BUILD_DIR/$binary"
    SCENE_DIR="$WORK_DIR/scene-$name"
    rm -rf -- "$SCENE_DIR"
    mkdir -p "$SCENE_DIR"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$@" \
        "$BUILD_DIR/$binary" --gtest_filter="$filter" \
        > "$WORK_DIR/$name.log" 2>&1 || {
        printf 'scene run %s failed; see %s\n' "$name" "$WORK_DIR/$name.log" >&2
        exit 1
    }
}

frame_path() { # frame_path <dir> <frame>
    printf '%s/frame%05d.bmp' "$1" "$2"
}

# --- conversion -------------------------------------------------------------
# BMP dumps are indexed already; palettegen/paletteuse on the single upscaled
# frame hands the PNG encoder a pal8 picture instead of 32-bit RGB.  The 2x
# nearest-neighbour upscale is what makes the 5x6 font legible on GitHub.
still2x() { # still2x <in.bmp> <out.png>
    [ -s "$1" ] || die "FAIL: expected frame $1 was never dumped"
    "${FFMPEG[@]}" -i "$1" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse=dither=none' \
        -frames:v 1 -update 1 -compression_level 9 "$2"
}

# PPM captures are true colour already; a nearest-neighbour double is all they
# need.
ppm_still() { # ppm_still <in.ppm> <out.png> [scale]
    local in="$1" out="$2" scale="${3:-640:400}"
    [ -s "$in" ] || die "FAIL: expected capture $in was never written"
    "${FFMPEG[@]}" -i "$in" -vf "scale=$scale:flags=neighbor" \
        -frames:v 1 -update 1 -compression_level 9 "$out"
}

# --- GIF assembly (scripts/media/capture_showcase.sh recipe) ----------------
# One concat-demuxer list line per animation frame with an explicit on-screen
# duration: a moment that should linger is a longer duration, not a repeated
# frame, because GIF stores per-frame delays natively.
emit() { # emit <list> <dir> <duration-secs> <first> <last> <step>
    local list=$1 dir=$2 dur=$3 first=$4 last=$5 step=$6 i
    for ((i = first; i <= last; i += step)); do
        printf "file '%s/frame%05d.bmp'\nduration %s\n" "$dir" "$i" "$dur" >> "$list"
    done
}

hold() { # hold <list> <dir> <frame> <duration-secs>
    printf "file '%s/frame%05d.bmp'\nduration %s\n" "$2" "$3" "$4" >> "$1"
}

# Two passes: palettegen collects the exact colours the frames use (indexed
# game frames have at most 256, so nothing is quantized), paletteuse maps
# every frame through that palette with no dithering.
encode_gif() { # encode_gif <list> <out.gif> <final-delay-cs>
    local list=$1 out=$2 final=$3 palette="$WORK_DIR/palette.png"
    [ -s "$list" ] || die "FAIL: $out has an empty frame list"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,palettegen' "$palette"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" -i "$palette" \
        -lavfi '[0:v]scale=iw*2:ih*2:flags=neighbor[x];[x][1:v]paletteuse=dither=none' \
        -final_delay "$final" "$out"
    rm -f -- "$palette"
}

# --- scenes -----------------------------------------------------------------

# Harness self-check.  Two one-frame captures of the same seeded level, aimed
# at two different map cells: they must differ, which proves the cell camera
# reached the game and not just the environment block.
scene_selftest() {
    local near="$OUT_DIR/k0-selftest-origin-$PHASE.png"
    local far="$OUT_DIR/k0-selftest-$PHASE.png"

    run_demo selftest-origin gladiator 5 4 'cell:0,0' 7 1
    still2x "$(frame_path "$FRAME_DIR" 0)" "$near"

    run_demo selftest-far gladiator 5 4 'cell:20,20' 7 1
    still2x "$(frame_path "$FRAME_DIR" 0)" "$far"

    if cmp -s "$near" "$far"; then
        die "FAIL: the cell camera produced identical frames at 0,0 and 20,20"
    fi
    expect_png "$near" 640 400 pal8
    expect_png "$far" 640 400 pal8
}

# The smooth.cpp autotiler gaps.  Deferred to issue #301: run it with
# --scene p2 from whichever branch fixes the smoother, once per phase.
#
# longseason 8 carries 66 dark-grass mask-14 strays, the densest of them the
# band at y=20-27 around x=35-43; westlands 13 carries 72, along its top rows;
# modes 841 holds the 30 mask-2 (left end of a horizontal run) wall cells on
# rows y=8 and y=16.  scripts/media/smooth_mask_census.py --level <grid.png>
# prints the coordinates these camera cells were picked from.
scene_p2() {
    local -a levels=(
        "p2-longseason-08 longseason 8 cell:20,20"
        "p2-westlands-13 westlands 13 cell:0,0"
        "p2-modes-841 modes 841 cell:0,4"
    )
    local row name campaign scen focus out
    for row in "${levels[@]}"; do
        read -r name campaign scen focus <<< "$row"
        out="$OUT_DIR/$name-$PHASE.png"
        run_demo "$name" "$campaign" "$scen" 2 "$focus" 7 1
        still2x "$(frame_path "$FRAME_DIR" 0)" "$out"
        expect_png "$out" 640 400 pal8
    done

    # The quantified claim that goes in the PR body next to the images.
    python3 scripts/media/smooth_mask_census.py \
        --campaign longseason --campaign westlands --campaign modes \
        > "$OUT_DIR/census.txt"
    [ -s "$OUT_DIR/census.txt" ] || die 'FAIL: the census wrote nothing'
}

for scene in "${SELECTED[@]}"; do
    printf '=== scene %s (%s) ===\n' "$scene" "$PHASE"
    "scene_$scene"
done

# --- verification tail ------------------------------------------------------
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
    printf 'ok %s (%sx%s %s)\n' "$file" "$width" "$height" "$fmt"
}

verify_gif() { # verify_gif <file>
    local file="$1" frames bytes
    [ -s "$file" ] || die "FAIL: $file was never produced"
    frames="$(ffprobe -loglevel error -select_streams v:0 -count_frames \
        -show_entries stream=nb_read_frames -of csv=p=0 "$file")"
    [ "${frames:-0}" -ge 2 ] \
        || die "FAIL: $file holds $frames frame(s); an animation needs at least 2"
    bytes="$(stat -c %s "$file")"
    [ "$bytes" -le "$GIF_MAX_BYTES" ] \
        || die "FAIL: $file is $bytes bytes, over the $GIF_MAX_BYTES budget"
    printf 'ok %s (%s frames, %s bytes)\n' "$file" "$frames" "$bytes"
}

printf '=== verifying %s ===\n' "$OUT_DIR"
if [ "${#EXPECT_PNG[@]}" -eq 0 ] && [ "${#EXPECT_GIF[@]}" -eq 0 ]; then
    die 'FAIL: the selected scenes declared no output'
fi
for row in ${EXPECT_PNG[@]+"${EXPECT_PNG[@]}"}; do
    IFS='|' read -r path w h fmt <<< "$row"
    verify_png "$path" "$w" "$h" "$fmt"
done
for path in ${EXPECT_GIF[@]+"${EXPECT_GIF[@]}"}; do
    verify_gif "$path"
done

# A headless run that cannot reach its scratch config directory falls back to
# the working directory and rewrites the tracked cfg/openglad.yaml.  That has
# cost enough debugging time to be worth failing over.
if command -v git > /dev/null && [ -e "$REPO_ROOT/.git" ]; then
    cfg_dirty="$(git -C "$REPO_ROOT" status --porcelain cfg/)"
    if [ -n "$cfg_dirty" ]; then
        printf 'FAIL: a capture run dirtied cfg/:\n%s\n' "$cfg_dirty" >&2
        exit 1
    fi
fi

ls -l "$OUT_DIR"
