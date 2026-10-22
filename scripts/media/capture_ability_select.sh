#!/usr/bin/env bash
# Every still the New Ability Select pull request shows a reviewer, in one
# run, into build/media/ability-select/ (gitignored). Finished pictures go
# to the openglad/openglad-screenshots repo, never to this one (AGENTS.md,
# "PR screenshots and proof media").
#
# This is a tool, not a test. Each picture comes from an ordinary test that
# runs on every lane and checks what it draws; this script only sets the
# variable that asks the test to also write the frame, then turns the frames
# into PNGs. Every run uses SDL's dummy drivers and its own throwaway config
# directory, from the repository root, so it can never rewrite the tracked
# cfg/openglad.yaml (checked at the end).
#
# Steps (run in this order; --only picks some):
#   hud        the in-game HUD: 1, 2 and 4 seats with the setting on and
#              off, and 1 and 4 seats with SHIFTER held
#              (scripts/media/capture_ability_select_hud.sh, which runs
#              GladHud.zz_capture_belt_hud in og_test_view)
#   radar      a real two-player game with the radar on and the setting on
#              (GameLoop.zz_capture_splitscreen_gameplay in og_test_game_core),
#              frame RADAR_FRAME
#   abilities  the ABILITIES screen: the Archmage (MIND CONTROL on the
#              bench), the same with a slot selected and after a placement,
#              a level-1 mage (locked rows grey), and the TRAIN door dimmed
#              with the setting off (AbilitiesFlow.* in og_test_abilities,
#              UXSHOTS_DIR)
#   ui-fx      Game Settings > UI FX with the New Ability Select row
#              (MenuCapture.zz_capture_menu_effects in og_test_menu_ui)
#   changelog  the CHANGELOG! page with the keyboard picture, captured at the
#              window's real size (ChangelogFlow.* in og_test_abilities with
#              OG_CHANGELOG_SHOT_DIR set; every .png or .ppm the tests write
#              there becomes changelog-<name>.png, unscaled). Until the
#              CHANGELOG! tests are in the build this step says SKIPPED and
#              the run still finishes; verify_media.py is then told those
#              files may be missing.
#
# Canvas pictures (everything but the CHANGELOG! page) are the game's
# 320x200 canvas doubled to 640x400 with nearest-neighbour, which keeps the
# small font legible. PPM to PNG is lossless.
#
# Usage:
#   scripts/media/capture_ability_select.sh [--only <step>]... [--list] [output-dir]
#   OPENGLAD_BUILD_DIR=<dir>   build tree holding the test binaries
#                              (default build/ci-test)
#   RADAR_FRAME=<n>            the radar step's frame (default 150)
#
# Build first (ci-test preset):
#   cmake --build --preset ci-test --target og_test_view og_test_game_core \
#       og_test_abilities og_test_menu_ui
# magick, ffprobe and python3 come from the dev shell (`nix develop`).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR="$REPO_ROOT/$BUILD_DIR" ;;
esac
RADAR_FRAME="${RADAR_FRAME:-150}"
STEPS=(hud radar abilities ui-fx changelog)

usage() {
    sed -n '/^# Usage:/,/^# Build first/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

# --- arguments --------------------------------------------------------------
SELECTED=()
OUT_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
        --list)
            printf '%s\n' "${STEPS[@]}"
            exit 0
            ;;
        --only)
            shift
            [ $# -gt 0 ] || die 'error: --only needs a step name'
            SELECTED+=("$1")
            ;;
        --only=*)
            SELECTED+=("${1#--only=}")
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        -*)
            usage >&2
            die "error: unknown argument '$1'"
            ;;
        *)
            [ -z "$OUT_DIR" ] || die "error: more than one output directory"
            OUT_DIR="$1"
            ;;
    esac
    shift
done
OUT_DIR="${OUT_DIR:-$REPO_ROOT/build/media/ability-select}"

if [ "${#SELECTED[@]}" -eq 0 ]; then
    SELECTED=("${STEPS[@]}")
fi
for want in "${SELECTED[@]}"; do
    found=0
    for known in "${STEPS[@]}"; do
        [ "$want" = "$known" ] && found=1
    done
    [ "$found" -eq 1 ] || die "error: no such step '$want' (see --list)"
