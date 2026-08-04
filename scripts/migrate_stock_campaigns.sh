#!/usr/bin/env bash
# Migrate the two hand-authored stock campaigns (gladiator, tryxian) to the
# BASE + DECOR tile layering (.fss v11) with tools/grid_migrate.
#
# grid_migrate proves equivalence in-process (per-cell passability across the
# five mover archetypes, concealment, damage semantics, door-frame genre,
# entity-stream identity, footing) and exits nonzero on ANY mismatch, so a
# successful run is the proof obligation, not just a conversion. Weather
# outdoor-vote changes are printed per level (report-only).
#
# The generated campaigns (modes, concept, westlands, longseason, tower) are
# NOT migrated here — they regenerate from their mapgen tools
# (scripts/generate_*.sh), which author decor planes directly.
#
# One-shot by design: the committed campaigns are already .fss v11, and
# grid_migrate's equivalence prover treats pre-existing decor in its INPUT
# as a verification failure (it expects legacy combined tiles), so re-running
# this script on migrated data fails loudly before publishing anything. It
# exists for the next legacy hand-authored campaign, not for repetition.
#
# The tool reads its input from the staged archives next to the binary
# (build/ci-test/builtin, composed from campaigns/ by og_builtin_campaigns),
# so the stage_runtime_assets dependency keeps the input in sync with the
# repo; outputs land in the committed campaigns/<id>/ source trees and the
# script recomposes the staged archives afterwards so tests see the
# migrated campaigns.
#
# Usage: scripts/migrate_stock_campaigns.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target grid_migrate -- -j8

for id in org.openglad.gladiator org.openglad.tryxian; do
    "${BUILD_DIR}/grid_migrate" "${id}" "campaigns/${id}"
done

# Recompose the staged archives so tests read the migrated campaigns.
cmake --build "${BUILD_DIR}" --target og_builtin_campaigns -- -j8
