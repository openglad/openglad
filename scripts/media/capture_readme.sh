#!/usr/bin/env bash
# Regenerate the six screenshots the README embeds. Output lands in
# build/media/readme/ (gitignored); the finished PNGs get committed to the
# openglad/openglad-screenshots repo under readme/ and the README links them
# by raw.githubusercontent URL pinned to that repo's commit — never to this
# one (AGENTS.md, "PR screenshots and proof media").
#
# Two capture seams, both headless:
#   * menu and lobby stills come from the uxshots probe in og_test_basecamp,
#     which writes one PPM per capture point when UXSHOTS_DIR is set;
#   * gameplay stills come from openglad_demo, which plays a seeded session
#     on the dummy video driver and dumps one indexed BMP per rendered frame.
#     Every run below is fully seeded, so the frame numbers pinned at the top
#     of this script stay on the moments they were picked for.
#
# ffmpeg turns both into the shipped media: the 320x200 game canvas doubled
# to 640x400 with nearest-neighbour, which is what makes the 5x6 menu font
# legible on GitHub. BMP dumps are already indexed and stay pal8 through the
# palettegen/paletteuse chain; PPM dumps are converted with a plain scale.
#
# Usage:
#   scripts/media/capture_readme.sh [--only <recipe>]... [--proof] [--list]
#     --list    print the recipe names and exit
#     --only    capture just this recipe (repeatable); default is all six
#     --proof   also capture the PR-proof extras (main menu without a
#               company, and the F1 help screen) at 1x and 4x
#   OPENGLAD_BUILD_DIR=<dir>          build tree holding the binaries
#                                     (default build/ci-test)
#   OPENGLAD_README_MEDIA_DIR=<dir>   output directory
#                                     (default build/media/readme)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"
OUT_DIR="${OPENGLAD_README_MEDIA_DIR:-$REPO_ROOT/build/media/readme}"

# Pinned frames. Each demo run is seeded, so these name specific moments:
# the hedge-line melee outside the keep, arrows in flight and a gold pile on
# the flank; the Capture the Flag arena with both squads on the plaza under
# the announcement stack the mode narrates itself with (there is no separate
# score HUD -- those yellow lines ARE the scoreboard, and no flag sprite is
# on screen in this run); the ninefold judgment pulse on the Ninefold Court
# (the frame capture_showcase.sh uses for the same level -- scen 605 is a
# single-floor Lua-scripted court, so the still is a level-script shot, not
# a multi-floor one).
README_GAMEPLAY_FRAME=396
README_CTF_FRAME=120
README_NINEFOLD_FRAME=301

RECIPES=(mainmenu gameplay basecamp-four-seats networking-lobby mode-ctf ninefold-court)

usage() {
    sed -n '/^# Usage:/,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

# --- arguments --------------------------------------------------------------
SELECTED=()
PROOF=0
while [ $# -gt 0 ]; do
    case "$1" in
        --list)
            printf '%s\n' "${RECIPES[@]}"
            exit 0
            ;;
        --only)
            shift
            [ $# -gt 0 ] || die 'error: --only needs a recipe name'
            SELECTED+=("$1")
            ;;
        --only=*)
            SELECTED+=("${1#--only=}")
            ;;
        --proof)
            PROOF=1
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

# --- prerequisites ----------------------------------------------------------
BASECAMP_BIN="$BUILD_DIR/og_test_basecamp"
DEMO_BIN="$BUILD_DIR/openglad_demo"

need_bin() { # need_bin <path>
    [ -x "$1" ] && return 0
    printf '%s not found; build it first:\n' "$1" >&2
    printf '  cmake --build --preset ci-test --target %s\n' "$(basename "$1")" >&2
    exit 1
}

if wanted mainmenu || wanted basecamp-four-seats || wanted networking-lobby \
    || [ "$PROOF" -eq 1 ]; then
    need_bin "$BASECAMP_BIN"
fi
if wanted gameplay || wanted mode-ctf || wanted ninefold-court; then
    need_bin "$DEMO_BIN"
fi
for tool in ffmpeg ffprobe; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "$OUT_DIR"

FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# --- capture seams ----------------------------------------------------------
# Every run is headless and writes its config into a scratch directory: a
# failing run that falls back to the working directory would rewrite the
# repository's tracked cfg/openglad.yaml, which the check at the bottom of
# this script catches.
run_shots() { # run_shots <name> <gtest_filter> <attempts> -> $SHOT_DIR
    local name="$1" filter="$2" attempts="$3" attempt=1
    SHOT_DIR="$WORK_DIR/shots-$name"
    while :; do
        rm -rf -- "$SHOT_DIR" "$WORK_DIR/config-$name"
        mkdir -p "$SHOT_DIR"
        if env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            SDL_RENDER_DRIVER=software \
            UXSHOTS_DIR="$SHOT_DIR" \
            OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
            "$BASECAMP_BIN" --gtest_filter="$filter" \
            > "$WORK_DIR/$name.log" 2>&1; then
            return 0
        fi
        if [ "$attempt" -ge "$attempts" ]; then
            printf 'capture run %s failed after %d attempt(s); see %s\n' \
                "$name" "$attempt" "$WORK_DIR/$name.log" >&2
            exit 1
        fi
        printf 'capture run %s failed (attempt %d/%d), retrying\n' \
            "$name" "$attempt" "$attempts" >&2
        attempt=$((attempt + 1))
    done
}