done

wanted() { # wanted <step>
    local name
    for name in "${SELECTED[@]}"; do
        [ "$name" = "$1" ] && return 0
    done
    return 1
}

# --- prerequisites ----------------------------------------------------------
need_bin() { # need_bin <name>
    [ -x "$BUILD_DIR/$1" ] && return 0
    printf '%s not found; build it first:\n' "$BUILD_DIR/$1" >&2
    printf '  cmake --build --preset ci-test --target %s\n' "$1" >&2
    exit 1
}

wanted hud && need_bin og_test_view
wanted radar && need_bin og_test_game_core
if wanted abilities || wanted changelog; then
    need_bin og_test_abilities
fi
wanted ui-fx && need_bin og_test_menu_ui
for tool in magick ffprobe python3; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done

# An output directory given relative to where the script was started.
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

cd "$REPO_ROOT"
CFG_BEFORE="$(git status --porcelain -- cfg/ 2> /dev/null || true)"

WORK_DIR="$(mktemp -d)"
KEEP_WORK=0
cleanup() {
    if [ "$KEEP_WORK" -eq 1 ]; then
        printf 'kept the raw frames and logs in %s\n' "$WORK_DIR" >&2
    else
        rm -rf -- "$WORK_DIR"
    fi
}
trap cleanup EXIT

# --- helpers ----------------------------------------------------------------
# run_test <name> <attempts> <binary> <gtest_filter> [VAR=value]...
# Runs one test binary headless with a throwaway config directory; the
# extra VAR=value pairs are the capture variables. Retries a failed run
# (the menu flows are timing-sensitive on a loaded box), then stops.
run_test() {
    local name="$1" attempts="$2" bin="$3" filter="$4" attempt=1
    shift 4
    while :; do
        rm -rf -- "$WORK_DIR/config-$name"
        if env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            SDL_RENDER_DRIVER=software \
            OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
            "$@" \
            "$BUILD_DIR/$bin" --gtest_filter="$filter" \
            > "$WORK_DIR/$name.log" 2>&1; then
            return 0
        fi
        if [ "$attempt" -ge "$attempts" ]; then
            KEEP_WORK=1
            die "capture run $name failed after $attempt attempt(s); see $WORK_DIR/$name.log"
        fi
        printf 'capture run %s failed (attempt %d/%d), retrying\n' \
            "$name" "$attempt" "$attempts" >&2
        attempt=$((attempt + 1))
    done
}

# canvas_png <in.ppm> <out-name>: a 320x200 frame doubled, nearest-neighbour.
canvas_png() {
    [ -s "$1" ] || {
        KEEP_WORK=1
        die "FAIL: expected capture $1 was never written"
    }
    magick "$1" -filter point -resize 200% "$OUT_DIR/$2.png"
    printf 'wrote %s\n' "$OUT_DIR/$2.png"
}

# native_png <in> <out-name>: a physical-size capture, kept at its size.
native_png() {
    [ -s "$1" ] || {
        KEEP_WORK=1
        die "FAIL: expected capture $1 was never written"
    }
    magick "$1" "$OUT_DIR/$2.png"
    printf 'wrote %s\n' "$OUT_DIR/$2.png"
}

SKIPPED=()

# --- steps ------------------------------------------------------------------
if wanted hud; then
    # The HUD script owns its test filter and writes one PPM per scene into
    # <dir>/belt_hud_<scene>/000.ppm (plus its own 3x PNGs, unused here).
    env OPENGLAD_BUILD_DIR="$BUILD_DIR" \
        OPENGLAD_HUD_MEDIA_DIR="$WORK_DIR/hud" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-hud" \
        bash "$REPO_ROOT/scripts/media/capture_ability_select_hud.sh" \
        > "$WORK_DIR/hud.log" 2>&1 || {
        KEEP_WORK=1
        die "the HUD capture failed; see $WORK_DIR/hud.log"
    }
    for scene in 1p_on 1p_off 2p_on 2p_off 4p_on 4p_off 1p_on_shift 4p_on_shift; do
        canvas_png "$WORK_DIR/hud/belt_hud_$scene/000.ppm" \
            "hud-$(printf '%s' "$scene" | tr '_' '-')"
    done
