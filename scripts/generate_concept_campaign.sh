#!/usr/bin/env bash
# Regenerate builtin/org.openglad.concept.glad with tools/concept_mapgen.
#
# Configures the ci-test preset when needed, builds the (EXCLUDE_FROM_ALL)
# concept_mapgen target, and runs it from the repo root so the committed
# campaign package is rewritten in place. The tool self-checks the produced
# package (every level reloads with the expected floor count) and exits nonzero
# on any validation failure.
#
# Usage: scripts/generate_concept_campaign.sh [output.glad]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"
OUTPUT="${1:-builtin/org.openglad.concept.glad}"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target concept_mapgen -- -j8

"${BUILD_DIR}/concept_mapgen" "${OUTPUT}"
