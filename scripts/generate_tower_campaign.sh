#!/usr/bin/env bash
# Regenerate campaigns/tower/ with tools/tower_mapgen.
#
# The package is Gate-ONLY (campaign.yaml with `mode: tower`, icon.png,
# scen700 "The Gate" + its grid PNG) — generated floors are written to the
# user path at run time and must NEVER ship in the package (mounted
# campaigns are prepended and would shadow/freeze a run; the tool and a
# unit test both enforce the member list).
#
# Configures the ci-test preset when needed, builds the (EXCLUDE_FROM_ALL)
# tower_mapgen target, and runs it from the repo root so the committed
# campaign source tree is rewritten in place (the shipped .glad is composed
# from that tree by the build). The tool self-checks the produced
# package (the Gate reloads with its exit, markers, type bits and budgets)
# and exits nonzero on any validation failure.
#
# Usage: scripts/generate_tower_campaign.sh [output-dir]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"
OUTPUT="${1:-campaigns/tower}"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target tower_mapgen -- -j8

"${BUILD_DIR}/tower_mapgen" "${OUTPUT}"

# Recompose the staged archive from the freshly written source tree. The
# test binaries and the playtest harnesses read the STAGED archive under
# build/ci-test/builtin/ (composed from campaigns/ by og_builtin_campaigns),
# which the build system only refreshes on the next build -- without this,
# every harness run after a regen exercised the PREVIOUS generation's
# package (a one-generation-stale race).
cmake --build "${BUILD_DIR}" --target og_builtin_campaigns -- -j8