fi

if wanted radar; then
    run_test radar 1 og_test_game_core 'GameLoop.zz_capture_splitscreen_gameplay' \
        OG_FX_CAPTURE_DIR="$WORK_DIR/radar"
    canvas_png "$(printf '%s/radar/splitscreen_gameplay/%03d.ppm' "$WORK_DIR" "$RADAR_FRAME")" \
        hud-2p-radar-on
fi

if wanted abilities; then
    run_test abilities 2 og_test_abilities \
        'AbilitiesFlow.train_door_opens_the_screen_and_the_edit_reaches_the_save:AbilitiesFlow.setting_off_dims_the_train_door:AbilitiesFlow.mage_page_greys_locked_rows_and_the_keyboard_walks_it' \
        UXSHOTS_DIR="$WORK_DIR/abilities"
    for shot in archmage archmage_selected archmage_placed mage train_door_off; do
        canvas_png "$WORK_DIR/abilities/abilities_$shot.ppm" \
            "abilities-$(printf '%s' "$shot" | tr '_' '-')"
    done
fi

if wanted ui-fx; then
    run_test ui-fx 2 og_test_menu_ui 'MenuCapture.zz_capture_menu_effects' \
        OG_FX_CAPTURE_DIR="$WORK_DIR/ui-fx"
    canvas_png "$WORK_DIR/ui-fx/menu_effects_stills/ui_fx.ppm" ui-fx
fi

if wanted changelog; then
    # The CHANGELOG! capture seam: tests named ChangelogFlow.* that write
    # their physical-size frames into $OG_CHANGELOG_SHOT_DIR. Asked of the
    # binary rather than assumed, so this step reports a build without them
    # instead of failing on it.
    listed="$("$BUILD_DIR/og_test_abilities" --gtest_list_tests \
        --gtest_filter='ChangelogFlow.*' 2> /dev/null | grep -c '^  ' || true)"
    if [ "${listed:-0}" -eq 0 ]; then
        SKIPPED+=(changelog)
        printf 'SKIPPED changelog: og_test_abilities has no ChangelogFlow tests yet\n' >&2
    else
        run_test changelog 2 og_test_abilities 'ChangelogFlow.*' \
            OG_CHANGELOG_SHOT_DIR="$WORK_DIR/changelog"
        made=0
        for shot in "$WORK_DIR"/changelog/*.png "$WORK_DIR"/changelog/*.ppm; do
            [ -e "$shot" ] || continue
            base="$(basename "$shot")"
            base="${base%.*}"
            native_png "$shot" "changelog-$(printf '%s' "$base" | tr '_' '-')"
            made=$((made + 1))
        done
        if [ "$made" -eq 0 ]; then
            KEEP_WORK=1
            die "FAIL: ChangelogFlow.* wrote nothing into OG_CHANGELOG_SHOT_DIR; see $WORK_DIR/changelog.log"
        fi
    fi
fi

# --- checks -----------------------------------------------------------------
CFG_AFTER="$(git status --porcelain -- cfg/ 2> /dev/null || true)"
[ "$CFG_BEFORE" = "$CFG_AFTER" ] \
    || die "FAIL: a capture run changed the tracked cfg/ (git status cfg/)"

VERIFY_ARGS=(--set ability-select)
for step in "${STEPS[@]}"; do
    if ! wanted "$step"; then
        VERIFY_ARGS+=(--allow-missing "$step")
    fi
done
for step in "${SKIPPED[@]+"${SKIPPED[@]}"}"; do
    VERIFY_ARGS+=(--allow-missing "$step")
done
python3 "$REPO_ROOT/scripts/media/verify_media.py" "${VERIFY_ARGS[@]}" "$OUT_DIR"

if [ "${#SKIPPED[@]}" -gt 0 ]; then
    printf 'NOTE: skipped %s; rerun once those tests are in the build\n' \
        "${SKIPPED[*]}"
fi
printf 'wrote the New Ability Select stills to %s/\n' "$OUT_DIR"
