#!/usr/bin/env bash
# Regenerate the campaign-scripting (issue #206 + friends) PR media from
# headless runs. Output lands in build/media/ (gitignored); finished
# artifacts get committed to the openglad/openglad-screenshots repo, never
# to this one.
#
# Two kinds of capture:
#   1. openglad_demo gameplay runs with OPENGLAD_DEMO_CAMPAIGN_STATE
#      seeding the decision store, so og.campaign_var consequences are on
#      camera: the Watch at the Deeping Wall, the Gold-Wraiths on the
#      Great River, the Pilgrim at the Grey Ships, the Collectors on the
#      Long Toll, the Carried at the Warm Mint, and a basketball arena
#      with the pinging ball + per-team HUD row.
#   2. Base Camp zone stills dumped by the zone UI integration test's
#      UXSHOTS_DIR seam (the scripted composition, the zone submenu, and
#      the default zone across campaigns).
#
# Usage: scripts/media/capture_campaign_scripting.sh [output-dir]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${OPENGLAD_BUILD:-$REPO_ROOT/build/ci-test}"
DEMO_BIN="${OPENGLAD_DEMO:-$BUILD/openglad_demo}"
OUT_DIR="${1:-$REPO_ROOT/build/media/campaign-scripting}"

for bin in "$DEMO_BIN" "$BUILD/og_test_matchup"; do
    if [ ! -x "$bin" ]; then
        printf '%s not found; build the ci-test preset first\n' "$bin" >&2
        exit 1
    fi
done
for tool in ffmpeg ffprobe; do
    command -v "$tool" > /dev/null || {
        printf '%s not found; enter the dev shell first: nix develop\n' "$tool" >&2
        exit 1; }
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "$OUT_DIR"

# BMP frame dir -> looping GIF via the two-pass palette chain.
frames_to_gif() {
    local dir="$1" out="$2" fps="${3:-12}"
    ffmpeg -loglevel error -y -framerate "$fps" -pattern_type glob \
        -i "$dir/*.bmp" -vf "palettegen=stats_mode=diff" "$WORK_DIR/pal.png"
    ffmpeg -loglevel error -y -framerate "$fps" -pattern_type glob \
        -i "$dir/*.bmp" -i "$WORK_DIR/pal.png" \
        -lavfi "paletteuse=dither=bayer:bayer_scale=4" -loop 0 "$out"
}

# One seeded consequence run: campaign, scenario, campaign-state, frames.
demo_capture() {
    local name="$1" campaign="$2" scen="$3" state="$4" frames="${5:-420}" focus="${6:-player}"
    local dir="$WORK_DIR/$name"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_SEED=1337 \
        OPENGLAD_DEMO_CAMPAIGN="$campaign" \
        OPENGLAD_DEMO_SCENARIOS="$scen" \
        OPENGLAD_DEMO_TEAM_SIZE=8 \
        OPENGLAD_DEMO_CAMPAIGN_STATE="$state" \
        OPENGLAD_DEMO_CAPTURE_DIR="$dir" \
        OPENGLAD_DEMO_CAPTURE_LIMIT="$frames" \
        OPENGLAD_DEMO_CAPTURE_FOCUS="$focus" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$DEMO_BIN" > "$WORK_DIR/$name.log" 2>&1
    frames_to_gif "$dir" "$OUT_DIR/$name.gif"
    printf 'wrote %s\n' "$OUT_DIR/$name.gif"
}

demo_capture watch-at-the-wall     westlands  15  watch_paid=900 420 boss
demo_capture wraiths-on-the-river  westlands  11  delve_counted=1 420 boss
demo_capture pilgrim-at-the-ships  westlands  26  watch_paid=900,sneak_bread=1 360
demo_capture collectors-long-toll  longseason 14  advance_debt=900
demo_capture carried-at-the-mint   longseason 18  coin_kept=87380
demo_capture basketball-ping-hud   modes      824 "" 480

# Base Camp zone stills: the CampaignZoneUi flows dump one PPM per state
# when UXSHOTS_DIR is set — the scripted composition, the zone submenu,
# and the default zone across campaigns. Every name below is a capture
# point in tests/integration/test_campaign_zone_ui.cpp; the test asserts
# each frame is non-blank and each file landed, and the loop at the bottom
# refuses to finish with any of them missing.
ZONE_SHOTS=(
    zone_scripted_camp
    zone_submenu_stores
    zone_default_camp
    zone_default_gladiator
    zone_default_modes
    zone_camp_westlands
    zone_default_longseason
    zone_default_imaginations
    uxr_after_bench
    uxr_lock_toast
    uxr_assign_war
    uxr_assign_burden
    uxr_level_fail_toast
    uxr_kit_toast
    uxr_kit_done_toast
    uxr_level_current
    uxr_submenu_after_buy
    uxr_submenu_level_fail
    uxr_back_at_root
    uxr_big_roster_p1
    uxr_big_roster_p2
)

UXDIR="$WORK_DIR/uxshots"
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy UXSHOTS_DIR="$UXDIR" \
    "$BUILD/og_test_matchup" \
    --gtest_filter='CampaignZoneUi.*' \
    > "$WORK_DIR/uxshots.log" 2>&1 || {
        printf 'zone capture run failed; see %s\n' "$WORK_DIR/uxshots.log" >&2
        exit 1; }
for shot in "$UXDIR"/*.ppm; do
    [ -e "$shot" ] || continue
    name="$(basename "$shot" .ppm)"
    ffmpeg -loglevel error -y -i "$shot" \
        -vf "scale=640:400:flags=neighbor" "$OUT_DIR/$name.png"
    printf 'wrote %s\n' "$OUT_DIR/$name.png"
done

# Sanity: every artifact decodes and carries frames.
for f in "$OUT_DIR"/*.gif; do
    frames="$(ffprobe -loglevel error -count_frames \
        -show_entries stream=nb_read_frames -of csv=p=0 "$f")"
    printf '%s: %s frames\n' "$(basename "$f")" "$frames"
    [ "${frames:-0}" -ge 2 ] || { printf 'FAIL: %s has no motion\n' "$f" >&2; exit 1; }
done

# Sanity: every expected still is actually there and decodes at the right
# size. A capture flow that stopped reaching one of its frames used to leave
# this script silently one PNG short — the missing artifact was only ever
# noticed at PR-writing time.
missing=0
for name in "${ZONE_SHOTS[@]}"; do
    png="$OUT_DIR/$name.png"
    if [ ! -s "$png" ]; then
        printf 'FAIL: expected still %s was never produced\n' "$png" >&2
        missing=1
        continue
    fi
    dims="$(ffprobe -loglevel error -select_streams v:0 \
        -show_entries stream=width,height -of csv=p=0:s=x "$png")"
    printf '%s: %s\n' "$(basename "$png")" "$dims"
    [ "$dims" = "640x400" ] || {
        printf 'FAIL: %s decoded as %s, expected 640x400\n' "$png" "$dims" >&2
        missing=1; }
done
[ "$missing" -eq 0 ] || exit 1
