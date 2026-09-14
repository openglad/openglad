#!/usr/bin/env bash
# Capture the before/after proof media for PR #292. Output lands in
# build/media/pr-292/<phase>/ (gitignored); the finished artifacts get
# committed to the openglad/openglad-screenshots repo under pr-292/, never to
# this one (AGENTS.md, "PR screenshots and proof media").
#
# A "before" run is captured on the base tree with only the capture scenes
# added; the "after" run is captured on the merged tree. Both use the same
# binaries and the same recipes, so the pair differs only by the fix.
#
# Usage:
#   scripts/media/capture_pr292.sh --before|--after [--only <recipe>]...
#     --list    print the recipe names and exit
#     --only    capture just this recipe (repeatable); default is all of them
#   OPENGLAD_BUILD_DIR=<dir>          build tree holding the binaries
#                                     (default build/ci-test)
#   OPENGLAD_PR292_MEDIA_DIR=<dir>    output directory
#                                     (default build/media/pr-292/<phase>)
#
# ffmpeg/ffprobe come from the dev shell (`nix develop`, see flake.nix); no
# encoder is hand-rolled here. The GIF recipe is the two-pass
# palettegen/paletteuse + concat-demuxer chain from
# scripts/media/capture_showcase.sh, verbatim.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"

RECIPES=(p5)
PHASE=""
SELECTED=()

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '/^# Usage:/,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list)
            printf '%s\n' "${RECIPES[@]}"
            exit 0
            ;;
        --before) PHASE=before ;;
        --after) PHASE=after ;;
        --only)
            shift
            [ $# -gt 0 ] || die 'error: --only needs a recipe name'
            SELECTED+=("$1")
            ;;
        --only=*) SELECTED+=("${1#--only=}") ;;
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

[ -n "$PHASE" ] || { usage >&2; die 'error: pass --before or --after'; }
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

for tool in ffmpeg ffprobe; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "$OUT_DIR"

FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# --- capture seams ----------------------------------------------------------
# A headless run that cannot reach its scratch config directory falls back to
# the working directory and rewrites the tracked cfg/openglad.yaml; every run
# below gets its own OPENGLAD_CONFIG_DIR and the tail of this script fails if
# cfg/ is dirty anyway. save/save0.gtl is backed up around the whole script:
# the gameplay scenes write a roster into slot 0 to stage themselves.
USER_SAVE="${OPENGLAD_CONFIG_DIR:-$HOME/.openglad}/save/save0.gtl"
if [ -f "$USER_SAVE" ]; then
    cp -- "$USER_SAVE" "$WORK_DIR/save0.gtl.bak"
    restore_save() { cp -- "$WORK_DIR/save0.gtl.bak" "$USER_SAVE"; }
else
    restore_save() { rm -f -- "$USER_SAVE"; }
fi

need_bin() { # need_bin <path>
    [ -x "$1" ] && return 0
    printf '%s not found; build it first:\n' "$1" >&2
    printf '  cmake --build --preset ci-test --target %s\n' "$(basename "$1")" >&2
    exit 1
}

# One gtest binary at a time, one --gtest_filter, never --gtest_shuffle
# (og_test_basecamp hangs on seeds 7 and 29). The scenes dump one P6 PPM per
# frame into $OG_FX_CAPTURE_DIR/<scene>/NNN.ppm.
run_scene() { # run_scene <name> <binary> <gtest_filter> -> $FRAME_ROOT, $SCENE_LOG
    local name="$1" binary="$2" filter="$3"
    need_bin "$BUILD_DIR/$binary"
    FRAME_ROOT="$WORK_DIR/frames-$name"
    SCENE_LOG="$WORK_DIR/$name.log"
    rm -rf -- "$FRAME_ROOT"
    mkdir -p "$FRAME_ROOT"
    # The P5 scene is RED by design until the fix lands (it asserts the heal
    # the refusal never performs), and its frames are dumped before the
    # assertion runs — so a failing exit status is expected on a "before" run
    # and must not stop the encode.
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OG_FX_CAPTURE_DIR="$FRAME_ROOT" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$BUILD_DIR/$binary" --gtest_filter="$filter" \
        > "$SCENE_LOG" 2>&1 || true
}