run_demo() { # run_demo <name> <campaign> <scenarios> <team> <focus> <seed> <limit> -> $FRAME_DIR
    local name="$1" campaign="$2" scenarios="$3" team="$4" focus="$5" seed="$6" limit="$7"
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
# they need.
ppm_still() { # ppm_still <in.ppm> <out.png> [scale]
    local in="$1" out="$2" scale="${3:-640:400}"
    [ -s "$in" ] || die "FAIL: expected capture $in was never written"
    "${FFMPEG[@]}" -i "$in" -vf "scale=$scale:flags=neighbor" \
        -frames:v 1 -update 1 -compression_level 9 "$out"
}

ppm_still_native() { # ppm_still_native <in.ppm> <out.png>
    [ -s "$1" ] || die "FAIL: expected capture $1 was never written"
    "${FFMPEG[@]}" -i "$1" -frames:v 1 -update 1 -compression_level 9 "$2"
}

# BMP dumps are indexed; palettegen/paletteuse on the single upscaled frame
# hands the PNG encoder a pal8 picture instead of 32-bit RGB (the recipe
# scripts/media/capture_showcase.sh uses for its stills).
still2x() { # still2x <in.bmp> <out.png>
    [ -s "$1" ] || die "FAIL: expected frame $1 was never dumped"
    "${FFMPEG[@]}" -i "$1" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse=dither=none' \
        -frames:v 1 -update 1 -compression_level 9 "$2"
}

frame_path() { # frame_path <dir> <frame>
    printf '%s/frame%05d.bmp' "$1" "$2"
}

# --- recipes ----------------------------------------------------------------
if wanted mainmenu; then
    run_shots mainmenu 'UxShots.b_mainmenu_with_company' 1
    ppm_still "$SHOT_DIR/mainmenu_with_company.ppm" "$OUT_DIR/mainmenu.png"
    verify_png "$OUT_DIR/mainmenu.png" 640 400
fi

if wanted basecamp-four-seats; then
    run_shots basecamp 'UxShots.i_basecamp_four_local_seats' 1
    ppm_still "$SHOT_DIR/basecamp_four_local_seats.ppm" \
        "$OUT_DIR/basecamp-four-seats.png"
    verify_png "$OUT_DIR/basecamp-four-seats.png" 640 400
fi

if wanted networking-lobby; then
    # The lobby flow drives a scripted client through several waits; on a
    # loaded box one of them can time out, so give it two more tries before
    # calling the shot lost.
    run_shots networking 'NetworkingUxShots.session_views_and_kick_confirm' 3
    ppm_still "$SHOT_DIR/networking_hosting_two_machines.ppm" \
        "$OUT_DIR/networking-lobby.png"
    verify_png "$OUT_DIR/networking-lobby.png" 640 400
fi

if wanted gameplay; then
    run_demo gameplay gladiator 5 4 player 7 420
    still2x "$(frame_path "$FRAME_DIR" "$README_GAMEPLAY_FRAME")" \
        "$OUT_DIR/gameplay.png"
    verify_png "$OUT_DIR/gameplay.png" 640 400 pal8
fi

if wanted mode-ctf; then
    run_demo ctf modes 500 8 center 1337 480
    still2x "$(frame_path "$FRAME_DIR" "$README_CTF_FRAME")" \
        "$OUT_DIR/mode-ctf.png"
    verify_png "$OUT_DIR/mode-ctf.png" 640 400 pal8
fi

if wanted ninefold-court; then
    # concept is a dev-only campaign: it composes into builtin-dev/ and is
    # reachable from a build tree, never from an installed or web build.
    run_demo ninefold concept 605 6 center 7 310
    still2x "$(frame_path "$FRAME_DIR" "$README_NINEFOLD_FRAME")" \
        "$OUT_DIR/ninefold-court.png"
    verify_png "$OUT_DIR/ninefold-court.png" 640 400 pal8
fi

# --- proof extras -----------------------------------------------------------
# Not embedded in the README: these are the stills a PR uses to prove the
# main menu and the help screen still read correctly, at the native canvas
# size and at 4x, where a 5x6 glyph is unambiguous.
if [ "$PROOF" -eq 1 ]; then
    run_shots proof 'UxShots.a_mainmenu_no_company:UxShots.n_help_screen' 3
    for shot in mainmenu_no_company help_controls; do
        ppm_still_native "$SHOT_DIR/$shot.ppm" "$OUT_DIR/$shot.png"
        verify_png "$OUT_DIR/$shot.png" 320 200
        ppm_still "$SHOT_DIR/$shot.ppm" "$OUT_DIR/${shot}_x4.png" 1280:800
        verify_png "$OUT_DIR/${shot}_x4.png" 1280 800
    done
fi

# A headless run that cannot reach its scratch config directory falls back to
# the working directory and rewrites the tracked cfg/openglad.yaml. That has
# cost enough debugging time to be worth failing over.
if command -v git > /dev/null && [ -e "$REPO_ROOT/.git" ]; then
    cfg_dirty="$(git -C "$REPO_ROOT" status --porcelain cfg/)"
    if [ -n "$cfg_dirty" ]; then
        printf 'FAIL: a capture run dirtied cfg/:\n%s\n' "$cfg_dirty" >&2
        exit 1
    fi
fi

ls -l "$OUT_DIR"
