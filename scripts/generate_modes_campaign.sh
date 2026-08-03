#!/usr/bin/env bash
# Regenerate builtin/org.openglad.modes.glad with tools/modes_mapgen.
#
# Configures the ci-test preset when needed, builds the (EXCLUDE_FROM_ALL)
# modes_mapgen target, and runs it from the repo root so the committed
# campaign package is rewritten in place. The tool also regenerates the
# committed level manifest tools/modes_mapgen/pack/lib/mode_levels.lua and
# fails when the committed copy was stale (rerun after it rewrites it).
# The tool self-checks the produced package and exits nonzero on any
# validation failure.
#
# Usage: scripts/generate_modes_campaign.sh [output.glad]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"
OUTPUT="${1:-builtin/org.openglad.modes.glad}"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target modes_mapgen -- -j8

"${BUILD_DIR}/modes_mapgen" "${OUTPUT}"
