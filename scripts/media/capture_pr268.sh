#!/usr/bin/env bash
# Issue #268 proof capture. The test always runs its text and pixel oracles;
# OG_FX_CAPTURE_DIR only asks the existing renderer seam to write two PPM
# frames (soldier PATH BLOCKED, cleric NO CORPSE NEARBY).
#
# Usage:
#   nix develop --command bash scripts/media/capture_pr268.sh
#   OPENGLAD_BUILD_DIR=build/ci-test OPENGLAD_PR268_MEDIA_DIR=build/media/pr-268 \
#     scripts/media/capture_pr268.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"
MEDIA_DIR="${OPENGLAD_PR268_MEDIA_DIR:-$REPO_ROOT/build/media/pr-268}"
TEST_BIN="$BUILD_DIR/og_test_rendering"
cd "$REPO_ROOT"

if [ ! -x "$TEST_BIN" ]; then
    printf 'test binary not found at %s\n' "$TEST_BIN" >&2
    printf 'build it with: cmake --build --preset ci-test --target og_test_rendering\n' >&2
    exit 1
fi

mkdir -p "$MEDIA_DIR"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    OG_FX_CAPTURE_DIR="$MEDIA_DIR" \
    "$TEST_BIN" \
      --gtest_filter='RenderEffects.special_failure_reason_reaches_player_hud'

printf 'wrote issue #268 frames to %s/special_failure_reasons/\n' "$MEDIA_DIR"
