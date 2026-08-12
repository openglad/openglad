#!/usr/bin/env bash
# Regenerate campaigns/imaginations/ with tools/imaginations_mapgen.
#
# Configures the ci-test preset when needed, builds the (EXCLUDE_FROM_ALL)
# imaginations_mapgen target, and runs it from the repo root so the
# committed campaign source tree is rewritten in place (the shipped .glad
# is composed from that one tree by the build; the hand-authored README.md
# is preserved, never rewritten; regeneration reviews as plain file
# diffs). The tool self-checks the produced package (army counts, text
# budgets, exit destinations, and the og::mapgen footing/reachability/
# spawn-egress audits) and exits nonzero on any validation failure.
#
# Usage: scripts/generate_imaginations_campaign.sh [output-dir]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"
OUTPUT="${1:-campaigns/imaginations}"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target imaginations_mapgen -- -j8

"${BUILD_DIR}/imaginations_mapgen" "${OUTPUT}"

# Recompose the staged archive from the freshly written source tree. The
# test binaries and the playtest harnesses read the STAGED archive under
# build/ci-test/builtin/ (composed from campaigns/ by og_builtin_campaigns),
# which the build system only refreshes on the next build -- without this,
# every harness run after a regen exercised the PREVIOUS generation's
# package (a one-generation-stale race).
cmake --build "${BUILD_DIR}" --target og_builtin_campaigns -- -j8
