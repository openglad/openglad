#!/usr/bin/env bash
# New Ability Select HUD stills: 1, 2 and 4 seats, the setting on and off
# (plus one seat with SHIFTER held), a time freeze and a seat clock running
# so the moved cells show; the hotbar's states (pressed, short MP, locked,
# empty) in two and four seats, box and no box; and the window itself
# (belt_hud_physical/hud-*.ppm): the split-screen hotbar is drawn at half
# size into a plane only the present composites, so the 320x200 canvas
# frames cannot show it and these are read back from the renderer at the
# window's size, over grass with the radar on. The test always runs its
# oracles; OG_FX_CAPTURE_DIR only asks it to write PPM frames, which this
# script turns into PNGs (canvas frames scaled 3x, nearest neighbour; the
# window frames at their own size) for review.
#
# Usage:
#   nix develop --command bash scripts/media/capture_ability_select_hud.sh
#   OPENGLAD_BUILD_DIR=build/ci-test OPENGLAD_HUD_MEDIA_DIR=build/media/ability-select-hud \
#     scripts/media/capture_ability_select_hud.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"
MEDIA_DIR="${OPENGLAD_HUD_MEDIA_DIR:-$REPO_ROOT/build/media/ability-select-hud}"
TEST_BIN="$BUILD_DIR/og_test_view"
cd "$REPO_ROOT"

if [ ! -x "$TEST_BIN" ]; then
    printf 'test binary not found at %s\n' "$TEST_BIN" >&2
    printf 'build it with: cmake --build --preset ci-test --target og_test_view\n' >&2
    exit 1
fi

# magick comes from the dev shell (`nix develop`).
command -v magick > /dev/null || {
    printf 'magick not found; run inside nix develop\n' >&2
    exit 1
}

mkdir -p "$MEDIA_DIR"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    OG_FX_CAPTURE_DIR="$MEDIA_DIR" \
    "$TEST_BIN" --gtest_filter='GladHud.zz_capture_belt_hud:GladHud.zz_capture_belt_hud_states:GladHud.zz_capture_belt_hud_physical'

for ppm in "$MEDIA_DIR"/belt_hud_*/000.ppm; do
    scene="$(basename "$(dirname "$ppm")")"
    magick "$ppm" -filter point -resize 300% "$MEDIA_DIR/$scene.png"
done
for ppm in "$MEDIA_DIR"/belt_hud_physical/*.ppm; do
    magick "$ppm" "$MEDIA_DIR/$(basename "$ppm" .ppm).png"
done
printf 'wrote New Ability Select HUD stills to %s/\n' "$MEDIA_DIR"
