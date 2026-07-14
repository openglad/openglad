#!/usr/bin/env bash
# Regenerate builtin/org.openglad.tower.glad with tools/tower_mapgen.
#
# The package is Gate-ONLY (campaign.yaml with `mode: tower`, icon.png,
# scen700 "The Gate" + its grid PNG) — generated floors are written to the
# user path at run time and must NEVER ship in the package (mounted
# campaigns are prepended and would shadow/freeze a run; the tool and a
# unit test both enforce the member list).
#
# Configures the ci-test preset when needed, builds the (EXCLUDE_FROM_ALL)
# tower_mapgen target, and runs it from the repo root so the committed
# campaign package is rewritten in place. The tool self-checks the produced
# package (the Gate reloads with its exit, markers, type bits and budgets)
# and exits nonzero on any validation failure.
#
# Usage: scripts/generate_tower_campaign.sh [output.glad]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"
OUTPUT="${1:-builtin/org.openglad.tower.glad}"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target tower_mapgen -- -j8

"${BUILD_DIR}/tower_mapgen" "${OUTPUT}"
