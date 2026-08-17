#!/usr/bin/env bash
# Regenerate the campaign-scripting (issue #206 + friends) PR media from
# headless runs. Output lands in build/media/ (gitignored); finished
# artifacts get committed to the openglad/openglad-screenshots repo, never
# to this one.
#
# Three kinds of capture:
#   1. openglad_demo gameplay runs with OPENGLAD_DEMO_CAMPAIGN_STATE
#      seeding the decision store, so og.campaign_var consequences are on
#      camera: the Watch at the Deeping Wall, the Gold-Wraiths on the
#      Great River, the Pilgrim at the Grey Ships, the Collectors on the
#      Long Toll, the Carried at the Warm Mint, and a basketball arena
#      with the pinging ball + per-team HUD row.
#   2. Base Camp zone stills dumped by the zone UI integration test's
#      UXSHOTS_DIR seam (the scripted composition, the zone submenu, and
#      the default zone across campaigns).
#   3. Menu stills from the uxshots probe's own flows (og_test_basecamp) —
#      the two main-menu variants and the Base Camp a company with a real
#      roster sees. The zone stills carry the command strip too, but the
#      probe is where the main menu is reachable, and the strip's own
#      still should not depend on a campaign happening to script a camp.
#   4. Ownership overlays drawn over two of those stills, showing which
#      rectangles a campaign's Lua composes and which belong to the engine.
#
# Usage: scripts/media/capture_campaign_scripting.sh [output-dir]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${OPENGLAD_BUILD:-$REPO_ROOT/build/ci-test}"
DEMO_BIN="${OPENGLAD_DEMO:-$BUILD/openglad_demo}"
OUT_DIR="${1:-$REPO_ROOT/build/media/campaign-scripting}"

for bin in "$DEMO_BIN" "$BUILD/og_test_matchup" "$BUILD/og_test_basecamp"; do
    if [ ! -x "$bin" ]; then
        printf '%s not found; build the ci-test preset first\n' "$bin" >&2
        exit 1
    fi
done
for tool in ffmpeg ffprobe magick; do
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
    zone_submenu_match_setup
    zone_default_camp
    zone_default_gladiator
    zone_default_modes
    zone_camp_westlands
    zone_camp_westlands_fork
    zone_camp_longseason
    zone_default_imaginations
    uxr_match_setup_cycled
    uxr_docket_pager_page1
    uxr_docket_pager_page2
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

# The menu stills the zone tour cannot reach: the main menu itself (the
# GAME SETTINGS / CLOUD SAVES pair that replaced the SETTINGS heading and
# its Difficulty row) and the Base Camp command strip under a real roster
# (the DIFFICULTY door, and the GO the re-grid narrowed to pay for it).
# Same PPM seam, a different test binary — the flows live with the rest of
# the uxshots probe in og_test_basecamp.
MENU_SHOTS=(
    mainmenu_no_company
    mainmenu_with_company
    basecamp_solo
)

MENUDIR="$WORK_DIR/menushots"
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy UXSHOTS_DIR="$MENUDIR" \
    "$BUILD/og_test_basecamp" \
    --gtest_filter='UxShots.a_mainmenu_no_company:UxShots.b_mainmenu_with_company:UxShots.g_basecamp_solo' \
    > "$WORK_DIR/menushots.log" 2>&1 || {
        printf 'menu capture run failed; see %s\n' "$WORK_DIR/menushots.log" >&2
        exit 1; }
for name in "${MENU_SHOTS[@]}"; do
    shot="$MENUDIR/$name.ppm"
    [ -s "$shot" ] || continue
    ffmpeg -loglevel error -y -i "$shot" \
        -vf "scale=640:400:flags=neighbor" "$OUT_DIR/$name.png"
    printf 'wrote %s\n' "$OUT_DIR/$name.png"
done

# The ownership overlays: the Base Camp is one screen with two authors, and
# a bare screenshot cannot show the seam. The generator paints a blue fill
# over every rectangle a campaign's Lua composes and an orange outline
# around the engine chrome, then writes a numbered legend under the picture.
# It reads the camp and submenu stills produced immediately above, so it has
# to run after both loops.
OVERLAY_SHOTS=(
    lua_ownership_basecamp
    lua_ownership_submenu
)

python3 "$REPO_ROOT/scripts/media/make_lua_ownership_overlays.py" "$OUT_DIR"

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
for name in "${ZONE_SHOTS[@]}" "${MENU_SHOTS[@]}"; do
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

# The overlays are the still plus a legend strip, so only the width is
# fixed: 640px is what makes GitHub render them 1:1 instead of resampling
# the game's pixels.
for name in "${OVERLAY_SHOTS[@]}"; do
    png="$OUT_DIR/$name.png"
    if [ ! -s "$png" ]; then
        printf 'FAIL: expected overlay %s was never produced\n' "$png" >&2
        missing=1
        continue
    fi
    dims="$(ffprobe -loglevel error -select_streams v:0 \
        -show_entries stream=width,height -of csv=p=0:s=x "$png")"
    printf '%s: %s\n' "$(basename "$png")" "$dims"
    case "$dims" in
        640x*) ;;
        *) printf 'FAIL: %s decoded as %s, expected 640 wide\n' "$png" "$dims" >&2
           missing=1;;
    esac
done
[ "$missing" -eq 0 ] || exit 1