# --- conversion + verification ---------------------------------------------
emit() { # emit <list> <dir> <duration-secs> <first> <last> <step>
    local list=$1 dir=$2 dur=$3 first=$4 last=$5 step=$6 i
    for ((i = first; i <= last; i += step)); do
        printf "file '%s/%03d.ppm'\nduration %s\n" "$dir" "$i" "$dur" >> "$list"
    done
}

hold() { # hold <list> <dir> <frame> <duration-secs>
    printf "file '%s/%03d.ppm'\nduration %s\n" "$2" "$3" "$4" >> "$1"
}

encode_gif() { # encode_gif <list> <out.gif> <final-delay-cs>
    local list=$1 out=$2 final=$3 palette="$WORK_DIR/palette.png"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,palettegen' "$palette"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" -i "$palette" \
        -lavfi '[0:v]scale=iw*2:ih*2:flags=neighbor[x];[x][1:v]paletteuse=dither=none' \
        -final_delay "$final" "$out"
    rm -f -- "$palette"
}

# verify_media.py's EXPECTED table cannot cover files it has never heard of,
# so every artifact this script writes is checked here instead.
verify_gif() { # verify_gif <file> <max-bytes>
    local file="$1" max="$2" frames bytes
    [ -s "$file" ] || die "FAIL: $file was never produced"
    frames="$(ffprobe -loglevel error -select_streams v:0 -count_frames \
        -show_entries stream=nb_read_frames -of csv=p=0 "$file")"
    [ "${frames:-0}" -ge 2 ] \
        || die "FAIL: $file holds $frames frame(s); an animation needs >= 2"
    bytes="$(stat -c %s "$file")"
    [ "$bytes" -le "$max" ] \
        || die "FAIL: $file is $bytes bytes, over the $max byte budget"
    printf 'wrote %s (%s frames, %s bytes)\n' "$file" "$frames" "$bytes"
}

# --- recipes ----------------------------------------------------------------
# P5: a low-magic AI cleric heals a hurt ally — or, before the fix, silently
# refuses to. The tells are motion (the patient's HP bar refilling) and the
# replicated "Cleric healed 1 man!" line, so this one is a GIF, not a still.
# Every 2nd frame of the 240-frame scene at 0.08 s == the 240 sim ticks played
# back at roughly real speed; the frame the heal first lands on is held for
# 0.6 s so the line is readable. The refusal cut has no such frame (the scene
# reports heal_frame=-1) and runs straight through.
if wanted p5; then
    run_scene p5 og_test_game_core \
        'GameLoop.zz_capture_cleric_low_magic_heal'
    scene_dir="$FRAME_ROOT/cleric_low_magic_heal"
    [ -s "$scene_dir/000.ppm" ] \
        || die "FAIL: the P5 scene dumped no frames; see $SCENE_LOG"
    heal_frame="$(sed -n 's/^p5 clinic: .*heal_frame=\(-\{0,1\}[0-9]\{1,\}\).*/\1/p' \
        "$SCENE_LOG" | head -n 1)"
    heal_frame="${heal_frame:--1}"
    printf 'p5 %s: %s\n' "$PHASE" \
        "$(grep -m1 '^p5 clinic: ' "$SCENE_LOG" || echo 'no scene summary')"
    list="$WORK_DIR/p5.ffconcat"
    : > "$list"
    if [ "$heal_frame" -ge 0 ]; then
        emit "$list" "$scene_dir" 0.08 0 "$((heal_frame - 1))" 2
        hold "$list" "$scene_dir" "$heal_frame" 0.6
        emit "$list" "$scene_dir" 0.08 "$((heal_frame + 1))" 238 2
    else
        emit "$list" "$scene_dir" 0.08 0 238 2
    fi
    encode_gif "$list" "$OUT_DIR/p5-cleric-low-magic-$PHASE.gif" 8
    verify_gif "$OUT_DIR/p5-cleric-low-magic-$PHASE.gif" 2000000
    # The raw PPM directory is what K4's side-by-side composition consumes.
    rm -rf -- "$OUT_DIR/p5-frames"
    cp -r -- "$scene_dir" "$OUT_DIR/p5-frames"
fi

restore_save

if command -v git > /dev/null && [ -e "$REPO_ROOT/.git" ]; then
    cfg_dirty="$(git -C "$REPO_ROOT" status --porcelain cfg/)"
    if [ -n "$cfg_dirty" ]; then
        printf 'FAIL: a capture run dirtied cfg/:\n%s\n' "$cfg_dirty" >&2
        exit 1
    fi
fi

ls -l "$OUT_DIR"
