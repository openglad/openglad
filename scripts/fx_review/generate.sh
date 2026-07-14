#!/usr/bin/env bash
# Regenerate the FX review site from scratch: build, run every env-gated
# capture test, encode the APNG site. Frames and site are build artifacts —
# they are rendered dynamically, never committed.
#
# Usage: scripts/fx_review/generate.sh [output-dir]
#   output-dir defaults to build/fx-review; the site lands in
#   <output-dir>/site. Serve it with:
#     python3 -m http.server -d <output-dir>/site 8790
#
# Iterating on one Westlands level: OG_FX_CAPTURE_ONLY=<level id> limits
# GameLoop.zz_capture_epic_battles / zz_capture_westlands to that scene.
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=${1:-build/fx-review}
ANIM="$OUT/anim"
SITE="$OUT/site"
BUILD=build/ci-test
mkdir -p "$ANIM"

cmake --preset ci-test
cmake --build --preset ci-test --target og_test_rendering og_test_game_core og_test_menu_ui

# The capture flows overwrite save/save0.gtl (they build their own rosters);
# preserve the real save around the run.
SAVE_BACKUP=""
if [ -f save/save0.gtl ]; then
    SAVE_BACKUP=$(mktemp)
    cp save/save0.gtl "$SAVE_BACKUP"
fi
restore_save() {
    if [ -n "$SAVE_BACKUP" ]; then
        cp "$SAVE_BACKUP" save/save0.gtl
        rm -f "$SAVE_BACKUP"
    fi
}
trap restore_save EXIT

echo "=== effect close-ups ==="
OG_FX_CAPTURE_DIR="$PWD/$ANIM" "$BUILD/og_test_rendering" \
    --gtest_filter='RenderEffects.zz_capture_effect_scenes'

echo "=== live gameplay + split-screen + epic battles ==="
OG_FX_CAPTURE_DIR="$PWD/$ANIM" ${OG_FX_CAPTURE_ONLY:+OG_FX_CAPTURE_ONLY="$OG_FX_CAPTURE_ONLY"} \
    "$BUILD/og_test_game_core" --gtest_filter='GameLoop.zz_capture_*'

echo "=== menu tours ==="
OG_FX_CAPTURE_DIR="$PWD/$ANIM" "$BUILD/og_test_menu_ui" \
    --gtest_filter='OptionsMenu.zz_capture_*'

echo "=== encoding site ==="
python3 scripts/fx_review/make_site.py --anim "$ANIM" --site "$SITE"

echo
echo "Done. Serve with: python3 -m http.server -d '$SITE' 8790"
