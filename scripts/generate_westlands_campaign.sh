#!/usr/bin/env bash
# Regenerate builtin/org.openglad.westlands.glad with tools/westlands_mapgen.
#
# Configures the ci-test preset when needed, builds the (EXCLUDE_FROM_ALL)
# westlands_mapgen target, and runs it from the repo root so the committed
# campaign package is rewritten in place. The tool self-checks the produced
# package (every registered level reloads with the expected rosters and exit
# destinations) and exits nonzero on any validation failure; exits into
# planned-but-not-yet-built levels only warn until the campaign is complete.
#
# Usage: scripts/generate_westlands_campaign.sh [output.glad]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"
OUTPUT="${1:-builtin/org.openglad.westlands.glad}"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target westlands_mapgen -- -j8

"${BUILD_DIR}/westlands_mapgen" "${OUTPUT}"
